/* SpeedGraphic
 * Generate speed-o-meter graphics
 *
 */
#ifndef SPEEDGFX_H
#define SPEEDGFX_H

#include "Gfx.h"

class SpeedGfx : public Gfx {
	int range;		// data range
	double scale;	// computed scale data->gfx
	int offlabel;	// offset to display the label
	char type;		// type of graphics to display

	double transforme( double angle );

protected:
	void calcScales( void );
	void generateBackGround( void );
	void generateOneGfx(const char *, char *, int , struct GPMFdata *);
	
public:
	SpeedGfx(GPVideo &v, char atype='2');

	void GenerateAllGfx( const char *dir, char *file );
};

#endif
