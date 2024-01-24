/*
  Hatari - screenConvert.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

/* Last used vw/vh/vbpp for Screen_GenConvert */
extern int ConvertW, ConvertH, ConvertBPP, ConvertNextLine;

void Screen_RemapPalette(void);
void Screen_SetPaletteColor(Uint8 idx, Uint8 red, Uint8 green, Uint8 blue);
SDL_Color Screen_GetPaletteColor(Uint8 idx);
void ScreenConv_MemorySnapShot_Capture(bool bSave);

void Screen_GenConvert(uint32_t vaddr, void *fvram, int vw, int vh,
                       int vbpp, int nextline, int hscroll,
                       int leftBorderSize, int rightBorderSize,
                       int upperBorderSize, int lowerBorderSize);

bool Screen_GenDraw(uint32_t vaddr, int vw, int vh, int vbpp, int nextline,
                    int leftBorderSize, int rightBorderSize,
                    int upperBorderSize, int lowerBorderSize);
