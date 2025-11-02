/*
  Hatari - screen_sdl.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREEN_SDL_H
#define HATARI_SCREEN_SDL_H

#include <SDL_video.h>    /* for SDL_Surface */

extern SDL_Surface *sdlscrn;
extern SDL_Window *sdlWindow;

void Screen_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects);
void Screen_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h);

#endif  /* ifndef HATARI_SCREEN_H */
