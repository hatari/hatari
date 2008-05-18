/*
  Hatari - blitter.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Blitter emulation.
*/

#ifndef BLITTER_H
#define BLITTER_H

extern void Blitter_SourceAddr_ReadLong(void);
extern void Blitter_Endmask1_ReadWord(void);
extern void Blitter_Endmask2_ReadWord(void);
extern void Blitter_Endmask3_ReadWord(void);
extern void Blitter_DestAddr_ReadLong(void);
extern void Blitter_WordsPerLine_ReadWord(void);
extern void Blitter_LinesPerBitblock_ReadWord(void);
extern void Blitter_HalftoneOp_ReadByte(void);
extern void Blitter_LogOp_ReadByte(void);
extern void Blitter_Control_ReadByte(void);
extern void Blitter_Skew_ReadByte(void);

extern void Blitter_SourceAddr_WriteLong(void);
extern void Blitter_Endmask1_WriteWord(void);
extern void Blitter_Endmask2_WriteWord(void);
extern void Blitter_Endmask3_WriteWord(void);
extern void Blitter_DestAddr_WriteLong(void);
extern void Blitter_WordsPerLine_WriteWord(void);
extern void Blitter_LinesPerBitblock_WriteWord(void);
extern void Blitter_HalftoneOp_WriteByte(void);
extern void Blitter_LogOp_WriteByte(void);
extern void Blitter_Control_WriteByte(void);
extern void Blitter_Skew_WriteByte(void);

extern void Blitter_MemorySnapShot_Capture(bool bSave);
extern void Blitter_InterruptHandler(void);

#endif /* BLITTER_H */
