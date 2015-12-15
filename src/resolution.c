/*
  Hatari - resolution.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  SDL resolution limitation and selection routines.
*/
const char Resolution_fileid[] = "Hatari resolution.c : " __DATE__ " " __TIME__;

#include <SDL.h>
#include "main.h"
#include "configuration.h"
#include "resolution.h"
#include "statusbar.h"
#include "screen.h"

#define DEBUG 0

#if DEBUG
# define DEBUGPRINT(x) printf x
#else
# define DEBUGPRINT(x)
#endif

static int DesktopWidth, DesktopHeight;

/**
 * Initilizes resolution settings (gets current desktop
 * resolution, sets max Falcon/TT Videl zooming resolution).
 */
void Resolution_Init(void)
{
#if WITH_SDL2
	SDL_DisplayMode dm;
	if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
	{
		DesktopWidth = dm.w;
		DesktopHeight = dm.h;
	}
	else
	{
		fprintf(stderr, "SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
		DesktopWidth = 2*NUM_VISIBLE_LINE_PIXELS;
		DesktopHeight = 2*NUM_VISIBLE_LINES+STATUSBAR_MAX_HEIGHT;
	}
#else
	/* Needs to be called after SDL video and configuration
	 * initialization, but before Hatari Screen init is called
	 * for the first time!
	 */
	const SDL_VideoInfo* info = SDL_GetVideoInfo();
	if (info->current_w >= 640 && info->current_h >= 400) {
		DesktopWidth = info->current_w;
		DesktopHeight = info->current_h;
	} else {
		/* target 800x600 screen with statusbar out of screen */
		DesktopWidth = 2*NUM_VISIBLE_LINE_PIXELS;
		DesktopHeight = 2*NUM_VISIBLE_LINES+STATUSBAR_MAX_HEIGHT;
		fprintf(stderr, "WARNING: invalid desktop size %dx%d, defaulting to %dx%d!\n",
			info->current_w, info->current_h, DesktopWidth, DesktopHeight);
	}
#endif
	/* if user hasn't set own max zoom size, use desktop size */
	if (!(ConfigureParams.Screen.nMaxWidth &&
	      ConfigureParams.Screen.nMaxHeight)) {
		ConfigureParams.Screen.nMaxWidth = DesktopWidth;
		ConfigureParams.Screen.nMaxHeight = DesktopHeight;
	}
	DEBUGPRINT(("Desktop resolution: %dx%d\n",DesktopWidth, DesktopHeight));
	fprintf(stderr, "Configured max Hatari resolution = %dx%d, optimal for ST = %dx%d\n",
		ConfigureParams.Screen.nMaxWidth, ConfigureParams.Screen.nMaxHeight,
		2*NUM_VISIBLE_LINE_PIXELS, 2*NUM_VISIBLE_LINES+STATUSBAR_MAX_HEIGHT);
}

/**
 * Get current desktop resolution
 */
void Resolution_GetDesktopSize(int *width, int *height)
{
	DEBUGPRINT(("resolution: limit to desktop size\n"));
	*width = DesktopWidth;
	*height = DesktopHeight;
}

/**
 * Get max resolution
 */
static void Resolution_GetMaxSize(int *width, int *height)
{
	DEBUGPRINT(("resolution: force to specified max size\n"));
	*width = ConfigureParams.Screen.nMaxWidth;
	*height = ConfigureParams.Screen.nMaxHeight;
}

/**
 * Select best resolution from given SDL video modes.
 * - If width and height are given, select the smallest mode larger
 *   or equal to requested size
 * - Otherwise select the largest available mode
 * return true for success and false if no matching mode was found.
 */
static inline bool Resolution_Select(SDL_Rect **modes, int *width, int *height)
{
#define TOO_LARGE 0x7fff
	int i, bestw, besth;

	if (!(*width && *height)) {
		/* search the largest mode (prefer wider ones) */
		for (i = 0; modes[i]; i++) {
			if ((modes[i]->w > *width) && (modes[i]->h >= *height)) {
				*width = modes[i]->w;
				*height = modes[i]->h;
			}
		}
		DEBUGPRINT(("resolution: largest found video mode: %dx%d\n",*width,*height));
		return true;
	}

	/* Search the smallest mode larger or equal to requested size */
	bestw = TOO_LARGE;
	besth = TOO_LARGE;
	for (i = 0; modes[i]; i++) {
		if ((modes[i]->w >= *width) && (modes[i]->h >= *height)) {
			if ((modes[i]->w < bestw) || (modes[i]->h < besth)) {
				bestw = modes[i]->w;
				besth = modes[i]->h;
			}
		}
	}
	if (bestw == TOO_LARGE || besth == TOO_LARGE) {
		return false;
	}
	*width = bestw;
	*height = besth;
	DEBUGPRINT(("resolution: video mode found: %dx%d\n",*width,*height));
	return true;
#undef TOO_LARGE
}


/**
 * Search video mode size that best suits the given width/height/bpp
 * constraints and set them into given arguments.  With zeroed arguments,
 * set largest video mode.
 * 
 * Return true if mode is forced (shouldn't be further limited).
 */
bool Resolution_Search(int *width, int *height, int *bpp, bool keep)
{
#if !WITH_SDL2
	SDL_Rect **modes;
	SDL_PixelFormat pixelformat;
	Uint32 modeflags = 0 /*SDL_HWSURFACE | SDL_HWPALETTE*/;
#endif

	/* Search in available modes the best suited */
	DEBUGPRINT(("resolution: video mode asked: %dx%dx%d (%s)\n",
		 *width, *height, *bpp, bInFullScreen ? "fullscreen" : "windowed"));

	if (bInFullScreen)
	{
		/* resolution change not allowed? */
		if (keep)
		{
			Resolution_GetDesktopSize(width, height);
#if WITH_SDL2
			return false;
#else
			return true;
#endif
		}
	}
	if (ConfigureParams.Screen.bForceMax)
	{
		/* force given max size */
		Resolution_GetMaxSize(width, height);
		return true;
	}

#if !WITH_SDL2
	if (bInFullScreen)
		modeflags |= SDL_FULLSCREEN;

	/*--- Search a video mode with asked bpp ---*/
	if (*bpp != 0) {
		pixelformat.BitsPerPixel = *bpp;
		modes = SDL_ListModes(&pixelformat, modeflags);
		if ((modes != (SDL_Rect **) 0) && (modes != (SDL_Rect **) -1)) {
			DEBUGPRINT(("resolution: searching a good video mode (given bpp)\n"));
			if (Resolution_Select(modes, width, height)) {
				DEBUGPRINT(("resolution: video mode selected: %dx%dx%d\n",
					 *width, *height, *bpp));
				return false;
			}
		}
	}

	/*--- Search a video mode with any bpp ---*/
	modes = SDL_ListModes(NULL, modeflags);
	if ((modes != (SDL_Rect **) 0) && (modes != (SDL_Rect **) -1)) {
		DEBUGPRINT(("resolution: searching a good video mode (any bpp)\n"));
		if (Resolution_Select(modes, width, height)) {
			DEBUGPRINT(("resolution: video mode selected: %dx%dx%d\n",
				 *width, *height, *bpp));
			return false;
		}
	}

	if (modes == (SDL_Rect **) 0) {
		fprintf(stderr, "WARNING: No suitable video modes available!\n");
	}

	if (modes == (SDL_Rect **) -1) {
		/* Any mode available */
		DEBUGPRINT(("resolution: All resolutions available.\n"));
	}
#endif

	DEBUGPRINT(("resolution: video mode selected: %dx%dx%d\n",
		 *width, *height, *bpp));
	return false;
}


/**
 * Set given width & height arguments to maximum size allowed in the
 * configuration, or if that's too large for the requested bit depth,
 * to the largest available video mode size.
 */
void Resolution_GetLimits(int *width, int *height, int *bpp, bool keep)
{
	*width = *height = 0;

	/* constrain max size to what HW/SDL offers */
	DEBUGPRINT(("resolution: request limits for: %dx%dx%d\n", *width, *height, *bpp));

	/* forced resolution? */
	if (Resolution_Search(width, height, bpp, keep)) {
		return;
	}

	if (!(*width && *height) ||
	    (ConfigureParams.Screen.nMaxWidth < *width &&
	     ConfigureParams.Screen.nMaxHeight < *height)) {
		DEBUGPRINT(("resolution: limit to user configured max\n"));
		*width = ConfigureParams.Screen.nMaxWidth;
		*height = ConfigureParams.Screen.nMaxHeight;
	}
}
