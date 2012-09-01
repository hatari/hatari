/*
  Hatari - statusbar.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_STATUSBAR_H
#define HATARI_STATUSBAR_H

#include <SDL.h>
#include "main.h"

typedef enum {
	DRIVE_LED_A,
	DRIVE_LED_B,
	DRIVE_LED_HD
} drive_index_t;

extern int Statusbar_SetHeight(int ScreenWidth, int ScreenHeight);
extern int Statusbar_GetHeightForSize(int width, int height);
extern int Statusbar_GetHeight(void);
extern void Statusbar_EnableHDLed(void);
extern void Statusbar_SetFloppyLed(drive_index_t drive, bool state);

extern void Statusbar_Init(SDL_Surface *screen);
extern void Statusbar_UpdateInfo(void);
extern void Statusbar_AddMessage(const char *msg, Uint32 msecs);
extern void Statusbar_OverlayBackup(SDL_Surface *screen);
extern void Statusbar_Update(SDL_Surface *screen);
extern void Statusbar_OverlayRestore(SDL_Surface *screen);

#endif /* HATARI_STATUSBAR_H */
