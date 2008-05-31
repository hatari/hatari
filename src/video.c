/*
  Hatari - video.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Video hardware handling. This code handling all to do with the video chip.
  So, we handle VBLs, HBLs, copying the ST screen to a buffer to simulate the
  TV raster trace, border removal, palette changes per HBL, the 'video address
  pointer' etc...
*/

/* 2007/03/xx	[NP]	Support for cycle precise border removal / hardware scrolling by using	*/
/*			Cycles_GetCounterOnWriteAccess (support left/right border and lines with*/
/*			length of +26, +2, -2, +44, -106 bytes).				*/
/*			Add support for 'Enchanted Lands' second removal of right border.	*/
/*			More precise support for reading video counter $ff8205/07/09.		*/
/* 2007/04/14	[NP]	Precise reloading of $ff8201/03 into $ff8205/07/09 at line 310 on cycle	*/
/*			RESTART_VIDEO_COUNTER_CYCLE (ULM DSOTS Demo).				*/
/* 2007/04/16	[NP]	Better Video_CalculateAddress. We must substract a "magic" 12 cycles to	*/
/*			Cycles_GetCounterOnReadAccess(CYCLES_COUNTER_VIDEO) to get a correct	*/
/*			value (No Cooper's video synchro protection is finaly OK :) ).		*/
/* 2007/04/17	[NP]	- Switch to 60 Hz to remove top border on line 33 should occur before	*/
/*			LINE_REMOVE_TOP_CYCLE (a few cycles before the HBL)			*/
/* 2007/04/23	[NP]	- Slight change in Video_StoreResolution to ignore hi res if the line	*/
/*			has left/right border removed -> assume of lo res line.			*/
/*			- Handle simultaneous removal of right border and bottom border with	*/
/*			the same long switch to 60 Hz (Sync Screen in SNY II).			*/
/* 2007/05/06	[NP]	More precise tests for top border's removal.				*/
/* 2007/05/11	[NP]	Add support for mid res overscan (No Cooper Greetings).			*/
/* 2007/05/12	[NP]	- LastCycleSync50 and LastCycleSync60 for better top border's removal	*/
/*			in Video_EndHBL.							*/
/*			- Use VideoOffset in Video_CopyScreenLineColor to handle missing planes	*/
/*			depending on line (mid/lo and borders).					*/
/* 2007/09/25	[NP]	Replace printf by calls to HATARI_TRACE.				*/
/* 2007/10/02	[NP]	Use the new int.c to add interrupts with INT_CPU_CYCLE / INT_MFP_CYCLE. */
/* 2007/10/23	[NP]	Add support for 0 byte line (60/50 switch at cycle 56). Allow 5 lines	*/
/*			hardscroll (e.g. SHFORSTV.EXE by Paulo Simmoes).			*/
/* 2007/10/31	[NP]	Use BORDERMASK_LEFT_OFF_MID when left border is removed with hi/med	*/
/*			switch (ST CNX in PYM).							*/
/* 2007/11/02	[NP]	Add support for 4 pixel hardware scrolling ("Let's Do The Twist" by	*/
/*			ST CNX in Punish Your Machine).						*/
/* 2007/11/05	[NP]	Depending on the position of the mid res switch, the planes will be	*/
/*			shifted when doing midres overscan (Best Part Of the Creation in PYM	*/
/*			or No Cooper Greetings).						*/
/* 2007/11/30	[NP]	A hi/mid switch to remove the left border can be either	used to initiate*/
/*			a right hardware scrolling in low res (St Cnx) or a complete mid res	*/
/*			overscan line (Dragonnels Reset Part).					*/
/*			Use bit 0-15, 16-19 and 20-23 in ScreenBorderMask[] to track border	*/
/*			trick, STF hardware scrolling and plane shifting.			*/
/* 2007/12/22	[NP]	Very precise values for VBL_VIDEO_CYCLE_OFFSET, HBL_VIDEO_CYCLE_OFFSET	*/
/*			TIMERB_VIDEO_CYCLE_OFFSET and RESTART_VIDEO_COUNTER_CYCLE. These values	*/
/*			were calculated using sttiming.s on a real STF and should give some very*/
/*			accurate results (also uses 56 cycles instead of 44 to process an	*/
/*			HBL/VBL/MFP exception).							*/
/* 2007/12/29	[NP]	Better support for starting line 2 bytes earlier (if the line starts in	*/
/*			60 Hz and goes back to 50 Hz later), when combined with top border	*/
/*			removal (Mindbomb Demo - D.I. No Shit).					*/
/* 2007/12/30	[NP]	Slight improvement of VideoAdress in Video_CalculateAddress when reading*/
/*			during the top border.							*/
/*			Correct the case where removing top border on line 33 could also be	*/
/*			interpreted as a right border removal (which is not possible since the	*/
/*			display is still off at that point).					*/
/* 2008/01/03	[NP]	Better handling of nStartHBL and nEndHBL when switching freq from	*/
/*			50 to 60 Hz. Allows emulation of a "short" 50 Hz screen of 171 lines	*/
/*			and a more precise removal of bottom border in 50 and 60 Hz.		*/
/* 2008/01/04	[NP]	More generic detection for removing 2 bytes to the right of the line	*/
/*			when switching from 60 to 50 Hz (works even with a big number of cycles	*/
/*			between the freq changes) (Phaleon's Menus).				*/
/* 2008/01/06	[NP]	More generic detection for stopping the display in the middle of a line	*/
/*			with a hi / lo res switch (-106 bytes per line). Although switch to	*/
/*			hi res should occur at cycle 160, some demos use 164 (Phaleon's Menus).	*/
/* 2008/01/06	[NP]	Better bottom border's removal in 50 Hz : switch to 60 Hz must occur	*/
/*			before cycle LINE_REMOVE_BOTTOM_CYCLE on line 263 and switch back to 50	*/
/*			Hz must occur after LINE_REMOVE_BOTTOM_CYCLE on line 263 (this means	*/
/*			we can already be in 50 Hz when Video_EndHBL is called and still remove	*/
/*			the bottom border). This is similar to the tests used to remove the	*/
/*			top border.								*/
/* 2008/01/12	[NP]	In Video_SetHBLPaletteMaskPointers, consider that if a color's change	*/
/*			occurs after cycle LINE_END_CYCLE_NO_RIGHT, then it's related to the	*/
/*			next line.								*/
/*			FIXME : it would be better to handle all color changes through spec512.c*/
/*			and drop the 16 colors palette per line.				*/
/*			FIXME : we should use Cycles_GetCounterOnWriteAccess, but it doesn't	*/
/*			support multiple accesses like move.l or movem.				*/
/* 2008/01/12	[NP]	Handle 60 Hz switch during the active display of the last line to remove*/
/*			the bottom border : this should also shorten line by 2 bytes (F.N.I.L.	*/
/*			Demo by TNT).								*/
/* 2008/01/15	[NP]	Don't do 'left+2' if switch back to 50 Hz occurs when line is not active*/
/*			(after cycle LINE_END_CYCLE_60) (XXX International Demos).		*/
/* 2008/01/31	[NP]	Improve left border detection : allow switch to low res on cycle <= 28  */
/*			instead of <= 20 (Vodka Demo Main Menu).				*/
/* 2008/02/02	[NP]	Added 0 byte line detection when switching hi/lo res at position 28	*/
/*			(Lemmings screen in Nostalgic-o-demo).					*/
/* 2008/02/03	[NP]	On STE, write to video counter $ff8205/07/09 should only be applied	*/
/*			immediatly if display has not started for the line (before cycle	*/
/*			LINE_END_CYCLE_50). If write occurs after, the change to pVideoRaster	*/
/*			should be delayed to the end of the line, after processing the current	*/
/*			line with Video_CopyScreenLineColor (Stardust Tunnel Demo).		*/
/* 2008/02/04	[NP]	The problem is similar when writing to hwscroll $ff8264, we must delay	*/
/*			the change until the end of the line if display was already started	*/
/*			(Mindrewind by Reservoir Gods).						*/
/* 2008/02/06	[NP]	On STE, when left/right borders are off and hwscroll > 0, we must read	*/
/*			6 bytes less than the expected value (E605 by Light).			*/
/* 2008/02/17	[NP]	In Video_CopyScreenLine, LineWidth*2 bytes should be added after	*/
/*			pNewVideoRaster is copied to pVideoRaster (Braindamage Demo).		*/
/*			When reading a byte at ff8205/07/09, all video address bytes should be	*/
/*			updated in Video_ScreenCounter_ReadByte, not just the byte that was	*/
/*			read. Fix programs that just modify one byte in the video address	*/
/*			counter (e.g. sub #1,$ff8207 in Braindamage Demo).			*/
/* 2008/02/19	[NP]	In Video_CalculateAddress, use pVideoRaster instead of VideoBase to	*/
/*			determine the video address when display is off in the upper part of	*/
/*			the screen (in case ff8205/07/09 were modified on STE).			*/
/* 2008/02/20	[NP]	Better handling in Video_ScreenCounter_WriteByte by changing only one	*/
/*			byte and keeping the other (Braindamage End Part).			*/
/* 2008/03/08	[NP]	Use M68000_INT_VIDEO when calling M68000_Exception().			*/
/* 2008/03/13	[NP]	On STE, LineWidth value in $ff820f is added to the shifter counter just	*/
/*			when display is turned off on a line (when right border is started,	*/
/*			which is usually on cycle 376).						*/
/*			This means a write to $ff820f should be applied immediatly only if it	*/
/*			occurs before cycle LineEndCycle. Else, it is stored in NewLineWidth	*/
/*			and used after Video_CopyScreenLine has processed the current line	*/
/*			(improve the bump mapping part in Pacemaker by Paradox).		*/
/*			LineWidth should be added to pVideoRaster before checking the possible	*/
/*			modification of $ff8205/07/09 in Video_CopyScreenLine.			*/
/* 2008/03/14	[NP]	Rename ScanLineSkip to LineWidth (more consistent with STE docs).	*/
/*			On STE, better support for writing to video counter, line width and	*/
/*			hw scroll. If write to register occurs just at the start of a new line	*/
/*			but before Video_EndHBL (because the move started just before cycle 512)*/
/*			then the new value should not be set immediatly but stored and set	*/
/*			during Video_EndHBL (fix the bump mapping part in Pacemaker by Paradox).*/
/* 2008/03/25	[NP]	On STE, when bSteBorderFlag is true, we should add 16 pixels to the left*/
/*			border, not to the right one (Just Musix 2 Menu by DHS).		*/
/* 2008/03/26	[NP]	Clear the rest of the border when using border tricks left+2, left+8	*/
/*			or right-106 (remove garbage pixels when hatari resolution changes).	*/
/* 2008/03/29	[NP]	Function Video_SetSystemTimings to use different values depending on	*/
/*			the machine type. On STE, top/bottom border removal can occur at cycle	*/
/*			500 instead of 504 on STF.						*/
/* 2008/04/02	[NP]	Correct a rare case in Video_Sync_WriteByte at the end of line 33 :	*/
/*			nStartHBL was set to 33 instead of 64, which gave a wrong address in	*/
/*			Video_CalculateAddress.							*/
/* 2008/04/04	[NP]	The value of RestartVideoCounterCycle is slightly different between	*/
/*			an STF and an STE.							*/
/* 2008/04/05	[NP]	The value of VblVideoCycleOffset is different of 4 cycles between	*/
/*			STF and STE (fix end part in Pacemaker by Paradox).			*/
/* 2008/04/09	[NP]	Preliminary support for lines using different frequencies in the same	*/
/*			screen.	In Video_InterruptHandler_EndLine, if the current freq is 50 Hz,*/
/*			then next int should be scheduled in 512 cycles ; if freq is 60 Hz,	*/
/*			next int should be in 508 cycles (used by timer B event count mode).	*/
/* 2008/04/10	[NP]	Update LineEndCycle after changing freq to 50 or 60 Hz.			*/
/*			Set EndLine interrupt to happen 28 cycles after LineEndCycle. This way	*/
/*			Timer B occurs at cycle 404 in 50 Hz, or cycle 400 in 60 Hz (improve	*/
/*			flickering bottom border in B.I.G. Demo screen 1).			*/
/* 2008/04/12	[NP]	In the case of a 'right-2' line, we should not change the EndLine's int	*/
/*			position when switching back to 50 Hz ; the int should happen at	*/
/*			position LINE_END_CYCLE_60 + 28 (Anomaly Demo main menu).		*/
/* 2008/05/31	[NP]	Ignore consecutives writes of the same value in the freq/res register.	*/
/*			Only the 1st write matters, else this could confuse the code to remove	*/
/*			top/bottom border (fix OSZI.PRG demo by ULM).				*/



const char Video_rcsid[] = "Hatari $Id: video.c,v 1.113 2008-05-31 17:57:33 npomarede Exp $";

#include <SDL_endian.h>

#include "main.h"
#include "configuration.h"
#include "cycles.h"
#include "fdc.h"
#include "int.h"
#include "ikbd.h"
#include "ioMem.h"
#include "keymap.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "printer.h"
#include "screen.h"
#include "shortcut.h"
#include "sound.h"
#include "spec512.h"
#include "stMemory.h"
#include "vdi.h"
#include "video.h"
#include "ymFormat.h"
#include "falcon/videl.h"
#include "falcon/hostscreen.h"


#define BORDERMASK_NONE    0x00                 /* Borders masks */
#define BORDERMASK_LEFT    0x01
#define BORDERMASK_RIGHT   0x02
#define BORDERMASK_MIDDLE  0x04

/* The border's mask allows to keep track of all the border tricks		*/
/* applied to one video line. The masks for all lines are stored in the array	*/
/* ScreenBorderMask[].								*/
/* - bits 0-15 are used to describe the border tricks.				*/
/* - bits 16-19 are used to store the pixels count in case of right hardware	*/
/*   scrolling on STF.								*/
/* - bits 20-23 are used to store the bytes offset to apply for some particular	*/
/*   tricks (for example mid res overscan can shift display by 0 or 2 bytes	*/
/*   depending on when the switch to mid res is done after removing the left	*/
/*   border).									*/

#define BORDERMASK_LEFT_OFF		0x01	/* removal of left border with hi/lo res switch -> +26 bytes */
#define BORDERMASK_LEFT_PLUS_2		0x02	/* line starts earlier in 60 Hz -> +2 bytes */
#define BORDERMASK_STOP_MIDDLE		0x04	/* line ends in hires at cycle 160 -> -106 bytes */
#define BORDERMASK_RIGHT_MINUS_2	0x08	/* line ends earlier in 60 Hz -> -2 bytes */
#define BORDERMASK_RIGHT_OFF		0x10	/* removal of right border -> +44 bytes */
#define BORDERMASK_RIGHT_OFF_FULL	0x20	/* full removal of right border and next left border -> +22 bytes */
#define BORDERMASK_OVERSCAN_MID_RES	0x40	/* some borders were removed and the line is in mid res instead of low res */
#define BORDERMASK_EMPTY_LINE		0x80	/* 60/50 Hz switch prevents the line to start, video counter is not incremented */
#define BORDERMASK_LEFT_OFF_MID		0x100	/* removal of left border with hi/mid res switch -> +26 bytes (for 4 pixels hardware scrolling) */



int STRes = ST_LOW_RES;                         /* current ST resolution */
int TTRes;                                      /* TT shifter resolution mode */

bool bUseSTShifter;                             /* Falcon: whether to use ST palette */
bool bUseHighRes;                               /* Use hi-res (ie Mono monitor) */
int OverscanMode;                               /* OVERSCANMODE_xxxx for current display frame */
Uint16 HBLPalettes[(NUM_VISIBLE_LINES+1)*16];   /* 1x16 colour palette per screen line, +1 line just incase write after line 200 */
Uint16 *pHBLPalettes;                           /* Pointer to current palette lists, one per HBL */
Uint32 HBLPaletteMasks[NUM_VISIBLE_LINES+1];    /* Bit mask of palette colours changes, top bit set is resolution change */
Uint32 *pHBLPaletteMasks;
int nScreenRefreshRate = 50;                    /* 50 or 60 Hz in color, 70 Hz in mono */
Uint32 VideoBase;                               /* Base address in ST Ram for screen (read on each VBL) */

int nVBLs;                                      /* VBL Counter */
int nHBL;                                       /* HBL line */
int nStartHBL;                                  /* Start HBL for visible screen */
int nEndHBL;                                    /* End HBL for visible screen */
int nScanlinesPerFrame = 313;                   /* Number of scan lines per frame */
int nCyclesPerLine = 512;                       /* Cycles per horizontal line scan */
static int nFirstVisibleHbl = 34;               /* The first line of the ST screen that is copied to the PC screen buffer */

static Uint8 HWScrollCount;                     /* HW scroll pixel offset, STe only (0...15) */
int NewHWScrollCount = -1;                      /* Used in STE mode when writing to the scrolling register $ff8265 */
static Uint8 LineWidth;                         /* Scan line width add, STe only (words, minus 1) */
int NewLineWidth = -1;				/* Used in STE mode when writing to the line width register $ff820f */
static Uint8 *pVideoRaster;                     /* Pointer to Video raster, after VideoBase in PC address space. Use to copy data on HBL */
static Uint8 VideoShifterByte;                  /* VideoShifter (0xff8260) value store in video chip */
static int LeftRightBorder;                     /* BORDERMASK_xxxx used to simulate left/right border removal */
static int LineStartCycle;                      /* Cycle where display starts for the current line */
static int LineEndCycle;                        /* Cycle where display ends for the current line */
static bool bSteBorderFlag;                     /* TRUE when screen width has been switched to 336 (e.g. in Obsession) */
static bool bTTColorsSync, bTTColorsSTSync;     /* whether TT colors need convertion to SDL */

int	ScreenBorderMask[ MAX_SCANLINES_PER_FRAME ];
int	LastCycleSync50;			/* value of Cycles_GetCounterOnWriteAccess last time ff820a was set to 0x02 for the current VBL */
int	LastCycleSync60;			/* value of Cycles_GetCounterOnWriteAccess last time ff820a was set to 0x00 for the current VBL */
int	NewVideoHi = -1;			/* new value for $ff8205 on STE */
int	NewVideoMed = -1;			/* new value for $ff8207 on STE */
int	NewVideoLo = -1;			/* new value for $ff8209 on STE */
int	LineTimerBCycle = LINE_END_CYCLE_50 + TIMERB_VIDEO_CYCLE_OFFSET;	/* position of the Timer B interrupt on active lines */

int	LineRemoveTopCycle = LINE_REMOVE_TOP_CYCLE_STF;
int	LineRemoveBottomCycle = LINE_REMOVE_BOTTOM_CYCLE_STF;
int	RestartVideoCounterCycle = RESTART_VIDEO_COUNTER_CYCLE_STF;
int	VblVideoCycleOffset = VBL_VIDEO_CYCLE_OFFSET_STF;


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void Video_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&VideoShifterByte, sizeof(VideoShifterByte));
	MemorySnapShot_Store(&TTRes, sizeof(TTRes));
	MemorySnapShot_Store(&bUseSTShifter, sizeof(bUseSTShifter));
	MemorySnapShot_Store(&bUseHighRes, sizeof(bUseHighRes));
	MemorySnapShot_Store(&nVBLs, sizeof(nVBLs));
	MemorySnapShot_Store(&nHBL, sizeof(nHBL));
	MemorySnapShot_Store(&nStartHBL, sizeof(nStartHBL));
	MemorySnapShot_Store(&nEndHBL, sizeof(nEndHBL));
	MemorySnapShot_Store(&OverscanMode, sizeof(OverscanMode));
	MemorySnapShot_Store(HBLPalettes, sizeof(HBLPalettes));
	MemorySnapShot_Store(HBLPaletteMasks, sizeof(HBLPaletteMasks));
	MemorySnapShot_Store(&VideoBase, sizeof(VideoBase));
	MemorySnapShot_Store(&LineWidth, sizeof(LineWidth));
	MemorySnapShot_Store(&HWScrollCount, sizeof(HWScrollCount));
	MemorySnapShot_Store(&pVideoRaster, sizeof(pVideoRaster));
	MemorySnapShot_Store(&nScanlinesPerFrame, sizeof(nScanlinesPerFrame));
	MemorySnapShot_Store(&nCyclesPerLine, sizeof(nCyclesPerLine));
	MemorySnapShot_Store(&nFirstVisibleHbl, sizeof(nFirstVisibleHbl));
	MemorySnapShot_Store(&bSteBorderFlag, sizeof(bSteBorderFlag));
}


/*-----------------------------------------------------------------------*/
/*
 * Set specific video timings, depending on the system being emulated.
 */
void	Video_SetSystemTimings(void)
{
  if ( ConfigureParams.System.nMachineType == MACHINE_ST )
    {
      LineRemoveTopCycle = LINE_REMOVE_TOP_CYCLE_STF;
      LineRemoveBottomCycle = LINE_REMOVE_BOTTOM_CYCLE_STF;
      RestartVideoCounterCycle = RESTART_VIDEO_COUNTER_CYCLE_STF;
      VblVideoCycleOffset = VBL_VIDEO_CYCLE_OFFSET_STF;
    }

  else					/* STE, Falcon, TT */
    {
      LineRemoveTopCycle = LINE_REMOVE_TOP_CYCLE_STE;
      LineRemoveBottomCycle = LINE_REMOVE_BOTTOM_CYCLE_STE;
      RestartVideoCounterCycle = RESTART_VIDEO_COUNTER_CYCLE_STE;
      VblVideoCycleOffset = VBL_VIDEO_CYCLE_OFFSET_STE;
    }
}


/*-----------------------------------------------------------------------*/
/**
 * Calculate and return video address pointer.
 */
static Uint32 Video_CalculateAddress(void)
{
	int X, nFrameCycles, NbBytes;
	int HblCounterVideo;
	Uint32 VideoAddress;      /* Address of video display in ST screen space */
	int nSyncByte;
	int LineBorderMask;
	int PrevSize;
	int CurSize;


	/* Find number of cycles passed during frame */
	/* We need to substract '12' for correct video address calculation */
	nFrameCycles = Cycles_GetCounterOnReadAccess(CYCLES_COUNTER_VIDEO) - 12;

	/* Now find which pixel we are on (ignore left/right borders) */
	X = nFrameCycles % nCyclesPerLine;

	/* Get real video line count (can be different from nHBL) */
	HblCounterVideo = nFrameCycles / nCyclesPerLine;


	nSyncByte = IoMem_ReadByte(0xff820a) & 2;	/* only keep bit 1 */
	if (nSyncByte)				/* 50 Hz */
	{
		LineStartCycle = LINE_START_CYCLE_50;
		LineEndCycle = LINE_END_CYCLE_50;
	}
	else						/* 60 Hz */
	{
		LineStartCycle = LINE_START_CYCLE_60;
		LineEndCycle = LINE_END_CYCLE_60;
	}


	/* Top of screen is usually 63 lines from VBL in 50 Hz */
	if (nFrameCycles < nStartHBL*nCyclesPerLine)
	{
		/* pVideoRaster was set during Video_ClearOnVBL using VideoBase */
		/* and it could also have been modified on STE by writing to ff8205/07/09 */
		/* We should not use ff8201/ff8203  which are reloaded in ff8205/ff8207 only once per VBL */
		VideoAddress = pVideoRaster - STRam;
	}

	else if (nFrameCycles > RestartVideoCounterCycle)
	{
		/* This is where ff8205/ff8207 are reloaded with the content of ff8201/ff8203 on a real ST */
		/* (used in ULM DSOTS demos). VideoBase is also reloaded in Video_ClearOnVBL to be sure */
		VideoBase = (Uint32)IoMem_ReadByte(0xff8201)<<16 | (Uint32)IoMem_ReadByte(0xff8203)<<8;
		if (ConfigureParams.System.nMachineType != MACHINE_ST)
		{
			/* on STe 2 aligned, on Falcon 4 aligned, on TT 8 aligned. We do STe. */
			VideoBase |= IoMem_ReadByte(0xff820d) & ~1;
		}

		VideoAddress = VideoBase;
	}

	else
	{
		VideoAddress = pVideoRaster - STRam;		/* pVideoRaster is updated by Video_CopyScreenLineColor */

		/* Now find which pixel we are on (ignore left/right borders) */
		X = ( Cycles_GetCounterOnReadAccess(CYCLES_COUNTER_VIDEO) - 12 ) % nCyclesPerLine;

		/* Get real video line count (can be different from nHBL) */
		HblCounterVideo = ( Cycles_GetCounterOnReadAccess(CYCLES_COUNTER_VIDEO) - 12 ) / nCyclesPerLine;

		/* Correct the case when read overlaps end of line / start of next line */
		/* Video_CopyScreenLineColor was not called yet to update VideoAddress */
		/* so we need to determine the size of the previous line to get the */
		/* correct value of VideoAddress. */
		PrevSize = 0;
		if ( HblCounterVideo < nHBL )
			X = 0;
		else if ( ( HblCounterVideo > nHBL )		/* HblCounterVideo = nHBL+1 */
		          &&  ( nHBL >= nStartHBL ) )		/* if nHBL was not visible, PrevSize = 0 */
		{
			LineBorderMask = ScreenBorderMask[ HblCounterVideo-1 ];	/* get border mask for nHBL */
			PrevSize = BORDERBYTES_NORMAL;		/* normal line */

			if (LineBorderMask & BORDERMASK_LEFT_OFF)
				PrevSize += BORDERBYTES_LEFT;
			else if (LineBorderMask & BORDERMASK_LEFT_PLUS_2)
				PrevSize += 2;

			if (LineBorderMask & BORDERMASK_STOP_MIDDLE)
				PrevSize -= 106;
			else if (LineBorderMask & BORDERMASK_RIGHT_MINUS_2)
				PrevSize -= 2;
			else if (LineBorderMask & BORDERMASK_RIGHT_OFF)
				PrevSize += BORDERBYTES_RIGHT;

			if (LineBorderMask & BORDERMASK_EMPTY_LINE)
				PrevSize = 0;
		}


		LineBorderMask = ScreenBorderMask[ HblCounterVideo ];

		CurSize = BORDERBYTES_NORMAL;			/* normal line */

		if (LineBorderMask & BORDERMASK_LEFT_OFF)
			CurSize += BORDERBYTES_LEFT;
		else if (LineBorderMask & BORDERMASK_LEFT_PLUS_2)
			CurSize += 2;

		if (LineBorderMask & BORDERMASK_STOP_MIDDLE)
			CurSize -= 106;
		else if (LineBorderMask & BORDERMASK_RIGHT_MINUS_2)
			CurSize -= 2;
		else if (LineBorderMask & BORDERMASK_RIGHT_OFF)
			CurSize += BORDERBYTES_RIGHT;
		if (LineBorderMask & BORDERMASK_RIGHT_OFF_FULL)
			CurSize += BORDERBYTES_RIGHT_FULL;

		if ( LineBorderMask & BORDERMASK_LEFT_PLUS_2)
			LineStartCycle = LINE_START_CYCLE_60;
		else if ( LineBorderMask & BORDERMASK_LEFT_OFF )
			LineStartCycle = LINE_START_CYCLE_70;

		LineEndCycle = LineStartCycle + CurSize*2;


		if ( X < LineStartCycle )
			X = LineStartCycle;				/* display is disabled in the left border */
		else if ( X > LineEndCycle )
			X = LineEndCycle;				/* display is disabled in the right border */

		NbBytes = ( (X-LineStartCycle)>>1 ) & (~1);	/* 2 cycles per byte */


		/* when left border is open, we have 2 bytes less than theorical value */
		/* (26 bytes in left border, which is not a multiple of 4 cycles) */
		if ( LineBorderMask & BORDERMASK_LEFT_OFF )
			NbBytes -= 2;

		if ( LineBorderMask & BORDERMASK_EMPTY_LINE )
			NbBytes = 0;

		/* Add line cycles if we have not reached end of screen yet: */
		if (nFrameCycles < nEndHBL*nCyclesPerLine)
			VideoAddress += PrevSize + NbBytes;
	}

	HATARI_TRACE ( HATARI_TRACE_VIDEO_ADDR , "video base=%x raster=%x addr=%x video_cyc=%d line_cyc=%d/X=%d @ nHBL=%d/video_hbl=%d %d<->%d pc=%x instr_cyc=%d\n",
	               VideoBase, pVideoRaster - STRam, VideoAddress, Cycles_GetCounter(CYCLES_COUNTER_VIDEO),
	               Cycles_GetCounter(CYCLES_COUNTER_VIDEO) %  nCyclesPerLine, X,
	               nHBL, HblCounterVideo, LineStartCycle, LineEndCycle, M68000_GetPC(), CurrentInstrCycles );

	return VideoAddress;
}


/*-----------------------------------------------------------------------*/
/**
 * Write to VideoShifter (0xff8260), resolution bits
 */
static void Video_WriteToShifter(Uint8 Byte)
{
	static int nLastHBL = -1, nLastVBL = -1, nLastByte, nLastCycles, nLastFrameCycles;
	int nFrameCycles, nLineCycles;
	int HblCounterVideo;

	nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);

	/* We only care for cycle position in the actual screen line */
	nLineCycles = nFrameCycles % nCyclesPerLine;

	HblCounterVideo = nFrameCycles / nCyclesPerLine;

	HATARI_TRACE ( HATARI_TRACE_VIDEO_RES ,"shifter=0x%2.2X video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n",
	               Byte, nFrameCycles, nLineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );

	/* Ignore consecutive writes of the same value */
	if ( Byte == nLastByte )
		return;						/* do nothing */

	/* Remove left border : +26 bytes */
	/* this can be done with a hi/lo res switch or a hi/med res switch */
	if (nLastByte == 0x02 && Byte == 0x00
	        && nLineCycles <= (LINE_START_CYCLE_70+28)
	        && nFrameCycles-nLastFrameCycles <= 30)
	{
		HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect remove left\n" );
		LeftRightBorder |= BORDERMASK_LEFT;
		ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_LEFT_OFF;
		LineStartCycle = LINE_START_CYCLE_70;
	}

	if (nLastByte == 0x02 && Byte == 0x01
	        && nLineCycles <= (LINE_START_CYCLE_70+20)
	        && nFrameCycles-nLastFrameCycles <= 30)
	{
		HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect remove left mid\n" );
		LeftRightBorder |= BORDERMASK_LEFT;
		ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_LEFT_OFF_MID;	/* a later switch to low res might gives right scrolling */
		/* By default, this line will be in mid res, except if we detect hardware scrolling later */
		ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_OVERSCAN_MID_RES | ( 2 << 20 );
		LineStartCycle = LINE_START_CYCLE_70;
	}

	/* Empty line switching res */
	else if ( ( nFrameCycles-nLastFrameCycles <= 16 )
	          && ( nLastCycles == LINE_EMPTY_CYCLE_70 ) )
	{
		HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect empty line res\n" );
		ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_EMPTY_LINE;
		LineStartCycle = 0;
		LineEndCycle = 0;
	}

	/* Start right border near middle of the line : -106 bytes */
//	if (nLastByte == 0x02 && Byte == 0x00
//	 && nFrameCycles-nLastFrameCycles <= 20
//	 && nLineCycles >= LINE_END_CYCLE_70 && nLineCycles <= (LINE_END_CYCLE_70+20) )
	if ( ( nLastByte == 0x02 && Byte == 0x00 )
	        && ( nLastHBL == HblCounterVideo )			/* switch during the same line */
	        && ( nLastCycles <= LINE_END_CYCLE_70+4 )		/* switch to hi res before cycle 164 */
	        && ( nLineCycles >= LINE_END_CYCLE_70+4 ) )		/* switch to lo res after cycle 164 */
	{
		HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect stop middle\n" );
		ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_STOP_MIDDLE;
		LineEndCycle = LINE_END_CYCLE_70;
	}

	/* Remove right border a second time after removing it a first time : */
	/* this removes left border on next line too  (used in 'Enchanted Lands')*/
	if ( ScreenBorderMask[ HblCounterVideo ] & BORDERMASK_RIGHT_OFF
	        && nLastByte == 0x02 && Byte == 0x00
	        && nFrameCycles-nLastFrameCycles <= 20
	        && nLastCycles == LINE_END_CYCLE_50_2 )
	{
		HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect remove right full\n" );
		LeftRightBorder |= BORDERMASK_RIGHT;    /* Program tries to open right border */
		ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_RIGHT_OFF_FULL;
		ScreenBorderMask[ HblCounterVideo+1 ] |= BORDERMASK_LEFT_OFF;	/* no left border on next line */
		LineEndCycle = LINE_END_CYCLE_FULL;
	}

	/* If left border is opened and we switch to medium resolution */
	/* during the next cycles, then we assume a mid res overscan line */
	/* instead of a low res overscan line */
	/* Used in 'No Cooper' greetings by 1984 and 'Punish Your Machine' by Delta Force */
	if ( ScreenBorderMask[ HblCounterVideo ] & BORDERMASK_LEFT_OFF
	        && Byte == 0x01 )
	{
		if ( nLineCycles == LINE_LEFT_MID_CYCLE_1 )		/* 'No Cooper' timing */
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect midres overscan offset 0 byte\n" );
			ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_OVERSCAN_MID_RES | ( 0 << 20 );
		}
		else if ( nLineCycles == LINE_LEFT_MID_CYCLE_2 )	/* 'Best Part Of The Creation / PYM' timing */
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect midres overscan offset 2 bytes\n" );
			ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_OVERSCAN_MID_RES | ( 2 << 20 );
		}
	}

	/* If left border was opened with a hi/mid res switch */
	/* we need to check if the switch to low res can trigger */
	/* a right hardware scrolling. */
	/* We store the pixels count in the upper 16 bits */
	if ( ScreenBorderMask[ HblCounterVideo ] & BORDERMASK_LEFT_OFF_MID
	        && Byte == 0x00 && nLineCycles <= LINE_SCROLL_1_CYCLE_50 )
	{
		/* The hi/mid switch was a switch to do low res hardware scrolling, */
		/* so we must cancel the mid res overscan bit. */
		ScreenBorderMask[ HblCounterVideo ] &= (~BORDERMASK_OVERSCAN_MID_RES);

		if ( nLineCycles == LINE_SCROLL_13_CYCLE_50 )		/* cycle 20 */
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect 13 pixels right scroll\n" );
			ScreenBorderMask[ HblCounterVideo ] |= ( 13 << 16 );
		}
		else if ( nLineCycles == LINE_SCROLL_9_CYCLE_50 )	/* cycle 24 */
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect 9 pixels right scroll\n" );
			ScreenBorderMask[ HblCounterVideo ] |= ( 9 << 16 );
		}
		else if ( nLineCycles == LINE_SCROLL_5_CYCLE_50 )	/* cycle 28 */
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect 5 pixels right scroll\n" );
			ScreenBorderMask[ HblCounterVideo ] |= ( 5 << 16 );
		}
		else if ( nLineCycles == LINE_SCROLL_1_CYCLE_50 )	/* cycle 32 */
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect 1 pixel right scroll\n" );
			ScreenBorderMask[ HblCounterVideo ] |= ( 1 << 16 );
		}
	}

	nLastVBL = nVBLs;
	nLastHBL = HblCounterVideo;
	nLastByte = Byte;
	nLastCycles = nLineCycles;
	nLastFrameCycles = nFrameCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Write to VideoSync (0xff820a), Hz setting
 */
void Video_Sync_WriteByte(void)
{
	static int nLastHBL = -1, nLastVBL = -1, nLastByte, nLastCycles, nLastFrameCycles;
	int nFrameCycles, nLineCycles;
	int HblCounterVideo;
	Uint8 Byte;

	/* We're only interested in lower 2 bits (50/60Hz) */
	Byte = IoMem[0xff820a] & 2;			/* only keep bit 1 (50/60 Hz) */

	nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);

	/* We only care for cycle position in the actual screen line */
	nLineCycles = nFrameCycles % nCyclesPerLine;

	HblCounterVideo = nFrameCycles / nCyclesPerLine;

	HATARI_TRACE ( HATARI_TRACE_VIDEO_SYNC ,"sync=0x%2.2X video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n",
	               Byte, nFrameCycles, nLineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );

	/* Ignore consecutive writes of the same value */
	if ( Byte == nLastByte )
		return;						/* do nothing */

	if ( ( nLastByte == 0x00 ) && ( Byte == 0x02 )/* switched from 60 Hz to 50 Hz? */
	        && ( nLastVBL == nVBLs ) )			/* switched during the same VBL */
	{
		/* Add 2 bytes to left border */
//		if ( nFrameCycles-nLastFrameCycles <= 24
//		 && nLastCycles <= LINE_START_CYCLE_60 && nLineCycles >= LINE_START_CYCLE_50 )
		if ( ( LastCycleSync60 <= HblCounterVideo * nCyclesPerLine + LINE_START_CYCLE_60 )
		        && ( nLineCycles >= LINE_START_CYCLE_50 )	/* The line started in 60 Hz and continues in 50 Hz */
		        && ( nLineCycles <= LINE_END_CYCLE_60 )		/* change when line is active */
		        && ( ( ScreenBorderMask[ HblCounterVideo ] & ( BORDERMASK_LEFT_OFF | BORDERMASK_LEFT_OFF_MID ) ) == 0 ) )
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect left+2\n" );
			ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_LEFT_PLUS_2;
			LineStartCycle = LINE_START_CYCLE_60;
		}

		/* Empty line switching freq */
		else if ( ( nFrameCycles-nLastFrameCycles <= 24 )
		          && ( nLastCycles == LINE_START_CYCLE_50 ) && ( nLineCycles > LINE_START_CYCLE_50 ) )
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect empty line freq\n" );
			ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_EMPTY_LINE;
			LineStartCycle = 0;
			LineEndCycle = 0;
		}

		/* Remove 2 bytes to the right */
//      else if ( nFrameCycles-nLastFrameCycles <= 128
//	&& nLastCycles <= LINE_END_CYCLE_60 && nLineCycles > LINE_END_CYCLE_60
		if ( ( nLineCycles > LINE_END_CYCLE_60 )
		        && ( ( nLastCycles > LINE_START_CYCLE_60 ) && ( nLastCycles <= LINE_END_CYCLE_60 ) )
		        && ( nLastHBL == HblCounterVideo )
		        && ( ( ScreenBorderMask[ HblCounterVideo ] & BORDERMASK_STOP_MIDDLE ) == 0 ) )
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect right-2\n" );
			LeftRightBorder |= BORDERMASK_MIDDLE;   /* Program tries to shorten line by 2 bytes */
			ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_RIGHT_MINUS_2;
			LineEndCycle = LINE_END_CYCLE_60;
		}
	}

	/* special case for right border : some programs don't switch back to */
	/* 50 Hz immediatly (sync screen in SNY II), so we just check if */
	/* freq changes to 60 Hz at the position where line should end in 50 Hz */
	if ( ( nLastByte == 0x02 && Byte == 0x00 )	/* switched from 50 Hz to 60 Hz? */
	        && ( HblCounterVideo >= nStartHBL ) )	/* only if display is on */
	{
		if ( ( nLineCycles == LINE_END_CYCLE_50 )
		        && ( ( ScreenBorderMask[ HblCounterVideo ] & BORDERMASK_STOP_MIDDLE ) == 0 ) )
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect remove right\n" );
			LeftRightBorder |= BORDERMASK_RIGHT;    /* Program tries to open right border */
			ScreenBorderMask[ HblCounterVideo ] |= BORDERMASK_RIGHT_OFF;
			LineEndCycle = LINE_END_CYCLE_NO_RIGHT;
		}
	}


	/* Store cycle position of freq 50/60 to check for top/bottom border removal in Video_EndHBL. */
	/* Also update start/end line depending on the current value of nHBL */
	if ( Byte == 0x02 )						/* switch to 50 Hz */
	{
		LastCycleSync50 = nFrameCycles;

		if ( ( HblCounterVideo < SCREEN_START_HBL_50HZ )	/* nStartHBL can change only if display is not ON yet */
		        && ( OverscanMode & OVERSCANMODE_TOP ) == 0 )	/* update only if top was not removed */
			nStartHBL = SCREEN_START_HBL_50HZ;

		if ( ( HblCounterVideo < SCREEN_END_HBL_50HZ )		/* nEndHBL can change only if display is not OFF yet */
		        && ( OverscanMode & OVERSCANMODE_BOTTOM ) == 0 )	/* update only if bottom was not removed */
			nEndHBL = SCREEN_END_HBL_50HZ;				/* 263 */

		if ( ( LineEndCycle == LINE_END_CYCLE_60 )		/* Freq is changed before the end of a 60 Hz line */
			&& ( nLineCycles < LINE_END_CYCLE_60 ) )
			LineEndCycle = LINE_END_CYCLE_50;

	}
	else if ( Byte == 0x00 )					/* switch to 60 Hz */
	{
		LastCycleSync60 = nFrameCycles;

		if ( ( HblCounterVideo < SCREEN_START_HBL_60HZ-1 )	/* nStartHBL can change only if display is not ON yet */
			|| ( ( HblCounterVideo == SCREEN_START_HBL_60HZ-1 ) && ( nLineCycles <= LineRemoveTopCycle ) ) )
			nStartHBL = SCREEN_START_HBL_60HZ;

		if ( ( HblCounterVideo < SCREEN_END_HBL_60HZ )		/* nEndHBL can change only if display is not OFF yet */
		        && ( OverscanMode & OVERSCANMODE_BOTTOM ) == 0 )	/* update only if bottom was not removed */
			nEndHBL = SCREEN_END_HBL_60HZ;				/* 234 */

		if ( ( LineEndCycle == LINE_END_CYCLE_50 )		/* Freq is changed before the end of a 50 Hz line */
			&& ( nLineCycles < LINE_END_CYCLE_60 ) )	/* and before the end of a 60 Hz line */
			LineEndCycle = LINE_END_CYCLE_60;
	}

	/* If the frequence changed, we need to update the EndLine interrupt */
	/* so that it happens 28 cycles after the current LineEndCycle.*/
	/* We check if the change affects the current line or the next one. */
	if ( Byte != nLastByte )
	{
		int nFrameCycles2 = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
		int nLineCycles2 = nFrameCycles2 % nCyclesPerLine;

		if ( ScreenBorderMask[ HblCounterVideo ] & BORDERMASK_RIGHT_MINUS_2 )		/* 60/50 Hz switch */
		{
			/* Do nothing when switching back to 50 Hz, keep timer B at pos LINE_END_CYCLE_60+TIMERB_VIDEO_CYCLE_OFFSET for this line */
		}

		else if ( nLineCycles2 < LineEndCycle )			/* freq changed before the end of the line */
		{
			LineTimerBCycle = LineEndCycle + TIMERB_VIDEO_CYCLE_OFFSET;
			Int_AddRelativeInterrupt ( LineTimerBCycle - nLineCycles2 ,
						INT_CPU_CYCLE , INTERRUPT_VIDEO_ENDLINE , 0 );
		}

		else							/* freq changed after the end of the line */
		{
			/* By default, next EndLine's int will be on line nHBL+1 at pos 376+28 or 372+28 */
			if ( Byte == 0x02 )		/* 50 Hz, pos 376+28 */
				LineTimerBCycle = LINE_END_CYCLE_50 + TIMERB_VIDEO_CYCLE_OFFSET;
			else				/* 60 Hz, pos 372+28 */
				LineTimerBCycle = LINE_END_CYCLE_60 + TIMERB_VIDEO_CYCLE_OFFSET;

			Int_AddRelativeInterrupt ( LineTimerBCycle - nLineCycles2 + nCyclesPerLine ,
						INT_CPU_CYCLE , INTERRUPT_VIDEO_ENDLINE , 0 );
		}
	}

	nLastVBL = nVBLs;
	nLastHBL = HblCounterVideo;
	nLastByte = Byte;
	nLastCycles = nLineCycles;
	nLastFrameCycles = nFrameCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Reset Sync/Shifter table at start of each HBL
 */
static void Video_StartHBL(void)
{
	int nSyncByte;

	LeftRightBorder = BORDERMASK_NONE;

	nSyncByte = IoMem_ReadByte(0xff820a);
	if (nSyncByte & 2)				/* 50 Hz */
	{
		LineStartCycle = LINE_START_CYCLE_50;
		LineEndCycle = LINE_END_CYCLE_50;
	}
	else						/* 60 Hz */
	{
		LineStartCycle = LINE_START_CYCLE_60;
		LineEndCycle = LINE_END_CYCLE_60;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Store whole palette on first line so have reference to work from
 */
static void Video_StoreFirstLinePalette(void)
{
	Uint16 *pp2;
	int i;

	pp2 = (Uint16 *)&IoMem[0xff8240];
	for (i = 0; i < 16; i++)
		HBLPalettes[i] = SDL_SwapBE16(*pp2++);

	/* And set mask flag with palette and resolution */
//	FIXME ; enlever PALETTEMASK_RESOLUTION

//	if ( ScreenBorderMask[ nFirstVisibleHbl ] == BORDERMASK_NONE )	// no border trick, store the current res
	HBLPaletteMasks[0] = (PALETTEMASK_RESOLUTION|PALETTEMASK_PALETTE) | (((Uint32)IoMem_ReadByte(0xff8260)&0x3)<<16);
//	else						// border removal, assume low res for the whole line
//		HBLPaletteMasks[0] = (PALETTEMASK_RESOLUTION|PALETTEMASK_PALETTE) | (0<<16);
}


/*-----------------------------------------------------------------------*/
/**
 * Store resolution on each line (used to test if mixed low/medium resolutions)
 */
static void Video_StoreResolution(int y)
{
	Uint8 res;
	int Mask;

	/* Clear resolution, and set with current value */
	if (!(bUseHighRes || bUseVDIRes))
	{
		HBLPaletteMasks[y] &= ~(0x3<<16);
		res = IoMem_ReadByte(0xff8260)&0x3;

		Mask = ScreenBorderMask[ y+nFirstVisibleHbl ];

		if ( Mask & BORDERMASK_OVERSCAN_MID_RES )		/* special case for mid res to render the overscan line */
			res = 1;						/* mid res instead of low res */
		else if ( Mask != BORDERMASK_NONE )			/* border removal : assume low res for the whole line */
			res = 0;

		HBLPaletteMasks[y] |= PALETTEMASK_RESOLUTION|((Uint32)res)<<16;

#if 0
		if ( ( Mask == BORDERMASK_NONE )			/* no border trick, store the current res */
		        || ( res == 0 ) || ( res == 1 ) )			/* if border trick, ignore passage to hi res */
			HBLPaletteMasks[y] |= PALETTEMASK_RESOLUTION|((Uint32)res)<<16;
		else						/* border removal or hi res : assume low res for the whole line */
			HBLPaletteMasks[y] |= (0)<<16;

		/* special case for mid res to render the overscan line */
		if ( Mask & BORDERMASK_OVERSCAN_MID_RES )
			HBLPaletteMasks[y] |= PALETTEMASK_RESOLUTION|((Uint32)1)<<16;	/* mid res instead of low res */
#endif

//   fprintf ( stderr , "store res %d line %d %x %x\n" , res , y , Mask , HBLPaletteMasks[y] );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Copy one line of monochrome screen into buffer for conversion later.
 */
static void Video_CopyScreenLineMono(void)
{
	Uint32 addr;

	/* Copy one line - 80 bytes in ST high resolution */
	memcpy(pSTScreen, pVideoRaster, SCREENBYTES_MONOLINE);
	pVideoRaster += SCREENBYTES_MONOLINE;

	/* Handle STE fine scrolling (HWScrollCount is zero on ST). */
	if (HWScrollCount)
	{
		Uint16 *pScrollAdj;
		int nNegScrollCnt;

		pScrollAdj = (Uint16 *)pSTScreen;
		nNegScrollCnt = 16 - HWScrollCount;

		/* Shift the whole line by the given scroll count */
		while ((Uint8*)pScrollAdj < pSTScreen + SCREENBYTES_MONOLINE-2)
		{
			do_put_mem_word(pScrollAdj, (do_get_mem_word(pScrollAdj) << HWScrollCount)
			                | (do_get_mem_word(pScrollAdj+1) >> nNegScrollCnt));
			++pScrollAdj;
		}

		/* Handle the last 16 pixels of the line */
		do_put_mem_word(pScrollAdj, (do_get_mem_word(pScrollAdj) << HWScrollCount)
		                | (do_get_mem_word(pVideoRaster) >> nNegScrollCnt));

		/* HW scrolling advances Shifter video counter by one */
		pVideoRaster += 1 * 2;
	}

	/* LineWidth is zero on ST. */
	/* On STE, the Shifter skips the given amount of words. */
	pVideoRaster += LineWidth*2;

	/* On STE, handle modifications of the video counter address $ff8205/07/09 */
	/* that occurred while the display was already ON */
	if ( NewVideoHi >= 0 )
	{
		addr = ( ( pVideoRaster - STRam ) & 0x00ffff ) | ( NewVideoHi << 16 );
		pVideoRaster = &STRam[addr & ~1];
		NewVideoHi = -1;
	}
	if ( NewVideoMed >= 0 )
	{
		addr = ( ( pVideoRaster - STRam ) & 0xff00ff ) | ( NewVideoMed << 8 );
		pVideoRaster = &STRam[addr & ~1];
		NewVideoMed = -1;
	}
	if ( NewVideoLo >= 0 )
	{
		addr = ( ( pVideoRaster - STRam ) & 0xffff00 ) | ( NewVideoLo );
		pVideoRaster = &STRam[addr & ~1];
		NewVideoLo = -1;
	}

	/* On STE, if we wrote to the hwscroll register, we set the */
	/* new value here, once the current line was processed */
	if ( NewHWScrollCount >= 0 )
	{
		HWScrollCount = NewHWScrollCount;
		NewHWScrollCount = -1;
	}

	/* On STE, if we wrote to the linewidth register, we set the */
	/* new value here, once the current line was processed */
	if ( NewLineWidth >= 0 )
	{
		LineWidth = NewLineWidth;
		NewLineWidth = -1;
	}

	/* Each screen line copied to buffer is always same length */
	pSTScreen += SCREENBYTES_MONOLINE;
}


/*-----------------------------------------------------------------------*/
/**
 * Copy one line of color screen into buffer for conversion later.
 * Possible lines may be top/bottom border, and/or left/right borders.
 */
static void Video_CopyScreenLineColor(void)
{
	int LineBorderMask = ScreenBorderMask[ nHBL ];
	int VideoOffset = 0;
	int STF_PixelScroll = 0;
	Uint32 addr;

	//fprintf(stderr , "copy line %d start %d end %d %d %x\n" , nHBL, nStartHBL, nEndHBL, LineBorderMask, pVideoRaster - STRam);

	/* If left border is opened, we need to compensate one missing word in low res (1 plan) */
	/* If overscan is in mid res, the offset is variable */
	if ( LineBorderMask & BORDERMASK_OVERSCAN_MID_RES )
		VideoOffset = - ( ( LineBorderMask >> 20 ) & 0x0f );		/* No Cooper=0  PYM=-2 in mid res overscan */

	else if ( LineBorderMask & BORDERMASK_LEFT_OFF )
		VideoOffset = -2;							/* always 2 bytes in low res overscan */

	/* Handle 4 pixels hardware scrolling ('ST Cnx' demo in 'Punish Your Machine') */
	/* Depending on the number of pixels, we need to compensate for some skipped words */
	else if ( LineBorderMask & BORDERMASK_LEFT_OFF_MID )
	{
		STF_PixelScroll = ( LineBorderMask >> 16 ) & 0x0f;

		if      ( STF_PixelScroll == 13 )	VideoOffset = 2+8;
		else if ( STF_PixelScroll == 9 )	VideoOffset = 0+8;
		else if ( STF_PixelScroll == 5 )	VideoOffset = -2+8;
		else if ( STF_PixelScroll == 1 )	VideoOffset = -4+8;
		else					VideoOffset = 0;	/* never used ? */

		// fprintf(stderr , "scr off %d %d\n" , STF_PixelScroll , VideoOffset);
	}

	/* Is total blank line? I.e. top/bottom border? */
	if ((nHBL < nStartHBL) || (nHBL >= nEndHBL)
	    || (LineBorderMask & BORDERMASK_EMPTY_LINE))	/* 60/50 Hz trick to obtain an empty line */
	{
		/* Clear line to color '0' */
		memset(pSTScreen, 0, SCREENBYTES_LINE);
	}
	else
	{
		/* Does have left border? If not, clear to color '0' */
		if ((LineBorderMask & BORDERMASK_LEFT_OFF)
		    || (LineBorderMask & BORDERMASK_LEFT_OFF_MID))
		{
			/* The "-2" in the following line is needed so that the offset is a multiple of 8 */
			pVideoRaster += BORDERBYTES_LEFT-SCREENBYTES_LEFT+VideoOffset;
			memcpy(pSTScreen, pVideoRaster, SCREENBYTES_LEFT);
			pVideoRaster += SCREENBYTES_LEFT;
		}
		else if (LineBorderMask & BORDERMASK_LEFT_PLUS_2)
		{
			/* bigger line by 2 bytes on the left */
			memset(pSTScreen,0,SCREENBYTES_LEFT-2);		/* clear unused pixels */
			memcpy(pSTScreen+SCREENBYTES_LEFT-2, pVideoRaster, 2);
			pVideoRaster += 2;
		}
		else if (bSteBorderFlag)				/* STE specific */
		{
			/* bigger line by 8 bytes on the left */
			memset(pSTScreen,0,SCREENBYTES_LEFT-4*2);	/* clear unused pixels */
			memcpy(pSTScreen+SCREENBYTES_LEFT-4*2, pVideoRaster, 4*2);
			pVideoRaster += 4*2;
		}
		else
			memset(pSTScreen,0,SCREENBYTES_LEFT);

		/* Short line due to hires in the middle ? */
		if (LineBorderMask & BORDERMASK_STOP_MIDDLE)
		{
			/* 106 bytes less in the line */
			memcpy(pSTScreen+SCREENBYTES_LEFT, pVideoRaster, SCREENBYTES_MIDDLE-106);
			memset(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE-106, 0, 106);	/* clear unused pixels */
			pVideoRaster += (SCREENBYTES_MIDDLE-106);
		}
		else
		{
			/* normal middle part (160 bytes) */
			memcpy(pSTScreen+SCREENBYTES_LEFT, pVideoRaster, SCREENBYTES_MIDDLE);
			pVideoRaster += SCREENBYTES_MIDDLE;
		}

		/* Does have right border ? */
		if (LineBorderMask & BORDERMASK_RIGHT_OFF)
		{
			memcpy(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE, pVideoRaster, SCREENBYTES_RIGHT);
			pVideoRaster += BORDERBYTES_RIGHT-SCREENBYTES_RIGHT;
			pVideoRaster += SCREENBYTES_RIGHT;
		}
		else if (LineBorderMask & BORDERMASK_RIGHT_MINUS_2)
		{
			/* Shortened line by 2 bytes */
			memset(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE-2, 0, SCREENBYTES_RIGHT+2);
			pVideoRaster -= 2;
		}
		else
		{
			/* Simply clear right border to '0' */
			memset(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE,0,SCREENBYTES_RIGHT);
		}

		/* Full right border removal up to the end of the line (cycle 512) */
		if (LineBorderMask & BORDERMASK_RIGHT_OFF_FULL)
			pVideoRaster += BORDERBYTES_RIGHT_FULL;

		/* Correct the offset for pVideoRaster from BORDERMASK_LEFT_OFF above if needed */
		pVideoRaster -= VideoOffset;		/* VideoOffset is 0 or -2 */


		/* Handle 4 pixels hardware scrolling ('ST Cnx' demo in 'Punish Your Machine') */
		/* Shift the line by STF_PixelScroll pixels to the right (we don't need to scroll */
		/* the first 16 pixels / 8 bytes). */
		if (STF_PixelScroll != 0)
		{
			Uint16 *pScreenLineEnd;
			int count;

			pScreenLineEnd = (Uint16 *) ( pSTScreen + SCREENBYTES_LINE - 2 );
			for ( count = 0 ; count < ( SCREENBYTES_LINE - 8 ) / 2 ; count++ , pScreenLineEnd-- )
				do_put_mem_word ( pScreenLineEnd , ( ( do_get_mem_word ( pScreenLineEnd - 4 ) << 16 ) | ( do_get_mem_word ( pScreenLineEnd ) ) ) >> STF_PixelScroll );
		}


		/* STE specific */
		if ( !bSteBorderFlag && HWScrollCount)     /* Handle STE fine scrolling (HWScrollCount is zero on ST) */
		{
			Uint16 *pScrollAdj;     /* Pointer to actual position in line */
			int nNegScrollCnt;
			Uint16 *pScrollEndAddr; /* Pointer to end of the line */

			nNegScrollCnt = 16 - HWScrollCount;
			if (LineBorderMask & BORDERMASK_LEFT_OFF)
				pScrollAdj = (Uint16 *)pSTScreen;
			else
				pScrollAdj = (Uint16 *)(pSTScreen + SCREENBYTES_LEFT);
			if (LineBorderMask & BORDERMASK_RIGHT_OFF)
				pScrollEndAddr = (Uint16 *)(pSTScreen + SCREENBYTES_LINE - 8);
			else
				pScrollEndAddr = (Uint16 *)(pSTScreen + SCREENBYTES_LEFT + SCREENBYTES_MIDDLE - 8);

			if (STRes == ST_MEDIUM_RES)
			{
				/* TODO: Implement fine scrolling for medium resolution, too */
				/* HW scrolling advances Shifter video counter by one */
				pVideoRaster += 2 * 2;
			}
			else
			{
				/* Shift the whole line to the left by the given scroll count */
				while (pScrollAdj < pScrollEndAddr)
				{
					do_put_mem_word(pScrollAdj, (do_get_mem_word(pScrollAdj) << HWScrollCount)
					                | (do_get_mem_word(pScrollAdj+4) >> nNegScrollCnt));
					++pScrollAdj;
				}
				/* Handle the last 16 pixels of the line */
				if (LineBorderMask & BORDERMASK_RIGHT_OFF)
				{
					/* When right border is open, we have to deal with this ugly offset
					 * of 46-SCREENBYTES_RIGHT - The demo "Mind rewind" is a good example */
					Uint16 *pVideoLineEnd = (Uint16 *)(pVideoRaster - (46 - SCREENBYTES_RIGHT));
					do_put_mem_word(pScrollAdj+0, (do_get_mem_word(pScrollAdj+0) << HWScrollCount)
					                | (do_get_mem_word(pVideoLineEnd++) >> nNegScrollCnt));
					do_put_mem_word(pScrollAdj+1, (do_get_mem_word(pScrollAdj+1) << HWScrollCount)
					                | (do_get_mem_word(pVideoLineEnd++) >> nNegScrollCnt));
					do_put_mem_word(pScrollAdj+2, (do_get_mem_word(pScrollAdj+2) << HWScrollCount)
					                | (do_get_mem_word(pVideoLineEnd++) >> nNegScrollCnt));
					do_put_mem_word(pScrollAdj+3, (do_get_mem_word(pScrollAdj+3) << HWScrollCount)
					                | (do_get_mem_word(pVideoLineEnd++) >> nNegScrollCnt));
				}
				else
				{
					do_put_mem_word(pScrollAdj+0, (do_get_mem_word(pScrollAdj+0) << HWScrollCount)
					                | (do_get_mem_word(pVideoRaster+0) >> nNegScrollCnt));
					do_put_mem_word(pScrollAdj+1, (do_get_mem_word(pScrollAdj+1) << HWScrollCount)
					                | (do_get_mem_word(pVideoRaster+2) >> nNegScrollCnt));
					do_put_mem_word(pScrollAdj+2, (do_get_mem_word(pScrollAdj+2) << HWScrollCount)
					                | (do_get_mem_word(pVideoRaster+4) >> nNegScrollCnt));
					do_put_mem_word(pScrollAdj+3, (do_get_mem_word(pScrollAdj+3) << HWScrollCount)
					                | (do_get_mem_word(pVideoRaster+6) >> nNegScrollCnt));
				}
				/* HW scrolling advances Shifter video counter by one */
				pVideoRaster += 4 * 2;

				/* On STE, when we have a 230 bytes overscan line and HWScrollCount > 0 */
				/* we must read 6 bytes less than expected */
				if ( (LineBorderMask & BORDERMASK_LEFT_OFF) && (LineBorderMask & BORDERMASK_RIGHT_OFF) )
					pVideoRaster -= 6;		/* we don't add 8 bytes, but 2 */

			}
		}

		/* LineWidth is zero on ST. */
		/* On STE, the Shifter skips the given amount of words. */
		pVideoRaster += LineWidth*2;

		/* On STE, handle modifications of the video counter address $ff8205/07/09 */
		/* that occurred while the display was already ON */
		if ( NewVideoHi >= 0 )
		{
			addr = ( ( pVideoRaster - STRam ) & 0x00ffff ) | ( NewVideoHi << 16 );
			pVideoRaster = &STRam[addr & ~1];
			NewVideoHi = -1;
		}
		if ( NewVideoMed >= 0 )
		{
			addr = ( ( pVideoRaster - STRam ) & 0xff00ff ) | ( NewVideoMed << 8 );
			pVideoRaster = &STRam[addr & ~1];
			NewVideoMed = -1;
		}
		if ( NewVideoLo >= 0 )
		{
			addr = ( ( pVideoRaster - STRam ) & 0xffff00 ) | ( NewVideoLo );
			pVideoRaster = &STRam[addr & ~1];
			NewVideoLo = -1;
		}

		/* On STE, if we wrote to the hwscroll register, we set the */
		/* new value here, once the current line was processed */
		if ( NewHWScrollCount >= 0 )
		{
			HWScrollCount = NewHWScrollCount;
			NewHWScrollCount = -1;
		}

		/* On STE, if we wrote to the linewidth register, we set the */
		/* new value here, once the current line was processed */
		if ( NewLineWidth >= 0 )
		{
			LineWidth = NewLineWidth;
			NewLineWidth = -1;
		}
	}

	/* Each screen line copied to buffer is always same length */
	pSTScreen += SCREENBYTES_LINE;
}


/*-----------------------------------------------------------------------*/
/**
 * Copy extended GEM resolution screen
 */
static void Video_CopyVDIScreen(void)
{
	/* Copy whole screen, don't care about being exact as for GEM only */
	memcpy(pSTScreen, pVideoRaster, ((VDIWidth*VDIPlanes)/8)*VDIHeight);
}


/*-----------------------------------------------------------------------*/
/**
 * Check at end of each HBL to see if any Sync/Shifter hardware tricks have been attempted
 * This is the place to check if top/bottom border were removed.
 * NOTE : the tests must be made with nHBL in ascending order.
 */
static void Video_EndHBL(void)
{
	Uint8 SyncByte = IoMem_ReadByte(0xff820a) & 2;	/* only keep bit 1 (50/60 Hz) */

	// fprintf(stderr,"video_endhbl %d last60 %d last 50 %d\n", nHBL, LastCycleSync60, LastCycleSync50);

	/* Remove top border if the switch to 60 Hz was made during this vbl before cycle	*/
	/* 33*512+LineRemoveTopCycle and if the switch to 50 Hz has not yet occured or	*/
	/* occured before the 60 Hz or occured after cycle 33*512+LineRemoveTopCycle.	*/
	if (( nHBL == SCREEN_START_HBL_60HZ-1)
	    && ((LastCycleSync60 >= 0) && (LastCycleSync60 <= (SCREEN_START_HBL_60HZ-1) * nCyclesPerLine + LineRemoveTopCycle))
	    && ((LastCycleSync50 < LastCycleSync60) || (LastCycleSync50 > (SCREEN_START_HBL_60HZ-1) * nCyclesPerLine + LineRemoveTopCycle)))
	{
		/* Top border */
		HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_V , "detect remove top\n" );
		OverscanMode |= OVERSCANMODE_TOP;		/* Set overscan bit */
		nStartHBL = SCREEN_START_HBL_60HZ;	/* New start screen line */
		pHBLPaletteMasks -= OVERSCAN_TOP;	// FIXME useless ?
		pHBLPalettes -= OVERSCAN_TOP;	// FIXME useless ?
	}

	/* Remove bottom border for a 60 Hz screen */
	else if ((nHBL == SCREEN_END_HBL_60HZ-1)	/* last displayed line in 60 Hz */
	         && (SyncByte == 0x02)			/* current freq is 50 Hz */
	         && (LastCycleSync50 >= 0)			/* change occurred during this VBL */
	         && (nStartHBL == SCREEN_START_HBL_60HZ)	/* screen starts in 60 Hz */
	         && ((OverscanMode & OVERSCANMODE_TOP) == 0))	/* and top border was not removed : this screen is only 60 Hz */
	{
		HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_V , "detect remove bottom 60Hz\n" );
		OverscanMode |= OVERSCANMODE_BOTTOM;
		nEndHBL = SCANLINES_PER_FRAME_60HZ;	/* new end for a 60 Hz screen */
	}

	/* Remove bottom border for a 50 Hz screen (similar method to the one for top border) */
	else if ((nHBL == SCREEN_END_HBL_50HZ-1)	/* last displayed line in 50 Hz */
	          && ((LastCycleSync60 >= 0) && (LastCycleSync60 <= (SCREEN_END_HBL_50HZ-1) * nCyclesPerLine + LineRemoveBottomCycle))
	          && ((LastCycleSync50 < LastCycleSync60) || (LastCycleSync50 > (SCREEN_END_HBL_50HZ-1) * nCyclesPerLine + LineRemoveBottomCycle))
	          && ((OverscanMode & OVERSCANMODE_BOTTOM) == 0))	/* border was not already removed at line SCREEN_END_HBL_60HZ */
	{
		HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_V , "detect remove bottom\n" );
		OverscanMode |= OVERSCANMODE_BOTTOM;
		nEndHBL = SCREEN_END_HBL_50HZ+OVERSCAN_BOTTOM;	/* new end for a 50 Hz screen */

		/* Some programs turn to 60 Hz during the active display of the last line to */
		/* remove the bottom border (FNIL by TNT), in that case, we should also remove */
		/* 2 bytes to this line (this is wrong practice as it can distort the display on a real ST) */
		if (LastCycleSync60 <= (SCREEN_END_HBL_50HZ-1) * nCyclesPerLine + LINE_END_CYCLE_60)
		{
			HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect right-2\n" );
			LeftRightBorder |= BORDERMASK_MIDDLE;		/* Program tries to shorten line by 2 bytes */
			ScreenBorderMask[ nHBL ] |= BORDERMASK_RIGHT_MINUS_2;
			LineEndCycle = LINE_END_CYCLE_60;
		}
	}

	/* Store palette for very first line on screen - HBLPalettes[0] */
	if (nHBL == nFirstVisibleHbl)
	{
		/* Store ALL palette for this line into raster table for datum */
		Video_StoreFirstLinePalette();
	}

	if (!bUseVDIRes)
	{
		if (bUseHighRes)
		{
			/* Copy for hi-res (no overscan) */
			if (nHBL >= nFirstVisibleHbl && nHBL < nFirstVisibleHbl+SCREEN_HEIGHT_HBL_MONO)
				Video_CopyScreenLineMono();
		}
		/* Are we in possible visible color display (including borders)? */
		else if (nHBL >= nFirstVisibleHbl && nHBL < nFirstVisibleHbl+NUM_VISIBLE_LINES)
		{
			/* Copy line of screen to buffer to simulate TV raster trace
			 * - required for mouse cursor display/game updates
			 * Eg, Lemmings and The Killing Game Show are good examples */
			Video_CopyScreenLineColor();

			/* Store resolution for every line so can check for mix low/med screens */
			Video_StoreResolution(nHBL-nFirstVisibleHbl);
		}
	}

	/* Finally increase HBL count */
	nHBL++;

	Video_StartHBL();                  /* Setup next one */
}


/*-----------------------------------------------------------------------*/
/**
 * HBL interrupt : this occurs at the end of every line, on cycle 512 (in 50 Hz)
 * It takes 56 cycles to handle the 68000's exception.
 */
void Video_InterruptHandler_HBL(void)
{
	HATARI_TRACE ( HATARI_TRACE_VIDEO_HBL , "HBL %d video_cyc=%d pending_int_cnt=%d\n" ,
	               nHBL , Cycles_GetCounter(CYCLES_COUNTER_VIDEO) , -INT_CONVERT_FROM_INTERNAL ( PendingInterruptCount , INT_CPU_CYCLE ) );

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Generate new HBL, if need to - there are 313 HBLs per frame in 50 Hz */
	if (nHBL < nScanlinesPerFrame-1)
		Int_AddAbsoluteInterrupt(nCyclesPerLine, INT_CPU_CYCLE, INTERRUPT_VIDEO_HBL);

	M68000_Exception ( EXCEPTION_HBLANK , M68000_INT_VIDEO );	/* Horizontal blank interrupt, level 2! */

	Video_EndHBL();              /* Increase HBL count, copy line to display buffer and do any video trickery */
}


/*-----------------------------------------------------------------------*/
/**
 * End Of Line interrupt
 *  As this occurs at the end of a line we cannot get timing for START of first
 * line (as in Spectrum 512)
 * This interrupt is started on cycle position 404 in 50 Hz and on cycle
 * position 400 in 60 Hz. 50 Hz display ends at cycle 376 and 60 Hz displays
 * ends at cycle 372. This means the EndLine interrupt happens 28 cycles
 * after LineEndCycle.
 */
void Video_InterruptHandler_EndLine(void)
{
	Uint8 SyncByte = IoMem_ReadByte(0xff820a) & 2;	/* only keep bit 1 (50/60 Hz) */
	int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
	int nLineCycles = nFrameCycles % nCyclesPerLine;

	HATARI_TRACE ( HATARI_TRACE_VIDEO_HBL , "EndLine TB %d video_cyc=%d line_cyc=%d pending_int_cnt=%d\n" ,
	               nHBL , nFrameCycles , nLineCycles , -INT_CONVERT_FROM_INTERNAL ( PendingInterruptCount , INT_CPU_CYCLE ) );

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();
	/* Generate new Endline, if need to - there are 313 HBLs per frame */
	if (nHBL < nScanlinesPerFrame-1)
	{
		/* If EndLine int is delayed too much (more than 100 cycles), nLineCycles will */
		/* be in the range 0..xxx instead of 400..512. In that case, we need to add */
		/* nCyclesPerLine to be in the range 512..x+512 */
		/* Maximum delay should be around 160 cycles (DIVS), we take LINE_END_CYCLE_60 to be sure */
		if ( nLineCycles < LINE_END_CYCLE_60 )			/* int happened in fact on the next line nHBL+1 */
			nLineCycles += nCyclesPerLine;

		/* By default, next EndLine's int will be on line nHBL+1 at pos 376+28 or 372+28 */
		if ( SyncByte == 0x02 )		/* 50 Hz, pos 376+28 */
			LineTimerBCycle = LINE_END_CYCLE_50 + TIMERB_VIDEO_CYCLE_OFFSET;
		else				/* 60 Hz, pos 372+28 */
			LineTimerBCycle = LINE_END_CYCLE_60 + TIMERB_VIDEO_CYCLE_OFFSET;

		Int_AddRelativeInterrupt ( LineTimerBCycle - nLineCycles + nCyclesPerLine ,
					INT_CPU_CYCLE , INTERRUPT_VIDEO_ENDLINE , 0 );
	}

	/* Is this a good place to send the keyboard packets? Done once per frame */
	if (nHBL == nStartHBL)
	{
		/* On each VBL send automatic keyboard packets for mouse, joysticks etc... */
		IKBD_SendAutoKeyboardCommands();
	}

	/* Timer B occurs at END of first visible screen line in Event Count mode */
	if (nHBL >= nStartHBL && nHBL < nEndHBL)
	{
		/* Handle Timer B when using Event Count mode */
		if (MFP_TBCR == 0x08)      /* Is timer in Event Count mode? */
			MFP_TimerB_EventCount_Interrupt();
	}

	/* If we don't often pump data into the event queue, the SDL misses events... grr... */
	if (!(nHBL & 63))
	{
		Main_EventHandler();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Clear raster line table to store changes in palette/resolution on a line
 * basic. Called once on VBL interrupt.
 */
void Video_SetScreenRasters(void)
{
	pHBLPaletteMasks = HBLPaletteMasks;
	pHBLPalettes = HBLPalettes;
	memset(pHBLPaletteMasks, 0, sizeof(Uint32)*NUM_VISIBLE_LINES);  /* Clear array */
}


/*-----------------------------------------------------------------------*/
/**
 * Set pointers to HBLPalette tables to store correct colours/resolutions
 */
static void Video_SetHBLPaletteMaskPointers(void)
{
	int FrameCycles;
	int Line;
	int Cycle;

	/* FIXME [NP] We should use Cycles_GetCounterOnWriteAccess, but it wouldn't	*/
	/* work when using multiple accesses instructions like move.l or movem	*/
	/* To correct this, assume a delay of 8 cycles (should give a good approximation */
	/* of a move.w or movem.l for example) */
	//  FrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
	FrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO) + 8;

	/* Find 'line' into palette - screen starts 63 lines down, less 29 for top overscan */
	Line = (FrameCycles-(nFirstVisibleHbl*nCyclesPerLine)+SCREEN_START_CYCLE)/nCyclesPerLine;
//	Line = ( FrameCycles / nCyclesPerLine ) - nFirstVisibleHbl;
	Cycle = FrameCycles % nCyclesPerLine;

	/* FIXME [NP] if the color change occurs after the last visible pixel of a line */
	/* we consider the palette should be modified on the next line. This is quite */
	/* a hack, we should handle all color changes through spec512.c to have cycle */
	/* accuracy all the time. */
//	if ( Cycle >= LINE_END_CYCLE_NO_RIGHT-8 );
//		Line++;

	if (Line < 0)        /* Limit to top/bottom of possible visible screen */
		Line = 0;
	if (Line >= NUM_VISIBLE_LINES)
		Line = NUM_VISIBLE_LINES-1;

	/* Store pointers */
	pHBLPaletteMasks = &HBLPaletteMasks[Line];  /* Next mask entry */
	pHBLPalettes = &HBLPalettes[16*Line];       /* Next colour raster list x16 colours */
}


/*-----------------------------------------------------------------------*/
/**
 * Set video shifter timing variables according to screen refresh rate.
 * Note: The following equation must be satisfied for correct timings:
 *
 *   nCyclesPerLine * nScanlinesPerFrame * nScreenRefreshRate = 8 MHz
 */
static void Video_ResetShifterTimings(void)
{
	Uint8 nSyncByte;

	nSyncByte = IoMem_ReadByte(0xff820a);

	if (bUseHighRes)
	{
		/* 71 Hz, monochrome */
		nScreenRefreshRate = 71;
		nScanlinesPerFrame = SCANLINES_PER_FRAME_71HZ;
		nCyclesPerLine = CYCLES_PER_LINE_71HZ;
		nStartHBL = SCREEN_START_HBL_71HZ;
		nFirstVisibleHbl = FIRST_VISIBLE_HBL_71HZ;
		LineTimerBCycle = LINE_END_CYCLE_70 + TIMERB_VIDEO_CYCLE_OFFSET;
	}
	else if (nSyncByte & 2)  /* Check if running in 50 Hz or in 60 Hz */
	{
		/* 50 Hz */
		nScreenRefreshRate = 50;
		nScanlinesPerFrame = SCANLINES_PER_FRAME_50HZ;
		nCyclesPerLine = CYCLES_PER_LINE_50HZ;
		nStartHBL = SCREEN_START_HBL_50HZ;
		nFirstVisibleHbl = FIRST_VISIBLE_HBL_50HZ;
		LineTimerBCycle = LINE_END_CYCLE_50 + TIMERB_VIDEO_CYCLE_OFFSET;
	}
	else
	{
		/* 60 Hz */
		nScreenRefreshRate = 60;
		nScanlinesPerFrame = SCANLINES_PER_FRAME_60HZ;
		nCyclesPerLine = CYCLES_PER_LINE_60HZ;
		nStartHBL = SCREEN_START_HBL_60HZ;
		nFirstVisibleHbl = FIRST_VISIBLE_HBL_60HZ;
		LineTimerBCycle = LINE_END_CYCLE_60 + TIMERB_VIDEO_CYCLE_OFFSET;
	}

	if (bUseHighRes)
	{
		nEndHBL = nStartHBL + SCREEN_HEIGHT_HBL_MONO;
	}
	else
	{
		nEndHBL = nStartHBL + SCREEN_HEIGHT_HBL_COLOR;
	}

	/* Reset freq changes position for the next VBL to come */
	LastCycleSync50 = -1;
	LastCycleSync60 = -1;
}


/*-----------------------------------------------------------------------*/
/**
 * Clear the array indicating the border state of each line
 */
static void Video_ClearScreenBorder(void)
{
	memset(ScreenBorderMask, 0, sizeof(int)*MAX_SCANLINES_PER_FRAME);
}


/*-----------------------------------------------------------------------*/
/**
 * Called on VBL, set registers ready for frame
 */
static void Video_ClearOnVBL(void)
{
	/* New screen, so first HBL */
	nHBL = 0;
	OverscanMode = OVERSCANMODE_NONE;

	Video_ResetShifterTimings();

	/* Get screen address pointer, aligned to 256 bytes on ST (ie ignore lowest byte) */
	VideoBase = (Uint32)IoMem_ReadByte(0xff8201)<<16 | (Uint32)IoMem_ReadByte(0xff8203)<<8;
	if (ConfigureParams.System.nMachineType != MACHINE_ST)
	{
		/* on STe 2 aligned, on Falcon 4 aligned, on TT 8 aligned. We do STe. */
		VideoBase |= IoMem_ReadByte(0xff820d) & ~1;
	}
	pVideoRaster = &STRam[VideoBase];
	pSTScreen = pFrameBuffer->pSTScreen;

	Video_StartHBL();
	Video_SetScreenRasters();
	Video_ClearScreenBorder();
	Spec512_StartVBL();
}


/*-----------------------------------------------------------------------*/
/**
 * Get width, height and bpp according to TT-Resolution
 */
void Video_GetTTRes(int *width, int *height, int *bpp)
{
	switch (TTRes)
	{
	 case ST_LOW_RES:   *width = 320;  *height = 200; *bpp = 4; break;
	 case ST_MEDIUM_RES:*width = 640;  *height = 200; *bpp = 2; break;
	 case ST_HIGH_RES:  *width = 640;  *height = 400; *bpp = 1; break;
	 case TT_LOW_RES:   *width = 320;  *height = 480; *bpp = 8; break;
	 case TT_MEDIUM_RES:*width = 640;  *height = 480; *bpp = 4; break;
	 case TT_HIGH_RES:  *width = 1280; *height = 960; *bpp = 1; break;
	 default:
		fprintf(stderr, "TT res error!\n");
		*width = 320; *height = 200; *bpp = 4;
		break;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Convert TT palette to SDL palette
 */
static void Video_UpdateTTPalette(int bpp)
{
	Uint32 ttpalette, src, dst;
	Uint8 r,g,b, lowbyte, highbyte;
	Uint16 stcolor, ttcolor;
	int i, offset, colors;

	ttpalette = 0xff8400;

	if (!bTTColorsSTSync)
	{
		/* sync TT ST-palette to TT-palette */
		src = 0xff8240;	/* ST-palette */
		offset = (IoMem_ReadWord(0xff8262) & 0x0f);
		/*fprintf(stderr, "offset: %d\n", offset);*/
		dst = ttpalette + offset * 16*SIZE_WORD;

		for (i = 0; i < 16; i++)
		{
			stcolor = IoMem_ReadWord(src);
			ttcolor = ((stcolor&0x700) << 1) | ((stcolor&0x70) << 1) | ((stcolor&0x7) << 1);
			IoMem_WriteWord(dst, ttcolor);
			src += SIZE_WORD;
			dst += SIZE_WORD;
		}
		bTTColorsSTSync = TRUE;
	}

	colors = 1 << bpp;
	if (bpp == 1)
	{
		/* Monochrome mode... palette is hardwired (?) */
		HostScreen_setPaletteColor(0, 255, 255, 255);
		HostScreen_setPaletteColor(1, 0, 0, 0);
	}
	else
	{
		for (i = 0; i < colors; i++)
		{
			lowbyte = IoMem_ReadByte(ttpalette++);
			highbyte = IoMem_ReadByte(ttpalette++);
			r = (lowbyte  & 0x0f) << 4;
			g = (highbyte & 0xf0);
			b = (highbyte & 0x0f) << 4;
			//printf("%d: (%d,%d,%d)\n", i,r,g,b);
			HostScreen_setPaletteColor(i, r,g,b);
		}
	}

	HostScreen_updatePalette(colors);
	bTTColorsSync = TRUE;
}


/*-----------------------------------------------------------------------*/
/**
 * Update TT palette and blit TT screen using VIDEL code
 */
static void Video_RenderTTScreen(void)
{
	static int nPrevTTRes = -1;
	int width, height, bpp;

	Video_GetTTRes(&width, &height, &bpp);
	if (TTRes != nPrevTTRes)
	{
		HostScreen_setWindowSize(width, height, 8);
		nPrevTTRes = TTRes;
		if (bpp == 1)   /* Assert that mono palette will be used in mono mode */
			bTTColorsSync = FALSE;
	}

	/* colors need synching? */
	if (!(bTTColorsSync && bTTColorsSTSync))
	{
		Video_UpdateTTPalette(bpp);
	}

	/* Yes, we are abusing the Videl routines for rendering the TT modes! */
	if (!HostScreen_renderBegin())
		return;
	if (ConfigureParams.Screen.bZoomLowRes)
		VIDEL_ConvertScreenZoom(width, height, bpp, width * bpp / 16);
	else
		VIDEL_ConvertScreenNoZoom(width, height, bpp, width * bpp / 16);
	HostScreen_renderEnd();
	HostScreen_update1(FALSE);
}


/*-----------------------------------------------------------------------*/
/**
 * Draw screen (either with ST/STE shifter drawing functions or with
 * Videl drawing functions)
 */
static void Video_DrawScreen(void)
{
	/* Skip frame if need to */
	if (nVBLs % (ConfigureParams.Screen.nFrameSkips+1))
		return;

	/* Use extended VDI resolution?
	 * If so, just copy whole screen on VBL rather than per HBL */
	if (bUseVDIRes)
		Video_CopyVDIScreen();

	/* Now draw the screen! */
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON && !bUseVDIRes)
	{
		VIDEL_renderScreen();
	}
	else if (ConfigureParams.System.nMachineType == MACHINE_TT && !bUseVDIRes)
	{
		Video_RenderTTScreen();
	}
	else
		Screen_Draw();
}


/*-----------------------------------------------------------------------*/
/**
 * Start HBL and VBL interrupts.
 */
void Video_StartInterrupts(void)
{
	/* Set int to cycle 376+28 or 372+28 because nCyclesPerLine is 512 or 508 */
	/* (this also means int is set to 512-108 or 508-108 depending on the current freq) */
	Int_AddAbsoluteInterrupt(nCyclesPerLine - ( CYCLES_PER_LINE_50HZ - LINE_END_CYCLE_50 ) + TIMERB_VIDEO_CYCLE_OFFSET - VblVideoCycleOffset,
	                         INT_CPU_CYCLE, INTERRUPT_VIDEO_ENDLINE);
	Int_AddAbsoluteInterrupt(nCyclesPerLine + HBL_VIDEO_CYCLE_OFFSET - VblVideoCycleOffset,
	                         INT_CPU_CYCLE, INTERRUPT_VIDEO_HBL);
	Int_AddAbsoluteInterrupt(CYCLES_PER_FRAME, INT_CPU_CYCLE, INTERRUPT_VIDEO_VBL);
}


/*-----------------------------------------------------------------------*/
/**
 * VBL interrupt, draw screen and reset counters
 */
void Video_InterruptHandler_VBL(void)
{
	int PendingCyclesOver;

	/* Store cycles we went over for this frame(this is our inital count) */
	PendingCyclesOver = -INT_CONVERT_FROM_INTERNAL ( PendingInterruptCount , INT_CPU_CYCLE );    /* +ve */

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Start VBL & HBL interrupts */
	Video_StartInterrupts();

	/* Set frame cycles, used for Video Address */
	Cycles_SetCounter(CYCLES_COUNTER_VIDEO, PendingCyclesOver + VblVideoCycleOffset);

	/* Clear any key presses which are due to be de-bounced (held for one ST frame) */
	Keymap_DebounceAllKeys();
	/* Act on shortcut keys */
	ShortCut_ActKey();

	Video_DrawScreen();

	/* Check printer status */
	Printer_CheckIdleStatus();

	/* Update counter for number of screen refreshes per second */
	nVBLs++;
	/* Set video registers for frame */
	Video_ClearOnVBL();
	/* Store off PSG registers for YM file, is enabled */
	YMFormat_UpdateRecording();
	/* Generate 1/50th second of sound sample data, to be played by sound thread */
	Sound_Update_VBL();

	HATARI_TRACE ( HATARI_TRACE_VIDEO_VBL , "VBL %d video_cyc=%d pending_cyc=%d\n" ,
	               nVBLs , Cycles_GetCounter(CYCLES_COUNTER_VIDEO) , PendingCyclesOver );

	M68000_Exception ( EXCEPTION_VBLANK , M68000_INT_VIDEO );	/* Vertical blank interrupt, level 4! */

	/* And handle any messages, check for quit message */
	Main_EventHandler();         /* Process messages, set 'bQuitProgram' if user tries to quit */
	if (bQuitProgram)
	{
		/* Pass NULL interrupt function to quit cleanly */
		Int_AddAbsoluteInterrupt(4, INT_CPU_CYCLE, INTERRUPT_NULL);
		M68000_SetSpecial(SPCFLAG_BRK);   /* Assure that CPU core shuts down */
	}

	Main_WaitOnVbl();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset video chip
 */
void Video_Reset(void)
{
	/* NOTE! Must reset all of these register type things here!!!! */

	/* Are we in high-res? */
	if (bUseHighRes)
		VideoShifterByte = ST_HIGH_RES;    /* Boot up for mono monitor */
	else
		VideoShifterByte = ST_LOW_RES;
	if (bUseVDIRes)
		VideoShifterByte = VDIRes;

	/* Set system specific timings */
	Video_SetSystemTimings();

	/* Reset VBL counter */
	nVBLs = 0;
	/* Reset addresses */
	VideoBase = 0L;
	/* Reset STe screen variables */
	LineWidth = 0;
	HWScrollCount = 0;
	bSteBorderFlag = FALSE;

	NewLineWidth = -1;			/* cancel pending modifications set before the reset */
	NewHWScrollCount = -1;

	/* Clear ready for new VBL */
	Video_ClearOnVBL();
}


/*-----------------------------------------------------------------------*/
/**
 * Write to video address base high and med register (0xff8201 and 0xff8203).
 * When a program writes to these registers, some other video registers
 * are reset to zero.
 */
void Video_ScreenBaseSTE_WriteByte(void)
{
	IoMem[0xff820d] = 0;          /* Reset screen base low register */

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_VIDEO_STE ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "write ste video base=%x video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" , (IoMem[0xff8201]<<16)+(IoMem[0xff8203]<<8) ,
		                     nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Read video address counter and update ff8205/07/09
 */
void Video_ScreenCounter_ReadByte(void)
{
	Uint32 addr;

	addr = Video_CalculateAddress();		/* get current video address */
	IoMem[0xff8205] = ( addr >> 16 ) & 0xff;
	IoMem[0xff8207] = ( addr >> 8 ) & 0xff;
	IoMem[0xff8209] = addr & 0xff;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to video address counter (0xff8205, 0xff8207 and 0xff8209).
 * Called on STE only and like with base address, you cannot set lowest bit.
 * If display has not started yet for this line, we can change pVideoRaster now.
 * Else we store the new value of the Hi/Med/Lo address to change it at the end
 * of the current line when Video_CopyScreenLineColor is called.
 * We must change only the byte that was modified and keep the two others ones.
 */
void Video_ScreenCounter_WriteByte(void)
{
	Uint8 AddrByte;
	Uint32 addr;
	int nFrameCycles;
	int nLineCycles;
	int HblCounterVideo;

	nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);;
	nLineCycles = nFrameCycles % nCyclesPerLine;

	/* Get real video line count (can be different from nHBL) */
	HblCounterVideo = nFrameCycles / nCyclesPerLine;

	AddrByte = IoMem[ IoAccessCurrentAddress ];

	/* If display has not started, we can still modify pVideoRaster */
	/* We must also check the write does not overlap the end of the line */
	if ( ( ( nLineCycles <= LINE_START_CYCLE_50 ) && ( nHBL == HblCounterVideo ) )
		|| ( nHBL < nStartHBL ) || ( nHBL >= nEndHBL ) )
	{
		addr = Video_CalculateAddress();		/* get current video address */
		if ( IoAccessCurrentAddress == 0xff8205 )
			addr = ( addr & 0x00ffff ) | ( AddrByte << 16 );
		else if ( IoAccessCurrentAddress == 0xff8207 )
			addr = ( addr & 0xff00ff ) | ( AddrByte << 8 );
		else if ( IoAccessCurrentAddress == 0xff8209 )
			addr = ( addr & 0xffff00 ) | ( AddrByte );

		pVideoRaster = &STRam[addr & ~1];		/* set new video address */
	}

	/* Can't change pVideoRaster now, store the modified byte for Video_CopyScreenLineColor */
	else
	{
		if ( IoAccessCurrentAddress == 0xff8205 )
			NewVideoHi = AddrByte;
		else if ( IoAccessCurrentAddress == 0xff8207 )
			NewVideoMed = AddrByte;
		else if ( IoAccessCurrentAddress == 0xff8209 )
			NewVideoLo = AddrByte;
	}

	HATARI_TRACE ( HATARI_TRACE_VIDEO_STE , "write ste video %x val=0x%x video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n" ,
				IoAccessCurrentAddress, AddrByte,
				nFrameCycles, nLineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
}

/*-----------------------------------------------------------------------*/
/**
 * Read video sync register (0xff820a)
 */
void Video_Sync_ReadByte(void)
{
	/* Nothing... */
}

/*-----------------------------------------------------------------------*/
/**
 * Read video base address low byte (0xff820d). A plain ST can only store
 * screen addresses rounded to 256 bytes (i.e. no lower byte).
 */
void Video_BaseLow_ReadByte(void)
{
	if (ConfigureParams.System.nMachineType == MACHINE_ST)
		IoMem[0xff820d] = 0;        /* On ST this is always 0 */

	/* Note that you should not do anything here for STe because
	 * VideoBase address is set in an interrupt and would be wrong
	 * here.   It's fine like this.
	 */
}

/*-----------------------------------------------------------------------*/
/**
 * Read video line width register (0xff820f)
 */
void Video_LineWidth_ReadByte(void)
{
	if (ConfigureParams.System.nMachineType == MACHINE_ST)
		IoMem[0xff820f] = 0;        /* On ST this is always 0 */
	else
		IoMem[0xff820f] = LineWidth;
}

/*-----------------------------------------------------------------------*/
/**
 * Read video shifter mode register (0xff8260)
 */
void Video_ShifterMode_ReadByte(void)
{
	if (bUseHighRes)
		IoMem[0xff8260] = 2;                  /* If mono monitor, force to high resolution */
	else
		IoMem[0xff8260] = VideoShifterByte;   /* Read shifter register */
}

/*-----------------------------------------------------------------------*/
/**
 * Read horizontal scroll register (0xff8265)
 */
void Video_HorScroll_Read(void)
{
	IoMem[0xff8265] = HWScrollCount;
}


/*-----------------------------------------------------------------------*/
/**
 * Write video line width register (0xff820f) - STE only.
 * Content of LineWidth is added to the shifter counter when display is
 * turned off (start of the right border, usually at cycle 376)
 */
void Video_LineWidth_WriteByte(void)
{
	Uint8 NewWidth;
	int nFrameCycles;
	int nLineCycles;
	int HblCounterVideo;

	nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);;
	nLineCycles = nFrameCycles % nCyclesPerLine;

	/* Get real video line count (can be different from nHBL) */
	HblCounterVideo = nFrameCycles / nCyclesPerLine;

	NewWidth = IoMem_ReadByte(0xff820f);

	/* We must also check the write does not overlap the end of the line */
	if ( ( ( nLineCycles <= LineEndCycle ) && ( nHBL == HblCounterVideo ) )
		|| ( nHBL < nStartHBL ) || ( nHBL >= nEndHBL ) )
		LineWidth = NewWidth;		/* display is on, we can still change */
	else
		NewLineWidth = NewWidth;	/* display is off, can't change LineWidth once in right border */

	HATARI_TRACE ( HATARI_TRACE_VIDEO_STE , "write ste linewidth=0x%x video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n" , NewWidth,
					nFrameCycles, nLineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
}

/*-----------------------------------------------------------------------*/
/**
 * Write to video shifter palette registers (0xff8240-0xff825e)
 */
static void Video_ColorReg_WriteWord(Uint32 addr)
{
	if (!bUseHighRes)                          /* Don't store if hi-res */
	{
		int idx;
		Uint16 col;
		Video_SetHBLPaletteMaskPointers();     /* Set 'pHBLPalettes' etc.. according cycles into frame */
		col = IoMem_ReadWord(addr);
		if (ConfigureParams.System.nMachineType == MACHINE_ST)
			col &= 0x777;                      /* Mask off to ST 512 palette */
		else
			col &= 0xfff;                      /* Mask off to STe 4096 palette */
		IoMem_WriteWord(addr, col);            /* (some games write 0xFFFF and read back to see if STe) */
		Spec512_StoreCyclePalette(col, addr);  /* Store colour into CyclePalettes[] */
		idx = (addr-0xff8240)/2;               /* words */
		pHBLPalettes[idx] = col;               /* Set colour x */
		*pHBLPaletteMasks |= 1 << idx;         /* And mask */

		if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_VIDEO_COLOR ) )
		{
			int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
			int nLineCycles = nFrameCycles % nCyclesPerLine;
			HATARI_TRACE_PRINT ( "write col addr=%x col=%x video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" , addr, col,
			                     nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
		}

	}
}

void Video_Color0_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8240);
}

void Video_Color1_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8242);
}

void Video_Color2_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8244);
}

void Video_Color3_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8246);
}

void Video_Color4_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8248);
}

void Video_Color5_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff824a);
}

void Video_Color6_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff824c);
}

void Video_Color7_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff824e);
}

void Video_Color8_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8250);
}

void Video_Color9_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8252);
}

void Video_Color10_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8254);
}

void Video_Color11_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8256);
}

void Video_Color12_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff8258);
}

void Video_Color13_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff825a);
}

void Video_Color14_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff825c);
}

void Video_Color15_WriteWord(void)
{
	Video_ColorReg_WriteWord(0xff825e);
}


/*-----------------------------------------------------------------------*/
/**
 * Write video shifter mode register (0xff8260)
 */
void Video_ShifterMode_WriteByte(void)
{
	if (ConfigureParams.System.nMachineType == MACHINE_TT)
	{
		TTRes = IoMem_ReadByte(0xff8260) & 7;
		IoMem_WriteByte(0xff8262, TTRes);           /* Copy to TT shifter mode register */
	}
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
	{
		/* - activate STE palette
		 * - TODO: set line width ($8210)
		 * - TODO: sets paramaters in $82c2 (double lines/interlace & cycles/pixel)
		 */
		bUseSTShifter = TRUE;
	}
	if (!bUseHighRes && !bUseVDIRes)                    /* Don't store if hi-res and don't store if VDI resolution */
	{
		VideoShifterByte = IoMem[0xff8260] & 3;     /* We only care for lower 2-bits */
		Video_WriteToShifter(VideoShifterByte);
		Video_SetHBLPaletteMaskPointers();
		*pHBLPaletteMasks &= 0xff00ffff;
		/* Store resolution after palette mask and set resolution write bit: */
		*pHBLPaletteMasks |= (((Uint32)VideoShifterByte|0x04)<<16);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Write to horizontal scroll register (0xff8265).
 * Note: The STE shifter has a funny "bug"  that allows to increase the
 * resolution to 336x200 instead of 320x200. It occurs when a program writes
 * certain values to 0xff8264:
 *	move.w  #1,$ffff8264      ; Word access!
 *	clr.b   $ffff8264         ; Byte access!
 * Some games (Obsession, Skulls) and demos (Pacemaker by Paradox) use this
 * feature to increase the resolution, so we have to emulate this bug, too!
 */
void Video_HorScroll_Write(void)
{
	static bool bFirstSteAccess = FALSE;
	Uint8 ScrollCount;
	int nFrameCycles;
	int nLineCycles;
	int HblCounterVideo;

	nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);;
	nLineCycles = nFrameCycles % nCyclesPerLine;

	/* Get real video line count (can be different from nHBL) */
	HblCounterVideo = nFrameCycles / nCyclesPerLine;

	ScrollCount = IoMem[0xff8265];
	ScrollCount &= 0x0f;

	/* We must also check the write does not overlap the end of the line */
	if ( ( ( nLineCycles <= LINE_START_CYCLE_50 ) && ( nHBL == HblCounterVideo ) )
		|| ( nHBL < nStartHBL ) || ( nHBL >= nEndHBL ) )
		HWScrollCount = ScrollCount;		/* display has not started, we can still change */
	else
		NewHWScrollCount = ScrollCount;		/* display has started, can't change HWScrollCount now */

	HATARI_TRACE ( HATARI_TRACE_VIDEO_STE , "write ste hwscroll=%x video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n" , ScrollCount,
		                     nFrameCycles, nLineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );

	/*fprintf(stderr, "Write to 0x%x (0x%x, 0x%x, %i)\n", IoAccessBaseAddress,
	        IoMem[0xff8264], ScrollCount, nIoMemAccessSize);*/

	if (IoAccessBaseAddress == 0xff8264 && nIoMemAccessSize == SIZE_WORD
	        && ScrollCount == 1)
	{
		/*fprintf(stderr, "STE border removal - access 1\n");*/
		bFirstSteAccess = TRUE;
	}
	else if (bFirstSteAccess && ScrollCount == 1 &&
	         IoAccessBaseAddress == 0xff8264 && nIoMemAccessSize == SIZE_BYTE)
	{
		/*fprintf(stderr, "STE border removal - access 2\n");*/
		bSteBorderFlag = TRUE;
		HATARI_TRACE ( HATARI_TRACE_VIDEO_BORDER_H , "detect STE left+8\n" );
	}
	else
	{
		bFirstSteAccess = bSteBorderFlag = FALSE;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Write to TT shifter mode register (0xff8262)
 */
void Video_TTShiftMode_WriteWord(void)
{
	TTRes = IoMem_ReadByte(0xff8262) & 7;

	/*fprintf(stderr, "Write to FF8262: %x, res=%i\n", IoMem_ReadWord(0xff8262), TTRes);*/

	/* Is it an ST compatible resolution? */
	if (TTRes <= 2)
	{
		IoMem_WriteByte(0xff8260, TTRes);
		Video_ShifterMode_WriteByte();
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Write to TT color register (0xff8400)
 */
void Video_TTColorRegs_WriteWord(void)
{
	bTTColorsSync = FALSE;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to ST color register on TT (0xff8400)
 */
void Video_TTColorSTRegs_WriteWord(void)
{
	bTTColorsSTSync = FALSE;
}
