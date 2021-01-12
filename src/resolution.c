/*
  Hatari - resolution.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  SDL resolution limitation and selection routines.
*/
const char Resolution_fileid[] = "Hatari resolution.c";

#include <SDL.h>
#include "main.h"
#include "configuration.h"
#include "log.h"
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
 * Initializes resolution settings (gets current desktop
 * resolution, sets max Falcon/TT Videl zooming resolution).
 */
void Resolution_Init(void)
{
	SDL_DisplayMode dm;
	if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
	{
		DesktopWidth = dm.w;
		DesktopHeight = dm.h;
	}
	else
	{
		Log_Printf(LOG_ERROR, "SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
		DesktopWidth = 2*NUM_VISIBLE_LINE_PIXELS;
		DesktopHeight = 2*NUM_VISIBLE_LINES+STATUSBAR_MAX_HEIGHT;
	}

	/* if user hasn't set own max zoom size, use desktop size */
	if (!(ConfigureParams.Screen.nMaxWidth &&
	      ConfigureParams.Screen.nMaxHeight)) {
		ConfigureParams.Screen.nMaxWidth = DesktopWidth;
		ConfigureParams.Screen.nMaxHeight = DesktopHeight;
	}
	DEBUGPRINT(("Desktop resolution: %dx%d\n",DesktopWidth, DesktopHeight));
	Log_Printf(LOG_DEBUG, "Configured max Hatari resolution = %dx%d, optimal for ST = %dx%d\n",
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
 * Search video mode size that best suits the given width/height/bpp
 * constraints and set them into given arguments.  With zeroed arguments,
 * set largest video mode.
 * 
 * Return true if mode is forced (shouldn't be further limited).
 */
bool Resolution_Search(int *width, int *height, int *bpp, bool keep)
{
	/* Search in available modes the best suited */
	DEBUGPRINT(("resolution: video mode asked: %dx%dx%d (%s)\n",
		 *width, *height, *bpp, bInFullScreen ? "fullscreen" : "windowed"));

	if (bInFullScreen)
	{
		/* resolution change not allowed? */
		if (keep)
		{
			Resolution_GetDesktopSize(width, height);
			return false;
		}
	}
	if (ConfigureParams.Screen.bForceMax)
	{
		/* force given max size */
		Resolution_GetMaxSize(width, height);
		return true;
	}

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
