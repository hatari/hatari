/*
  Hatari - video.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VIDEO_H
#define HATARI_VIDEO_H

/*
  All the following processor timings are based on a bog standard 8MHz 68000 as
  found in all standard STs:

  Clock cycles per line (50Hz)      : 512
  NOPs per scan line (50Hz)         : 128
  Scan lines per VBL (50Hz)         : 313 (64 at top,200 screen,49 bottom)

  Clock cycles per line (60Hz)      : 508
  NOPs per scan line (60Hz)         : 127
  Scan lines per VBL (60Hz)         : 263

  Clock cycles per VBL (50Hz)       : 160256
  NOPs per VBL (50Hz)               : 40064

  Pixels per clock cycle (low res)  : 1
  Pixels per clock cycle (med res)  : 2
  Pixels per clock cycle (high res) : 4
  Pixels per NOP (low res)          : 4
  Pixels per NOP (med res)          : 8
  Pixels per NOP (high res)         : 16
*/

/* Scan lines per frame */
#define SCANLINES_PER_FRAME_50HZ 313    /* Number of scan lines per frame in 50 Hz */
#define SCANLINES_PER_FRAME_60HZ 263    /* Number of scan lines per frame in 60 Hz */
#define MAX_SCANLINES_PER_FRAME  313    /* Max. number of scan lines per frame */

/* Cycles per line */
#define CYCLES_PER_LINE_50HZ  512
#define CYCLES_PER_LINE_60HZ  508

/* Vertical border/display enable/disable:
 * Normal screen starts 63 lines in, top border is 29 lines */
#define SCREEN_START_HBL_50HZ   63      /* Usually the first line of the displayed screen in 50 Hz */
#define SCREEN_START_HBL_60HZ   34      /* The first line of the displayed screen in 60 Hz */
#define FIRST_VISIBLE_HBL_50HZ  34      /* At this line we start rendering our screen in 50 Hz */
#define FIRST_VISIBLE_HBL_60HZ  (34-29) /* At this line we start rendering our screen in 60 Hz (29 = 63-34) */

#define SCREEN_HEIGHT_HBL  200          /* This is usually the height of the screen */

/* FIXME: SCREEN_START_CYCLE should rather be 52 or so, but this breaks a lot of other things at the moment... */
#define SCREEN_START_CYCLE  56          /* Cycle first normal pixel appears on */

/* Bytes for opened left and right border: */
#define BORDERBYTES_LEFT  26
#define BORDERBYTES_RIGHT 44

/* Legacy defines: */
#define CYCLES_PER_FRAME    (nScanlinesPerFrame*nCyclesPerLine)  /* Cycles per VBL @ 50fps = 160256 */


extern BOOL bUseHighRes;
extern int nVBLs,nHBL;
extern int nStartHBL, nEndHBL;
extern int OverscanMode;
extern Uint16 HBLPalettes[];
extern Uint16 *pHBLPalettes;
extern Uint32 HBLPaletteMasks[];
extern Uint32 *pHBLPaletteMasks;
extern Uint32 VideoBase;
extern int nScreenRefreshRate;

extern int nScanlinesPerFrame;
extern int nCyclesPerLine;


extern void Video_Reset(void);
extern void Video_MemorySnapShot_Capture(BOOL bSave);
extern void Video_GetTTRes(int *width, int *height, int *bpp);
extern void Video_StartInterrupts(void);
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
extern void Video_HorScroll_Read(void);

extern void Video_ScreenBaseSTE_WriteByte(void);
extern void Video_ScreenCounter_WriteByte(void);
extern void Video_Sync_WriteByte(void);
extern void Video_LineWidth_WriteByte(void);
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
extern void Video_HorScroll_Write(void);
extern void Video_TTShiftMode_WriteWord(void);
extern void Video_TTColorRegs_WriteWord(void);
extern void Video_TTColorSTRegs_WriteWord(void);

#endif  /* HATARI_VIDEO_H */
