/*
  Hatari - spec512.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SPEC512_H
#define HATARI_SPEC512_H

#define MAX_CYCLEPALETTES_PERLINE  (512/4)  /* As 68000 clock multiple of 4 this mean we can only write to the palette this many time per scanline */

/* Store writes to palette by cycles per scan line, colour and index in ST */
typedef struct
{
  int LineCycles;                   /* Number of cycles into line(MUST be div by 4) */
  Uint16 Colour;                    /* ST Colour value */
  unsigned short int Index;         /* Index into ST palette (0...15) */
} CYCLEPALETTE;

extern BOOL Spec512_IsImage(void);
extern void Spec512_StartVBL(void);
extern void Spec512_StoreCyclePalette(Uint16 col, Uint32 addr);
extern void Spec512_StartFrame(void);
extern void Spec512_ScanWholeLine(void);
extern void Spec512_StartScanLine(void);
extern void Spec512_EndScanLine(void);
extern void Spec512_UpdatePaletteSpan(void);

#endif  /* HATARI_SPEC512_H */
