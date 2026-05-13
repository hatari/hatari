/*
  Hatari - screen_metal.h
  Copyright (C) 2026 by manni07
  Created: 2026-05-13

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREEN_METAL_H
#define HATARI_SCREEN_METAL_H

#include <stdbool.h>

#if ENABLE_SDL3
#include <SDL3/SDL_video.h>
#else
#include <SDL_video.h>
#endif

#if defined(ENABLE_METAL_RENDERER) && ENABLE_METAL_RENDERER
bool ScreenMetal_Available(void);
bool ScreenMetal_Init(SDL_Window *window, int width, int height, int win_width, int win_height);
void ScreenMetal_UnInit(void);
void ScreenMetal_Update(SDL_Surface *screen);
void ScreenMetal_SetTextureScale(int width, int height, int win_width, int win_height, bool nearest);
void ScreenMetal_GetContentRect(int *x, int *y, int *width, int *height);
#else
static inline bool ScreenMetal_Available(void)
{
	return false;
}
static inline bool ScreenMetal_Init(SDL_Window *window, int width, int height, int win_width, int win_height)
{
	(void)window;
	(void)width;
	(void)height;
	(void)win_width;
	(void)win_height;
	return false;
}
static inline void ScreenMetal_UnInit(void)
{
}
static inline void ScreenMetal_Update(SDL_Surface *screen)
{
	(void)screen;
}
static inline void ScreenMetal_SetTextureScale(int width, int height, int win_width, int win_height, bool nearest)
{
	(void)width;
	(void)height;
	(void)win_width;
	(void)win_height;
	(void)nearest;
}
static inline void ScreenMetal_GetContentRect(int *x, int *y, int *width, int *height)
{
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}
#endif

#endif
