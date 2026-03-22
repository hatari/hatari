/*
  Hatari - conv_get.c

  Generic screen conversion functions.

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "main.h"
#include "configuration.h"
#include "conv_gen.h"
#include "conv_st.h"
#include "endianswap.h"
#include "gui_event.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "screen.h"
#include "statusbar.h"
#include "stMemory.h"
#include "video.h"
#include "vdi.h"


struct screen_zoom_s
{
	uint16_t zoomwidth;
	uint16_t prev_scrwidth;
	uint16_t zoomheight;
	uint16_t prev_scrheight;
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

struct rgba
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

/* TOS palette (bpp < 16) to host color mapping */
static struct
{
	struct rgba standard[256];
	uint32_t native[256];
} palette;

void ConvGen_SetPaletteColor(uint8_t idx, uint8_t red, uint8_t green, uint8_t blue)
{
	// set the standard RGB palette settings
	palette.standard[idx].r = red;
	palette.standard[idx].g = green;
	palette.standard[idx].b = blue;
	// convert the color to native
	palette.native[idx] = Screen_MapRGB(red, green, blue);
}

void ConvGen_GetPaletteColor(int idx, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = palette.standard[idx].r;
	*g = palette.standard[idx].g;
	*b = palette.standard[idx].b;
}

void ConvGen_RemapPalette(void)
{
	int i;
	uint32_t *native = palette.native;
	struct rgba *standard = palette.standard;

	for(i = 0; i < 256; i++, native++, standard++) {
		*native = Screen_MapRGB(standard->r, standard->g, standard->b);
	}
}

void ConvGen_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(palette.standard, sizeof(palette.standard));
	if (!bSave)
		ConvGen_RemapPalette();
}

static void ConvGen_memset_uint32(uint32_t *addr, uint32_t color, int count)
{
	while (count-- > 0) {
		*addr++ = color;
	}
}

static inline uint32_t idx2pal(uint8_t idx)
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
static void ConvGen_BitplaneToChunky32(uint16_t *atariBitplaneData,
                                       uint16_t bpp, uint32_t *hvram)
{
	uint32_t a, b, c, d, x;

	if (bpp >= 4) {
		d = *(uint32_t *)&atariBitplaneData[0];
		c = *(uint32_t *)&atariBitplaneData[2];
		if (bpp == 4) {
			a = b = 0;
		} else {
			b = *(uint32_t *)&atariBitplaneData[4];
			a = *(uint32_t *)&atariBitplaneData[6];
		}

		x = a;
		a =  (a & 0xf0f0f0f0)       | ((c & 0xf0f0f0f0) >> 4);
		c = ((x & 0x0f0f0f0f) << 4) |  (c & 0x0f0f0f0f);
	} else {
		a = b = c = 0;
		if (bpp == 2) {
			d = *(uint32_t *)&atariBitplaneData[0];
		} else {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
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

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
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


static inline uint32_t *ConvGen_BitplaneLineTo32bpp(uint16_t *fvram_column,
                                                    uint32_t *hvram_column, int vw,
                                                    int vbpp, int hscrolloffset)
{
	uint32_t hvram_buf[16];
	int i;

	/* First 16 pixels */
	ConvGen_BitplaneToChunky32(fvram_column, vbpp, hvram_buf);
	for (i = hscrolloffset; i < 16; i++)
	{
		*hvram_column++ = hvram_buf[i];
	}
	fvram_column += vbpp;

	/* Now the main part of the line */
	for (i = 1; i < (vw + 15) >> 4; i++)
	{
		ConvGen_BitplaneToChunky32(fvram_column, vbpp, hvram_column);
		hvram_column += 16;
		fvram_column += vbpp;
	}

	/* Last pixels of the line for fine scrolling */
	if (hscrolloffset)
	{
		ConvGen_BitplaneToChunky32(fvram_column, vbpp, hvram_buf);
		for (i = 0; i < hscrolloffset; i++)
		{
			*hvram_column++ = hvram_buf[i];
		}
	}

	return hvram_column;
}


static void ConvGen_BitplaneTo32bppNoZoom(uint16_t *fvram_line,
                                          uint32_t *hvram, int pitch,
                                          int scrwidth, int scrheight,
                                          int vw, int vh, int vbpp,
                                          int nextline, int hscrolloffset,
                                          int leftBorder, int rightBorder,
                                          int upperBorder, int lowBorder)
{
	uint32_t *hvram_line = hvram;
	uint32_t nLineEndAddr = nScreenBaseAddr + nextline * 2;
	int h;

	/* Render the upper border */
	for (h = 0; h < upperBorder; h++)
	{
		ConvGen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}

	/* Render the graphical area */
	for (h = 0; h < vh; h++)
	{
		uint32_t *hvram_column = hvram_line;

		if (nLineEndAddr > STRamEnd)
		{
			ConvGen_memset_uint32(hvram_line, palette.native[0], pitch);
			hvram_line += pitch;
			continue;
		}

		nSampleHoldIdx = 0;

		/* Left border first */
		ConvGen_memset_uint32(hvram_column, palette.native[0], leftBorder);
		hvram_column += leftBorder;

		hvram_column = ConvGen_BitplaneLineTo32bpp(fvram_line, hvram_column,
		                                              vw, vbpp, hscrolloffset);

		/* Right border */
		ConvGen_memset_uint32(hvram_column, palette.native[0], rightBorder);

		nLineEndAddr += nextline * 2;
		fvram_line += nextline;
		hvram_line += pitch;
	}

	/* Render the lower border */
	for (h = 0; h < lowBorder; h++)
	{
		ConvGen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}
}

static void ConvGen_HiColorTo32bppNoZoom(uint16_t *fvram_line,
                                         uint32_t *hvram, int pitch,
                                         int scrwidth, int scrheight,
                                         int vw, int vh, int vbpp,
                                         int nextline,
                                         int leftBorder, int rightBorder,
                                         int upperBorder, int lowBorder)
{
	uint32_t *hvram_line = hvram;
	uint32_t nLineEndAddr = nScreenBaseAddr + nextline * 2;
	int h, w;

	/* Render the upper border */
	for (h = 0; h < upperBorder; h++)
	{
		ConvGen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}

	/* Render the graphical area */
	for (h = 0; h < vh; h++)
	{
		uint16_t *fvram_column = fvram_line;
		uint32_t *hvram_column = hvram_line;

		if (nLineEndAddr > STRamEnd)
		{
			ConvGen_memset_uint32(hvram_line, palette.native[0], pitch);
			hvram_line += pitch;
			continue;
		}

		/* Left border first */
		ConvGen_memset_uint32(hvram_column, palette.native[0], leftBorder);
		hvram_column += leftBorder;

		/* Graphical area */
		for (w = 0; w < vw; w++)
		{
			uint16_t srcword = be_swap16(*fvram_column++);
			uint8_t r = ((srcword >> 8) & 0xf8) | (srcword >> 13);
			uint8_t g = ((srcword >> 3) & 0xfc) | ((srcword >> 9) & 0x3);
			uint8_t b = (srcword << 3) | ((srcword >> 2) & 0x07);
			*hvram_column ++ = Screen_MapRGB(r, g, b);
		}

		/* Right border */
		ConvGen_memset_uint32(hvram_column, palette.native[0], rightBorder);

		nLineEndAddr += nextline * 2;
		fvram_line += nextline;
		hvram_line += pitch;
	}

	/* Render the bottom border */
	for (h = 0; h < lowBorder; h++)
	{
		ConvGen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}
}

static void ConvGen_ConvertWithoutZoom(uint16_t *fvram, int vw, int vh, int vbpp, int nextline,
                                      int hscrolloffset, int leftBorder, int rightBorder,
                                      int upperBorder, int lowerBorder)
{
	uint32_t *hvram;
	uint16_t lowBorderSize, rightBorderSize;
	int scrwidth, scrheight;
	int vw_clip, vh_clip;
	int pitch;

	/* Horizontal scroll register set? */
	if (hscrolloffset) {
		/* Yes, so we need to adjust offset to next line: */
		nextline += vbpp;
		ConvertNextLine = nextline * 2;
	}

	/* The sample-hold feature exists only on the TT */
	bTTSampleHold = (TTSpecialVideoMode & 0x80) != 0;

	/* Clip to screen dimension */
	Screen_GetDimension(&hvram, &scrwidth, NULL, &pitch);
	scrheight = Screen_GetGenConvHeight();
	pitch = pitch / sizeof(uint32_t);
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
	hvram += ((scrheight-vh_clip)>>1) * pitch;
	hvram += ((scrwidth-vw_clip)>>1);

	scrwidth = leftBorder + vw + rightBorder;

	/* render the graphic area */
	if (vbpp < 16) {
		/* Bitplanes modes */
		ConvGen_BitplaneTo32bppNoZoom(fvram, hvram, pitch,
		                              scrwidth, scrheight, vw, vh,
		                              vbpp, nextline, hscrolloffset,
		                              leftBorder, rightBorderSize,
		                              upperBorder, lowBorderSize);
	} else {
		/* Falcon TC (High Color) */
		ConvGen_HiColorTo32bppNoZoom(fvram, hvram, pitch,
		                             scrwidth, scrheight, vw, vh,
		                             vbpp, nextline,
		                             leftBorder, rightBorderSize,
		                             upperBorder, lowBorderSize);
	}
}


static void ConvGen_BitplaneTo32bppZoomed(uint16_t *fvram,
                                          uint32_t *hvram, int pitch,
                                          int scrwidth, int scrheight,
                                          int vw, int vh, int vbpp,
                                          int nextline, int hscrolloffset,
                                          int leftBorder, int rightBorder,
                                          int upperBorder, int lowerBorder,
                                          int coefx, int coefy)
{
	/* One complete 16-pixel aligned planar 2 chunky line */
	uint32_t *p2cline = malloc(sizeof(uint32_t) * ((vw+15) & ~15));
	uint32_t *hvram_line = hvram;
	uint32_t *hvram_column = p2cline;
	uint16_t *fvram_line;
	uint32_t nLineEndAddr = nScreenBaseAddr + nextline * 2;
	int cursrcline = -1;
	int scrIdx = 0;
	int w, h;

	/* Render the upper border */
	for (h = 0; h < upperBorder * coefy; h++)
	{
		ConvGen_memset_uint32(hvram_line, palette.native[0], scrwidth);
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
			memcpy(hvram_line, hvram_line - pitch, scrwidth * sizeof(uint32_t));
		}
		else if (nLineEndAddr > STRamEnd)
		{
			ConvGen_memset_uint32(hvram_line, palette.native[0], pitch);
		}
		else
		{
			ConvGen_BitplaneLineTo32bpp(fvram_line, p2cline,
			                            vw, vbpp, hscrolloffset);

			hvram_column = hvram_line;
			/* Display the Left border */
			ConvGen_memset_uint32(hvram_column, palette.native[0], leftBorder * coefx);
			hvram_column += leftBorder * coefx;

			/* Display the Graphical area */
			for (w = 0; w < vw * coefx; w++)
			{
				hvram_column[w] = p2cline[screen_zoom.zoomxtable[w]];
			}
			hvram_column += vw * coefx;

			/* Display the Right border */
			ConvGen_memset_uint32(hvram_column, palette.native[0], rightBorder * coefx);

			nLineEndAddr += nextline * 2;
		}

		hvram_line += pitch;
		cursrcline = screen_zoom.zoomytable[h];
	}

	/* Render the lower border */
	for (h = 0; h < lowerBorder * coefy; h++)
	{
		ConvGen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}

	free(p2cline);
}

static void ConvGen_HiColorTo32bppZoomed(uint16_t *fvram,
                                         uint32_t *hvram, int pitch,
                                         int scrwidth, int scrheight,
                                         int vw, int vh, int vbpp,
                                         int nextline,
                                         int leftBorder, int rightBorder,
                                         int upperBorder, int lowerBorder,
                                         int coefx, int coefy)
{
	uint32_t *hvram_line = hvram;
	uint32_t *hvram_column = hvram_line;
	uint16_t *fvram_line;
	uint32_t nLineEndAddr = nScreenBaseAddr + nextline * 2;
	int cursrcline = -1;
	int scrIdx = 0;
	int w, h;

	/* Render the upper border */
	for (h = 0; h < upperBorder * coefy; h++)
	{
		ConvGen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}

	/* Render the graphical area */
	for (h = 0; h < scrheight; h++)
	{
		uint16_t *fvram_column;

		fvram_line = fvram + (screen_zoom.zoomytable[scrIdx] * nextline);
		scrIdx ++;
		fvram_column = fvram_line;

		/* Recopy the same line ? */
		if (screen_zoom.zoomytable[h] == cursrcline)
		{
			memcpy(hvram_line, hvram_line - pitch, scrwidth * sizeof(uint32_t));
		}
		else if (nLineEndAddr > STRamEnd)
		{
			ConvGen_memset_uint32(hvram_line, palette.native[0], pitch);
		}
		else
		{
			hvram_column = hvram_line;

			/* Display the Left border */
			ConvGen_memset_uint32(hvram_column, palette.native[0], leftBorder * coefx);
			hvram_column += leftBorder * coefx;

			/* Display the Graphical area */
			for (w = 0; w < vw * coefx; w++)
			{
				uint16_t srcword;
				uint8_t r, g, b;
				srcword = be_swap16(fvram_column[screen_zoom.zoomxtable[w]]);
				r = ((srcword >> 8) & 0xf8) | (srcword >> 13);
				g = ((srcword >> 3) & 0xfc) | ((srcword >> 9) & 0x3);
				b = (srcword << 3) | ((srcword >> 2) & 0x07);
				*hvram_column ++ = Screen_MapRGB(r, g, b);
			}

			/* Display the Right border */
			ConvGen_memset_uint32(hvram_column, palette.native[0], rightBorder * coefx);

			nLineEndAddr += nextline * 2;
		}

		hvram_line += pitch;
		cursrcline = screen_zoom.zoomytable[h];
	}

	/* Render the lower border */
	for (h = 0; h < lowerBorder * coefy; h++)
	{
		ConvGen_memset_uint32(hvram_line, palette.native[0], scrwidth);
		hvram_line += pitch;
	}
}

static void ConvGen_ConvertWithZoom(uint16_t *fvram, int vw, int vh, int vbpp, int nextline,
                                    int hscrolloffset, int leftBorder, int rightBorder,
                                    int upperBorder, int lowerBorder)
{
	int coefx = 1;
	int coefy = 1;
	int scrpitch, scrwidth, scrheight;
	uint32_t *hvram;
	int vw_b, vh_b;
	int i;

	/* The sample-hold feature exists only on the TT */
	bTTSampleHold = (TTSpecialVideoMode & 0x80) != 0;

	vw_b = vw + leftBorder + rightBorder;
	vh_b = vh + upperBorder + lowerBorder;

	/* Host screen infos */
	Screen_GetDimension(&hvram, &scrwidth, NULL, &scrpitch);
	scrpitch = scrpitch / sizeof(uint32_t);
	scrheight = Screen_GetGenConvHeight();

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
		hvram += ((Screen_GetGenConvHeight() - scrheight) >> 1) * scrpitch;
		hvram += ((Screen_GetGenConvWidth() - scrwidth) >> 1);
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
		ConvGen_BitplaneTo32bppZoomed(fvram, hvram, scrpitch,
		                              scrwidth, scrheight,
		                              vw, vh, vbpp, nextline, hscrolloffset,
		                              leftBorder, rightBorder, upperBorder,
		                              lowerBorder, coefx, coefy);
	} else {
		/* Falcon high-color (16-bit) mode */
		ConvGen_HiColorTo32bppZoomed(fvram, hvram, scrpitch,
		                             scrwidth, scrheight,
		                             vw, vh, vbpp, nextline,
		                             leftBorder, rightBorder, upperBorder,
		                             lowerBorder, coefx, coefy);
	}
}

void ConvGen_Convert(uint32_t vaddr, void *fvram, int vw, int vh,
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
		ConvGen_ConvertWithZoom(fvram, vw, vh, vbpp, nextline, hscroll,
		                        leftBorderSize, rightBorderSize,
		                        upperBorderSize, lowerBorderSize);
	} else {
		ConvGen_ConvertWithoutZoom(fvram, vw, vh, vbpp, nextline, hscroll,
		                           leftBorderSize, rightBorderSize,
		                           upperBorderSize, lowerBorderSize);
	}
}

bool ConvGen_Draw(uint32_t vaddr, int vw, int vh, int vbpp, int nextline,
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

	ConvGen_Convert(vaddr, &STRam[vaddr], vw, vh, vbpp, nextline, hscrolloffset,
	                leftBorder, rightBorder, upperBorder, lowerBorder);

	Screen_UnLock();
	Screen_GenConvUpdate(true);

	return true;
}


/**
 * Set given width & height arguments to maximum size allowed in the
 * configuration.
 */
void ConvGen_GetLimits(int *width, int *height)
{
	*width = *height = 0;

	if (bInFullScreen)
	{
		/* resolution change not allowed? */
		if (ConfigureParams.Screen.bKeepResolution)
		{
			Screen_GetDesktopSize(width, height);
		}
	}

	if (!(*width && *height) ||
	    ConfigureParams.Screen.bForceMax ||
	    (ConfigureParams.Screen.nMaxWidth < *width &&
	     ConfigureParams.Screen.nMaxHeight < *height))
	{
		*width = ConfigureParams.Screen.nMaxWidth;
		*height = ConfigureParams.Screen.nMaxHeight;
	}
}


/**
 * This is used to set the size of the screen
 * when we're using the generic conversion functions.
 */
void ConvGen_SetSize(int width, int height, bool bForceChange)
{
	static int genconv_width_req, genconv_height_req;
	int screenwidth, screenheight, maxw, maxh;
	int scalex, scaley, sbarheight;

	if (width == -1)
		width = genconv_width_req;
	else
		genconv_width_req = width;

	if (height == -1)
		height = genconv_height_req;
	else
		genconv_height_req = height;

	/* constrain size request to user's desktop size */
	ConvGen_GetLimits(&maxw, &maxh);

	nScreenZoomX = nScreenZoomY = 1;

	if (ConfigureParams.Screen.bAspectCorrect)
	{
		/* Falcon (and TT) pixel scaling factors seem to 2^x
		 * (quarter/half pixel, interlace/double line), so
		 * do aspect correction as 2's exponent. */
		while (nScreenZoomX*width < height &&
		       2 * nScreenZoomX * width < maxw)
		{
			nScreenZoomX *= 2;
		}
		while (2 * nScreenZoomY * height < width &&
		       2 * nScreenZoomY * height < maxh)
		{
			nScreenZoomY *= 2;
		}
		if (nScreenZoomX*nScreenZoomY > 2)
		{
			Log_Printf(LOG_INFO, "Strange screen size %dx%d -> aspect corrected by %dx%d!\n",
				width, height, nScreenZoomX, nScreenZoomY);
		}
	}

	/* then select scale as close to target size as possible
	 * without having larger size than it */
	scalex = maxw/(nScreenZoomX*width);
	scaley = maxh/(nScreenZoomY*height);
	if (scalex > 1 && scaley > 1)
	{
		/* keep aspect ratio */
		if (scalex < scaley)
		{
			nScreenZoomX *= scalex;
			nScreenZoomY *= scalex;
		}
		else
		{
			nScreenZoomX *= scaley;
			nScreenZoomY *= scaley;
		}
	}

	width *= nScreenZoomX;
	height *= nScreenZoomY;

	/* get statusbar size for this screen size */
	sbarheight = Statusbar_GetHeightForSize(width, height);
	screenheight = height + sbarheight;
	screenwidth = width;

	/* re-calculate statusbar height for this resolution */
	sbarheight = Statusbar_SetHeight(screenwidth, screenheight-sbarheight);

	if (!Screen_SetVideoSize(screenwidth, screenheight, bForceChange))
	{
		/* same host screen size despite Atari resolution change,
		 * -> no time consuming host video mode change needed */
		if (screenwidth > width || screenheight > height+sbarheight)
		{
			/* Atari screen smaller than host -> clear screen */
			Screen_ClearScreen();
		}
		return;
	}

	/* In case surface format changed, remap the native palette */
	ConvGen_RemapPalette();

	GuiEvent_WarpMouse(screenwidth / 2, screenheight / 2, false);
}


/**
 * Return true if Falcon/TT/VDI generic screen convert functions
 * need to be used instead of the ST/STE functions.
 */
bool ConvGen_UseGenConvScreen(void)
{
	return Config_IsMachineFalcon() || Config_IsMachineTT()
		|| bUseHighRes || bUseVDIRes;
}
