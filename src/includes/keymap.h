/*
  Hatari - keymap.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_KEYMAP_H
#define HATARI_KEYMAP_H

#include <SDL_keyboard.h>

extern void Keymap_Init(void);
extern char Keymap_RemapKeyToSTScanCode(SDL_keysym* pKeySym);
extern void Keymap_LoadRemapFile(char *pszFileName);
extern void Keymap_DebounceAllKeys(void);
extern void Keymap_KeyDown(SDL_keysym *sdlkey);
extern void Keymap_KeyUp(SDL_keysym *sdlkey);

#endif
