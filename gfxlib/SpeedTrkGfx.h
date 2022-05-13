/* SpeedTracker
 * Generate speed tracker graphicsi
 */
#ifndef SPEEDTRKFX_H
#define SPEEDTRKFX_H

#include "Gfx.h"

class SpeedTrkGfx : public Gfx {
	int range;
	double scale_h, scale_w;	// Scales
	char type;	// 2 ou 3

	/* Draw GPMF data
	 * -> offset : move the curve (used to draw the shadow)
	 * -> current : current position.
	 *  if set, the color will not be the same in front and in back of this position
	 *  if not set, we are drawing the shadow : THE PATH IS PRESERVED
	 */
	void drawGPMF(cairo_t *cr, int offset, GPVideo::GPMFdata *current);

protected:
	void calcScales( void );
	void generateBackground( void );
	void generateOneGfx( const char *, char *, int , GPVideo::GPMFdata *, GPX * );

public:
	SpeedTrkGfx(GPVideo &v, char type);

	void GenerateAllGfx( const char *dir, char *file, GPX * );
};

#endif
