/**
 * Hatari - Blitter emulation.
 * This file has been taken from STonX.
 *
 * Original information text follows:
 *
 *
 * This file is part of STonX, the Atari ST Emulator for Unix/X
 * ============================================================
 * STonX is free software and comes with NO WARRANTY - read the file
 * COPYING for details
 */

#ifndef BLITTER_H
#define BLITTER_H

extern void Do_Blit(void);

extern void Blitter_Endmask1_ReadWord(void);
extern void Blitter_Endmask2_ReadWord(void);
extern void Blitter_Endmask3_ReadWord(void);
extern void Blitter_DestAddr_ReadLong(void);
extern void Blitter_WordsPerLine_ReadWord(void);
extern void Blitter_LinesPerBitblock_ReadWord(void);
extern void Blitter_HalftoneOp_ReadByte(void);
extern void Blitter_LogOp_ReadByte(void);
extern void Blitter_LineNum_ReadByte(void);
extern void Blitter_Skew_ReadByte(void);

extern void Blitter_Endmask1_WriteWord(void);
extern void Blitter_Endmask2_WriteWord(void);
extern void Blitter_Endmask3_WriteWord(void);
extern void Blitter_DestAddr_WriteLong(void);
extern void Blitter_WordsPerLine_WriteWord(void);
extern void Blitter_LinesPerBitblock_WriteWord(void);
extern void Blitter_HalftoneOp_WriteByte(void);
extern void Blitter_LogOp_WriteByte(void);
extern void Blitter_LineNum_WriteByte(void);
extern void Blitter_Skew_WriteByte(void);

extern void Blitter_MemorySnapShot_Capture(BOOL bSave);

#endif /* BLITTER_H */
