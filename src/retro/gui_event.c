/*
  Hatari - gui_event.c

  SPDX-License-Identifier: GPL-2.0-or-later

  User interface (libretro) event handling
*/

#include <libretro.h>

#include "main.h"
#include "configuration.h"
#include "control.h"
#include "conv_st.h"
#include "gui_event.h"
#include "ikbd.h"
#include "log.h"
#include "main_retro.h"
#include "screen.h"
#include "shortcut.h"
#include "video.h"


void GuiEvent_WarpMouse(int x, int y, bool restore)
{
}


/**
 * Handle mouse motion.
 */
static void GuiEvent_HandleMouseMotion(void)
{
	static int ax = 0, ay = 0;
	int dx, dy;

	dx  = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
	dy  = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);

	/* Ignore motion when position has changed right after a reset or TOS
	 * (especially version 4.04) might get confused and play key clicks */
	if (nVBLs < 10)
	{
		return;
	}

	/* In zoomed low res mode, we divide dx and dy by the zoom factor so that
	 * the ST mouse cursor stays in sync with the host mouse. However, we have
	 * to take care of lowest bit of dx and dy which will get lost when
	 * dividing. So we store these bits in ax and ay and add them to dx and dy
	 * the next time. */
	if (nScreenZoomX != 1)
	{
		dx += ax;
		ax = dx % nScreenZoomX;
		dx /= nScreenZoomX;
	}
	if (nScreenZoomY != 1)
	{
		dy += ay;
		ay = dy % nScreenZoomY;
		dy /= nScreenZoomY;
	}

	KeyboardProcessor.Mouse.dx += dx;
	KeyboardProcessor.Mouse.dy += dy;
}


static void GuiEvent_HandleMouseButton(void)
{
	bool bl = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
	bool bm = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE);
	bool br = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);

	if (bl)
	{
		if (Keyboard.LButtonDblClk == 0)
			Keyboard.bLButtonDown |= BUTTON_MOUSE;
	}
	else
	{
		Keyboard.bLButtonDown &= ~BUTTON_MOUSE;
	}

	if (br)
	{
		Keyboard.bRButtonDown |= BUTTON_MOUSE;
	}
	else
	{
		Keyboard.bRButtonDown &= ~BUTTON_MOUSE;
	}


	if (bm)
	{
		/* Start double-click sequence in emulation time */
		Keyboard.LButtonDblClk = 1;
	}
}


/**
 * Poll events from libretro.
 * Here we process the events (keyboard, mouse, ...) and map it to
 * Atari IKBD events.
 */
void GuiEvent_EventHandler(void)
{
	GuiEvent_HandleMouseMotion();
	GuiEvent_HandleMouseButton();
}
