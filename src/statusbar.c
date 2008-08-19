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
    draw the updated information to the statusbar (outside
    of screen locking)
  - If screen redraws may be partial, Statusbar_OverlayRestore()
    needs to be called before locking the screen for drawing and
    Statusbar_OverlayBackup() called after screen unlocking but
    before calling Statusbar_Update().  This is needed for
    hiding the overlay drive led when drive led is disabled.
*/
const char statusbar_rcsid[] = "$Id: statusbar.c,v 1.4 2008-08-19 19:47:29 eerot Exp $";

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

/* drive leds size & y-pos */
static SDL_Rect LedRect;

/* overlay led size & pos */
static SDL_Rect OverlayLedRect;

/* screen contents left under overlay led */
static SDL_Surface *OverlayUnderside;

static enum {
	OVERLAY_NONE,
	OVERLAY_DRAWN,
	OVERLAY_RESTORED
} bOverlayState;

/* led colors */
Uint32 LedColorOn, LedColorOff, LedColorBg;

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
 * Set overlay led size/pos on given screen to internal Rect
 * and free previous resources.
 */
static void Statusbar_OverlayInit(const SDL_Surface *surf)
{
	int h;
	/* led size/pos needs to be re-calculated in case screen changed */
	h = surf->h / 50;
	OverlayLedRect.w = 2*h;
	OverlayLedRect.h = h;
	OverlayLedRect.x = surf->w - 5*h/2;
	OverlayLedRect.y = h/2;
	/* free previous restore surface if it's incompatible */
	if (OverlayUnderside &&
	    OverlayUnderside->w == OverlayLedRect.w &&
	    OverlayUnderside->h == OverlayLedRect.h &&
	    OverlayUnderside->format->BitsPerPixel == surf->format->BitsPerPixel) {
		SDL_FreeSurface(OverlayUnderside);
		OverlayUnderside = NULL;
	}
	bOverlayState = OVERLAY_NONE;
}

/*-----------------------------------------------------------------------*/
/**
 * (re-)initialize statusbar internal variables for given screen surface
 * (sizes need to be re-calculated in case screen size changes).
 */
void Statusbar_Init(SDL_Surface *surf)
{
	Uint32 gray;
	SDL_Rect ledbox, sbarbox;
	int i, y, fw, fh, distance, offset;
	char text[] = "A:";

	assert(surf);

	/* dark green and light green for leds themselves */
	LedColorOff = SDL_MapRGB(surf->format, 0x00, 0x40, 0x00);
	LedColorOn  = SDL_MapRGB(surf->format, 0x00, 0xe0, 0x00);
	LedColorBg  = SDL_MapRGB(surf->format, 0x00, 0x00, 0x00);

	Statusbar_OverlayInit(surf);
	if (!StatusbarHeight) {
		return;
	}

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

	/* set led members and draw their boxes */
	for (i = 0; i < MAX_DRIVE_LEDS; i++) {
		offset = (i+1)*distance;
		Led[i].offset = offset;

		ledbox.x = offset - 1;
		SDL_FillRect(surf, &ledbox, LedColorBg);

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
 * Restore the area left under overlay led
 */
void Statusbar_OverlayRestore(SDL_Surface *surf)
{
	if (StatusbarHeight && ConfigureParams.Screen.bShowStatusbar) {
		/* overlay not used with statusbar */
		return;
	}
	if (bOverlayState == OVERLAY_DRAWN && OverlayUnderside) {
		assert(surf);
		SDL_BlitSurface(OverlayUnderside, NULL, surf, &OverlayLedRect);
		/* update to screen happens in the draw function with this */
		bOverlayState = OVERLAY_RESTORED;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Save the area that will be left under overlay led
 */
void Statusbar_OverlayBackup(SDL_Surface *surf)
{
	if (StatusbarHeight && ConfigureParams.Screen.bShowStatusbar) {
		/* overlay not used with statusbar */
		return;
	}
	assert(surf);
	if (!OverlayUnderside) {
		SDL_Surface *bak;
		SDL_PixelFormat *fmt = surf->format;
		bak = SDL_CreateRGBSurface(surf->flags,
					   OverlayLedRect.w, OverlayLedRect.h,
					   fmt->BitsPerPixel,
					   fmt->Rmask, fmt->Gmask, fmt->Bmask,
					   fmt->Amask);
		assert(bak);
		OverlayUnderside = bak;
	}
	SDL_BlitSurface(surf, &OverlayLedRect, OverlayUnderside, NULL);
}

/*-----------------------------------------------------------------------*/
/**
 * Draw overlay led onto screen surface if drives are enabled.
 */
static void Statusbar_OverlayDraw(SDL_Surface *surf)
{
	int i;
	assert(surf);
	for (i = 0; i < MAX_DRIVE_LEDS; i++) {
		if (Led[i].state) {
			/* enabled led with border */
			SDL_Rect rect = OverlayLedRect;
			rect.x += 1;
			rect.y += 1;
			rect.w -= 2;
			rect.h -= 2;
			SDL_FillRect(surf, &OverlayLedRect, LedColorBg);
			SDL_FillRect(surf, &rect, LedColorOn);
			bOverlayState = OVERLAY_DRAWN;
			break;
		}
	}
	/* possible state transitions:
	 *   NONE -> DRAWN -> RESTORED -> DRAWN -> RESTORED -> NONE
	 * Other than NONE state needs to be updated on screen
	 */
	switch (bOverlayState) {
	case OVERLAY_RESTORED:
		bOverlayState = OVERLAY_NONE;
	case OVERLAY_DRAWN:
		SDL_UpdateRects(surf, 1, &OverlayLedRect);
		break;
	case OVERLAY_NONE:
		break;
	}
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
		/* not enabled (anymore), show overlay led instead? */
		if (ConfigureParams.Screen.bShowDriveLed) {
			Statusbar_OverlayDraw(surf);
		}
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
