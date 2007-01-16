/*
  Hatari - video.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Video hardware handling. This code handling all to do with the video chip.
  So, we handle VBLs, HBLs, copying the ST screen to a buffer to simulate the
  TV raster trace, border removal, palette changes per HBL, the 'video address
  pointer' etc...
*/
const char Video_rcsid[] = "Hatari $Id: video.c,v 1.66 2007-01-16 18:42:59 thothy Exp $";

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


BOOL bUseHighRes = FALSE;                       /* Use hi-res (ie Mono monitor) */
int nVBLs, nHBL;                                /* VBL Counter, HBL line */
int nStartHBL, nEndHBL;                         /* Start/End HBL for visible screen */
int OverscanMode;                               /* OVERSCANMODE_xxxx for current display frame */
Uint16 HBLPalettes[(NUM_VISIBLE_LINES+1)*16];   /* 1x16 colour palette per screen line, +1 line just incase write after line 200 */
Uint16 *pHBLPalettes;                           /* Pointer to current palette lists, one per HBL */
Uint32 HBLPaletteMasks[NUM_VISIBLE_LINES+1];    /* Bit mask of palette colours changes, top bit set is resolution change */
Uint32 *pHBLPaletteMasks;
int nScreenRefreshRate = 50;                    /* 50 or 60 Hz in color, 70 Hz in mono */
Uint32 VideoBase;                               /* Base address in ST Ram for screen (read on each VBL) */

int nScanlinesPerFrame = 313;                   /* Number of scan lines per frame */
int nCyclesPerLine = 512;                       /* Cycles per horizontal line scan */
static int nFirstVisibleHbl = 34;               /* The first line of the ST screen that is copied to the PC screen buffer */

static Uint8 HWScrollCount;                     /* HW scroll pixel offset, STe only (0...15) */
static Uint8 ScanLineSkip;                      /* Scan line width add, STe only (words, minus 1) */
static Uint8 *pVideoRaster;                     /* Pointer to Video raster, after VideoBase in PC address space. Use to copy data on HBL */
static Uint8 VideoShifterByte;                  /* VideoShifter (0xff8260) value store in video chip */
static int LeftRightBorder;                     /* BORDERMASK_xxxx used to simulate left/right border removal */
static int nLastAccessCycleLeft;                /* Line cycle where program tried to open left border */
static BOOL bSteBorderFlag;                     /* TRUE when screen width has been switched to 336 (e.g. in Obsession) */
static int nTTRes;                              /* TT shifter resolution mode */


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void Video_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  MemorySnapShot_Store(&VideoShifterByte, sizeof(VideoShifterByte));
  MemorySnapShot_Store(&nTTRes, sizeof(nTTRes));
  MemorySnapShot_Store(&bUseHighRes,sizeof(bUseHighRes));
  MemorySnapShot_Store(&nVBLs,sizeof(nVBLs));
  MemorySnapShot_Store(&nHBL,sizeof(nHBL));
  MemorySnapShot_Store(&nStartHBL,sizeof(nStartHBL));
  MemorySnapShot_Store(&nEndHBL,sizeof(nEndHBL));
  MemorySnapShot_Store(&OverscanMode,sizeof(OverscanMode));
  MemorySnapShot_Store(HBLPalettes,sizeof(HBLPalettes));
  MemorySnapShot_Store(HBLPaletteMasks,sizeof(HBLPaletteMasks));
  MemorySnapShot_Store(&VideoBase,sizeof(VideoBase));
  MemorySnapShot_Store(&ScanLineSkip,sizeof(ScanLineSkip));
  MemorySnapShot_Store(&HWScrollCount,sizeof(HWScrollCount));
  MemorySnapShot_Store(&pVideoRaster,sizeof(pVideoRaster));
  MemorySnapShot_Store(&nScanlinesPerFrame, sizeof(nScanlinesPerFrame));
  MemorySnapShot_Store(&nCyclesPerLine, sizeof(nCyclesPerLine));
  MemorySnapShot_Store(&nFirstVisibleHbl, sizeof(nFirstVisibleHbl));
  MemorySnapShot_Store(&bSteBorderFlag, sizeof(bSteBorderFlag));
}


/*-----------------------------------------------------------------------*/
/**
 * Calculate and return video address pointer.
 */
static Uint32 Video_CalculateAddress(void)
{
  int X, FrameCycles;
  Uint32 VideoAddress;      /* Address of video display in ST screen space */

  /* Find number of cycles passed during frame */
  FrameCycles = Cycles_GetCounterOnReadAccess(CYCLES_COUNTER_VIDEO);

  /* Top of screen is usually 63 lines from VBL in 50 Hz */
  if (FrameCycles < nStartHBL*nCyclesPerLine)
  {
    VideoAddress = VideoBase;
  }
  else
  {
    /* Now find which pixel we are on (ignore left/right borders) */
    X = FrameCycles % nCyclesPerLine;
    if (X < SCREEN_START_CYCLE)       /* Limit if in NULL area outside screen */
      X = SCREEN_START_CYCLE;
    if (X > (nCyclesPerLine - SCREEN_START_CYCLE))
      X = (nCyclesPerLine - SCREEN_START_CYCLE);
    /* X is now in the correct range, subtract SCREEN_START_CYCLE to give X pixel across screen! */
    X = ((X-SCREEN_START_CYCLE)>>1)&(~1);

    VideoAddress = pVideoRaster - STRam;
    /* Add line cycles if we have not reached end of screen yet: */
    if (FrameCycles < nEndHBL*nCyclesPerLine)
      VideoAddress += X;
  }

  return VideoAddress;
}


/*-----------------------------------------------------------------------*/
/**
 * Write to VideoShifter (0xff8260), resolution bits
 */
static void Video_WriteToShifter(Uint8 Byte)
{
  static int nLastByte, nLastFrameCycles;
  int nFrameCycles, nLineCycles;
  
  nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);

  /* We only care for cycle position in the actual screen line */
  nLineCycles = nFrameCycles % nCyclesPerLine;

  /*fprintf(stderr,"Shifter=0x%2.2X %d (%d) @ %d\n",
          Byte, nFrameCycles, nLineCycles, nHBL);*/

  /* Check if program tries to open left border.
   * FIXME: This is a very inaccurate test that should be improved,
   * but we probably need better CPU cycles emulation first. There's
   * also no support for sync-scrolling yet :-( */
  if (nLastByte == 0x02 && Byte == 0x00 && nLineCycles <= 12
      && nFrameCycles-nLastFrameCycles <= 16)
  {
    LeftRightBorder |= BORDERMASK_LEFT;
    nLastAccessCycleLeft = nLineCycles;
  }

  nLastByte = Byte;
  nLastFrameCycles = nFrameCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Write to VideoSync (0xff820a), Hz setting
 */
void Video_Sync_WriteByte(void)
{
  static int nLastHBL = -1, LastByte, nLastCycles;
  int nFrameCycles, nLineCycles;
  Uint8 Byte;

  /* Note: We're only interested in lower 2 bits (50/60Hz) */
  Byte = IoMem[0xff820a] & 3;

  nFrameCycles = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);

  /* We only care for cycle position in the actual screen line */
  nLineCycles = nFrameCycles % nCyclesPerLine;

  /*fprintf(stderr,"Sync=0x%2.2X %d (%d) @ %d\n",
          Byte, nFrameCycles, nLineCycles, nHBL);*/

  /* Check if program tries to open a border.
   * FIXME: These are very inaccurate tests that should be improved,
   * but we probably need better CPU cycles emulation first. There's
   * also no support for sync-scrolling yet :-( */
  if (LastByte == 0x02 && Byte == 0x00)   /* switched from 50 Hz to 60 Hz? */
  {
    if (nHBL >= SCREEN_START_HBL_60HZ-1 && nHBL <= SCREEN_START_HBL_60HZ+1)
    {
      /* Top border */
      OverscanMode |= OVERSCANMODE_TOP;       /* Set overscan bit */
      nStartHBL = SCREEN_START_HBL_60HZ;      /* New start screen line */
      pHBLPaletteMasks -= OVERSCAN_TOP;
      pHBLPalettes -= OVERSCAN_TOP;
    }
    else if (nHBL == SCREEN_START_HBL_50HZ+SCREEN_HEIGHT_HBL)
    {
      /* Bottom border */
      OverscanMode |= OVERSCANMODE_BOTTOM;    /* Set overscan bit */
      nEndHBL = SCREEN_START_HBL_50HZ+SCREEN_HEIGHT_HBL+OVERSCAN_BOTTOM;  /* New end screen line */
    }
  }

  if (LastByte == 0x00 && Byte == 0x02)   /* switched from 60 Hz to 50 Hz? */
  {
    if (nHBL == nLastHBL && nLineCycles-nLastCycles <= 16
        && nLineCycles >= (SCREEN_START_CYCLE+320-40)
        && nLineCycles <= (SCREEN_START_CYCLE+320+40))
    {
      /* Right border */
      //fprintf(stderr,"Right sync: %i - %i = %i\n", nLineCycles, nLastAccessCycleLeft, nLineCycles - nLastAccessCycleLeft);
      if (nLineCycles - nLastAccessCycleLeft == 368)
      {
        LeftRightBorder |= BORDERMASK_MIDDLE;   /* Program tries to shorten line by 2 bytes */
      }
      else
      {
        LeftRightBorder |= BORDERMASK_RIGHT;    /* Program tries to open right border */
      }
    }
  }

  nLastHBL = nHBL;
  LastByte = Byte;
  nLastCycles = nLineCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Reset Sync/Shifter table at start of each HBL
 */
static void Video_StartHBL(void)
{
  LeftRightBorder = BORDERMASK_NONE;
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
  for(i=0; i<16; i++)
    HBLPalettes[i] = SDL_SwapBE16(*pp2++);
  /* And set mask flag with palette and resolution */
  HBLPaletteMasks[0] = (PALETTEMASK_RESOLUTION|PALETTEMASK_PALETTE) | (((Uint32)IoMem_ReadByte(0xff8260)&0x3)<<16);
}


/*-----------------------------------------------------------------------*/
/**
 * Store resolution on each line(used to test if mixed low/medium resolutions)
 */
static void Video_StoreResolution(int y)
{
  /* Clear resolution, and set with current value */
  if (!(bUseHighRes || bUseVDIRes))
  {
    HBLPaletteMasks[y] &= ~(0x3<<16);
    HBLPaletteMasks[y] |= ((Uint32)IoMem_ReadByte(0xff8260)&0x3)<<16;
  }
}


/*-----------------------------------------------------------------------*/
/**
 * Copy one line of monochrome screen into buffer for conversion later.
 */
static void Video_CopyScreenLineMono(void)
{
  int i;

  /* Since Hatari does not emulate monochrome HBLs correctly yet
   * (only 200 are raised, just like in low resolution), we have to
   * copy two lines each HBL to finally copy all 400 lines. */
  for (i = 0; i < 2; i++)
  {
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

    /* ScanLineSkip is zero on ST. */
    /* On STE, the Shifter skips the given amount of words. */
    pVideoRaster += ScanLineSkip*2;

    /* Each screen line copied to buffer is always same length */
    pSTScreen += SCREENBYTES_MONOLINE;
  }
}


/*-----------------------------------------------------------------------*/
/**
 * Copy one line of color screen into buffer for conversion later.
 * Possible lines may be top/bottom border, and/or left/right borders.
 */
static void Video_CopyScreenLineColor(void)
{
  /* Is total blank line? I.e. top/bottom border? */
  if (nHBL < nStartHBL || nHBL >= nEndHBL)
  {
    /* Clear line to color '0' */
    memset(pSTScreen, 0, SCREENBYTES_LINE);
  }
  else
  {
    /* Does have left border? If not, clear to color '0' */
    if (LeftRightBorder & BORDERMASK_LEFT)
    {
      /* The "-2" in the following line is needed so that the offset is a multiple of 8 */
      pVideoRaster += BORDERBYTES_LEFT-SCREENBYTES_LEFT-2;
      memcpy(pSTScreen, pVideoRaster, SCREENBYTES_LEFT);
      pVideoRaster += SCREENBYTES_LEFT;
    }
    else
      memset(pSTScreen,0,SCREENBYTES_LEFT);

    /* Copy middle - always present */
    memcpy(pSTScreen+SCREENBYTES_LEFT, pVideoRaster, SCREENBYTES_MIDDLE);
    pVideoRaster += SCREENBYTES_MIDDLE;

    /* Does have right border? */
    if (LeftRightBorder & BORDERMASK_RIGHT)
    {
      memcpy(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE, pVideoRaster, SCREENBYTES_RIGHT);
      pVideoRaster += BORDERBYTES_RIGHT-SCREENBYTES_RIGHT;
      pVideoRaster += SCREENBYTES_RIGHT;
    }
    else if (LeftRightBorder & BORDERMASK_MIDDLE)
    {
      /* Shortened line by 2 bytes? */
      memset(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE-2, 0, SCREENBYTES_RIGHT+2);
      pVideoRaster -= 2;
    }
    else
    {
      /* Simply clear right border to '0' */
      memset(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE,0,SCREENBYTES_RIGHT);
    }

    /* Correct the "-2" offset for pVideoRaster from BORDERMASK_LEFT above */
    if (LeftRightBorder & BORDERMASK_LEFT)
      pVideoRaster += 2;

    if (bSteBorderFlag)
    {
      memcpy(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE, pVideoRaster, 4*2);
      pVideoRaster += 4 * 2;
    }
    else if (HWScrollCount)     /* Handle STE fine scrolling (HWScrollCount is zero on ST) */
    {
      Uint16 *pScrollAdj;       /* Pointer to actual position in line */
      int nNegScrollCnt;
      Uint16 *pScrollEndAddr;   /* Pointer to end of the line */

      nNegScrollCnt = 16 - HWScrollCount;
      if (LeftRightBorder & BORDERMASK_LEFT)
        pScrollAdj = (Uint16 *)pSTScreen;
      else
        pScrollAdj = (Uint16 *)(pSTScreen + SCREENBYTES_LEFT);
      if (LeftRightBorder & BORDERMASK_RIGHT)
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
        /* Shift the whole line by the given scroll count */
        while (pScrollAdj < pScrollEndAddr)
        {
          do_put_mem_word(pScrollAdj, (do_get_mem_word(pScrollAdj) << HWScrollCount)
                          | (do_get_mem_word(pScrollAdj+4) >> nNegScrollCnt));
          ++pScrollAdj;
        }
        /* Handle the last 16 pixels of the line */
        if (LeftRightBorder & BORDERMASK_RIGHT)
        {
          /* When right border is open, we have to deal with this ugly offset
           * of 46-SCREENBYTES_RIGHT=30 - The demo "Mind rewind" is a good example */
          do_put_mem_word(pScrollAdj+0, (do_get_mem_word(pScrollAdj+0) << HWScrollCount)
                          | (do_get_mem_word(pVideoRaster-30) >> nNegScrollCnt));
          do_put_mem_word(pScrollAdj+1, (do_get_mem_word(pScrollAdj+1) << HWScrollCount)
                          | (do_get_mem_word(pVideoRaster-28) >> nNegScrollCnt));
          do_put_mem_word(pScrollAdj+2, (do_get_mem_word(pScrollAdj+2) << HWScrollCount)
                          | (do_get_mem_word(pVideoRaster-26) >> nNegScrollCnt));
          do_put_mem_word(pScrollAdj+3, (do_get_mem_word(pScrollAdj+3) << HWScrollCount)
                          | (do_get_mem_word(pVideoRaster-24) >> nNegScrollCnt));
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
      }
    }

    /* ScanLineSkip is zero on ST. */
    /* On STE, the Shifter skips the given amount of words. */
    pVideoRaster += ScanLineSkip*2;
  }

  /* Each screen line copied to buffer is always same length */
  pSTScreen += SCREENBYTES_LINE;
}


/*-----------------------------------------------------------------------*/
/**
 * Copy line of screen into buffer for conversion later.
 */
static void Video_CopyScreenLine(void)
{
  /* Only copy screen line if not doing high VDI resolution */
  if (!bUseVDIRes)
  {
    if (bUseHighRes)
    {
      /* Copy for hi-res (no overscan) */
      Video_CopyScreenLineMono();
    }
    else
    {
      /* Copy for low and medium resolution */
      Video_CopyScreenLineColor();
    }
  }
}


/*-----------------------------------------------------------------------*/
/**
 * Copy extended GEM resolution screen
 */
static void Video_CopyVDIScreen(void)
{
  /* Copy whole screen, don't care about being exact as for GEM only */
  memcpy(pSTScreen, pVideoRaster, ((VDIWidth*VDIPlanes)/8)*VDIHeight);  /* 640x400x16colour */
}


/*-----------------------------------------------------------------------*/
/**
 * Check at end of each HBL to see if any Sync/Shifter hardware tricks have been attempted
 */
static void Video_EndHBL(void)
{
  Uint8 nSyncByte = IoMem_ReadByte(0xff820a);

  /* Check if we need to open borders: If we are running basically in 50 Hz, but
   * a program switched to 60 Hz at certain screen lines, we have to open the
   * corresponding border. The "Level 16" fullscreen in the Union demo is a good example. */
  if ((nSyncByte & 2) == 0)
  {
    if (nHBL == SCREEN_START_HBL_60HZ-1 && nStartHBL == SCREEN_START_HBL_50HZ)
    {
      /* Top border */
      OverscanMode |= OVERSCANMODE_TOP;       /* Set overscan bit */
      nStartHBL = SCREEN_START_HBL_60HZ;      /* New start screen line */
      pHBLPaletteMasks -= OVERSCAN_TOP;
      pHBLPalettes -= OVERSCAN_TOP;
    }
    else if (nHBL == SCREEN_START_HBL_50HZ+SCREEN_HEIGHT_HBL-1
             && nEndHBL == SCREEN_START_HBL_50HZ+SCREEN_HEIGHT_HBL)
    {
      /* Bottom border */
      OverscanMode |= OVERSCANMODE_BOTTOM;    /* Set overscan bit */
      nEndHBL = SCREEN_START_HBL_50HZ+SCREEN_HEIGHT_HBL+OVERSCAN_BOTTOM;  /* New end screen line */
    }
  }

  /* Are we in possible visible display (including borders)? */
  if (nHBL >= nFirstVisibleHbl && nHBL < nFirstVisibleHbl+NUM_VISIBLE_LINES)
  {
    /* Copy line of screen to buffer to simulate TV raster trace
     * - required for mouse cursor display/game updates
     * Eg, Lemmings and The Killing Game Show are good examples */
    Video_CopyScreenLine();

    if (nHBL == nFirstVisibleHbl)    /* Very first line on screen - HBLPalettes[0] */
    {
      /* Store ALL palette for this line into raster table for datum */
      Video_StoreFirstLinePalette();
    }
    /* Store resolution for every line so can check for mix low/medium screens */
    Video_StoreResolution(nHBL-nFirstVisibleHbl);
  }

  /* Finally increase HBL count */
  nHBL++;

  Video_StartHBL();                  /* Setup next one */
}


/*-----------------------------------------------------------------------*/
/**
 * HBL interrupt - this is very inaccurate on ST and appears to occur around
 * 1/3rd way into the display!
 */
void Video_InterruptHandler_HBL(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  /* Generate new HBL, if need to - there are 313 HBLs per frame in 50 Hz */
  if (nHBL < nScanlinesPerFrame-1)
    Int_AddAbsoluteInterrupt(nCyclesPerLine, INTERRUPT_VIDEO_HBL);

  M68000_Exception(EXCEPTION_HBLANK);   /* Horizontal blank interrupt, level 2! */
}


/*-----------------------------------------------------------------------*/
/**
 * End Of Line interrupt
 *  As this occurs at the end of a line we cannot get timing for START of first
 * line (as in Spectrum 512)
 */
void Video_InterruptHandler_EndLine(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();
  /* Generate new HBL, if need to - there are 313 HBLs per frame */
  if (nHBL < nScanlinesPerFrame-1)
    Int_AddAbsoluteInterrupt(nCyclesPerLine, INTERRUPT_VIDEO_ENDLINE);

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

  FDC_UpdateHBL();             /* Update FDC motion */
  Video_EndHBL();              /* Increase HBL count, copy line to display buffer and do any video trickery */

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

  /* Top of standard screen is 63 lines from VBL (in 50 Hz)      */
  /* Each line is 64+320+64+64(Blank) = 512 pixels per scan line */
  /* Timer occurs at end of 64+320+64; Display Enable(DE)=Low    */
  /* HBL is incorrect on machine and occurs around 96+ cycles in */

  FrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);

  /* Find 'line' into palette - screen starts 63 lines down, less 29 for top overscan */
  /* And if write to last 96 cycle of line it will count as the NEXT line(needed else games may flicker) */
  Line = (FrameCycles-(nFirstVisibleHbl*nCyclesPerLine)+SCREEN_START_CYCLE)/nCyclesPerLine;
  if (Line<0)          /* Limit to top/bottom of possible visible screen */
    Line = 0;
  if (Line>=NUM_VISIBLE_LINES)
    Line = NUM_VISIBLE_LINES-1;

  /* Store pointers */
  pHBLPaletteMasks = &HBLPaletteMasks[Line];  /* Next mask entry */
  pHBLPalettes = &HBLPalettes[16*Line];       /* Next colour raster list x16 colours */
}


/*-----------------------------------------------------------------------*/
/**
 * Set video shifter timing variables according to screen refresh rate
 */
static void Video_ResetShifterTimings(void)
{
  Uint8 nSyncByte;

  nSyncByte = IoMem_ReadByte(0xff820a);

  /* Check if running in 50 Hz or in 60 Hz */
  if (nSyncByte & 2)
  {
    /* 50 Hz */
    nStartHBL = SCREEN_START_HBL_50HZ;
    nScanlinesPerFrame = SCANLINES_PER_FRAME_50HZ;
    nCyclesPerLine = CYCLES_PER_LINE_50HZ;
    nFirstVisibleHbl = FIRST_VISIBLE_HBL_50HZ;
  }
  else
  {
    /* 60 Hz */
    nStartHBL = SCREEN_START_HBL_60HZ;
    nScanlinesPerFrame = SCANLINES_PER_FRAME_60HZ;
    nCyclesPerLine = CYCLES_PER_LINE_60HZ;
    nFirstVisibleHbl = FIRST_VISIBLE_HBL_60HZ;
  }

  nEndHBL = nStartHBL + SCREEN_HEIGHT_HBL;

  /* Set the screen refresh rate */
#if 0
  if(bUseHighRes)
    nScreenRefreshRate = 70;
  else if (nSyncByte & 2)               /* Is it 50Hz or is it 60Hz? */
    nScreenRefreshRate = 50;
  else
    nScreenRefreshRate = 60;
#endif
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
  Spec512_StartVBL();
}


/*-----------------------------------------------------------------------*/
/**
 * Get width, height and bpp according to TT-Resolution
 */
void Video_GetTTRes(int *width, int *height, int *bpp)
{
  switch (nTTRes)
  {
    case 0: *width = 320; *height = 200; *bpp = 4; break;
    case 1: *width = 640; *height = 200; *bpp = 2; break;
    case 2: *width = 640; *height = 400; *bpp = 1; break;
    case 4: *width = 640; *height = 480; *bpp = 4; break;
    case 6: *width = 1280; *height = 960; *bpp = 1; break;
    case 7: *width = 320; *height = 480; *bpp = 8; break;
    default:
      fprintf(stderr, "TT res error!\n");
      *width = 320; *height = 200; *bpp = 4;
      break;
  }
}


/*-----------------------------------------------------------------------*/
/**
 * Draw screen (either with ST/STE shifter drawing functions or with
 * Videl drawing functions)
 */
static void Video_DrawScreen(void)
{
  /* Skip frame if need to */
  if (nVBLs % (ConfigureParams.Screen.FrameSkips+1))
    return;

  /* Use extended VDI resolution?
   * If so, just copy whole screen on VBL rather than per HBL */
  if (bUseVDIRes)
    Video_CopyVDIScreen();

  /* Now draw the screen! */
#if ENABLE_FALCON
  if (ConfigureParams.System.nMachineType == MACHINE_FALCON && !bUseVDIRes)
  {
    VIDEL_renderScreen();
  }
  else if (ConfigureParams.System.nMachineType == MACHINE_TT && !bUseVDIRes)
  {
    static int nPrevTTRes = -1;
    int width, height, bpp;
    Video_GetTTRes(&width, &height, &bpp);
    if (nTTRes != nPrevTTRes)
    {
      HostScreen_setWindowSize(width, height, 8);
    }
    /* Yes, we are abusing the Videl routines for rendering the TT modes! */
    if (ConfigureParams.Screen.bZoomLowRes)
      VIDEL_ConvertScreenZoom(width, height, bpp, width * bpp / 16);
    else
      VIDEL_ConvertScreenNoZoom(width, height, bpp, width * bpp / 16);
    HostScreen_update1( FALSE );
    nPrevTTRes = nTTRes;
  }
  else
#endif
    Screen_Draw();
}


/*-----------------------------------------------------------------------*/
/**
 * Start HBL and VBL interrupts.
 */
void Video_StartInterrupts(void)
{
  Int_AddAbsoluteInterrupt(nCyclesPerLine-96, INTERRUPT_VIDEO_ENDLINE);
  Int_AddAbsoluteInterrupt(nCyclesPerLine, INTERRUPT_VIDEO_HBL);
  Int_AddAbsoluteInterrupt(CYCLES_PER_FRAME, INTERRUPT_VIDEO_VBL);
}


/*-----------------------------------------------------------------------*/
/**
 * VBL interrupt, draw screen and reset counters
 */
void Video_InterruptHandler_VBL(void)
{
  int PendingCyclesOver;

  /* Store cycles we went over for this frame(this is our inital count) */
  PendingCyclesOver = -PendingInterruptCount;    /* +ve */

  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  /* Start VBL & HBL interrupts */
  Video_StartInterrupts();

  /* Set frame cycles, used for Video Address */
  Cycles_SetCounter(CYCLES_COUNTER_VIDEO, PendingCyclesOver);

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

  M68000_Exception(EXCEPTION_VBLANK);   /* Vertical blank interrupt, level 4! */

  /* And handle any messages, check for quit message */
  Main_EventHandler();         /* Process messages, set 'bQuitProgram' if user tries to quit */
  if (bQuitProgram)
  {
    /* Pass NULL interrupt function to quit cleanly */
    Int_AddAbsoluteInterrupt(4, INTERRUPT_NULL);
    set_special(SPCFLAG_BRK);         /* Assure that CPU core shuts down */
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
  if(bUseVDIRes)
    VideoShifterByte = VDIRes;

  /* Reset VBL counter */
  nVBLs = 0;
  /* Reset addresses */
  VideoBase = 0L;
  /* Reset STe screen variables */
  ScanLineSkip = 0;
  HWScrollCount = 0;
  bSteBorderFlag = FALSE;

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
}

/*-----------------------------------------------------------------------*/
/**
 * Read video address counter high byte (0xff8205)
 */
void Video_ScreenCounterHigh_ReadByte(void)
{
  IoMem[0xff8205] = Video_CalculateAddress() >> 16;   /* Get video address counter high byte */
}

/*-----------------------------------------------------------------------*/
/**
 * Read video address counter med byte (0xff8207)
 */
void Video_ScreenCounterMed_ReadByte(void)
{
  IoMem[0xff8207] = Video_CalculateAddress() >> 8;    /* Get video address counter med byte */
}

/*-----------------------------------------------------------------------*/
/**
 * Read video address counter low byte (0xff8209)
 */
void Video_ScreenCounterLow_ReadByte(void)
{
  IoMem[0xff8209] = Video_CalculateAddress();         /* Get video address counter low byte */
}

/*-----------------------------------------------------------------------*/
/**
 * Write to video address counter (0xff8205, 0xff8207 and 0xff8209).
 * Called on STE only and like with base address, you cannot set lowest bit.
 */
void Video_ScreenCounter_WriteByte(void)
{
  Uint32 addr;
  addr = (IoMem[0xff8205] << 16) | (IoMem[0xff8207] << 8) | IoMem[0xff8209];
  pVideoRaster = &STRam[addr & ~1];
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
    IoMem[0xff820f] = ScanLineSkip;
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
 */
void Video_LineWidth_WriteByte(void)
{
    ScanLineSkip = IoMem_ReadByte(0xff820f);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to video shifter palette registers (0xff8240-0xff825e)
 */
static void Video_ColorReg_WriteWord(Uint32 addr)
{
  if (!bUseHighRes)                        /* Don't store if hi-res */
  {
    int idx;
    Uint16 col;
    Video_SetHBLPaletteMaskPointers();     /* Set 'pHBLPalettes' etc.. according cycles into frame */
    col = IoMem_ReadWord(addr);
    if (ConfigureParams.System.nMachineType == MACHINE_ST)
      col &= 0x777;                          /* Mask off to ST 512 palette */
    else
      col &= 0xfff;                          /* Mask off to STe 4096 palette */
    IoMem_WriteWord(addr, col);            /* (some games write 0xFFFF and read back to see if STe) */
    Spec512_StoreCyclePalette(col, addr);  /* Store colour into CyclePalettes[] */
    idx = (addr-0xff8240)/2;               /* words */
    pHBLPalettes[idx] = col;               /* Set colour x */
    *pHBLPaletteMasks |= 1 << idx;         /* And mask */
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
 * Write video shifter mode register (0xff860)
 */
void Video_ShifterMode_WriteByte(void)
{
  if (!bUseHighRes && !bUseVDIRes)                    /* Don't store if hi-res and don't store if VDI resolution */
  {
    VideoShifterByte = IoMem[0xff8260] & 3;           /* We only care for lower 2-bits */
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
  static BOOL bFirstSteAccess = FALSE;

  HWScrollCount = IoMem[0xff8265];

  /*fprintf(stderr, "Write to 0x%x (0x%x, 0x%x, %i)\n", IoAccessBaseAddress,
          IoMem[0xff8264], HWScrollCount, nIoMemAccessSize);*/

  if (IoAccessBaseAddress == 0xff8264 && nIoMemAccessSize == SIZE_WORD
      && HWScrollCount == 1)
  {
    /*fprintf(stderr, "STE border removal - access 1\n");*/
    bFirstSteAccess = TRUE;
  }
  else if (bFirstSteAccess && HWScrollCount == 1 &&
           IoAccessBaseAddress == 0xff8264 && nIoMemAccessSize == SIZE_BYTE)
  {
    /*fprintf(stderr, "STE border removal - access 2\n");*/
    bSteBorderFlag = TRUE;
  }
  else
  {
    bFirstSteAccess = bSteBorderFlag = FALSE;
  }

  HWScrollCount &= 0x0f;
}


/*-----------------------------------------------------------------------*/
/**
 * Write to TT shifter mode register (0xff8262)
 */
void Video_TTShiftMode_WriteWord(void)
{
	nTTRes = IoMem_ReadByte(0xff8262) & 7;

	/*fprintf(stderr, "Write to FF8262: %x, res=%i\n", IoMem_ReadWord(0xff8262),nTTRes);*/

	/* Is it an ST compatible resolution? */
	if (nTTRes <= 2)
	{
		IoMem_WriteByte(0xff8260, nTTRes);
		Video_ShifterMode_WriteByte();
	}
}
