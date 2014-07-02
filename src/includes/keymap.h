/*
  Hatari - keymap.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_KEYMAP_H
#define HATARI_KEYMAP_H

#include <SDL_keyboard.h>

#if WITH_SDL2
#define SDLKey SDL_Keycode
#define SDL_keysym SDL_Keysym
#define KMOD_LMETA KMOD_LGUI
#define KMOD_RMETA KMOD_RGUI
#define SDLK_LMETA SDLK_LGUI
#define SDLK_RMETA SDLK_RGUI
#define SDLK_NUMLOCK SDLK_NUMLOCKCLEAR
#endif

extern void Keymap_Init(void);
extern void Keymap_LoadRemapFile(char *pszFileName);
extern void Keymap_DebounceAllKeys(void);
extern void Keymap_KeyDown(SDL_keysym *sdlkey);
extern void Keymap_KeyUp(SDL_keysym *sdlkey);
extern void Keymap_SimulateCharacter(char asckey, bool press);

#endif
