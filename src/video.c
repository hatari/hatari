/*
  Hatari - video.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Video hardware handling. This code handling all to do with the video chip. So, we handle
  VBLs, HBLs, copying the ST screen to a buffer to simulate the TV raster trace, border
  removal, palette changes per HBL, the 'video address pointer' etc...
*/
static char rcsid[] = "Hatari $Id: video.c,v 1.15 2003-04-16 12:49:09 thothy Exp $";

#include <SDL.h>

#include "main.h"
#include "debug.h"
#include "decode.h"
#include "configuration.h"
#include "fdc.h"
#include "int.h"
#include "ikbd.h"
#include "keymap.h"
#include "m68000.h"
#include "memAlloc.h"
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

#include "syncTables.h"                         /* Clock-cycle tables */


long VideoAddress;                              /* Address of video display in ST screen space */
unsigned char VideoSyncByte,VideoShifterByte;   /* VideoSync (0xff820a) and VideoShifter(0xff8260) values store in video chip */
BOOL bUseHighRes = FALSE;                       /* Use hi-res (ie Mono monitor) */
int nVBLs, nHBL;                                /* VBL Counter, HBL line */
int nStartHBL,nEndHBL;                          /* Start/End HBL for visible screen(64 lines in Top border) */
int OverscanMode;                               /* OVERSCANMODE_xxxx for current display frame */
unsigned short int HBLPalettes[(NUM_VISIBLE_LINES+1)*16];  /* 1x16 colour palette per screen line, +1 line just incase write after line 200 */
unsigned long HBLPaletteMasks[NUM_VISIBLE_LINES+1];  /* Bit mask of palette colours changes, top bit set is resolution change */
unsigned short int *pHBLPalettes;               /* Pointer to current palette lists, one per HBL */
unsigned long *pHBLPaletteMasks;
unsigned long VideoBase;                        /* Base address in ST Ram for screen(read on each VBL) */
unsigned long VideoRaster;                      /* Pointer to Video raster, after VideoBase in PC address space. Use to copy data on HBL */
int SyncHandler_Value;                          /* Value to pass to 'Video_SyncHandler_xxxx' functions */
int LeftRightBorder;                            /* BORDERMASK_xxxx used to simulate left/right border removal */
int VBLCounter;                                 /* VBL counter */
int nScreenRefreshRate = 50;                    /* 50 or 60 Hz in color, 70 Hz in mono */


/*-----------------------------------------------------------------------*/
/*
  Reset video chip
*/
void Video_Reset(void)
{
  /* NOTE! Must reset all of these register type things here!!!! */
  VideoSyncByte = 0;
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
  MemorySnapShot_Store(&VideoAddress,sizeof(VideoAddress));
  MemorySnapShot_Store(&VideoSyncByte,sizeof(VideoSyncByte));
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
  MemorySnapShot_Store(&VideoRaster,sizeof(VideoRaster));
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

  /* Get screen address pointer, align to 256 bytes(ie ignore lowest byte) */
  VideoBase = (unsigned long)STMemory_ReadByte(0xff8201)<<16 | (unsigned long)STMemory_ReadByte(0xff8203)<<8;
  VideoRaster = (unsigned long)STRam+VideoBase;
  pSTScreen = pFrameBuffer->pSTScreen;

  Video_StartHBL();
  Video_SetScreenRasters();
  Spec512_StartVBL();
}

/*-----------------------------------------------------------------------*/
/*
  Calculate Video address pointer and store in 'VideoAddress'
*/
void Video_CalculateAddress(void)
{
  int X,Y,FrameCycles,nPixelsIn;

  /* Find number of cycles passed during frame */
  FrameCycles = Int_FindFrameCycles();

  /* Top of screen is usually 64 lines from VBL(64x512=32768 cycles) */
  if (FrameCycles<(nStartHBL*CYCLES_PER_LINE))
    VideoAddress = 0;
  else {
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
}

/*-----------------------------------------------------------------------*/
/*
  Read Video address pointer(from current cycle count), and return 24-bit address in 'ebx'
*/
unsigned long Video_ReadAddress(void)
{
  Video_CalculateAddress();  /* Find address from current cycle count into display frame */
  return( VideoAddress );
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
  else if(STRam[0xff820a] & 2)               /* Is it 50Hz or is it 60Hz? */
    nScreenRefreshRate = 50;
  else
    nScreenRefreshRate = 60;
#endif

  VBLCounter += 1;

  /* Clear any key presses which are due to be de-bounced (held for one ST frame) */
  Keymap_DebounceAllKeys();
  /* Check 'Function' keys, so if press F12 we update screen correctly to Window! */
  ShortCut_CheckKeys();
  /* See if need to save/restore emulation state during this 'safe-zone' */
  MemorySnapShot_CheckSaveRestore();
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

  MakeSR();
  if (4>FIND_IPL)              /* Vertical blank, level 4! */
  {
    ExceptionVector = EXCEPTION_VBLANK;
    M68000_Exception();        /* VBL interrupt */
  }
  else
  {
    /* Higher priority interrupt is currently being executed(eg MFP). Set VBL to occur later */
    if (!Int_InterruptActive(INTERRUPT_VIDEO_VBL_PENDING))
      Int_AddAbsoluteInterrupt(100,INTERRUPT_VIDEO_VBL_PENDING);
  }

  /* And handle any messages, check for quit message */
  Main_EventHandler();         /* Process messages, set 'bQuitProgram' if user tries to quit */
  if(bQuitProgram)
    Int_AddAbsoluteInterrupt(4, 0L);  /* Pass NULL interrupt function to quit cleanly */

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


/*-----------------------------------------------------------------------*/
/*
  This doesn't make sense... I always thought if a 68000 interrupt, eg the VBL, occurs while a
  higher priority interrupt was in service the interrupt was ignored. This does not seem to be
  the case. This really needs checking on a real ST, but I have noticed some menu discs run
  sound routines from the MFP timers which overlap the VBL, yet the VBL still occurs...
*/
void Video_InterruptHandler_VBL_Pending(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  MakeSR();
  /* Check if can execute VBL */
  if (4>FIND_IPL)              /* Vertical blank, level 4! */
  {
    ExceptionVector = EXCEPTION_VBLANK;
    M68000_Exception();        /* VBL interrupt */
  }
  else
    Int_AddAbsoluteInterrupt(100,INTERRUPT_VIDEO_VBL_PENDING);
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
  //  if ( (nHBL>=FIRST_VISIBLE_HBL) && (nHBL<FIRST_VISIBLE_HBL+NUM_VISIBLE_LINES) )
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
  if( !(nHBL&127) )
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
  if (nHBL<(SCANLINES_PER_FRAME-1)) {
    Int_AddAbsoluteInterrupt(CYCLES_PER_LINE,INTERRUPT_VIDEO_HBL);
  }

  MakeSR();
  if (2>FIND_IPL)                  /* Horizontal blank, level 2! */
  {
    ExceptionVector = EXCEPTION_HBLANK;
    M68000_Exception();            /* HBL interrupt */
  }
}


/*-----------------------------------------------------------------------*/
/*
  Sync Table handlers
*/
void Video_SyncHandler_SetLeftRightBorder(void)
{
  LeftRightBorder |= SyncHandler_Value;   /* Turn on left/right borders */
}

void Video_SyncHandler_SetSyncScrollOffset(void)
{
  VideoRaster += SyncHandler_Value;       /* Add offset to video address(sync-scroll) */
}

void Video_SyncHandler_SetTopBorder(void)
{
  OverscanMode |= OVERSCANMODE_TOP;       /* Set overscan bit */
  nStartHBL = FIRST_VISIBLE_HBL;          /* New start screen line */
}

void Video_SyncHandler_SetBottomBorder(void)
{
  OverscanMode |= OVERSCANMODE_BOTTOM;    /* Set overscan bit */
  nEndHBL = SCREEN_START_HBL+SCREEN_HEIGHT_HBL+OVERSCAN_BOTTOM;  /* New end screen line */
}


/*-----------------------------------------------------------------------*/
/*
  Write to VideoShifter(0xff8260), resolution bits
*/
void Video_WriteToShifter(void)
{
  /* Store into table so can test at end of HBL */
  Video_StoreSyncShifterAccess(0xff8260,VideoShifterByte);
}


/*-----------------------------------------------------------------------*/
/*
  Write to VideoSync(0xff820a), Hz setting
*/
void Video_WriteToSync(void)
{
  /* Store into table so can test at end of HBL */
  Video_StoreSyncShifterAccess(0xff820a,VideoSyncByte);
}


/*-----------------------------------------------------------------------*/
/*
  Check access Video(0xff8260 or 0xff820a) to see if opened top/bottom/left/right borders or
  even tried for a SyncScroll
*/
BOOL Video_CheckSyncShifterTable(unsigned int Address, unsigned char Byte, int FrameCycles, SYNCSHIFTER_ACCESS_TABLE *pAccessTable)
{
  SYNCSHIFTER_ACCESS *pSyncShifter;
  BOOL bFoundMatch=FALSE;
  int i=0;

  /* Scan each shifter entry to see if this new values matches - if not rest */
  while(pAccessTable[i].nChecks) {
    /* Get table of entries, index at 'counter' */
    pSyncShifter = &pAccessTable[i].pSyncShifterAccess[pAccessTable[i].nCount];
    /* Does next value match table? */
    if ( (pSyncShifter->Address==Address) && (pSyncShifter->Byte==Byte) && (pSyncShifter->FrameCycles==FrameCycles) ) {
      /* Set flag (used for debugging) */
      bFoundMatch = TRUE;

      /* Got match increase count */
      pAccessTable[i].nCount++;
      /* Is table complete? */
      if (pAccessTable[i].nCount==pAccessTable[i].nChecks) {
        /* Reset count, ready for next time */
        pAccessTable[i].nCount = 0;
        /* Yes, call handler function with passed variable */
        SyncHandler_Value = pAccessTable[i].Value;
        CALL_VAR(pAccessTable[i].pFunc);
      }
    }
    else {
      /* No match, reset table count */
      pAccessTable[i].nCount = 0;
    }

    i++;
  }

  return(bFoundMatch);
}


/*-----------------------------------------------------------------------*/
/*
  Store Sync/Shifter write into table so can check at end of HBL for border/sync-scroll hardware tricks
*/
void Video_StoreSyncShifterAccess(unsigned int Address,unsigned char Byte)
{
  int FrameCycles = Int_FindFrameCycles();

  /* Get back to where instruction started for timing */
  //FrameCycles -= lastInstructionCycles;

//  char szString[256];
//  sprintf(szString,"0x%X=0x%2.2X %d(%d) @ %d",Address,Byte,FrameCycles,FrameCycles&511,nHBL);
//  debug << szString << endl;

  Video_CheckSyncShifterTable(Address,Byte,FrameCycles&511, pLeftRightBorderAccessTable);
//  Video_CheckSyncShifterTable(Address,Byte,FrameCycles&511, pSyncScrollerAccessTable);
}


/*-----------------------------------------------------------------------*/
/*
  Reset Sync/Shifter table at start of each HBL
*/
void Video_StartHBL(void)
{
  LeftRightBorder = 0;
}


/*-----------------------------------------------------------------------*/
/*
  Store whole palette on first line so have reference to work from
*/
void Video_StoreFirstLinePalette(void)
{
  unsigned short int *pp2;
  int i;

  pp2 = (unsigned short int *)((unsigned long)STRam+0xff8240);
  for(i=0; i<16; i++)
    HBLPalettes[i] = STMemory_Swap68000Int(*pp2++);
  /* And set mask flag with palette and resolution */
  HBLPaletteMasks[0] = (PALETTEMASK_RESOLUTION|PALETTEMASK_PALETTE) | (((unsigned long)STMemory_ReadByte(0xff8260)&0x3)<<16);
}


/*-----------------------------------------------------------------------*/
/*
  Store resolution on each line(used to test if mixed low/medium resolutions)
*/
void Video_StoreResolution(int y)
{
  /* Clear resolution, and set with current value */
  if (!(bUseHighRes || bUseVDIRes) ) {
    HBLPaletteMasks[y] &= ~(0x3<<16);
    HBLPaletteMasks[y] |= ((unsigned long)STMemory_ReadByte(0xff8260)&0x3)<<16;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Copy line of screen into buffer for conversion later.
  Possible lines may be top/bottom border, and/or left/right borders
*/
void Video_CopyScreenLine(int BorderMask)
{
  /* Only copy screen line if not doing high VDI resolution */
  if (!bUseVDIRes) {
    BorderMask |= LeftRightBorder;

    if (bUseHighRes) {
      /* Copy for hi-res (no overscan) */
      memcpy(pSTScreen,(char *)VideoRaster,SCREENBYTES_MIDDLE);
      VideoRaster += SCREENBYTES_MIDDLE;
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
          VideoRaster += 24-SCREENBYTES_LEFT;
          memcpy(pSTScreen,(char *)VideoRaster,SCREENBYTES_LEFT);
          VideoRaster += SCREENBYTES_LEFT;
        }
        else
          memset(pSTScreen,0,SCREENBYTES_LEFT);
        /* Copy middle - always present */
        memcpy(pSTScreen+SCREENBYTES_LEFT,(char *)VideoRaster,SCREENBYTES_MIDDLE);
        VideoRaster += SCREENBYTES_MIDDLE;
        /* Does have right border? If not, clear to colour '0' */
        if (BorderMask&BORDERMASK_RIGHT) {
          memcpy(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE,(char *)VideoRaster,SCREENBYTES_RIGHT);
          VideoRaster += 46-SCREENBYTES_RIGHT;
          VideoRaster += SCREENBYTES_RIGHT;
        }
        else
          memset(pSTScreen+SCREENBYTES_LEFT+SCREENBYTES_MIDDLE,0,SCREENBYTES_RIGHT);
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
  memcpy(pSTScreen,(char *)VideoRaster,((VDIWidth*VDIPlanes)/8)*VDIHeight);  /* 640x400x16colour */
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
  Memory_Clear(pHBLPaletteMasks,sizeof(unsigned long)*NUM_VISIBLE_LINES);
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
