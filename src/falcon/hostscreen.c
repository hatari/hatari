/*
  Hatari - hostscreen.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Host video routines. This file originally came from the Aranym project but
  has been thoroughly reworked for Hatari. However, integration with the rest
  of the Hatari source code is still bad and needs a lot of improvement...
*/
const char HostScreen_fileid[] = "Hatari hostscreen.c : " __DATE__ " " __TIME__;

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


static SDL_Surface *mainSurface;        // The main window surface
static SDL_Surface *surf;               // pointer to actual surface

/* TODO: put these hostscreen globals to some struct */
static Uint32 sdl_videoparams;
static int hs_width, hs_height, hs_width_req, hs_height_req, hs_bpp;
static bool   doUpdate; // the HW surface is available -> the SDL need not to update the surface after ->pixel access

static struct { // TOS palette (bpp < 16) to SDL color mapping
	SDL_Color	standard[256];
	Uint32		native[256];
} palette;


static const unsigned long default_palette[] = {
    RGB_WHITE, RGB_RED, RGB_GREEN, RGB_YELLOW,
    RGB_BLUE, RGB_MAGENTA, RGB_CYAN, RGB_LTGRAY,
    RGB_GRAY, RGB_LTRED, RGB_LTGREEN, RGB_LTYELLOW,
    RGB_LTBLUE, RGB_LTMAGENTA, RGB_LTCYAN, RGB_BLACK
};


void HostScreen_Init(void)
{
	int i;
	for(i=0; i<256; i++) {
		unsigned long color = default_palette[i%16];
		palette.standard[i].r = color >> 24;
		palette.standard[i].g = (color >> 16) & 0xff;
		palette.standard[i].b = color & 0xff;
	}

	mainSurface=NULL;
}

void HostScreen_UnInit(void)
{
}


void HostScreen_toggleFullScreen(void)
{
	sdl_videoparams ^= SDL_FULLSCREEN;

	HostScreen_setWindowSize(hs_width_req, hs_height_req, hs_bpp);
	/* refresh the screen */
	HostScreen_update1(true);
}


void HostScreen_setWindowSize(int width, int height, int bpp)
{
	int screenwidth, screenheight, maxw, maxh;
	int scalex, scaley, sbarheight;

	if (bpp == 24)
		bpp = 32;

	Resolution_GetLimits(&maxw, &maxh, &bpp);

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

	sbarheight = Statusbar_GetHeightForSize(width, height);
	screenheight = height + sbarheight;
	screenwidth = width;

	// Select a correct video mode
	Resolution_Search(&screenwidth, &screenheight, &bpp);
	sbarheight = Statusbar_SetHeight(screenwidth, screenheight-sbarheight);

	hs_bpp = bpp;
	/* videl.c might scale things differently in fullscreen than
	 * in windowed mode because this uses screensize instead of using
	 * the aspect scaled sizes directly, but it works better this way.
	 */
	hs_width = screenwidth;
	hs_height = screenheight - sbarheight;

	if (sdlscrn && (!bpp || sdlscrn->format->BitsPerPixel == bpp) &&
	    sdlscrn->w == (signed)screenwidth && sdlscrn->h == (signed)screenheight &&
	    (sdlscrn->flags&SDL_FULLSCREEN) == (sdl_videoparams&SDL_FULLSCREEN))
	{
		/* no time consuming host video mode change needed */
		if (screenwidth > width || screenheight > height+sbarheight) {
			/* Atari screen smaller than host -> clear screen */
			SDL_Rect rect;
			rect.x = 0;
			rect.y = 0;
			rect.w = sdlscrn->w;
			rect.h = sdlscrn->h - sbarheight;
			SDL_FillRect(sdlscrn, &rect, SDL_MapRGB(sdlscrn->format, 0, 0, 0));
		}
		return;
	}

	if (bInFullScreen) {
		/* un-embed the Hatari WM window for fullscreen */
		Control_ReparentWindow(screenwidth, screenheight, bInFullScreen);

		sdl_videoparams = SDL_SWSURFACE|SDL_HWPALETTE|SDL_FULLSCREEN;
	} else {
		sdl_videoparams = SDL_SWSURFACE|SDL_HWPALETTE;
	}
	mainSurface = SDL_SetVideoMode(screenwidth, screenheight, bpp, sdl_videoparams);
	if (!bInFullScreen) {
		/* re-embed the new Hatari SDL window */
		Control_ReparentWindow(screenwidth, screenheight, bInFullScreen);
	}
	sdlscrn = surf = mainSurface;

	// update the surface's palette
	HostScreen_updatePalette( 256 );

	// redraw statusbar
	Statusbar_Init(mainSurface);

	Dprintf(("Surface Pitch = %d, width = %d, height = %d\n", surf->pitch, surf->w, surf->h));
	Dprintf(("Must Lock? %s\n", SDL_MUSTLOCK(surf) ? "YES" : "NO"));

	// is the SDL_update needed?
	doUpdate = ( surf->flags & SDL_HWSURFACE ) == 0;

	HostScreen_renderBegin();

//	VideoRAMBaseHost = (uint8 *) surf->pixels;
//	InitVMEMBaseDiff(VideoRAMBaseHost, VideoRAMBase);
//	Dprintf(("VideoRAM starts at %p (%08x)\n", VideoRAMBaseHost, VideoRAMBase));
	Dprintf(("surf->pixels = %p, getVideoSurface() = %p\n",
			surf->pixels, SDL_GetVideoSurface()->pixels));

	HostScreen_renderEnd();

	Dprintf(("Pixel format:bitspp=%d, tmasks r=%04x g=%04x b=%04x"
			", tshifts r=%d g=%d b=%d"
			", tlosses r=%d g=%d b=%d\n",
			surf->format->BitsPerPixel,
			surf->format->Rmask, surf->format->Gmask, surf->format->Bmask,
			surf->format->Rshift, surf->format->Gshift, surf->format->Bshift,
			surf->format->Rloss, surf->format->Gloss, surf->format->Bloss));

	Main_WarpMouse(sdlscrn->w/2,sdlscrn->h/2);
}


static void HostScreen_update5(Sint32 x, Sint32 y, Sint32 w, Sint32 h, bool forced)
{
	if ( !forced && !doUpdate ) // the HW surface is available
		return;

	SDL_UpdateRect(mainSurface, x, y, w, h);
}

void HostScreen_update1(bool forced)
{
	HostScreen_update5( 0, 0, hs_width, hs_height, forced );
}


Uint32 HostScreen_getBpp(void)
{
	return surf->format->BytesPerPixel;
}

Uint32 HostScreen_getPitch(void)
{
	return surf->pitch;
}

Uint32 HostScreen_getWidth(void)
{
	return hs_width;
}

Uint32 HostScreen_getHeight(void)
{
	return hs_height;
}

Uint8 *HostScreen_getVideoramAddress(void)
{
	return surf->pixels;	/* FIXME maybe this should be mainSurface? */
}

void HostScreen_setPaletteColor(Uint8 idx, Uint32 red, Uint32 green, Uint32 blue)
{
	// set the SDL standard RGB palette settings
	palette.standard[idx].r = red;
	palette.standard[idx].g = green;
	palette.standard[idx].b = blue;
	// convert the color to native
	palette.native[idx] = SDL_MapRGB( surf->format, red, green, blue );
}

Uint32 HostScreen_getPaletteColor(Uint8 idx)
{
	return palette.native[idx];
}

void HostScreen_updatePalette(Uint16 colorCount)
{
	SDL_SetColors( surf, palette.standard, 0, colorCount );
}

Uint32 HostScreen_getColor(Uint32 red, Uint32 green, Uint32 blue)
{
	return SDL_MapRGB( surf->format, red, green, blue );
}


bool HostScreen_renderBegin(void)
{
	if (SDL_MUSTLOCK(surf))
		if (SDL_LockSurface(surf) < 0) {
			printf("Couldn't lock surface to refresh!\n");
			return false;
		}

	return true;
}

void HostScreen_renderEnd(void)
{
	if (SDL_MUSTLOCK(surf))
		SDL_UnlockSurface(surf);
	Statusbar_Update(surf);
}
