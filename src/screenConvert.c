/*
  Hatari - screenConvert.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include <SDL_endian.h>
#include "main.h"
#include "configuration.h"
#include "log.h"
#include "ioMem.h"
#include "memorySnapShot.h"
#include "screen.h"
#include "screenConvert.h"
#include "statusbar.h"
#include "stMemory.h"
#include "video.h"

#define Atari2HostAddr(a) (&STRam[a])

struct screen_zoom_s {
	Uint16 zoomwidth;
	Uint16 prev_scrwidth;
	Uint16 zoomheight;
	Uint16 prev_scrheight;
	int *zoomxtable;
	int *zoomytable;
};

static struct screen_zoom_s screen_zoom;
static bool bTTSampleHold = false;		/* TT special video mode */
static int nSampleHoldIdx;


/* TOS palette (bpp < 16) to SDL color mapping */
static struct
{
	SDL_Color	standard[256];
	Uint32		native[256];
} palette;

void Screen_SetPaletteColor(Uint8 idx, Uint8 red, Uint8 green, Uint8 blue)
{
	// set the SDL standard RGB palette settings
	palette.standard[idx].r = red;
	palette.standard[idx].g = green;
	palette.standard[idx].b = blue;
	// convert the color to native
	palette.native[idx] = SDL_MapRGB(sdlscrn->format, red, green, blue);
}

void Screen_RemapPalette(void)
{
	int i;
	Uint32 *native = palette.native;
	SDL_Color *standard = palette.standard;
	SDL_PixelFormat *fmt = sdlscrn->format;

	for(i = 0; i < 256; i++, native++, standard++) {
		*native = SDL_MapRGB(fmt, standard->r, standard->g, standard->b);
	}
}

void ScreenConv_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(palette.standard, sizeof(palette.standard));
	if (!bSave)
		Screen_RemapPalette();
}

static void Screen_memset_uint32(Uint32 *addr, Uint32 color, int count)
{
	while (count-- > 0) {
		*addr++ = color;
	}
}

static void Screen_memset_uint16(Uint16 *addr, Uint16 color, int count)
{
	while (count-- > 0) {
		*addr++ = color;
	}
}

static inline Uint32 idx2pal(Uint8 idx)
{
	if (unlikely(bTTSampleHold))
	{
		if (idx == 0)
			return palette.native[nSampleHoldIdx];
		nSampleHoldIdx = idx;
	}
	return palette.native[idx];
}

/**
 * Performs conversion from the TOS's bitplane word order (big endian) data
 * into the native 16-bit chunky pixels.
 */
static void Screen_BitplaneToChunky16(Uint16 *atariBitplaneData, Uint16 bpp,
                                      Uint16 *hvram)
{
	Uint32 a, b, c, d, x;

	/* Obviously the different cases can be broken out in various
	 * ways to lessen the amount of work needed for <8 bit modes.
	 * It's doubtful if the usage of those modes warrants it, though.
	 * The branches below should be ~100% correctly predicted and
	 * thus be more or less for free.
	 */
	if (bpp >= 4) {
		d = *(Uint32 *)&atariBitplaneData[0];
		c = *(Uint32 *)&atariBitplaneData[2];
		if (bpp == 4) {
			a = b = 0;
		} else {
			b = *(Uint32 *)&atariBitplaneData[4];
			a = *(Uint32 *)&atariBitplaneData[6];
		}

		x = a;
		a =  (a & 0xf0f0f0f0)       | ((c & 0xf0f0f0f0) >> 4);
		c = ((x & 0x0f0f0f0f) << 4) |  (c & 0x0f0f0f0f);
	} else {
		a = b = c = 0;
		if (bpp == 2) {
			d = *(Uint32 *)&atariBitplaneData[0];
		} else {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			d = atariBitplaneData[0]<<16;
#else
			d = atariBitplaneData[0];
#endif
		}
	}

	x = b;
	b =  (b & 0xf0f0f0f0)       | ((d & 0xf0f0f0f0) >> 4);
	d = ((x & 0x0f0f0f0f) << 4) |  (d & 0x0f0f0f0f);

	x = a;
	a =  (a & 0xcccccccc)       | ((b & 0xcccccccc) >> 2);
	b = ((x & 0x33333333) << 2) |  (b & 0x33333333);
	x = c;
	c =  (c & 0xcccccccc)       | ((d & 0xcccccccc) >> 2);
	d = ((x & 0x33333333) << 2) |  (d & 0x33333333);

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	a = (a & 0x5555aaaa) | ((a & 0x00005555) << 17) | ((a & 0xaaaa0000) >> 17);
	b = (b & 0x5555aaaa) | ((b & 0x00005555) << 17) | ((b & 0xaaaa0000) >> 17);
	c = (c & 0x5555aaaa) | ((c & 0x00005555) << 17) | ((c & 0xaaaa0000) >> 17);
	d = (d & 0x5555aaaa) | ((d & 0x00005555) << 17) | ((d & 0xaaaa0000) >> 17);

	*hvram++ = idx2pal(a >> 8);
	*hvram++ = idx2pal(a >> 24);
	*hvram++ = idx2pal(b >> 8);
	*hvram++ = idx2pal(b >> 24);
	*hvram++ = idx2pal(c >> 8);
	*hvram++ = idx2pal(c >> 24);
	*hvram++ = idx2pal(d >> 8);
	*hvram++ = idx2pal(d >> 24);
	*hvram++ = idx2pal(a);
	*hvram++ = idx2pal(a >> 16);
	*hvram++ = idx2pal(b);
	*hvram++ = idx2pal(b >> 16);
	*hvram++ = idx2pal(c);
	*hvram++ = idx2pal(c >> 16);
	*hvram++ = idx2pal(d);
	*hvram++ = idx2pal(d >> 16);
#else
	a = (a & 0xaaaa5555) | ((a & 0x0000aaaa) << 15) | ((a & 0x55550000) >> 15);
	b = (b & 0xaaaa5555) | ((b & 0x0000aaaa) << 15) | ((b & 0x55550000) >> 15);
	c = (c & 0xaaaa5555) | ((c & 0x0000aaaa) << 15) | ((c & 0x55550000) >> 15);
	d = (d & 0xaaaa5555) | ((d & 0x0000aaaa) << 15) | ((d & 0x55550000) >> 15);

	*hvram++ = idx2pal(a >> 16);
	*hvram++ = idx2pal(a);
	*hvram++ = idx2pal(b >> 16);
	*hvram++ = idx2pal(b);
	*hvram++ = idx2pal(c >> 16);
	*hvram++ = idx2pal(c);
	*hvram++ = idx2pal(d >> 16);
	*hvram++ = idx2pal(d);
	*hvram++ = idx2pal(a >> 24);
	*hvram++ = idx2pal(a >> 8);
	*hvram++ = idx2pal(b >> 24);
	*hvram++ = idx2pal(b >> 8);
	*hvram++ = idx2pal(c >> 24);
	*hvram++ = idx2pal(c >> 8);
	*hvram++ = idx2pal(d >> 24);
	*hvram++ = idx2pal(d >> 8);
#endif
}

/**
 * Performs conversion from the TOS's bitplane word order (big endian) data
 * into the native 32-bit chunky pixels.
 */
static void Screen_BitplaneToChunky32(Uint16 *atariBitplaneData, Uint16 bpp,
                                      Uint32 *hvram)
{
	Uint32 a, b, c, d, x;

	if (bpp >= 4) {
		d = *(Uint32 *)&atariBitplaneData[0];
		c = *(Uint32 *)&atariBitplaneData[2];
		if (bpp == 4) {
			a = b = 0;
		} else {
			b = *(Uint32 *)&atariBitplaneData[4];
			a = *(Uint32 *)&atariBitplaneData[6];
		}

		x = a;
		a =  (a & 0xf0f0f0f0)       | ((c & 0xf0f0f0f0) >> 4);
		c = ((x & 0x0f0f0f0f) << 4) |  (c & 0x0f0f0f0f);
	} else {
		a = b = c = 0;
		if (bpp == 2) {
			d = *(Uint32 *)&atariBitplaneData[0];
		} else {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			d = atariBitplaneData[0]<<16;
#else
			d = atariBitplaneData[0];
#endif
		}
	}

	x = b;
	b =  (b & 0xf0f0f0f0)       | ((d & 0xf0f0f0f0) >> 4);
	d = ((x & 0x0f0f0f0f) << 4) |  (d & 0x0f0f0f0f);

	x = a;
	a =  (a & 0xcccccccc)       | ((b & 0xcccccccc) >> 2);
	b = ((x & 0x33333333) << 2) |  (b & 0x33333333);
	x = c;
	c =  (c & 0xcccccccc)       | ((d & 0xcccccccc) >> 2);
	d = ((x & 0x33333333) << 2) |  (d & 0x33333333);

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	a = (a & 0x5555aaaa) | ((a & 0x00005555) << 17) | ((a & 0xaaaa0000) >> 17);
	b = (b & 0x5555aaaa) | ((b & 0x00005555) << 17) | ((b & 0xaaaa0000) >> 17);
	c = (c & 0x5555aaaa) | ((c & 0x00005555) << 17) | ((c & 0xaaaa0000) >> 17);
	d = (d & 0x5555aaaa) | ((d & 0x00005555) << 17) | ((d & 0xaaaa0000) >> 17);

	*hvram++ = idx2pal(a >> 8);
	*hvram++ = idx2pal(a >> 24);
	*hvram++ = idx2pal(b >> 8);
	*hvram++ = idx2pal(b >> 24);
	*hvram++ = idx2pal(c >> 8);
	*hvram++ = idx2pal(c >> 24);
	*hvram++ = idx2pal(d >> 8);
	*hvram++ = idx2pal(d >> 24);
	*hvram++ = idx2pal(a);
	*hvram++ = idx2pal(a >> 16);
	*hvram++ = idx2pal(b);
	*hvram++ = idx2pal(b >> 16);
	*hvram++ = idx2pal(c);
	*hvram++ = idx2pal(c >> 16);
	*hvram++ = idx2pal(d);
	*hvram++ = idx2pal(d >> 16);
#else
	a = (a & 0xaaaa5555) | ((a & 0x0000aaaa) << 15) | ((a & 0x55550000) >> 15);
	b = (b & 0xaaaa5555) | ((b & 0x0000aaaa) << 15) | ((b & 0x55550000) >> 15);
	c = (c & 0xaaaa5555) | ((c & 0x0000aaaa) << 15) | ((c & 0x55550000) >> 15);
	d = (d & 0xaaaa5555) | ((d & 0x0000aaaa) << 15) | ((d & 0x55550000) >> 15);

	*hvram++ = idx2pal(a >> 16);
	*hvram++ = idx2pal(a);
	*hvram++ = idx2pal(b >> 16);
	*hvram++ = idx2pal(b);
	*hvram++ = idx2pal(c >> 16);
	*hvram++ = idx2pal(c);
	*hvram++ = idx2pal(d >> 16);
	*hvram++ = idx2pal(d);
	*hvram++ = idx2pal(a >> 24);
	*hvram++ = idx2pal(a >> 8);
	*hvram++ = idx2pal(b >> 24);
	*hvram++ = idx2pal(b >> 8);
	*hvram++ = idx2pal(c >> 24);
	*hvram++ = idx2pal(c >> 8);
	*hvram++ = idx2pal(d >> 24);
	*hvram++ = idx2pal(d >> 8);
#endif
}


static void Screen_ConvertWithoutZoom(uint32_t vaddr, int vw, int vh, int vbpp, int nextline,
                                      int leftBorder, int rightBorder,
                                      int upperBorder, int lowerBorder)
{
	int scrpitch = sdlscrn->pitch;
	int h, w, j;

	Uint16 *fvram = (Uint16 *) Atari2HostAddr(vaddr);
	Uint16 *fvram_line;
	Uint8 *hvram = sdlscrn->pixels;

	Uint16 lowBorderSize, rightBorderSize;
	int scrwidth, scrheight;
	int vw_clip, vh_clip;

	int hscrolloffset = IoMem_ReadByte(0xff8265) & 0x0f;

	/* Horizontal scroll register set? */
	if (hscrolloffset) {
		/* Yes, so we need to adjust offset to next line: */
		nextline += vbpp;
	}

	/* The sample-hold feature exists only on the TT */
	bTTSampleHold = (TTSpecialVideoMode & 0x80) != 0;

	/* Clip to SDL_Surface dimensions */
	scrwidth = Screen_GetGenConvWidth();
	scrheight = Screen_GetGenConvHeight();
	vw_clip = vw + rightBorder + leftBorder;
	vh_clip = vh + upperBorder + lowerBorder;
	if (vw_clip > scrwidth)
		vw_clip = scrwidth;
	if (vh_clip > scrheight)
		vh_clip = scrheight;

	/* If there's not enough space to display the left border, just return */
	if (vw_clip < leftBorder)
		return;
	/* If there's not enough space for the left border + the graphic area, we clip */
	if (vw_clip < vw + leftBorder) {
		vw = vw_clip - leftBorder;
		rightBorderSize = 0;
	}
	/* if there's not enough space for the left border + the graphic area + the right border, we clip the border */
	else if (vw_clip < vw + leftBorder + rightBorder)
		rightBorderSize = vw_clip - leftBorder - vw;
	else
		rightBorderSize = rightBorder;

	/* If there's not enough space to display the upper border, just return */
	if (vh_clip < upperBorder)
		return;

	/* If there's not enough space for the upper border + the graphic area, we clip */ 
	if (vh_clip < vh + upperBorder) {
		vh = vh_clip - upperBorder;
		lowBorderSize = 0;
	}
	/* if there's not enough space for the upper border + the graphic area + the lower border, we clip the border */
	else if (vh_clip < vh + upperBorder + lowerBorder)
		lowBorderSize = vh_clip - upperBorder - vh;
	else
		lowBorderSize = lowerBorder;

	/* Center screen */
	hvram += ((scrheight-vh_clip)>>1)*scrpitch;
	hvram += ((scrwidth-vw_clip)>>1) * sdlscrn->format->BytesPerPixel;

	fvram_line = fvram;
	scrwidth = leftBorder + vw + rightBorder;

	/* render the graphic area */
	if (vbpp < 16) {
		/* Bitplanes modes */
		switch (sdlscrn->format->BytesPerPixel)
		{
			case 2:
			{
				Uint16 *hvram_line = (Uint16 *)hvram;
				Uint16 hvram_buf[16];

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					Screen_memset_uint16(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>1;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint16 *hvram_column = hvram_line;

					nSampleHoldIdx = 0;

					/* Left border first */
					Screen_memset_uint16(hvram_column, palette.native[0], leftBorder);
					hvram_column += leftBorder;

					/* First 16 pixels */
					Screen_BitplaneToChunky16(fvram_column, vbpp, hvram_buf);
					for (j = hscrolloffset; j < 16; j++) {
						*hvram_column++ = hvram_buf[j];
					}
					fvram_column += vbpp;
					/* Now the main part of the line */
					for (w = 1; w < (vw+15)>>4; w++) {
						Screen_BitplaneToChunky16(fvram_column, vbpp, hvram_column);
						hvram_column += 16;
						fvram_column += vbpp;
					}
					/* Last pixels of the line for fine scrolling */
					if (hscrolloffset) {
						Screen_BitplaneToChunky16(fvram_column, vbpp, hvram_buf);
						for (j = 0; j < hscrolloffset; j++) {
							*hvram_column++ = hvram_buf[j];
						}
					}
					/* Right border */
					Screen_memset_uint16(hvram_column, palette.native[0], rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch>>1;
				}

				/* Render the lower border */
				for (h = 0; h < lowBorderSize; h++) {
					Screen_memset_uint16(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>1;
				}
			}
			break;
			case 4:
			{
				Uint32 *hvram_line = (Uint32 *)hvram;
				Uint32 hvram_buf[16];

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>2;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint32 *hvram_column = hvram_line;

					nSampleHoldIdx = 0;

					/* Left border first */
					Screen_memset_uint32(hvram_column, palette.native[0], leftBorder);
					hvram_column += leftBorder;

					/* First 16 pixels */
					Screen_BitplaneToChunky32(fvram_column, vbpp, hvram_buf);
					for (j = hscrolloffset; j < 16; j++) {
						*hvram_column++ = hvram_buf[j];
					}
					fvram_column += vbpp;
					/* Now the main part of the line */
					for (w = 1; w < (vw+15)>>4; w++) {
						Screen_BitplaneToChunky32(fvram_column, vbpp, hvram_column);
						hvram_column += 16;
						fvram_column += vbpp;
					}
					/* Last pixels of the line for fine scrolling */
					if (hscrolloffset) {
						Screen_BitplaneToChunky32(fvram_column, vbpp, hvram_buf);
						for (j = 0; j < hscrolloffset; j++) {
							*hvram_column++ = hvram_buf[j];
						}
					}
					/* Right border */
					Screen_memset_uint32(hvram_column, palette.native[0], rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch>>2;
				}

				/* Render the lower border */
				for (h = 0; h < lowBorderSize; h++) {
					Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>2;
				}
			}
			break;
		}

	} else {

		/* Falcon TC (High Color) */
		switch (sdlscrn->format->BytesPerPixel)
		{
			case 2:
			{
				Uint16 *hvram_line = (Uint16 *)hvram;

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					Screen_memset_uint16(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>1;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *hvram_column = hvram_line;
#if SDL_BYTEORDER != SDL_BIG_ENDIAN
					Uint16 *fvram_column;
#endif
					/* Left border first */
					Screen_memset_uint16(hvram_column, palette.native[0], leftBorder);
					hvram_column += leftBorder;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
					/* FIXME: here might be a runtime little/big video endian switch like:
						if ( " videocard memory in Motorola endian format " false)
					*/
					memcpy(hvram_column, fvram_line, vw<<1);
					hvram_column += vw<<1;
#else
					fvram_column = fvram_line;
					/* Graphical area */
					for (w = 0; w < vw; w++)
						*hvram_column ++ = SDL_SwapBE16(*fvram_column++);
#endif /* SDL_BYTEORDER == SDL_BIG_ENDIAN */

					/* Right border */
					Screen_memset_uint16(hvram_column, palette.native[0], rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch>>1;
				}

				/* Render the bottom border */
				for (h = 0; h < lowBorderSize; h++) {
					Screen_memset_uint16(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>1;
				}
			}
			break;
			case 4:
			{
				Uint32 *hvram_line = (Uint32 *)hvram;

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>2;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint32 *hvram_column = hvram_line;

					/* Left border first */
					Screen_memset_uint32(hvram_column, palette.native[0], leftBorder);
					hvram_column += leftBorder;

					/* Graphical area */
					for (w = 0; w < vw; w++) {
						Uint16 srcword = *fvram_column++;
						*hvram_column ++ = SDL_MapRGB(sdlscrn->format, srcword & 0xf8,
						                              ((srcword & 0x07) << 5) | ((srcword >> 11) & 0x3c),
						                              (srcword >> 5) & 0xf8);
					}

					/* Right border */
					Screen_memset_uint32(hvram_column, palette.native[0], rightBorderSize);
				}

				fvram_line += nextline;
				hvram_line += scrpitch>>2;

				/* Render the bottom border */
				for (h = 0; h < lowBorderSize; h++) {
					Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>2;
				}
			}
			break;
		}
	}
}


static void Screen_ConvertWithZoom(uint32_t vaddr, int vw, int vh, int vbpp, int nextline,
                                   int leftBorder, int rightBorder,
                                   int upperBorder, int lowerBorder)
{
	int i, j, w, h, cursrcline;

	Uint16 *fvram = (Uint16 *) Atari2HostAddr(vaddr);
	Uint16 *fvram_line;
	Uint16 scrIdx = 0;

	int coefx = 1;
	int coefy = 1;
	int scrpitch, scrwidth, scrheight, scrbpp, hscrolloffset;
	Uint8 *hvram;
	int vw_b, vh_b;

	/* The sample-hold feature exists only on the TT */
	bTTSampleHold = (TTSpecialVideoMode & 0x80) != 0;

	vw_b = vw + leftBorder + rightBorder;
	vh_b = vh + upperBorder + lowerBorder;

	/* Host screen infos */
	scrpitch = sdlscrn->pitch;
	scrwidth = Screen_GetGenConvWidth();
	scrheight = Screen_GetGenConvHeight();
	scrbpp = sdlscrn->format->BytesPerPixel;
	hvram = sdlscrn->pixels;

	hscrolloffset = IoMem_ReadByte(0xff8265) & 0x0f;

	/* Horizontal scroll register set? */
	if (hscrolloffset) {
		/* Yes, so we need to adjust offset to next line: */
		nextline += vbpp;
	}

	/* Integer zoom coef ? */
	if (scrwidth >= vw_b && scrheight >= vh_b) {
		coefx = scrwidth / vw_b;
		coefy = scrheight / vh_b;

		scrwidth = vw_b * coefx;
		scrheight = vh_b * coefy;

		/* Center screen */
		hvram += ((Screen_GetGenConvHeight()-scrheight)>>1)*scrpitch;
		hvram += ((Screen_GetGenConvWidth()-scrwidth)>>1)*scrbpp;
	}

	/* New zoom ? */
	if (screen_zoom.zoomwidth != vw_b || scrwidth != screen_zoom.prev_scrwidth) {
		if (screen_zoom.zoomxtable) {
			free(screen_zoom.zoomxtable);
		}
		screen_zoom.zoomxtable = malloc(sizeof(int)*scrwidth);
		for (i=0; i<scrwidth; i++) {
			screen_zoom.zoomxtable[i] = (vw_b * i) / scrwidth;
		}
		screen_zoom.zoomwidth = vw_b;
		screen_zoom.prev_scrwidth = scrwidth;
	}
	if (screen_zoom.zoomheight != vh_b || scrheight != screen_zoom.prev_scrheight) {
		if (screen_zoom.zoomytable) {
			free(screen_zoom.zoomytable);
		}
		screen_zoom.zoomytable = malloc(sizeof(int)*scrheight);
		for (i=0; i<scrheight; i++) {
			screen_zoom.zoomytable[i] = (vh_b * i) / scrheight;
		}
		screen_zoom.zoomheight = vh_b;
		screen_zoom.prev_scrheight = scrheight;
	}

	cursrcline = -1;

	/* scrwidth must not change */
	scrheight = vh * coefy;

	if (vw < 16) {
		Log_Printf(LOG_WARN, "ERROR: Videl <16 screen width (%dx%d without borders)\nIf this happens at TOS boot, remove hatari.nvram,\nNVRAM video settings in it are corrupted.\n", vw, vh);
		/* prevent memory corruption */
		return;
	}

	if (vbpp<16) {
		/* Bitplanes modes */
		switch(scrbpp) {
			case 2:
			{
				/* One complete 16-pixel aligned planar 2 chunky line */
				Uint16 *p2cline = malloc(sizeof(Uint16) * ((vw+15) & ~15));
				Uint16 *hvram_line = (Uint16 *)hvram;
				Uint16 *hvram_column = p2cline;
				Uint16 hvram_buf[16];

				/* Render the upper border */
				for (h = 0; h < upperBorder * coefy; h++) {
					Screen_memset_uint16(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>1;
				}

				/* Render the graphical area */
				for (h = 0; h < scrheight; h++) {
					fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
					scrIdx ++;
					nSampleHoldIdx = 0;

					/* Recopy the same line ? */
					if (screen_zoom.zoomytable[h] == cursrcline) {
						memcpy(hvram_line, hvram_line-(scrpitch>>1), scrwidth*scrbpp);
					} else {
						Uint16 *fvram_column = fvram_line;
						hvram_column = p2cline;

						/* First 16 pixels of a new line */
						Screen_BitplaneToChunky16(fvram_column, vbpp, hvram_buf);
						for (j = hscrolloffset; j < 16; j++) {
							*hvram_column++ = hvram_buf[j];
						}
						fvram_column += vbpp;
						/* Convert the main part of the new line */
						for (w = 1; w < (vw+15)>>4; w++) {
							Screen_BitplaneToChunky16(fvram_column, vbpp, hvram_column);
							hvram_column += 16;
							fvram_column += vbpp;
						}
						/* Last pixels of the new line for fine scrolling */
						if (hscrolloffset) {
							Screen_BitplaneToChunky16(fvram_column, vbpp, hvram_buf);
							for (j = 0; j < hscrolloffset; j++) {
								*hvram_column++ = hvram_buf[j];
							}
						}

						hvram_column = hvram_line;

						/* Display the Left border */
						Screen_memset_uint16(hvram_column, palette.native[0], leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w=0; w<(vw*coefx); w++)
							hvram_column[w] = p2cline[screen_zoom.zoomxtable[w]];
						hvram_column += vw * coefx;

						/* Display the Right border */
						Screen_memset_uint16(hvram_column, palette.native[0], rightBorder * coefx);
						hvram_column += rightBorder * coefx;
					}

					hvram_line += scrpitch>>1;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					Screen_memset_uint16(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>1;
				}

				free(p2cline);
			}
			break;
			case 4:
			{
				/* One complete 16-pixel aligned planar 2 chunky line */
				Uint32 *p2cline = malloc(sizeof(Uint32) * ((vw+15) & ~15));
				Uint32 *hvram_line = (Uint32 *)hvram;
				Uint32 *hvram_column = p2cline;
				Uint32 hvram_buf[16];

				/* Render the upper border */
				for (h = 0; h < upperBorder * coefy; h++) {
					Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>2;
				}

				/* Render the graphical area */
				for (h = 0; h < scrheight; h++) {
					fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
					scrIdx ++;
					nSampleHoldIdx = 0;

					/* Recopy the same line ? */
					if (screen_zoom.zoomytable[h] == cursrcline) {
						memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
					} else {
						Uint16 *fvram_column = fvram_line;
						hvram_column = p2cline;

						/* First 16 pixels of a new line */
						Screen_BitplaneToChunky32(fvram_column, vbpp, hvram_buf);
						for (j = hscrolloffset; j < 16; j++) {
							*hvram_column++ = hvram_buf[j];
						}
						fvram_column += vbpp;
						/* Convert the main part of the new line */
						for (w = 1; w < (vw+15)>>4; w++) {
							Screen_BitplaneToChunky32(fvram_column, vbpp, hvram_column);
							hvram_column += 16;
							fvram_column += vbpp;
						}
						/* Last pixels of the new line for fine scrolling */
						if (hscrolloffset) {
							Screen_BitplaneToChunky32(fvram_column, vbpp, hvram_buf);
							for (j = 0; j < hscrolloffset; j++) {
								*hvram_column++ = hvram_buf[j];
							}
						}

						hvram_column = hvram_line;
						/* Display the Left border */
						Screen_memset_uint32(hvram_column, palette.native[0], leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w=0; w<(vw*coefx); w++) {
							hvram_column[w] = p2cline[screen_zoom.zoomxtable[w]];
						}
						hvram_column += vw * coefx;

						/* Display the Right border */
						Screen_memset_uint32(hvram_column, palette.native[0], rightBorder * coefx);
						hvram_column += rightBorder * coefx;
					}

					hvram_line += scrpitch>>2;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>2;
				}

				free(p2cline);
			}
			break;
		}
	} else {
		/* Falcon high-color (16-bit) mode */

		switch(scrbpp) {
			case 2:
			{
				Uint16 *hvram_line = (Uint16 *)hvram;
				Uint16 *hvram_column = hvram_line;

				/* Render the upper border */
				for (h = 0; h < upperBorder * coefy; h++) {
					Screen_memset_uint16(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>1;
				}

				/* Render the graphical area */
				for (h = 0; h < scrheight; h++) {
					Uint16 *fvram_column;

					fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
					scrIdx ++;

					fvram_column = fvram_line;

					/* Recopy the same line ? */
					if (screen_zoom.zoomytable[h] == cursrcline) {
						memcpy(hvram_line, hvram_line-(scrpitch>>1), scrwidth*scrbpp);
					} else {

						hvram_column = hvram_line;

						/* Display the Left border */
						Screen_memset_uint16(hvram_column, palette.native[0], leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w=0; w<(vw*coefx); w++)
							*hvram_column++ = SDL_SwapBE16(fvram_column[screen_zoom.zoomxtable[w]]);


						/* Display the Right border */
						Screen_memset_uint16(hvram_column, palette.native[0], rightBorder * coefx);
						hvram_column += rightBorder * coefx;
					}

					hvram_line += scrpitch>>1;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					Screen_memset_uint16(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>1;
				}
			}
			break;
			case 4:
			{
				Uint32 *hvram_line = (Uint32 *)hvram;
				Uint32 *hvram_column = hvram_line;

				/* Render the upper border */
				for (h = 0; h < upperBorder * coefy; h++) {
					Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
				}

				/* Render the graphical area */
				for (h = 0; h < scrheight; h++) {
					Uint16 *fvram_column;

					fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
					scrIdx ++;
					fvram_column = fvram_line;

					/* Recopy the same line ? */
					if (screen_zoom.zoomytable[h] == cursrcline) {
						memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						hvram_line += scrpitch>>2;
					} else {

						hvram_column = hvram_line;

						/* Display the Left border */
						Screen_memset_uint32(hvram_column, palette.native[0], leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w = 0; w<(vw*coefx); w++) {
							Uint16 srcword;

							srcword = fvram_column[screen_zoom.zoomxtable[w]];
							*hvram_column++ = SDL_MapRGB(sdlscrn->format, srcword & 0xf8,
							                             ((srcword & 0x07) << 5) | ((srcword >> 11) & 0x3c),
							                             (srcword >> 5) & 0xf8);
						}

						/* Display the Right border */
						Screen_memset_uint32(hvram_column, palette.native[0], rightBorder * coefx);
						hvram_column += rightBorder * coefx;
					}

					hvram_line += scrpitch>>2;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
					hvram_line += scrpitch>>2;
				}
			}
			break;
		}
	}
}

void Screen_GenConvert(uint32_t vaddr, int vw, int vh, int vbpp, int nextline,
                       int leftBorderSize, int rightBorderSize,
                       int upperBorderSize, int lowerBorderSize)
{
	if (nScreenZoomX * nScreenZoomY != 1) {
		Screen_ConvertWithZoom(vaddr, vw, vh, vbpp, nextline,
		                       leftBorderSize, rightBorderSize,
		                       upperBorderSize, lowerBorderSize);
	} else {
		Screen_ConvertWithoutZoom(vaddr, vw, vh, vbpp, nextline,
		                          leftBorderSize, rightBorderSize,
		                          upperBorderSize, lowerBorderSize);
	}
}

bool Screen_GenDraw(uint32_t vaddr, int vw, int vh, int vbpp, int nextline,
                    int leftBorder, int rightBorder,
                    int upperBorder, int lowerBorder)
{
	if (!Screen_Lock())
		return false;

	Screen_GenConvert(vaddr, vw, vh, vbpp, nextline, leftBorder, rightBorder,
	                  upperBorder, lowerBorder);

	Screen_UnLock();
	Screen_GenConvUpdate(Statusbar_Update(sdlscrn, false), false);
	return true;
}
