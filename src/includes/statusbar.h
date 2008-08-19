/*
  Hatari - statusbar.h
  
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_STATUSBAR_H
#define HATARI_STATUSBAR_H

#include <SDL.h>
#include "main.h"

typedef enum {
	DRIVE_LED_A,
	DRIVE_LED_B,
	DRIVE_LED_C
} drive_index_t;

#define MAX_DRIVE_LEDS (DRIVE_LED_C + 1)

extern void Statusbar_SetDriveLed(drive_index_t drive, bool state);
extern int Statusbar_SetHeight(int ScreenHeight, bool FullScreen);
extern int Statusbar_GetHeight(void);

extern void Statusbar_Init(SDL_Surface *screen);
extern void Statusbar_OverlayBackup(SDL_Surface *screen);
extern void Statusbar_Update(SDL_Surface *screen);
extern void Statusbar_OverlayRestore(SDL_Surface *screen);

#endif /* HATARI_STATUSBAR_H */
