#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <bzlib.h>
#include <zlib.h>

#include "common.h"
#include "NativeApp.hh"
#include "inputs.hh"
#include "models/cost.hh"
#include "models/ecc33.hh"
#include "models/ericsson.hh"
#include "models/fspl.hh"
#include "models/hata.hh"
#include "models/itwom3.0.hh"
#include "models/sui.hh"
#include "image.hh"

void DoPathLoss(char *filename, unsigned char geo, unsigned char kml,
		unsigned char ngs, struct site *xmtr, unsigned char txsites)
{
	/* This function generates a topographic map in Portable Pix Map
	   (PPM) format based on the content of flags held in the mask[][]
	   array (only).  The image created is rotated counter-clockwise
	   90 degrees from its representation in dem[][] so that north
	   points up and east points right in the image generated. */

	char mapfile[255];
	unsigned red, green, blue, terrain = 0;
	unsigned char found, mask, cityorcounty;
	int indx, x, y, z, x0 = 0, y0 = 0, loss, match;
	double lat, lon, conversion, one_over_gamma, minwest;
	FILE *fd;
	image_ctx_t ctx;
	int success;

	if( (success = image_init(&ctx, width, (kml ? height : height + 30), IMAGE_RGB, IMAGE_DEFAULT)) != 0 ){
		fprintf(stderr,"Error initializing image: %s\n", strerror(success));
//		exit(success);
        return;
	}

	one_over_gamma = 1.0 / GAMMA;
	conversion =
	    255.0 / pow((double)(max_elevation - min_elevation),
			one_over_gamma);

	if( (success = LoadLossColors(xmtr[0])) != 0 ){
		fprintf(stderr,"Error loading loss colors\n");
//		exit(success);  // Now a fatal error!
        return;
	}

	if( filename != NULL ) {

		if (filename[0] == 0) {
			strncpy(filename, xmtr[0].filename, 254);
			filename[strlen(filename) - 4] = 0;	/* Remove .qth */
		}

		if(image_get_filename(&ctx,mapfile,sizeof(mapfile),filename) != 0){
			fprintf(stderr,"Error creating file name\n");
//			exit(1);
			return;
		}

		fd = fopen(mapfile,"wb");

	} else {

		fprintf(stderr,"Writing to stdout\n");
		fd = stdout;

	}

	minwest = ((double)min_west) + dpp;

	if (minwest > 360.0)
		minwest -= 360.0;

	north = (double)max_north - dpp;

	if (kml || geo)
		south = (double)min_north;	/* No bottom legend */
	else
		south = (double)min_north - (30.0 / ppd);	/* 30 pixels for bottom legend */

	east = (minwest < 180.0 ? -minwest : 360.0 - min_west);
	west = (double)(max_west < 180 ? -max_west : 360 - max_west);

	if (debug) {
		fprintf(stderr, "\nWriting \"%s\" (%ux%u pixmap image)...\n",
			filename != NULL ? mapfile : "to stdout", width, (kml ? height : height + 30));
		fflush(stderr);
	}

	for (y = 0, lat = north; y < (int)height;
	     y++, lat = north - (dpp * (double)y)) {
		for (x = 0, lon = max_west; x < (int)width;
		     x++, lon = max_west - (dpp * (double)x)) {
			if (lon < 0.0)
				lon += 360.0;

			for (indx = 0, found = 0;
			     indx < MAXPAGES && found == 0;) {
				x0 = (int)rint(ppd *
					       (lat -
						(double)dem[indx].min_north));
				y0 = mpi -
				    (int)rint(ppd *
					      (LonDiff
					       ((double)dem[indx].max_west,
						lon)));
				 // fix for multi-tile lidar
                              /*  if(width==10000 && (indx==1 || indx==3)){
                                        if(y0 >= 3432){ //3535
                                                y0=y0-3432;
                                        }
                                }*/


				if (x0 >= 0 && x0 <= mpi && y0 >= 0
				    && y0 <= mpi)
					found = 1;
				else
					indx++;
			}

			if (found) {
				mask = dem[indx].mask[x0][y0];
				loss = (dem[indx].signal[x0][y0]);
				cityorcounty = 0;

				match = 255;

				red = 0;
				green = 0;
				blue = 0;

				if (loss <= region.level[0])
					match = 0;
				else {
					for (z = 1;
					     (z < region.levels
					      && match == 255); z++) {
						if (loss >= region.level[z - 1]
						    && loss < region.level[z])
							match = z;
					}
				}

				if (match < region.levels) {
					red = region.color[match][0];
					green = region.color[match][1];
					blue = region.color[match][2];
				}

				if (mask & 2) {
					/* Text Labels: Red or otherwise */

					if (red >= 180 && green <= 75
					    && blue <= 75 && loss == 0)
						ADD_PIXEL(&ctx, 255 ^ red,
							255 ^ green,
							255 ^ blue);
					else
						ADD_PIXEL(&ctx, 255, 0,
							0);

					cityorcounty = 1;
				}

				else if (mask & 4) {
					/* County Boundaries: Black */

					ADD_PIXEL(&ctx, 0, 0, 0);

					cityorcounty = 1;
				}

				if (cityorcounty == 0) {
					if (loss == 0
					    || (contour_threshold != 0
						&& loss >
						abs(contour_threshold))) {
						if (ngs)	/* No terrain */
							ADD_PIXEL(&ctx, 
								255, 255, 255);
						else {
							/* Display land or sea elevation */

							if (dem[indx].
							    data[x0][y0] == 0)
								ADD_PIXEL(&ctx, 
									0, 0,
									170);
							else {
								terrain =
								    (unsigned)
								    (0.5 +
								     pow((double)(dem[indx].data[x0][y0] - min_elevation), one_over_gamma) * conversion);
								ADD_PIXEL(&ctx, 
									terrain,
									terrain,
									terrain);
							}
						}
					}

					else {
						/* Plot path loss in color */

						if (red != 0 || green != 0
						    || blue != 0)
							ADD_PIXEL(&ctx, 
								red, green,
								blue);

						else {	/* terrain / sea-level */

							if (dem[indx].
							    data[x0][y0] == 0)
								ADD_PIXEL(&ctx, 
									0, 0,
									170);
							else {
								/* Elevation: Greyscale */
								terrain =
								    (unsigned)
								    (0.5 +
								     pow((double)(dem[indx].data[x0][y0] - min_elevation), one_over_gamma) * conversion);
								ADD_PIXEL(&ctx, 
									terrain,
									terrain,
									terrain);
							}
						}
					}
				}
			}

			else {
				/* We should never get here, but if */
				/* we do, display the region as black */

				ADD_PIXEL(&ctx, 0, 0, 0);
			}
		}
	}

	if((success = image_write(&ctx,fd)) != 0){
		fprintf(stderr,"Error writing image\n");
//		exit(success);
	    return;
	}


	image_free(&ctx);

	if( filename != NULL ) {
		fclose(fd);
		fd = NULL;
	}

}

int DoSigStr(char *filename, unsigned char geo, unsigned char kml,
	      unsigned char ngs, struct site *xmtr, unsigned char txsites)
{
	/* This function generates a topographic map in Portable Pix Map
	   (PPM) format based on the signal strength values held in the
	   signal[][] array.  The image created is rotated counter-clockwise
	   90 degrees from its representation in dem[][] so that north
	   points up and east points right in the image generated. */

	char mapfile[255];
	unsigned terrain, red, green, blue;
	unsigned char found, mask, cityorcounty;
	int indx, x, y, z = 1, x0 = 0, y0 = 0, signal, match;
	double conversion, one_over_gamma, lat, lon, minwest;
	FILE *fd;
	image_ctx_t ctx;
	int success;

	if((success = image_init(&ctx, width, (kml ? height : height + 30), IMAGE_RGB, IMAGE_DEFAULT)) != 0){
		fprintf(stderr,"Error initializing image: %s\n", strerror(success));
//		exit(success);
	    return success;
	}

	one_over_gamma = 1.0 / GAMMA;
	conversion =
	    255.0 / pow((double)(max_elevation - min_elevation),
			one_over_gamma);

	if( (success = LoadSignalColors(xmtr[0])) != 0 ){
		fprintf(stderr,"Error loading signal colors\n");
		//exit(success);
		return success;
	}

	if( filename != NULL ) {

		if (filename[0] == 0) {
			strncpy(filename, xmtr[0].filename, 254);
			filename[strlen(filename) - 4] = 0;	/* Remove .qth */
		}

		if(image_get_filename(&ctx,mapfile,sizeof(mapfile),filename) != 0){
			fprintf(stderr,"Error creating file name\n");
//			exit(1);
			return 1;
		}

		fd = fopen(mapfile,"wb");

	} else {

		fprintf(stderr,"Writing to stdout\n");
		fd = stdout;

	}

	minwest = ((double)min_west) + dpp;

	if (minwest > 360.0)
		minwest -= 360.0;

	north = (double)max_north - dpp;

	south = (double)min_north;	/* No bottom legend */

	east = (minwest < 180.0 ? -minwest : 360.0 - min_west);
	west = (double)(max_west < 180 ? -max_west : 360 - max_west);

	if (debug) {
		fprintf(stderr, "\nWriting \"%s\" (%ux%u pixmap image)...\n",
			filename != NULL ? mapfile : "to stdout", width, (kml ? height : height + 30));
		fflush(stderr);
	}

	for (y = 0, lat = north; y < (int)height;
	     y++, lat = north - (dpp * (double)y)) {
		for (x = 0, lon = max_west; x < (int)width;
		     x++, lon = max_west - (dpp * (double)x)) {
			if (lon < 0.0)
				lon += 360.0;

			for (indx = 0, found = 0;
			     indx < MAXPAGES && found == 0;) {
				x0 = (int)rint(ppd *
					       (lat -
						(double)dem[indx].min_north));
				y0 = mpi -
				    (int)rint(ppd *
					      (LonDiff
					       ((double)dem[indx].max_west,
						lon)));

				 // fix for multi-tile lidar
                           /*     if(width==10000 && (indx==1 || indx==3)){
                                        if(y0 >= 3432){ //3535
                                                y0=y0-3432;
                                        }
                                }
				*/

				if (x0 >= 0 && x0 <= mpi && y0 >= 0
				    && y0 <= mpi)
					found = 1;
				else
					indx++;
			}

			if (found) {
				mask = dem[indx].mask[x0][y0];
				signal = (dem[indx].signal[x0][y0]) - 100;
				cityorcounty = 0;
				match = 255;

				red = 0;
				green = 0;
				blue = 0;

				if (signal >= region.level[0])
					match = 0;
				else {
					for (z = 1;
					     (z < region.levels
					      && match == 255); z++) {
						if (signal < region.level[z - 1]
						    && signal >=
						    region.level[z])
							match = z;
					}
				}

				if (match < region.levels) {
					red = region.color[match][0];
					green = region.color[match][1];
					blue = region.color[match][2];
				}

				if (mask & 2) {
					/* Text Labels: Red or otherwise */

					if (red >= 180 && green <= 75
					    && blue <= 75)
						ADD_PIXEL(&ctx, 255 ^ red,
							255 ^ green,
							255 ^ blue);
					else
						ADD_PIXEL(&ctx, 255, 0,
							0);

					cityorcounty = 1;
				}

				else if (mask & 4) {
					/* County Boundaries: Black */

					ADD_PIXEL(&ctx, 0, 0, 0);

					cityorcounty = 1;
				}

				if (cityorcounty == 0) {
					if (contour_threshold != 0
					    && signal < contour_threshold) {
						if (ngs)
							ADD_PIXEL(&ctx, 
								255, 255, 255);
						else {
							/* Display land or sea elevation */

							if (dem[indx].
							    data[x0][y0] == 0)
								ADD_PIXEL(&ctx, 
									0, 0,
									170);
							else {
								terrain =
								    (unsigned)
								    (0.5 +
								     pow((double)(dem[indx].data[x0][y0] - min_elevation), one_over_gamma) * conversion);
								ADD_PIXEL(&ctx, 
									terrain,
									terrain,
									terrain);
							}
						}
					}

					else {
						/* Plot field strength regions in color */

						if (red != 0 || green != 0
						    || blue != 0)
							ADD_PIXEL(&ctx, 
								red, green,
								blue);

						else {	/* terrain / sea-level */

							if (ngs)
								ADD_PIXEL(&ctx, 
									255,
									255,
									255);
							else {
								if (dem[indx].
								    data[x0][y0]
								    == 0)
									ADD_PIXEL(&ctx, 
									     0,
									     0,
									     170);
								else {
									/* Elevation: Greyscale */
									terrain
									    =
									    (unsigned)
									    (0.5
									     +
									     pow
									     ((double)(dem[indx].data[x0][y0] - min_elevation), one_over_gamma) * conversion);
									ADD_PIXEL(&ctx, 
									     terrain,
									     terrain,
									     terrain);
								}
							}
						}
					}
				}
			}

			else {
				/* We should never get here, but if */
				/* we do, display the region as black */

				ADD_PIXEL(&ctx, 255, 255, 255);
			}
		}
	}

	if((success = image_write(&ctx,fd)) != 0){
		fprintf(stderr,"Error writing image\n");
//		exit(success);
        return success;
	}

	image_free(&ctx);

	if( filename != NULL ) {
		fclose(fd);
		fd = NULL;
	}
	return 0;
}

void DoRxdPwr(char *filename, unsigned char geo, unsigned char kml,
	      unsigned char ngs, struct site *xmtr, unsigned char txsites)
{
	/* This function generates a topographic map in Portable Pix Map
	   (PPM) format based on the signal power level values held in the
	   signal[][] array.  The image created is rotated counter-clockwise
	   90 degrees from its representation in dem[][] so that north
	   points up and east points right in the image generated. */

	char mapfile[255];
	unsigned terrain, red, green, blue;
	unsigned char found, mask, cityorcounty;
	int indx, x, y, z = 1, x0 = 0, y0 = 0, dBm, match;
	double conversion, one_over_gamma, lat, lon, minwest;
	FILE *fd;
	image_ctx_t ctx;
	int success;

	if( (success = image_init(&ctx, width, (kml ? height : height + 30), IMAGE_RGB, IMAGE_DEFAULT)) != 0 ){
		fprintf(stderr,"Error initializing image: %s\n", strerror(success));
//		exit(success);
		return;
	}

	one_over_gamma = 1.0 / GAMMA;
	conversion =
	    255.0 / pow((double)(max_elevation - min_elevation),
			one_over_gamma);

	if( (success = LoadDBMColors(xmtr[0])) != 0 ){
		fprintf(stderr,"Error loading DBM colors\n");
//		exit(success);  //Now a fatal error!
	    return;
	}

	if( filename != NULL ) {

		if (filename[0] == 0) {
			strncpy(filename, xmtr[0].filename, 254);
			filename[strlen(filename) - 4] = 0;	/* Remove .qth */
		}

		if(image_get_filename(&ctx,mapfile,sizeof(mapfile),filename) != 0){
			fprintf(stderr,"Error creating file name\n");
//			exit(1);
		    return;
		}

		fd = fopen(mapfile,"wb");

	} else {

		fprintf(stderr,"Writing to stdout\n");
		fd = stdout;

	}

	minwest = ((double)min_west) + dpp;

	if (minwest > 360.0)
		minwest -= 360.0;

	north = (double)max_north - dpp;

	south = (double)min_north;	/* No bottom legend */

	east = (minwest < 180.0 ? -minwest : 360.0 - min_west);
	west = (double)(max_west < 180 ? -max_west : 360 - max_west);

	if (debug) {
		fprintf(stderr, "\nWriting \"%s\" (%ux%u pixmap image)...\n",
			(filename != NULL ? mapfile : "to stdout"), width, (kml ? height : height));
		fflush(stderr);
	}

	// Draw image of x by y pixels
	for (y = 0, lat = north; y < (int)height;
	     y++, lat = north - (dpp * (double)y)) {
		for (x = 0, lon = max_west; x < (int)width;
		     x++, lon = max_west - (dpp * (double)x)) {
			if (lon < 0.0)
				lon += 360.0;

			for (indx = 0, found = 0;
			     indx < MAXPAGES && found == 0;) {

				x0 = (int)rint((ppd *
					      (lat -
						(double)dem[indx].min_north))); 
				y0 = mpi -
				    (int)rint(ppd * 
					      (LonDiff
					       ((double)dem[indx].max_west,lon)));


				if (x0 >= 0 && x0 <= mpi && y0 >= 0
				    && y0 <= mpi)
					found = 1;
				else
					indx++;


			}

			if (found) {
				mask = dem[indx].mask[x0][y0];
				dBm = (dem[indx].signal[x0][y0]) - 200;
				cityorcounty = 0;
				match = 255;

				red = 0;
				green = 0;
				blue = 0;

				if (dBm >= region.level[0])
					match = 0;
				else {
					for (z = 1;
					     (z < region.levels
					      && match == 255); z++) {
						if (dBm < region.level[z - 1]
						    && dBm >= region.level[z])
							match = z;
					}
				}

				if (match < region.levels) {
					red = region.color[match][0];
					green = region.color[match][1];
					blue = region.color[match][2];
				}

				if (mask & 2) {
					/* Text Labels: Red or otherwise */

					if (red >= 180 && green <= 75
					    && blue <= 75 && dBm != 0)
						ADD_PIXEL(&ctx, 255 ^ red,
							255 ^ green,
							255 ^ blue);
					else
						ADD_PIXEL(&ctx, 255, 0,
							0);

					cityorcounty = 1;
				}

				else if (mask & 4) {
					/* County Boundaries: Black */
					ADD_PIXEL(&ctx, 0, 0, 0);
					cityorcounty = 1;
				}

				if (cityorcounty == 0) {
					if (contour_threshold != 0
					    && dBm < contour_threshold) {
						if (ngs)	/* No terrain */
							ADD_PIXEL(&ctx,
								255, 255, 255);
						else {
							/* Display land or sea elevation */

							if (dem[indx].
							    data[x0][y0] == 0)
								ADD_PIXEL(&ctx,
									0, 0,
									170);
							else {
								terrain =
								    (unsigned)
								    (0.5 +
								     pow((double)(dem[indx].data[x0][y0] - min_elevation), one_over_gamma) * conversion);
								ADD_PIXEL(&ctx,
									terrain,
									terrain,
									terrain);
							}
						}
					}

					else {
						/* Plot signal power level regions in color */

						if (red != 0 || green != 0
						    || blue != 0)
							ADD_PIXEL(&ctx,
								red, green,
								blue);

						else {	/* terrain / sea-level */

							if (ngs)
								ADD_PIXEL(&ctx, 
									255,
									255,
									255); // WHITE
							else {
								if (dem[indx].
								    data[x0][y0]
								    == 0)
									ADD_PIXEL(&ctx, 
									     0,
									     0,
									     170); // BLUE
								else {
									/* Elevation: Greyscale */
									terrain
									    =
									    (unsigned)
									    (0.5
									     +
									     pow
									     ((double)(dem[indx].data[x0][y0] - min_elevation), one_over_gamma) * conversion);
									ADD_PIXEL(&ctx, 
									     terrain,
									     terrain,
									     terrain);
								}
							}
						}
					}
				}
			}

			else {
				/* We should never get here, but if */
				/* we do, display the region as black */

				ADD_PIXEL(&ctx, 255, 255, 255);
			}
		}
	}

	if((success = image_write(&ctx,fd)) != 0){
		fprintf(stderr,"Error writing image\n");
//		exit(success);
        return;
	}

	fflush(fd);

	image_free(&ctx);

	if( filename != NULL ) {
		fclose(fd);
		fd = NULL;
	}

}

void DoLOS(char *filename, unsigned char geo, unsigned char kml,
	   unsigned char ngs, struct site *xmtr, unsigned char txsites)
{
	/* This function generates a topographic map in Portable Pix Map
	   (PPM) format based on the signal power level values held in the
	   signal[][] array.  The image created is rotated counter-clockwise
	   90 degrees from its representation in dem[][] so that north
	   points up and east points right in the image generated. */

	char mapfile[255];
	unsigned terrain;
	unsigned char found, mask;
	int indx, x, y, x0 = 0, y0 = 0;
	double conversion, one_over_gamma, lat, lon, minwest;
	FILE *fd;
	image_ctx_t ctx;
	int success;

	if((success = image_init(&ctx, width, (kml ? height : height + 30), IMAGE_RGB, IMAGE_DEFAULT)) != 0){
		fprintf(stderr,"Error initializing image: %s\n", strerror(success));
//		exit(success);
	    return;
	}

	one_over_gamma = 1.0 / GAMMA;
	conversion =
	    255.0 / pow((double)(max_elevation - min_elevation),
			one_over_gamma);

	if( filename != NULL ){

		if (filename[0] == 0) {
			strncpy(filename, xmtr[0].filename, 254);
			filename[strlen(filename) - 4] = 0;	/* Remove .qth */
		}

		if(image_get_filename(&ctx,mapfile,sizeof(mapfile),filename) != 0){
			fprintf(stderr,"Error creating file name\n");
//			exit(1);
		    return;
		}

		fd = fopen(mapfile,"wb");

	} else {
		
		fprintf(stderr,"Writing to stdout\n");
		fd = stdout;

	}

	minwest = ((double)min_west) + dpp;

	if (minwest > 360.0)
		minwest -= 360.0;

	north = (double)max_north - dpp;

	south = (double)min_north;	/* No bottom legend */

	east = (minwest < 180.0 ? -minwest : 360.0 - min_west);
	west = (double)(max_west < 180 ? -max_west : 360 - max_west);

	if (debug) {
		fprintf(stderr, "\nWriting \"%s\" (%ux%u pixmap image)...\n",
			filename != NULL ? mapfile : "to stdout", width, (kml ? height : height + 30));
		fflush(stderr);
	}

	for (y = 0, lat = north; y < (int)height;
	     y++, lat = north - (dpp * (double)y)) {
		for (x = 0, lon = max_west; x < (int)width;
		     x++, lon = max_west - (dpp * (double)x)) {
			if (lon < 0.0)
				lon += 360.0;

			for (indx = 0, found = 0;
			     indx < MAXPAGES && found == 0;) {
				x0 = (int)rint(ppd *
					       (lat -
						(double)dem[indx].min_north));
				y0 = mpi -
				    (int)rint(ppd *
					      (LonDiff
					       ((double)dem[indx].max_west,
						lon)));

				if (x0 >= 0 && x0 <= mpi && y0 >= 0
				    && y0 <= mpi)
					found = 1;
				else
					indx++;
			}

			if (found) {
				mask = dem[indx].mask[x0][y0];

				if (mask & 2)
					/* Text Labels: Red */
					ADD_PIXEL(&ctx, 255, 0, 0);

				else if (mask & 4)
					/* County Boundaries: Light Cyan */
					ADD_PIXEL(&ctx, 128, 128, 255);

				else
					switch (mask & 57) {
					case 1:
						/* TX1: Green */
						ADD_PIXEL(&ctx, 0, 255,
							0);
						break;

					case 8:
						/* TX2: Cyan */
						ADD_PIXEL(&ctx, 0, 255,
							255);
						break;

					case 9:
						/* TX1 + TX2: Yellow */
						ADD_PIXEL(&ctx, 255, 255,
							0);
						break;

					case 16:
						/* TX3: Medium Violet */
						ADD_PIXEL(&ctx, 147, 112,
							219);
						break;

					case 17:
						/* TX1 + TX3: Pink */
						ADD_PIXEL(&ctx, 255, 192,
							203);
						break;

					case 24:
						/* TX2 + TX3: Orange */
						ADD_PIXEL(&ctx, 255, 165,
							0);
						break;

					case 25:
						/* TX1 + TX2 + TX3: Dark Green */
						ADD_PIXEL(&ctx, 0, 100,
							0);
						break;

					case 32:
						/* TX4: Sienna 1 */
						ADD_PIXEL(&ctx, 255, 130,
							71);
						break;

					case 33:
						/* TX1 + TX4: Green Yellow */
						ADD_PIXEL(&ctx, 173, 255,
							47);
						break;

					case 40:
						/* TX2 + TX4: Dark Sea Green 1 */
						ADD_PIXEL(&ctx, 193, 255,
							193);
						break;

					case 41:
						/* TX1 + TX2 + TX4: Blanched Almond */
						ADD_PIXEL(&ctx, 255, 235,
							205);
						break;

					case 48:
						/* TX3 + TX4: Dark Turquoise */
						ADD_PIXEL(&ctx, 0, 206,
							209);
						break;

					case 49:
						/* TX1 + TX3 + TX4: Medium Spring Green */
						ADD_PIXEL(&ctx, 0, 250,
							154);
						break;

					case 56:
						/* TX2 + TX3 + TX4: Tan */
						ADD_PIXEL(&ctx, 210, 180,
							140);
						break;

					case 57:
						/* TX1 + TX2 + TX3 + TX4: Gold2 */
						ADD_PIXEL(&ctx, 238, 201,
							0);
						break;

					default:
						if (ngs)	/* No terrain */
							ADD_PIXEL(&ctx, 
								255, 255, 255);
						else {
							/* Sea-level: Medium Blue */
							if (dem[indx].
							    data[x0][y0] == 0)
								ADD_PIXEL(&ctx, 
									0, 0,
									170);
							else {
								/* Elevation: Greyscale */
								terrain =
								    (unsigned)
								    (0.5 +
								     pow((double)(dem[indx].data[x0][y0] - min_elevation), one_over_gamma) * conversion);
								ADD_PIXEL(&ctx, 
									terrain,
									terrain,
									terrain);
							}
						}
					}
			}

			else {
				/* We should never get here, but if */
				/* we do, display the region as black */

				ADD_PIXEL(&ctx, 255, 255, 255);
			}
		}
	}

	if((success = image_write(&ctx,fd)) != 0){
		fprintf(stderr,"Error writing image\n");
//		exit(success);
	    return;
	}

	image_free(&ctx);

	if( filename != NULL) {
		fclose(fd);
		fd = NULL;
	}

}

void PathReport(struct site source, struct site destination, char *name,
		char graph_it, int propmodel, int pmenv, double rxGain)
{
	/* This function writes a PPA Path Report (name.txt) to
	   the filesystem.  If (graph_it == 1), then gnuplot is invoked
	   to generate an appropriate output file indicating the Longley-Rice
	   model loss between the source and destination locations.
	   "filename" is the name assigned to the output file generated
	   by gnuplot.  The filename extension is used to set gnuplot's
	   terminal setting and output file type.  If no extension is
	   found, .png is assumed. */

	int x, y, z, errnum;
	char basename[255], term[30], ext[15], strmode[100],
	    report_name[80], block = 0;
	double maxloss = -100000.0, minloss = 100000.0, angle1, angle2,
	    azimuth, pattern = 1.0, patterndB = 0.0,
	    total_loss = 0.0, cos_xmtr_angle, cos_test_angle = 0.0,
	    source_alt, test_alt, dest_alt, source_alt2, dest_alt2,
	    distance, elevation, four_thirds_earth,
	    free_space_loss = 0.0, eirp =
	    0.0, voltage, rxp, power_density, dkm;
	FILE *fd = NULL, *fd2 = NULL;

	snprintf(report_name, 80, "%s.txt%c", name, 0);
	four_thirds_earth = FOUR_THIRDS * EARTHRADIUS;

	fd2 = fopen(report_name, "w");

	fprintf(fd2, "\n\t\t--==[ Path Profile Analysis ]==--\n\n");
	fprintf(fd2, "Transmitter site: %s\n", source.name);

	if (source.lat >= 0.0) {

		if (source.lon <= 180){
			fprintf(fd2, "Site location: %.4f, -%.4f\n",source.lat, source.lon);
		}else{
			fprintf(fd2, "Site location: %.4f, %.4f\n",source.lat, 360 - source.lon);
		}
	}

	else {

		if (source.lon <= 180){
			fprintf(fd2, "Site location: %.4f, -%.4f\n",source.lat, source.lon);
		}else{
			fprintf(fd2, "Site location: %.4f, %.4f\n",source.lat, 360 - source.lon);
		}
	}

	if (metric) {
		fprintf(fd2, "Ground elevation: %.2f meters AMSL\n",
			METERS_PER_FOOT * GetElevation(source));
		fprintf(fd2,
			"Antenna height: %.2f meters AGL / %.2f meters AMSL\n",
			METERS_PER_FOOT * source.alt,
			METERS_PER_FOOT * (source.alt + GetElevation(source)));
	}

	else {
		fprintf(fd2, "Ground elevation: %.2f feet AMSL\n",
			GetElevation(source));
		fprintf(fd2, "Antenna height: %.2f feet AGL / %.2f feet AMSL\n",
			source.alt, source.alt + GetElevation(source));
	}

	azimuth = Azimuth(source, destination);
	angle1 = ElevationAngle(source, destination);
	angle2 = ElevationAngle2(source, destination, earthradius);

	if (got_azimuth_pattern || got_elevation_pattern) {
		x = (int)rint(10.0 * (10.0 - angle2));

		if (x >= 0 && x <= 1000)
			pattern =
			    (double)LR.antenna_pattern[(int)rint(azimuth)][x];

		patterndB = 20.0 * log10(pattern);
	}

	if (metric)
		fprintf(fd2, "Distance to %s: %.2f kilometers\n",
			destination.name, KM_PER_MILE * Distance(source,
								 destination));

	else
		fprintf(fd2, "Distance to %s: %.2f miles\n", destination.name,
			Distance(source, destination));

	fprintf(fd2, "Azimuth to %s: %.2f degrees grid\n", destination.name,
		azimuth);


	fprintf(fd2, "Downtilt angle to %s: %+.4f degrees\n",
		destination.name, angle1);



	/* Receiver */

	fprintf(fd2, "\nReceiver site: %s\n", destination.name);

	if (destination.lat >= 0.0) {

		if (destination.lon <= 180){
			fprintf(fd2, "Site location: %.4f, -%.4f\n",destination.lat, destination.lon);
		}else{
			fprintf(fd2, "Site location: %.4f, %.4f\n",destination.lat, 360 - destination.lon);
		}
	}

	else {

		if (destination.lon <= 180){
			fprintf(fd2, "Site location: %.4f, -%.4f\n",destination.lat, destination.lon);
		}else{
			fprintf(fd2, "Site location: %.4f, %.4f\n",destination.lat, 360 - destination.lon);
		}
	}

	if (metric) {
		fprintf(fd2, "Ground elevation: %.2f meters AMSL\n",
			METERS_PER_FOOT * GetElevation(destination));
		fprintf(fd2,
			"Antenna height: %.2f meters AGL / %.2f meters AMSL\n",
			METERS_PER_FOOT * destination.alt,
			METERS_PER_FOOT * (destination.alt +
					   GetElevation(destination)));
	}

	else {
		fprintf(fd2, "Ground elevation: %.2f feet AMSL\n",
			GetElevation(destination));
		fprintf(fd2, "Antenna height: %.2f feet AGL / %.2f feet AMSL\n",
			destination.alt,
			destination.alt + GetElevation(destination));
	}

	if (metric)
		fprintf(fd2, "Distance to %s: %.2f kilometers\n", source.name,
			KM_PER_MILE * Distance(source, destination));

	else
		fprintf(fd2, "Distance to %s: %.2f miles\n", source.name,
			Distance(source, destination));

	azimuth = Azimuth(destination, source);

	angle1 = ElevationAngle(destination, source);
	angle2 = ElevationAngle2(destination, source, earthradius);

	fprintf(fd2, "Azimuth to %s: %.2f degrees grid\n", source.name, azimuth);


	fprintf(fd2, "Downtilt angle to %s: %+.4f degrees\n",
		source.name, angle1);

	if (LR.frq_mhz > 0.0) {
		fprintf(fd2, "\n\nPropagation model: ");

		switch (propmodel) {
		case 1:
			fprintf(fd2, "Irregular Terrain Model\n");
			break;
		case 2:
			fprintf(fd2, "Line of sight\n");
			break;
		case 3:
			fprintf(fd2, "Okumura-Hata\n");
			break;
		case 4:
			fprintf(fd2, "ECC33 (ITU-R P.529)\n");
			break;
		case 5:
			fprintf(fd2, "Stanford University Interim\n");
			break;
		case 6:
			fprintf(fd2, "COST231-Hata\n");
			break;
		case 7:
			fprintf(fd2, "Free space path loss (ITU-R.525)\n");
			break;
		case 8:
			fprintf(fd2, "ITWOM 3.0\n");
			break;
		case 9:
			fprintf(fd2, "Ericsson\n");
			break;
		}

		fprintf(fd2, "Model sub-type: ");

		switch (pmenv) {
		case 1:
			fprintf(fd2, "City / Conservative\n");
			break;
		case 2:
			fprintf(fd2, "Suburban / Average\n");
			break;
		case 3:
			fprintf(fd2, "Rural / Optimistic\n");
			break;
		}
		fprintf(fd2, "Earth's Dielectric Constant: %.3lf\n",
			LR.eps_dielect);
		fprintf(fd2, "Earth's Conductivity: %.3lf Siemens/meter\n",
			LR.sgm_conductivity);
		fprintf(fd2,
			"Atmospheric Bending Constant (N-units): %.3lf ppm\n",
			LR.eno_ns_surfref);
		fprintf(fd2, "Frequency: %.3lf MHz\n", LR.frq_mhz);
		fprintf(fd2, "Radio Climate: %d (", LR.radio_climate);

		switch (LR.radio_climate) {
		case 1:
			fprintf(fd2, "Equatorial");
			break;

		case 2:
			fprintf(fd2, "Continental Subtropical");
			break;

		case 3:
			fprintf(fd2, "Maritime Subtropical");
			break;

		case 4:
			fprintf(fd2, "Desert");
			break;

		case 5:
			fprintf(fd2, "Continental Temperate");
			break;

		case 6:
			fprintf(fd2, "Maritime Temperate, Over Land");
			break;

		case 7:
			fprintf(fd2, "Maritime Temperate, Over Sea");
			break;

		default:
			fprintf(fd2, "Unknown");
		}

		fprintf(fd2, ")\nPolarisation: %d (", LR.pol);

		if (LR.pol == 0)
			fprintf(fd2, "Horizontal");

		if (LR.pol == 1)
			fprintf(fd2, "Vertical");

		fprintf(fd2, ")\nFraction of Situations: %.1lf%c\n",
			LR.conf * 100.0, 37);
		fprintf(fd2, "Fraction of Time: %.1lf%c\n", LR.rel * 100.0, 37);

		if (LR.erp != 0.0) {
			fprintf(fd2, "\nReceiver gain: %.1f dBd / %.1f dBi\n", rxGain, rxGain+2.14);
			fprintf(fd2, "Transmitter ERP plus Receiver gain: ");

			if (LR.erp < 1.0)
				fprintf(fd2, "%.1lf milliwatts",
					1000.0 * LR.erp);

			if (LR.erp >= 1.0 && LR.erp < 10.0)
				fprintf(fd2, "%.1lf Watts", LR.erp);

			if (LR.erp >= 10.0 && LR.erp < 10.0e3)
				fprintf(fd2, "%.0lf Watts", LR.erp);

			if (LR.erp >= 10.0e3)
				fprintf(fd2, "%.3lf kilowatts", LR.erp / 1.0e3);

			dBm = 10.0 * (log10(LR.erp * 1000.0));
			fprintf(fd2, " (%+.2f dBm)\n", dBm);
			fprintf(fd2, "Transmitter ERP minus Receiver gain: %.2f dBm\n", dBm-rxGain);

			/* EIRP = ERP + 2.14 dB */

			fprintf(fd2, "Transmitter EIRP plus Receiver gain: ");

			eirp = LR.erp * 1.636816521;

			if (eirp < 1.0)
				fprintf(fd2, "%.1lf milliwatts", 1000.0 * eirp);

			if (eirp >= 1.0 && eirp < 10.0)
				fprintf(fd2, "%.1lf Watts", eirp);

			if (eirp >= 10.0 && eirp < 10.0e3)
				fprintf(fd2, "%.0lf Watts", eirp);

			if (eirp >= 10.0e3)
				fprintf(fd2, "%.3lf kilowatts", eirp / 1.0e3);

			dBm = 10.0 * (log10(eirp * 1000.0));
			fprintf(fd2, " (%+.2f dBm)\n", dBm);

			// Rx gain
			fprintf(fd2, "Transmitter EIRP minus Receiver gain: %.2f dBm\n", dBm-rxGain);
		}

		fprintf(fd2, "\nSummary for the link between %s and %s:\n\n",
			source.name, destination.name);

		if (patterndB != 0.0)
		        fprintf(fd2, "%s antenna pattern towards %s: %.3f (%.2f dB)\n",
				source.name, destination.name, pattern,
				patterndB);

		ReadPath(source, destination);	/* source=TX, destination=RX */

		/* Copy elevations plus clutter along
		   path into the elev[] array. */

		for (x = 1; x < path.length - 1; x++)
			elev[x + 2] =
			    METERS_PER_FOOT * (path.elevation[x] ==
					       0.0 ? path.
					       elevation[x] : (clutter +
							       path.
							       elevation[x]));

		/* Copy ending points without clutter */

		elev[2] = path.elevation[0] * METERS_PER_FOOT;
		elev[path.length + 1] =
		    path.elevation[path.length - 1] * METERS_PER_FOOT;

		azimuth = rint(Azimuth(source, destination));

		for (y = 2; y < (path.length - 1); y++) {	/* path.length-1 avoids LR error */
			distance = FEET_PER_MILE * path.distance[y];

			source_alt = four_thirds_earth + source.alt + path.elevation[0];
			dest_alt = four_thirds_earth + destination.alt +
			    path.elevation[y];
			dest_alt2 = dest_alt * dest_alt;
			source_alt2 = source_alt * source_alt;

			/* Calculate the cosine of the elevation of
			   the receiver as seen by the transmitter. */

			cos_xmtr_angle =
			    ((source_alt2) + (distance * distance) -
			     (dest_alt2)) / (2.0 * source_alt * distance);

			if (got_elevation_pattern) {
				/* If an antenna elevation pattern is available, the
				   following code determines the elevation angle to
				   the first obstruction along the path. */

				for (x = 2, block = 0; x < y && block == 0; x++) {
					distance =
					    FEET_PER_MILE * (path.distance[y] -
						      path.distance[x]);
					test_alt =
					    four_thirds_earth +
					    path.elevation[x];

					/* Calculate the cosine of the elevation
					   angle of the terrain (test point)
					   as seen by the transmitter. */

					cos_test_angle =
					    ((source_alt2) +
					     (distance * distance) -
					     (test_alt * test_alt)) / (2.0 *
								       source_alt
								       *
								       distance);

					/* Compare these two angles to determine if
					   an obstruction exists.  Since we're comparing
					   the cosines of these angles rather than
					   the angles themselves, the sense of the
					   following "if" statement is reversed from
					   what it would be if the angles themselves
					   were compared. */

					if (cos_xmtr_angle >= cos_test_angle)
						block = 1;
				}

				/* At this point, we have the elevation angle
				   to the first obstruction (if it exists). */
			}

			/* Determine path loss for each point along the
			   path using Longley-Rice's point_to_point mode
			   starting at x=2 (number_of_points = 1), the
			   shortest distance terrain can play a role in
			   path loss. */

			elev[0] = y - 1;	/* (number of points - 1) */

			/* Distance between elevation samples */

			elev[1] =
			    METERS_PER_MILE * (path.distance[y] -
					       path.distance[y - 1]);

			/*
			   point_to_point(elev, source.alt*METERS_PER_FOOT,
			   destination.alt*METERS_PER_FOOT, LR.eps_dielect,
			   LR.sgm_conductivity, LR.eno_ns_surfref, LR.frq_mhz,
			   LR.radio_climate, LR.pol, LR.conf, LR.rel, loss,
			   strmode, errnum);
			 */
			dkm = (elev[1] * elev[0]) / 1000;	// km

			switch (propmodel) {
			case 1:
				// Longley Rice ITM
				point_to_point_ITM(source.alt * METERS_PER_FOOT,
						   destination.alt *
						   METERS_PER_FOOT,
						   LR.eps_dielect,
						   LR.sgm_conductivity,
						   LR.eno_ns_surfref,
						   LR.frq_mhz, LR.radio_climate,
						   LR.pol, LR.conf, LR.rel,
						   loss, strmode, errnum);
				break;
			case 3:
				//HATA 1, 2 & 3
				loss =
				    HATApathLoss(LR.frq_mhz, source.alt * METERS_PER_FOOT,
						 (path.elevation[y] * METERS_PER_FOOT) +
						 (destination.alt * METERS_PER_FOOT), dkm, pmenv);
				break;
			case 4:
				// COST231-HATA
				loss =
				    ECC33pathLoss(LR.frq_mhz, source.alt * METERS_PER_FOOT,
						  (path.elevation[y] * METERS_PER_FOOT) +
						  (destination.alt * METERS_PER_FOOT), dkm, pmenv);
				break;
			case 5:
				// SUI
				loss =
				    SUIpathLoss(LR.frq_mhz, source.alt * METERS_PER_FOOT,
						(path.elevation[y] * METERS_PER_FOOT) +
						(destination.alt * METERS_PER_FOOT), dkm, pmenv);
				break;
			case 6:
				loss =
				    COST231pathLoss(LR.frq_mhz, source.alt * METERS_PER_FOOT,
						    (path.elevation[y] * METERS_PER_FOOT) +
						    (destination.alt * METERS_PER_FOOT), dkm,pmenv);
				break;
			case 7:
				// ITU-R P.525 Free space path loss
				loss = FSPLpathLoss(LR.frq_mhz, dkm);
				break;
			case 8:
				// ITWOM 3.0
				point_to_point(source.alt * METERS_PER_FOOT,
					       destination.alt *
					       METERS_PER_FOOT, LR.eps_dielect,
					       LR.sgm_conductivity,
					       LR.eno_ns_surfref, LR.frq_mhz,
					       LR.radio_climate, LR.pol,
					       LR.conf, LR.rel, loss, strmode,
					       errnum);
				break;
			case 9:
				// Ericsson
				loss =
				    EricssonpathLoss(LR.frq_mhz, source.alt * METERS_PER_FOOT,
						     (path.elevation[y] * METERS_PER_FOOT) +
						     (destination.alt *
						      METERS_PER_FOOT), dkm,
						     pmenv);
				break;

			default:
				point_to_point_ITM(source.alt * METERS_PER_FOOT,
						   destination.alt *
						   METERS_PER_FOOT,
						   LR.eps_dielect,
						   LR.sgm_conductivity,
						   LR.eno_ns_surfref,
						   LR.frq_mhz, LR.radio_climate,
						   LR.pol, LR.conf, LR.rel,
						   loss, strmode, errnum);

			}

			if (block)
				elevation =
				    ((acos(cos_test_angle)) / DEG2RAD) - 90.0;
			else
				elevation =
				    ((acos(cos_xmtr_angle)) / DEG2RAD) - 90.0;

			/* Integrate the antenna's radiation
			   pattern into the overall path loss. */

			x = (int)rint(10.0 * (10.0 - elevation));

			if (x >= 0 && x <= 1000) {
				pattern =
				    (double)LR.antenna_pattern[(int)azimuth][x];

				if (pattern != 0.0){
					patterndB = 20.0 * log10(pattern);
				}else{
					patterndB = 0.0;
				}
			}

			else
				patterndB = 0.0;

			total_loss = loss - patterndB;

			if (total_loss > maxloss)
				maxloss = total_loss;

			if (total_loss < minloss)
				minloss = total_loss;
		}

		distance = Distance(source, destination);

		if (distance != 0.0) {
			free_space_loss =
			    36.6 + (20.0 * log10(LR.frq_mhz)) +
			    (20.0 * log10(distance));
			fprintf(fd2, "Free space path loss: %.2f dB\n",
				free_space_loss);
		}

		fprintf(fd2, "Computed path loss: %.2f dB\n", loss);


                if((loss*1.5) < free_space_loss){
			fprintf(fd2,"Model error! Computed loss of %.1fdB is greater than free space loss of %.1fdB. Check your inuts for model %d\n",loss,free_space_loss,propmodel);
                        fprintf(stderr,"Model error! Computed loss of %.1fdB is greater than free space loss of %.1fdB. Check your inuts for model %d\n",loss,free_space_loss,propmodel);
                        return;
                }

		if (free_space_loss != 0.0)
			fprintf(fd2,
				"Attenuation due to terrain shielding: %.2f dB\n",
				loss - free_space_loss);

		if (patterndB != 0.0)
		        fprintf(fd2,"Total path loss including %s antenna pattern: %.2f dB\n",
				source.name, total_loss);

		if (LR.erp != 0.0) {
			field_strength =
			    (139.4 + (20.0 * log10(LR.frq_mhz)) - total_loss) +
			    (10.0 * log10(LR.erp / 1000.0));

			/* dBm is referenced to EIRP */

			rxp = eirp / (pow(10.0, (total_loss / 10.0)));
			dBm = 10.0 * (log10(rxp * 1000.0));
			power_density =
			    (eirp /
			     (pow
			      (10.0, (total_loss - free_space_loss) / 10.0)));
			/* divide by 4*PI*distance_in_meters squared */
			power_density /= (4.0 * PI * distance * distance *
					  2589988.11);

			fprintf(fd2, "Field strength at %s: %.2f dBuV/meter\n",
				destination.name, field_strength);
			fprintf(fd2, "Signal power level at %s: %+.2f dBm\n",
				destination.name, dBm);
			fprintf(fd2,
				"Signal power density at %s: %+.2f dBW per square meter\n",
				destination.name, 10.0 * log10(power_density));
			voltage =
			    1.0e6 * sqrt(50.0 *
					 (eirp /
					  (pow
					   (10.0,
					    (total_loss - 2.14) / 10.0))));
			fprintf(fd2,
				"Voltage across 50 ohm dipole at %s: %.2f uV (%.2f dBuV)\n",
				destination.name, voltage,
				20.0 * log10(voltage));

			voltage =
			    1.0e6 * sqrt(75.0 *
					 (eirp /
					  (pow
					   (10.0,
					    (total_loss - 2.14) / 10.0))));
			fprintf(fd2,
				"Voltage across 75 ohm dipole at %s: %.2f uV (%.2f dBuV)\n",
				destination.name, voltage,
				20.0 * log10(voltage));
		}

		if (propmodel == 1) {
			fprintf(fd2, "Longley-Rice model error number: %d",
				errnum);

			switch (errnum) {
			case 0:
				fprintf(fd2, " (No error)\n");
				break;

			case 1:
				fprintf(fd2,
					"\n  Warning: Some parameters are nearly out of range.\n");
				fprintf(fd2,
					"  Results should be used with caution.\n");
				break;

			case 2:
				fprintf(fd2,
					"\n  Note: Default parameters have been substituted for impossible ones.\n");
				break;

			case 3:
				fprintf(fd2,
					"\n  Warning: A combination of parameters is out of range for this model.\n");
				fprintf(fd2,
					"  Results should be used with caution.\n");
				break;

			default:
				fprintf(fd2,
					"\n  Warning: Some parameters are out of range for this model.\n");
				fprintf(fd2,
					"  Results should be used with caution.\n");
			}
		}

	}

	ObstructionAnalysis(source, destination, LR.frq_mhz, fd2);
	fclose(fd2);

	fprintf(stderr,
		"Path loss (dB), Received Power (dBm), Field strength (dBuV):\n%.1f\n%.1f\n%.1f",
		loss, dBm, field_strength);

	/* Skip plotting the graph if ONLY a path-loss report is needed. */

	if (graph_it) {
		if (name[0] == '.') {
			/* Default filename and output file type */

			strncpy(basename, "profile\0", 8);
			strncpy(term, "png\0", 4);
			strncpy(ext, "png\0", 4);
		}

		else {
			/* Extract extension and terminal type from "name" */

			ext[0] = 0;
			y = strlen(name);
			strncpy(basename, name, 254);

			for (x = y - 1; x > 0 && name[x] != '.'; x--) ;

			if (x > 0) {	/* Extension found */
				for (z = x + 1; z <= y && (z - (x + 1)) < 10;
				     z++) {
					ext[z - (x + 1)] = tolower(name[z]);
					term[z - (x + 1)] = name[z];
				}

				ext[z - (x + 1)] = 0;	/* Ensure an ending 0 */
				term[z - (x + 1)] = 0;
				basename[x] = 0;
			}
		}

		if (ext[0] == 0) {	/* No extension -- Default is png */
			strncpy(term, "png\0", 4);
			strncpy(ext, "png\0", 4);
		}

		/* Either .ps or .postscript may be used
		   as an extension for postscript output. */

		if (strncmp(term, "postscript", 10) == 0)
			strncpy(ext, "ps\0", 3);

		else if (strncmp(ext, "ps", 2) == 0)
			strncpy(term, "postscript enhanced color\0", 26);

		fd = fopen("ppa.gp", "w");

		fprintf(fd, "set grid\n");
		fprintf(fd, "set yrange [%2.3f to %2.3f]\n", minloss, maxloss);
		fprintf(fd, "set encoding iso_8859_1\n");
		fprintf(fd, "set term %s\n", term);
		fprintf(fd,
			"set title \"Path Loss Profile Along Path Between %s and %s (%.2f%c azimuth)\"\n",
			destination.name, source.name, Azimuth(destination,
							       source), 176);

		if (metric)
			fprintf(fd,
				"set xlabel \"Distance Between %s and %s (%.2f kilometers)\"\n",
				destination.name, source.name,
				KM_PER_MILE * Distance(destination, source));
		else
			fprintf(fd,
				"set xlabel \"Distance Between %s and %s (%.2f miles)\"\n",
				destination.name, source.name,
				Distance(destination, source));

		if (got_azimuth_pattern || got_elevation_pattern)
			fprintf(fd,
				"set ylabel \"Total Path Loss (including TX antenna pattern) (dB)");
		else
			fprintf(fd, "set ylabel \"Longley-Rice Path Loss (dB)");

		fprintf(fd, "\"\nset output \"%s.%s\"\n", basename, ext);
		fprintf(fd,
			"plot \"profile.gp\" title \"Path Loss\" with lines\n");

		fclose(fd);

		x = system("gnuplot ppa.gp");

		if (x != -1) {
			if (gpsav == 0) {
				//unlink("ppa.gp");
				//unlink("profile.gp");
				//unlink("reference.gp");
			}

		}

		else
			fprintf(stderr,
				"\n*** ERROR: Error occurred invoking gnuplot!\n");
	}

}

void SeriesData(struct site source, struct site destination, char *name,
		unsigned char fresnel_plot, unsigned char normalised)
{
	int x, y, z;
	char basename[255], term[30], ext[15], profilename[255],
	    referencename[255], cluttername[255], curvaturename[255],
	    fresnelname[255], fresnel60name[255];
	double a, b, c, height = 0.0, refangle, cangle, maxheight =
	    -100000.0, minheight = 100000.0, lambda = 0.0, f_zone =
	    0.0, fpt6_zone = 0.0, nm = 0.0, nb = 0.0, ed = 0.0, es = 0.0, r =
	    0.0, d = 0.0, d1 = 0.0, terrain, azimuth, distance, minterrain =
	    100000.0, minearth = 100000.0;
	struct site remote;
	FILE *fd = NULL, *fd1 = NULL, *fd2 = NULL, *fd3 = NULL, *fd4 =
	    NULL, *fd5 = NULL;

	ReadPath(destination, source);
	azimuth = Azimuth(destination, source);
	distance = Distance(destination, source);
	refangle = ElevationAngle(destination, source);
	b = GetElevation(destination) + destination.alt + earthradius;

	if (debug) {
	        fprintf(stderr, "SeriesData: az = %lf, dist = %lf, ref = %lf, b = %lf\n", azimuth, distance, refangle, b);
		fflush(stderr);
	}
	
	if (fresnel_plot) {
		lambda = 9.8425e8 / (LR.frq_mhz * 1e6);
		d = FEET_PER_MILE * path.distance[path.length - 1];
	}

	if (normalised) {
		ed = GetElevation(destination);
		es = GetElevation(source);
		nb = -destination.alt - ed;
		nm = (-source.alt - es - nb) / (path.distance[path.length - 1]);
	}

	strcpy(profilename, name);
	strcat(profilename, "_profile\0");
	strcpy(referencename, name);
	strcat(referencename, "_reference\0");
	strcpy(cluttername, name);
	strcat(cluttername, "_clutter\0");
	strcpy(curvaturename, name);
	strcat(curvaturename, "_curvature\0");
	strcpy(fresnelname, name);
	strcat(fresnelname, "_fresnel\0");
	strcpy(fresnel60name, name);
	strcat(fresnel60name, "_fresnel60\0");

	fd = fopen(profilename, "wb");
	if (clutter > 0.0)
		fd1 = fopen(cluttername, "wb");
	fd2 = fopen(referencename, "wb");
	fd5 = fopen(curvaturename, "wb");

	if ((LR.frq_mhz >= 20.0) && (LR.frq_mhz <= 100000.0) && fresnel_plot) {
		fd3 = fopen(fresnelname, "wb");
		fd4 = fopen(fresnel60name, "wb");
	}

	for (x = 0; x < path.length - 1; x++) {
		remote.lat = path.lat[x];
		remote.lon = path.lon[x];
		remote.alt = 0.0;
		terrain = GetElevation(remote);
		if (x == 0)
			terrain += destination.alt;	/* RX antenna spike */

		a = terrain + earthradius;
		cangle = FEET_PER_MILE * Distance(destination, remote) / earthradius;
		c = b * sin(refangle * DEG2RAD + HALFPI) / sin(HALFPI -
							       refangle *
							       DEG2RAD -
							       cangle);
		height = a - c;

		/* Per Fink and Christiansen, Electronics
		 * Engineers' Handbook, 1989:
		 *
		 *   H = sqrt(lamba * d1 * (d - d1)/d)
		 *
		 * where H is the distance from the LOS
		 * path to the first Fresnel zone boundary.
		 */

		if ((LR.frq_mhz >= 20.0) && (LR.frq_mhz <= 100000.0)
		    && fresnel_plot) {
			d1 = FEET_PER_MILE * path.distance[x];
			f_zone = -1.0 * sqrt(lambda * d1 * (d - d1) / d);
			fpt6_zone = f_zone * fzone_clearance;
		}

		if (normalised) {
			r = -(nm * path.distance[x]) - nb;
			height += r;

			if ((LR.frq_mhz >= 20.0) && (LR.frq_mhz <= 100000.0)
			    && fresnel_plot) {
				f_zone += r;
				fpt6_zone += r;
			}
		}

		else
			r = 0.0;

		if (metric) {
			if (METERS_PER_FOOT * height > 0) {
				fprintf(fd, "%.3f %.3f\n",
					KM_PER_MILE * path.distance[x],
					METERS_PER_FOOT * height);
			}

			if (fd1 != NULL && x > 0 && x < path.length - 2)
				fprintf(fd1, "%.3f %.3f\n",
					KM_PER_MILE * path.distance[x],
					METERS_PER_FOOT * (terrain ==
							   0.0 ? height
							   : (height +
							      clutter)));

			fprintf(fd2, "%.3f %.3f\n",
				KM_PER_MILE * path.distance[x],
				METERS_PER_FOOT * r);
			fprintf(fd5, "%.3f %.3f\n",
				KM_PER_MILE * path.distance[x],
				METERS_PER_FOOT * (height - terrain));

		}

		else {
			fprintf(fd, "%.3f %.3f\n", path.distance[x], height);

			if (fd1 != NULL && x > 0 && x < path.length - 2)
				fprintf(fd1, "%.3f %.3f\n", path.distance[x],
					(terrain ==
					 0.0 ? height : (height + clutter)));

			fprintf(fd2, "%.3f %.3f\n", path.distance[x], r);
			fprintf(fd5, "%.3f %.3f\n", path.distance[x],
				height - terrain);
		}

		if ((LR.frq_mhz >= 20.0) && (LR.frq_mhz <= 100000.0)
		    && fresnel_plot) {
			if (metric) {
				fprintf(fd3, "%.3f %.3f\n",
					KM_PER_MILE * path.distance[x],
					METERS_PER_FOOT * f_zone);
				fprintf(fd4, "%.3f %.3f\n",
					KM_PER_MILE * path.distance[x],
					METERS_PER_FOOT * fpt6_zone);
			}

			else {
				fprintf(fd3, "%.3f %.3f\n", path.distance[x],
					f_zone);
				fprintf(fd4, "%.3f %.3f\n", path.distance[x],
					fpt6_zone);
			}

			if (f_zone < minheight)
				minheight = f_zone;
		}

		if ((height + clutter) > maxheight)
			maxheight = height + clutter;

		if (height < minheight)
			minheight = height;

		if (r > maxheight)
			maxheight = r;

		if (terrain < minterrain)
			minterrain = terrain;

		if ((height - terrain) < minearth)
			minearth = height - terrain;
	}			// End of loop

	if (normalised)
		r = -(nm * path.distance[path.length - 1]) - nb;
	else
		r = 0.0;

	if (metric) {
		fprintf(fd, "%.3f %.3f",
			KM_PER_MILE * path.distance[path.length - 1],
			METERS_PER_FOOT * r);
		fprintf(fd2, "%.3f %.3f",
			KM_PER_MILE * path.distance[path.length - 1],
			METERS_PER_FOOT * r);
	}

	else {
		fprintf(fd, "%.3f %.3f", path.distance[path.length - 1], r);
		fprintf(fd2, "%.3f %.3f", path.distance[path.length - 1], r);
	}

	if ((LR.frq_mhz >= 20.0) && (LR.frq_mhz <= 100000.0) && fresnel_plot) {
		if (metric) {
			fprintf(fd3, "%.3f %.3f",
				KM_PER_MILE * path.distance[path.length - 1],
				METERS_PER_FOOT * r);
			fprintf(fd4, "%.3f %.3f",
				KM_PER_MILE * path.distance[path.length - 1],
				METERS_PER_FOOT * r);
		}

		else {
			fprintf(fd3, "%.3f %.3f",
				path.distance[path.length - 1], r);
			fprintf(fd4, "%.3f %.3f",
				path.distance[path.length - 1], r);
		}
	}

	if (r > maxheight)
		maxheight = r;

	if (r < minheight)
		minheight = r;

	fclose(fd);

	if (fd1 != NULL)
		fclose(fd1);

	fclose(fd2);
	fclose(fd5);

	if ((LR.frq_mhz >= 20.0) && (LR.frq_mhz <= 100000.0) && fresnel_plot) {
		fclose(fd3);
		fclose(fd4);
	}

	if (name[0] == '.') {
		strncpy(basename, "profile\0", 8);
		strncpy(term, "png\0", 4);
		strncpy(ext, "png\0", 4);
	}

	else {

		ext[0] = 0;
		y = strlen(name);
		strncpy(basename, name, 254);

		for (x = y - 1; x > 0 && name[x] != '.'; x--) ;

		if (x > 0) {
			for (z = x + 1; z <= y && (z - (x + 1)) < 10; z++) {
				ext[z - (x + 1)] = tolower(name[z]);
				term[z - (x + 1)] = name[z];
			}

			ext[z - (x + 1)] = 0;
			term[z - (x + 1)] = 0;
			basename[x] = 0;
		}

		if (ext[0] == 0) {
			strncpy(term, "png\0", 4);
			strncpy(ext, "png\0", 4);
		}
	}

	fprintf(stderr, "\n");
	fflush(stderr);

}
