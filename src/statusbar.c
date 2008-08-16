/*
  Hatari - statusbar.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Code to draw statusbar area, floppy leds etc.
  
  Use like this:
  - Before screen surface is (re-)created Statusbar_SetHeight()
    has to be called with the new screen height. Add the returned
    value to screen height
  - After screen surface is (re-)created, call Statusbar_Init()
    to re-initialize / re-draw the statusbar
  - Call Statusbar_SetDriveLed() to set drive led ON or OFF
  - Whenever screen is redrawn, call Statusbar_Update() to
    draw the updated information to the statusbar
*/
const char statusbar_rcsid[] = "$Id: statusbar.c,v 1.3 2008-08-16 15:49:45 eerot Exp $";

#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "sdlgui.h"
#include "statusbar.h"

/* whether drive leds should be ON and their previous shown state */
struct {
	bool state;
	bool oldstate;
	/* led x-pos on screen */
	int offset;
} Led[MAX_DRIVE_LEDS];

/* led size & y-pos on the screen */
static SDL_Rect LedRect;

/* led colors */
Uint32 LedColorOn, LedColorOff;

/* screen height above statusbar and height of statusbar below screen */
static int ScreenHeight;
static int StatusbarHeight;


/*-----------------------------------------------------------------------*/
/**
 * Set screen height used for statusbar height calculation.
 *
 * Return height of statusbar that should be added to the screen
 * height when screen is (re-)created, or zero if statusbar will
 * not be shown
 */
int Statusbar_SetHeight(int height, bool fullscreen)
{
	ScreenHeight = height;
	if (ConfigureParams.Screen.bShowStatusbar && !fullscreen) {
		/* make an assumption about the font size
		 * that fits into the statusbar
		 */
		if (height >= 400) {
			StatusbarHeight = 24;
		} else {
			StatusbarHeight = 12;
		}
	} else {
		StatusbarHeight = 0;
	}
	return StatusbarHeight;
}

/*-----------------------------------------------------------------------*/
/**
 * Return height of statusbar set with Statusbar_SetHeight()
 */
int Statusbar_GetHeight(void)
{
	return StatusbarHeight;
}

/*-----------------------------------------------------------------------*/
/**
 * Return height of statusbar set with Statusbar_SetHeight()
 */
void Statusbar_SetDriveLed(drive_index_t drive, bool state)
{
	assert(drive < MAX_DRIVE_LEDS);
	Led[drive].state = state;
}

/*-----------------------------------------------------------------------*/
/**
 * (re-)initialize statusbar internal variables for given screen surface
 * (sizes need to be re-calculated in case screen size changes).
 */
void Statusbar_Init(SDL_Surface *surf)
{
	Uint32 gray, black;
	SDL_Rect ledbox, sbarbox;
	int i, y, fw, fh, distance, offset;
	char text[] = "A:";

	if (!StatusbarHeight) {
		return;
	}
	assert(surf);
	/* Statusbar_SetHeight() not called before this? */
	assert(surf->h == ScreenHeight + StatusbarHeight);

	/* prepare fonts */
	SDLGui_Init();
	SDLGui_SetScreen(surf);
	SDLGui_GetFontSize(&fw, &fh);
	assert(fh+2 < StatusbarHeight);

	/* draw statusbar background gray so that text shows */
	sbarbox.x = 0;
	sbarbox.y = surf->h - StatusbarHeight;
	sbarbox.w = surf->w;
	sbarbox.h = StatusbarHeight;
	gray = SDL_MapRGB(surf->format, 0xc0, 0xc0, 0xc0);
	SDL_FillRect(surf, &sbarbox, gray);

	/* distance between leds */
	distance = surf->w / (MAX_DRIVE_LEDS + 1);

	/* led size */
	LedRect.w = 2*fh;
	LedRect.h = fh - 4;
	LedRect.y = ScreenHeight + StatusbarHeight/2 - LedRect.h/2;
	
	/* black box for the leds */
	ledbox = LedRect;
	ledbox.y -= 1;
	ledbox.w += 2;
	ledbox.h += 2;
	black = SDL_MapRGB(surf->format, 0x00, 0x00, 0x00);

	/* dark green and light green for leds themselves */
	LedColorOff = SDL_MapRGB(surf->format, 0x00, 0x40, 0x00);
	LedColorOn  = SDL_MapRGB(surf->format, 0x00, 0xf0, 0x00);

	/* set led members and draw their boxes */
	for (i = 0; i < MAX_DRIVE_LEDS; i++) {
		offset = (i+1)*distance;
		Led[i].offset = offset;

		ledbox.x = offset - 1;
		SDL_FillRect(surf, &ledbox, black);

		LedRect.x = offset;
		SDL_FillRect(surf, &LedRect, LedColorOff);
		Led[i].oldstate = FALSE;
	}

	/* TODO:
	 * - box for red wav/ym recording indicator
	 * - TOS Version
	 * - frameskip...
	 * ?
	 */
	
	/* draw led box labels */
	y = LedRect.y - 2;
	for (i = 0; i < MAX_DRIVE_LEDS; i++) {
		text[0] = 'A' + i;
		SDLGui_Text(Led[i].offset - 3*fw, y, text);
	}

	/* and blit statusbar on screen */
	SDL_UpdateRects(surf, 1, &sbarbox);
}

/*-----------------------------------------------------------------------*/
/**
 * Update statusbar information (leds etc) if/when needed.
 * 
 * May not be called when screen is locked (SDL limitation).
 */
void Statusbar_Update(SDL_Surface *surf)
{
	SDL_Rect rect;
	Uint32 color;
	int i;

	if (!(StatusbarHeight && ConfigureParams.Screen.bShowStatusbar)) {
		/* not enabled (anymore) */
		return;
	}
	assert(surf);
	/* Statusbar_SetHeight() not called before this? */
	assert(surf->h == ScreenHeight + StatusbarHeight);

	rect = LedRect;
	for (i = 0; i < MAX_DRIVE_LEDS; i++) {
		if (Led[i].state == Led[i].oldstate) {
			continue;
		}
		Led[i].oldstate = Led[i].state;
		if (Led[i].state) {
			color = LedColorOn;
		} else {
			color = LedColorOff;
		}
		rect.x = Led[i].offset;
		SDL_FillRect(surf, &rect, color);
		SDL_UpdateRects(surf, 1, &rect);
	}
}
