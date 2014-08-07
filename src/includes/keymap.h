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
#define SDLK_KP0 SDLK_KP_0
#define SDLK_KP1 SDLK_KP_1
#define SDLK_KP2 SDLK_KP_2
#define SDLK_KP3 SDLK_KP_3
#define SDLK_KP4 SDLK_KP_4
#define SDLK_KP5 SDLK_KP_5
#define SDLK_KP6 SDLK_KP_6
#define SDLK_KP7 SDLK_KP_7
#define SDLK_KP8 SDLK_KP_8
#define SDLK_KP9 SDLK_KP_9
#define SDLK_PRINT SDLK_PRINTSCREEN
#define SDLK_SCROLLOCK SDLK_SCROLLLOCK
#endif

extern void Keymap_Init(void);
extern void Keymap_LoadRemapFile(char *pszFileName);
extern void Keymap_DebounceAllKeys(void);
extern void Keymap_KeyDown(SDL_keysym *sdlkey);
extern void Keymap_KeyUp(SDL_keysym *sdlkey);
extern void Keymap_SimulateCharacter(char asckey, bool press);

#endif
