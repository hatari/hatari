/*
  Hatari - video.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Video hardware handling. This code handling all to do with the video chip.
  So, we handle VBLs, HBLs, copying the ST screen to a buffer to simulate the
  TV raster trace, border removal, palette changes per HBL, the 'video address
  pointer' etc...
*/
char Video_rcsid[] = "Hatari $Id: video.c,v 1.32 2005-07-30 09:26:14 eerot Exp $";

#include <SDL.h>
#include <SDL_endian.h>

#include "main.h"
#include "configuration.h"
#include "fdc.h"
#include "int.h"
#include "ikbd.h"
#include "ioMem.h"
#include "keymap.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "screen.h"
#include "shortcut.h"
#include "sound.h"
#include "spec512.h"
#include "stMemory.h"
#include "vdi.h"
#include "video.h"
#include "ymFormat.h"


BOOL bUseHighRes = FALSE;                       /* Use hi-res (ie Mono monitor) */
int nVBLs, nHBL;                                /* VBL Counter, HBL line */
int nStartHBL, nEndHBL;                         /* Start/End HBL for visible screen(64 lines in Top border) */
int OverscanMode;                               /* OVERSCANMODE_xxxx for current display frame */
Uint16 HBLPalettes[(NUM_VISIBLE_LINES+1)*16];   /* 1x16 colour palette per screen line, +1 line just incase write after line 200 */
Uint16 *pHBLPalettes;                           /* Pointer to current palette lists, one per HBL */
Uint32 HBLPaletteMasks[NUM_VISIBLE_LINES+1];    /* Bit mask of palette colours changes, top bit set is resolution change */
Uint32 *pHBLPaletteMasks;
int VBLCounter;                                 /* VBL counter */
int nScreenRefreshRate = 50;                    /* 50 or 60 Hz in color, 70 Hz in mono */
Uint32 VideoBase;                               /* Base address in ST Ram for screen (read on each VBL) */
Uint8 HWScrollCount;				/* HW scroll count, STe only (0...15) */

static Uint8 ScanLineWidth;			/* Scan line width add, STe only (words, minus 1) */
static Uint8 *pVideoRaster;                     /* Pointer to Video raster, after VideoBase in PC address space. Use to copy data on HBL */
static Uint8 VideoShifterByte;                  /* VideoShifter (0xff8260) value store in video chip */
static int LeftRightBorder;                     /* BORDERMASK_xxxx used to simulate left/right border removal */


/*-----------------------------------------------------------------------*/
/*
  Reset video chip
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
  ScanLineWidth = 0;
  HWScrollCount = 0;
  /* Clear ready for new VBL */
  Video_ClearOnVBL();
}

/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void Video_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  MemorySnapShot_Store(&VideoShifterByte,sizeof(VideoShifterByte));
  MemorySnapShot_Store(&bUseHighRes,sizeof(bUseHighRes));
  MemorySnapShot_Store(&nVBLs,sizeof(nVBLs));
  MemorySnapShot_Store(&nHBL,sizeof(nHBL));
  MemorySnapShot_Store(&nStartHBL,sizeof(nStartHBL));
  MemorySnapShot_Store(&nEndHBL,sizeof(nEndHBL));
  MemorySnapShot_Store(&OverscanMode,sizeof(OverscanMode));
  MemorySnapShot_Store(HBLPalettes,sizeof(HBLPalettes));
  MemorySnapShot_Store(HBLPaletteMasks,sizeof(HBLPaletteMasks));
  MemorySnapShot_Store(&VideoBase,sizeof(VideoBase));
  MemorySnapShot_Store(&ScanLineWidth,sizeof(ScanLineWidth));
  MemorySnapShot_Store(&HWScrollCount,sizeof(HWScrollCount));
  MemorySnapShot_Store(&pVideoRaster,sizeof(pVideoRaster));
}

/*-----------------------------------------------------------------------*/
/*
  Called on VBL, set registers ready for frame
*/
void Video_ClearOnVBL(void)
{
  /* New screen, so first HBL */
  nHBL = 0;
  nStartHBL = SCREEN_START_HBL;
  nEndHBL = SCREEN_START_HBL+SCREEN_HEIGHT_HBL;
  OverscanMode = OVERSCANMODE_NONE;

  /* Get screen address pointer, aligned to 256 bytes on ST (ie ignore lowest byte) */
  VideoBase = (Uint32)STMemory_ReadByte(0xff8201)<<16 | (Uint32)STMemory_ReadByte(0xff8203)<<8;
  if (ConfigureParams.System.nMachineType != MACHINE_ST) {
    /* on STe 2 aligned, on Falcon 4 aligned, on TT 4 aligned. We do STe. */
    VideoBase |= STMemory_ReadByte(0xff820d) & ~1;
  }
  pVideoRaster = &STRam[VideoBase];
  pSTScreen = pFrameBuffer->pSTScreen;

  Video_StartHBL();
  Video_SetScreenRasters();
  Spec512_StartVBL();
}


/*-----------------------------------------------------------------------*/
/*
  Calculate and return video address pointer.
*/
static Uint32 Video_CalculateAddress(void)
{
  int X,Y,FrameCycles,nPixelsIn;
  Uint32 VideoAddress;      /* Address of video display in ST screen space */

  /* Find number of cycles passed during frame */
  FrameCycles = Int_FindFrameCycles();

  /* Top of screen is usually 64 lines from VBL(64x512=32768 cycles) */
  if (FrameCycles<(nStartHBL*CYCLES_PER_LINE))
  {
    VideoAddress = 0;
  }
  else
  {
    /* Now find which pixel we are on(ignore left/right borders) */
    /* 96 + 320 + 96 = 512 pixels per scan line(each pixel is one cycle) */
    nPixelsIn = FrameCycles-(nStartHBL*CYCLES_PER_LINE);
    /* Convert this to an X,Y pixel on screen */
    Y = nPixelsIn/512;
    X = nPixelsIn - (Y*512);
    if (X<SCREEN_START_CYCLE)       /* Limit if in NULL area outside screen */
      X = SCREEN_START_CYCLE;
    if (X>(512-SCREEN_START_CYCLE))
      X = (512-SCREEN_START_CYCLE);
    /* X is now from 96 to 416(320 pixels), subtract 96 to give X pixel across screen! */
    X = ((X-SCREEN_START_CYCLE)>>1)&(~1);

    if (Y<(nEndHBL-nStartHBL))      /* Limit to end of screen */
      VideoAddress = (Y*160) + X;
    else
      VideoAddress = ((nEndHBL-nStartHBL)*160);
  }

  /* Offset from start of screen(MUST use address loading into video display) */
  VideoAddress += VideoBase;

  return VideoAddress;
}


/*-----------------------------------------------------------------------*/
/*
  VBL interrupt, draw screen and reset counters
*/
void Video_InterruptHandler_VBL(void)
{
  int PendingCyclesOver;
  int nNewMilliTicks;
  static int nOldMilliTicks = 0;
  signed int nDelay;

  /* Store cycles we went over for this frame(this is our inital count) */
  PendingCyclesOver = -PendingInterruptCount;    /* +ve */

  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();
  /* Start HBL interrupts - MUST do before add in cycles */
  Int_AddAbsoluteInterrupt(CYCLES_ENDLINE,INTERRUPT_VIDEO_ENDLINE);
  Int_AddAbsoluteInterrupt(CYCLES_HBL,INTERRUPT_VIDEO_HBL);
  Int_AddAbsoluteInterrupt(CYCLES_PER_FRAME,INTERRUPT_VIDEO_VBL);

  /* Set frame cycles, used for Video Address */
  nFrameCyclesOver = PendingCyclesOver;      /* Number of cycles into frame */

  /* Set the screen refresh rate */
#if 0
  if(bUseHighRes)
    nScreenRefreshRate = 70;
  else if(IoMem[0xff820a] & 2)               /* Is it 50Hz or is it 60Hz? */
    nScreenRefreshRate = 50;
  else
    nScreenRefreshRate = 60;
#endif

  VBLCounter += 1;

  /* Clear any key presses which are due to be de-bounced (held for one ST frame) */
  Keymap_DebounceAllKeys();
  /* Check shortcut keys */
  ShortCut_CheckKeys();

  /* Use extended VDI resolution? If so, just copy whole screen on VBL rather than per HBL */
  if (bUseVDIRes)
    Video_CopyVDIScreen();

  /* Draw screen, skip frame if need to */
  if (ConfigureParams.Screen.bFrameSkip)
  {
    if (nVBLs&1)
      Screen_Draw();
  }
  else
    Screen_Draw();

  /* Update counter for number of screen refreshes per second(for debugging) */
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
    Int_AddAbsoluteInterrupt(4, 0L);  /* Pass NULL interrupt function to quit cleanly */
    set_special(SPCFLAG_BRK);         /* Assure that CPU core shuts down */
  }

  if (ConfigureParams.System.nMinMaxSpeed != MINMAXSPEED_MAX)
  {
    /* Wait, so we stay in sync with the sound */
    do
    {
      nNewMilliTicks = SDL_GetTicks();
      nDelay = 1000/nScreenRefreshRate - (nNewMilliTicks-nOldMilliTicks);
      if(nDelay > 2)
      {
        /* SDL_Delay seems to be quite inaccurate, so we don't wait the whole time */
        SDL_Delay(nDelay - 1);
      }
    }
    while(nDelay > 0);
    nOldMilliTicks = nNewMilliTicks;
  }
}


/*-----------------------------------------------------------------------*/
/*
  End Of Line interrupt
  As this occurs at the end of a line we cannot get timing for START of first
  line (as in Spectrum 512)
*/
void Video_InterruptHandler_EndLine(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();
  /* Generate new HBL, if need to - there are 313 HBLs per frame */
  if (nHBL<(SCANLINES_PER_FRAME-1))
    Int_AddAbsoluteInterrupt(CYCLES_PER_LINE,INTERRUPT_VIDEO_ENDLINE);

  /* Is this a good place to send the keyboard packets? Done once per frame */
  if(nHBL == nStartHBL)
  {
    /* On each VBL send automatic keyboard packets for mouse, joysticks etc... */
    IKBD_SendAutoKeyboardCommands();
  }

  /* Timer A/B occur at END of first visible screen line in Event Count mode */
  if ( (nHBL>=nStartHBL) && (nHBL<nEndHBL) )
   {
    /* Handle Timers A and B when using Event Count mode(timer taken from HBL) */
    if (MFP_TACR==0x08)        /* Is timer in Event Count mode? */
      MFP_TimerA_EventCount_Interrupt();
    if (MFP_TBCR==0x08)        /* Is timer in Event Count mode? */
      MFP_TimerB_EventCount_Interrupt();
   }

  FDC_UpdateHBL();             /* Update FDC motion */
  Video_EndHBL();              /* Increase HBL count, copy line to display buffer and do any video trickery */

  /* If we don't often pump data into the event queue, the SDL misses events... grr... */
  if( !(nHBL&63) )
  {
    Main_EventHandler();
  }
}


/*-----------------------------------------------------------------------*/
/*
  HBL interrupt - this is very inaccurate on ST and appears to occur around 1/3rd way into
  the display!
*/
void Video_InterruptHandler_HBL(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  /* Generate new Timer AB, if need to - there are 313 HBLs per frame */
  if(nHBL < (SCANLINES_PER_FRAME-1))
    Int_AddAbsoluteInterrupt(CYCLES_PER_LINE,INTERRUPT_VIDEO_HBL);

  M68000_Exception(EXCEPTION_HBLANK);   /* Horizontal blank interrupt, level 2! */
}


/*-----------------------------------------------------------------------*/
/*
  Write to VideoShifter (0xff8260), resolution bits
*/
void Video_WriteToShifter(Uint8 Byte)
{
  static int nLastHBL = -1, LastByte, nLastCycles;
  int nFrameCycles, nLineCycles;
  
  nFrameCycles = Int_FindFrameCycles();

  /* We only care for cycle position in the actual screen line */
  nLineCycles = nFrameCycles % CYCLES_PER_LINE;

  /*fprintf(stderr,"Shifter=0x%2.2X %d (%d) @ %d\n",
          Byte, nFrameCycles, nLineCycles, nHBL);*/

  /* Check if program tries to open left border.
   * FIXME: This is a very inaccurate test that should be improved,
   * but we probably need better CPU cycles emulation first. There's
   * also no support for sync-scrolling yet :-( */
  if (nHBL == nLastHBL && LastByte == 0x02 && Byte == 0x00
      && nLineCycles <= 48 && nLineCycles-nLastCycles <= 16)
  {
    LeftRightBorder |= BORDERMASK_LEFT;
  }

  nLastHBL = nHBL;
  LastByte = Byte;
  nLastCycles = nLineCycles;
}


/*-----------------------------------------------------------------------*/
/*
  Write to VideoSync (0xff820a), Hz setting
*/
void Video_Sync_WriteByte(void)
{
  static int nLastHBL = -1, LastByte, nLastCycles;
  int nFrameCycles, nLineCycles;
  Uint8 Byte;

  /* Note: We're only interested in lower 2 bits (50/60Hz) */
  Byte = IoMem[0xff820a] & 3;

  nFrameCycles = Int_FindFrameCycles();

  /* We only care for cycle position in the actual screen line */
  nLineCycles = nFrameCycles % CYCLES_PER_LINE;

  /*fprintf(stderr,"Sync=0x%2.2X %d (%d) @ %d\n",
          Byte, nFrameCycles, nLineCycles, nHBL);*/

  /* Check if program tries to open a border.
   * FIXME: These are very inaccurate tests that should be improved,
   * but we probably need better CPU cycles emulation first. There's
   * also no support for sync-scrolling yet :-( */
  if (LastByte == 0x00 && Byte == 0x02)   /* switched from 50 Hz to 60 Hz and back to 50 Hz? */
  {
    if (nHBL >= OVERSCAN_TOP && nHBL <= 39 && nStartHBL > FIRST_VISIBLE_HBL)
    {
      /* Top border */
      OverscanMode |= OVERSCANMODE_TOP;       /* Set overscan bit */
      nStartHBL = FIRST_VISIBLE_HBL;          /* New start screen line */
      pHBLPaletteMasks -= OVERSCAN_TOP;
      pHBLPalettes -= OVERSCAN_TOP;
    }
    else if (nHBL == SCREEN_START_HBL+SCREEN_HEIGHT_HBL)
    {
      /* Bottom border */
      OverscanMode |= OVERSCANMODE_BOTTOM;    /* Set overscan bit */
      nEndHBL = SCREEN_START_HBL+SCREEN_HEIGHT_HBL+OVERSCAN_BOTTOM;  /* New end screen line */
    }

    if (nHBL == nLastHBL && nLineCycles >= 400 && nLineCycles <= 480
        && nLineCycles-nLastCycles <= 16)
    {
      /* Right border */
      LeftRightBorder |= BORDERMASK_RIGHT;
    }
  }

  nLastHBL = nHBL;
  LastByte = Byte;
  nLastCycles = nLineCycles;
}


/*-----------------------------------------------------------------------*/
/*
  Reset Sync/Shifter table at start of each HBL
*/
void Video_StartHBL(void)
{
  if (ConfigureParams.System.nMachineType != MACHINE_ST) {
    ScanLineWidth = STMemory_ReadByte(0xff820f);
    HWScrollCount = STMemory_ReadByte(0xff8265);
  }
  LeftRightBorder = 0;
}


/*-----------------------------------------------------------------------*/
/*
  Store whole palette on first line so have reference to work from
*/
static void Video_StoreFirstLinePalette(void)
{
  Uint16 *pp2;
  int i;

  pp2 = (Uint16 *)&IoMem[0xff8240];
  for(i=0; i<16; i++)
    HBLPalettes[i] = SDL_SwapBE16(*pp2++);
  /* And set mask flag with palette and resolution */
  HBLPaletteMasks[0] = (PALETTEMASK_RESOLUTION|PALETTEMASK_PALETTE) | (((unsigned long)IoMem_ReadByte(0xff8260)&0x3)<<16);
}


/*-----------------------------------------------------------------------*/
/*
  Store resolution on each line(used to test if mixed low/medium resolutions)
*/
static void Video_StoreResolution(int y)
{
  /* Clear resolution, and set with current value */
  if (!(bUseHighRes || bUseVDIRes))
  {
    HBLPaletteMasks[y] &= ~(0x3<<16);
    HBLPaletteMasks[y] |= ((unsigned long)IoMem_ReadByte(0xff8260)&0x3)<<16;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Copy line of screen into buffer for conversion later.
  Possible lines may be top/bottom border, and/or left/right borders
*/
static void Video_CopyScreenLine(int BorderMask)
{
  /* Only copy screen line if not doing high VDI resolution */
  if (!bUseVDIRes) {
    BorderMask |= LeftRightBorder;

    if (bUseHighRes) {
      /* Copy for hi-res (no overscan) */
      memcpy(pSTScreen, pVideoRaster, SCREENBYTES_MIDDLE);
      pVideoRaster += SCREENBYTES_MIDDLE;
      /* Each screen line copied to buffer is always same length */
      pSTScreen += SCREENBYTES_MIDDLE;
    }
    else {
      /* Is total blank line? Ie top/bottom border? */
      if (BorderMask&(BORDERMASK_TOP|BORDERMASK_BOTTOM)) {
        /* Clear line to colour '0' */
        memset(pSTScreen,0,SCREENBYTES_LINE);
      }
      else {
        /* Does have left border? If not, clear to colour '0' */
        if (BorderMask&BORDERMASK_LEFT) {
          pVideoRaster += 24-SCREENBYTES_LEFT;
          memcpy(pSTScreen, pVideoRaster, SCREENBYTES_LEFT);
          pVideoRaster += SCREENBYTES_LEFT;
        }
        else
          memset(pSTScreen,0,SCREENBYTES_LEFT);	      
        
        /* Copy middle - always present */
        memcpy(pSTScreen+SCREENBYTES_LEFT, pVideoRaster, SCREENBYTES_MIDDLE);
        pVideoRaster += SCREENBYTES_MIDDLE;

        /* Does have right border? If not, clear to colour '0' */
        if (BorderMask&BORDERMASK_RIGHT) {
          memcpy(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE, pVideoRaster, SCREENBYTES_RIGHT);
          pVideoRaster += 46-SCREENBYTES_RIGHT;
          pVideoRaster += SCREENBYTES_RIGHT;
        }
        else
          memset(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE,0,SCREENBYTES_RIGHT);

	/* ScanLineWidth and HWScrollCount are zero on ST.
         * on STe, Shifter skips the given amount of words
         */
        pVideoRaster += ScanLineWidth*2;
        if (HWScrollCount) {
          /* HW scrolling advances Shifter video counter by one */
          switch(STRes) {
          case ST_HIGH_RES:
            pVideoRaster += 1 * 2;
            break;
          case ST_MEDIUM_RES:
            pVideoRaster += 2 * 2;
            break;
          case ST_LOW_RES:
          default:
            pVideoRaster += 4 * 2;
            break;
          }
        }
      }

      /* Each screen line copied to buffer is always same length */
      pSTScreen += SCREENBYTES_LINE;
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Copy extended GEM resolution screen
*/
void Video_CopyVDIScreen(void)
{
  /* Copy whole screen, don't care about being exact as for GEM only */
  memcpy(pSTScreen, pVideoRaster, ((VDIWidth*VDIPlanes)/8)*VDIHeight);  /* 640x400x16colour */
}


/*-----------------------------------------------------------------------*/
/*
  Check at end of each HBL to see if any Sync/Shifter hardware tricks have been attempted
*/
void Video_EndHBL(void)
{
  /* Are we in possible visible display (including borders)? */
  if ( (nHBL>=FIRST_VISIBLE_HBL) && (nHBL<(FIRST_VISIBLE_HBL+NUM_VISIBLE_LINES)) ) {
    /* Copy line of screen to buffer to simulate TV raster trace - required for mouse cursor display/game updates */
    /* Eg, Lemmings and The Killing Game Show are good examples */

    if (nHBL<nStartHBL)                /* Are we in top border blank (ie no top overscan enabled) */
      Video_CopyScreenLine(BORDERMASK_TOP);
    else if (nHBL>=nEndHBL)            /* Are we in bottom border blank */
      Video_CopyScreenLine(BORDERMASK_BOTTOM);
    else                               /* Must be in visible screen(including overscan), ignore left/right borders for now */
      Video_CopyScreenLine(BORDERMASK_NONE);

    if (nHBL==FIRST_VISIBLE_HBL) {    /* Very first line on screen - HBLPalettes[0] */
      /* Store ALL palette for this line into raster table for datum */
      Video_StoreFirstLinePalette();
    }
    /* Store resolution for every line so can check for mix low/medium screens */
    Video_StoreResolution(nHBL-FIRST_VISIBLE_HBL);
  }

  /* Finally increase HBL count */
  nHBL++;

  Video_StartHBL();                  /* Setup next one */
}


/* Clear raster line table to store changes in palette/resolution on a line basic */
/* Call once on VBL interrupt */
void Video_SetScreenRasters(void)
{
  pHBLPaletteMasks = HBLPaletteMasks;
  pHBLPalettes = HBLPalettes;
  memset(pHBLPaletteMasks, 0, sizeof(unsigned long)*NUM_VISIBLE_LINES);  /* Clear array */
}


/*-----------------------------------------------------------------------*/
/*
  Set pointers to HBLPalette tables to store correct colours/resolutions
*/
void Video_SetHBLPaletteMaskPointers(void)
{
  int FrameCycles;
  int Line;

  /* Top of standard screen is 64 lines from VBL(64x512=32768 cycles) */
  /* Each line is 64+320+64+64(Blank) = 512 pixels per scan line      */
  /* Timer occurs at end of 64+320+64; Display Enable(DE)=Low         */
  /* HBL is incorrect on machine and occurs around 96+ cycles in      */

  /* Top of standard screen is 64 lines from VBL(64x512=32768 cycles) */
  /* Each line is 96 + 320 + 96 = 512 pixels per scan line(each pixel is one cycle) */
  FrameCycles = Int_FindFrameCycles();

  /* Find 'line' into palette - screen starts 64 lines down, less 28 for top overscan */
  /* And if write to last 96 cycle of line it will count as the NEXT line(needed else games may flicker) */
  Line = (FrameCycles-(FIRST_VISIBLE_HBL*CYCLES_PER_LINE)+SCREEN_START_CYCLE)/CYCLES_PER_LINE;
  if (Line<0)          /* Limit to top/bottom of possible visible screen */
    Line = 0;
  if (Line>=NUM_VISIBLE_LINES)
    Line = NUM_VISIBLE_LINES-1;

  /* Store pointers */
  pHBLPaletteMasks = &HBLPaletteMasks[Line];  /* Next mask entry */
  pHBLPalettes = &HBLPalettes[16*Line];       /* Next colour raster list x16 colours */
}


/*-----------------------------------------------------------------------*/
/*
  Write to video address counter (0xff8205, 0xff8207 and 0xff8209).
  Called on STE only and like with base address, you cannot set lowest bit.
*/
void Video_ScreenCounter_WriteByte(void)
{
  Uint32 addr;
  addr = (IoMem[0xff8205] << 16) | (IoMem[0xff8207] << 8) | IoMem[0xff8209];
  pVideoRaster = &STRam[addr & ~1];
}

/*-----------------------------------------------------------------------*/
/*
  Read video address counter high byte (0xff8205)
*/
void Video_ScreenCounterHigh_ReadByte(void)
{
  IoMem[0xff8205] = Video_CalculateAddress() >> 16;   /* Get video address counter high byte */
}

/*-----------------------------------------------------------------------*/
/*
  Read video address counter med byte (0xff8207)
*/
void Video_ScreenCounterMed_ReadByte(void)
{
  IoMem[0xff8207] = Video_CalculateAddress() >> 8;    /* Get video address counter med byte */
}

/*-----------------------------------------------------------------------*/
/*
  Read video address counter low byte (0xff8209)
*/
void Video_ScreenCounterLow_ReadByte(void)
{
  IoMem[0xff8209] = Video_CalculateAddress();         /* Get video address counter low byte */
}


/*-----------------------------------------------------------------------*/
/*
  Read video sync register (0xff820a)
*/
void Video_Sync_ReadByte(void)
{
  /* Nothing... */
}

/*-----------------------------------------------------------------------*/
/*
  Read video base address low byte (0xff820d). A plain ST can only store
  screen addresses rounded to 256 bytes (i.e. no lower byte).
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
/*
  Read video line width register (0xff820f)
*/
void Video_LineWidth_ReadByte(void)
{
  if (ConfigureParams.System.nMachineType == MACHINE_ST)
    IoMem[0xff820f] = 0;        /* On ST this is always 0 */
}

/*-----------------------------------------------------------------------*/
/*
  Read video shifter mode register (0xff8260)
*/
void Video_ShifterMode_ReadByte(void)
{
  if (bUseHighRes)
    IoMem[0xff8260] = 2;                  /* If mono monitor, force to high resolution */
  else
    IoMem[0xff8260] = VideoShifterByte;   /* Read shifter register */
}



/*-----------------------------------------------------------------------*/
/*
  Write to video shifter palette registers (0xff8240-0xff825e)
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
/*
  Write video shifter mode register (0xff860)
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
    *pHBLPaletteMasks |= (((unsigned long)VideoShifterByte|0x04)<<16);
  }
}
