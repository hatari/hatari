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
/* 2007/05/11	[NP]	Add support for med res overscan (No Cooper Greetings).			*/
/* 2007/05/12	[NP]	- LastCycleSync50 and LastCycleSync60 for better top border's removal	*/
/*			in Video_EndHBL.							*/
/*			- Use VideoOffset in Video_CopyScreenLineColor to handle missing planes	*/
/*			depending on line (med/lo and borders).					*/
/* 2007/09/25	[NP]	Replace printf by calls to HATARI_TRACE.				*/
/* 2007/10/02	[NP]	Use the new int.c to add interrupts with INT_CPU_CYCLE / INT_MFP_CYCLE. */
/* 2007/10/23	[NP]	Add support for 0 byte line (60/50 switch at cycle 56). Allow 5 lines	*/
/*			hardscroll (e.g. SHFORSTV.EXE by Paulo Simmoes).			*/
/* 2007/10/31	[NP]	Use BORDERMASK_LEFT_OFF_MED when left border is removed with hi/med	*/
/*			switch (ST CNX in PYM).							*/
/* 2007/11/02	[NP]	Add support for 4 pixel hardware scrolling ("Let's Do The Twist" by	*/
/*			ST CNX in Punish Your Machine).						*/
/* 2007/11/05	[NP]	Depending on the position of the med res switch, the planes will be	*/
/*			shifted when doing med res overscan (Best Part Of the Creation in PYM	*/
/*			or No Cooper Greetings).						*/
/* 2007/11/30	[NP]	A hi/med switch to remove the left border can be either	used to initiate*/
/*			a right hardware scrolling in low res (St Cnx) or a complete med res	*/
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
/* 2008/06/07	[NP]	In Video_SetHBLPaletteMaskPointers, use LineStartCycle instead of the	*/
/*			50 Hz constant SCREEN_START_CYCLE.					*/
/*			Rename SCREEN_START_HBL_xxx to VIDEO_START_HBL_xxx.			*/
/*			Rename SCREEN_END_HBL_xxx to VIDEO_END_HBL_xxx.				*/
/*			Rename SCREEN_HEIGHT_HBL_xxx to VIDEO_HEIGHT_HBL_xxx.			*/
/*			Use VIDEO_HEIGHT_BOTTOM_50HZ instead of OVERSCAN_BOTTOM.		*/
/* 2008/06/16	[NP]	When Hatari is configured to display the screen's borders, 274 lines	*/
/*			will be rendered on screen, but if the shifter is in 60 Hz, the last	*/
/*			16 lines will never be used, which can leave some bad pixels on		*/
/*			screen. We clear the remaining lines before calling 'Screen_Draw'.	*/
/*			(in FNIL by Delta Force, fix flickering gfx in the bottom border of the */
/*			F2 screen : last 16 lines were the ones from the menu where bottom	*/
/*			border was removed ).							*/
/* 2008/06/26	[NP]	Improve STE scrolling : handle $ff8264 (no prefetch) and $ff8265	*/
/*			(prefetch). See Video_HorScroll_Write for details on both registers.	*/
/*			More generic support for starting display 16 pixels earlier on STE	*/
/*			by writing to $ff8265 and settting $ff8264=0 just after.		*/
/*			(fix Digiworld 2 by ICE, which uses $ff8264 for horizontal scroll).	*/
/* 2008/07/07	[NP]	Ignore other 50/60 Hz switches once the right border was removed, keep	*/
/*			the timer B to occur at pos 460+28 (fix Oxygene screen in Transbeauce 2)*/
/* 2008/07/14	[NP]	When removing only left border in 60Hz, line size is 26+158 bytes	*/
/*			instead of 26+160 bytes in 50 Hz (HigResMode demo by Paradox).		*/
/* 2008/07/19	[NP]	If $ff8260==3 (which is not a valid resolution mode), we use 0 instead	*/
/*			(low res) (fix Omegakul screen in old Omega Demo from 1988).		*/
/* 2008/09/05	[NP]	No need to test 60/50 switch if HblCounterVideo < nStartHBL (display	*/
/*			has not started yet).							*/
/* 2008/09/25	[NP]	Use nLastVisibleHbl to store the number of the last hbl line that should*/
/*			be copied to the emulator's screen buffer.				*/
/*			On STE, allow to change immediatly video address, hw scroll and		*/
/*			linewidth when nHBL>=nLastVisibleHbl instead of nHBL>=nEndHBL		*/
/*			(fix Power Rise / Xtrem D demo).					*/
/* 2008/11/15	[NP]	For STE registers, add in the TRACE call if the write is delayed or	*/
/*			not (linewidth, hwscroll, video address).				*/
/*			On STE, allow to change linewdith, hwscroll and video address with no	*/
/*			delay as soon as nHBL >= nEndHBL (revert previous changes). Power Rise	*/
/*			is still working due to NewHWScrollCount=-1 when setting immediate	*/
/*			hwscroll. Fix regression in Braindamage.				*/
/* 2008/11/29	[NP]	Increment jitter's index for HBL and VBL each time a possible interrupt	*/
/*			occurs. Each interrupt can have a jitter between 0, 4 and 8 cycles ; the*/
/*			jitter follows a predefined pattern of 5 values. The HBL and the VBL	*/
/*			have their own pattern. See InterruptAddJitter() in uae-cpu/newcpu.c	*/
/*			(fix Fullscreen tunnel in Suretrip 49% by Checkpoint and digi sound in	*/
/*			Swedish New Year's TCB screen).						*/
/* 2008/12/10	[NP]	Enhance support for 0 byte line. The 60/50 Hz switch can happen at	*/
/*			cycles 56/64, but also at 58/66 (because access to $ff820a doesn't	*/
/*			require to be on a 4 cycles boundary). As hatari doesn't handle		*/
/*			multiple of 2 cycles, we allow cycles 56/64 and 60/68 (fix nosync.tos	*/
/*			that uses the STOP instruction to produce a 0 byte line on the first	*/
/*			displayed line (found on atari-forum.com)).				*/
/* 2008/12/26	[NP]	When reading $ff8260 on ST/STE, set unused bits to 1 instead of 0	*/
/*			(fix wrong TOS resolution in Awesome Menu Disk 16).			*/
/*			Set unused bit to 1 when reading $ff820a too.				*/
/* 2009/01/16	[NP]	Handle special case when writing only in upper byte of a color reg.	*/
/* 2009/01/21	[NP]	Implement STE horizontal scroll for medium res (fixes cool_ste.prg).	*/
/*			Take the current res into account in Video_CopyScreenLineColor to	*/
/*			allow mixing low/med res with horizontal scroll on STE.			*/	
/* 2009/01/24	[NP]	Better detection of 'right-2' when freq is changed to 60 Hz and 	*/
/*			restored to 50 after the end of the current line (fixes games menu on	*/
/*			BBC compil 10).								*/
/* 2009/01/31	[NP]	Handle a rare case where 'move.b #8,$fffa1f' to start the timer B is	*/
/*			done just a few cycles before the actual signal for end of line. In that*/
/*			case we must ensure that the write was really effective before the end	*/
/*			of line (else no interrupt should be made) (fix Pompey Pirate Menu #57).*/
/* 2009/02/08	[NP]	Handle special case for simultaneous HBL exceptions (fixes flickering in*/
/*			Monster	Business and Super Monaco GP).					*/
/* 2009/02/25	[NP]	Ignore other 50/60 Hz switches after display was stopped in the middle	*/
/*			of the line with a hi/lo switch. Correct missing end of line timer B	*/
/*			interrupt in that case (fix flickering Dragon Ball part in Blood disk 2	*/
/*			by Holocaust).								*/
/* 2008/02/02	[NP]	Added 0 byte line detection in STE mode when switching hi/lo res	*/
/*			at position 32 (Lemmings screen in Nostalgic-o-demo).			*/
/* 2009/03/28	[NP]	Depending on bit 3 of MFP's AER, timer B will count end of line events	*/
/*			(bit=0) or start of line events (bit=1) (fix Seven Gates Of Jambala).	*/
/* 2009/04/02	[NP]	Add another method to obtain a 0 byte line, by switching to hi/lo res	*/
/*			at position 500/508 (fix the game No Buddies Land).			*/
/* 2009/04/xx	[NP]	Rewrite of many parts : add SHIFTER_FRAME structure, better accuracy	*/
/*			when mixing 50/60 Hz lines and reading $ff8209, better emulation of	*/
/*			HBL and Timer B position when changing freq/res, better emulation of	*/
/*			freq changes for top/bottom/right borders.				*/
/* 2009/07/16	[NP]	In Video_SetHBLPaletteMaskPointers, if LineCycle>460 we consider the	*/
/*			color's change should be applied to next line (used when spec512 mode	*/
/*			if off).								*/



const char Video_fileid[] = "Hatari video.c : " __DATE__ " " __TIME__;

#include <SDL_endian.h>

#include "main.h"
#include "configuration.h"
#include "cycles.h"
#include "fdc.h"
#include "int.h"
#include "ioMem.h"
#include "keymap.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "printer.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "shortcut.h"
#include "sound.h"
#include "spec512.h"
#include "stMemory.h"
#include "vdi.h"
#include "video.h"
#include "ymFormat.h"
#include "falcon/videl.h"
#include "falcon/hostscreen.h"


/* The border's mask allows to keep track of all the border tricks		*/
/* applied to one video line. The masks for all lines are stored in the array	*/
/* ScreenBorderMask[].								*/
/* - bits 0-15 are used to describe the border tricks.				*/
/* - bits 20-23 are used to store the bytes offset to apply for some particular	*/
/*   tricks (for example med res overscan can shift display by 0 or 2 bytes	*/
/*   depending on when the switch to med res is done after removing the left	*/
/*   border).									*/

#define BORDERMASK_NONE			0x00	/* no effect on this line */
#define BORDERMASK_LEFT_OFF		0x01	/* removal of left border with hi/lo res switch -> +26 bytes */
#define BORDERMASK_LEFT_PLUS_2		0x02	/* line starts earlier in 60 Hz -> +2 bytes */
#define BORDERMASK_STOP_MIDDLE		0x04	/* line ends in hires at cycle 160 -> -106 bytes */
#define BORDERMASK_RIGHT_MINUS_2	0x08	/* line ends earlier in 60 Hz -> -2 bytes */
#define BORDERMASK_RIGHT_OFF		0x10	/* removal of right border -> +44 bytes */
#define BORDERMASK_RIGHT_OFF_FULL	0x20	/* full removal of right border and next left border -> +22 bytes */
#define BORDERMASK_OVERSCAN_MED_RES	0x40	/* some borders were removed and the line is in med res instead of low res */
#define BORDERMASK_EMPTY_LINE		0x80	/* 60/50 Hz switch prevents the line to start, video counter is not incremented */
#define BORDERMASK_LEFT_OFF_MED		0x100	/* removal of left border with hi/med res switch -> +26 bytes (for 4 pixels hardware scrolling) */


int STRes = ST_LOW_RES;                         /* current ST resolution */
int TTRes;                                      /* TT shifter resolution mode */
int nFrameSkips = 0;				/* speed up by skipping video frames */

bool bUseSTShifter;                             /* Falcon: whether to use ST palette */
bool bUseHighRes;                               /* Use hi-res (ie Mono monitor) */
int OverscanMode;                               /* OVERSCANMODE_xxxx for current display frame */
Uint16 HBLPalettes[(NUM_VISIBLE_LINES+1)*16];   /* 1x16 colour palette per screen line, +1 line just incase write after line 200 */
Uint16 *pHBLPalettes;                           /* Pointer to current palette lists, one per HBL */
Uint32 HBLPaletteMasks[NUM_VISIBLE_LINES+1];    /* Bit mask of palette colours changes, top bit set is resolution change */
Uint32 *pHBLPaletteMasks;
int nScreenRefreshRate = 50;                    /* 50 or 60 Hz in color, 71 Hz in mono */
Uint32 VideoBase;                               /* Base address in ST Ram for screen (read on each VBL) */

int nVBLs;                                      /* VBL Counter */
int nHBL;                                       /* HBL line */
int nStartHBL;                                  /* Start HBL for visible screen */
int nEndHBL;                                    /* End HBL for visible screen */
int nScanlinesPerFrame = 313;                   /* Number of scan lines per frame */
int nCyclesPerLine = 512;                       /* Cycles per horizontal line scan */
static int nFirstVisibleHbl = FIRST_VISIBLE_HBL_50HZ;			/* The first line of the ST screen that is copied to the PC screen buffer */
static int nLastVisibleHbl = FIRST_VISIBLE_HBL_50HZ+NUM_VISIBLE_LINES;	/* The last line of the ST screen that is copied to the PC screen buffer */

static Uint8 HWScrollCount;			/* HW scroll pixel offset, STE only (0...15) */
static int NewHWScrollCount = -1;		/* Used in STE mode when writing to the scrolling registers $ff8264/65 */
static Uint8 HWScrollPrefetch;			/* 0 when scrolling with $ff8264, 1 when scrolling with $ff8265 */
static int NewHWScrollPrefetch = -1;		/* Used in STE mode when writing to the scrolling registers $ff8264/65 */
static Uint8 LineWidth;				/* Scan line width add, STe only (words, minus 1) */
static int NewLineWidth = -1;			/* Used in STE mode when writing to the line width register $ff820f */
static Uint8 *pVideoRaster;			/* Pointer to Video raster, after VideoBase in PC address space. Use to copy data on HBL */
static Uint8 VideoShifterByte;			/* VideoShifter (0xff8260) value store in video chip */
static bool bSteBorderFlag;			/* true when screen width has been switched to 336 (e.g. in Obsession) */
static int NewSteBorderFlag = -1;		/* New value for next line */
static bool bTTColorsSync, bTTColorsSTSync;	/* whether TT colors need convertion to SDL */

int	LastCycleScroll8264;			/* value of Cycles_GetCounterOnWriteAccess last time ff8264 was set for the current VBL */
int	LastCycleScroll8265;			/* value of Cycles_GetCounterOnWriteAccess last time ff8265 was set for the current VBL */

int	LastCycleHblException;			/* value of Cycles_GetCounter last time a level 2 interrupt was executed by the CPU */

int	NewVideoHi = -1;			/* new value for $ff8205 on STE */
int	NewVideoMed = -1;			/* new value for $ff8207 on STE */
int	NewVideoLo = -1;			/* new value for $ff8209 on STE */

int	LineTimerBCycle = LINE_END_CYCLE_50 + TIMERB_VIDEO_CYCLE_OFFSET;	/* position of the Timer B interrupt on active lines */
int	TimerBEventCountCycleStart = -1;	/* value of Cycles_GetCounterOnWriteAccess last time timer B was started for the current VBL */

int	LineRemoveTopCycle = LINE_REMOVE_TOP_CYCLE_STF;
int	LineRemoveBottomCycle = LINE_REMOVE_BOTTOM_CYCLE_STF;
int	RestartVideoCounterCycle = RESTART_VIDEO_COUNTER_CYCLE_STF;
int	VblVideoCycleOffset = VBL_VIDEO_CYCLE_OFFSET_STF;

#define HBL_JITTER_MAX_POS 5
int	HblJitterIndex = 0;
int	HblJitterArray[] = 		{ 8,4,4,0,0 };		/* measured on STF */
int	HblJitterArrayPending[] =	{ 4,4,4,4,4 };	// { 8,8,12,8,12 };	/* measured on STF, not always accurate */
#define VBL_JITTER_MAX_POS 5
int	VblJitterIndex = 0;
int	VblJitterArray[] = 		{ 8,0,4,0,4 };		/* measured on STF */
int	VblJitterArrayPending[] =	{ 8,8,12,8,12 };	/* not verified on STF, use the same as HBL */



typedef struct
{
	int	VBL;				/* VBL for this Pos (or -1 if Pos not defined for now) */
	int	FrameCycles;			/* Number of cycles since this VBL */
	int	HBL;				/* HBL in the VBL */
	int	LineCycles;			/* cycles in the HBL */
} SHIFTER_POS;


typedef struct 
{
	int	StartCycle;			/* first cycle of this line, as returned by Cycles_GetCounter */

	Uint32	BorderMask;			/* borders' states for this line */
	int	DisplayPixelShift;		/* number of pixels to shift the whole line (<0 shift to the left, >0 shift to the right) */
						/* On STF, this is obtained when switching hi/med for a variable number of cycles, */
						/* but just removing left border will shift the line too. */

	int	DisplayStartCycle;		/* cycle where display starts for this line (0-512) : 0, 52 or 56 */
	int	DisplayEndCycle;		/* cycle where display ends for this line (0-512) : 0, 160, 372, 376, 460 or 512 */
	int	DisplayBytes;			/* how many bytes to display for this line */

} SHIFTER_LINE;


typedef struct
{
	int	HBL_CyclePos;			/* cycle position for the HBL int (depends on freq/res) */
	int	TimerB_CyclePos;		/* cycle position for the Timer B int (depends on freq/res) */

	int	Freq;				/* value of ff820a & 2, or -1 if not set */
	int	Res;				/* value of ff8260 & 3, or -1 if not set */
	SHIFTER_POS	FreqPos50;		/* position of latest freq change to 50 Hz*/
	SHIFTER_POS	FreqPos60;		/* position of latest freq change to 60 Hz*/
	SHIFTER_POS	ResPosLo;		/* position of latest change to low res */
	SHIFTER_POS	ResPosMed;		/* position of latest change to med res */
	SHIFTER_POS	ResPosHi;		/* position of latest change to high res */

	SHIFTER_POS	Scroll8264Pos;		/* position of latest write to $ff8264 */
	SHIFTER_POS	Scroll8265Pos;		/* position of latest write to $ff8265 */

	SHIFTER_LINE	ShifterLines[ MAX_SCANLINES_PER_FRAME ];
} SHIFTER_FRAME;


SHIFTER_FRAME	ShifterFrame;



/*--------------------------------------------------------------*/
/* Local functions prototypes                                   */
/*--------------------------------------------------------------*/

static void	Video_SetSystemTimings ( void );

static Uint32	Video_CalculateAddress ( void );
static void	Video_WriteToShifter ( Uint8 Res );
static void 	Video_Sync_SetDefaultStartEnd ( Uint8 Freq , int HblCounterVideo , int LineCycles );

static int	Video_HBL_GetPos ( void );
static void	Video_EndHBL ( void );
static void	Video_StartHBL ( void );

static void	Video_StoreFirstLinePalette(void);
static void	Video_StoreResolution(int y);
static void	Video_CopyScreenLineMono(void);
static void	Video_CopyScreenLineColor(void);
static void	Video_CopyVDIScreen(void);
static void	Video_SetHBLPaletteMaskPointers(void);

static void	Video_UpdateTTPalette(int bpp);
static bool	Video_RenderTTScreen(void);
static void	Video_DrawScreen(void);

static void	Video_ResetShifterTimings(void);
static void	Video_InitShifterLines(void);
static void	Video_ClearOnVBL(void);

static void	Video_AddInterrupt ( int Pos , interrupt_id Handler );
static void	Video_AddInterruptHBL ( int Pos );
static void	Video_AddInterruptTimerB ( int Pos );

static void	Video_ColorReg_WriteWord(Uint32 addr);


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
	MemorySnapShot_Store(&HblJitterIndex, sizeof(HblJitterIndex));
	MemorySnapShot_Store(&VblJitterIndex, sizeof(VblJitterIndex));
	MemorySnapShot_Store(&ShifterFrame, sizeof(ShifterFrame));
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

	/* Reset shifter's state variables */
	ShifterFrame.Freq = -1;
	ShifterFrame.Res = -1;
	ShifterFrame.FreqPos50.VBL = -1;
	ShifterFrame.FreqPos60.VBL = -1;
	ShifterFrame.ResPosLo.VBL = -1;
	ShifterFrame.ResPosMed.VBL = -1;
	ShifterFrame.ResPosHi.VBL = -1;
	ShifterFrame.Scroll8264Pos.VBL = -1;
	ShifterFrame.Scroll8265Pos.VBL = -1;

	Video_InitShifterLines ();

	/* Reset STE screen variables */
	LineWidth = 0;
	HWScrollCount = 0;
	bSteBorderFlag = false;

	NewLineWidth = -1;			/* cancel pending modifications set before the reset */
	NewHWScrollCount = -1;

	/* Reset jitter indexes */
	HblJitterIndex = 0;
	VblJitterIndex = 0;

	/* Clear framecycles counter */
	Cycles_SetCounter(CYCLES_COUNTER_VIDEO, 0);

	/* Clear ready for new VBL */
	Video_ClearOnVBL();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the GLUE chip responsible for generating the H/V sync signals.
 * When the 68000 RESET instruction is called, frequency and resolution
 * should be reset to 0.
 */
void Video_Reset_Glue(void)
{
	IoMem_WriteByte(0xff820a,0);		/* video freq */
	IoMem_WriteByte(0xff8260,0);		/* video res */
}


/*-----------------------------------------------------------------------*/
/*
 * Set specific video timings, depending on the system being emulated.
 */
static void	Video_SetSystemTimings(void)
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
 * Convert the elapsed number of cycles since the start of the VBL
 * into the corresponding HBL number and the cycle position in the current
 * HBL. We use the starting cycle position of the closest HBL to compute
 * the cycle position on the line (this allows to mix lines with different
 * values for nCyclesPerLine).
 * We can have 2 cases on the limit where the real video line count can be
 * different from nHBL :
 * - when reading video address between cycle 0 and 12, LineCycle will be <0,
 *   so we need to use the data from line nHBL-1
 * - if LineCycle >= nCyclesPerLine, this means the HBL int was not processed
 *   yet, so the video line number is in fact nHBL+1
 */

void	Video_ConvertPosition ( int FrameCycles , int *pHBL , int *pLineCycles )
{
#if 0
	/* Get the real video line count (can be different from nHBL) */
	*pHBL = FrameCycles / nCyclesPerLine;

	/* Get the position on the currently displayed line */
	*pLineCycles = FrameCycles % nCyclesPerLine;
#endif

	*pHBL = nHBL;
	*pLineCycles = FrameCycles - ShifterFrame.ShifterLines[ nHBL ].StartCycle;

	if ( *pLineCycles < 0 )					/* reading from the previous video line */
	{
		*pHBL = nHBL-1;
		*pLineCycles = FrameCycles - ShifterFrame.ShifterLines[ nHBL-1 ].StartCycle;
	}
	
	else if ( *pLineCycles >= nCyclesPerLine )		/* reading on the next line, but HBL int was delayed */
	{
		*pHBL = nHBL+1;
		*pLineCycles -= nCyclesPerLine;
	}


if ( *pLineCycles < 0 )
	fprintf ( stderr , "bug nHBL=%d %d %d\n" , nHBL , *pHBL , *pLineCycles );

//if ( ( *pHBL != FrameCycles / nCyclesPerLine ) || ( *pLineCycles != FrameCycles % nCyclesPerLine ) )
//  LOG_TRACE ( TRACE_VIDEO_ADDR , "conv pos %d %d - %d %d\n" , *pHBL , FrameCycles / nCyclesPerLine , *pLineCycles , FrameCycles % nCyclesPerLine );
//  LOG_TRACE ( TRACE_VIDEO_ADDR , "conv pos %d %d %d\n" , FrameCycles , *pHBL , *pLineCycles );
}


void	Video_GetPosition ( int *pFrameCycles , int *pHBL , int *pLineCycles )
{
	*pFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
	Video_ConvertPosition ( *pFrameCycles , pHBL , pLineCycles );
}


void	Video_GetPosition_OnWriteAccess ( int *pFrameCycles , int *pHBL , int *pLineCycles )
{
	*pFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
	Video_ConvertPosition ( *pFrameCycles , pHBL , pLineCycles );
}


void	Video_GetPosition_OnReadAccess ( int *pFrameCycles , int *pHBL , int *pLineCycles )
{
	*pFrameCycles = Cycles_GetCounterOnReadAccess(CYCLES_COUNTER_VIDEO);
	Video_ConvertPosition ( *pFrameCycles , pHBL , pLineCycles );
}


/*-----------------------------------------------------------------------*/
/**
 * Calculate and return video address pointer.
 */
static Uint32 Video_CalculateAddress ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;
	int X, NbBytes;
	Uint32 VideoAddress;      /* Address of video display in ST screen space */
	int nSyncByte;
	int LineBorderMask;
	int PrevSize;
	int CurSize;
	int LineStartCycle , LineEndCycle;
	

	/* Find number of cycles passed during frame */
	/* We need to substract '12' for correct video address calculation */
	FrameCycles = Cycles_GetCounterOnReadAccess(CYCLES_COUNTER_VIDEO) - 12;

	/* Now find which pixel we are on (ignore left/right borders) */
//	X = FrameCycles % nCyclesPerLine;

	/* Get real video line count and position in the line (can be different from nHBL) */
//	HblCounterVideo = FrameCycles / nCyclesPerLine;
	Video_ConvertPosition ( FrameCycles , &HblCounterVideo , &LineCycles );
	X = LineCycles;

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
#if 0
	if (FrameCycles < nStartHBL*nCyclesPerLine)
#else
	if ( HblCounterVideo < nStartHBL )
#endif
	{
		/* pVideoRaster was set during Video_ClearOnVBL using VideoBase */
		/* and it could also have been modified on STE by writing to ff8205/07/09 */
		/* So, we should not use ff8201/ff8203 which are reloaded in ff8205/ff8207 only once per VBL */
		/* but use pVideoRaster - STRam instead to get current shifter video address */
		VideoAddress = pVideoRaster - STRam;
	}

	else if (FrameCycles > RestartVideoCounterCycle)
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
//		X = ( Cycles_GetCounterOnReadAccess(CYCLES_COUNTER_VIDEO) - 12 ) % nCyclesPerLine;

		/* Get real video line count (can be different from nHBL) */
//		HblCounterVideo = ( Cycles_GetCounterOnReadAccess(CYCLES_COUNTER_VIDEO) - 12 ) / nCyclesPerLine;

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
//			LineBorderMask = ScreenBorderMask[ HblCounterVideo-1 ];	/* get border mask for nHBL */
			LineBorderMask = ShifterFrame.ShifterLines[ HblCounterVideo-1 ].BorderMask; /* get border mask for nHBL */
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


//		LineBorderMask = ScreenBorderMask[ HblCounterVideo ];
		LineBorderMask = ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask;

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
			LineStartCycle = LINE_START_CYCLE_71;

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
#if 0
		if (FrameCycles < nEndHBL*nCyclesPerLine)
#else
		if ( HblCounterVideo < nEndHBL )
#endif
			VideoAddress += PrevSize + NbBytes;
	}

	LOG_TRACE(TRACE_VIDEO_ADDR , "video base=%x raster=%x addr=%x video_cyc=%d line_cyc=%d/X=%d @ nHBL=%d/video_hbl=%d %d<->%d pc=%x instr_cyc=%d\n",
	               VideoBase, pVideoRaster - STRam, VideoAddress, Cycles_GetCounter(CYCLES_COUNTER_VIDEO),
	               LineCycles, X,
	               nHBL, HblCounterVideo, LineStartCycle, LineEndCycle, M68000_GetPC(), CurrentInstrCycles );

	return VideoAddress;
}


/*-----------------------------------------------------------------------*/
/**
 * Write to VideoShifter (0xff8260), resolution bits
 */
static void Video_WriteToShifter ( Uint8 Res )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition_OnWriteAccess ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_VIDEO_RES ,"shifter=0x%2.2X video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n",
	               Res, FrameCycles, LineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );


	/* Ignore consecutive writes of the same value */
	if ( Res == ShifterFrame.Res )
		return;						/* do nothing */


	if ( Res == 0x02 )					/* switch to high res */
	{
		if ( LineCycles < ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle )	/* start could be 0,52,56 */
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle = LINE_START_CYCLE_71;

		if ( ( LineCycles < ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle )	/* end could be 160,372,376,460 */
		  && ( LineCycles < LINE_END_CYCLE_71 ) )
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = LINE_END_CYCLE_71;
	}
	else							/* switch to lo/med res */
	{
		/* In lo/med res, display start/end depends on the freq register in $ff820a */
		Video_Sync_SetDefaultStartEnd ( IoMem[0xff820a] & 2 , HblCounterVideo , LineCycles );
	}


	/* Remove left border : +26 bytes */
	/* This can be done with a hi/lo res switch or a hi/med res switch */
	if ( ( ShifterFrame.Res == 0x02 ) && ( Res == 0x00 )	/* switched from hi res to lo res */
	        && ( LineCycles <= (LINE_START_CYCLE_71+28) )
	        && ( FrameCycles - ShifterFrame.ResPosHi.FrameCycles <= 30 ) )
	{
		ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_LEFT_OFF;
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle = LINE_START_CYCLE_71;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect remove left %d<->%d\n" ,
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
	}

	if ( ( ShifterFrame.Res == 0x02 ) && ( Res == 0x01 )	/* switched from hi res to med res */
	        && ( LineCycles <= (LINE_START_CYCLE_71+20) )
	        && ( FrameCycles - ShifterFrame.ResPosHi.FrameCycles <= 30 ) )
	{
		ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_LEFT_OFF_MED;	/* a later switch to low res might gives right scrolling */
		/* By default, this line will be in med res, except if we detect hardware scrolling later */
		ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_OVERSCAN_MED_RES | ( 2 << 20 );
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle = LINE_START_CYCLE_71;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect remove left med %d<->%d\n" ,
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
	}

	/* Empty line switching res on STF : switch to hi res on cycle 28, then go back to med/lo res */
	/* This creates a 0 byte line, the video counter won't change for this line */
#if 0
	else if ( ( FrameCycles-nLastFrameCycles <= 16 )
	          && ( nLastCycles == LINE_EMPTY_CYCLE_71_STF )
		  && ( ConfigureParams.System.nMachineType == MACHINE_ST ) )
#else
	else if ( ( ShifterFrame.Res == 0x02 )			/* switched from hi res */
		  && ( FrameCycles - ShifterFrame.ResPosHi.FrameCycles <= 16 )
	          && ( ShifterFrame.ResPosHi.LineCycles == LINE_EMPTY_CYCLE_71_STF )
		  && ( ConfigureParams.System.nMachineType == MACHINE_ST ) )
#endif
	{
		ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_EMPTY_LINE;
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle = 0;
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = 0;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect empty line res %d<->%d\n" ,
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
	}

	/* Empty line switching res on STE (switch is 4 cycles later than on STF) */
	else if ( ( ShifterFrame.Res == 0x02 )			/* switched from hi res */
		  && ( FrameCycles - ShifterFrame.ResPosHi.FrameCycles <= 16 )
	          && ( ShifterFrame.ResPosHi.LineCycles == LINE_EMPTY_CYCLE_71_STE )
		  && ( ConfigureParams.System.nMachineType == MACHINE_STE ) )
	{
		ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_EMPTY_LINE;
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle = 0;
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = 0;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect empty line res %d<->%d\n" ,
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
	}

	/* Empty line switching res on STF : switch to hi res just before the HBL then go back to lo/med res */
#if 0
	else if ( ( nLastCycles == 500 ) && ( LineCycles == 508 ) )
#else
	else if ( ( ShifterFrame.Res == 0x02 )			/* switched from hi res */
	          && ( ShifterFrame.ResPosHi.LineCycles == 500 )
	          && ( LineCycles == 508 ) )
#endif
	{
		ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_EMPTY_LINE;
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle = 0;
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = 0;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect empty line res 2 %d<->%d\n" ,
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
	}

	/* Start right border near middle of the line : -106 bytes */ 
	/* Switch to hi res just before the start of the right border in hi res, then go back to lo/mid res */
#if 0
	if ( ( ShifterFrame.Res == 0x02 && Res == 0x00 )
	        && ( nLastHBL == HblCounterVideo )			/* switch during the same line */
	        && ( nLastCycles <= LINE_END_CYCLE_71+4 )		/* switch to hi res before cycle 164 */
	        && ( LineCycles >= LINE_END_CYCLE_71+4 ) )		/* switch to lo res after cycle 164 */
#else
	if ( ( ShifterFrame.Res == 0x02 )				/* switched from hi res */
	        && ( ShifterFrame.ResPosHi.HBL == HblCounterVideo )	/* switch during the same line */
	        && ( ShifterFrame.ResPosHi.LineCycles <= LINE_END_CYCLE_71+4 )	/* switched to hi res before cycle 164 */
	        && ( LineCycles >= LINE_END_CYCLE_71+4 ) )		/* switch to lo res after cycle 164 */
#endif
	{
		ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_STOP_MIDDLE;
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = LINE_END_CYCLE_71;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect stop middle %d<->%d\n" ,
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
	}

	/* Remove right border a second time after removing it a first time. Display will */
	/* stop at cycle 512 instead of 460. */
	/* This removes left border on next line too (used in 'Enchanted Lands') */
#if 0
	if ( ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask & BORDERMASK_RIGHT_OFF
	        && ShifterFrame.Res == 0x02 && Res == 0x00
	        && FrameCycles-nLastFrameCycles <= 20
	        && nLastCycles == LINE_END_CYCLE_50_2 )
#else
	if ( ( ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask & BORDERMASK_RIGHT_OFF )
	        && ( ShifterFrame.Res == 0x02 )				/* switched from hi res */
	        && ( FrameCycles - ShifterFrame.ResPosHi.FrameCycles <= 20 )
	        && ( ShifterFrame.ResPosHi.LineCycles == LINE_END_CYCLE_50_2 ) )	/* switch at end of right border */
#endif
	{
		ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_RIGHT_OFF_FULL;
		ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = LINE_END_CYCLE_FULL;
		ShifterFrame.ShifterLines[ HblCounterVideo+1 ].BorderMask |= BORDERMASK_LEFT_OFF;	/* no left border on next line */
		ShifterFrame.ShifterLines[ HblCounterVideo+1 ].DisplayStartCycle = LINE_START_CYCLE_71;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect remove right full %d<->%d\n" ,
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
	}

	/* If left border is opened and we switch to medium resolution during the next cycles, */
	/* then we assume a med res overscan line instead of a low res overscan line. */
	/* Note that in that case, the switch to med res can shift the display by 0-3 words */
	/* Used in 'No Cooper' greetings by 1984 and 'Punish Your Machine' by Delta Force */
	if ( ( ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask & BORDERMASK_LEFT_OFF )
	        && ( Res == 0x01 ) )
	{
		if ( LineCycles == LINE_LEFT_MED_CYCLE_1 )		/* 'No Cooper' timing */
		{
			LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect med res overscan offset 0 byte\n" );
			ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_OVERSCAN_MED_RES | ( 0 << 20 );
		}
		else if ( LineCycles == LINE_LEFT_MED_CYCLE_2 )		/* 'Best Part Of The Creation / PYM' timing */
		{
			LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect med res overscan offset 2 bytes\n" );
			ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_OVERSCAN_MED_RES | ( 2 << 20 );
		}
	}

	/* If left border was opened with a hi/med res switch we need to check */
	/* if the switch to low res can trigger a right hardware scrolling. */
	/* We store the pixels count in DisplayPixelShift */
	if ( ( ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask & BORDERMASK_LEFT_OFF_MED )
	        && ( Res == 0x00 ) && ( LineCycles <= LINE_SCROLL_1_CYCLE_50 ) )
	{
		/* The hi/med switch was a switch to do low res hardware scrolling, */
		/* so we must cancel the med res overscan bit. */
		ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask &= (~BORDERMASK_OVERSCAN_MED_RES);

		if ( LineCycles == LINE_SCROLL_13_CYCLE_50 )		/* cycle 20 */
		{
			LOG_TRACE(TRACE_VIDEO_BORDER_H , "detect 13 pixels right scroll\n" );
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayPixelShift = 13;
		}
		else if ( LineCycles == LINE_SCROLL_9_CYCLE_50 )	/* cycle 24 */
		{
			LOG_TRACE(TRACE_VIDEO_BORDER_H , "detect 9 pixels right scroll\n" );
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayPixelShift = 9;
		}
		else if ( LineCycles == LINE_SCROLL_5_CYCLE_50 )	/* cycle 28 */
		{
			LOG_TRACE(TRACE_VIDEO_BORDER_H , "detect 5 pixels right scroll\n" );
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayPixelShift = 5;
		}
		else if ( LineCycles == LINE_SCROLL_1_CYCLE_50 )	/* cycle 32 */
		{
			LOG_TRACE(TRACE_VIDEO_BORDER_H , "detect 1 pixel right scroll\n" );
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayPixelShift = 1;
		}
	}


	/* Update HBL's position only if display has not reached pos LINE_START_CYCLE_50 */
	/* and HBL interrupt was already handled at the beginning of this line. */
	/* This also changes the number of cycles per line. */
	if ( ( LineCycles <= LINE_START_CYCLE_50 ) && ( HblCounterVideo == nHBL ) )
	{
		nCyclesPerLine = Video_HBL_GetPos();
		Video_AddInterruptHBL ( nCyclesPerLine );
#if 0
		Video_AddInterruptHBL ( Video_HBL_GetPos() );

		if ( IoMem_ReadByte( 0xff8260 ) == 2 )			/* hi res */
			nCyclesPerLine = CYCLES_PER_LINE_71HZ;
		else
		{
			if ( IoMem_ReadByte(0xff820a) & 2 )		/* 50 Hz */
				nCyclesPerLine = CYCLES_PER_LINE_50HZ;
			else						/* 60 Hz */
				nCyclesPerLine = CYCLES_PER_LINE_60HZ;
		}
#endif
	}

#if 1
	/* Update Timer B's position */
	LineTimerBCycle = Video_TimerB_GetPos ( HblCounterVideo );
	Video_AddInterruptTimerB ( LineTimerBCycle );
#endif

	ShifterFrame.Res = Res;
	if ( Res == 0x02 )						/* high res */
	{
		ShifterFrame.ResPosHi.VBL = nVBLs;
		ShifterFrame.ResPosHi.FrameCycles = FrameCycles;
		ShifterFrame.ResPosHi.HBL = HblCounterVideo;
		ShifterFrame.ResPosHi.LineCycles = LineCycles;
	}
	else if ( Res == 0x01 )						/* med res */
	{
		ShifterFrame.ResPosMed.VBL = nVBLs;
		ShifterFrame.ResPosMed.FrameCycles = FrameCycles;
		ShifterFrame.ResPosMed.HBL = HblCounterVideo;
		ShifterFrame.ResPosMed.LineCycles = LineCycles;
	}
	else								/* low res */
	{
		ShifterFrame.ResPosLo.VBL = nVBLs;
		ShifterFrame.ResPosLo.FrameCycles = FrameCycles;
		ShifterFrame.ResPosLo.HBL = HblCounterVideo;
		ShifterFrame.ResPosLo.LineCycles = LineCycles;
	}
}



/*-----------------------------------------------------------------------*/
/**
 * Set some default values for DisplayStartCycle/DisplayEndCycle
 * when changing frequency in lo/med res (testing orders are important
 * because the line can already have some borders changed).
 * This is necessary as some freq changes can modify start/end
 * even if they're not made at the exact borders' positions.
 * These values will be modified later if some borders are changed.
 */
static void	Video_Sync_SetDefaultStartEnd ( Uint8 Freq , int HblCounterVideo , int LineCycles )
{
	if ( Freq == 0x02 )					/* switch to 50 Hz */
	{
		if ( ( LineCycles <= ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle )	/* start could be 0,52,56 */
		  && ( ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle == LINE_START_CYCLE_60 ) )
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle = LINE_START_CYCLE_50;

		if ( ( LineCycles < ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle )	/* end could be 160,372,376,460 */
		  && ( ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle < LINE_END_CYCLE_50 ) )
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = LINE_END_CYCLE_50;
	}

	else							/* switch to 60 Hz */
	{
		if ( LineCycles < ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle )	/* start could be 0,52,56 */
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle = LINE_START_CYCLE_60;

		if ( ( LineCycles < ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle )	/* end could be 160,372,376,460 */
		  && ( ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle <= LINE_END_CYCLE_50 ) )
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = LINE_END_CYCLE_60;
	}

//fprintf ( stderr , "sync default pos %d %d\n", ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
}


/*-----------------------------------------------------------------------*/
/**
 * Write to VideoSync (0xff820a), Hz setting
 */
void Video_Sync_WriteByte ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;
	Uint8 Freq;

	/* We're only interested in bit 1 (50/60Hz) */
	Freq = IoMem[0xff820a] & 2;

	Video_GetPosition_OnWriteAccess ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_VIDEO_SYNC ,"sync=0x%2.2X video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n",
	               Freq, FrameCycles, LineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );

	/* Ignore consecutive writes of the same value */
	if ( Freq == ShifterFrame.Freq )
		return;						/* do nothing */

	/* Ignore freq changes if we are in high res */
	/* 2009/04/26 : don't ignore for now (see ST Cnx in Punish Your Machine) */
//	if ( ShifterFrame.Res == 0x02 )
//		return;						/* do nothing */

	/* Set some default values for DisplayStartCycle/DisplayEndCycle before checking for border removal */
	Video_Sync_SetDefaultStartEnd ( Freq , HblCounterVideo , LineCycles );


	if ( ( ShifterFrame.Freq == 0x00 ) && ( Freq == 0x02 )	/* switched from 60 Hz to 50 Hz ? */
//	        && ( ShifterFrame.FreqPos60.VBL == nVBLs )	/* switched during the same VBL */
	        && ( HblCounterVideo >= nStartHBL )		/* only if display is on */
	        && ( HblCounterVideo < nEndHBL ) )		/* only if display is on */
	{
		/* Add 2 bytes to left border : switch to 60 Hz before LINE_START_CYCLE_60 to force an early start */
		/* of the DE signal, then go back to 50 Hz. Note that depending on where the 50 Hz switch is made */
		/* the HBL signal will be at position 508 (60 Hz line) or 512 (50 Hz line) */
		/* Obtaining a +2 line with 512 cycles requires a 2 cycles precision and is "wake up" state dependant */
		if ( ( ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle == LINE_START_CYCLE_60 )
		        && ( LineCycles >= LINE_START_CYCLE_50 )	/* The line started in 60 Hz and continues in 50 Hz */
		        && ( LineCycles <= ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle ) )	/* change when line is active */
		{
			ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_LEFT_PLUS_2;
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = LINE_END_CYCLE_50;
			LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect left+2 %d<->%d\n" ,
				ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
		}

		/* Empty line switching freq : start the line in 50 Hz, change to 60 Hz at the exact place */
		/* where display is enabled in 50 Hz, then go back to 50 Hz. */
		/* Due to 4 cycles precision instead of 2, we must accept a 60 Hz switch at pos 56 or 56+4 */
		else if ( ( FrameCycles - ShifterFrame.FreqPos60.FrameCycles <= 24 )
			&& ( ( ShifterFrame.FreqPos60.LineCycles == LINE_START_CYCLE_50 ) || ( ShifterFrame.FreqPos60.LineCycles == LINE_START_CYCLE_50+4 ) )
			&& ( LineCycles > LINE_START_CYCLE_50 ) )
		{
			ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_EMPTY_LINE;
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle = 0;
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = 0;
			LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect empty line freq %d<->%d\n" ,
				ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
		}

		/* Remove 2 bytes to the right : start the line in 50 Hz (pos 0 or 56), change to 60 Hz before the position */
		/* where display is disabled in 60 Hz, then go back to 50 Hz */
		if ( ( LineCycles > LINE_END_CYCLE_60 )					/* back to 50 Hz after end of 60 Hz line */
			&& ( ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle != LINE_START_CYCLE_60 )	/* start could be 0 or 56 */
			&& ( ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle == LINE_END_CYCLE_60 ) )
		{
			ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_RIGHT_MINUS_2;
			LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect right-2 %d<->%d\n" ,
				ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
		}

	}


	if ( ( ShifterFrame.Freq == 0x02 && Freq == 0x00 )	/* switched from 50 Hz to 60 Hz ? */
	        && ( HblCounterVideo >= nStartHBL )		/* only if display is on */
	        && ( HblCounterVideo < nEndHBL ) )		/* only if display is on */
	{
		/* remove right border, display 44 bytes more : switch to 60 Hz at the position where */
		/* the line ends in 50 Hz. Some programs don't switch back to 50 Hz immediatly */
		/* (sync screen in SNY II), so we just check if freq changes to 60 Hz at the position where line should end in 50 Hz */
		if ( ( LineCycles == LINE_END_CYCLE_50 )
			&& ( ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle == LINE_END_CYCLE_50 ) )
		{
			ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask |= BORDERMASK_RIGHT_OFF;
			ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle = LINE_END_CYCLE_50;
			LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect remove right %d<->%d\n" ,
				ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayStartCycle , ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle );
		}
	}


	/* Store cycle position of freq 50/60 to check for top/bottom border removal in Video_EndHBL. */
	if ( Freq == 0x02 )						/* switch to 50 Hz */
	{
		if ( ( HblCounterVideo < VIDEO_START_HBL_50HZ )		/* nStartHBL can change only if display is not ON yet */
		        && ( OverscanMode & OVERSCANMODE_TOP ) == 0 )	/* update only if top was not removed */
			nStartHBL = VIDEO_START_HBL_50HZ;

		if ( ( HblCounterVideo < VIDEO_END_HBL_50HZ )		/* nEndHBL can change only if display is not OFF yet */
		        && ( OverscanMode & OVERSCANMODE_BOTTOM ) == 0 )	/* update only if bottom was not removed */
			nEndHBL = VIDEO_END_HBL_50HZ;				/* 263 */
	}
	else if ( Freq == 0x00 )					/* switch to 60 Hz */
	{
		if ( ( HblCounterVideo < VIDEO_START_HBL_60HZ-1 )	/* nStartHBL can change only if display is not ON yet */
			|| ( ( HblCounterVideo == VIDEO_START_HBL_60HZ-1 ) && ( LineCycles <= LineRemoveTopCycle ) ) )
			nStartHBL = VIDEO_START_HBL_60HZ;

		if ( ( HblCounterVideo < VIDEO_END_HBL_60HZ )		/* nEndHBL can change only if display is not OFF yet */
		        && ( OverscanMode & OVERSCANMODE_BOTTOM ) == 0 )	/* update only if bottom was not removed */
			nEndHBL = VIDEO_END_HBL_60HZ;				/* 234 */
	}


	/* If the frequence changed, we need to update the EndLine interrupt */
	/* so that it happens 28 cycles after the current DisplayEndCycle.*/
	/* We check if the change affects the current line or the next one. */
	/* We also need to check if the HBL interrupt and nCyclesPerLine need */
	/* to be updated first. */
	if ( Freq != ShifterFrame.Freq )
	{
		/* Update HBL's position only if display has not reached pos LINE_START_CYCLE_50 */
		/* and HBL interrupt was already handled at the beginning of this line. */
		/* This also changes the number of cycles per line. */
		if ( ( LineCycles <= LINE_START_CYCLE_50 ) && ( HblCounterVideo == nHBL ) )
		{
			nCyclesPerLine = Video_HBL_GetPos();
			Video_AddInterruptHBL ( nCyclesPerLine );
		}

#if 0
		int FrameCycles2, HblCounterVideo2, LineCycles2;

		Video_GetPosition ( &FrameCycles2 , &HblCounterVideo2 , &LineCycles2 );

		if ( ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask & BORDERMASK_RIGHT_MINUS_2 )		/* 60/50 Hz switch */
		{
			/* Do nothing when switching back to 50 Hz, keep timer B at pos LINE_END_CYCLE_60+TIMERB_VIDEO_CYCLE_OFFSET for this line */
		}

		else if ( ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask & BORDERMASK_RIGHT_OFF )	/* 60/50 Hz switch */
		{
			/* Ignore all other 50/60 Hz switches that could occur on this line after */
			/* right border was removed. Keep timer B at pos 460+28 */
		}

		else if ( ShifterFrame.ShifterLines[ HblCounterVideo ].BorderMask & BORDERMASK_STOP_MIDDLE )
		{
			/* Ignore all other 50/60 Hz switches that could occur on this line after */
			/* display was stopped in the middle of the line. Keep timer B at pos 376+28 */
		}

		else if ( LineCycles2 < LineEndCycle )			/* freq changed before the end of the line */
		{
			LineTimerBCycle = LineEndCycle + TIMERB_VIDEO_CYCLE_OFFSET;
			Int_AddRelativeInterrupt(LineTimerBCycle - LineCycles2,
						 INT_CPU_CYCLE, INTERRUPT_VIDEO_ENDLINE);
		}

		else							/* freq changed after the end of the line */
		{
			/* By default, next EndLine's int will be on line nHBL+1 at pos 376+28 or 372+28 */
			if ( Freq == 0x02 )		/* 50 Hz, pos 376+28 */
				LineTimerBCycle = LINE_END_CYCLE_50 + TIMERB_VIDEO_CYCLE_OFFSET;
			else				/* 60 Hz, pos 372+28 */
				LineTimerBCycle = LINE_END_CYCLE_60 + TIMERB_VIDEO_CYCLE_OFFSET;

			Int_AddRelativeInterrupt(LineTimerBCycle - LineCycles2 + nCyclesPerLine,
						 INT_CPU_CYCLE, INTERRUPT_VIDEO_ENDLINE);
		}
#else
		/* Update Timer B's position */
		LineTimerBCycle = Video_TimerB_GetPos ( HblCounterVideo );
		Video_AddInterruptTimerB ( LineTimerBCycle );
#endif
	}


	ShifterFrame.Freq = Freq;
	if ( Freq == 0x02 )						/* 50 Hz */
	{
		ShifterFrame.FreqPos50.VBL = nVBLs;
		ShifterFrame.FreqPos50.FrameCycles = FrameCycles;
		ShifterFrame.FreqPos50.HBL = HblCounterVideo;
		ShifterFrame.FreqPos50.LineCycles = LineCycles;
	}
	else
	{
		ShifterFrame.FreqPos60.VBL = nVBLs;
		ShifterFrame.FreqPos60.FrameCycles = FrameCycles;
		ShifterFrame.FreqPos60.HBL = HblCounterVideo;
		ShifterFrame.FreqPos60.LineCycles = LineCycles;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Compute the cycle position where the HBL should happen on each line.
 * In low/med res, the position depends on the video frequency (50/60 Hz)
 * In high res, the position is always the same.
 * This position also gives the number of CPU cycles per video line.
 */
static int Video_HBL_GetPos ( void )
{
	int Pos;

	if ( ( IoMem_ReadByte ( 0xff8260 ) & 3 ) == 2 )		/* hi res */
		Pos = CYCLES_PER_LINE_71HZ;

	else							/* low res or med res */
	{
		if ( IoMem_ReadByte ( 0xff820a ) & 2 )		/* 50 Hz, pos 512 */
			Pos = CYCLES_PER_LINE_50HZ;
		else                          			/* 60 Hz, pos 508 */
			Pos = CYCLES_PER_LINE_60HZ;
	}

	return Pos;
}


/*-----------------------------------------------------------------------*/
/**
 * Compute the cycle position where the timer B should happen on each
 * visible line.
 * We compute Timer B position for the given LineNumber, using start/end
 * display cycles from ShifterLines[ LineNumber ].
 * The position depends on the start of line / end of line positions
 * (which depend on the current frequency / border tricks) and
 * on the value of the bit 3 in the MFP's AER.
 * If bit is 0, timer B will count end of line events (usual case),
 * but if bit is 1, timer B will count start of line events (eg Seven Gates Of Jambala)
 */
int Video_TimerB_GetPos ( int LineNumber )
{
	int Pos;
#if 0
	Uint8 SyncByte = IoMem_ReadByte(0xff820a) & 2;  /* only keep bit 1 (50/60 Hz) */

	if ( ( IoMem_ReadByte( 0xff8260 ) & 3 ) == 2 )	/* hi res */
		Pos = LINE_END_CYCLE_71 + TIMERB_VIDEO_CYCLE_OFFSET;

	else						/* low res or med res */
	{
		/* Are we counting end of line events ? */
		if ( ( IoMem[0xfffa03] & ( 1 << 3 ) ) == 0 )
		{
			// Pos = LineEndCycle + TIMERB_VIDEO_CYCLE_OFFSET;
			if ( SyncByte == 0x02 )         /* 50 Hz, pos 376+28 */
				Pos = LINE_END_CYCLE_50 + TIMERB_VIDEO_CYCLE_OFFSET;
			else                            /* 60 Hz, pos 372+28 */
				Pos = LINE_END_CYCLE_60 + TIMERB_VIDEO_CYCLE_OFFSET;
		}

		/* Else, we're counting start of line events */
		else
		{
			// Pos = LineStartCycle + TIMERB_VIDEO_CYCLE_OFFSET;
			if ( SyncByte == 0x02 )         /* 50 Hz, pos 56+28 */
				Pos = LINE_START_CYCLE_50 + TIMERB_VIDEO_CYCLE_OFFSET;
			else                            /* 60 Hz, pos 52+28 */
				Pos = LINE_START_CYCLE_60 + TIMERB_VIDEO_CYCLE_OFFSET;
		}
	}
#else
	if ( ( IoMem[0xfffa03] & ( 1 << 3 ) ) == 0 )			/* we're counting end of line events */
	{
		Pos = ShifterFrame.ShifterLines[ LineNumber ].DisplayEndCycle + TIMERB_VIDEO_CYCLE_OFFSET;
	}
	else								/* we're counting start of line events */
	{
		Pos = ShifterFrame.ShifterLines[ LineNumber ].DisplayStartCycle + TIMERB_VIDEO_CYCLE_OFFSET;
	}
#endif
	return Pos;
}


/*-----------------------------------------------------------------------*/
/**
 * HBL interrupt : this occurs at the end of every line, on cycle 512 (in 50 Hz)
 * It takes 56 cycles to handle the 68000's exception.
 */
void Video_InterruptHandler_HBL ( void )
{
	int FrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
	int PendingCyclesOver;
	int NewHBLPos;

	/* How many cycle was this HBL delayed (>= 0) */
	PendingCyclesOver = -INT_CONVERT_FROM_INTERNAL ( PendingInterruptCount , INT_CPU_CYCLE );

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Increment the hbl jitter index */
	HblJitterIndex++;
	if ( HblJitterIndex == HBL_JITTER_MAX_POS )
		HblJitterIndex = 0;
	
	LOG_TRACE ( TRACE_VIDEO_HBL , "HBL %d video_cyc=%d pending_cyc=%d jitter=%d\n" ,
	               nHBL , FrameCycles , PendingCyclesOver , HblJitterArray[ HblJitterIndex ] );

	/* Default cycle position for next HBL */
	NewHBLPos = Video_HBL_GetPos();

	/* Generate new HBL, if need to - there are 313 HBLs per frame in 50 Hz */
	if (nHBL < nScanlinesPerFrame-1)
//		Int_AddAbsoluteInterrupt ( NewHBLPos , INT_CPU_CYCLE , INTERRUPT_VIDEO_HBL );
		Video_AddInterruptHBL ( NewHBLPos );


	/* We must handle a very special case : if we had a pending HBL that becomes active */
	/* because SR is now <= $2100, then we must process an exception for this pending HBL. */
	/* But if a "real" (non pending) HBL occurs during the first 56 cycles needed by the */
	/* CPU to prepare the exception, we must ignore this HBL signal (instead of considering */
	/* this new HBL should be put in pending state to be processed later) */
        /* -> any HBL occuring between LastCycleHblException and LastCycleHblException+56 should be ignored */
	if ( ( LastCycleHblException != -1 )
	  && ( ( FrameCycles - PendingCyclesOver - HblJitterArray[ HblJitterIndex ] ) - LastCycleHblException <= 56 ) )
	{
		/* simultaneous case, don't call M68000_Exception */
		LOG_TRACE ( TRACE_VIDEO_HBL , "HBL %d video_cyc=%d signal ignored during pending hbl exception at cycle %d\n" ,
			nHBL , Cycles_GetCounter(CYCLES_COUNTER_VIDEO) , LastCycleHblException );
		LastCycleHblException = -1;
	}
	else
	{
		/* "normal" case, this HBL doesn't occur during the processing of a pending HBL */
		M68000_Exception ( EXCEPTION_HBLANK , M68000_EXCEPTION_SRC_INT_VIDEO );	/* Horizontal blank interrupt, level 2! */
	}


	Video_EndHBL();					/* Check some borders removal and copy line to display buffer */

	nHBL++;						/* Increase HBL count */

	/* Update start cycle for next HBL */
	ShifterFrame.ShifterLines[ nHBL ].StartCycle = FrameCycles - PendingCyclesOver;
LOG_TRACE ( TRACE_VIDEO_HBL , "HBL %d start=%d %x\n" , nHBL , ShifterFrame.ShifterLines[ nHBL ].StartCycle , ShifterFrame.ShifterLines[ nHBL ].StartCycle );

	Video_StartHBL();				/* Setup next HBL */
}


/*-----------------------------------------------------------------------*/
/**
 * Check at end of each HBL to see if any Shifter hardware tricks have been attempted
 * and copy the line to the screen buffer.
 * This is the place to check if top/bottom border were removed, as well as if some
 * left/right border changes were not validated before.
 * NOTE : the tests must be made with nHBL in ascending order.
 */
static void Video_EndHBL(void)
{
	//
	// Handle top/bottom borders removal when switching freq
	//

	/* Remove top border if the switch to 60 Hz was made during this vbl before cycle	*/
	/* LineRemoveTopCycle on line 33 and if the switch to 50 Hz has not yet occured or	*/
	/* occured before the 60 Hz or occured after cycle LineRemoveTopCycle on line 33.	*/
	if ( ( nHBL == VIDEO_START_HBL_60HZ-1 )				/* last HBL before first line of a 60 Hz screen */
		&& ( ShifterFrame.FreqPos60.VBL == nVBLs )		/* switch to 60 Hz during this VBL */
		&& ( ( ShifterFrame.FreqPos60.HBL < nHBL ) || ( ShifterFrame.FreqPos60.LineCycles <= LineRemoveTopCycle ) )
		&& (   ( ShifterFrame.FreqPos50.VBL < nVBLs )
		    || ( ShifterFrame.FreqPos50.FrameCycles < ShifterFrame.FreqPos60.FrameCycles )
		    || ( ShifterFrame.FreqPos50.HBL > nHBL )
		    || ( ( ShifterFrame.FreqPos50.HBL == nHBL ) && ( ShifterFrame.FreqPos50.LineCycles > LineRemoveTopCycle ) ) ) )
	{
		/* Top border */
		LOG_TRACE ( TRACE_VIDEO_BORDER_V , "detect remove top\n" );
		OverscanMode |= OVERSCANMODE_TOP;	/* Set overscan bit */
		nStartHBL = VIDEO_START_HBL_60HZ;	/* New start screen line */
		pHBLPaletteMasks -= OVERSCAN_TOP;	// FIXME useless ?
		pHBLPalettes -= OVERSCAN_TOP;	// FIXME useless ?
	}

	/* Remove bottom border for a 60 Hz screen (tests are similar to the ones for top border) */
	else if ( ( nHBL == VIDEO_END_HBL_60HZ-1 )			/* last displayed line in 60 Hz */
		&& ( nStartHBL == VIDEO_START_HBL_60HZ )		/* screen started in 60 Hz */
		&& ( ( OverscanMode & OVERSCANMODE_TOP ) == 0 )		/* and top border was not removed : this screen is only 60 Hz */
		&& ( ShifterFrame.FreqPos50.VBL == nVBLs )		/* switch to 50 Hz during this VBL */
		&& ( ( ShifterFrame.FreqPos50.HBL < nHBL ) || ( ShifterFrame.FreqPos50.LineCycles <= LineRemoveBottomCycle ) )
		&& (   ( ShifterFrame.FreqPos60.VBL < nVBLs )
		    || ( ShifterFrame.FreqPos60.FrameCycles < ShifterFrame.FreqPos50.FrameCycles )
		    || ( ShifterFrame.FreqPos60.HBL > nHBL )
		    || ( ( ShifterFrame.FreqPos60.HBL == nHBL ) && ( ShifterFrame.FreqPos60.LineCycles > LineRemoveBottomCycle ) ) ) )
	{
		LOG_TRACE ( TRACE_VIDEO_BORDER_V , "detect remove bottom 60Hz\n" );
		OverscanMode |= OVERSCANMODE_BOTTOM;
		nEndHBL = SCANLINES_PER_FRAME_60HZ;	/* new end for a 60 Hz screen */
	}

	/* Remove bottom border for a 50 Hz screen (tests are similar to the ones for top border) */
	else if ( ( nHBL == VIDEO_END_HBL_50HZ-1 )			/* last displayed line in 50 Hz */
		&& ( ( OverscanMode & OVERSCANMODE_BOTTOM ) == 0 )	/* border was not already removed at line VIDEO_END_HBL_60HZ-1 */
		&& ( ShifterFrame.FreqPos60.VBL == nVBLs )		/* switch to 60 Hz during this VBL */
		&& ( ( ShifterFrame.FreqPos60.HBL < nHBL ) || ( ShifterFrame.FreqPos60.LineCycles <= LineRemoveBottomCycle ) )
		&& (   ( ShifterFrame.FreqPos50.VBL < nVBLs )
		    || ( ShifterFrame.FreqPos50.FrameCycles < ShifterFrame.FreqPos60.FrameCycles )
		    || ( ShifterFrame.FreqPos50.HBL > nHBL )
		    || ( ( ShifterFrame.FreqPos50.HBL == nHBL ) && ( ShifterFrame.FreqPos50.LineCycles > LineRemoveBottomCycle ) ) ) )
	{
		LOG_TRACE ( TRACE_VIDEO_BORDER_V , "detect remove bottom\n" );
		OverscanMode |= OVERSCANMODE_BOTTOM;
		nEndHBL = VIDEO_END_HBL_50HZ+VIDEO_HEIGHT_BOTTOM_50HZ;	/* new end for a 50 Hz screen */
	}


	//
	// Check some left/right borders effects that were not detected earlier
	// (this is usually due to staying in 60 Hz for too long, which is often a bad
	// coding practice as it can distort the display on a real ST)
	//

	/* Special case when the line was not started in 60 Hz, then switched to 60 Hz */
	/* and was not restored to 50 Hz before the end of the line. In that case, the */
	/* line ends 2 bytes earlier on the right (line can start at LINE_START_CYCLE_71/50) */
	/* Some programs also turn to 60 Hz too early during the active display of the last */
	/* line to remove the bottom border (FNIL by TNT), in that case, we should also remove */
	/* 2 bytes to this line */
	if ( ( ( ShifterFrame.ShifterLines[ nHBL ].BorderMask & BORDERMASK_RIGHT_MINUS_2 ) == 0 )
	  && ( ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle != LINE_START_CYCLE_60 )		/* start could be 0 or 56 */
	  && ( ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle == LINE_END_CYCLE_60 ) )
	{
		ShifterFrame.ShifterLines[ nHBL ].BorderMask |= BORDERMASK_RIGHT_MINUS_2;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect late right-2 %d<->%d\n" ,
			ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle , ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle );
	}


	/* Similar case when line started in 60 Hz but did not end at the usual LINE_END_CYCLE_60 position */
	/* (line can end at LINE_END_CYCLE_71/50 or have right border removed) */
	/* This means left border had 2 bytes more to display */
	if ( ( ( ShifterFrame.ShifterLines[ nHBL ].BorderMask & BORDERMASK_LEFT_PLUS_2 ) == 0 )
	  && ( ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle == LINE_START_CYCLE_60 )
	  && ( ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle != LINE_END_CYCLE_60 ) )		/* end could be 160, 372 or 460 */
	{
		ShifterFrame.ShifterLines[ nHBL ].BorderMask |= BORDERMASK_LEFT_PLUS_2;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "detect late left+2 %d<->%d\n" ,
			ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle , ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle );
	}


	/* Although a 'left+2' was detected earlier, the freq was switched back to 60 Hz during DE, so the line is just */
	/* a normal 60 Hz line ; we must cancel the 'left+2' flag */
	else if ( ( ShifterFrame.ShifterLines[ nHBL ].BorderMask & BORDERMASK_LEFT_PLUS_2 )
	  && ( ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle == LINE_END_CYCLE_60 ) )
	{
		ShifterFrame.ShifterLines[ nHBL ].BorderMask &= ~BORDERMASK_LEFT_PLUS_2;
		LOG_TRACE ( TRACE_VIDEO_BORDER_H , "cancel late left+2 %d<->%d\n" ,
			ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle , ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle );
	}



	/* Store palette for very first line on screen - HBLPalettes[0] */
	if (nHBL == nFirstVisibleHbl-1)
	{
		/* Store ALL palette for this line into raster table for datum */
		Video_StoreFirstLinePalette();
	}

	if (bUseHighRes)
	{
		/* Copy for hi-res (no overscan) */
		if (nHBL >= nFirstVisibleHbl && nHBL < nLastVisibleHbl)
			Video_CopyScreenLineMono();
	}
	/* Are we in possible visible color display (including borders)? */
	else if (nHBL >= nFirstVisibleHbl && nHBL < nLastVisibleHbl)
	{
		/* Store resolution for every line so can check for mix low/med screens */
		Video_StoreResolution(nHBL-nFirstVisibleHbl);

		/* Copy line of screen to buffer to simulate TV raster trace
		 * - required for mouse cursor display/game updates
		 * Eg, Lemmings and The Killing Game Show are good examples */
		Video_CopyScreenLineColor();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Set default values for the next HBL, depending on the current res/freq.
 * We set the number of cycles per line, as well as some default values
 * for display start/end cycle.
 */
static void Video_StartHBL(void)
{
	if ( ( IoMem_ReadByte( 0xff8260 ) & 3 ) == 2 )		/* hi res */
	{
		nCyclesPerLine = CYCLES_PER_LINE_71HZ;
		ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle = LINE_START_CYCLE_71;
		ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle = LINE_END_CYCLE_71;
	}

	else
	{
		if ( IoMem_ReadByte ( 0xff820a ) & 2 )		/* 50 Hz */
		{
			nCyclesPerLine = CYCLES_PER_LINE_50HZ;
			if ( ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle == -1 )	/* start not set yet */
				ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle = LINE_START_CYCLE_50;
			ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle = LINE_END_CYCLE_50;
		}
		else						/* 60 Hz */
		{
			nCyclesPerLine = CYCLES_PER_LINE_60HZ;
			if ( ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle == -1 )	/* start not set yet */
				ShifterFrame.ShifterLines[ nHBL ].DisplayStartCycle = LINE_START_CYCLE_60;
			ShifterFrame.ShifterLines[ nHBL ].DisplayEndCycle = LINE_END_CYCLE_60;
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * End Of Line interrupt
 * This interrupt is started on cycle position 404 in 50 Hz and on cycle
 * position 400 in 60 Hz. 50 Hz display ends at cycle 376 and 60 Hz displays
 * ends at cycle 372. This means the EndLine interrupt happens 28 cycles
 * after DisplayEndCycle.
 * Note that if bit 3 of MFP AER is 1, then timer B will count start of line
 * instead of end of line (at cycle 52+28 or 56+28)
 */
void Video_InterruptHandler_EndLine(void)
{
	int FrameCycles, HblCounterVideo, LineCycles;
	int PendingCycles = -INT_CONVERT_FROM_INTERNAL ( PendingInterruptCount , INT_CPU_CYCLE );

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE ( TRACE_VIDEO_HBL , "EndLine TB %d video_cyc=%d line_cyc=%d pending_int_cnt=%d\n" ,
	               nHBL , FrameCycles , LineCycles , PendingCycles );

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Ignore HBLs in VDI mode */
	if (bUseVDIRes)
		return;

	/* Generate new Endline, if need to - there are 313 HBLs per frame */
	if (nHBL < nScanlinesPerFrame-1)
	{
		/* By default, next EndLine's int will be on line nHBL+1 at pos 376+28 or 372+28 */
		if ( ( IoMem[0xfffa03] & ( 1 << 3 ) ) == 0 )		/* count end of line */
		{
			/* If EndLine int is delayed too much (more than 100 cycles), nLineCycles will */
			/* be in the range 0..xxx instead of 400..512. In that case, we need to add */
			/* nCyclesPerLine to be in the range 512..x+512 */
			/* Maximum possible delay should be around 160 cycles on STF (DIVS) */
			/* In that case, HBL int will be delayed too, so we will have HblCounterVideo == nHBL+1 */
			if ( HblCounterVideo == nHBL+1 )		/* int happened in fact on the next line nHBL+1 */
				LineCycles += nCyclesPerLine;

			LineTimerBCycle = Video_TimerB_GetPos( HblCounterVideo );
		}

		else							/* count start of line, no possible delay to handle */
		{
			LineTimerBCycle = Video_TimerB_GetPos( HblCounterVideo );
		}

//fprintf ( stderr , "new tb %d %d %d\n" , LineTimerBCycle , nCyclesPerLine , LineTimerBCycle - LineCycles + nCyclesPerLine );
		Int_AddRelativeInterrupt ( LineTimerBCycle - LineCycles + nCyclesPerLine,
					 INT_CPU_CYCLE, INTERRUPT_VIDEO_ENDLINE );
	}

	/* Timer B occurs at END of first visible screen line in Event Count mode */
	if (nHBL >= nStartHBL && nHBL < nEndHBL)
	{
		/* Handle Timer B when using Event Count mode */
		/* We must ensure that the write to fffa1b to activate timer B was */
		/* completed before the point where the end of line signal was generated */
		/* (in the case of a move.b #8,$fffa1b that would happen 4 cycles */
		/* before end of line, the interrupt should not be generated) */
		if ( (MFP_TBCR == 0x08)						/* Is timer in Event Count mode ? */
			&& ( ( TimerBEventCountCycleStart == -1 )		/* timer B was started during a previous VBL */
			  || ( TimerBEventCountCycleStart < FrameCycles-PendingCycles ) ) )	/* timer B was started before this possible interrupt */
			MFP_TimerB_EventCount_Interrupt();			/* we have a valid timer B interrupt */
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

//	if ( ShifterFrame.ShifterLines[ nFirstVisibleHbl ].BorderMask == BORDERMASK_NONE )	// no border trick, store the current res
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

		Mask = ShifterFrame.ShifterLines[ y+nFirstVisibleHbl ].BorderMask;

		if ( Mask & BORDERMASK_OVERSCAN_MED_RES )		/* special case for med res to render the overscan line */
			res = 1;					/* med res instead of low res */
		else if ( Mask != BORDERMASK_NONE )			/* border removal : assume low res for the whole line */
			res = 0;

		HBLPaletteMasks[y] |= PALETTEMASK_RESOLUTION|((Uint32)res)<<16;

#if 0
		if ( ( Mask == BORDERMASK_NONE )			/* no border trick, store the current res */
		        || ( res == 0 ) || ( res == 1 ) )			/* if border trick, ignore passage to hi res */
			HBLPaletteMasks[y] |= PALETTEMASK_RESOLUTION|((Uint32)res)<<16;
		else						/* border removal or hi res : assume low res for the whole line */
			HBLPaletteMasks[y] |= (0)<<16;

		/* special case for med res to render the overscan line */
		if ( Mask & BORDERMASK_OVERSCAN_MED_RES )
			HBLPaletteMasks[y] |= PALETTEMASK_RESOLUTION|((Uint32)1)<<16;	/* med res instead of low res */
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
	int LineBorderMask;
	int VideoOffset = 0;
	int STF_PixelScroll = 0;
	Uint32 addr;
	int LineRes;


	LineBorderMask = ShifterFrame.ShifterLines[ nHBL ].BorderMask;

	/* Get resolution for this line (in case of mixed low/med screen) */
	LineRes = ( HBLPaletteMasks[nHBL-nFirstVisibleHbl] >> 16 ) & 1;		/* 0=low res  1=med res */

	//fprintf(stderr , "copy line %d start %d end %d %d %x\n" , nHBL, nStartHBL, nEndHBL, LineBorderMask, pVideoRaster - STRam);

	/* If left border is opened, we need to compensate one missing word in low res (1 plan) */
	/* If overscan is in med res, the offset is variable */
	if ( LineBorderMask & BORDERMASK_OVERSCAN_MED_RES )
		VideoOffset = - ( ( LineBorderMask >> 20 ) & 0x0f );		/* No Cooper=0  PYM=-2 in med res overscan */

	else if ( LineBorderMask & BORDERMASK_LEFT_OFF )
		VideoOffset = -2;						/* always 2 bytes in low res overscan */

	/* Handle 4 pixels hardware scrolling ('ST Cnx' demo in 'Punish Your Machine') */
	/* Depending on the number of pixels, we need to compensate for some skipped words */
	else if ( LineBorderMask & BORDERMASK_LEFT_OFF_MED )
	{
//		STF_PixelScroll = ( LineBorderMask >> 16 ) & 0x0f;
		STF_PixelScroll = ShifterFrame.ShifterLines[ nHBL ].DisplayPixelShift;

		if      ( STF_PixelScroll == 13 )	VideoOffset = 2+8;
		else if ( STF_PixelScroll == 9 )	VideoOffset = 0+8;
		else if ( STF_PixelScroll == 5 )	VideoOffset = -2+8;
		else if ( STF_PixelScroll == 1 )	VideoOffset = -4+8;
		else					VideoOffset = 0;	/* never used ? */

		// fprintf(stderr , "scr off %d %d\n" , STF_PixelScroll , VideoOffset);
	}

#if 0
// done in Video_EndHBL
	/* A 60 Hz line with only the left border removed is 26+158 bytes instead */
	/* of 26+160 in 50 Hz */
	if ( ( LineBorderMask & ( BORDERMASK_LEFT_OFF | BORDERMASK_LEFT_OFF_MED ) )
	  && ( LineEndCycle == LINE_END_CYCLE_60 ) )
	  LineBorderMask |= BORDERMASK_RIGHT_MINUS_2;
#endif

	/* Is total blank line? I.e. top/bottom border? */
	if ((nHBL < nStartHBL) || (nHBL >= nEndHBL)
	    || (LineBorderMask & BORDERMASK_EMPTY_LINE))
	{
		/* Clear line to color '0' */
		memset(pSTScreen, 0, SCREENBYTES_LINE);
	}
	else
	{
		/* Does have left border? If not, clear to color '0' */
		if ( LineBorderMask & ( BORDERMASK_LEFT_OFF | BORDERMASK_LEFT_OFF_MED ) )
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
		if (!bSteBorderFlag && HWScrollCount)		/* Handle STE fine scrolling (HWScrollCount is zero on ST) */
		{
			Uint16 *pScrollAdj;	/* Pointer to actual position in line */
			int nNegScrollCnt;
			Uint16 *pScrollEndAddr;	/* Pointer to end of the line */

			nNegScrollCnt = 16 - HWScrollCount;
			if (LineBorderMask & BORDERMASK_LEFT_OFF)
				pScrollAdj = (Uint16 *)pSTScreen;
			else
				pScrollAdj = (Uint16 *)(pSTScreen + SCREENBYTES_LEFT);
			if (LineBorderMask & BORDERMASK_RIGHT_OFF)
				pScrollEndAddr = (Uint16 *)(pSTScreen + SCREENBYTES_LINE - 8);
			else
				pScrollEndAddr = (Uint16 *)(pSTScreen + SCREENBYTES_LEFT + SCREENBYTES_MIDDLE - 8);

			if ( LineRes == 1 )			/* med res */
			{
				/* in med res, 16 pixels are 4 bytes, not 8 as in low res, so only the last 4 bytes need a special case */
				pScrollEndAddr += 2;				/* 2 Uint16 -> 4 bytes */

				/* Shift the whole line to the left by the given scroll count */
				while (pScrollAdj < pScrollEndAddr)
				{
					do_put_mem_word(pScrollAdj, (do_get_mem_word(pScrollAdj) << HWScrollCount)
					                | (do_get_mem_word(pScrollAdj+2) >> nNegScrollCnt));
					++pScrollAdj;
				}
				/* Handle the last 16 pixels of the line */
				if (LineBorderMask & BORDERMASK_RIGHT_OFF)	/* FIXME : med + right off should be tested on a real STE */
				{
					Uint16 *pVideoLineEnd = (Uint16 *)(pVideoRaster - (46 - SCREENBYTES_RIGHT));
					do_put_mem_word(pScrollAdj+0, (do_get_mem_word(pScrollAdj+0) << HWScrollCount)
					                | (do_get_mem_word(pVideoLineEnd++) >> nNegScrollCnt));
					do_put_mem_word(pScrollAdj+1, (do_get_mem_word(pScrollAdj+1) << HWScrollCount)
					                | (do_get_mem_word(pVideoLineEnd++) >> nNegScrollCnt));
				}
				else
				{
					do_put_mem_word(pScrollAdj+0, (do_get_mem_word(pScrollAdj+0) << HWScrollCount)
					                | (do_get_mem_word(pVideoRaster+0) >> nNegScrollCnt));
					do_put_mem_word(pScrollAdj+1, (do_get_mem_word(pScrollAdj+1) << HWScrollCount)
					                | (do_get_mem_word(pVideoRaster+2) >> nNegScrollCnt));
				}

				/* Depending on whether $ff8264 or $ff8265 was used to scroll, */
				/* we prefetched 16 pixel (4 bytes) */
				if ( HWScrollPrefetch == 1 )		/* $ff8265 prefetches 16 pixels */
					pVideoRaster += 2 * 2;		/* 2 bitplans */

				/* If scrolling with $ff8264, there's no prefetch, which means display starts */
				/* 16 pixels later but still stops at the normal point (eg we display */
				/* (320-16) pixels in low res). We shift the whole line 4 bytes to the right to */
				/* get the correct result (using memmove, as src/dest are overlapping). */
				else
				{
					if (LineBorderMask & BORDERMASK_RIGHT_OFF)
						memmove ( pSTScreen+4 , pSTScreen , SCREENBYTES_LINE - 4 );
					else
						memmove ( pSTScreen+4 , pSTScreen , SCREENBYTES_LEFT + SCREENBYTES_MIDDLE - 4 );

					memset ( pSTScreen , 0 , 4 );	/* first 16 pixels are color '0' */
				}
			}

			else					/* low res */
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

				/* Depending on whether $ff8264 or $ff8265 was used to scroll, */
				/* we prefetched 16 pixel (8 bytes) */
				if ( HWScrollPrefetch == 1 )		/* $ff8265 prefetches 16 pixels */
					pVideoRaster += 4 * 2;		/* 4 bitplans */

				/* If scrolling with $ff8264, there's no prefetch, which means display starts */
				/* 16 pixels later but still stops at the normal point (eg we display */
				/* (320-16) pixels in low res). We shift the whole line 8 bytes to the right to */
				/* get the correct result (using memmove, as src/dest are overlapping). */
				else
				{
					if (LineBorderMask & BORDERMASK_RIGHT_OFF)
						memmove ( pSTScreen+8 , pSTScreen , SCREENBYTES_LINE - 8 );
					else
						memmove ( pSTScreen+8 , pSTScreen , SCREENBYTES_LEFT + SCREENBYTES_MIDDLE - 8 );

					memset ( pSTScreen , 0 , 8 );	/* first 16 pixels are color '0' */
				}

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
			HWScrollPrefetch = NewHWScrollPrefetch;
			NewHWScrollCount = -1;
			NewHWScrollPrefetch = -1;
		}

		/* On STE, if we trigger the left border + 16 pixels trick, we set the */
		/* new value here, once the current line was processed */
		if ( NewSteBorderFlag >= 0 )
		{
			if ( NewSteBorderFlag == 0 )
				bSteBorderFlag = false;
			else
				bSteBorderFlag = true;
			NewSteBorderFlag = -1;
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
	int FrameCycles, HblCounterVideo, LineCycles;
	int Line;

	/* FIXME [NP] We should use Cycles_GetCounterOnWriteAccess, but it wouldn't	*/
	/* work when using multiple accesses instructions like move.l or movem	*/
	/* To correct this, we assume a delay of 8 cycles (should give a good approximation */
	/* of a move.w or movem.l for example) */
	//  FrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
	FrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO) + 8;

#if 0
	/* Find 'line' into palette - screen starts 63 lines down, less 29 for top overscan */
	Line = (FrameCycles-(nFirstVisibleHbl*nCyclesPerLine)+LineStartCycle)/nCyclesPerLine;
//	Line = ( FrameCycles / nCyclesPerLine ) - nFirstVisibleHbl;
	Cycle = FrameCycles % nCyclesPerLine;
#else
	/* Find 'line' into palette - screen starts 63 lines down, less 29 for top overscan */
	Video_ConvertPosition ( FrameCycles , &HblCounterVideo , &LineCycles );
	Line = HblCounterVideo - nFirstVisibleHbl;
#endif

	/* FIXME [NP] if the color change occurs after the last visible pixel of a line */
	/* we consider the palette should be modified on the next line. This is quite */
	/* a hack, we should handle all color changes through spec512.c to have cycle */
	/* accuracy all the time. */
	if ( LineCycles >= LINE_END_CYCLE_NO_RIGHT )
		Line++;

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
		nStartHBL = VIDEO_START_HBL_71HZ;
		nFirstVisibleHbl = FIRST_VISIBLE_HBL_71HZ;
		nLastVisibleHbl = FIRST_VISIBLE_HBL_71HZ + VIDEO_HEIGHT_HBL_MONO;
//		LineTimerBCycle = Video_TimerB_GetPos();
	}
	else if (nSyncByte & 2)  /* Check if running in 50 Hz or in 60 Hz */
	{
		/* 50 Hz */
		nScreenRefreshRate = 50;
		nScanlinesPerFrame = SCANLINES_PER_FRAME_50HZ;
		nCyclesPerLine = CYCLES_PER_LINE_50HZ;
		nStartHBL = VIDEO_START_HBL_50HZ;
		nFirstVisibleHbl = FIRST_VISIBLE_HBL_50HZ;
		nLastVisibleHbl = FIRST_VISIBLE_HBL_50HZ + NUM_VISIBLE_LINES;
//		LineTimerBCycle = Video_TimerB_GetPos();
	}
	else
	{
		/* 60 Hz */
		nScreenRefreshRate = 60;
		nScanlinesPerFrame = SCANLINES_PER_FRAME_60HZ;
		nCyclesPerLine = CYCLES_PER_LINE_60HZ;
		nStartHBL = VIDEO_START_HBL_60HZ;
		nFirstVisibleHbl = FIRST_VISIBLE_HBL_60HZ;
		nLastVisibleHbl = FIRST_VISIBLE_HBL_60HZ + NUM_VISIBLE_LINES;
//		LineTimerBCycle = Video_TimerB_GetPos();
	}

	if (bUseHighRes)
	{
		nEndHBL = nStartHBL + VIDEO_HEIGHT_HBL_MONO;
	}
	else
	{
		nEndHBL = nStartHBL + VIDEO_HEIGHT_HBL_COLOR;
	}

	/* Reset freq changes position for the next VBL to come */
	LastCycleScroll8264 = -1;
	LastCycleScroll8265 = -1;

	TimerBEventCountCycleStart = -1;		/* reset timer B activation cycle for this VBL */

	LastCycleHblException = -1;
}


/*-----------------------------------------------------------------------*/
/**
 * Clear the array indicating the state of each video line.
 */
static void Video_InitShifterLines ( void )
{
	int	i;

	for ( i=0 ; i<MAX_SCANLINES_PER_FRAME ; i++ )
	{
		ShifterFrame.ShifterLines[i].BorderMask = 0;
		ShifterFrame.ShifterLines[i].DisplayPixelShift = 0;
		ShifterFrame.ShifterLines[i].DisplayStartCycle = -1;
	}

	ShifterFrame.ShifterLines[0].StartCycle = 0;			/* 1st HBL starts at cycle 0 */
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

	Video_SetScreenRasters();
	Video_InitShifterLines();
	Spec512_StartVBL();
	Video_StartHBL();					/* Init ShifterFrame.ShifterLines[0] */
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
		bTTColorsSTSync = true;
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
	bTTColorsSync = true;
}


/*-----------------------------------------------------------------------*/
/**
 * Update TT palette and blit TT screen using VIDEL code.
 * @return  true if the screen contents changed
 */
static bool Video_RenderTTScreen(void)
{
	static int nPrevTTRes = -1;
	int width, height, bpp;

	Video_GetTTRes(&width, &height, &bpp);
	if (TTRes != nPrevTTRes)
	{
		HostScreen_setWindowSize(width, height, 8);
		nPrevTTRes = TTRes;
		if (bpp == 1)   /* Assert that mono palette will be used in mono mode */
			bTTColorsSync = false;
	}

	/* colors need synching? */
	if (!(bTTColorsSync && bTTColorsSTSync))
	{
		Video_UpdateTTPalette(bpp);
	}

	/* Yes, we are abusing the Videl routines for rendering the TT modes! */
	if (!HostScreen_renderBegin())
		return false;
	if (ConfigureParams.Screen.bZoomLowRes)
		VIDEL_ConvertScreenZoom(width, height, bpp, width * bpp / 16);
	else
		VIDEL_ConvertScreenNoZoom(width, height, bpp, width * bpp / 16);
	HostScreen_renderEnd();
	HostScreen_update1(false);

	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Draw screen (either with ST/STE shifter drawing functions or with
 * Videl drawing functions)
 */
static void Video_DrawScreen(void)
{
	bool bScreenChanged;

	/* Skip frame if need to */
	if (nVBLs % (nFrameSkips+1))
		return;

	/* Use extended VDI resolution?
	 * If so, just copy whole screen on VBL rather than per HBL */
	if (bUseVDIRes)
		Video_CopyVDIScreen();

	/* Now draw the screen! */
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON && !bUseVDIRes)
	{
		bScreenChanged = VIDEL_renderScreen();
	}
	else if (ConfigureParams.System.nMachineType == MACHINE_TT && !bUseVDIRes)
	{
		bScreenChanged = Video_RenderTTScreen();
	}
	else
	{
		/* Before drawing the screen, ensure all unused lines are cleared to color 0 */
		/* (this can happen in 60 Hz when hatari is displaying the screen's border) */
		/* pSTScreen was set during Video_CopyScreenLineColor */
		if (!bUseVDIRes && nHBL < nLastVisibleHbl)
			memset(pSTScreen, 0, SCREENBYTES_LINE * ( nLastVisibleHbl - nHBL ) );

		bScreenChanged = Screen_Draw();
	}

	/* Grab any animation */
	if (bRecordingAnimation)
		ScreenSnapShot_RecordFrame(bScreenChanged);
}


/*-----------------------------------------------------------------------*/
/**
 * Start HBL, Timer B and VBL interrupts.
 */


/**
 * Start HBL or Timer B interrupt at position Pos. If position Pos was
 * already reached, then the interrupt is set on the next line.
 */

static void Video_AddInterrupt ( int Pos , interrupt_id Handler )
{
	int FrameCycles , HblCounterVideo , LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	if ( LineCycles < Pos )			/* changed before reaching the new Pos on the current line */
		Int_AddRelativeInterrupt ( Pos - LineCycles , INT_CPU_CYCLE, Handler );
	else					/* Pos will be applied on next line */
		Int_AddRelativeInterrupt ( Pos - LineCycles + nCyclesPerLine , INT_CPU_CYCLE, Handler );
}


static void Video_AddInterruptHBL ( int Pos )
{
//fprintf ( stderr , "add hbl %d\n" , Pos );
	Video_AddInterrupt ( Pos , INTERRUPT_VIDEO_HBL );
}


static void Video_AddInterruptTimerB ( int Pos )
{
	Video_AddInterrupt ( Pos , INTERRUPT_VIDEO_ENDLINE );
}


/**
 * Add some video interrupts to handle the first HBL and the first Timer B
 * in a new VBL. Also add an interrupt to trigger the next VBL.
 * This function is called from the VBL, so we use PendingCycleOver to take into account
 * the possible delay occurring when the VBL was executed.
 */
void Video_StartInterrupts ( int PendingCyclesOver )
{
#if 0
	/* HBL/Timer B are not emulated in VDI mode */
	if (!bUseVDIRes)
	{
		/* Set Timer B interrupt to cycle 376+28 or 372+28 because nCyclesPerLine is 512 or 508 */
		/* (this also means int is set to 512-108 or 508-108 depending on the current freq) */
		/* We check bit 3 of MFP AER to see if we count end of line or start of line events */
		if ( ( IoMem[0xfffa03] & ( 1 << 3 ) ) == 0 )		/* count end of line */
			Int_AddAbsoluteInterrupt(nCyclesPerLine - ( CYCLES_PER_LINE_50HZ - LINE_END_CYCLE_50 )
		                         + TIMERB_VIDEO_CYCLE_OFFSET - VblVideoCycleOffset,
		                         INT_CPU_CYCLE, INTERRUPT_VIDEO_ENDLINE);
		else							/* count start of line */
			Int_AddAbsoluteInterrupt(nCyclesPerLine - ( CYCLES_PER_LINE_50HZ - LINE_START_CYCLE_50 )
		                         + TIMERB_VIDEO_CYCLE_OFFSET - VblVideoCycleOffset,
		                         INT_CPU_CYCLE, INTERRUPT_VIDEO_ENDLINE);

		/* Set HBL interrupt */
		Int_AddAbsoluteInterrupt(nCyclesPerLine + HBL_VIDEO_CYCLE_OFFSET - VblVideoCycleOffset,
		                         INT_CPU_CYCLE, INTERRUPT_VIDEO_HBL);
	}


	Int_AddAbsoluteInterrupt(CYCLES_PER_FRAME, INT_CPU_CYCLE, INTERRUPT_VIDEO_VBL);
#else
	/* HBL/Timer B are not emulated in VDI mode */
	if (!bUseVDIRes)
	{
		/* Set Timer B interrupt for line 0*/
		Video_AddInterruptTimerB ( Video_TimerB_GetPos ( 0 ) );

		/* Set HBL interrupt */
		Video_AddInterruptHBL ( Video_HBL_GetPos() );
	}

	/* TODO replace CYCLES_PER_FRAME */
	Int_AddRelativeInterrupt ( CYCLES_PER_FRAME - PendingCyclesOver , INT_CPU_CYCLE , INTERRUPT_VIDEO_VBL );
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * VBL interrupt : set new interrupts, draw screen, generate sound,
 * reset counters, ...
 */
void Video_InterruptHandler_VBL ( void )
{
	int PendingCyclesOver;

	/* Store cycles we went over for this frame(this is our inital count) */
	PendingCyclesOver = -INT_CONVERT_FROM_INTERNAL ( PendingInterruptCount , INT_CPU_CYCLE );    /* +ve */

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Increment the vbl jitter index */
	VblJitterIndex++;
	if ( VblJitterIndex == VBL_JITTER_MAX_POS )
		VblJitterIndex = 0;
	
	/* Set frame cycles, used for Video Address */
	Cycles_SetCounter(CYCLES_COUNTER_VIDEO, PendingCyclesOver + VblVideoCycleOffset);

	/* Clear any key presses which are due to be de-bounced (held for one ST frame) */
	Keymap_DebounceAllKeys();

	Video_DrawScreen();

	/* Check printer status */
	Printer_CheckIdleStatus();

	/* Update counter for number of screen refreshes per second */
	nVBLs++;
	/* Set video registers for frame */
	Video_ClearOnVBL();

	/* Start VBL, HBL and Timer B interrupts (this must be done after resetting
         * video cycle counter setting default freq values in Video_ClearOnVBL) */
	Video_StartInterrupts(PendingCyclesOver);

	/* Act on shortcut keys */
	ShortCut_ActKey();

	/* Store off PSG registers for YM file, is enabled */
	YMFormat_UpdateRecording();
	/* Generate 1/50th second of sound sample data, to be played by sound thread */
	Sound_Update_VBL();

	LOG_TRACE(TRACE_VIDEO_VBL , "VBL %d video_cyc=%d pending_cyc=%d jitter=%d\n" ,
	               nVBLs , Cycles_GetCounter(CYCLES_COUNTER_VIDEO) , PendingCyclesOver , VblJitterArray[ VblJitterIndex ] );

	M68000_Exception ( EXCEPTION_VBLANK , M68000_EXCEPTION_SRC_INT_VIDEO );	/* Vertical blank interrupt, level 4! */

	Main_WaitOnVbl();
}


/*-----------------------------------------------------------------------*/
/**
 * Write to video address base high, med and low register (0xff8201/03/0d).
 * On STE, when a program writes to high or med registers, base low register
 * is reset to zero.
 */
void Video_ScreenBaseSTE_WriteByte(void)
{
	if ( ( IoAccessCurrentAddress == 0xff8201 ) || ( IoAccessCurrentAddress == 0xff8203 ) )
		IoMem[0xff820d] = 0;          /* Reset screen base low register */

	if (LOG_TRACE_LEVEL(TRACE_VIDEO_STE))
	{
		int FrameCycles, HblCounterVideo, LineCycles;

		Video_GetPosition_OnWriteAccess ( &FrameCycles , &HblCounterVideo , &LineCycles );
		
		LOG_TRACE_PRINT ( "write ste video base=0x%x video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n" ,
			(IoMem[0xff8201]<<16)+(IoMem[0xff8203]<<8)+IoMem[0xff820d] ,
			FrameCycles, LineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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
	int FrameCycles, HblCounterVideo, LineCycles;
	int Delayed;

#if 0
	nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
	nLineCycles = nFrameCycles % nCyclesPerLine;

	/* Get real video line count (can be different from nHBL) */
	HblCounterVideo = nFrameCycles / nCyclesPerLine;
#endif
	Video_GetPosition_OnWriteAccess ( &FrameCycles , &HblCounterVideo , &LineCycles );

	AddrByte = IoMem[ IoAccessCurrentAddress ];

	/* If display has not started, we can still modify pVideoRaster */
	/* We must also check the write does not overlap the end of the line */
	if ( ( ( LineCycles <= LINE_START_CYCLE_50 ) && ( nHBL == HblCounterVideo ) )
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
		Delayed = false;
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
		Delayed = true;
	}

	LOG_TRACE(TRACE_VIDEO_STE , "write ste video %x val=0x%x delayed=%s video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n" ,
				IoAccessCurrentAddress, AddrByte, Delayed ? "yes" : "no" ,
				FrameCycles, LineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
}

/*-----------------------------------------------------------------------*/
/**
 * Read video sync register (0xff820a)
 */
void Video_Sync_ReadByte(void)
{
	if ( (ConfigureParams.System.nMachineType == MACHINE_ST) || (ConfigureParams.System.nMachineType == MACHINE_STE) )
		IoMem[0xff820a] |= 0xfc;		/* set unused bits 2-7 to 1 */
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
		IoMem[0xff8260] = 2;			/* If mono monitor, force to high resolution */
	else
		IoMem[0xff8260] = VideoShifterByte;	/* Read shifter register, set unused bits to 1 */

	if ( (ConfigureParams.System.nMachineType == MACHINE_ST) || (ConfigureParams.System.nMachineType == MACHINE_STE) )
		IoMem[0xff8260] |= 0xfc;		/* set unused bits 2-7 to 1 */
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
	int FrameCycles, HblCounterVideo, LineCycles;
	int Delayed;

#if 0
	nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
	nLineCycles = nFrameCycles % nCyclesPerLine;

	/* Get real video line count (can be different from nHBL) */
	HblCounterVideo = nFrameCycles / nCyclesPerLine;
#endif
	Video_GetPosition_OnWriteAccess ( &FrameCycles , &HblCounterVideo , &LineCycles );

	NewWidth = IoMem_ReadByte(0xff820f);

	/* We must also check the write does not overlap the end of the line */
	if ( ( ( nHBL == HblCounterVideo ) && ( LineCycles <= ShifterFrame.ShifterLines[ HblCounterVideo ].DisplayEndCycle ) )
		|| ( nHBL < nStartHBL ) || ( nHBL >= nEndHBL ) )
	{
		LineWidth = NewWidth;		/* display is on, we can still change */
		NewLineWidth = -1;		/* cancel 'pending' change */
		Delayed = false;
	}
	else
	{
		NewLineWidth = NewWidth;	/* display is off, can't change LineWidth once in right border */
		Delayed = true;
	}

	LOG_TRACE(TRACE_VIDEO_STE , "write ste linewidth=0x%x delayed=%s video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n",
					NewWidth, Delayed ? "yes" : "no" ,
					FrameCycles, LineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
}

/*-----------------------------------------------------------------------*/
/**
 * Write to video shifter palette registers (0xff8240-0xff825e)
 *
 * Note that there's a special "strange" case when writing only to the upper byte
 * of the color reg (instead of writing 16 bits at once with .W/.L).
 * In that case, the byte written to address x is automatically written
 * to address x+1 too (but we shouldn't copy x in x+1 after masking x ; we apply the mask at the end)
 * So :	move.w #0,$ff8240	-> color 0 is now $000
 *	move.b #7,$ff8240	-> color 0 is now $707 !
 *	move.b #$55,$ff8241	-> color 0 is now $755 ($ff8240 remains unchanged)
 *	move.b #$71,$ff8240	-> color 0 is now $171 (bytes are first copied, then masked)
 */
static void Video_ColorReg_WriteWord(Uint32 addr)
{
	if (!bUseHighRes)                          /* Don't store if hi-res */
	{
		int idx;
		Uint16 col;
		Video_SetHBLPaletteMaskPointers();     /* Set 'pHBLPalettes' etc.. according cycles into frame */
		col = IoMem_ReadWord(addr);

		/* Handle special case when writing only to the upper byte of the color reg */
		if ( ( nIoMemAccessSize == SIZE_BYTE ) && ( ( IoAccessCurrentAddress & 1 ) == 0 ) )
			col = ( IoMem_ReadByte(addr) << 8 ) + IoMem_ReadByte(addr);		/* copy upper byte into lower byte */

		if (ConfigureParams.System.nMachineType == MACHINE_ST)
			col &= 0x777;                      /* Mask off to ST 512 palette */
		else
			col &= 0xfff;                      /* Mask off to STe 4096 palette */
		IoMem_WriteWord(addr, col);            /* (some games write 0xFFFF and read back to see if STe) */
		Spec512_StoreCyclePalette(col, addr);  /* Store colour into CyclePalettes[] */
		idx = (addr-0xff8240)/2;               /* words */
		pHBLPalettes[idx] = col;               /* Set colour x */
		*pHBLPaletteMasks |= 1 << idx;         /* And mask */

		if (LOG_TRACE_LEVEL(TRACE_VIDEO_COLOR))
		{
			int FrameCycles, HblCounterVideo, LineCycles;

			Video_GetPosition_OnWriteAccess ( &FrameCycles , &HblCounterVideo , &LineCycles );

			LOG_TRACE_PRINT ( "write col addr=%x col=%x video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n" ,
				addr, col,
				FrameCycles, LineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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
		bUseSTShifter = true;
	}
//	if (!bUseHighRes && !bUseVDIRes)                    /* Don't store if hi-res and don't store if VDI resolution */
	if (!bUseVDIRes)                    /* Don't store if hi-res and don't store if VDI resolution */
	{
		VideoShifterByte = IoMem[0xff8260] & 3;		/* We only care for lower 2-bits */
		if ( VideoShifterByte == 3 )			/* 3 is not a valid resolution, use low res instead */
		{
			VideoShifterByte = 0;
			IoMem_WriteByte(0xff8260,0);
		}
		Video_WriteToShifter(VideoShifterByte);
		Video_SetHBLPaletteMaskPointers();
		*pHBLPaletteMasks &= 0xff00ffff;
		/* Store resolution after palette mask and set resolution write bit: */
		*pHBLPaletteMasks |= (((Uint32)VideoShifterByte|0x04)<<16);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle horizontal scrolling to the left.
 * On STE, there're 2 registers that can scroll the line :
 *  - $ff8264 : scroll without prefetch
 *  - $ff8265 : scroll with prefetch
 * Both registers will scroll the line to the left by skipping the amount
 * of pixels in $ff8264 or $ff8265 (from 0 to 15).
 * As some pixels will be skipped, this means the shifter needs to read
 * 16 other pixels in advance in some internal registers to have an uninterrupted flow of pixels.
 *
 * These 16 pixels can be prefetched before the display starts (on cycle 56 for example) when using
 * $ff8265 to scroll the line. In that case 8 more bytes per line (low res) will be read. Most programs
 * are using $ff8265 to scroll the line.
 *
 * When using $ff8264, the next 16 pixels will not be prefetched before the display
 * starts, they will be read when the display normally starts (cycle 56). While
 * reading these 16 pixels, the shifter won't be able to display anything, which will
 * result in 16 pixels having the color 0. So, reading the 16 pixels will in fact delay
 * the real start of the line, which will look as if it started 16 pixels later. As the
 * shifter will stop the display at cycle 56+320 anyway, this means the last 16 pixels
 * of each line won't be displayed and you get the equivalent of a shorter 304 pixels line.
 * As a consequence, this register is rarely used to scroll the line.
 *
 * By writing a value > 0 in $ff8265 (to start prefetching) and immediatly after a value of 0
 * in $ff8264 (no scroll and no prefetch), it's possible to fill the internal registers used
 * for the scrolling even if scrolling is set to 0. In that case, the shifter will start displaying
 * each line 16 pixels earlier (as the data are already available in the internal registers).
 * This allows to have 336 pixels per line (instead of 320) for all the remaining lines on the screen.
 *
 * Although some programs are using this sequence :
 *	move.w  #1,$ffff8264		; Word access!
 *	clr.b   $ffff8264		; Byte access!
 * It is also possible to add 16 pixels by doing :
 *	move.b  #X,$ff8265		; with X > 0
 *	move.b	#0,$ff8264
 * Some games (Obsession, Skulls) and demos (Pacemaker by Paradox) use this
 * feature to increase the resolution, so we have to emulate this bug, too!
 *
 * So considering a low res line of 320 pixels (160 bytes) :
 * 	- if both $ff8264/65 are 0, no scrolling happens, the shifter reads 160 bytes and displays 320 pixels (same as STF)
 *	- if $ff8265 > 0, line is scrolled, the shifter reads 168 bytes and displays 320 pixels.
 *	- if $ff8264 > 0, line is scrolled, the shifter reads 160 bytes and displays 304 pixels,
 *		the display starts 16 pixels later.
 *	- if $ff8265 > 0 and then $ff8264 = 0, there's no scrolling, the shifter reads 168 bytes and displays 336 pixels,
 *		the display starts 16 pixels earlier.
 */

void Video_HorScroll_Write_8264(void)
{
	Video_HorScroll_Write();
}

void Video_HorScroll_Write_8265(void)
{
	Video_HorScroll_Write();
}

void Video_HorScroll_Write(void)
{
	Uint32 RegAddr;
	Uint8 ScrollCount;
	Uint8 Prefetch;
	int FrameCycles, HblCounterVideo, LineCycles;
	bool Add16px = false;
	static Uint8 LastVal8265 = 0;
	int Delayed;

#if 0
	nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
	nLineCycles = nFrameCycles % nCyclesPerLine;

	/* Get real video line count (can be different from nHBL) */
	HblCounterVideo = nFrameCycles / nCyclesPerLine;
#endif
	Video_GetPosition_OnWriteAccess ( &FrameCycles , &HblCounterVideo , &LineCycles );

	RegAddr = IoAccessCurrentAddress;		/* 0xff8264 or 0xff8265 */
	ScrollCount = IoMem[ RegAddr ];
	ScrollCount &= 0x0f;

	if ( RegAddr == 0xff8264 )
	{
		Prefetch = 0;				/* scroll without prefetch */
		LastCycleScroll8264 = FrameCycles;

		ShifterFrame.Scroll8264Pos.VBL = nVBLs;
		ShifterFrame.Scroll8264Pos.FrameCycles = FrameCycles;
		ShifterFrame.Scroll8264Pos.HBL = HblCounterVideo;
		ShifterFrame.Scroll8264Pos.LineCycles = LineCycles;

#if 0
		if ( ( ScrollCount == 0 ) && ( LastVal8265 > 0 ) && ( LastCycleScroll8265 >= 0 )
			&& ( LastCycleScroll8264 - LastCycleScroll8265 <= 40 ) )
#else
		if ( ( ScrollCount == 0 ) && ( LastVal8265 > 0 )
			&& ( ShifterFrame.Scroll8265Pos.VBL > 0 )		/* a write to ff8265 has been made */
			&& ( ShifterFrame.Scroll8265Pos.VBL == ShifterFrame.Scroll8264Pos.VBL )		/* during the same VBL */
			&& ( ShifterFrame.Scroll8264Pos.FrameCycles - ShifterFrame.Scroll8265Pos.FrameCycles <= 40 ) )
#endif
		{
			LOG_TRACE(TRACE_VIDEO_BORDER_H , "detect ste left+16 pixels\n" );
			Add16px = true;
		}
	}
	else
	{
		Prefetch = 1;				/* scroll with prefetch */
		LastCycleScroll8265 = FrameCycles;

		ShifterFrame.Scroll8265Pos.VBL = nVBLs;
		ShifterFrame.Scroll8265Pos.FrameCycles = FrameCycles;
		ShifterFrame.Scroll8265Pos.HBL = HblCounterVideo;
		ShifterFrame.Scroll8265Pos.LineCycles = LineCycles;

		LastVal8265 = ScrollCount;
		Add16px = false;
	}


	/* If the write was made before display starts on the current line, then */
	/* we can still change the value now. Else, the new values will be used */
	/* for line n+1. */
	/* We must also check the write does not overlap the end of the line */
	if ( ( ( LineCycles <= LINE_START_CYCLE_50 ) && ( nHBL == HblCounterVideo ) )
		|| ( nHBL < nStartHBL ) || ( nHBL >= nEndHBL ) )
	{
		HWScrollCount = ScrollCount;		/* display has not started, we can still change */
		HWScrollPrefetch = Prefetch;
		bSteBorderFlag = Add16px;
		NewHWScrollCount = -1;			/* cancel 'pending' change */
		Delayed = false;
	}
	else
	{
		NewHWScrollCount = ScrollCount;		/* display has started, can't change HWScrollCount now */
		NewHWScrollPrefetch = Prefetch;
		if ( Add16px )
			NewSteBorderFlag = 1;
		else
			NewSteBorderFlag = 0;
		Delayed = true;
	}

	LOG_TRACE(TRACE_VIDEO_STE , "write ste %x hwscroll=%x delayed=%s video_cyc_w=%d line_cyc_w=%d @ nHBL=%d/video_hbl_w=%d pc=%x instr_cyc=%d\n" ,
		RegAddr , ScrollCount, Delayed ? "yes" : "no" ,
		FrameCycles, LineCycles, nHBL, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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
	bTTColorsSync = false;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to ST color register on TT (0xff8240)
 */
void Video_TTColorSTRegs_WriteWord(void)
{
	bTTColorsSTSync = false;
}
