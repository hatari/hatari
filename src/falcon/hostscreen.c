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


static Uint32 sdl_videoparams;
static Uint32 hs_width, hs_height, hs_bpp;
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

static int HostScreen_selectVideoMode(SDL_Rect **modes, Uint32 *width, Uint32 *height);
static void HostScreen_searchVideoMode( Uint32 *width, Uint32 *height, Uint32 *bpp );


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
	if (sdl_videoparams & SDL_FULLSCREEN) {
		/* un-embed the Hatari WM window for fullscreen */
		Control_ReparentWindow(hs_width, hs_height, true);
	}
	if(SDL_WM_ToggleFullScreen(mainSurface) == 0) {
		// SDL_WM_ToggleFullScreen() did not work.
		// We have to change video mode "by hand".
		SDL_Surface *temp = SDL_ConvertSurface(mainSurface, mainSurface->format,
		                                       mainSurface->flags);
		Dprintf(("toggleFullScreen: SDL_WM_ToggleFullScreen() not supported"
		         " -> using SDL_SetVideoMode()"));
		if (temp == NULL)
			Dprintf(("toggleFullScreen: Unable to save screen content."));

#if 1
		HostScreen_setWindowSize(hs_width, hs_height, hs_bpp);
#else
		mainSurface = SDL_SetVideoMode(width, height, bpp, sdl_videoparams);
		if (mainSurface == NULL)
			Dprintf(("toggleFullScreen: Unable to set new video mode."));
		if (mainSurface->format->BitsPerPixel <= 8)
			SDL_SetColors(mainSurface, temp->format->palette->colors, 0,
			              temp->format->palette->ncolors);
#endif

		if (SDL_BlitSurface(temp, NULL, mainSurface, NULL) != 0)
			Dprintf(("toggleFullScreen: Unable to restore screen content."));
		SDL_FreeSurface(temp);

		/* refresh the screen */
		HostScreen_update1(true);
	} else {
		if (!(sdl_videoparams & SDL_FULLSCREEN)) {
			/* re-embed the new Hatari SDL window */
			Control_ReparentWindow(hs_width, hs_height, false);
		}
	}
}

static int HostScreen_selectVideoMode(SDL_Rect **modes, Uint32 *width, Uint32 *height)
{
	int i, bestw, besth;

	/* Search the smallest nearest mode */
	bestw = modes[0]->w;
	besth = modes[0]->h;
	for (i=0;modes[i]; ++i) {
		if ((modes[i]->w >= *width) && (modes[i]->h >= *height)) {
			if ((modes[i]->w < bestw) || (modes[i]->h < besth)) {
				bestw = modes[i]->w;
				besth = modes[i]->h;
			}			
		}
	}

	*width = bestw;
	*height = besth;
	Dprintf(("hostscreen: video mode found: %dx%d\n",*width,*height));

	return 1;
}

static void HostScreen_searchVideoMode( Uint32 *width, Uint32 *height, Uint32 *bpp )
{
	SDL_Rect **modes;
	SDL_PixelFormat pixelformat;
	int modeflags;

	/* Search in available modes the best suited */
	Dprintf(("hostscreen: video mode asked: %dx%dx%d\n",*width,*height,*bpp));

	if ((*width == 0) || (*height == 0)) {
		*width = 640;
		*height = 480;
	}

	/* Read available video modes */
	modeflags = 0 /*SDL_HWSURFACE | SDL_HWPALETTE*/;
	if (bInFullScreen)
		modeflags |= SDL_FULLSCREEN;

	/*--- Search a video mode with asked bpp ---*/
	if (*bpp != 0) {
		pixelformat.BitsPerPixel = *bpp;
		modes = SDL_ListModes(&pixelformat, modeflags);
		if ((modes != (SDL_Rect **) 0) && (modes != (SDL_Rect **) -1)) {
			Dprintf(("hostscreen: searching a good video mode (any bpp)\n"));
			if (HostScreen_selectVideoMode(modes,width,height)) {
				Dprintf(("hostscreen: video mode selected: %dx%dx%d\n",*width,*height,*bpp));
				return;
			}
		}
	}

	/*--- Search a video mode with any bpp ---*/
	modes = SDL_ListModes(NULL, modeflags);
	if ((modes != (SDL_Rect **) 0) && (modes != (SDL_Rect **) -1)) {
		Dprintf(("hostscreen: searching a good video mode\n"));
		if (HostScreen_selectVideoMode(modes,width,height)) {
			Dprintf(("hostscreen: video mode selected: %dx%dx%d\n",*width,*height,*bpp));
			return;
		}
	}

	if (modes == (SDL_Rect **) 0) {
		Dprintf(("hostscreen: No modes available\n"));
	}

	if (modes == (SDL_Rect **) -1) {
		/* Any mode available */
		Dprintf(("hostscreen: Any modes available\n"));
	}

	Dprintf(("hostscreen: video mode selected: %dx%dx%d\n",*width,*height,*bpp));
}

void HostScreen_setWindowSize( Uint32 width, Uint32 height, Uint32 bpp )
{
	Uint32 screenheight;

	if (bpp == 24)
		bpp = 32;

	nScreenZoomX = 1;
	nScreenZoomY = 1;
	if (ConfigureParams.Screen.bZoomLowRes)
	{
		/* Ugly: 400x300 threshold is currently hard-coded. */
		/* Should rather be selectable by the user! */	
		if (width && width <= 400)
		{
			nScreenZoomX = (800/width);
			width *= nScreenZoomX; 
		}
		if (height && height <= 300)
		{
			nScreenZoomY = (550/height);
			height *= nScreenZoomY;
		}
	}
	screenheight = height + Statusbar_SetHeight(width, height);

	// Select a correct video mode
	HostScreen_searchVideoMode(&width, &screenheight, &bpp);

	hs_width = width;
	hs_height = height;
	hs_bpp = bpp;

	// SelectVideoMode();
	if (bInFullScreen) {
		/* un-embed the Hatari WM window for fullscreen */
		Control_ReparentWindow(width, screenheight, bInFullScreen);

		sdl_videoparams = SDL_SWSURFACE|SDL_HWPALETTE|SDL_FULLSCREEN;
	} else {
		sdl_videoparams = SDL_SWSURFACE|SDL_HWPALETTE;
	}
	mainSurface = SDL_SetVideoMode(width, screenheight, bpp, sdl_videoparams);
	if (!bInFullScreen) {
		/* re-embed the new Hatari SDL window */
		Control_ReparentWindow(width, screenheight, bInFullScreen);
	}
	sdlscrn = surf = mainSurface;

	// update the surface's palette
	HostScreen_updatePalette( 256 );

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
