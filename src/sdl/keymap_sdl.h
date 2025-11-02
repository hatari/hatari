/*
  Hatari - keymap_sdl.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_KEYMAP_SDL_H
#define HATARI_KEYMAP_SDL_H

#include <SDL_keyboard.h>

extern void Keymap_KeyDown(const SDL_Keysym *sdlkey);
extern void Keymap_KeyUp(const SDL_Keysym *sdlkey);

#endif
