/*
  Hatari - screen.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This code handles the SDL-related screen functions.
*/

const char Screen_fileid[] = "Hatari screen.c";

#include <assert.h>

#include "main.h"

#if ENABLE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#define SDL_MapSurfaceRGB(s, r, g, b) SDL_MapRGB(s->format, r, g, b)
#define SDL_SetWindowRelativeMouseMode(w, b) SDL_SetRelativeMouseMode(b)
#endif

#include "configuration.h"
#include "control.h"
#include "conv_gen.h"
#include "conv_st.h"
#include "avi_record.h"
#include "file.h"
#include "log.h"
#include "paths.h"
#include "options.h"
#include "screen.h"
#include "screen_sdl.h"
#include "sdlgui.h"
#include "spec512.h"
#include "statusbar.h"
#include "statusbar_sdl.h"
#include "vdi.h"
#include "version.h"
#include "video.h"
#include "videl.h"

#define DEBUG 0

#if DEBUG
# define DEBUGPRINT(x) printf x
#else
# define DEBUGPRINT(x)
#endif

/* extern for several purposes */
SDL_Surface *sdlscrn = NULL;                /* The SDL screen surface */

/* extern for shortcuts etc. */
bool bGrabMouse = false;      /* Grab the mouse cursor in the window */
bool bInFullScreen = false;   /* true if in full screen */

static SDL_Rect STScreenRect;       /* screen size without statusbar */

SDL_Window *sdlWindow;
static SDL_Renderer *sdlRenderer;
static SDL_Texture *sdlTexture;
static bool bUseSdlRenderer;            /* true when using SDL2 renderer */
static bool bIsSoftwareRenderer;
static int desktop_width, desktop_height;


void Screen_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects)
{
	if (bUseSdlRenderer)
	{
		SDL_UpdateTexture(sdlTexture, NULL, screen->pixels, screen->pitch);
		/* Need to clear the renderer context for certain accelerated cards */
		if (!bIsSoftwareRenderer)
			SDL_RenderClear(sdlRenderer);
		SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
		SDL_RenderPresent(sdlRenderer);
	}
	else
	{
		SDL_UpdateWindowSurfaceRects(sdlWindow, rects, numrects);
	}
}

void Screen_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h)
{
	SDL_Rect rect;

	if (w == 0 && h == 0) {
		x = y = 0;
		w = screen->w;
		h = screen->h;
	}

	rect.x = x; rect.y = y;
	rect.w = w; rect.h = h;
	Screen_UpdateRects(screen, 1, &rect);
}


uint32_t Screen_MapRGB(uint8_t red, uint8_t green, uint8_t blue)
{
	return SDL_MapSurfaceRGB(sdlscrn, red, green, blue);
}


static void Screen_FreeSDL2Resources(void)
{
	if (sdlTexture)
	{
		SDL_DestroyTexture(sdlTexture);
		sdlTexture = NULL;
	}
	if (sdlscrn)
	{
		if (bUseSdlRenderer)
			SDL_FreeSurface(sdlscrn);
		sdlscrn = NULL;
	}
	if (sdlRenderer)
	{
		SDL_DestroyRenderer(sdlRenderer);
		sdlRenderer = NULL;
	}
}


/**
 * Get pixel format information (mask and shift values)
 */
void Screen_GetPixelFormat(uint32_t *rmask, uint32_t *gmask, uint32_t *bmask,
                           int *rshift, int *gshift, int *bshift)
{
#if ENABLE_SDL3
	if (rmask)
		*rmask = 0x00FF0000;
	if (gmask)
		*gmask = 0x0000FF00;
	if (bmask)
		*bmask = 0x000000FF;

	if (rshift)
		*rshift = 16;
	if (gshift)
		*gshift = 8;
	if (bshift)
		*bshift = 0;
#else
	if (rmask)
		*rmask = sdlscrn->format->Rmask;
	if (gmask)
		*gmask = sdlscrn->format->Gmask;
	if (bmask)
		*bmask = sdlscrn->format->Bmask;

	if (rshift)
		*rshift = sdlscrn->format->Rshift;
	if (gshift)
		*gshift = sdlscrn->format->Gshift;
	if (bshift)
		*bshift = sdlscrn->format->Bshift;
#endif
}


/**
 * Get the dimension and start address of the SDL screen.
 */
void Screen_GetDimension(uint32_t **pixels, int *width, int *height, int *pitch)
{
	if (pixels)
		*pixels = sdlscrn ? sdlscrn->pixels : NULL;
	if (width)
		*width = sdlscrn ? sdlscrn->w : 0;
	if (height)
		*height = sdlscrn ? sdlscrn->h : 0;
	if (pitch)
		*pitch = sdlscrn ? sdlscrn->pitch : 0;
}


/*-----------------------------------------------------------------------
 * Window reparenting - Currently works only on X11.
 *
 * SDL_syswm.h automatically includes everything else needed.
 */

/* X11 available and SDL_config.h states that SDL supports X11 */
#if HAVE_X11 && SDL_VIDEO_DRIVER_X11
#include <SDL_syswm.h>

/**
 * Reparent Hatari window if so requested.  Needs to be done inside
 * Hatari because if SDL itself is requested to reparent itself,
 * SDL window stops accepting any input (specifically done like
 * this in SDL backends for some reason).
 *
 * 'noembed' argument tells whether the SDL window should be embedded
 * or not.
 *
 * If the window is embedded (which means that SDL WM window needs
 * to be hidden) when SDL is asked to fullscreen, Hatari window just
 * disappears when returning back from fullscreen.  I.e. call this
 * with noembed=true _before_ fullscreening and any other time with
 * noembed=false after changing window size.  You can do this by
 * giving bInFullscreen as the noembed value.
 */
static void Screen_ReparentWindow(int width, int height, bool noembed)
{
	Display *display;
	Window parent_win, sdl_win;
	const char *parent_win_id;
	SDL_SysWMinfo info;
	Window wm_win;
	Window dw1, *dw2;
	unsigned int nwin;

	parent_win_id = getenv("PARENT_WIN_ID");
	if (!parent_win_id) {
		return;
	}
	parent_win = strtol(parent_win_id, NULL, 0);
	if (!parent_win) {
		Log_Printf(LOG_WARN, "Invalid PARENT_WIN_ID value '%s'\n", parent_win_id);
		return;
	}

	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(sdlWindow, &info)) {
		Log_Printf(LOG_WARN, "Failed to get SDL_GetWMInfo()\n");
		return;
	}

	display = info.info.x11.display;
	sdl_win = info.info.x11.window;
	XQueryTree(display, sdl_win, &dw1, &wm_win, &dw2, &nwin);

	if (noembed)
	{
		/* show WM window again */
		XMapWindow(display, wm_win);
	}
	else
	{
		if (parent_win != wm_win) {
			/* hide WM window for Hatari */
			XUnmapWindow(display, wm_win);

			/* reparent main Hatari window to given parent */
			XReparentWindow(display, sdl_win, parent_win, 0, 0);
		}

		Log_Printf(LOG_INFO, "New %dx%d SDL window with ID: %lx\n",
			   width, height, sdl_win);

		/* inform remote end of new window size if requested */
		Control_SendEmbedSize(width, height);
	}

	XSync(display, false);
}

/**
 * Return the X connection socket or zero
 */
int Screen_GetUISocket(void)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(sdlWindow, &info)) {
		Log_Printf(LOG_WARN, "Failed to get SDL_GetWMInfo()\n");
		return 0;
	}
	return ConnectionNumber(info.info.x11.display);
}

#else	/* HAVE_X11 */

static void Screen_ReparentWindow(int width, int height, bool noembed)
{
	/* TODO: implement the Windows part.  SDL sources offer example */
	Log_Printf(LOG_TODO, "Support for Hatari window reparenting not built in\n");
}
int Screen_GetUISocket(void)
{
	return 0;
}

#endif /* HAVE_X11 */


/**
 * Get current desktop resolution
 */
void Screen_GetDesktopSize(int *width, int *height)
{
	*width = desktop_width;
	*height = desktop_height;
}


/*
 * Create window backing texture when needed, with suitable scaling
 * quality.
 *
 * Window size is affected by ZoomFactor setting and window resizes
 * done by the user, and constrained by maximum window size setting
 * and desktop size.
 *
 * Calculate scale factor for the given resulting window size, compared
 * to the size of the SDL frame buffer rendered by Hatari, and based on
 * that, set the render scaling quality hint to:
 * - (sharp) nearest pixel sampling for integer zoom factors
 * - (smoothing/blurring) linear filtering otherwise
 *
 * If hint value changes from earlier one (or force flag is used),
 * window texture needs to be re-created to apply the scaling quality
 * change.
 */
void Screen_SetTextureScale(int width, int height, int win_width, int win_height, bool bForce)
{
	float scale_w, scale_h, scale;
	static bool prev_nearest;
	bool nearest;

	if (!(bUseSdlRenderer && sdlRenderer))
		return;

	scale_w = (float)win_width / width;
	scale_h = (float)win_height / height;
	if (bInFullScreen)
		/* SDL letterboxes fullscreen so it's enough for
		 * closest dimension to window size being evenly
		 * divisible.
		 */
		scale = fminf(scale_w, scale_h);
	else
		/* For windowed mode (= no letterboxing), both
		 * dimensions (here, their avg) need to be evenly
		 * divisible for nearest neighbor scaling to look good.
		 */
		scale = (scale_w + scale_h) / 2.0;

	nearest = (scale == floorf(scale));	// use nearest pixel filtering?

	DEBUGPRINT(("%dx%d / %dx%d -> scale = %g, nearest pixel = %d\n",
		    win_width, win_height, width, height, scale, nearest));

	if (bForce || nearest != prev_nearest)
	{
#if !ENABLE_SDL3
		char hint[2] = { nearest ? '0' : '1', 0 };

		/* hint needs to be there before texture */
		SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, hint, SDL_HINT_OVERRIDE);
#endif

		if (sdlTexture)
		{
			SDL_DestroyTexture(sdlTexture);
			sdlTexture = NULL;
		}

		sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGB888,
					       SDL_TEXTUREACCESS_STREAMING,
					       width, height);
		if (!sdlTexture)
		{
			fprintf(stderr, "%dx%d texture\n", width, height);
			Main_ErrorExit("Failed to create texture:", SDL_GetError(), -3);
		}

#if ENABLE_SDL3
		if (nearest)
			SDL_SetTextureScaleMode(sdlTexture, SDL_SCALEMODE_NEAREST);
#endif
		prev_nearest = nearest;
	}
}


/**
 * Change the SDL video mode.
 * @return true if mode has been changed, false if change was not necessary
 */
bool Screen_SetVideoSize(int width, int height, bool bForceChange)
{
	Uint32 sdlVideoFlags;
	const char *psSdlVideoDriver;
	bool bUseDummyMode;
	static bool bPrevUseVsync = false;
	static bool bPrevInFullScreen;
	int win_width, win_height;
	float scale = 1.0;

	/* Check if we really have to change the video mode: */
	if (sdlscrn != NULL && sdlscrn->w == width && sdlscrn->h == height && !bForceChange)
	{
		/* re-calculate variables in case height + statusbar height
		 * don't anymore match SDL surface size (there's an assert
		 * for that) */
		Statusbar_Init(sdlscrn);
		return false;
	}

	psSdlVideoDriver = SDL_getenv("SDL_VIDEODRIVER");
	bUseDummyMode = psSdlVideoDriver && !strcmp(psSdlVideoDriver, "dummy");

	if (bInFullScreen)
	{
		/* unhide the Hatari WM window for fullscreen */
		Screen_ReparentWindow(width, height, bInFullScreen);
	}

	bUseSdlRenderer = ConfigureParams.Screen.bUseSdlRenderer && !bUseDummyMode;

	/* SDL Video attributes: */
	win_width = width;
	win_height = height;
	if (bUseSdlRenderer)
	{
		scale = ConfigureParams.Screen.nZoomFactor;
		win_width *= scale;
		win_height *= scale;
	}
	if (bInFullScreen)
	{
		sdlVideoFlags = SDL_WINDOW_INPUT_GRABBED;
#if !ENABLE_SDL3
		if (ConfigureParams.Screen.bKeepResolution)
			sdlVideoFlags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_FULLSCREEN_DESKTOP;
		else
#endif
			sdlVideoFlags |= SDL_WINDOW_FULLSCREEN;
	}
	else
	{
		if (getenv("PARENT_WIN_ID") != NULL)	/* Embedded window? */
			sdlVideoFlags = SDL_WINDOW_BORDERLESS|SDL_WINDOW_HIDDEN;
		else if (ConfigureParams.Screen.bResizable && bUseSdlRenderer)
			sdlVideoFlags = SDL_WINDOW_RESIZABLE;
		else
			sdlVideoFlags = 0;
		/* Make sure that window is not bigger than current desktop */
		if (bUseSdlRenderer)
		{
			if (win_width > desktop_width)
				win_width = desktop_width;
			if (win_height > desktop_height)
				win_height = desktop_height;
		}
	}

	Screen_FreeSDL2Resources();
	if (sdlWindow &&
	    ((bInFullScreen && !ConfigureParams.Screen.bKeepResolution)
#if SDL_MAJOR_VERSION == 2 && SDL_PATCHLEVEL < 50
	     /* Real SDL2 (i.e. not the sdl2-compat lib) show weird behavior
	      * when keeping the window (reported on KDE with Wayland), thus
	      * allow to force-destroy it here */
	     || bForceChange
#endif
	    ))
	{
		SDL_DestroyWindow(sdlWindow);
		sdlWindow = NULL;
	}

	if (bPrevUseVsync != ConfigureParams.Screen.bUseVsync)
	{
		char hint[2] = { '0' + ConfigureParams.Screen.bUseVsync, 0 };
		SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, hint, SDL_HINT_OVERRIDE);
		bPrevUseVsync = ConfigureParams.Screen.bUseVsync;
	}

	/* Disable closing Hatari with alt+F4 under Windows as alt+F4 can be used by some emulated programs */
#if ENABLE_SDL3
	SDL_SetHintWithPriority(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0", SDL_HINT_OVERRIDE);
#else
	SDL_SetHintWithPriority(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, "1", SDL_HINT_OVERRIDE);
#endif

	/* Set new video mode */
	DEBUGPRINT(("SDL screen request: %d x %d (%s) -> window: %d x %d\n", width, height,
	           (bInFullScreen ? "fullscreen" : "windowed"), win_width, win_height));

	if (sdlWindow)
	{
		if (bPrevInFullScreen != bInFullScreen)
		{
#if ENABLE_SDL3
			SDL_SetWindowFullscreen(sdlWindow, bInFullScreen);
			SDL_SyncWindow(sdlWindow);
#else
			int mask = SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP;
			SDL_SetWindowFullscreen(sdlWindow, sdlVideoFlags & mask);
#endif
		}
		else if ((SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_MAXIMIZED) == 0)
		{
			SDL_SetWindowSize(sdlWindow, win_width, win_height);
		}
	}
	else
	{
		sdlWindow = SDL_CreateWindow("Hatari",
#if !ENABLE_SDL3
		                             SDL_WINDOWPOS_UNDEFINED,
		                             SDL_WINDOWPOS_UNDEFINED,
#endif
		                             win_width, win_height, sdlVideoFlags);
		if (!sdlWindow)
		{
			fprintf(stderr, "%dx%d window\n", win_width, win_height);
			Main_ErrorExit("Failed to create window:", SDL_GetError(), -1);
		}
	}
	if (bUseSdlRenderer)
	{
		int rm, bm, gm;

#if ENABLE_SDL3
		sdlRenderer = SDL_CreateRenderer(sdlWindow, NULL);
#else
		sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, 0);
#endif
		if (!sdlRenderer)
		{
			fprintf(stderr, "%dx%d renderer\n", win_width, win_height);
			Main_ErrorExit("Failed to create renderer:", SDL_GetError(), 1);
		}

		if (bInFullScreen)
#if ENABLE_SDL3
			SDL_SetRenderLogicalPresentation(sdlRenderer, width,
							 height,
							 SDL_LOGICAL_PRESENTATION_LETTERBOX);
#else
			SDL_RenderSetLogicalSize(sdlRenderer, width, height);
#endif
		else
			SDL_RenderSetScale(sdlRenderer, scale, scale);

		/* Force to black to stop side bar artifacts on 16:9 monitors. */
		SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
		SDL_RenderClear(sdlRenderer);
		SDL_RenderPresent(sdlRenderer);

		rm = 0x00FF0000;
		gm = 0x0000FF00;
		bm = 0x000000FF;
#if ENABLE_SDL3
		sdlscrn = SDL_CreateSurface(width, height,
					    SDL_GetPixelFormatForMasks(32, rm, gm, bm, 0));
#else
		sdlscrn = SDL_CreateRGBSurface(0, width, height, 32, rm, gm, bm, 0);

		SDL_RendererInfo sRenderInfo = { 0 };
		SDL_GetRendererInfo(sdlRenderer, &sRenderInfo);
		bIsSoftwareRenderer = sRenderInfo.flags & SDL_RENDERER_SOFTWARE;
#endif

		Screen_SetTextureScale(width, height, win_width, win_height, true);
	}
	else
	{
		sdlscrn = SDL_GetWindowSurface(sdlWindow);
		bIsSoftwareRenderer = true;
	}

	/* Exit if we can not open a screen */
	if (!sdlscrn)
	{
		Main_ErrorExit("Could not set video mode:", SDL_GetError(), -2);
	}

	DEBUGPRINT(("SDL screen granted: %dx%d @ %d, pitch=%d, locking required=%s\n",
	            sdlscrn->w, sdlscrn->h, sdlscrn->format->BitsPerPixel,
	            sdlscrn->pitch, SDL_MUSTLOCK(sdlscrn) ? "YES" : "NO"));
	DEBUGPRINT(("Pixel format: masks r=%04x g=%04x b=%04x, "
	            "shifts r=%d g=%d b=%d, losses r=%d g=%d b=%d\n",
	            sdlscrn->format->Rmask, sdlscrn->format->Gmask, sdlscrn->format->Bmask,
	            sdlscrn->format->Rshift, sdlscrn->format->Gshift, sdlscrn->format->Bshift,
	            sdlscrn->format->Rloss, sdlscrn->format->Gloss, sdlscrn->format->Bloss));

	if (!bInFullScreen)
	{
		/* re-embed the new Hatari SDL window */
		Screen_ReparentWindow(width, height, bInFullScreen);
	}

	Statusbar_Init(sdlscrn);

	/* screen area without the statusbar */
	STScreenRect.x = 0;
	STScreenRect.y = 0;
	STScreenRect.w = sdlscrn->w;
	STScreenRect.h = sdlscrn->h - Statusbar_GetHeight();

	Avi_SetSurface(sdlscrn->pixels, sdlscrn->w, sdlscrn->h, sdlscrn->pitch);

	bPrevInFullScreen = bInFullScreen;

	return true;
}


/**
 * Change the resolution - but only if it was already initialized before
 */
void Screen_ModeChanged(bool bForceChange)
{
	if (sdlscrn)	/* Do it only if we're already up and running */
	{
		ConvST_ChangeResolution(bForceChange);
	}
}


/**
 * Set Hatari window title. Use NULL for default
 */
void Screen_SetTitle(const char *title)
{
	if (title)
		SDL_SetWindowTitle(sdlWindow, title);
	else
		SDL_SetWindowTitle(sdlWindow, PROG_NAME);
}


/**
 * Init screen-related things of the SDL
 */
void Screen_Init(void)
{
	SDL_Surface *pIconSurf;
	char sIconFileName[FILENAME_MAX];

#if ENABLE_SDL3
	const SDL_DisplayMode *dm;

	/* Init SDL's video subsystem. Note: Audio subsystem
	   will be initialized later (failure not fatal). */
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		Main_ErrorExit("Could not initialize the SDL library:", SDL_GetError(), -1);
	}

	/* Get information about desktop resolution */
	dm = SDL_GetDesktopDisplayMode(1);
	if (dm)
	{
		desktop_width = dm->w;
		desktop_height = dm->h;
	}
	else
	{
		Log_Printf(LOG_ERROR, "SDL_GetDesktopDisplayMode failed: %s",
		           SDL_GetError());
		desktop_width = 2 * NUM_VISIBLE_LINE_PIXELS;
		desktop_height = 2 * NUM_VISIBLE_LINES + STATUSBAR_MAX_HEIGHT;
	}
#else
	SDL_DisplayMode dm;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		Main_ErrorExit("Could not initialize the SDL library:", SDL_GetError(), -1);
	}

	/* Needed on maemo but useful also with normal X11 window managers for
	 * window grouping when you have multiple Hatari SDL windows open */
	SDL_setenv("SDL_VIDEO_X11_WMCLASS", "hatari", 1);

	/* Get information about desktop resolution */
	if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
	{
		desktop_width = dm.w;
		desktop_height = dm.h;
	}
	else
	{
		Log_Printf(LOG_ERROR, "SDL_GetDesktopDisplayMode failed: %s",
		           SDL_GetError());
		desktop_width = 2 * NUM_VISIBLE_LINE_PIXELS;
		desktop_height = 2 * NUM_VISIBLE_LINES + STATUSBAR_MAX_HEIGHT;
	}
#endif

	/* If user hasn't set own max zoom size, use desktop size */
	if (!(ConfigureParams.Screen.nMaxWidth && ConfigureParams.Screen.nMaxHeight))
	{
		ConfigureParams.Screen.nMaxWidth = desktop_width;
		ConfigureParams.Screen.nMaxHeight = desktop_height;
	}
	DEBUGPRINT(("Desktop resolution: %dx%d\n",DesktopWidth, DesktopHeight));
	Log_Printf(LOG_DEBUG, "Configured max Hatari resolution = %dx%d, optimal for ST = %dx%d(+%d)\n",
		ConfigureParams.Screen.nMaxWidth, ConfigureParams.Screen.nMaxHeight,
		2*NUM_VISIBLE_LINE_PIXELS, 2*NUM_VISIBLE_LINES, STATUSBAR_MAX_HEIGHT);

	/* Set initial window resolution */
	bInFullScreen = ConfigureParams.Screen.bFullScreen;
	ConvST_ChangeResolution(false);

	/* Load and set icon */
	File_MakePathBuf(sIconFileName, sizeof(sIconFileName), Paths_GetDataDir(),
	                 "hatari-icon", "bmp");
	pIconSurf = SDL_LoadBMP(sIconFileName);
	if (pIconSurf)
	{
		SDL_SetColorKey(pIconSurf, SDL_TRUE, SDL_MapSurfaceRGB(pIconSurf, 255, 255, 255));
		SDL_SetWindowIcon(sdlWindow, pIconSurf);
		SDL_FreeSurface(pIconSurf);
	}

	/* Configure some SDL stuff: */
	Screen_ShowCursor(false);
	Screen_SetTitle(NULL);

	SDLGui_Init();
}


/**
 * Free screen bitmap and allocated resources
 */
void Screen_UnInit(void)
{
	SDLGui_UnInit();

	Screen_FreeSDL2Resources();
	if (sdlWindow)
	{
		SDL_DestroyWindow(sdlWindow);
		sdlWindow = NULL;
	}
}


/**
 * Clear Window display memory
 */
void Screen_ClearScreen(void)
{
	SDL_FillRect(sdlscrn, &STScreenRect, Screen_MapRGB(0, 0, 0));
}


/**
 * Enter Full screen mode
 */
void Screen_EnterFullScreen(void)
{
	bool bWasRunning;

	if (!bInFullScreen)
	{
		/* Hold things... */
		bWasRunning = Main_PauseEmulation(false);
		bInFullScreen = true;

		if (ConvGen_UseGenConvScreen())
		{
			ConvGen_SetSize(-1, -1, true);
			/* force screen redraw */
			Screen_GenConvUpdate(false);
		}
		else
		{
			ConvST_SetSTResolution(true);
			Screen_ClearScreen();       /* Black out screen bitmap as will be invalid when return */
		}

		if (!ConfigureParams.Screen.bKeepResolution)
		{
			/* Give monitor time to change to new resolution */
			SDL_Delay(20);
		}

		if (bWasRunning)
		{
			/* And off we go... */
			Main_UnPauseEmulation();
		}
		else
		{
			ConvST_Refresh(true);
		}
		/* Grab mouse pointer in fullscreen */
		SDL_SetWindowRelativeMouseMode(sdlWindow, true);
	}
}


/**
 * Return from Full screen mode back to a window
 */
void Screen_ReturnFromFullScreen(void)
{
	bool bWasRunning;

	if (bInFullScreen)
	{
		/* Hold things... */
		bWasRunning = Main_PauseEmulation(false);
		bInFullScreen = false;

		if (ConvGen_UseGenConvScreen())
		{
			ConvGen_SetSize(-1, -1, true);
			/* force screen redraw */
			Screen_GenConvUpdate(false);
		}
		else
		{
			ConvST_SetSTResolution(true);
		}

		if (!ConfigureParams.Screen.bKeepResolution)
		{
			/* Give monitor time to switch resolution */
			SDL_Delay(20);
		}

		if (bWasRunning)
		{
			/* And off we go... */
			Main_UnPauseEmulation();
		}
		else
		{
			ConvST_Refresh(true);
		}

		if (!bGrabMouse)
		{
			/* Un-grab mouse pointer in windowed mode */
			SDL_SetWindowRelativeMouseMode(sdlWindow, false);
		}
	}
}


bool Screen_UngrabMouse(void)
{
	bool old_grab = bGrabMouse;

	SDL_SetWindowRelativeMouseMode(sdlWindow, false);
	bGrabMouse = false;

	return old_grab;
}


void Screen_GrabMouseIfNecessary(void)
{
	SDL_SetWindowRelativeMouseMode(sdlWindow, bInFullScreen || bGrabMouse);
}


/**
 * Lock full-screen for drawing
 */
bool Screen_Lock(void)
{
	if (SDL_MUSTLOCK(sdlscrn))
	{
		if (SDL_LockSurface(sdlscrn))
		{
			Screen_ReturnFromFullScreen();   /* All OK? If not need to jump back to a window */
			return false;
		}
	}

	return true;
}

/**
 * UnLock full-screen
 */
void Screen_UnLock(void)
{
	if ( SDL_MUSTLOCK(sdlscrn) )
		SDL_UnlockSurface(sdlscrn);
}


/**
 * Blit our converted ST screen to window/full-screen
 */
static void Screen_Blit(SDL_Rect *sbar_rect)
{
	int count = 1;
	SDL_Rect rects[2];

	rects[0] = STScreenRect;
	if (sbar_rect)
	{
		rects[1] = *sbar_rect;
		count = 2;
	}
	Screen_UpdateRects(sdlscrn, count, rects);
}


/**
 * Draw ST screen to window/full-screen
 * @param  bForceFlip  Force screen update, even if contents did not change
 */
bool Screen_Draw(bool bForceFlip)
{
	SDL_Rect *sbar_rect;
	bool screen_changed;

	if (bQuitProgram)
	{
		return false;
	}

	/* restore area potentially left under overlay led
	 * and saved by Statusbar_OverlayBackup() */
	Statusbar_OverlayRestore(sdlscrn);

	/* And draw (if screen contents changed) */
	screen_changed = ConvST_DrawFrame();

	/* draw overlay led(s) or statusbar after unlock */
	Statusbar_OverlayBackup(sdlscrn);
	sbar_rect = Statusbar_Update(sdlscrn, false);

	/* And show to user */
	if (screen_changed || bForceFlip || sbar_rect)
	{
		Screen_Blit(sbar_rect);
	}

	return screen_changed;
}


void Screen_GenConvUpdate(bool update_statusbar)
{
	SDL_Rect rects[2], *extra = NULL;
	int count = 1;

	/* Don't update anything on screen if video output is disabled */
	if ( ConfigureParams.Screen.DisableVideo )
		return;

	if (update_statusbar)
		extra = Statusbar_Update(sdlscrn, false);

	rects[0] = STScreenRect;
	if (extra) {
		rects[1] = *extra;
		count = 2;
	}
	Screen_UpdateRects(sdlscrn, count, rects);
}

uint32_t Screen_GetGenConvWidth(void)
{
	return STScreenRect.w;
}

uint32_t Screen_GetGenConvHeight(void)
{
	return STScreenRect.h;
}

/**
 * Wrapper for SDL BPM save function
 * return 1 for success, -1 for fail
 */
int Screen_SaveBMP(const char *filename)
{
#if ENABLE_SDL3
	if(!SDL_SaveBMP_IO(sdlscrn, SDL_IOFromFile(filename, "wb"), 1))
#else
	if(SDL_SaveBMP_RW(sdlscrn, SDL_RWFromFile(filename, "wb"), 1) < 0)
#endif
	{
		Log_Printf(LOG_WARN, "SDL_SaveBMP_RW failed: %s", SDL_GetError());
		return -1;
	}
	return 1;
}

/**
 * Wrapper for Statusbar_AddMessage() and Statusbar_Update() in one go.
 */
void Screen_StatusbarMessage(const char *msg, uint32_t msecs)
{
	Statusbar_AddMessage(msg, msecs);
	Statusbar_Update(sdlscrn, true);
}


/**
 * Minimize window
 */
void Screen_MinimizeWindow(void)
{
	SDL_MinimizeWindow(sdlWindow);
}

/**
 * Get mouse coordinates
 */
uint32_t Screen_GetMouseState(int *mx, int *my)
{
	uint32_t ret;

#if ENABLE_SDL3
	float fx, fy;
	ret = SDL_GetMouseState(&fx, &fy);
	*mx = fx;
	*my = fy;
#else
	ret = SDL_GetMouseState(mx, my);
#endif
	return ret;
}


/**
 * Set mouse cursor visibility and return if it was visible before.
 */
bool Screen_ShowCursor(bool show)
{
	bool bOldVisibility;

#if ENABLE_SDL3
	bOldVisibility = SDL_CursorVisible();
	if (bOldVisibility != show)
	{
		if (show)
		{
			SDL_ShowCursor();
		}
		else
		{
			SDL_HideCursor();
		}
	}
#else
	bOldVisibility = SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
	if (bOldVisibility != show)
	{
		if (show)
		{
			SDL_ShowCursor(SDL_ENABLE);
		}
		else
		{
			SDL_ShowCursor(SDL_DISABLE);
		}
	}
#endif
	return bOldVisibility;
}
