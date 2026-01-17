/*
  Hatari - screen.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This code handles the screen related functions.
*/

const char Screen_fileid[] = "Hatari screen.c";

#include <assert.h>
#include <libretro.h>

#include "main.h"
#include "main_retro.h"
#include "configuration.h"
#include "control.h"
#include "conv_gen.h"
#include "conv_st.h"
#include "avi_record.h"
#include "file.h"
#include "log.h"
#include "m68000.h"
#include "paths.h"
#include "options.h"
#include "screen.h"
#include "spec512.h"
#include "statusbar.h"
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

/* extern for shortcuts etc. */
bool bGrabMouse = false;      /* Grab the mouse cursor in the window */
bool bInFullScreen = false;   /* true if in full screen */

static uint32_t *framebuffer;
static int screen_width, screen_height;


/**
 * Get pixel format information (mask and shift values)
 */
void Screen_GetPixelFormat(uint32_t *rmask, uint32_t *gmask, uint32_t *bmask,
                           int *rshift, int *gshift, int *bshift)
{
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
}

uint32_t Screen_MapRGB(uint8_t red, uint8_t green, uint8_t blue)
{
	return (red << 16) | (green << 8) | blue;
}


/**
 * Get the dimension and start address of the screen.
 */
void Screen_GetDimension(uint32_t **pixels, int *width, int *height, int *pitch)
{
	if (pixels)
		*pixels = framebuffer;
	if (width)
		*width = screen_width;
	if (height)
		*height = screen_height;
	if (pitch)
		*pitch = screen_width * 4;
}


int Screen_GetUISocket(void)
{
	return 0;
}

/**
 * Get current desktop resolution
 */
void Screen_GetDesktopSize(int *width, int *height)
{
	*width = 1024;
	*height = 768;
}


/**
 * Change the video mode.
 * @return true if mode has been changed, false if change was not necessary
 */
bool Screen_SetVideoSize(int width, int height, bool bForceChange)
{
	struct retro_system_av_info av_info;

	if (width == screen_width && height == screen_height && !bForceChange)
		return false;

	if (framebuffer)
		free(framebuffer);

	framebuffer = malloc(width * height * sizeof(*framebuffer));
	if (!framebuffer)
	{
		perror("malloc in Screen_SetVideoSize");
		return false;
	}

	screen_width = width;
	screen_height = height;

	retro_get_system_av_info(&av_info);
	environment_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av_info);

	return true;
}


/**
 * Change the resolution - but only if it was already initialized before
 */
void Screen_ModeChanged(bool bForceChange)
{
	ConvST_ChangeResolution(bForceChange);
}


/**
 * Set Hatari window title. Use NULL for default
 */
void Screen_SetTitle(const char *title)
{
}


/**
 * Init screen-related things
 */
void Screen_Init(void)
{
	/* Zooming will be done by libretro - so disable it here for now */
	ConfigureParams.Screen.nZoomFactor = 1.0;
	ConfigureParams.Screen.nMaxWidth = 320;
	ConfigureParams.Screen.nMaxHeight = 200;
	ConfigureParams.Screen.bAllowOverscan = false;

	/* Auto-frameskipping does not work well yet, so hard-wire to 1 */
	ConfigureParams.Screen.nFrameSkips = 1;
}


/**
 * Free screen bitmap and allocated resources
 */
void Screen_UnInit(void)
{
}


/**
 * Clear Window display memory
 */
void Screen_ClearScreen(void)
{
	if (framebuffer)
	{
		memset(framebuffer, 0,
		       screen_width * screen_height * sizeof(*framebuffer));
	}
}


/**
 * Enter Full screen mode
 */
void Screen_EnterFullScreen(void)
{
}


/**
 * Return from Full screen mode back to a window
 */
void Screen_ReturnFromFullScreen(void)
{
}


bool Screen_UngrabMouse(void)
{
	return false;
}


void Screen_GrabMouseIfNecessary(void)
{
}


/**
 * Lock full-screen for drawing
 */
bool Screen_Lock(void)
{
	return true;
}

/**
 * UnLock full-screen
 */
void Screen_UnLock(void)
{
}


static void Screen_QuitCpuLoop(void)
{
	M68000_SetSpecial(SPCFLAG_BRK);
	quit_program = UAE_QUIT;
}


/**
 * Draw ST screen to window/full-screen
 * @param  bForceFlip  Force screen update, even if contents did not change
 */
bool Screen_Draw(bool bForceFlip)
{
	bool screen_changed;

	/* And draw (if screen contents changed) */
	screen_changed = ConvST_DrawFrame();

	if (screen_changed)
	{
		video_refresh_cb(framebuffer, screen_width, screen_height,
		                 screen_width * 4);
	}

	Screen_QuitCpuLoop();
	return screen_changed;
}


void Screen_GenConvUpdate(bool update_statusbar)
{
	video_refresh_cb(framebuffer, screen_width, screen_height,
	                 screen_width * 4);

	Screen_QuitCpuLoop();
}

uint32_t Screen_GetGenConvWidth(void)
{
	return screen_width;
}

uint32_t Screen_GetGenConvHeight(void)
{
	return screen_height;
}

/**
 * Wrapper for BPM save function
 * return 1 for success, -1 for fail
 */
int Screen_SaveBMP(const char *filename)
{
	return -1;
}

/**
 * Wrapper for Statusbar_AddMessage() and Statusbar_Update() in one go.
 */
void Screen_StatusbarMessage(const char *msg, uint32_t msecs)
{
	Statusbar_AddMessage(msg, msecs);
}


/**
 * Minimize window
 */
void Screen_MinimizeWindow(void)
{
}

/**
 * Get mouse coordinates
 */
uint32_t Screen_GetMouseState(int *mx, int *my)
{
	uint32_t ret = 0;

	return ret;
}


/**
 * Set mouse cursor visibility and return if it was visible before.
 */
bool Screen_ShowCursor(bool show)
{
	return false;
}
