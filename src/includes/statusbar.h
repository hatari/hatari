/*
  Hatari - statusbar.h
  
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_STATUSBAR_H
#define HATARI_STATUSBAR_H

#include <SDL.h>
#include "main.h"

extern void Statusbar_SetDriveLed(int drive, bool state);
extern int Statusbar_SetHeight(int ScreenHeight, bool FullScreen);
extern int Statusbar_GetHeight(void);

extern void Statusbar_Init(SDL_Surface *screen);
extern SDL_Rect* Statusbar_Update(SDL_Surface *screen);

#endif /* HATARI_STATUSBAR_H */
