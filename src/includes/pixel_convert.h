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
 *  unpack 32-bit RGBA pixels to 24-bit RGB pixels
 */
static inline void PixelConvert_32to24Bits(uint8_t *dst, uint32_t *src, int dw, int sw)
{
	uint32_t rmask, gmask, bmask, sval;
	int rshift, gshift, bshift;
	int dx;

	Screen_GetPixelFormat(&rmask, &gmask, &bmask, &rshift, &gshift, &bshift);

	for (dx = 0; dx < dw; dx++)
	{
		sval = src[(dx * sw + dw/2) / dw];
		*dst++ = (sval & rmask) >> rshift;
		*dst++ = (sval & gmask) >> gshift;
		*dst++ = (sval & bmask) >> bshift;
	}
}

/**
 * Remap 32-bit RGBA pixels back to 16-color ST palette if possible, false if failed.
 * Note that this cannot disambiguate indices if the palette has duplicate colors.
 */
static inline bool PixelConvert_32to8Bits(uint8_t *dst, uint32_t *src, int dw, int sw)
{
	uint32_t sval;
	int dval;
	int i,dx;
	bool valid = true;

	for (dx = 0; dx < dw; dx++)
	{
		sval = src[(dx * sw + dw/2) / dw];
		dval = ConvertPaletteSize;
		for (i = 0; i < ConvertPaletteSize; i++)
		{
			if (sval == ConvertPalette[i])
			{
				dval = i;
				break;
			}
		}
		if (dval >= ConvertPaletteSize)
		{
			valid = false;
			dval = 0;
		}
		*dst++ = (uint8_t)dval;
	}
	return valid;
}


/*----------------------------------------------------------------------*/
/* Convert pixels to 24-bit BGR (3 bytes per pixel, used in BMP format)	*/
/*----------------------------------------------------------------------*/

/**
 *  unpack 32-bit RGBA pixels to 24-bit BGR pixels
 */
static inline void PixelConvert_32to24Bits_BGR(uint8_t *dst, uint32_t *src, int dw, int sw)
{
	uint32_t rmask, gmask, bmask, sval;
	int rshift, gshift, bshift;
	int dx;

	Screen_GetPixelFormat(&rmask, &gmask, &bmask, &rshift, &gshift, &bshift);

	for (dx = 0; dx < dw; dx++)
	{
		sval = src[(dx * sw + dw/2) / dw];
		*dst++ = (sval & bmask) >> bshift;
		*dst++ = (sval & gmask) >> gshift;
		*dst++ = (sval & rmask) >> rshift;
	}
}
