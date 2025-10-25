/*
  Hatari - conv_gen.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

/* Last used vw/vh/vbpp for ConvGen_Convert */
extern int ConvertW, ConvertH, ConvertBPP, ConvertNextLine;

void ConvGen_RemapPalette(void);
void ConvGen_SetPaletteColor(uint8_t idx, uint8_t red, uint8_t green, uint8_t blue);
void ConvGen_GetPaletteColor(int idx, uint8_t *r, uint8_t *g, uint8_t *b);
void ConvGen_MemorySnapShot_Capture(bool bSave);

void ConvGen_Convert(uint32_t vaddr, void *fvram, int vw, int vh,
                     int vbpp, int nextline, int hscroll,
                     int leftBorderSize, int rightBorderSize,
                     int upperBorderSize, int lowerBorderSize);

bool ConvGen_Draw(uint32_t vaddr, int vw, int vh, int vbpp, int nextline,
                  int leftBorderSize, int rightBorderSize,
                  int upperBorderSize, int lowerBorderSize);
void ConvGen_SetSize(int width, int height, bool bForceChange);
bool ConvGen_UseGenConvScreen(void);
