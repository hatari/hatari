/*
  Hatari - keymap_sdl.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_KEYMAP_SDL_H
#define HATARI_KEYMAP_SDL_H

#if ENABLE_SDL3
#include <SDL3/SDL_keyboard.h>
#else
#include <SDL_keyboard.h>
#endif

void Keymap_KeyDown(SDL_Keymod modkey, SDL_Keycode symkey, SDL_Scancode scancode);
void Keymap_KeyUp(SDL_Keymod modkey, SDL_Keycode symkey, SDL_Scancode scancode);

#endif
