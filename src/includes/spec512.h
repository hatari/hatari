/*
  Hatari - spec512.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SPEC512_H
#define HATARI_SPEC512_H

extern bool Spec512_IsImage(void);
extern void Spec512_StartVBL(void);
extern void Spec512_StoreCyclePalette(Uint16 col, Uint32 addr);
extern void Spec512_StartFrame(void);
extern void Spec512_ScanWholeLine(void);
extern void Spec512_StartScanLine(void);
extern void Spec512_EndScanLine(void);
extern void Spec512_UpdatePaletteSpan(void);

#endif  /* HATARI_SPEC512_H */
