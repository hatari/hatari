/*
  Hatari - pixel_convert.h

  Functions to convert pixels from different bit depths to 24 bits RGB or BGR.
  Used to save png screenshot and to record avi.

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/


/*----------------------------------------------------------------------*/
/* Convert pixels to 24-bit RGB (3 bytes per pixel)			*/
/*----------------------------------------------------------------------*/

/**
 * Unpack 16-bit RGB pixels to 24-bit RGB pixels
 */
static inline void PixelConvert_16to24Bits(Uint8 *dst, Uint16 *src, int dw, SDL_Surface *surf)
{
	SDL_PixelFormat *fmt = surf->format;
	Uint16 sval;
	int dx;

	for (dx = 0; dx < dw; dx++)
	{
		sval = src[(dx * surf->w + dw/2) / dw];
		*dst++ = (((sval & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
		*dst++ = (((sval & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((sval & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
	}
}

/**
 *  unpack 32-bit RGBA pixels to 24-bit RGB pixels
 */
static inline void PixelConvert_32to24Bits(Uint8 *dst, Uint32 *src, int dw, SDL_Surface *surf)
{
	SDL_PixelFormat *fmt = surf->format;
	Uint32 sval;
	int dx;

	for (dx = 0; dx < dw; dx++)
	{
		sval = src[(dx * surf->w + dw/2) / dw];
		*dst++ = (((sval & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
		*dst++ = (((sval & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((sval & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
	}
}



/*----------------------------------------------------------------------*/
/* Convert pixels to 24-bit BGR (3 bytes per pixel, used in BMP format)	*/
/*----------------------------------------------------------------------*/

/**
 * Unpack 16-bit RGB pixels to 24-bit BGR pixels
 */
static inline void PixelConvert_16to24Bits_BGR(Uint8 *dst, Uint16 *src, int dw, SDL_Surface *surf)
{
	SDL_PixelFormat *fmt = surf->format;
	Uint16 sval;
	int dx;

	for (dx = 0; dx < dw; dx++)
	{
		sval = src[(dx * surf->w + dw/2) / dw];
		*dst++ = (((sval & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
		*dst++ = (((sval & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((sval & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
	}
}

/**
 *  unpack 32-bit RGBA pixels to 24-bit BGR pixels
 */
static inline void PixelConvert_32to24Bits_BGR(Uint8 *dst, Uint32 *src, int dw, SDL_Surface *surf)
{
	SDL_PixelFormat *fmt = surf->format;
	Uint32 sval;
	int dx;

	for (dx = 0; dx < dw; dx++)
	{
		sval = src[(dx * surf->w + dw/2) / dw];
		*dst++ = (((sval & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
		*dst++ = (((sval & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((sval & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
	}
}
