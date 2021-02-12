#include "common.h"
#include "NativeApp.hh"
#include "tiles.hh"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include "common.h"
#include "NativeApp.hh"
#include "tiles.hh"
#include <bzlib.h>
#include <zlib.h>

#define BZBUFFER 65536
#define GZBUFFER 32768


static char buffer[BZBUFFER+1];

extern char *color_file;

extern int bzerror, bzbuf_empty, gzerr, gzbuf_empty;

extern long bzbuf_pointer, bzbytes_read, gzbuf_pointer, gzbytes_read;

extern double antenna_rotation,antenna_downtilt,antenna_dt_direction;


int loadClutter(char *filename, double radius, struct site tx)
{
	/* This function reads a MODIS 17-class clutter file in ASCII Grid format.
	   The nominal heights it applies to each value, eg. 5 (Mixed forest) = 15m are 
	   taken from ITU-R P.452-11.
	   It doesn't have it's own matrix, instead it boosts the DEM matrix like point clutter
	   AddElevation(lat, lon, height);
 	   If tiles are standard 2880 x 3840 then cellsize is constant at 0.004166
	 */
	int x, y, z, h = 0, w = 0;
	double clh, xll, yll, cellsize, cellsize2, xOffset, yOffset, lat, lon;
	char line[100000];
	char *s, *pch = NULL;
	FILE *fd;

	if ((fd = fopen(filename, "rb")) == NULL)
		return errno;

	if (fgets(line, 19, fd) != NULL) {
		pch = strtok(line," ");
		pch = strtok(NULL, " ");
		w = atoi(pch);
	}

	if (fgets(line, 19, fd) != NULL) {
		pch = strtok(line," ");
		pch = strtok(NULL, " ");
		h = atoi(pch);
	}

	if (w==2880 && h==3840) {
		cellsize=0.004167;
		cellsize2 = cellsize * 3;
	} else {
		if (debug) {
			fprintf(stderr, "\nError Loading clutter file, unsupported resolution %d x %d.\n", w,h);
			fflush(stderr);
		}
		return 0; // can't work with this yet
	}

	if (debug) {
	        fprintf(stderr, "\nLoading clutter file \"%s\" %d x %d...\n", filename, w,h);
		fflush(stderr);
	}

	if (fgets(line, 25, fd) != NULL) {
		sscanf(pch, "%lf", &xll);
	}

	s = fgets(line, 25, fd);
	if (fgets(line, 25, fd) != NULL) {
		sscanf(pch, "%lf", &yll);
	}

	if (debug) {
		fprintf(stderr, "\nxll %.2f yll %.2f\n", xll, yll);
		fflush(stderr);
	}

	s = fgets(line, 25, fd); // cellsize

	if (s)
	  ;

	//loop over matrix
	for (y = h; y > 0; y--) {
		x = 0;
		if (fgets(line, sizeof(line)-1, fd) != NULL) {
			pch = strtok(line, " ");
			while (pch != NULL && x < w) {
				z = atoi(pch);
				// Apply ITU-R P.452-11
				// Treat classes 0, 9, 10, 11, 15, 16 as water, (Water, savanna, grassland, wetland, snow, barren)
				clh = 0.0;

				// evergreen, evergreen, urban
				if(z == 1 || z == 2 || z == 13)
					clh = 20.0;
				// deciduous, deciduous, mixed
				if(z==3 || z==4 || z==5)
					clh = 15.0;
				// woody shrublands & savannas
				if(z==6 || z==8)
					clh = 4.0;
				// shrublands, savannas, croplands...
				if(z==7 || z==9 || z==10 || z==12 || z==14)
					clh = 2.0;

				if(clh>1){
					xOffset=x*cellsize; // 12 deg wide
					yOffset=y*cellsize; // 16 deg high

					// make all longitudes positive 
					if(xll+xOffset>0){
						lon=360-(xll+xOffset);
					}else{
						lon=(xll+xOffset)*-1;
					}
					lat = yll+yOffset;

					// bounding box
					if(lat > tx.lat - radius && lat < tx.lat + radius && lon > tx.lon - radius && lon < tx.lon + radius){
						// not in near field
						if((lat > tx.lat+cellsize2 || lat < tx.lat-cellsize2) || (lon > tx.lon + cellsize2 || lon < tx.lon - cellsize2)){
							AddElevation(lat,lon,clh,2);
						}

					}
				}

				x++;
				pch = strtok(NULL, " ");
			}//while
		} else {
			fprintf(stderr, "Clutter error @ x %d y %d\n", x, y);
		}//if
	}//for

	fclose(fd);
	return 0;
}

int averageHeight(int height, int width, int x, int y){
	int total = 0;
	int c=0;
	if(dem[0].data[y-1][x-1]>0){
		total+=dem[0].data[y-1][x-1];
		c++;
	}
	if(dem[0].data[y+1][x+1]>0){
		total+=dem[0].data[y+1][x+1];
		c++;
	}
	if(dem[0].data[y-1][x+1]>0){
		total+=dem[0].data[y-1][x+1];
		c++;
	}
	if(dem[0].data[y+1][x-1]>0){
		total+=dem[0].data[y+1][x-1];
		c++;
	}

	if(c>0){
		return (int)(total/c);
	}else{
		return 0;
	}
}

int loadLIDAR(char *filenames, int resample)
{
	char *filename;
	char *files[900]; // 20x20=400, 16x16=256 tiles
	int indx = 0, fc = 0, success;
	double avgCellsize = 0, smCellsize = 0;
	tile_t *tiles;

	// Initialize global variables before processing files
	min_west = 361; // any value will be lower than this
	max_west = 0;   // any value will be higher than this

	// test for multiple files
	filename = strtok(filenames, " ,");
	while (filename != NULL) {
		files[fc] = filename;
		filename = strtok(NULL, " ,");	
		fc++;
	}

	/* Allocate the tile array */
	if( (tiles = (tile_t*) calloc(fc+1, sizeof(tile_t))) == NULL ) {
		if (debug)
			fprintf(stderr,"Could not allocate %d\n tiles",fc+1);
		return ENOMEM;
	}

	/* Load each tile in turn */
	for (indx = 0; indx < fc; indx++) {

		/* Grab the tile metadata */
		if( (success = tile_load_lidar(&tiles[indx], files[indx])) != 0 ){
			fprintf(stderr,"Failed to load LIDAR tile %s\n",files[indx]);
			fflush(stderr);
			free(tiles);
			return success;
		}

		if (debug) {
			fprintf(stderr, "Loading \"%s\" into page %d with width %d...\n", files[indx], indx, tiles[indx].width);
			fflush(stderr);
		}

		// Increase the "average" cell size
		avgCellsize += tiles[indx].cellsize;
		// Update the smallest cell size
		if (smCellsize == 0 || tiles[indx].cellsize < smCellsize) {
			smCellsize = tiles[indx].cellsize;
		}

		// Update a bunch of globals
		if (tiles[indx].max_el > max_elevation)
			max_elevation = tiles[indx].max_el; 
		if (tiles[indx].min_el < min_elevation)
			min_elevation = tiles[indx].min_el;

		if (max_north == -90 || tiles[indx].max_north > max_north)
			max_north = tiles[indx].max_north;

		if (min_north == 90 || tiles[indx].min_north < min_north)
			min_north = tiles[indx].min_north;

		//Meridian switch. max_west=0
		if (abs(tiles[indx].max_west - max_west) < 180 || tiles[indx].max_west < 360) {
		        if (tiles[indx].max_west > max_west)
			        max_west = tiles[indx].max_west; // update highest value
		} else {
		        if (tiles[indx].max_west < max_west)
			        max_west = tiles[indx].max_west;
		}
		if (fabs(tiles[indx].min_west - min_west) < 180.0 || tiles[indx].min_west <= 360) {
			if (tiles[indx].min_west < min_west)
				min_west = tiles[indx].min_west;
		} else {
			if (tiles[indx].min_west > min_west)
				min_west = tiles[indx].min_west;
		}
		// Handle tile with 360 XUR
		if(min_west>359) min_west=0.0;
	}

	/* Iterate through all of the tiles to find the smallest resolution. We will
	 * need to rescale every tile from here on out to this value */
	float smallest_res = 0;
	for (size_t i = 0; i < (unsigned)fc; i++) {
		if ( smallest_res == 0 || tiles[i].resolution < smallest_res ){
			smallest_res = tiles[i].resolution;
		}
	}

	/* Now we need to rescale all tiles the the lowest resolution or the requested resolution. ie if we have
	 * one 1m lidar and one 2m lidar, resize the 2m to fake 1m */
	float desired_resolution = resample != 0 && smallest_res < resample ? resample : smallest_res;

	if(resample>1){
		desired_resolution=smallest_res*resample;
	}

	// Don't resize large 1 deg tiles in large multi-degree plots as it gets messy
	if(tiles[0].width != 3600){

	  for (size_t i = 0; i < (unsigned)fc; i++) {
			float rescale = tiles[i].resolution / (float)desired_resolution;
			if(debug) {
				fprintf(stderr,"res %.5f desired_res %.5f\n",tiles[i].resolution,(float)desired_resolution);
				fflush(stderr);
			}
			if (rescale != 1){
				if( (success = tile_rescale(&tiles[i], rescale) != 0 ) ){
					fprintf(stderr, "Error resampling tiles\n");
					fflush(stderr);
					return success;
				}
			}
		}

	}

	/* Now we work out the size of the giant lidar tile. */
	if(debug){
		fprintf(stderr,"mw:%lf Mnw:%lf\n", max_west, min_west);
	}
	double total_width = max_west - min_west >= 0 ? max_west - min_west : max_west + (360 - min_west);
	double total_height = max_north - min_north;
	if (debug) {
		fprintf(stderr,"totalh: %.7f - %.7f = %.7f totalw: %.7f - %.7f = %.7f fc: %d\n", max_north, min_north, total_height, max_west, min_west, total_width,fc);
	}

	//detect problematic layouts eg. vertical rectangles
	// 1x2
	if(fc >= 2 && desired_resolution < 28 && total_height > total_width*1.5){
		tiles[fc].max_north=max_north;
		tiles[fc].min_north=min_north;
		westoffset=westoffset-(total_height-total_width); // WGS84 for stdout only
		max_west=max_west+(total_height-total_width); // Positive westing
		tiles[fc].max_west=max_west; // Positive westing
		tiles[fc].min_west=max_west;
		tiles[fc].ppdy=tiles[fc-1].ppdy;
		tiles[fc].ppdy=tiles[fc-1].ppdx;
		tiles[fc].width=(total_height-total_width);
		tiles[fc].height=total_height;
		tiles[fc].data=tiles[fc-1].data;
		fc++;

		//calculate deficit

		if (debug) {
		        fprintf(stderr,"deficit: %.4f cellsize: %.9f tiles needed to square: %.1f, desired_resolution %f\n", total_width-total_height, avgCellsize, (total_width-total_height)/avgCellsize, (float)desired_resolution);
			fflush (stderr);
		}
	}
	// 2x1
	if (fc >= 2 && desired_resolution < 28 && total_width > total_height*1.5) {
		tiles[fc].max_north=max_north+(total_width-total_height);
		tiles[fc].min_north=max_north;
		tiles[fc].max_west=max_west; // Positive westing
		max_north=max_north+(total_width-total_height); // Positive westing
		tiles[fc].min_west=min_west;
		tiles[fc].ppdy=tiles[fc-1].ppdy;
		tiles[fc].ppdy=tiles[fc-1].ppdx;
		tiles[fc].width=total_width; 
		tiles[fc].height=(total_width-total_height);
		tiles[fc].data=tiles[fc-1].data;
		fc++;

		//calculate deficit

		if (debug) {
			fprintf(stderr,"deficit: %.4f cellsize: %.9f tiles needed to square: %.1f\n", total_width-total_height,avgCellsize,(total_width-total_height)/avgCellsize);
			fflush(stdout);
		}
	}
	size_t new_height = 0;
	size_t new_width = 0;
	for ( size_t i = 0; i < (unsigned)fc; i++ ) {
		double north_offset = max_north - tiles[i].max_north;
		double west_offset = max_west - tiles[i].max_west >= 0 ? max_west - tiles[i].max_west : max_west + (360 - tiles[i].max_west);
		size_t north_pixel_offset = north_offset * tiles[i].ppdy;
		size_t west_pixel_offset = west_offset * tiles[i].ppdx;

		if ( west_pixel_offset + tiles[i].width > new_width )
			new_width = west_pixel_offset + tiles[i].width;
		if ( north_pixel_offset + tiles[i].height > new_height )
			new_height = north_pixel_offset + tiles[i].height;
		if (debug)
			fprintf(stderr,"north_pixel_offset %zu west_pixel_offset %zu, %zu x %zu\n", north_pixel_offset, west_pixel_offset,new_height,new_width);

      		//sanity check!
        	if (new_width > 39e3 || new_height > 39e3) {
                	fprintf(stdout,"Not processing a tile with these dimensions: %zu x %zu\n",new_width,new_height);
//                	exit(1);
        	        return 1;
        	}
	}

	size_t new_tile_alloc = new_width * new_height;
	short * new_tile = (short*) calloc( new_tile_alloc, sizeof(short) );

	if ( new_tile == NULL ) {
	        if (debug) {
			fprintf(stderr,"Could not allocate %zu bytes\n", new_tile_alloc);
			fflush(stderr);
		}
		free(tiles);
		return ENOMEM;
	}
	if (debug) {
		fprintf(stderr,"Lidar tile dimensions w:%lf(%zu) h:%lf(%zu)\n", total_width, new_width, total_height, new_height);
		fflush(stderr);
	}

	/* ...If we wanted a value other than sea level here, we would
	   need to initialize the array... */

	/* Fill out the array one tile at a time */
	for (size_t i = 0; i < (unsigned)fc; i++) {
		double north_offset = max_north - tiles[i].max_north;
		double west_offset = max_west - tiles[i].max_west >= 0 ? max_west - tiles[i].max_west : max_west + (360 - tiles[i].max_west);
		size_t north_pixel_offset = north_offset * tiles[i].ppdy;
		size_t west_pixel_offset = west_offset * tiles[i].ppdx; 

		if (debug) {
			fprintf(stderr,"mn: %lf mw:%lf globals: %lf %lf\n", tiles[i].max_north, tiles[i].max_west, max_north, max_west);
			fprintf(stderr,"Offset n:%zu(%lf) w:%zu(%lf)\n", north_pixel_offset, north_offset, west_pixel_offset, west_offset);
			fprintf(stderr,"Height: %d\n", tiles[i].height);
			fflush(stderr);
		}

		/* Copy it row-by-row from the tile */
		for (size_t h = 0; h < (unsigned)tiles[i].height; h++) {
			register short *dest_addr = &new_tile[ (north_pixel_offset+h)*new_width + west_pixel_offset];
			register short *src_addr = &tiles[i].data[h*tiles[i].width];
			// Check if we might overflow
			if ( dest_addr + tiles[i].width > new_tile + new_tile_alloc || dest_addr < new_tile ){
			        if (debug) {
					fprintf(stderr, "Overflow %zu\n",i);
					fflush(stderr);
				}
				continue;
			}
			memcpy( dest_addr, src_addr, tiles[i].width * sizeof(short) );
		}
	}

	// SUPER tile
	MAXPAGES = 1;
	IPPD = MAX(new_width,new_height);
	ippd=IPPD;

	ARRAYSIZE = (MAXPAGES * IPPD) + 10;
	do_allocs();

	height = new_height;
	width = new_width;

	if (debug) {
		fprintf(stderr,"Setting IPPD to %d height %d width %d\n",IPPD,height,width);
		fflush(stderr);
	}

	/* Load the data into the global dem array */
	dem[0].max_north = max_north;
	dem[0].min_west = min_west;
	dem[0].min_north = min_north;
	dem[0].max_west = max_west;
	dem[0].max_el = max_elevation;
	dem[0].min_el = min_elevation;

	/*
	 * Copy the lidar tile data into the dem array. The dem array is then rotated
	 * 90 degrees(!)...it's a legacy thing.
	 */
	int y = new_height-1;
	for (size_t h = 0; h < new_height; h++, y--) {
		int x = new_width-1;
		for (size_t w = 0; w < new_width; w++, x--) {
			dem[0].data[y][x] = new_tile[h*new_width + w];
			dem[0].signal[y][x] = 0;
			dem[0].mask[y][x] = 0;
		}
	}

	//Polyfilla for warped tiles
	y = new_height-2;
	for (size_t h = 0; h < new_height-2; h++, y--) {
		int x = new_width-2;
		for (size_t w = 0; w < new_width-2; w++, x--) {

			if(dem[0].data[y][x]<=0){
				dem[0].data[y][x] = averageHeight(new_height,new_width,x,y);
			}
		}
	}
	if (width > 3600 * 8) {
		fprintf(stdout,"DEM fault. Contact system administrator: %d\n",width);
		fflush(stderr);
//		exit(1);
	    return 1;
	}

	if (debug) {
		fprintf(stderr, "LIDAR LOADED %d x %d\n", width, height);
		fprintf(stderr, "fc %d WIDTH %d HEIGHT %d ippd %d minN %.5f maxN %.5f minW %.5f maxW %.5f avgCellsize %.5f\n", fc, width, height, ippd,min_north,max_north,min_west,max_west,avgCellsize);
		fflush(stderr);
	}

	if ( tiles != NULL )
	        for (size_t i = 0; i < (unsigned)fc-1; i++)
			tile_destroy(&tiles[i]);
	free(tiles);

	return 0;
}

int LoadSDF_SDF(char *name)
{
	/* This function reads uncompressed ss Data Files (.sdf)
	   containing digital elevation model data into memory.
	   Elevation data, maximum and minimum elevations, and
	   quadrangle limits are stored in the first available
	   dem[] structure. 
	   NOTE: On error, this function returns a negative errno */

	int x, y, data = 0, indx, minlat, minlon, maxlat, maxlon, j;
	char found, free_page = 0, line[20], jline[20], sdf_file[255],
	    path_plus_name[PATH_MAX];

	FILE *fd;

	for (x = 0; name[x] != '.' && name[x] != 0 && x < 250; x++)
		sdf_file[x] = name[x];

	sdf_file[x] = 0;

	/* Parse filename for minimum latitude and longitude values */

	if( sscanf(sdf_file, "%d:%d:%d:%d", &minlat, &maxlat, &minlon, &maxlon) != 4 )
		return -EINVAL;

	sdf_file[x] = '.';
	sdf_file[x + 1] = 's';
	sdf_file[x + 2] = 'd';
	sdf_file[x + 3] = 'f';
	sdf_file[x + 4] = 0;

	/* Is it already in memory? */

	for (indx = 0, found = 0; indx < MAXPAGES && found == 0; indx++) {
		if (minlat == dem[indx].min_north
		    && minlon == dem[indx].min_west
		    && maxlat == dem[indx].max_north
		    && maxlon == dem[indx].max_west)
			found = 1;
	}

	/* Is room available to load it? */

	if (found == 0) {
		for (indx = 0, free_page = 0; indx < MAXPAGES && free_page == 0;
		     indx++)
			if (dem[indx].max_north == -90)
				free_page = 1;
	}

	indx--;

	if (free_page && found == 0 && indx >= 0 && indx < MAXPAGES) {
		/* Search for SDF file in current working directory first */

		strncpy(path_plus_name, sdf_file, sizeof(path_plus_name)-1);

		if( (fd = fopen(path_plus_name, "rb")) == NULL ){
			/* Next, try loading SDF file from path specified
			   in $HOME/.ss_path file or by -d argument */

			strncpy(path_plus_name, sdf_path, sizeof(path_plus_name)-1);
			strncat(path_plus_name, sdf_file, sizeof(path_plus_name)-1);
			if( (fd = fopen(path_plus_name, "rb")) == NULL ){
				return -errno;
			}
		}

		if (debug == 1) {
			fprintf(stderr,
				"Loading \"%s\" into page %d...\n",
				path_plus_name, indx + 1);
			fflush(stderr);
		}

		if (fgets(line, 19, fd) != NULL) {
			if( sscanf(line, "%f", &dem[indx].max_west) == EOF )
				return -errno;
		}

		if (fgets(line, 19, fd) != NULL) {
			if( sscanf(line, "%f", &dem[indx].min_north) == EOF )
				return -errno;
		}

		if (fgets(line, 19, fd) != NULL) {
			if( sscanf(line, "%f", &dem[indx].min_west) == EOF )
				return -errno;
		}

		if (fgets(line, 19, fd) != NULL) {
			if( sscanf(line, "%f", &dem[indx].max_north) == EOF )
				return -errno;
		}

		/*
		   Here X lines of DEM will be read until IPPD is reached.
		   Each .sdf tile contains 1200x1200 = 1.44M 'points'
		   Each point is sampled for 1200 resolution!
		 */
		for (x = 0; x < ippd; x++) {
			for (y = 0; y < ippd; y++) {

				for (j = 0; j < jgets; j++) {
					if( fgets(jline, sizeof(jline), fd) == NULL )
						return -EIO;
				}

				if (fgets(line, sizeof(line), fd) != NULL) {
					data = atoi(line);
				}

				dem[indx].data[x][y] = data;
				dem[indx].signal[x][y] = 0;
				dem[indx].mask[x][y] = 0;

				if (data > dem[indx].max_el)
					dem[indx].max_el = data;

				if (data < dem[indx].min_el)
					dem[indx].min_el = data;

			}

			if (ippd == 600) {
				for (j = 0; j < IPPD; j++) {
					if( fgets(jline, sizeof(jline), fd) == NULL )
						return -EIO;
				}
			}
			if (ippd == 300) {
				for (j = 0; j < IPPD; j++) {
					if( fgets(jline, sizeof(jline), fd) == NULL )
						return -EIO;
					if( fgets(jline, sizeof(jline), fd) == NULL )
						return -EIO;
					if( fgets(jline, sizeof(jline), fd) == NULL )
						return -EIO;
				}
			}
		}

		fclose(fd);

		if (dem[indx].min_el < min_elevation)
			min_elevation = dem[indx].min_el;

		if (dem[indx].max_el > max_elevation)
			max_elevation = dem[indx].max_el;

		if (max_north == -90)
			max_north = dem[indx].max_north;

		else if (dem[indx].max_north > max_north)
			max_north = dem[indx].max_north;

		if (min_north == 90)
			min_north = dem[indx].min_north;

		else if (dem[indx].min_north < min_north)
			min_north = dem[indx].min_north;

		if (max_west == -1)
			max_west = dem[indx].max_west;

		else {
			if (abs(dem[indx].max_west - max_west) < 180) {
				if (dem[indx].max_west > max_west)
					max_west = dem[indx].max_west;
			}

			else {
				if (dem[indx].max_west < max_west)
					max_west = dem[indx].max_west;
			}
		}

		if (min_west == 360)
			min_west = dem[indx].min_west;

		else {
			if (fabs(dem[indx].min_west - min_west) < 180.0) {
				if (dem[indx].min_west < min_west)
					min_west = dem[indx].min_west;
			}

			else {
				if (dem[indx].min_west > min_west)
					min_west = dem[indx].min_west;
			}
		}

		return 1;
	}

	else
		return 0;
}

char *BZfgets(char *output, BZFILE *bzfd, unsigned length)
{
	/* This function returns at most one less than 'length' number
	   of characters from a bz2 compressed file whose file descriptor
	   is pointed to by *bzfd.   A NULL string return indicates an
	   error condition. */

	if (length > BZBUFFER)
	        return NULL;
	for (size_t i = 0; (unsigned)i < length; i++) {
		if (bzbuf_empty) {  // Uncompress data into buffer if empty */

		        bzbytes_read = (long)BZ2_bzRead(&bzerror, bzfd, buffer, BZBUFFER);
			buffer[bzbytes_read] = 0;
			bzbuf_empty = 0;
			/*
			if (bzbytes_read < BZBUFFER)
			          if (bzerror == BZ_STREAM_END)   // if we got EOF during last read
				        BZ2_bzReadGetUnused (&bzerror, bzfd,void** unused, int* nUnused );
			  */
			if (bzerror != BZ_OK && bzerror != BZ_STREAM_END)
			        return (NULL);
		}
	        if (!bzbuf_empty) {  // Build string from buffer if not empty
		        output[i]=buffer[bzbuf_pointer++];

			if (bzbuf_pointer >= bzbytes_read) {
				bzbuf_pointer = 0L;
				bzbuf_empty = 1;
			}

			if (output[i] == '\n')
			        output[++i]=0;

			if (output[i] == 0)
			        break;
		}

	}
	return (output);
}

int LoadSDF_BZ(char *name)
{
	/* This function reads Bzip2 ncompressed ss Data Files (.sdf.bz2)
	   containing digital elevation model data into memory.
	   Elevation data, maximum and minimum elevations, and
	   quadrangle limits are stored in the first available
	   dem[] structure. 
	   NOTE: On error, this function returns a negative errno */

        int x, y, found, data = 0, indx, minlat, minlon, maxlat, maxlon, j,
	  success, pos;
	char free_page = 0, line[20], jline[20], sdf_file[255],
	  path_plus_name[PATH_MAX], bzline[20], *posn;

	FILE *fd;
	BZFILE *bzfd;

	for (x = 0; name[x] != '.' && name[x] != 0 && x < 247; x++)
		sdf_file[x] = name[x];

	sdf_file[x] = 0;

	/* Parse filename for minimum latitude and longitude values */

	if( sscanf(sdf_file, "%d:%d:%d:%d", &minlat, &maxlat, &minlon, &maxlon) != 4 )
		return -EINVAL;

	sdf_file[x]='.';
	sdf_file[x+1]='s';
	sdf_file[x+2]='d';
	sdf_file[x+3]='f';
	sdf_file[x+4]='.';
	sdf_file[x+5]='b';
	sdf_file[x+6]='z';
	sdf_file[x+7]='2';
	sdf_file[x+8]=0;

	/* Is it already in memory? */

	for (indx = 0, found = 0; indx < MAXPAGES && found == 0; indx++) {
		if (minlat == dem[indx].min_north
		    && minlon == dem[indx].min_west
		    && maxlat == dem[indx].max_north
		    && maxlon == dem[indx].max_west)
			found = 1;
	}

	/* Is room available to load it? */

	if (found == 0) {
		for (indx = 0, free_page = 0; indx < MAXPAGES && free_page == 0;
		     indx++)
			if (dem[indx].max_north == -90)
				free_page = 1;
	}

	indx--;

	if (free_page && found == 0 && indx >= 0 && indx < MAXPAGES) {
		/* Search for SDF file in current working directory first */

		strncpy(path_plus_name, sdf_file, sizeof(path_plus_name)-1);


		success = 0;
		fd = fopen(path_plus_name, "rb");
		bzfd=BZ2_bzReadOpen(&bzerror,fd,0,0,NULL,0);

		if (fd != NULL && bzerror == BZ_OK)
		        success = 1;
		else {	
		  /* Next, try loading SDF file from path specified
		     in $HOME/.ss_path file or by -d argument */

		        strncpy(path_plus_name, sdf_path, sizeof(path_plus_name)-1);
			strncat(path_plus_name, sdf_file, sizeof(path_plus_name)-1);
			fd = fopen(path_plus_name, "rb");
			bzfd=BZ2_bzReadOpen(&bzerror,fd,0,0,NULL,0);
			if (fd != NULL && bzerror == BZ_OK)
			        success = 1;
		}
		if (!success)
		        return -errno;

		if (debug == 1) {
			fprintf(stderr,
				"Decompressing \"%s\" into page %d...\n",
				path_plus_name, indx + 1);
			fflush(stderr);
		}

		pos = EOF;
		bzbuf_empty = 1;
		bzbuf_pointer = bzbytes_read = 0L;


		pos = sscanf(BZfgets(bzline, bzfd, 19), "%f", &dem[indx].max_west);
		if (bzerror != BZ_OK || pos == EOF)
		        return -errno;

		pos = sscanf(BZfgets(bzline, bzfd, 19), "%f", &dem[indx].min_north);
		if (bzerror != BZ_OK || pos == EOF)
		        return -errno;

		pos = sscanf(BZfgets(bzline, bzfd, 19), "%f", &dem[indx].min_west);
		if (bzerror != BZ_OK || pos == EOF)
		        return -errno;

		pos = sscanf(BZfgets(bzline, bzfd, 19), "%f", &dem[indx].max_north);
		if (bzerror != BZ_OK || pos == EOF)
		        return -errno;

		/*
		   Here X lines of DEM will be read until IPPD is reached.
		   Each .sdf tile contains 1200x1200 = 1.44M 'points'
		   Each point is sampled for 1200 resolution!
		 */
		posn = NULL;
		for (x = 0; x < ippd; x++) {
			for (y = 0; y < ippd; y++) {

				for (j = 0; j < jgets; j++) {
				        posn = BZfgets(jline, bzfd, 19);
					if ((bzerror != BZ_STREAM_END && bzerror != BZ_OK) || posn == NULL)
					        return -EIO;
				}

				posn = BZfgets(line, bzfd, 19);
				if ((bzerror != BZ_STREAM_END && bzerror != BZ_OK) || posn == NULL)
				       return -EIO;
				data = atoi(line);

				dem[indx].data[x][y] = data;
				dem[indx].signal[x][y] = 0;
				dem[indx].mask[x][y] = 0;

				if (data > dem[indx].max_el)
					dem[indx].max_el = data;

				if (data < dem[indx].min_el)
					dem[indx].min_el = data;

			}

			if (ippd == 600) {
				for (j = 0; j < IPPD; j++) {
				        posn = BZfgets(jline, bzfd, 19);
					if ((bzerror != BZ_STREAM_END && bzerror != BZ_OK) || posn == NULL)
					        return -EIO;
				}
			}
			if (ippd == 300) {
				for (j = 0; j < IPPD; j++) {
				        posn = BZfgets(jline, bzfd, 19);
					if ((bzerror != BZ_STREAM_END && bzerror != BZ_OK) || posn == NULL)
					        return -EIO;
				        posn = BZfgets(jline, bzfd, 19);
					if ((bzerror != BZ_STREAM_END && bzerror != BZ_OK) || posn == NULL)
					        return -EIO;
				        posn = BZfgets(jline, bzfd, 19);
					if ((bzerror != BZ_STREAM_END && bzerror != BZ_OK) || posn == NULL)
					        return -EIO;
				}
			}
		}

		BZ2_bzReadClose(&bzerror,bzfd);
		fclose(fd);

		if (dem[indx].min_el < min_elevation)
			min_elevation = dem[indx].min_el;

		if (dem[indx].max_el > max_elevation)
			max_elevation = dem[indx].max_el;

		if (max_north == -90)
			max_north = dem[indx].max_north;

		else if (dem[indx].max_north > max_north)
			max_north = dem[indx].max_north;

		if (min_north == 90)
			min_north = dem[indx].min_north;

		else if (dem[indx].min_north < min_north)
			min_north = dem[indx].min_north;

		if (max_west == -1)
			max_west = dem[indx].max_west;

		else {
			if (abs(dem[indx].max_west - max_west) < 180) {
				if (dem[indx].max_west > max_west)
					max_west = dem[indx].max_west;
			}

			else {
				if (dem[indx].max_west < max_west)
					max_west = dem[indx].max_west;
			}
		}

		if (min_west == 360)
			min_west = dem[indx].min_west;

		else {
			if (fabs(dem[indx].min_west - min_west) < 180.0) {
				if (dem[indx].min_west < min_west)
					min_west = dem[indx].min_west;
			}

			else {
				if (dem[indx].min_west > min_west)
					min_west = dem[indx].min_west;
			}
		}

		return 1;
	}

	else
		return 0;
}

char *GZfgets(char *output, gzFile gzfd, unsigned length)
{
	/* This function returns at most one less than 'length' number
	   of characters from a Gzip compressed file whose file descriptor
	   is pointed to by gzfd.   A NULL string return indicates an
	   error condition. */

	const char *errmsg;
  
	if (length > GZBUFFER-2)
	        return NULL;


	for (size_t i = 0; (unsigned)i < length; i++) {
		if (gzbuf_empty) {  // Uncompress data into buffer if empty */

		        gzbytes_read = (long)gzread(gzfd, buffer, (unsigned) GZBUFFER-2);
			errmsg = gzerror(gzfd, &gzerr);

			buffer[gzbytes_read] = 0;
			gzbuf_empty = 0;

			if (gzerr != Z_OK && gzerr != Z_STREAM_END)
			        return (NULL);

			if (gzbytes_read < GZBUFFER-2) {
			        if (gzeof(gzfd))
				        gzclearerr(gzfd);
				else
				        return (NULL);
			}
		}
	        if (!gzbuf_empty) {  // Build string from buffer if not empty
		        output[i]=buffer[gzbuf_pointer++];

			if (gzbuf_pointer >= gzbytes_read) {
			        errmsg = gzerror(gzfd, &gzerr);
				gzbuf_pointer = 0L;
				gzbuf_empty = 1;
			}

			if (output[i] == '\n')
			        output[++i]=0;

			if (output[i] == 0)
			        break;
		}

	}
	if (debug && (errmsg != NULL) && (gzerr != Z_OK && gzerr != Z_STREAM_END)) {
	        fprintf(stderr, "GZfgets: gzerr = %d, errmsg = [%s]\n", gzerr, errmsg);
		fflush(stderr);
	}
	return (output);
}


int LoadSDF_GZ(char *name)
{
	/* This function reads Gzip compressed ss Data Files (.sdf.gz)
	   containing digital elevation model data into memory.
	   Elevation data, maximum and minimum elevations, and
	   quadrangle limits are stored in the first available
	   dem[] structure. 
	   NOTE: On error, this function returns a negative errno */

        int x, y, found, data = 0, indx, minlat, minlon, maxlat, maxlon, j,
	  success, pos;
	char free_page = 0, line[20], jline[20], sdf_file[255],
	  path_plus_name[PATH_MAX], gzline[20], *posn;
	const char *errmsg;

	gzFile gzfd;


	for (x = 0; name[x] != '.' && name[x] != 0 && x < 247; x++)
		sdf_file[x] = name[x];

	sdf_file[x] = 0;

	/* Parse filename for minimum latitude and longitude values */

	if( sscanf(sdf_file, "%d:%d:%d:%d", &minlat, &maxlat, &minlon, &maxlon) != 4 )
		return -EINVAL;

	sdf_file[x]='.';
	sdf_file[x+1]='s';
	sdf_file[x+2]='d';
	sdf_file[x+3]='f';
	sdf_file[x+4]='.';
	sdf_file[x+5]='g';
	sdf_file[x+6]='z';
	sdf_file[x+7]=0;

	/* Is it already in memory? */

	for (indx = 0, found = 0; indx < MAXPAGES && found == 0; indx++) {
		if (minlat == dem[indx].min_north
		    && minlon == dem[indx].min_west
		    && maxlat == dem[indx].max_north
		    && maxlon == dem[indx].max_west)
			found = 1;
	}

	/* Is room available to load it? */

	if (found == 0) {
		for (indx = 0, free_page = 0; indx < MAXPAGES && free_page == 0;
		     indx++)
			if (dem[indx].max_north == -90)
				free_page = 1;
	}

	indx--;

	if (free_page && found == 0 && indx >= 0 && indx < MAXPAGES) {
		/* Search for SDF file in current working directory first */

		strncpy(path_plus_name, sdf_file, sizeof(path_plus_name)-1);


		success = 0;
		gzfd = gzopen(path_plus_name, "rb");

		if (gzfd != NULL)
		        success = 1;
		else {	
		  /* Next, try loading SDF file from path specified
		     in $HOME/.ss_path file or by -d argument */

		        strncpy(path_plus_name, sdf_path, sizeof(path_plus_name)-1);
			strncat(path_plus_name, sdf_file, sizeof(path_plus_name)-1);
			gzfd = gzopen(path_plus_name, "rb");

			if (gzfd != NULL)
			        success = 1;
		}
		if (!success)
		        return -errno;

		if (gzbuffer(gzfd, GZBUFFER))  // Allocate 32K buffer
		        return -EIO;

		if (debug == 1) {
			fprintf(stderr,
				"Decompressing \"%s\" into page %d...\n",
				path_plus_name, indx + 1);
			fflush(stderr);
		}

		pos = EOF;
		gzbuf_empty = 1;
		gzbuf_pointer = gzbytes_read = 0L;

		pos = sscanf(GZfgets(gzline, gzfd, 19), "%f", &dem[indx].max_west);
		errmsg = gzerror(gzfd, &gzerr);
		if (gzerr != Z_OK || pos == EOF)
		        return -errno;

		pos = sscanf(GZfgets(gzline, gzfd, 19), "%f", &dem[indx].min_north);
		errmsg = gzerror(gzfd, &gzerr);
		if (gzerr != Z_OK || pos == EOF)
		        return -errno;

		pos = sscanf(GZfgets(gzline, gzfd, 19), "%f", &dem[indx].min_west);
		errmsg = gzerror(gzfd, &gzerr);
		if (gzerr != Z_OK || pos == EOF)
		        return -errno;

		pos = sscanf(GZfgets(gzline, gzfd, 19), "%f", &dem[indx].max_north);
		errmsg = gzerror(gzfd, &gzerr);
		if (gzerr != Z_OK || pos == EOF)
		        return -errno;

		if (debug && (errmsg != NULL) && (gzerr != Z_OK && gzerr != Z_STREAM_END)) {
		        fprintf(stderr, "LoadSDF_GZ: gzerr = %d, errmsg = [%s]\n", gzerr, errmsg);
			fflush(stderr);
		}

		/*
		   Here X lines of DEM will be read until IPPD is reached.
		   Each .sdf tile contains 1200x1200 = 1.44M 'points'
		   Each point is sampled for 1200 resolution!
		 */
		posn = NULL;
		for (x = 0; x < ippd; x++) {
			for (y = 0; y < ippd; y++) {

				for (j = 0; j < jgets; j++) {
				        posn = GZfgets(jline, gzfd, 19);
					errmsg = gzerror(gzfd, &gzerr);
					if ((gzerr != Z_STREAM_END && gzerr != Z_OK) || posn == NULL)
					        return -EIO;
				}

				posn = GZfgets(line, gzfd, 19);
				errmsg = gzerror(gzfd, &gzerr);
				if ((gzerr != Z_STREAM_END && gzerr != Z_OK) || posn == NULL)
				       return -EIO;

				data = atoi(line);

				dem[indx].data[x][y] = data;
				dem[indx].signal[x][y] = 0;
				dem[indx].mask[x][y] = 0;

				if (data > dem[indx].max_el)
					dem[indx].max_el = data;

				if (data < dem[indx].min_el)
					dem[indx].min_el = data;

			}

			if (ippd == 600) {
				for (j = 0; j < IPPD; j++) {
				        posn = GZfgets(jline, gzfd, 19);
					errmsg = gzerror(gzfd, &gzerr);
					if ((gzerr != Z_STREAM_END && gzerr != Z_OK) || posn == NULL)
					        return -EIO;
				}
			}
			if (ippd == 300) {
				for (j = 0; j < IPPD; j++) {
				        posn = GZfgets(jline, gzfd, 19);
					errmsg = gzerror(gzfd, &gzerr);
					if ((gzerr != Z_STREAM_END && gzerr != Z_OK) || posn == NULL)
					        return -EIO;
				        posn = GZfgets(jline, gzfd, 19);
					errmsg = gzerror(gzfd, &gzerr);
					if ((gzerr != Z_STREAM_END && gzerr != Z_OK) || posn == NULL)
					        return -EIO;
				        posn = GZfgets(jline, gzfd, 19);
					errmsg = gzerror(gzfd, &gzerr);
					if ((gzerr != Z_STREAM_END && gzerr != Z_OK) || posn == NULL)
					        return -EIO;
				}
			}
		}

		gzclose_r(gzfd);  // close for reading (avoids write code)

		if (dem[indx].min_el < min_elevation)
			min_elevation = dem[indx].min_el;

		if (dem[indx].max_el > max_elevation)
			max_elevation = dem[indx].max_el;

		if (max_north == -90)
			max_north = dem[indx].max_north;

		else if (dem[indx].max_north > max_north)
			max_north = dem[indx].max_north;

		if (min_north == 90)
			min_north = dem[indx].min_north;

		else if (dem[indx].min_north < min_north)
			min_north = dem[indx].min_north;

		if (max_west == -1)
			max_west = dem[indx].max_west;

		else {
			if (abs(dem[indx].max_west - max_west) < 180) {
				if (dem[indx].max_west > max_west)
					max_west = dem[indx].max_west;
			}

			else {
				if (dem[indx].max_west < max_west)
					max_west = dem[indx].max_west;
			}
		}

		if (min_west == 360)
			min_west = dem[indx].min_west;

		else {
			if (fabs(dem[indx].min_west - min_west) < 180.0) {
				if (dem[indx].min_west < min_west)
					min_west = dem[indx].min_west;
			}

			else {
				if (dem[indx].min_west > min_west)
					min_west = dem[indx].min_west;
			}
		}

		return 1;
	}

	else
		return 0;
}


int LoadSDF(char *name)
{
	/* This function loads the requested SDF file from the filesystem.
	   It first tries to invoke the LoadSDF_SDF() function to load an
	   uncompressed SDF file (since uncompressed files load slightly
	   faster).  If that attempt fails, then it tries to load a
	   compressed SDF file by invoking the LoadSDF_BZ() function.
	   If that attempt fails, then it tries again to load a
	   compressed SDF file by invoking the LoadSDF_GZ() function.
	   If that fails, then we can assume that no elevation data
	   exists for the region requested, and that the region
	   requested must be entirely over water. */

	int x, y, indx, minlat, minlon, maxlat, maxlon;
	char found, free_page = 0;
	int return_value = -1;

	/* Try to load an uncompressed SDF first. */

	return_value = LoadSDF_SDF(name);

	/* If that fails, try loading a BZ2 compressed SDF. */

	if ( return_value <= 0 )
	        return_value = LoadSDF_BZ(name);

	/* If that fails, try loading a gzip compressed SDF. */

	if ( return_value <= 0 )
	        return_value = LoadSDF_GZ(name);

	/* If no file format can be found, then assume the area is water. */

	if ( return_value <= 0 ) {

		sscanf(name, "%d:%d:%d:%d", &minlat, &maxlat, &minlon,
		       &maxlon);

		/* Is it already in memory? */

		for (indx = 0, found = 0; indx < MAXPAGES && found == 0; indx++) {
			if (minlat == dem[indx].min_north
			    && minlon == dem[indx].min_west
			    && maxlat == dem[indx].max_north
			    && maxlon == dem[indx].max_west)
				found = 1;
		}

		/* Is room available to load it? */

		if (found == 0) {
			for (indx = 0, free_page = 0;
			     indx < MAXPAGES && free_page == 0; indx++)
				if (dem[indx].max_north == -90)
					free_page = 1;
		}

		indx--;

		if (free_page && found == 0 && indx >= 0 && indx < MAXPAGES) {
			if (debug == 1) {
				fprintf(stderr,
					"Region  \"%s\" assumed as sea-level into page %d...\n",
					name, indx + 1);
				fflush(stderr);
			}

			dem[indx].max_west = maxlon;
			dem[indx].min_north = minlat;
			dem[indx].min_west = minlon;
			dem[indx].max_north = maxlat;

			/* Fill DEM with sea-level topography */

			for (x = 0; x < ippd; x++)
				for (y = 0; y < ippd; y++) {
					dem[indx].data[x][y] = 0;
					dem[indx].signal[x][y] = 0;
					dem[indx].mask[x][y] = 0;

					if (dem[indx].min_el > 0)
						dem[indx].min_el = 0;
				}

			if (dem[indx].min_el < min_elevation)
				min_elevation = dem[indx].min_el;

			if (dem[indx].max_el > max_elevation)
				max_elevation = dem[indx].max_el;

			if (max_north == -90)
				max_north = dem[indx].max_north;

			else if (dem[indx].max_north > max_north)
				max_north = dem[indx].max_north;

			if (min_north == 90)
				min_north = dem[indx].min_north;

			else if (dem[indx].min_north < min_north)
				min_north = dem[indx].min_north;

			if (max_west == -1)
				max_west = dem[indx].max_west;

			else {
				if (abs(dem[indx].max_west - max_west) < 180) {
					if (dem[indx].max_west > max_west)
						max_west = dem[indx].max_west;
				}

				else {
					if (dem[indx].max_west < max_west)
						max_west = dem[indx].max_west;
				}
			}

			if (min_west == 360)
				min_west = dem[indx].min_west;

			else {
				if (abs(dem[indx].min_west - min_west) < 180) {
					if (dem[indx].min_west < min_west)
						min_west = dem[indx].min_west;
				}

				else {
					if (dem[indx].min_west > min_west)
						min_west = dem[indx].min_west;
				}
			}

			return_value = 1;
		}
	}

	return return_value;
}

int LoadPAT(char *az_filename, char *el_filename)
{
	/* This function reads and processes antenna pattern (.az
	   and .el) files that may correspond in name to previously
	   loaded ss .lrp files or may be user-supplied by cmdline.  */

	int a, b, w, x, y, z, last_index, next_index, span;
	char string[255], *pointer = NULL;
	float az, xx, elevation, amplitude, rotation, valid1, valid2,
	    delta, azimuth[361], azimuth_pattern[361], el_pattern[10001],
	    elevation_pattern[361][1001], slant_angle[361], tilt,
	    mechanical_tilt = 0.0, tilt_azimuth, tilt_increment, sum;
	FILE *fd = NULL;
	unsigned char read_count[10001];

	rotation = 0.0;

	got_azimuth_pattern = 0;
	got_elevation_pattern = 0;

	/* Load .az antenna pattern file */

	if( az_filename != NULL && (fd = fopen(az_filename, "r")) == NULL && errno != ENOENT )
		/* Any error other than file not existing is an error */
		return errno;

	if( fd != NULL ){
	        if (debug) {

		        fprintf(stderr, "\nAntenna Pattern Azimuth File = [%s]\n", az_filename);
			fflush(stderr);
		}

		/* Clear azimuth pattern array */
		for (x = 0; x <= 360; x++) {
			azimuth[x] = 0.0;
			read_count[x] = 0;
		}

		/* Read azimuth pattern rotation
		   in degrees measured clockwise
		   from true North. */

		if (fgets(string, 254, fd) == NULL) {
			//fprintf(stderr,"Azimuth read error\n");
			//exit(0);
		}
		pointer = strchr(string, ';');

		if (pointer != NULL)
			*pointer = 0;

		if (antenna_rotation != -1)  // If cmdline override
		  rotation = (float)antenna_rotation;
		else
		        sscanf(string, "%f", &rotation);
		
	        if (debug) {
		        fprintf(stderr, "Antenna Pattern Rotation = %f\n", rotation);
			fflush(stderr);
		}
		/* Read azimuth (degrees) and corresponding
		   normalized field radiation pattern amplitude
		   (0.0 to 1.0) until EOF is reached. */

		if (fgets(string, 254, fd) == NULL) {
			//fprintf(stderr,"Azimuth read error\n");
			//exit(0);
		}
		pointer = strchr(string, ';');

		if (pointer != NULL)
			*pointer = 0;

		sscanf(string, "%f %f", &az, &amplitude);

		do {
			x = (int)rintf(az);

			if (x >= 0 && x <= 360 && fd != NULL) {
				azimuth[x] += amplitude;
				read_count[x]++;
			}

			if (fgets(string, 254, fd) == NULL) {
				//fprintf(stderr,"Azimuth read error\n");
				// exit(0);
			}
			pointer = strchr(string, ';');

			if (pointer != NULL)
				*pointer = 0;

			sscanf(string, "%f %f", &az, &amplitude);

		} while (feof(fd) == 0);

		fclose(fd);
		fd = NULL;

		/* Handle 0=360 degree ambiguity */

		if ((read_count[0] == 0) && (read_count[360] != 0)) {
			read_count[0] = read_count[360];
			azimuth[0] = azimuth[360];
		}

		if ((read_count[0] != 0) && (read_count[360] == 0)) {
			read_count[360] = read_count[0];
			azimuth[360] = azimuth[0];
		}

		/* Average pattern values in case more than
		   one was read for each degree of azimuth. */

		for (x = 0; x <= 360; x++) {
			if (read_count[x] > 1)
				azimuth[x] /= (float)read_count[x];
		}

		/* Interpolate missing azimuths
		   to completely fill the array */

		last_index = -1;
		next_index = -1;

		for (x = 0; x <= 360; x++) {
			if (read_count[x] != 0) {
				if (last_index == -1)
					last_index = x;
				else
					next_index = x;
			}

			if (last_index != -1 && next_index != -1) {
				valid1 = azimuth[last_index];
				valid2 = azimuth[next_index];

				span = next_index - last_index;
				delta = (valid2 - valid1) / (float)span;

				for (y = last_index + 1; y < next_index; y++)
					azimuth[y] = azimuth[y - 1] + delta;

				last_index = y;
				next_index = -1;
			}
		}

		/* Perform azimuth pattern rotation
		   and load azimuth_pattern[361] with
		   azimuth pattern data in its final form. */

		for (x = 0; x < 360; x++) {
			y = x + (int)rintf(rotation);

			if (y >= 360)
				y -= 360;

			azimuth_pattern[y] = azimuth[x];
		}

		azimuth_pattern[360] = azimuth_pattern[0];

		got_azimuth_pattern = 255;
	}

	/* Read and process .el file */

	if( el_filename != NULL && (fd = fopen(el_filename, "r")) == NULL && errno != ENOENT )
		/* Any error other than file not existing is an error */
		return errno;

	if( fd != NULL ){
	        if (debug) {
		        fprintf(stderr, "Antenna Pattern Elevation File = [%s]\n", el_filename);
			fflush(stderr);
		}

		/* Clear azimuth pattern array */

		for (x = 0; x <= 10000; x++) {
			el_pattern[x] = 0.0;
			read_count[x] = 0;
		}

		/* Read mechanical tilt (degrees) and
		   tilt azimuth in degrees measured
		   clockwise from true North. */

		if (fgets(string, 254, fd) == NULL) {
			//fprintf(stderr,"Tilt read error\n");
			//exit(0);
		}
		pointer = strchr(string, ';');

		if (pointer != NULL)
			*pointer = 0;

		sscanf(string, "%f %f", &mechanical_tilt, &tilt_azimuth);

		if (antenna_downtilt != 99.0) {  // If Cmdline override
		        if (antenna_dt_direction == -1) // dt_dir not specified
			        tilt_azimuth = rotation;  // use rotation value
		        mechanical_tilt = (float)antenna_downtilt;
		}

		if (antenna_dt_direction != -1) // If Cmdline override
		        tilt_azimuth = (float)antenna_dt_direction;
		
	        if (debug) {
		        fprintf(stderr, "Antenna Pattern Mechamical Downtilt = %f\n", mechanical_tilt);
		        fprintf(stderr, "Antenna Pattern Mechanical Downtilt Direction = %f\n\n", tilt_azimuth);
			fflush(stderr);
		}

		/* Read elevation (degrees) and corresponding
		   normalized field radiation pattern amplitude
		   (0.0 to 1.0) until EOF is reached. */

		if (fgets(string, 254, fd) == NULL) {
			//fprintf(stderr,"Ant elevation read error\n");
			//exit(0);
		}
		pointer = strchr(string, ';');

		if (pointer != NULL)
			*pointer = 0;

		sscanf(string, "%f %f", &elevation, &amplitude);

		while (feof(fd) == 0) {
			/* Read in normalized radiated field values
			   for every 0.01 degrees of elevation between
			   -10.0 and +90.0 degrees */

			x = (int)rintf(100.0 * (elevation + 10.0));

			if (x >= 0 && x <= 10000) {
				el_pattern[x] += amplitude;
				read_count[x]++;
			}

			if (fgets(string, 254, fd) != NULL) {
				pointer = strchr(string, ';');
			}
			if (pointer != NULL)
				*pointer = 0;

			sscanf(string, "%f %f", &elevation, &amplitude);
		}

		fclose(fd);

		/* Average the field values in case more than
		   one was read for each 0.01 degrees of elevation. */

		for (x = 0; x <= 10000; x++) {
			if (read_count[x] > 1)
				el_pattern[x] /= (float)read_count[x];
		}

		/* Interpolate between missing elevations (if
		   any) to completely fill the array and provide
		   radiated field values for every 0.01 degrees of
		   elevation. */

		last_index = -1;
		next_index = -1;

		for (x = 0; x <= 10000; x++) {
			if (read_count[x] != 0) {
				if (last_index == -1)
					last_index = x;
				else
					next_index = x;
			}

			if (last_index != -1 && next_index != -1) {
				valid1 = el_pattern[last_index];
				valid2 = el_pattern[next_index];

				span = next_index - last_index;
				delta = (valid2 - valid1) / (float)span;

				for (y = last_index + 1; y < next_index; y++)
					el_pattern[y] =
					    el_pattern[y - 1] + delta;

				last_index = y;
				next_index = -1;
			}
		}

		/* Fill slant_angle[] array with offset angles based
		   on the antenna's mechanical beam tilt (if any)
		   and tilt direction (azimuth). */

		if (mechanical_tilt == 0.0) {
			for (x = 0; x <= 360; x++)
				slant_angle[x] = 0.0;
		}

		else {
			tilt_increment = mechanical_tilt / 90.0;

			for (x = 0; x <= 360; x++) {
				xx = (float)x;
				y = (int)rintf(tilt_azimuth + xx);

				while (y >= 360)
					y -= 360;

				while (y < 0)
					y += 360;

				if (x <= 180)
					slant_angle[y] =
					    -(tilt_increment * (90.0 - xx));

				if (x > 180)
					slant_angle[y] =
					    -(tilt_increment * (xx - 270.0));
			}
		}

		slant_angle[360] = slant_angle[0];	/* 360 degree wrap-around */

		for (w = 0; w <= 360; w++) {
			tilt = slant_angle[w];

	    /** Convert tilt angle to
	            an array index offset **/

			y = (int)rintf(100.0 * tilt);

			/* Copy shifted el_pattern[10001] field
			   values into elevation_pattern[361][1001]
			   at the corresponding azimuth, downsampling
			   (averaging) along the way in chunks of 10. */

			for (x = y, z = 0; z <= 1000; x += 10, z++) {
				for (sum = 0.0, a = 0; a < 10; a++) {
					b = a + x;

					if (b >= 0 && b <= 10000)
						sum += el_pattern[b];
					if (b < 0)
						sum += el_pattern[0];
					if (b > 10000)
						sum += el_pattern[10000];
				}

				elevation_pattern[w][z] = sum / 10.0;
			}
		}

		got_elevation_pattern = 255;

		for (x = 0; x <= 360; x++) {
			for (y = 0; y <= 1000; y++) {
				if (got_elevation_pattern)
					elevation = elevation_pattern[x][y];
				else
					elevation = 1.0;

				if (got_azimuth_pattern)
					az = azimuth_pattern[x];
				else
					az = 1.0;

				LR.antenna_pattern[x][y] = az * elevation;
			}
		}
	}
	return 0;
}

int LoadSignalColors(struct site xmtr)
{
	int x, y, ok, val[4];
	char filename[255], string[80], *pointer = NULL, *s;
	FILE *fd = NULL;

	if (color_file != NULL && color_file[0] != 0)
	        for (x = 0; color_file[x] != '.' && color_file[x] != 0 && x < 250; x++)
		        filename[x] = color_file[x];
	else
	        for (x = 0; xmtr.filename[x] != '.' && xmtr.filename[x] != 0 && x < 250; x++)
		        filename[x] = xmtr.filename[x];

	filename[x] = '.';
	filename[x + 1] = 's';
	filename[x + 2] = 'c';
	filename[x + 3] = 'f';
	filename[x + 4] = 0;

	/* Default values */

	region.level[0] = 128;
	region.color[0][0] = 255;
	region.color[0][1] = 0;
	region.color[0][2] = 0;

	region.level[1] = 118;
	region.color[1][0] = 255;
	region.color[1][1] = 165;
	region.color[1][2] = 0;

	region.level[2] = 108;
	region.color[2][0] = 255;
	region.color[2][1] = 206;
	region.color[2][2] = 0;

	region.level[3] = 98;
	region.color[3][0] = 255;
	region.color[3][1] = 255;
	region.color[3][2] = 0;

	region.level[4] = 88;
	region.color[4][0] = 184;
	region.color[4][1] = 255;
	region.color[4][2] = 0;

	region.level[5] = 78;
	region.color[5][0] = 0;
	region.color[5][1] = 255;
	region.color[5][2] = 0;

	region.level[6] = 68;
	region.color[6][0] = 0;
	region.color[6][1] = 208;
	region.color[6][2] = 0;

	region.level[7] = 58;
	region.color[7][0] = 0;
	region.color[7][1] = 196;
	region.color[7][2] = 196;

	region.level[8] = 48;
	region.color[8][0] = 0;
	region.color[8][1] = 148;
	region.color[8][2] = 255;

	region.level[9] = 38;
	region.color[9][0] = 80;
	region.color[9][1] = 80;
	region.color[9][2] = 255;

	region.level[10] = 28;
	region.color[10][0] = 0;
	region.color[10][1] = 38;
	region.color[10][2] = 255;

	region.level[11] = 18;
	region.color[11][0] = 142;
	region.color[11][1] = 63;
	region.color[11][2] = 255;

	region.level[12] = 8;
	region.color[12][0] = 140;
	region.color[12][1] = 0;
	region.color[12][2] = 128;

	region.levels = 13;

	/* Don't save if we don't have an output file */
	if ( (fd = fopen(filename, "r")) == NULL && xmtr.filename[0] == '\0' )
		return 0;

	if (fd == NULL) {
		if( (fd = fopen(filename, "w")) == NULL )
			return errno;

		for (x = 0; x < region.levels; x++)
			fprintf(fd, "%3d: %3d, %3d, %3d\n", region.level[x],
				region.color[x][0], region.color[x][1],
				region.color[x][2]);

		fclose(fd);
	}

	else {
		x = 0;
		s = fgets(string, 80, fd);

		if (s)
		  ;

		while (x < 128 && feof(fd) == 0) {
			pointer = strchr(string, ';');

			if (pointer != NULL)
				*pointer = 0;

			ok = sscanf(string, "%d: %d, %d, %d", &val[0], &val[1],
				    &val[2], &val[3]);

			if (ok == 4) {
			        if (debug) {
				        fprintf(stderr, "\nLoadSignalColors() %d: %d, %d, %d\n", val[0],val[1],val[2],val[3]);
					fflush(stderr);
				}

				for (y = 0; y < 4; y++) {
					if (val[y] > 255)
						val[y] = 255;

					if (val[y] < 0)
						val[y] = 0;
				}

				region.level[x] = val[0];
				region.color[x][0] = val[1];
				region.color[x][1] = val[2];
				region.color[x][2] = val[3];
				x++;
			}

			s = fgets(string, 80, fd);
		}

		fclose(fd);
		region.levels = x;
	}
	return 0;
}

int LoadLossColors(struct site xmtr)
{
	int x, y, ok, val[4];
	char filename[255], string[80], *pointer = NULL, *s;
	FILE *fd = NULL;

	if (color_file != NULL && color_file[0] != 0)
	        for (x = 0; color_file[x] != '.' && color_file[x] != 0 && x < 250; x++)
		        filename[x] = color_file[x];
	else
	        for (x = 0; xmtr.filename[x] != '.' && xmtr.filename[x] != 0 && x < 250; x++)
		        filename[x] = xmtr.filename[x];

	filename[x] = '.';
	filename[x + 1] = 'l';
	filename[x + 2] = 'c';
	filename[x + 3] = 'f';
	filename[x + 4] = 0;

	/* Default values */

	region.level[0] = 80;
	region.color[0][0] = 255;
	region.color[0][1] = 0;
	region.color[0][2] = 0;

	region.level[1] = 90;
	region.color[1][0] = 255;
	region.color[1][1] = 128;
	region.color[1][2] = 0;

	region.level[2] = 100;
	region.color[2][0] = 255;
	region.color[2][1] = 165;
	region.color[2][2] = 0;

	region.level[3] = 110;
	region.color[3][0] = 255;
	region.color[3][1] = 206;
	region.color[3][2] = 0;

	region.level[4] = 120;
	region.color[4][0] = 255;
	region.color[4][1] = 255;
	region.color[4][2] = 0;

	region.level[5] = 130;
	region.color[5][0] = 184;
	region.color[5][1] = 255;
	region.color[5][2] = 0;

	region.level[6] = 140;
	region.color[6][0] = 0;
	region.color[6][1] = 255;
	region.color[6][2] = 0;

	region.level[7] = 150;
	region.color[7][0] = 0;
	region.color[7][1] = 208;
	region.color[7][2] = 0;

	region.level[8] = 160;
	region.color[8][0] = 0;
	region.color[8][1] = 196;
	region.color[8][2] = 196;

	region.level[9] = 170;
	region.color[9][0] = 0;
	region.color[9][1] = 148;
	region.color[9][2] = 255;

	region.level[10] = 180;
	region.color[10][0] = 80;
	region.color[10][1] = 80;
	region.color[10][2] = 255;

	region.level[11] = 190;
	region.color[11][0] = 0;
	region.color[11][1] = 38;
	region.color[11][2] = 255;

	region.level[12] = 200;
	region.color[12][0] = 142;
	region.color[12][1] = 63;
	region.color[12][2] = 255;

	region.level[13] = 210;
	region.color[13][0] = 196;
	region.color[13][1] = 54;
	region.color[13][2] = 255;

	region.level[14] = 220;
	region.color[14][0] = 255;
	region.color[14][1] = 0;
	region.color[14][2] = 255;

	region.level[15] = 230;
	region.color[15][0] = 255;
	region.color[15][1] = 194;
	region.color[15][2] = 204;

	region.levels = 16;
/*	region.levels = 120; // 240dB max PL */

/*	for(int i=0; i<region.levels;i++){
		region.level[i] = i*2;
		region.color[i][0] = i*2;
		region.color[i][1] = i*2;
		region.color[i][2] = i*2;
	}
*/
	/* Don't save if we don't have an output file */
	if ( (fd = fopen(filename, "r")) == NULL && xmtr.filename[0] == '\0' )
		return 0;

	if (fd == NULL) {
		if( (fd = fopen(filename, "w")) == NULL )
			return errno;

		for (x = 0; x < region.levels; x++)
			fprintf(fd, "%3d: %3d, %3d, %3d\n", region.level[x],
				region.color[x][0], region.color[x][1],
				region.color[x][2]);

		fclose(fd);

                if (debug) {
                fprintf(stderr, "loadLossColors: fopen fail: %s\n", filename);
                fflush(stderr);
                }

	}

	else {
		x = 0;
		s = fgets(string, 80, fd);

		if (s)
		  ;

		while (x < 128 && feof(fd) == 0) {
			pointer = strchr(string, ';');

			if (pointer != NULL)
				*pointer = 0;

			ok = sscanf(string, "%d: %d, %d, %d", &val[0], &val[1],
				    &val[2], &val[3]);

			if (ok == 4) {
                                 if (debug) {
                                fprintf(stderr, "\nLoadLossColors() %d: %d, %d, %d\n", val[0],val[1],val[2],val[3]);
                                fflush(stderr);
                                 }

				for (y = 0; y < 4; y++) {
					if (val[y] > 255)
						val[y] = 255;

					if (val[y] < 0)
						val[y] = 0;
				}

				region.level[x] = val[0];
				region.color[x][0] = val[1];
				region.color[x][1] = val[2];
				region.color[x][2] = val[3];
				x++;
			}

			s = fgets(string, 80, fd);
		}

		fclose(fd);
		region.levels = x;
	}
	return 0;
}

int LoadDBMColors(struct site xmtr)
{
	int x, y, ok, val[4];
	char filename[255], string[80], *pointer = NULL, *s;
	FILE *fd = NULL;

	if (color_file != NULL && color_file[0] != 0)
	        for (x = 0; color_file[x] != '.' && color_file[x] != 0 && x < 250; x++)
		        filename[x] = color_file[x];
	else
	        for (x = 0; xmtr.filename[x] != '.' && xmtr.filename[x] != 0 && x < 250; x++)
		        filename[x] = xmtr.filename[x];

	filename[x] = '.';
	filename[x + 1] = 'd';
	filename[x + 2] = 'c';
	filename[x + 3] = 'f';
	filename[x + 4] = 0;

	/* Default values */

	region.level[0] = 0;
	region.color[0][0] = 255;
	region.color[0][1] = 0;
	region.color[0][2] = 0;

	region.level[1] = -10;
	region.color[1][0] = 255;
	region.color[1][1] = 128;
	region.color[1][2] = 0;

	region.level[2] = -20;
	region.color[2][0] = 255;
	region.color[2][1] = 165;
	region.color[2][2] = 0;

	region.level[3] = -30;
	region.color[3][0] = 255;
	region.color[3][1] = 206;
	region.color[3][2] = 0;

	region.level[4] = -40;
	region.color[4][0] = 255;
	region.color[4][1] = 255;
	region.color[4][2] = 0;

	region.level[5] = -50;
	region.color[5][0] = 184;
	region.color[5][1] = 255;
	region.color[5][2] = 0;

	region.level[6] = -60;
	region.color[6][0] = 0;
	region.color[6][1] = 255;
	region.color[6][2] = 0;

	region.level[7] = -70;
	region.color[7][0] = 0;
	region.color[7][1] = 208;
	region.color[7][2] = 0;

	region.level[8] = -80;
	region.color[8][0] = 0;
	region.color[8][1] = 196;
	region.color[8][2] = 196;

	region.level[9] = -90;
	region.color[9][0] = 0;
	region.color[9][1] = 148;
	region.color[9][2] = 255;

	region.level[10] = -100;
	region.color[10][0] = 80;
	region.color[10][1] = 80;
	region.color[10][2] = 255;

	region.level[11] = -110;
	region.color[11][0] = 0;
	region.color[11][1] = 38;
	region.color[11][2] = 255;

	region.level[12] = -120;
	region.color[12][0] = 142;
	region.color[12][1] = 63;
	region.color[12][2] = 255;

	region.level[13] = -130;
	region.color[13][0] = 196;
	region.color[13][1] = 54;
	region.color[13][2] = 255;

	region.level[14] = -140;
	region.color[14][0] = 255;
	region.color[14][1] = 0;
	region.color[14][2] = 255;

	region.level[15] = -150;
	region.color[15][0] = 255;
	region.color[15][1] = 194;
	region.color[15][2] = 204;

	region.levels = 16;

	/* Don't save if we don't have an output file */
	if ( (fd = fopen(filename, "r")) == NULL && xmtr.filename[0] == '\0' )
		return 0;

	if (fd == NULL) {
		if( (fd = fopen(filename, "w")) == NULL )
			return errno;

		for (x = 0; x < region.levels; x++)
			fprintf(fd, "%+4d: %3d, %3d, %3d\n", region.level[x],
				region.color[x][0], region.color[x][1],
				region.color[x][2]);

		fclose(fd);
	}

	else {
		x = 0;
		s = fgets(string, 80, fd);

		if (s)
		  ;

		while (x < 128 && feof(fd) == 0) {
			pointer = strchr(string, ';');

			if (pointer != NULL)
				*pointer = 0;

			ok = sscanf(string, "%d: %d, %d, %d", &val[0], &val[1],
				    &val[2], &val[3]);

			if (ok == 4) {
                                 if (debug) {
                                fprintf(stderr, "\nLoadDBMColors() %d: %d, %d, %d\n", val[0],val[1],val[2],val[3]);
                                fflush(stderr);
                                 }

				if (val[0] < -200)
					val[0] = -200;

				if (val[0] > +40)
					val[0] = +40;

				region.level[x] = val[0];

				for (y = 1; y < 4; y++) {
					if (val[y] > 255)
						val[y] = 255;

					if (val[y] < 0)
						val[y] = 0;
				}

				region.color[x][0] = val[1];
				region.color[x][1] = val[2];
				region.color[x][2] = val[3];
				x++;
			}

			s = fgets(string, 80, fd);
		}

		fclose(fd);
		region.levels = x;
	}
	return 0;
}

int LoadTopoData(double max_lon, double min_lon, double max_lat, double min_lat)
{
	/* This function loads the SDF files required
	   to cover the limits of the region specified. */

	int x, y, width, ymin, ymax;
	int success;
	char basename[255], string[258];

	width = ReduceAngle(max_lon - min_lon);

	if ((max_lon - min_lon) <= 180.0) {
		for (y = 0; y <= width; y++)
		        for (x = min_lat; x <= (int)max_lat; x++) {
				ymin = (int)(min_lon + (double)y);

				while (ymin < 0)
					ymin += 360;

				while (ymin >= 360)
					ymin -= 360;

				ymax = ymin + 1;

				while (ymax < 0)
					ymax += 360;

				while (ymax >= 360)
					ymax -= 360;

				snprintf(basename, 255, "%d:%d:%d:%d", x, x + 1, ymin, ymax);
				strcpy(string, basename);


				if (ippd == 3600)
				        strcat(string, "-hd");

				if( (success = LoadSDF(string)) < 0 )
					return -success;
			}
	} else {
		for (y = 0; y <= width; y++)
		        for (x = (int)min_lat; x <= (int)max_lat; x++) {
			        ymin = (int)(max_lon + (double)y);

				while (ymin < 0)
					ymin += 360;

				while (ymin >= 360)
					ymin -= 360;

				ymax = ymin + 1;

				while (ymax < 0)
					ymax += 360;

				while (ymax >= 360)
					ymax -= 360;

				snprintf(string, 255, "%d:%d:%d:%d", x, x + 1, ymin, ymax);
				strcpy(string, basename);

				if (ippd == 3600)
				        strcat(string, "-hd");

				if( (success = LoadSDF(string)) < 0 )
					return -success;
			}
	}
	return 0;
}

int LoadUDT(char *filename)
{
	/* This function reads a file containing User-Defined Terrain
	   features for their addition to the digital elevation model
	   data used by SPLAT!.  Elevations in the UDT file are evaluated
	   and then copied into a temporary file under /tmp.  Then the
	   contents of the temp file are scanned, and if found to be unique,
	   are added to the ground elevations described by the digital
	   elevation data already loaded into memory. */

	int i, x, y, z, ypix, xpix, tempxpix, tempypix, fd = 0, n = 0;
	char input[80], str[3][80], tempname[15], *pointer = NULL, *s = NULL;
	double latitude, longitude, height, tempheight, old_longitude = 0.0,
	  old_latitude = 0.0;
	FILE *fd1 = NULL, *fd2 = NULL;

	strcpy(tempname, "/tmp/XXXXXX");

	if( (fd1 = fopen(filename, "r")) == NULL )
		return errno;

	if( (fd = mkstemp(tempname)) == -1 )
		return errno;

	if( (fd2 = fdopen(fd,"w")) == NULL ){
		fclose(fd1);
		close(fd);
		return errno;
	}

	s = fgets(input, 78, fd1);

	if (s)
	  ;

	pointer = strchr(input, ';');

	if (pointer != NULL)
		*pointer = 0;

	while (feof(fd1) == 0) {
	  // Parse line for latitude, longitude, height

		for (x = 0, y = 0, z = 0;
		     x < 78 && input[x] != 0 && z < 3; x++) {
			if (input[x] != ',' && y < 78) {
				str[z][y] = input[x];
				y++;
			}

			else {
				str[z][y] = 0;
				z++;
				y = 0;
			}
		}

		old_latitude = latitude = ReadBearing(str[0]);
		old_longitude = longitude = ReadBearing(str[1]);

		latitude = fabs(latitude);  // Clip if negative
		longitude = fabs(longitude);

		// Remove <CR> and/or <LF> from antenna height string

		for (i = 0;
		     str[2][i] != 13 && str[2][i] != 10
		     && str[2][i] != 0; i++) ;

		str[2][i] = 0;

		/* The terrain feature may be expressed in either
		   feet or meters.  If the letter 'M' or 'm' is
		   discovered in the string, then this is an
		   indication that the value given is expressed
		   in meters.  Otherwise the height is interpreted
		   as being expressed in feet.  */

		for (i = 0;
		     str[2][i] != 'M' && str[2][i] != 'm'
		     && str[2][i] != 0 && i < 48; i++) ;

		if (str[2][i] == 'M' || str[2][i] == 'm') {
			str[2][i] = 0;
			height = rint(atof(str[2]));
		}

		else {
			str[2][i] = 0;
			height = rint(METERS_PER_FOOT * atof(str[2]));
		}

		if (height > 0.0)
			fprintf(fd2, "%d, %d, %f\n",
				(int)rint(latitude / dpp),
				(int)rint(longitude / dpp), height);

		s = fgets(input, 78, fd1);

		pointer = strchr(input, ';');

		if (pointer != NULL)
			*pointer = 0;
	}

	fclose(fd1);
	fclose(fd2);

	if( (fd1 = fopen(tempname, "r")) == NULL )
		return errno;

	if( (fd2 = fopen(tempname, "r")) == NULL ){
		fclose(fd1);
		return errno;
	}

	y = 0;

	n = fscanf(fd1, "%d, %d, %lf", &xpix, &ypix, &height);

	if (n)
	  ;

	do {
		x = 0;
		z = 0;

		n = fscanf(fd2, "%d, %d, %lf", &tempxpix, &tempypix,
			   &tempheight);

		do {
			if (x > y && xpix == tempxpix
			    && ypix == tempypix) {
			        z = 1;	// Dupe Found!

				if (tempheight > height)
					height = tempheight;
			}

			else {
				n = fscanf(fd2, "%d, %d, %lf",
					   &tempxpix, &tempypix,
					   &tempheight);
				x++;
			}

		} while (feof(fd2) == 0 && z == 0);

		if (z == 0) {
			// No duplicate found
		        if (debug) {
			        fprintf(stderr,"Adding UDT Point: %lf, %lf, %lf\n", old_latitude, old_longitude,height);
				fflush(stderr);
			}
			AddElevation((double)xpix * dpp, (double)ypix * dpp, height, 1);
		}

		fflush(stderr);

		n = fscanf(fd1, "%d, %d, %lf", &xpix, &ypix, &height);
		y++;

		rewind(fd2);

	} while (feof(fd1) == 0);

	fclose(fd1);
	fclose(fd2);
	unlink(tempname);
	return 0;
}
