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
static uint32_t nScreenBaseAddr;		/* address of screen in STRam */
int ConvertW = 0;
int ConvertH = 0;
int ConvertBPP = 1;
int ConvertNextLine = 0;

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

SDL_Color Screen_GetPaletteColor(Uint8 idx)
{
	return palette.standard[idx];
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


static inline Uint32 *ScreenConv_BitplaneLineTo32bpp(Uint16 *fvram_column,
                                                     Uint32 *hvram_column, int vw,
                                                     int vbpp, int hscrolloffset)
{
	Uint32 hvram_buf[16];
	int i;

	/* First 16 pixels */
	Screen_BitplaneToChunky32(fvram_column, vbpp, hvram_buf);
	for (i = hscrolloffset; i < 16; i++)
	{
		*hvram_column++ = hvram_buf[i];
	}
	fvram_column += vbpp;

	/* Now the main part of the line */
	for (i = 1; i < (vw + 15) >> 4; i++)
	{
		Screen_BitplaneToChunky32(fvram_column, vbpp, hvram_column);
		hvram_column += 16;
		fvram_column += vbpp;
	}

	/* Last pixels of the line for fine scrolling */
	if (hscrolloffset)
	{
		Screen_BitplaneToChunky32(fvram_column, vbpp, hvram_buf);
		for (i = 0; i < hscrolloffset; i++)
		{
			*hvram_column++ = hvram_buf[i];
		}
	}

	return hvram_column;
}


static void ScreenConv_BitplaneTo32bppNoZoom(Uint16 *fvram_line, Uint8 *hvram,
                                             int scrwidth, int scrheight,
                                             int vw, int vh, int vbpp,
                                             int nextline, int hscrolloffset,
                                             int leftBorder, int rightBorder,
                                             int upperBorder, int lowBorder)
{
	Uint32 *hvram_line = (Uint32 *)hvram;
	uint32_t nLineEndAddr = nScreenBaseAddr + nextline * 2;
	int pitch = sdlscrn->pitch >> 2;
	int h;

	/* Render the upper border */
	for (h = 0; h < upperBorder; h++)
	{
		Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}

	/* Render the graphical area */
	for (h = 0; h < vh; h++)
	{
		Uint32 *hvram_column = hvram_line;

		if (nLineEndAddr > STRamEnd)
		{
			Screen_memset_uint32(hvram_line, palette.native[0], pitch);
			hvram_line += pitch;
			continue;
		}

		nSampleHoldIdx = 0;

		/* Left border first */
		Screen_memset_uint32(hvram_column, palette.native[0], leftBorder);
		hvram_column += leftBorder;

		hvram_column = ScreenConv_BitplaneLineTo32bpp(fvram_line, hvram_column,
		                                              vw, vbpp, hscrolloffset);

		/* Right border */
		Screen_memset_uint32(hvram_column, palette.native[0], rightBorder);

		nLineEndAddr += nextline * 2;
		fvram_line += nextline;
		hvram_line += pitch;
	}

	/* Render the lower border */
	for (h = 0; h < lowBorder; h++)
	{
		Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}
}

static void ScreenConv_HiColorTo32bppNoZoom(Uint16 *fvram_line, Uint8 *hvram,
                                            int scrwidth, int scrheight,
                                            int vw, int vh, int vbpp,
                                            int nextline, int hscrolloffset,
                                            int leftBorder, int rightBorder,
                                            int upperBorder, int lowBorder)
{
	Uint32 *hvram_line = (Uint32 *)hvram;
	uint32_t nLineEndAddr = nScreenBaseAddr + nextline * 2;
	int pitch = sdlscrn->pitch >> 2;
	int h, w;

	/* Render the upper border */
	for (h = 0; h < upperBorder; h++)
	{
		Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}

	/* Render the graphical area */
	for (h = 0; h < vh; h++)
	{
		Uint16 *fvram_column = fvram_line;
		Uint32 *hvram_column = hvram_line;

		if (nLineEndAddr > STRamEnd)
		{
			Screen_memset_uint32(hvram_line, palette.native[0], pitch);
			hvram_line += pitch;
			continue;
		}

		/* Left border first */
		Screen_memset_uint32(hvram_column, palette.native[0], leftBorder);
		hvram_column += leftBorder;

		/* Graphical area */
		for (w = 0; w < vw; w++)
		{
			Uint16 srcword = SDL_SwapBE16(*fvram_column++);
			Uint8 r = ((srcword >> 8) & 0xf8) | (srcword >> 13);
			Uint8 g = ((srcword >> 3) & 0xfc) | ((srcword >> 9) & 0x3);
			Uint8 b = (srcword << 3) | ((srcword >> 2) & 0x07);
			*hvram_column ++ = SDL_MapRGB(sdlscrn->format, r, g, b);
		}

		/* Right border */
		Screen_memset_uint32(hvram_column, palette.native[0], rightBorder);

		nLineEndAddr += nextline * 2;
		fvram_line += nextline;
		hvram_line += pitch;
	}

	/* Render the bottom border */
	for (h = 0; h < lowBorder; h++)
	{
		Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}
}

static void Screen_ConvertWithoutZoom(Uint16 *fvram, int vw, int vh, int vbpp, int nextline,
                                      int hscrolloffset, int leftBorder, int rightBorder,
                                      int upperBorder, int lowerBorder)
{
	Uint8 *hvram = sdlscrn->pixels;

	Uint16 lowBorderSize, rightBorderSize;
	int scrwidth, scrheight;
	unsigned int nBytesPerPixel = sdlscrn->format->BytesPerPixel;
	int vw_clip, vh_clip;

	/* Horizontal scroll register set? */
	if (hscrolloffset) {
		/* Yes, so we need to adjust offset to next line: */
		nextline += vbpp;
		ConvertNextLine = nextline * 2;
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
	hvram += ((scrheight-vh_clip)>>1) * sdlscrn->pitch;
	hvram += ((scrwidth-vw_clip)>>1) * nBytesPerPixel;

	scrwidth = leftBorder + vw + rightBorder;

	/* render the graphic area */
	if (vbpp < 16) {
		/* Bitplanes modes */
		ScreenConv_BitplaneTo32bppNoZoom(fvram, hvram,
		                                 scrwidth, scrheight, vw, vh,
		                                 vbpp, nextline, hscrolloffset,
		                                 leftBorder, rightBorderSize,
		                                 upperBorder, lowBorderSize);
	} else {
		/* Falcon TC (High Color) */
		ScreenConv_HiColorTo32bppNoZoom(fvram, hvram,
		                                scrwidth, scrheight, vw, vh,
		                                vbpp, nextline, hscrolloffset,
		                                leftBorder, rightBorderSize,
		                                upperBorder, lowBorderSize);
	}
}


static void ScreenConv_BitplaneTo32bppZoomed(Uint16 *fvram, Uint8 *hvram,
                                             int scrwidth, int scrheight,
                                             int vw, int vh, int vbpp,
                                             int nextline, int hscrolloffset,
                                             int leftBorder, int rightBorder,
                                             int upperBorder, int lowerBorder,
                                             int coefx, int coefy)
{
	/* One complete 16-pixel aligned planar 2 chunky line */
	Uint32 *p2cline = malloc(sizeof(Uint32) * ((vw+15) & ~15));
	Uint32 *hvram_line = (Uint32 *)hvram;
	Uint32 *hvram_column = p2cline;
	Uint16 *fvram_line;
	uint32_t nLineEndAddr = nScreenBaseAddr + nextline * 2;
	unsigned int nBytesPerPixel = sdlscrn->format->BytesPerPixel;
	int pitch = sdlscrn->pitch >> 2;
	int cursrcline = -1;
	int scrIdx = 0;
	int w, h;

	/* Render the upper border */
	for (h = 0; h < upperBorder * coefy; h++)
	{
		Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}

	/* Render the graphical area */
	for (h = 0; h < scrheight; h++)
	{
		fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
		scrIdx ++;
		nSampleHoldIdx = 0;

		/* Recopy the same line ? */
		if (screen_zoom.zoomytable[h] == cursrcline)
		{
			memcpy(hvram_line, hvram_line - pitch, scrwidth * nBytesPerPixel);
		}
		else if (nLineEndAddr > STRamEnd)
		{
			Screen_memset_uint32(hvram_line, palette.native[0], pitch);
		}
		else
		{
			ScreenConv_BitplaneLineTo32bpp(fvram_line, p2cline,
			                               vw, vbpp, hscrolloffset);

			hvram_column = hvram_line;
			/* Display the Left border */
			Screen_memset_uint32(hvram_column, palette.native[0], leftBorder * coefx);
			hvram_column += leftBorder * coefx;

			/* Display the Graphical area */
			for (w = 0; w < vw * coefx; w++)
			{
				hvram_column[w] = p2cline[screen_zoom.zoomxtable[w]];
			}
			hvram_column += vw * coefx;

			/* Display the Right border */
			Screen_memset_uint32(hvram_column, palette.native[0], rightBorder * coefx);

			nLineEndAddr += nextline * 2;
		}

		hvram_line += pitch;
		cursrcline = screen_zoom.zoomytable[h];
	}

	/* Render the lower border */
	for (h = 0; h < lowerBorder * coefy; h++)
	{
		Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}

	free(p2cline);
}

static void ScreenConv_HiColorTo32bppZoomed(Uint16 *fvram, Uint8 *hvram,
                                            int scrwidth, int scrheight,
                                            int vw, int vh, int vbpp,
                                            int nextline, int hscrolloffset,
                                            int leftBorder, int rightBorder,
                                            int upperBorder, int lowerBorder,
                                            int coefx, int coefy)
{
	Uint32 *hvram_line = (Uint32 *)hvram;
	Uint32 *hvram_column = hvram_line;
	Uint16 *fvram_line;
	uint32_t nLineEndAddr = nScreenBaseAddr + nextline * 2;
	unsigned int nBytesPerPixel = sdlscrn->format->BytesPerPixel;
	int pitch = sdlscrn->pitch >> 2;
	int cursrcline = -1;
	int scrIdx = 0;
	int w, h;

	/* Render the upper border */
	for (h = 0; h < upperBorder * coefy; h++)
	{
		Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}

	/* Render the graphical area */
	for (h = 0; h < scrheight; h++)
	{
		Uint16 *fvram_column;

		fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
		scrIdx ++;
		fvram_column = fvram_line;

		/* Recopy the same line ? */
		if (screen_zoom.zoomytable[h] == cursrcline)
		{
			memcpy(hvram_line, hvram_line - pitch, scrwidth * nBytesPerPixel);
		}
		else if (nLineEndAddr > STRamEnd)
		{
			Screen_memset_uint32(hvram_line, palette.native[0], pitch);
		}
		else
		{
			hvram_column = hvram_line;

			/* Display the Left border */
			Screen_memset_uint32(hvram_column, palette.native[0], leftBorder * coefx);
			hvram_column += leftBorder * coefx;

			/* Display the Graphical area */
			for (w = 0; w < vw * coefx; w++)
			{
				Uint16 srcword;
				Uint8 r, g, b;
				srcword = SDL_SwapBE16(fvram_column[screen_zoom.zoomxtable[w]]);
				r = ((srcword >> 8) & 0xf8) | (srcword >> 13);
				g = ((srcword >> 3) & 0xfc) | ((srcword >> 9) & 0x3);
				b = (srcword << 3) | ((srcword >> 2) & 0x07);
				*hvram_column ++ = SDL_MapRGB(sdlscrn->format, r, g, b);
			}

			/* Display the Right border */
			Screen_memset_uint32(hvram_column, palette.native[0], rightBorder * coefx);

			nLineEndAddr += nextline * 2;
		}

		hvram_line += pitch;
		cursrcline = screen_zoom.zoomytable[h];
	}

	/* Render the lower border */
	for (h = 0; h < lowerBorder * coefy; h++)
	{
		Screen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}
}

static void Screen_ConvertWithZoom(Uint16 *fvram, int vw, int vh, int vbpp, int nextline,
                                   int hscrolloffset, int leftBorder, int rightBorder,
                                   int upperBorder, int lowerBorder)
{
	int coefx = 1;
	int coefy = 1;
	int scrpitch, scrwidth, scrheight, scrbpp;
	Uint8 *hvram;
	int vw_b, vh_b;
	int i;

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

	/* Horizontal scroll register set? */
	if (hscrolloffset) {
		/* Yes, so we need to adjust offset to next line: */
		nextline += vbpp;
		ConvertNextLine = nextline * 2;
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

	/* scrwidth must not change */
	scrheight = vh * coefy;

	if (vw < 16) {
		Log_Printf(LOG_WARN, "Videl <16 screen width (%dx%d without borders)\n"
		           "If this happens at TOS boot, remove hatari.nvram,\n"
		           "NVRAM video settings in it are corrupted.\n", vw, vh);
		/* prevent memory corruption */
		return;
	}

	if (vbpp<16) {
		/* Bitplanes modes */
		ScreenConv_BitplaneTo32bppZoomed(fvram, hvram, scrwidth, scrheight,
		                                 vw, vh, vbpp, nextline, hscrolloffset,
		                                 leftBorder, rightBorder, upperBorder,
		                                 lowerBorder, coefx, coefy);
	} else {
		/* Falcon high-color (16-bit) mode */
		ScreenConv_HiColorTo32bppZoomed(fvram, hvram, scrwidth, scrheight,
		                                vw, vh, vbpp, nextline, hscrolloffset,
		                                leftBorder, rightBorder, upperBorder,
		                                lowerBorder, coefx, coefy);
	}
}

void Screen_GenConvert(uint32_t vaddr, void *fvram, int vw, int vh,
                       int vbpp, int nextline, int hscroll,
                       int leftBorderSize, int rightBorderSize,
                       int upperBorderSize, int lowerBorderSize)
{
	nScreenBaseAddr = vaddr;
	ConvertW = vw;
	ConvertH = vh;
	ConvertBPP = vbpp;
	ConvertNextLine = nextline * 2;	/* bytes per line */

	/* Override drawing palette for screenshots */
	ConvertPalette = palette.native;
	ConvertPaletteSize = 1 << vbpp;
	if (ConvertPaletteSize > 256)
		ConvertPaletteSize = 256;

	if (nScreenZoomX * nScreenZoomY != 1) {
		Screen_ConvertWithZoom(fvram, vw, vh, vbpp, nextline, hscroll,
		                       leftBorderSize, rightBorderSize,
		                       upperBorderSize, lowerBorderSize);
	} else {
		Screen_ConvertWithoutZoom(fvram, vw, vh, vbpp, nextline, hscroll,
		                          leftBorderSize, rightBorderSize,
		                          upperBorderSize, lowerBorderSize);
	}
}

bool Screen_GenDraw(uint32_t vaddr, int vw, int vh, int vbpp, int nextline,
                    int leftBorder, int rightBorder,
                    int upperBorder, int lowerBorder)
{
	int hscrolloffset;

	if (ConfigureParams.Screen.DisableVideo || !Screen_Lock())
		return false;

	if (Config_IsMachineST())
		hscrolloffset = 0;
	else
		hscrolloffset = IoMem_ReadByte(0xff8265) & 0x0f;

	Screen_GenConvert(vaddr, &STRam[vaddr], vw, vh, vbpp, nextline, hscrolloffset,
	                  leftBorder, rightBorder, upperBorder, lowerBorder);

	Screen_UnLock();
	Screen_GenConvUpdate(Statusbar_Update(sdlscrn, false), false);
	return true;
}
