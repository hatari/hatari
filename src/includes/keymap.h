/*
  Hatari
*/

#include <SDL_keyboard.h>

extern char Keymap_RemapKeyToSTScanCode(unsigned int Key);
extern void Keymap_LoadRemapFile(char *pszFileName);
extern void Keymap_DebounceAllKeys(void);
extern void Keymap_KeyDown(SDL_keysym *sdlkey);
extern void Keymap_KeyUp(SDL_keysym *sdlkey);
extern void Keymap_FromScancodes();
