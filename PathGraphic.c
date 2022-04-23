/* PathGraphic
 * Generate simple path graphics
 *
 */

#include "Shared.h"
#include "PathGraphic.h"
#include "GpxHelper.h"

#include <stdio.h>
#include <stdlib.h>
#include <cairo.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#define GFX 300
#define R (6371000)	/* terrestrial radius */

	/* From https://forums.futura-sciences.com/mathematiques-superieur/39838-conversion-lat-long-x-y.html */
static void posXY( double lat, double lgt, int *x, int *y){
	/* Degree -> Radian */
	lat *= M_PI/180.0;
	lgt *= M_PI/180.0;

	double sinlgt = sin(lgt),
		coslgt = cos(lgt),
		sintlat = sin(lat),
		coslat = cos(lat);

	*x = (int)(2 * R * sinlgt*coslat / (1 + coslgt*coslat));
	*y = (int)(2 * R * sintlat / (1 + coslgt*coslat));
}


static int min_x, min_y;
static int max_x, max_y;
static int range_x, range_y;
static int off_x, off_y;
static double scale;

static cairo_surface_t *background;

/* Draw GoPro's own data
 * cr -> cairo context
 * offset -> offset the curve (do draw shadows)
 */
static void drawGPMF(cairo_t *cr, int offset, struct GPMFdata *current){
	struct GPMFdata *p;

	cairo_save(cr);
	if(current)
		cairo_set_source_rgb(cr, 0.11, 0.65, 0.88);

	for(p = first; p; p = p->next){
		int x,y;

		posXY(p->latitude, p->longitude, &x, &y);
		x = off_x + (x-min_x) * scale + offset;
		y = GFX - off_y - (y-min_y)*scale + offset;

		if(p == first)
			cairo_move_to(cr, x, y);
		else
			cairo_line_to(cr, x, y);

		if(current == p){
			cairo_stroke(cr);
			cairo_restore(cr);
			cairo_move_to(cr, x, y);
		}
	}
	cairo_stroke(cr);
}

/* Draw external GPX data
 * cr -> cairo context
 * offset -> offset the curve (do draw shadows)
 */
static void drawGPX(cairo_t *cr, int offset){
	struct GpxData *p;
	for(p = Gpx; p; p = p->next){
		int x,y;

		posXY(p->latitude, p->longitude, &x, &y);
		x = off_x + (x-min_x) * scale + offset;
		y = GFX - off_y - (y-min_y)*scale + offset;

		if(p == Gpx)
			cairo_move_to(cr, x, y);
		else
			cairo_line_to(cr, x, y);
	}
	cairo_stroke(cr);
}

static void generateBackGround(){
		/*
		 * compute limits, scales
		 */
	if(Gpx){
		posXY( minGpx.latitude, minGpx.longitude, &min_x, &min_y);
		posXY( maxGpx.latitude, maxGpx.longitude, &max_x, &max_y);
	} else {
		posXY( min.latitude, min.longitude, &min_x, &min_y);
		posXY( max.latitude, max.longitude, &max_x, &max_y);
	}

	range_x = max_x - min_x;
	range_y = max_y - min_y;

	scale = (double)(GFX-20)/(double)((range_x > range_y) ? range_x : range_y);

	off_x = (GFX - range_x * scale)/2;
	off_y = (GFX - range_y * scale)/2;

	if(debug)
		printf("*D* min: (%d,%d), max: (%d,%d) -> (%d, %d) (scale: %.f) => off (%d,%d)\n",
			min_x, min_y, max_x, max_y, range_x, range_y, scale, off_x, off_y
		);

	background = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, GFX, GFX);
	if(cairo_surface_status(background) != CAIRO_STATUS_SUCCESS){
		puts("*F* Can't create Cairo's surface");
		exit(EXIT_FAILURE);
	}

	cairo_t *cr = cairo_create(background);

		/* Draw GPX gfx */
	if(Gpx){
			/* Draw shadow */
		cairo_set_line_width(cr, 2);
		cairo_set_source_rgba(cr, 0,0,0, 0.55);
		drawGPX(cr, 2);

			/* Draw path */
		cairo_set_source_rgb(cr, 1,1,1);	/* Set white color */
		cairo_set_line_width(cr, 2);
		drawGPX(cr, 0);
	} else {
			/* Draw shadow */
		cairo_set_line_width(cr, 2);
		cairo_set_source_rgba(cr, 0,0,0, 0.55);
		drawGPMF(cr, 2, NULL);
	}

		/* Cleaning */
	cairo_destroy(cr);
}

static void GeneratePathGfx( const char *fulltarget, char *filename, int index, struct GPMFdata *current){
	cairo_surface_t *srf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, GFX, GFX);
	if(cairo_surface_status(srf) != CAIRO_STATUS_SUCCESS){
		puts("*F* Can't create Cairo's surface");
		exit(EXIT_FAILURE);
	}

	cairo_t *cr = cairo_create(srf);
	cairo_set_source_surface(cr, background, 0, 0);
	cairo_rectangle(cr, 0, 0, GFX, GFX);
	cairo_fill(cr);
	cairo_stroke(cr);

	if(Gpx)
		cairo_set_source_rgb(cr, 0.3, 0.4, 0.6);
	else
		cairo_set_source_rgb(cr, 1,1,1);
	cairo_set_line_width(cr, 3);
	drawGPMF(cr, 0, current);

	cairo_set_source_rgb(cr, 1,1,1);
	int x,y;
	posXY(current->latitude, current->longitude, &x, &y);
	x = off_x + (x-min_x) * scale;
	y = GFX - off_y - (y-min_y)*scale;

	cairo_set_line_width(cr, 5);
	cairo_arc(cr, x, y, 5, 0, 2 * M_PI);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 0.8, 0.2, 0.2);
	cairo_fill(cr);

	sprintf(filename, "pth%07d.png", index);
	if(verbose)
		printf("*D* Writing '%s'\r", fulltarget);
	
	cairo_status_t err;
	if((err = cairo_surface_write_to_png(srf, fulltarget)) != CAIRO_STATUS_SUCCESS){
		printf("*F* Writing surface : %s / %s\n", cairo_status_to_string(err), strerror(errno));
		exit(EXIT_FAILURE);
	}

		/* Cleaning */
	cairo_destroy(cr);
	cairo_surface_destroy(srf);
}

void GenerateAllPathGfx( const char *fulltarget, char *filename ){
	int i;
	struct GPMFdata *p;

	generateBackGround();
	for(i = 0, p = first; i < samples_count; i++, p=p->next)
		GeneratePathGfx(fulltarget, filename, i, p);

		/* Cleaning */
	cairo_surface_destroy(background);

		/* Generate video */
	if(video)
		generateVideo(fulltarget, filename, "pth", "path");
}

