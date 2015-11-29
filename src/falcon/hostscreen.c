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
#include "screenConvert.h"
#include "statusbar.h"

#define VIDEL_DEBUG 0

#if VIDEL_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


/* TODO: put these hostscreen globals to some struct */
static SDL_Rect hs_rect;
static int hs_width_req, hs_height_req, hs_bpp;
static bool   doUpdate; // the HW surface is available -> the SDL need not to update the surface after ->pixel access


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

#if !WITH_SDL2
	/* get resolution corresponding to these */
	Resolution_Search(&screenwidth, &screenheight, &bpp, keep);
#endif
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
	Screen_RemapPalette();

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

Uint32 HostScreen_getWidth(void)
{
	return hs_rect.w;
}

Uint32 HostScreen_getHeight(void)
{
	return hs_rect.h;
}
