/*
  Hatari
*/

extern char Keymap_RemapKeyToSTScanCode(unsigned int Key);
extern void Keymap_LoadRemapFile(char *pszFileName);
extern void Keymap_DebounceAllKeys(void);
extern void Keymap_KeyDown(unsigned int sdlkey, unsigned int sdlmod);
extern void Keymap_KeyUp(unsigned int sdlkey, unsigned int sdlmod);
