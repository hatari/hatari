/*
  Hatari - video.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
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

#define	VIDEO_50HZ		50
#define	VIDEO_60HZ		60
#define	VIDEO_71HZ		71

/* Scan lines per frame */
#define SCANLINES_PER_FRAME_50HZ 313    /* Number of scan lines per frame in 50 Hz */
#define SCANLINES_PER_FRAME_60HZ 263    /* Number of scan lines per frame in 60 Hz */
#define SCANLINES_PER_FRAME_71HZ 501    /* could also be 500 ? */
#define MAX_SCANLINES_PER_FRAME  SCANLINES_PER_FRAME_71HZ    /* Max. number of scan lines per frame */

/* Cycles per line */
#define CYCLES_PER_LINE_50HZ  512
#define CYCLES_PER_LINE_60HZ  508
#define CYCLES_PER_LINE_71HZ  224

/* Vertical border/display enable/disable:
 * Normal screen starts 63 lines in, top border is 29 lines */
#define VIDEO_START_HBL_50HZ   63	/* Usually the first line of the displayed screen in 50 Hz */
#define VIDEO_START_HBL_60HZ   34	/* The first line of the displayed screen in 60 Hz */
#define VIDEO_START_HBL_71HZ   34	/* FIXME: 34 is not verified */
#define FIRST_VISIBLE_HBL_50HZ  34	/* At this line we start rendering our screen in 50 Hz */
#define FIRST_VISIBLE_HBL_60HZ  (34-29)	/* At this line we start rendering our screen in 60 Hz (29 = 63-34) */
#define FIRST_VISIBLE_HBL_71HZ  34	/* FIXME: 34 is not verified */

#define VIDEO_HEIGHT_HBL_COLOR  200	/* This is usually the height of the screen */
#define VIDEO_HEIGHT_HBL_MONO   400

#define VIDEO_HEIGHT_BOTTOM_50HZ 47	/* number of lines in a 50 Hz bottom border */
#define VIDEO_HEIGHT_BOTTOM_60HZ 29	/* number of lines in a 60 Hz bottom border */

#define VIDEO_END_HBL_50HZ	( VIDEO_START_HBL_50HZ + VIDEO_HEIGHT_HBL_COLOR )	/* 263 */
#define VIDEO_END_HBL_60HZ	( VIDEO_START_HBL_60HZ + VIDEO_HEIGHT_HBL_COLOR )	/* 234 */
#define VIDEO_END_HBL_71HZ	( VIDEO_START_HBL_71HZ + VIDEO_HEIGHT_HBL_MONO )	/* 434 */

#define LINE_REMOVE_TOP_CYCLE_STF	504	/* switch to 60 Hz on line 33 should not occur after cycle 504 to remove top border */
						/* switch to 50 Hz should occur after cycle 504 on line 33 */
#define LINE_REMOVE_BOTTOM_CYCLE_STF	504	/* same value than top border, but on line 262 (50 Hz) or 233 (60 Hz) */

#define LINE_REMOVE_TOP_CYCLE_STE	500	/* on STE, switch can occur 4 cycles earlier than STF */
#define LINE_REMOVE_BOTTOM_CYCLE_STE	500


/* Values for VerticalOverscan */
#define	V_OVERSCAN_NONE			0x00
#define	V_OVERSCAN_NO_TOP		0x01
#define	V_OVERSCAN_NO_BOTTOM_50		0x02
#define	V_OVERSCAN_NO_BOTTOM_60		0x04
#define	V_OVERSCAN_BOTTOM_SHORT_50	0x08
#define	V_OVERSCAN_BOTTOM_NO_DE		0x10


#define LINE_START_CYCLE_50	56
#define LINE_START_CYCLE_60	52
#define LINE_START_CYCLE_71	0
#define LINE_END_CYCLE_50	376		/* LINE_START_CYCLE_50 + 320 */
#define LINE_END_CYCLE_60	372		/* LINE_START_CYCLE_60 + 320 */
#define LINE_END_CYCLE_71	160
#define LINE_END_CYCLE_NO_RIGHT	460		/* 372 + 44*2 */
#define LINE_END_CYCLE_50_2	(LINE_END_CYCLE_50+44*2)	/* 464, used in enchanted lands */
#define LINE_END_CYCLE_FULL	512				/* used in enchanted lands */
#define LINE_LEFT_STAB_LOW	16	/* remove left + med res stab using hi/med/lo switches */
#define LINE_SCROLL_13_CYCLE_50	20	/* 13 pixels right "hardware" scrolling */
#define LINE_SCROLL_9_CYCLE_50	24	/*  9 pixels right "hardware" scrolling */
#define LINE_SCROLL_5_CYCLE_50	28	/*  5 pixels right "hardware" scrolling */
#define LINE_SCROLL_1_CYCLE_50	32	/*  1 pixels right "hardware" scrolling */
#define LINE_LEFT_MED_CYCLE_1	20	/* med res overscan, shifts display by 0 byte */
#define LINE_LEFT_MED_CYCLE_2	28	/* med res overscan, shifts display by 2 bytes */
#define	LINE_EMPTY_CYCLE_71_STF	28	/* on STF switch to hi/lo will create an empty line */
#define	LINE_EMPTY_CYCLE_71_STE	(28+4)	/* on STE switch to hi/lo will create an empty line */

/* Bytes for opened left and right border: */
#define BORDERBYTES_NORMAL	160	/* size of a "normal" line */
#define BORDERBYTES_LEFT	26
#define BORDERBYTES_LEFT_2_STE	20
#define BORDERBYTES_RIGHT	44
#define BORDERBYTES_RIGHT_FULL	22

/* Legacy defines: */
#define CYCLES_PER_FRAME    (nScanlinesPerFrame*nCyclesPerLine)  /* Cycles per VBL @ 50fps = 160256 */


#define VBL_VIDEO_CYCLE_OFFSET_STF	64			/* value of cycle counter when VBL signal is sent */
#define VBL_VIDEO_CYCLE_OFFSET_STE	(64+4)			/* 4 cycles difference on STE */

#define HBL_VIDEO_CYCLE_OFFSET		0			/* cycles after end of current line (ie on every 512 cycles in 50 Hz) */
#define TIMERB_VIDEO_CYCLE_OFFSET	24			/* cycles after last displayed pixels : 376+24 in 50 Hz or 372+24 in 60 Hz */

/* This is when ff8205/07/09 are reloaded with the content of ff8201/03 : on line 310 cycle 48/52 in 50 Hz and on line 260 cycle 48/52 in 60 Hz */
/* (values were measured on real STF/STE) */
#define RESTART_VIDEO_COUNTER_LINE_50HZ		( SCANLINES_PER_FRAME_50HZ-3 )
#define RESTART_VIDEO_COUNTER_LINE_60HZ		( SCANLINES_PER_FRAME_60HZ-3 )
#define RESTART_VIDEO_COUNTER_CYCLE_STF		( 48 )
#define RESTART_VIDEO_COUNTER_CYCLE_STE		( 48 + 4 )	/* 4 cycles later than STF */

/* anything above 4 uses automatic frameskip */
#define AUTO_FRAMESKIP_LIMIT	5

extern int STRes;
extern int TTRes;
extern int nFrameSkips;
extern bool bUseHighRes;
extern int nVBLs;
extern int nHBL;
extern int nStartHBL;
extern int nEndHBL;
extern int VerticalOverscan;
extern Uint16 HBLPalettes[HBL_PALETTE_LINES];
extern Uint16 *pHBLPalettes;
extern Uint32 HBLPaletteMasks[HBL_PALETTE_MASKS];
extern Uint32 *pHBLPaletteMasks;
extern Uint32 VideoBase;
extern int nScreenRefreshRate;

extern int nScanlinesPerFrame;
extern int nCyclesPerLine;
extern int TTSpecialVideoMode;
extern int LineTimerBCycle;
extern int TimerBEventCountCycleStart;

#define HBL_JITTER_ARRAY_SIZE 5
extern int HblJitterIndex;
extern const int HblJitterArray[HBL_JITTER_ARRAY_SIZE];
extern const int HblJitterArrayPending[HBL_JITTER_ARRAY_SIZE];
#define VBL_JITTER_ARRAY_SIZE 5
extern int VblJitterIndex;
extern const int VblJitterArray[VBL_JITTER_ARRAY_SIZE];
extern const int VblJitterArrayPending[VBL_JITTER_ARRAY_SIZE];


/*--------------------------------------------------------------*/
/* Functions prototypes						*/
/*--------------------------------------------------------------*/

extern void	Video_MemorySnapShot_Capture(bool bSave);

extern void 	Video_Reset(void);
extern void	Video_Reset_Glue(void);

extern void	Video_InitTimings(void);
extern void	Video_SetTimings( MACHINETYPE MachineType , VIDEOTIMINGMODE Mode );
extern const char* Video_GetTimings_Name ( void );

extern void	Video_ConvertPosition( int FrameCycles , int *pHBL , int *pLineCycles );
extern void	Video_GetPosition( int *pFrameCycles , int *pHBL , int *pLineCycles );
extern void	Video_GetPosition_OnWriteAccess( int *pFrameCycles , int *pHBL , int *pLineCycles );
extern void	Video_GetPosition_OnReadAccess( int *pFrameCycles , int *pHBL , int *pLineCycles );

extern void 	Video_Sync_WriteByte(void);

extern int	Video_TimerB_GetPos( int LineNumber );

extern void	Video_InterruptHandler_HBL(void);
extern void	Video_InterruptHandler_EndLine(void);

extern void	Video_SetScreenRasters(void);
extern void	Video_GetTTRes(int *width, int *height, int *bpp);
extern bool	Video_RenderTTScreen(void);

extern void	Video_AddInterruptTimerB ( int LineVideo , int CycleVideo , int Pos );

extern void	Video_StartInterrupts ( int PendingCyclesOver );
extern void	Video_InterruptHandler_VBL(void);

extern void Video_ScreenBase_WriteByte(void);
extern void Video_ScreenCounter_ReadByte(void);
extern void Video_ScreenCounter_WriteByte(void);
extern void Video_Sync_ReadByte(void);
extern void Video_BaseLow_ReadByte(void);
extern void Video_LineWidth_ReadByte(void);
extern void Video_Res_ReadByte(void);
extern void Video_HorScroll_Read(void);
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
extern void Video_Color0_ReadWord(void);
extern void Video_Color1_ReadWord(void);
extern void Video_Color2_ReadWord(void);
extern void Video_Color3_ReadWord(void);
extern void Video_Color4_ReadWord(void);
extern void Video_Color5_ReadWord(void);
extern void Video_Color6_ReadWord(void);
extern void Video_Color7_ReadWord(void);
extern void Video_Color8_ReadWord(void);
extern void Video_Color9_ReadWord(void);
extern void Video_Color10_ReadWord(void);
extern void Video_Color11_ReadWord(void);
extern void Video_Color12_ReadWord(void);
extern void Video_Color13_ReadWord(void);
extern void Video_Color14_ReadWord(void);
extern void Video_Color15_ReadWord(void);
extern void Video_Res_WriteByte(void);
extern void Video_HorScroll_Read_8264(void);
extern void Video_HorScroll_Read_8265(void);
extern void Video_HorScroll_Write_8264(void);
extern void Video_HorScroll_Write_8265(void);
extern void Video_HorScroll_Write(void);
extern void Video_TTShiftMode_WriteWord(void);
extern void Video_TTColorRegs_Write(void);
extern void Video_TTColorRegs_STRegWrite(void);

extern void Video_Info(FILE *fp, Uint32 dummy);

#endif  /* HATARI_VIDEO_H */
