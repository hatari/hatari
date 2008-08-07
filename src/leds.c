/*
  Hatari - leds.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This code draws indicators like the floppy light on top of the Hatari screen
*/
const char leds_rcsid[] = "$Id: leds.c,v 1.2 2008-08-07 19:41:53 eerot Exp $";

#include <assert.h>
#include "configuration.h"
#include "leds.h"

/* whether floppy led should be on */
bool bFloppyLight;

#define LEDS_DEBUG 0


static bool bLedsDrawn;			/* whether leds were drawn */

static SDL_Rect screen_rect;		/* leds position on screen */
static SDL_Surface *screen_surf;	/* screen surface where to draw leds */
static SDL_Surface *leds_image;		/* led images to draw */
static SDL_Surface *leds_under;		/* screen are left under leds */


/*-----------------------------------------------------------------------*/
/**
 * (Re-)initialize leds for given screen surface
 * 
 * TODO: any other indicators/leds needed besides single floppy one?
 */
void Leds_ReInit(SDL_Surface *surf)
{
	Uint32 black, green;
	SDL_Rect green_rect;
	SDL_PixelFormat *fmt;
	int w, h, bpp;
	
	assert(surf);
	screen_surf = surf;
	fmt = surf->format;
	bpp = fmt->BitsPerPixel;

	/* new screen surface doesn't have leds */
	bLedsDrawn = FALSE;

	/* size need to be re-calculated in case screen size changes */
	w = surf->w / 32;
	h = surf->h / 50;
	screen_rect.w = w;
	screen_rect.h = h;
	screen_rect.x = surf->w - 5*w/4;
	screen_rect.y = h/2;

	/* re-alloc new surface for leds image? */
	if (leds_image) {
		if (w == leds_image->w && h == leds_image->h
		    && bpp == leds_image->format->BitsPerPixel) {
			return;
		}
		if (leds_under) {
			SDL_FreeSurface(leds_under);
			leds_under = NULL;
		}
		SDL_FreeSurface(leds_image);
	}
	leds_image = SDL_CreateRGBSurface(surf->flags, w, h, bpp,
					  fmt->Rmask, fmt->Gmask, fmt->Bmask,
					  0);
	assert(leds_image);

	/* image as green box with black borders, colors need
	 * to be re-calculated between different bitdepths
	 */
	if (fmt->BytesPerPixel == 1) {
		SDL_Color colors[2];
		memset(colors, 0, sizeof(colors));
		colors[1].g = 0xf0;
		SDL_SetColors(leds_image, colors, 0, 2);
		black = 0;
		green = 1;
	} else {
		black = SDL_MapRGB(fmt, 0x00, 0x00, 0x00);
		green = SDL_MapRGB(fmt, 0x00, 0xf0, 0x00);
	}
#if LEDS_DEBUG
	printf("bpp: %d, black: %06x, green: %06x\n",
	       fmt->BytesPerPixel, black, green);
#endif
	SDL_FillRect(leds_image, NULL, black);
	green_rect.w = w - 2;
	green_rect.h = h - 2;
	green_rect.x = 1;
	green_rect.y = 1;
	SDL_FillRect(leds_image, &green_rect, green);
}


/*-----------------------------------------------------------------------*/
/**
 * Draw leds on screen if/when needed.
 * Should not be called when the display surface is locked.
 * Return SDL_Rect* of the updated area or NULL if nothing is drawn
 */
SDL_Rect* Leds_Show(void)
{
	int ok;

	if (!ConfigureParams.Screen.bShowLeds) {
		return NULL;
	}
	if (!bFloppyLight) {
		return NULL;
	}
	assert(screen_surf);
	
	/* backup area left under leds */
	if (!leds_under) {
		SDL_PixelFormat *fmt = screen_surf->format;
		leds_under = SDL_CreateRGBSurface(screen_surf->flags,
						  screen_rect.w, screen_rect.h,
						  fmt->BitsPerPixel,
						  fmt->Rmask, fmt->Gmask, fmt->Bmask,
						  fmt->Amask);
		assert(leds_under);
	}
	SDL_BlitSurface(screen_surf, &screen_rect, leds_under, NULL);
	
	/* and show led(s) */
	ok = SDL_BlitSurface(leds_image, NULL, screen_surf, &screen_rect);
#if LEDS_DEBUG
	printf("blit leds: %dx%d+%d+%d -> %d\n",
	       screen_rect.w, screen_rect.h,
	       screen_rect.x, screen_rect.y, ok);
	if (ok < 0) {
		printf("leds blitting error: %s\n", SDL_GetError());
	}
#endif
	bLedsDrawn = TRUE;
	return &screen_rect;
}


/*-----------------------------------------------------------------------*/
/**
 * Restore area left under leds.
 * Should not be called when the display surface is locked.
 * Return TRUE if something was drawn, FALSE otherwise.
 */
SDL_Rect* Leds_Hide(void)
{
	if (leds_under && bLedsDrawn) {
		assert(screen_surf);
		SDL_BlitSurface(leds_under, NULL, screen_surf, &screen_rect);
		bLedsDrawn = FALSE;
		return &screen_rect;
	}
	return NULL;
}
