#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include "tiles.hh"
#include "common.h"

#define MAX_LINE 50000

/* Computes the distance between two long/lat points */
double haversine_formula(double th1, double ph1, double th2, double ph2)
{
	#define TO_RAD (3.1415926536 / 180)
	int R = 6371;
	double dx, dy, dz;
	ph1 -= ph2;
	ph1 *= TO_RAD, th1 *= TO_RAD, th2 *= TO_RAD;
	dz = sin(th1) - sin(th2);
	dx = cos(ph1) * cos(th1) - cos(th2);
	dy = sin(ph1) * cos(th1);
	return asin(sqrt(dx * dx + dy * dy + dz * dz) / 2) * 2 * R;
}

int tile_load_lidar(tile_t *tile, char *filename){
	FILE *fd;
	char line[MAX_LINE];
	short nextval;
	char *pch;

	/* Clear the tile data */
	memset(tile, 0x00, sizeof(tile_t));

	/* Open the file handle and return on error */
	if ( (fd = fopen(filename,"r")) == NULL )
		return errno;

	/* This is where we read the header data */
	/* The string is split for readability but is parsed as a block */
	if( fscanf(fd,"%*s %d\n" "%*s %d\n" "%*s %lf\n" "%*s %lf\n" "%*s %lf\n" "%*s %d\n",&tile->width,&tile->height,&tile->xll,&tile->yll,&tile->cellsize,(int *)&tile->nodata) != 6 ){
		fclose(fd);
		return -1;
	}

	tile->datastart = ftell(fd);

	if(debug){
		fprintf(stderr,"w:%d h:%d s:%lf\n", tile->width, tile->height, tile->cellsize);
		fflush(stderr);
	}

	/* Set the filename */
	tile->filename = strdup(filename);

	/* Perform xur calcs */
	tile->xur = tile->xll+(tile->cellsize*tile->width);
	tile->yur = tile->yll+(tile->cellsize*tile->height);

	if (tile->xur > eastoffset)
		eastoffset = tile->xur;
	if (tile->xll < westoffset)
		westoffset = tile->xll;

	 if (debug)
	 	fprintf(stderr,"%d, %d, %.7f, %.7f, %.7f, %.7f, %.7f\n",tile->width,tile->height,tile->xll,tile->yll,tile->cellsize,tile->yur,tile->xur);

	// Greenwich straddling hack
	/* if (tile->xll <= 0 && tile->xur > 0) {
	 	tile->xll = (tile->xur - tile->xll); // full width
	 	tile->xur = 0.0; // budge it along so it's west of greenwich
	 	delta = eastoffset; // add to Tx longitude later
	 } else {*/
		// Transform WGS84 longitudes into 'west' values as society finishes east of Greenwich ;)
		if (tile->xll >= 0)
			tile->xll = 360-tile->xll;
		if(tile->xur >= 0)
			tile->xur = 360-tile->xur;
		if(tile->xll < 0)
			tile->xll = tile->xll * -1;
		if(tile->xur < 0)
			tile->xur = tile->xur * -1;
	// }

	if (debug)
		fprintf(stderr, "POST yll %.7f yur %.7f xur %.7f xll %.7f delta %.6f\n", tile->yll, tile->yur, tile->xur, tile->xll, delta);

	/* Read the actual tile data */
	/* Allocate the array for the lidar data */
	if ( (tile->data = (short*) calloc(tile->width * tile->height, sizeof(short))) == NULL ) {
		fclose(fd);
		free(tile->filename);
		return ENOMEM;
	}

	size_t loaded = 0;
	for (size_t h = 0; h < (unsigned)tile->height; h++) {
		if (fgets(line, MAX_LINE, fd) != NULL) {
			pch = strtok(line, " "); // split line into values
			for (size_t w = 0; w < (unsigned)tile->width && pch != NULL; w++) {
				/* If the data is less than a *magic* minimum, normalize it to zero */
				nextval = atoi(pch);
				if (nextval <= 0)
					nextval = 0;
				tile->data[h*tile->width + w] = nextval;
				loaded++;
				if ( nextval > tile->max_el )
					tile->max_el = nextval;
				if ( nextval < tile->min_el )
					tile->min_el = nextval;
				pch = strtok(NULL, " ");
			}//while
		} else {
			fprintf(stderr, "LIDAR error @ h %zu file %s\n", h, filename);
		}//if
	}

	double current_res_km = haversine_formula(tile->max_north, tile->max_west, tile->max_north, tile->min_west);
	tile->precise_resolution = (current_res_km/MAX(tile->width,tile->height)*1000);

	// Round to nearest 0.5
	tile->resolution = tile->precise_resolution < 0.5f ? 0.5f : ceil((tile->precise_resolution * 2)+0.5) / 2;

	// Positive westing
	tile->width_deg = tile->max_west - tile->min_west >= 0 ? tile->max_west - tile->min_west : tile->max_west + (360 - tile->min_west);
	tile->height_deg = tile->max_north - tile->min_north;

	tile->ppdx = tile->width / tile->width_deg;
	tile->ppdy = tile->height / tile->height_deg;

	if (debug)
		fprintf(stderr,"Pixels loaded: %zu/%d (PPD %dx%d, Res %f (%.2f))\n", loaded, tile->width*tile->height, tile->ppdx, tile->ppdy, tile->precise_resolution, tile->resolution);

	/* All done, close the LIDAR file */
	fclose(fd);

	return 0;
}

/*
 * tile_rescale
 * This is used to resample tile data. It is particularly designed for
 * use with LIDAR tiles where the resolution can be anything up to 2m.
 * This function is capable of merging neighbouring pixel values
 * The scaling factor is the distance to merge pixels.
 * NOTE: This means that new resolutions can only increment in multiples of the original
 * (ie 2m LIDAR can be 4/6/8/... and 20m can be 40/60)
 */
int tile_rescale(tile_t *tile, float scale){
	short *new_data;
	size_t skip_count = 1;
	size_t copy_count = 1;

	if (scale == 1) {
		return 0;	
	}

	size_t new_height = tile->height * scale;
	size_t new_width = tile->width * scale;

	/* Allocate the array for the lidar data */
	if ( (new_data = (short*) calloc(new_height * new_width, sizeof(short))) == NULL ) {
		return ENOMEM;
	}

	tile->max_el = -32768;
	tile->min_el = 32768;

	/* Making the tile data smaller */
	if (scale < 1) {
		skip_count = 1 / scale;
	} else {
		copy_count = (size_t) scale;
	}

	if (debug) {
		fprintf(stderr,"Resampling tile %s [%.1f]:\n\tOld %dx%d. New %zux%zu\n\tScale %f Skip %zu Copy %zu\n", tile->filename, tile->resolution, tile->width, tile->height, new_width, new_height, scale, skip_count, copy_count);
		fflush(stderr);
	}
	/* Nearest neighbour normalization. For each subsample of the original, simply
	 * assign the value in the top left to the new pixel 
	 * SOURCE: X / Y
	 * DEST:   I / J */

	for (size_t y = 0, j = 0; y < (unsigned)tile->height && j < new_height; y += skip_count, j += copy_count) {

		for (size_t x = 0, i = 0; x < (unsigned)tile->width && i < new_width; x += skip_count, i += copy_count) {
		
			/* These are for scaling up the data */
			for (size_t copy_y = 0; copy_y < copy_count; copy_y++) {
				for (size_t copy_x = 0; copy_x < copy_count; copy_x++) {
					size_t new_j = j + copy_y;
					size_t new_i = i + copy_x;
					/* Do the copy */
					new_data[ new_j * new_width + new_i ] = tile->data[y * tile->width + x];
				}
			}
			/* Update local min / max values */
			if (tile->data[y * tile->width + x] > tile->max_el)
				tile->max_el = tile->data[y * tile->width + x];
			if (tile->data[y * tile->width + x] < tile->min_el)
				tile->min_el = tile->data[y * tile->width + x];
		}
	}

	/* Update the date in the tile */
	free(tile->data);
	tile->data = new_data;

	/* Update the height and width values */
	tile->height = new_height;
	tile->width = new_width;
	tile->resolution *= 1/scale;	// A scale of 2 is HALF the resolution
	tile->ppdx = tile->width / tile->width_deg;
	tile->ppdy = tile->height / tile->height_deg;
	// tile->width_deg *= scale;
	// tile->height_deg *= scale;
	if (debug)
		fprintf(stderr, "Resampling complete. New resolution: %.1f\n", tile->resolution);

	return 0;
}

/*
 * tile_resize
 * This function works in conjuntion with resample_data. It takes a
 * resolution value in meters as its argument. It then calculates the
 * nearest (via averaging) resample value and calls resample_data
 */
int tile_resize(tile_t* tile, int resolution){
	double current_res_km = haversine_formula(tile->max_north, tile->max_west, tile->max_north, tile->min_west);
	int current_res = (int) ceil((current_res_km/IPPD)*1000);
	float scaling_factor = resolution / current_res;
	if (debug)
		fprintf(stderr, "Resampling: Current %dm Desired %dm Scale %.1f\n", current_res, resolution, scaling_factor);
	return tile_rescale(tile, scaling_factor);
}

/*
 * tile_destroy
 * This function simply destroys any data associated with a tile
 */
void tile_destroy(tile_t* tile){
	if (tile->data != NULL)
		free(tile->data);
}

