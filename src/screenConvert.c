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
#include "screen.h"
#include "screenConvert.h"
#include "stMemory.h"
#include "falcon/videl.h"
#include "falcon/hostscreen.h"

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


static void VIDEL_memset_uint32(Uint32 *addr, Uint32 color, int count)
{
	while (count-- > 0) {
		*addr++ = color;
	}
}

static void VIDEL_memset_uint16(Uint16 *addr, Uint16 color, int count)
{
	while (count-- > 0) {
		*addr++ = color;
	}
}

static void VIDEL_memset_uint8(Uint8 *addr, Uint8 color, int count)
{
	memset(addr, color, count);
}

/**
 * Performs conversion from the TOS's bitplane word order (big endian) data
 * into the native chunky color index.
 */
static void VIDEL_bitplaneToChunky(Uint16 *atariBitplaneData, Uint16 bpp,
                                   Uint8 colorValues[16])
{
	Uint32 a, b, c, d, x;

	/* Obviously the different cases can be broken out in various
	 * ways to lessen the amount of work needed for <8 bit modes.
	 * It's doubtful if the usage of those modes warrants it, though.
	 * The branches below should be ~100% correctly predicted and
	 * thus be more or less for free.
	 * Getting the palette values inline does not seem to help
	 * enough to worry about. The palette lookup is much slower than
	 * this code, though, so it would be nice to do something about it.
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

	x = a;
	a =  (a & 0xf0f0f0f0)       | ((c & 0xf0f0f0f0) >> 4);
	c = ((x & 0x0f0f0f0f) << 4) |  (c & 0x0f0f0f0f);
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

	colorValues[ 8] = a;
	a >>= 8;
	colorValues[ 0] = a;
	a >>= 8;
	colorValues[ 9] = a;
	a >>= 8;
	colorValues[ 1] = a;

	colorValues[10] = b;
	b >>= 8;
	colorValues[ 2] = b;
	b >>= 8;
	colorValues[11] = b;
	b >>= 8;
	colorValues[ 3] = b;

	colorValues[12] = c;
	c >>= 8;
	colorValues[ 4] = c;
	c >>= 8;
	colorValues[13] = c;
	c >>= 8;
	colorValues[ 5] = c;

	colorValues[14] = d;
	d >>= 8;
	colorValues[ 6] = d;
	d >>= 8;
	colorValues[15] = d;
	d >>= 8;
	colorValues[ 7] = d;
#else
	a = (a & 0xaaaa5555) | ((a & 0x0000aaaa) << 15) | ((a & 0x55550000) >> 15);
	b = (b & 0xaaaa5555) | ((b & 0x0000aaaa) << 15) | ((b & 0x55550000) >> 15);
	c = (c & 0xaaaa5555) | ((c & 0x0000aaaa) << 15) | ((c & 0x55550000) >> 15);
	d = (d & 0xaaaa5555) | ((d & 0x0000aaaa) << 15) | ((d & 0x55550000) >> 15);

	colorValues[ 1] = a;
	a >>= 8;
	colorValues[ 9] = a;
	a >>= 8;
	colorValues[ 0] = a;
	a >>= 8;
	colorValues[ 8] = a;

	colorValues[ 3] = b;
	b >>= 8;
	colorValues[11] = b;
	b >>= 8;
	colorValues[ 2] = b;
	b >>= 8;
	colorValues[10] = b;

	colorValues[ 5] = c;
	c >>= 8;
	colorValues[13] = c;
	c >>= 8;
	colorValues[ 4] = c;
	c >>= 8;
	colorValues[12] = c;

	colorValues[ 7] = d;
	d >>= 8;
	colorValues[15] = d;
	d >>= 8;
	colorValues[ 6] = d;
	d >>= 8;
	colorValues[14] = d;
#endif
}

static void Screen_ConvertWithoutZoom(uint32_t vaddr, int vw, int vh, int vbpp, int nextline,
                                      int leftBorder, int rightBorder,
                                      int upperBorder, int lowerBorder)
{
	int scrpitch = HostScreen_getPitch();
	int h, w, j;

	Uint16 *fvram = (Uint16 *) Atari2HostAddr(vaddr);
	Uint16 *fvram_line;
	Uint8 *hvram = HostScreen_getVideoramAddress();
	SDL_PixelFormat *scrfmt = HostScreen_getFormat();

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
	if (ConfigureParams.System.nMachineType != MACHINE_TT) {
		bTTSampleHold = false;
	}

	/* Clip to SDL_Surface dimensions */
	scrwidth = HostScreen_getWidth();
	scrheight = HostScreen_getHeight();
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
	hvram += ((scrwidth-vw_clip)>>1)*HostScreen_getBpp();

	fvram_line = fvram;
	scrwidth = leftBorder + vw + rightBorder;

	/* render the graphic area */
	if (vbpp < 16) {
		/* Bitplanes modes */

		/* The SDL colors blitting... */
		Uint8 color[16];

		/* FIXME: The byte swap could be done here by enrolling the loop into 2 each by 8 pixels */
		switch ( HostScreen_getBpp() ) {
			case 1:
			{
				Uint8 *hvram_line = hvram;

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint8 *hvram_column = hvram_line;

					/* Left border first */
					VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), leftBorder);
					hvram_column += leftBorder;

					/* First 16 pixels */
					VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
					memcpy(hvram_column, color+hscrolloffset, 16-hscrolloffset);
					hvram_column += 16-hscrolloffset;
					fvram_column += vbpp;
					/* Now the main part of the line */
					for (w = 1; w < (vw+15)>>4; w++) {
						VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
						memcpy(hvram_column, color, 16);
						hvram_column += 16;
						fvram_column += vbpp;
					}
					/* Last pixels of the line for fine scrolling */
					if (hscrolloffset) {
						VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
						memcpy(hvram_column, color, hscrolloffset);
					}
					/* Right border */
					VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					if (bTTSampleHold) {
						Uint8 TMPPixel = 0;
						for (w=0; w < (vw); w++) {
							if (hvram_line[w] == 0) {
								hvram_line[w] = TMPPixel;
							} else {
								TMPPixel = hvram_line[w];
							}
						}
					}

					fvram_line += nextline;
					hvram_line += scrpitch;
				}

				/* Render the lower border */
				for (h = 0; h < lowBorderSize; h++) {
					VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch;
				}
			}
			break;
			case 2:
			{
				Uint16 *hvram_line = (Uint16 *)hvram;

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>1;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint16 *hvram_column = hvram_line;

					/* Left border first */
					VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), leftBorder);
					hvram_column += leftBorder;

					/* First 16 pixels */
					VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
					for (j = 0; j < 16 - hscrolloffset; j++) {
						*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
					}
					fvram_column += vbpp;
					/* Now the main part of the line */
					for (w = 1; w < (vw+15)>>4; w++) {
						VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
						for (j=0; j<16; j++) {
							*hvram_column++ = HostScreen_getPaletteColor( color[j] );
						}
						fvram_column += vbpp;
					}
					/* Last pixels of the line for fine scrolling */
					if (hscrolloffset) {
						VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
						for (j = 0; j < hscrolloffset; j++) {
							*hvram_column++ = HostScreen_getPaletteColor(color[j]);
						}
					}
					/* Right border */
					VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch>>1;
				}

				/* Render the lower border */
				for (h = 0; h < lowBorderSize; h++) {
					VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>1;
				}
			}
			break;
			case 4:
			{
				Uint32 *hvram_line = (Uint32 *)hvram;

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>2;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint32 *hvram_column = hvram_line;

					/* Left border first */
					VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), leftBorder);
					hvram_column += leftBorder;

					/* First 16 pixels */
					VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
					for (j = 0; j < 16 - hscrolloffset; j++) {
						*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
					}
					fvram_column += vbpp;
					/* Now the main part of the line */
					for (w = 1; w < (vw+15)>>4; w++) {
						VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
						for (j=0; j<16; j++) {
							*hvram_column++ = HostScreen_getPaletteColor( color[j] );
						}
						fvram_column += vbpp;
					}
					/* Last pixels of the line for fine scrolling */
					if (hscrolloffset) {
						VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
						for (j = 0; j < hscrolloffset; j++) {
							*hvram_column++ = HostScreen_getPaletteColor(color[j]);
						}
					}
					/* Right border */
					VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch>>2;
				}

				/* Render the lower border */
				for (h = 0; h < lowBorderSize; h++) {
					VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>2;
				}
			}
			break;
		}

	} else {

		/* Falcon TC (High Color) */
		switch ( HostScreen_getBpp() )  {
			case 1:
			{
				/* FIXME: when Videl switches to 16bpp, set the palette to 3:3:2 */
				Uint8 *hvram_line = hvram;

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint8 *hvram_column = hvram_line;
					int tmp;

					/* Left border first */
					VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), leftBorder);
					hvram_column += leftBorder;

					/* Graphical area */
					for (w = 0; w < vw; w++) {
						tmp = SDL_SwapBE16(*fvram_column++);
						*hvram_column++ = (((tmp>>13) & 7) << 5) + (((tmp>>8) & 7) << 2) + (((tmp>>2) & 3));
					}

					/* Right border */
					VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch;
				}
				/* Render the bottom border */
				for (h = 0; h < lowBorderSize; h++) {
					VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch;
				}
			}
			break;
			case 2:
			{
				Uint16 *hvram_line = (Uint16 *)hvram;

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>1;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *hvram_column = hvram_line;
#if SDL_BYTEORDER != SDL_BIG_ENDIAN
					Uint16 *fvram_column;
#endif
					/* Left border first */
					VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), leftBorder);
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
					VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch>>1;
				}

				/* Render the bottom border */
				for (h = 0; h < lowBorderSize; h++) {
					VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>1;
				}
			}
			break;
			case 4:
			{
				Uint32 *hvram_line = (Uint32 *)hvram;

				/* Render the upper border */
				for (h = 0; h < upperBorder; h++) {
					VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>2;
				}

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint32 *hvram_column = hvram_line;

					/* Left border first */
					VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), leftBorder);
					hvram_column += leftBorder;

					/* Graphical area */
					for (w = 0; w < vw; w++) {
						Uint16 srcword = *fvram_column++;
						*hvram_column ++ = SDL_MapRGB(scrfmt, (srcword & 0xf8), (((srcword & 0x07) << 5) | ((srcword >> 11) & 0x3c)), ((srcword >> 5) & 0xf8));
					}

					/* Right border */
					VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);
				}

				fvram_line += nextline;
				hvram_line += scrpitch>>2;

				/* Render the bottom border */
				for (h = 0; h < lowBorderSize; h++) {
					VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
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
	SDL_PixelFormat *scrfmt;
	int vw_b, vh_b;

	/* The sample-hold feature exists only on the TT */
	if (ConfigureParams.System.nMachineType != MACHINE_TT) {
		bTTSampleHold = false;
	}

	vw_b = vw + leftBorder + rightBorder;
	vh_b = vh + upperBorder + lowerBorder;

	/* Host screen infos */
	scrpitch = HostScreen_getPitch();
	scrwidth = HostScreen_getWidth();
	scrheight = HostScreen_getHeight();
	scrbpp = HostScreen_getBpp();
	scrfmt = HostScreen_getFormat();
	hvram = (Uint8 *) HostScreen_getVideoramAddress();

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
		hvram += ((HostScreen_getHeight()-scrheight)>>1)*scrpitch;
		hvram += ((HostScreen_getWidth()-scrwidth)>>1)*scrbpp;
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
		Uint8 color[16];

		/* Bitplanes modes */
		switch(scrbpp) {
			case 1:
			{
				/* One complete 16-pixel aligned planar 2 chunky line */
				Uint8 *p2cline = malloc(sizeof(Uint8) * ((vw+15) & ~15));
				Uint8 *hvram_line = hvram;
				Uint8 *hvram_column = p2cline;

				/* Render the upper border */
				for (h = 0; h < upperBorder * coefy; h++) {
					VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch;
				}

				/* Render the graphical area */
				for (h = 0; h < scrheight; h++) {

					fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
					scrIdx ++;

					/* Recopy the same line ? */
					if (screen_zoom.zoomytable[h] == cursrcline) {
						memcpy(hvram_line, hvram_line-scrpitch, scrwidth*scrbpp);
					} else {
						Uint16 *fvram_column = fvram_line;
						hvram_column = p2cline;

						/* First 16 pixels of a new line */
						VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
						memcpy(hvram_column, color+hscrolloffset, 16-hscrolloffset);
						hvram_column += 16-hscrolloffset;
						fvram_column += vbpp;
						/* Convert main part of the new line */
						for (w=1; w < (vw+15)>>4; w++) {
							VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
							memcpy(hvram_column, color, 16);
							hvram_column += 16;
							fvram_column += vbpp;
						}
						/* Last pixels of the line for fine scrolling */
						if (hscrolloffset) {
							VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
							memcpy(hvram_column, color, hscrolloffset);
						}

						hvram_column = hvram_line;

						/* Display the Left border */
						VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w=0; w<(vw*coefx); w++)
							hvram_column[w] = p2cline[screen_zoom.zoomxtable[w - leftBorder * coefx]];
						hvram_column += vw * coefx;

						/* Display the Right border */
						VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), rightBorder * coefx);
						hvram_column += rightBorder * coefx;

						if (bTTSampleHold) {
							Uint8 TMPPixel = 0;
							for (w=0; w < (vw*coefx); w++) {
								if (hvram_line[w] == 0) {
									hvram_line[w] = TMPPixel;
								} else {
									TMPPixel = hvram_line[w];
								}
							}
						}

					}

					hvram_line += scrpitch;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch;
				}

				free(p2cline);
			}
			break;
			case 2:
			{
				/* One complete 16-pixel aligned planar 2 chunky line */
				Uint16 *p2cline = malloc(sizeof(Uint16) * ((vw+15) & ~15));
				Uint16 *hvram_line = (Uint16 *)hvram;
				Uint16 *hvram_column = p2cline;

				/* Render the upper border */
				for (h = 0; h < upperBorder * coefy; h++) {
					VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>1;
				}

				/* Render the graphical area */
				for (h = 0; h < scrheight; h++) {
					fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
					scrIdx ++;

					/* Recopy the same line ? */
					if (screen_zoom.zoomytable[h] == cursrcline) {
						memcpy(hvram_line, hvram_line-(scrpitch>>1), scrwidth*scrbpp);
					} else {
						Uint16 *fvram_column = fvram_line;
						hvram_column = p2cline;

						/* First 16 pixels of a new line */
						VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
						for (j = 0; j < 16 - hscrolloffset; j++) {
							*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
						}
						fvram_column += vbpp;
						/* Convert the main part of the new line */
						for (w = 1; w < (vw+15)>>4; w++) {
							VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
							for (j=0; j<16; j++) {
								*hvram_column++ = HostScreen_getPaletteColor( color[j] );
							}
							fvram_column += vbpp;
						}
						/* Last pixels of the new line for fine scrolling */
						if (hscrolloffset) {
							VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
							for (j = 0; j < hscrolloffset; j++) {
								*hvram_column++ = HostScreen_getPaletteColor(color[j]);
							}
						}

						hvram_column = hvram_line;

						/* Display the Left border */
						VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w=0; w<(vw*coefx); w++)
							hvram_column[w] = p2cline[screen_zoom.zoomxtable[w]];
						hvram_column += vw * coefx;

						/* Display the Right border */
						VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), rightBorder * coefx);
						hvram_column += rightBorder * coefx;
					}

					hvram_line += scrpitch>>1;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
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

				/* Render the upper border */
				for (h = 0; h < upperBorder * coefy; h++) {
					VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>2;
				}

				/* Render the graphical area */
				for (h = 0; h < scrheight; h++) {
					fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
					scrIdx ++;
					/* Recopy the same line ? */
					if (screen_zoom.zoomytable[h] == cursrcline) {
						memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
					} else {
						Uint16 *fvram_column = fvram_line;
						hvram_column = p2cline;

						/* First 16 pixels of a new line */
						VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
						for (j = 0; j < 16 - hscrolloffset; j++) {
							*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
						}
						fvram_column += vbpp;
						/* Convert the main part of the new line */
						for (w = 1; w < (vw+15)>>4; w++) {
							VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
							for (j=0; j<16; j++) {
								*hvram_column++ = HostScreen_getPaletteColor( color[j] );
							}
							fvram_column += vbpp;
						}
						/* Last pixels of the new line for fine scrolling */
						if (hscrolloffset) {
							VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
							for (j = 0; j < hscrolloffset; j++) {
								*hvram_column++ = HostScreen_getPaletteColor(color[j]);
							}
						}

						hvram_column = hvram_line;
						/* Display the Left border */
						VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w=0; w<(vw*coefx); w++) {
							hvram_column[w] = p2cline[screen_zoom.zoomxtable[w]];
						}
						hvram_column += vw * coefx;

						/* Display the Right border */
						VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), rightBorder * coefx);
						hvram_column += rightBorder * coefx;
					}

					hvram_line += scrpitch>>2;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch>>2;
				}

				free(p2cline);
			}
			break;
		}
	} else {
		/* Falcon high-color (16-bit) mode */

		switch(scrbpp) {
			case 1:
			{
				/* FIXME: when Videl switches to 16bpp, set the palette to 3:3:2 */
				Uint8 *hvram_line = hvram;
				Uint8 *hvram_column = hvram_line;

				/* Render the upper border */
				for (h = 0; h < upperBorder * coefy; h++) {
					VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch;
				}

				/* Render the graphical area */
				for (h = 0; h < scrheight; h++) {
					Uint16 *fvram_column;

					fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
					scrIdx ++;

					fvram_column = fvram_line;

					/* Recopy the same line ? */
					if (screen_zoom.zoomytable[h] == cursrcline) {
						memcpy(hvram_line, hvram_line-scrpitch, scrwidth*scrbpp);
					} else {

						hvram_column = hvram_line;
						/* Display the Left border */
						VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w = 0; w<(vw*coefx); w++) {
							Uint16 srcword;
							Uint8 dstbyte;

							srcword = SDL_SwapBE16(fvram_column[screen_zoom.zoomxtable[w - leftBorder * coefx]]);

							dstbyte = ((srcword>>13) & 7) << 5;
							dstbyte |= ((srcword>>8) & 7) << 2;
							dstbyte |= ((srcword>>2) & 3);
							*hvram_column++ = dstbyte;
						}

						/* Display the Right border */
						VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), rightBorder * coefx);
						hvram_column += rightBorder * coefx;
					}

					hvram_line += scrpitch;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
					hvram_line += scrpitch;
				}
			}
			break;
			case 2:
			{
				Uint16 *hvram_line = (Uint16 *)hvram;
				Uint16 *hvram_column = hvram_line;

				/* Render the upper border */
				for (h = 0; h < upperBorder * coefy; h++) {
					VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
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
						VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w=0; w<(vw*coefx); w++)
							*hvram_column++ = SDL_SwapBE16(fvram_column[screen_zoom.zoomxtable[w]]);


						/* Display the Right border */
						VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), rightBorder * coefx);
						hvram_column += rightBorder * coefx;
					}

					hvram_line += scrpitch>>1;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
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
					VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
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
						VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), leftBorder * coefx);
						hvram_column += leftBorder * coefx;

						/* Display the Graphical area */
						for (w = 0; w<(vw*coefx); w++) {
							Uint16 srcword;

							srcword = fvram_column[screen_zoom.zoomxtable[w]];
							*hvram_column++ = SDL_MapRGB(scrfmt, (srcword & 0xf8), (((srcword & 0x07) << 5) | ((srcword >> 11) & 0x3c)), ((srcword >> 5) & 0xf8));
						}

						/* Display the Right border */
						VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), rightBorder * coefx);
						hvram_column += rightBorder * coefx;
					}

					hvram_line += scrpitch>>2;
					cursrcline = screen_zoom.zoomytable[h];
				}

				/* Render the lower border */
				for (h = 0; h < lowerBorder * coefy; h++) {
					VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), scrwidth);
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
