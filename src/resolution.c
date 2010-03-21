/*
  Hatari - resolution.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  SDL resolution limitation and selection routines.
*/
const char Resolution_fileid[] = "Hatari resolution.c : " __DATE__ " " __TIME__;

#include <SDL.h>
#include "main.h"
#include "configuration.h"
#include "resolution.h"
#include "screen.h"

#define RESOLUTION_DEBUG 0

#if RESOLUTION_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


/**
 * Select best resolution from given SDL video modes.
 * - If width and height are given, select the smallest mode larger
 *   or equal to requested size
 * - Otherwise select the largest available mode
 * return true for success and false if no matching mode was found.
 */
static bool Resolution_Select(SDL_Rect **modes, int *width, int *height)
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
		Dprintf(("hostscreen: largest found video mode: %dx%d\n",*width,*height));
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
	Dprintf(("hostscreen: video mode found: %dx%d\n",*width,*height));
	return true;
#undef TOO_LARGE
}


/**
 * Search video mode size that best suits the given width/height/bpp
 * constraints and set them into given arguments.  With zeroed arguments,
 * return largest video mode.
 */
void Resolution_Search(int *width, int *height, int *bpp)
{
	SDL_Rect **modes;
	SDL_PixelFormat pixelformat;
	Uint32 modeflags;

	/* Search in available modes the best suited */
	Dprintf(("hostscreen: video mode asked: %dx%dx%d\n",
		 *width, *height, *bpp));

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
			if (Resolution_Select(modes, width, height)) {
				Dprintf(("hostscreen: video mode selected: %dx%dx%d\n",
					 *width, *height, *bpp));
				return;
			}
		}
	}

	/*--- Search a video mode with any bpp ---*/
	modes = SDL_ListModes(NULL, modeflags);
	if ((modes != (SDL_Rect **) 0) && (modes != (SDL_Rect **) -1)) {
		Dprintf(("hostscreen: searching a good video mode\n"));
		if (Resolution_Select(modes, width, height)) {
			Dprintf(("hostscreen: video mode selected: %dx%dx%d\n",
				 *width, *height, *bpp));
			return;
		}
	}

	if (modes == (SDL_Rect **) 0) {
		fprintf(stderr, "WARNING: No suitable video modes available!\n");
	}

	if (modes == (SDL_Rect **) -1) {
		/* Any mode available */
		Dprintf(("hostscreen: All resolutions available.\n"));
	}

	Dprintf(("hostscreen: video mode selected: %dx%dx%d\n",
		 *width, *height, *bpp));
}


/**
 * Set given width & height arguments to maximum size in the configuration,
 * or if that's too large for the requested bit depth, to the largest
 * available video mode size.
 */
void Resolution_GetLimits(int *width, int *height, int *bpp)
{
	*width = *height = 0;
	/* constrain max size to what HW/SDL offers */
	Resolution_Search(width, height, bpp);
	if (!(*width && *width) ||
	    (ConfigureParams.Screen.nMaxWidth < *width &&
	     ConfigureParams.Screen.nMaxHeight < *height)) {
		/* already within it, use user provided value */
		*width = ConfigureParams.Screen.nMaxWidth;
		*height = ConfigureParams.Screen.nMaxHeight;
	}
}
