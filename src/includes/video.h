/*
  Hatari - video.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VIDEO_H
#define HATARI_VIDEO_H

#include <SDL_types.h>

#define BORDERMASK_NONE    0x0000      /* Borders masks */
#define BORDERMASK_TOP     0x0001
#define BORDERMASK_BOTTOM  0x0002
#define BORDERMASK_LEFT    0x0004
#define BORDERMASK_RIGHT   0x0008

extern long VideoAddress;
extern unsigned char VideoSyncByte,VideoShifterByte;
extern BOOL bUseHighRes;
extern int nVBLs,nHBL;
extern int nStartHBL,nEndHBL;
extern int OverscanMode;
extern unsigned short int HBLPalettes[(NUM_VISIBLE_LINES+1)*16];
extern unsigned long HBLPaletteMasks[NUM_VISIBLE_LINES+1];
extern unsigned short int *pHBLPalettes;
extern unsigned long *pHBLPaletteMasks;
extern unsigned long VideoBase;
extern unsigned long VideoRaster;
extern int VBLCounter;
extern int nScreenRefreshRate;

extern void Video_Reset(void);
extern void Video_MemorySnapShot_Capture(BOOL bSave);
extern void Video_ClearOnVBL(void);
extern void Video_CalculateAddress(void);
extern unsigned long Video_ReadAddress(void);
extern void Video_InterruptHandler_VBL(void);
extern void Video_InterruptHandler_EndLine(void);
extern void Video_InterruptHandler_HBL(void);
extern void Video_WriteToShifter(Uint8 Byte);
extern void Video_WriteToSync(Uint8 Byte);
extern void Video_StartHBL(void);
extern void Video_CopyVDIScreen(void);
extern void Video_EndHBL(void);
extern void Video_SetScreenRasters(void);
extern void Video_SetHBLPaletteMaskPointers(void);

#endif  /* HATARI_VIDEO_H */
