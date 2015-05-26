/*
  Hatari - blitter.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Blitter emulation.
*/

#ifndef BLITTER_H
#define BLITTER_H

extern void Blitter_Halftone00_ReadWord(void);
extern void Blitter_Halftone01_ReadWord(void);
extern void Blitter_Halftone02_ReadWord(void);
extern void Blitter_Halftone03_ReadWord(void);
extern void Blitter_Halftone04_ReadWord(void);
extern void Blitter_Halftone05_ReadWord(void);
extern void Blitter_Halftone06_ReadWord(void);
extern void Blitter_Halftone07_ReadWord(void);
extern void Blitter_Halftone08_ReadWord(void);
extern void Blitter_Halftone09_ReadWord(void);
extern void Blitter_Halftone10_ReadWord(void);
extern void Blitter_Halftone11_ReadWord(void);
extern void Blitter_Halftone12_ReadWord(void);
extern void Blitter_Halftone13_ReadWord(void);
extern void Blitter_Halftone14_ReadWord(void);
extern void Blitter_Halftone15_ReadWord(void);
extern void Blitter_SourceXInc_ReadWord(void);
extern void Blitter_SourceYInc_ReadWord(void);
extern void Blitter_SourceAddr_ReadLong(void);
extern void Blitter_Endmask1_ReadWord(void);
extern void Blitter_Endmask2_ReadWord(void);
extern void Blitter_Endmask3_ReadWord(void);
extern void Blitter_DestXInc_ReadWord(void);
extern void Blitter_DestYInc_ReadWord(void);
extern void Blitter_DestAddr_ReadLong(void);
extern void Blitter_WordsPerLine_ReadWord(void);
extern void Blitter_LinesPerBitblock_ReadWord(void);
extern void Blitter_HalftoneOp_ReadByte(void);
extern void Blitter_LogOp_ReadByte(void);
extern void Blitter_Control_ReadByte(void);
extern void Blitter_Skew_ReadByte(void);

extern void Blitter_Halftone00_WriteWord(void);
extern void Blitter_Halftone01_WriteWord(void);
extern void Blitter_Halftone02_WriteWord(void);
extern void Blitter_Halftone03_WriteWord(void);
extern void Blitter_Halftone04_WriteWord(void);
extern void Blitter_Halftone05_WriteWord(void);
extern void Blitter_Halftone06_WriteWord(void);
extern void Blitter_Halftone07_WriteWord(void);
extern void Blitter_Halftone08_WriteWord(void);
extern void Blitter_Halftone09_WriteWord(void);
extern void Blitter_Halftone10_WriteWord(void);
extern void Blitter_Halftone11_WriteWord(void);
extern void Blitter_Halftone12_WriteWord(void);
extern void Blitter_Halftone13_WriteWord(void);
extern void Blitter_Halftone14_WriteWord(void);
extern void Blitter_Halftone15_WriteWord(void);
extern void Blitter_SourceXInc_WriteWord(void);
extern void Blitter_SourceYInc_WriteWord(void);
extern void Blitter_SourceAddr_WriteLong(void);
extern void Blitter_Endmask1_WriteWord(void);
extern void Blitter_Endmask2_WriteWord(void);
extern void Blitter_Endmask3_WriteWord(void);
extern void Blitter_DestXInc_WriteWord(void);
extern void Blitter_DestYInc_WriteWord(void);
extern void Blitter_DestAddr_WriteLong(void);
extern void Blitter_WordsPerLine_WriteWord(void);
extern void Blitter_LinesPerBitblock_WriteWord(void);
extern void Blitter_HalftoneOp_WriteByte(void);
extern void Blitter_LogOp_WriteByte(void);
extern void Blitter_Control_WriteByte(void);
extern void Blitter_Skew_WriteByte(void);

extern void Blitter_MemorySnapShot_Capture(bool bSave);
extern void Blitter_InterruptHandler(void);
extern void Blitter_Info(FILE *fp, Uint32 arg);

#endif /* BLITTER_H */
