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
 * Unpack 8-bit data with RGB palette to 24-bit RGB pixels
 */
static inline void PixelConvert_8to24Bits(Uint8 *dst, Uint8 *src, int w, SDL_Color *colors)
{
	int x;
	for (x = 0; x < w; x++, src++) {
		*dst++ = colors[*src].r;
		*dst++ = colors[*src].g;
		*dst++ = colors[*src].b;
	}
}

/**
 * Unpack 16-bit RGB pixels to 24-bit RGB pixels
 */
static inline void PixelConvert_16to24Bits(Uint8 *dst, Uint16 *src, int w, SDL_PixelFormat *fmt)
{
	int x;
	for (x = 0; x < w; x++, src++) {
		*dst++ = (((*src & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
		*dst++ = (((*src & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((*src & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
	}
}

/**
 *  unpack 32-bit RGBA pixels to 24-bit RGB pixels
 */
static inline void PixelConvert_32to24Bits(Uint8 *dst, Uint32 *src, int w, SDL_PixelFormat *fmt)
{
	int x;
	for (x = 0; x < w; x++, src++)
	{
		*dst++ = (((*src & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
		*dst++ = (((*src & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((*src & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
	}
}



/*----------------------------------------------------------------------*/
/* Convert pixels to 24-bit BGR (3 bytes per pixel, used in BMP format)	*/
/*----------------------------------------------------------------------*/

/**
 * Unpack 8-bit data with RGB palette to 24-bit BGR pixels
 */
static inline void PixelConvert_8to24Bits_BGR(Uint8 *dst, Uint8 *src, int w, SDL_Color *colors)
{
	int x;
	for (x = 0; x < w; x++, src++) {
		*dst++ = colors[*src].b;
		*dst++ = colors[*src].g;
		*dst++ = colors[*src].r;
	}
}

/**
 * Unpack 16-bit RGB pixels to 24-bit BGR pixels
 */
static inline void PixelConvert_16to24Bits_BGR(Uint8 *dst, Uint16 *src, int w, SDL_PixelFormat *fmt)
{
	int x;
	for (x = 0; x < w; x++, src++) {
		*dst++ = (((*src & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
		*dst++ = (((*src & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((*src & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
	}
}

/**
 *  unpack 24-bit RGB pixels to 24-bit BGR pixels
 */
static inline void PixelConvert_24to24Bits_BGR(Uint8 *dst, Uint8 *src, int w)
{
	int x;
	for (x = 0; x < w; x++, src += 3) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		*dst++ = src[2];	/* B */
		*dst++ = src[1];	/* G */
		*dst++ = src[0];	/* R */
#else
		*dst++ = src[0];	/* B */
		*dst++ = src[1];	/* G */
		*dst++ = src[2];	/* R */
#endif
	}
}

/**
 *  unpack 32-bit RGBA pixels to 24-bit BGR pixels
 */
static inline void PixelConvert_32to24Bits_BGR(Uint8 *dst, Uint32 *src, int w, SDL_PixelFormat *fmt)
{
	int x;
	for (x = 0; x < w; x++, src++)
	{
		*dst++ = (((*src & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
		*dst++ = (((*src & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((*src & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
	}
}
