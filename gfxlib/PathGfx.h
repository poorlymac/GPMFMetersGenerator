/* PathGraphic
 * Generate path graphics
 */
#ifndef PATHGFX_H
#define PATHGFX_H

#include "Gfx.h"

class PathGfx : public Gfx {
	int min_x, min_y;
	int max_x, max_y;

	int range_x, range_y;
	int off_x, off_y;		// offsets
	double scale;

	/* Convert position to coordinates */
	void posXY( double, double, int &, int &);

	/* Draw GPMF data
	 * -> offset : move the curve (used to draw the shadow)
	 * -> current : current position.
	 *  if set, the color will not be the same in front and in back of this position
	 *  if not set, we are drawing the shadow : THE PATH IS PRESERVED
	 */
	void drawGPMF(cairo_t *cr, int offset, GPVideo::GPMFdata *current);

protected:
	void calcScales( void );
	void generateBackground( GPX * );
	void generateOneGfx( const char *, char *, int , GPVideo::GPMFdata *, GPX * );

public:
	PathGfx(GPVideo &v);

	void GenerateAllGfx( const char *dir, char *file, GPX * );
};

#endif
