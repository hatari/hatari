/*
  Hatari - hostscreen.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Host video routines. This file originally came from the Aranym project but
  has been thoroughly reworked for Hatari. However, integration with the rest
  of the Hatari source code is still bad and needs a lot of improvement...
*/
const char HostScreen_fileid[] = "Hatari hostscreen.c : " __DATE__ " " __TIME__;

#include <SDL.h>
#include "main.h"
#include "configuration.h"
#include "control.h"
#include "sysdeps.h"
#include "stMemory.h"
#include "ioMem.h"
#include "hostscreen.h"
#include "resolution.h"
#include "screen.h"
#include "statusbar.h"

#define VIDEL_DEBUG 0

#if VIDEL_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


#define RGB_BLACK     0x00000000
#define RGB_BLUE      0x000000ff
#define RGB_GREEN     0x00ff0000
#define RGB_CYAN      0x00ff00ff
#define RGB_RED       0xff000000
#define RGB_MAGENTA   0xff0000ff
#define RGB_LTGRAY    0xbbbb00bb
#define RGB_GRAY      0x88880088
#define RGB_LTBLUE    0x000000aa
#define RGB_LTGREEN   0x00aa0000
#define RGB_LTCYAN    0x00aa00aa
#define RGB_LTRED     0xaa000000
#define RGB_LTMAGENTA 0xaa0000aa
#define RGB_YELLOW    0xffff0000
#define RGB_LTYELLOW  0xaaaa0000
#define RGB_WHITE     0xffff00ff


/* TODO: put these hostscreen globals to some struct */
static SDL_Rect hs_rect;
static int hs_width_req, hs_height_req, hs_bpp;
static bool   doUpdate; // the HW surface is available -> the SDL need not to update the surface after ->pixel access

static void HostScreen_remapPalette(void);

static struct { // TOS palette (bpp < 16) to SDL color mapping
	SDL_Color	standard[256];
	Uint32		native[256];
} palette;


static const Uint32 default_palette[] = {
    RGB_WHITE, RGB_RED, RGB_GREEN, RGB_YELLOW,
    RGB_BLUE, RGB_MAGENTA, RGB_CYAN, RGB_LTGRAY,
    RGB_GRAY, RGB_LTRED, RGB_LTGREEN, RGB_LTYELLOW,
    RGB_LTBLUE, RGB_LTMAGENTA, RGB_LTCYAN, RGB_BLACK
};


void HostScreen_Init(void)
{
	int i;
	for(i = 0; i < 256; i++) {
		Uint32 color = default_palette[i%16];
		palette.standard[i].r = color >> 24;
		palette.standard[i].g = (color >> 16) & 0xff;
		palette.standard[i].b = color & 0xff;
	}
}

void HostScreen_UnInit(void)
{
}


void HostScreen_toggleFullScreen(void)
{
	HostScreen_setWindowSize(hs_width_req, hs_height_req, hs_bpp, true);
	/* force screen redraw */
	HostScreen_update1(NULL, true);
}


void HostScreen_setWindowSize(int width, int height, int bpp, bool bForceChange)
{
	const bool keep = ConfigureParams.Screen.bKeepResolution;
	int screenwidth, screenheight, maxw, maxh;
	int scalex, scaley, sbarheight;

	if (bpp == 24)
		bpp = 32;

	/* constrain size request to user's desktop size */
	Resolution_GetDesktopSize(&maxw, &maxh);
	scalex = scaley = 1;
	while (width > maxw*scalex) {
		scalex *= 2;
	}
	while (height > maxh*scaley) {
		scaley *= 2;
	}
	if (scalex * scaley > 1) {
		fprintf(stderr, "WARNING: too large screen size %dx%d -> divided by %dx%d!\n",
			width, height, scalex, scaley);
		width /= scalex;
		height /= scaley;
	}

	Resolution_GetLimits(&maxw, &maxh, &bpp, keep);
	nScreenZoomX = nScreenZoomY = 1;
	
	if (ConfigureParams.Screen.bAspectCorrect) {
		/* Falcon (and TT) pixel scaling factors seem to 2^x
		 * (quarter/half pixel, interlace/double line), so
		 * do aspect correction as 2's exponent.
		 */
		while (nScreenZoomX*width < height &&
		       2*nScreenZoomX*width < maxw) {
			nScreenZoomX *= 2;
		}
		while (2*nScreenZoomY*height < width &&
		       2*nScreenZoomY*height < maxh) {
			nScreenZoomY *= 2;
		}
		if (nScreenZoomX*nScreenZoomY > 2) {
			fprintf(stderr, "WARNING: strange screen size %dx%d -> aspect corrected by %dx%d!\n",
				width, height, nScreenZoomX, nScreenZoomY);
		}
	}

	/* then select scale as close to target size as possible
	 * without having larger size than it
	 */
	scalex = maxw/(nScreenZoomX*width);
	scaley = maxh/(nScreenZoomY*height);
	if (scalex > 1 && scaley > 1) {
		/* keep aspect ratio */
		if (scalex < scaley) {
			nScreenZoomX *= scalex;
			nScreenZoomY *= scalex;
		} else {
			nScreenZoomX *= scaley;
			nScreenZoomY *= scaley;
		}
	}

	hs_width_req = width;
	hs_height_req = height;
	width *= nScreenZoomX;
	height *= nScreenZoomY;

	/* get statusbar size for this screen size */
	sbarheight = Statusbar_GetHeightForSize(width, height);
	screenheight = height + sbarheight;
	screenwidth = width;

	/* get resolution corresponding to these */
	Resolution_Search(&screenwidth, &screenheight, &bpp, keep);
	/* re-calculate statusbar height for this resolution */
	sbarheight = Statusbar_SetHeight(screenwidth, screenheight-sbarheight);

	hs_bpp = bpp;
	/* videl.c might scale things differently in fullscreen than
	 * in windowed mode because this uses screensize instead of using
	 * the aspect scaled sizes directly, but it works better this way.
	 */
	hs_rect.x = 0;
	hs_rect.y = 0;
	hs_rect.w = screenwidth;
	hs_rect.h = screenheight - sbarheight;

	if (!Screen_SetSDLVideoSize(screenwidth, screenheight, bpp, bForceChange))
	{
		/* same host screen size despite Atari resolution change,
		 * -> no time consuming host video mode change needed
		 */
		if (screenwidth > width || screenheight > height+sbarheight) {
			/* Atari screen smaller than host -> clear screen */
			SDL_Rect rect;
			rect.x = 0;
			rect.y = 0;
			rect.w = sdlscrn->w;
			rect.h = sdlscrn->h - sbarheight;
			SDL_FillRect(sdlscrn, &rect, SDL_MapRGB(sdlscrn->format, 0, 0, 0));
			/* re-calculate variables in case height + statusbar height
			 * don't anymore match SDL surface size (there's an assert
			 * for that)
			 */
			Statusbar_Init(sdlscrn);
		}
#if WITH_SDL2
		doUpdate = true;
#else
		// check in case switched from VDI to Hostscreen
		doUpdate = ( sdlscrn->flags & SDL_HWSURFACE ) == 0;
#endif
		return;
	}

	// In case surface format changed, remap the native palette
	HostScreen_remapPalette();

	// redraw statusbar
	Statusbar_Init(sdlscrn);

	Dprintf(("Surface Pitch = %d, width = %d, height = %d\n", sdlscrn->pitch, sdlscrn->w, sdlscrn->h));
	Dprintf(("Must Lock? %s\n", SDL_MUSTLOCK(sdlscrn) ? "YES" : "NO"));

#if WITH_SDL2
	doUpdate = true;
#else
	// is the SDL_update needed?
	doUpdate = ( sdlscrn->flags & SDL_HWSURFACE ) == 0;
#endif

	Dprintf(("Pixel format:bitspp=%d, tmasks r=%04x g=%04x b=%04x"
			", tshifts r=%d g=%d b=%d"
			", tlosses r=%d g=%d b=%d\n",
			sdlscrn->format->BitsPerPixel,
			sdlscrn->format->Rmask, sdlscrn->format->Gmask, sdlscrn->format->Bmask,
			sdlscrn->format->Rshift, sdlscrn->format->Gshift, sdlscrn->format->Bshift,
			sdlscrn->format->Rloss, sdlscrn->format->Gloss, sdlscrn->format->Bloss));

	Main_WarpMouse(sdlscrn->w/2,sdlscrn->h/2, false);
}


void HostScreen_update1(SDL_Rect *extra, bool forced)
{
	SDL_Rect rects[2];
	int count = 1;

	if ( !forced && !doUpdate ) // the HW surface is available
		return;

	rects[0] = hs_rect;
	if (extra) {
		rects[1] = *extra;
		count = 2;
	}
	SDL_UpdateRects(sdlscrn, count, rects);
}


Uint32 HostScreen_getBpp(void)
{
	return sdlscrn->format->BytesPerPixel;
}

Uint32 HostScreen_getPitch(void)
{
	return sdlscrn->pitch;
}

Uint32 HostScreen_getWidth(void)
{
	return hs_rect.w;
}

Uint32 HostScreen_getHeight(void)
{
	return hs_rect.h;
}

Uint8 *HostScreen_getVideoramAddress(void)
{
	return sdlscrn->pixels;
}

SDL_PixelFormat *HostScreen_getFormat(void)
{
	return sdlscrn->format;
}

void HostScreen_setPaletteColor(Uint8 idx, Uint8 red, Uint8 green, Uint8 blue)
{
	// set the SDL standard RGB palette settings
	palette.standard[idx].r = red;
	palette.standard[idx].g = green;
	palette.standard[idx].b = blue;
	// convert the color to native
	palette.native[idx] = SDL_MapRGB( sdlscrn->format, red, green, blue );
}

Uint32 HostScreen_getPaletteColor(Uint8 idx)
{
	return palette.native[idx];
}

static void HostScreen_remapPalette(void)
{
	int i;
	Uint32 *native = palette.native;
	SDL_Color *standard = palette.standard;
	SDL_PixelFormat *fmt = sdlscrn->format;

	for(i = 0; i < 256; i++, native++, standard++) {
		*native = SDL_MapRGB(fmt, standard->r, standard->g, standard->b);
	}
}

bool HostScreen_renderBegin(void)
{
	if (SDL_MUSTLOCK(sdlscrn))
		if (SDL_LockSurface(sdlscrn) < 0) {
			printf("Couldn't lock surface to refresh!\n");
			return false;
		}

	return true;
}

/**
 * Direct surface writes done, so unlock screen,
 * check for statusbar updates and if there were such,
 * return which area needs update.
 */
SDL_Rect* HostScreen_renderEnd(void)
{
	if (SDL_MUSTLOCK(sdlscrn))
		SDL_UnlockSurface(sdlscrn);
	return Statusbar_Update(sdlscrn, false);
}
