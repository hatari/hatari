/*
  Hatari - statusbar_sdl.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_STATUSBAR_SDL_H
#define HATARI_STATUSBAR_SDL_H

#if ENABLE_SDL3
#include <SDL3/SDL_video.h>
#else
#include <SDL_video.h>
#endif

void Statusbar_Init(SDL_Surface *screen);
void Statusbar_OverlayBackup(SDL_Surface *screen);
SDL_Rect* Statusbar_Update(SDL_Surface *screen, bool do_update);
void Statusbar_OverlayRestore(SDL_Surface *screen);

#endif
