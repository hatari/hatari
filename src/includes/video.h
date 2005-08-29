/*
  Hatari - video.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VIDEO_H
#define HATARI_VIDEO_H

#include <SDL_types.h>

extern BOOL bUseHighRes;
extern int nVBLs,nHBL;
extern int nStartHBL, nEndHBL;
extern int OverscanMode;
extern Uint16 HBLPalettes[(NUM_VISIBLE_LINES+1)*16];
extern Uint16 *pHBLPalettes;
extern Uint32 HBLPaletteMasks[NUM_VISIBLE_LINES+1];
extern Uint32 *pHBLPaletteMasks;
extern Uint32 VideoBase;
extern int nScreenRefreshRate;

extern void Video_Reset(void);
extern void Video_MemorySnapShot_Capture(BOOL bSave);
extern void Video_InterruptHandler_VBL(void);
extern void Video_InterruptHandler_EndLine(void);
extern void Video_InterruptHandler_HBL(void);
extern void Video_SetScreenRasters(void);

extern void Video_ScreenCounterHigh_ReadByte(void);
extern void Video_ScreenCounterMed_ReadByte(void);
extern void Video_ScreenCounterLow_ReadByte(void);
extern void Video_Sync_ReadByte(void);
extern void Video_BaseLow_ReadByte(void);
extern void Video_LineWidth_ReadByte(void);
extern void Video_ShifterMode_ReadByte(void);

extern void Video_ScreenBaseSTE_WriteByte(void);
extern void Video_ScreenCounter_WriteByte(void);
extern void Video_Sync_WriteByte(void);
extern void Video_Color0_WriteWord(void);
extern void Video_Color1_WriteWord(void);
extern void Video_Color2_WriteWord(void);
extern void Video_Color3_WriteWord(void);
extern void Video_Color4_WriteWord(void);
extern void Video_Color5_WriteWord(void);
extern void Video_Color6_WriteWord(void);
extern void Video_Color7_WriteWord(void);
extern void Video_Color8_WriteWord(void);
extern void Video_Color9_WriteWord(void);
extern void Video_Color10_WriteWord(void);
extern void Video_Color11_WriteWord(void);
extern void Video_Color12_WriteWord(void);
extern void Video_Color13_WriteWord(void);
extern void Video_Color14_WriteWord(void);
extern void Video_Color15_WriteWord(void);
extern void Video_ShifterMode_WriteByte(void);

#endif  /* HATARI_VIDEO_H */
