#ifndef _TILES_HH_
#define _TILES_HH_

typedef struct _tile_t{
	char	*filename;
	union{
		int	cols;
		int	width;
	};
	union{
		int	rows;
		int	height;
	};
	union{
		double	xll;
		double	max_west;
	};
	union{
		double	yll;
		double	min_north;
	};
	union{
		double	xur;
		double	min_west;
	};
	union{
		double	yur;
		double	max_north;
	};
	double	cellsize;
	long long datastart;
	short	nodata;
	short 	max_el;
	short	min_el;
	short	*data;
	float 	precise_resolution;
	float	resolution;
	double	width_deg;
	double	height_deg;
	int		ppdx;
	int		ppdy;
} tile_t, *ptile_t;

int tile_load_lidar(tile_t*, char *);
int tile_rescale(tile_t *, float);
void tile_destroy(tile_t *);

#endif