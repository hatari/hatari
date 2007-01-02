/*
  Hatari - main.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Main initialization and event handling routines.
*/
const char Opt_rcsid[] = "Hatari $Id: main.c,v 1.97 2007-01-02 22:20:43 thothy Exp $";

#include <time.h>
#include <unistd.h>

#include <SDL.h>

#include "main.h"
#include "configuration.h"
#include "options.h"
#include "dialog.h"
#include "audio.h"
#include "joy.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "ikbd.h"
#include "ioMem.h"
#include "keymap.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "midi.h"
#include "nvram.h"
#include "printer.h"
#include "reset.h"
#include "rs232.h"
#include "screen.h"
#include "sdlgui.h"
#include "shortcut.h"
#include "sound.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"
#include "video.h"
#include "ymFormat.h"
#include "debugui.h"

#include "uae-cpu/hatari-glue.h"

#if ENABLE_FALCON
#include "falcon/hostscreen.h"
#include "falcon/dsp.h"
#endif


BOOL bQuitProgram = FALSE;                /* Flag to quit program cleanly */
BOOL bEnableDebug = FALSE;                /* Enable debug UI? */
char szWorkingDir[FILENAME_MAX];          /* Working directory */
static BOOL bEmulationActive = TRUE;      /* Run emulation when started */
static BOOL bAccurateDelays;              /* Host system has an accurate SDL_Delay()? */
static char szBootDiskImage[FILENAME_MAX];   /* boot disk path or empty */
static BOOL bIgnoreNextMouseMotion = FALSE;  /* Next mouse motion will be ignored (needed after SDL_WarpMouse) */


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void Main_MemorySnapShot_Capture(BOOL bSave)
{
  int nBytes;

  /* Save/Restore details */
  /* Only save/restore area of memory machine ie set to, eg 1Mb */
  if (bSave) {
    nBytes = STRamEnd;
    MemorySnapShot_Store(&nBytes,sizeof(nBytes));
    MemorySnapShot_Store(STRam,nBytes);
  }
  else {
    MemorySnapShot_Store(&nBytes,sizeof(nBytes));
    MemorySnapShot_Store(STRam,nBytes);
  }
  /* And Cart/TOS/Hardware area */
  MemorySnapShot_Store(&STRam[0xE00000],0x200000);
  MemorySnapShot_Store(szBootDiskImage, sizeof(szBootDiskImage));
  MemorySnapShot_Store(szWorkingDir,sizeof(szWorkingDir));
}


/*-----------------------------------------------------------------------*/
/*
  Pause emulation, stop sound
*/
void Main_PauseEmulation(void)
{
  if( bEmulationActive )
  {
    Audio_EnableAudio(FALSE);
    bEmulationActive = FALSE;
  }
}

/*-----------------------------------------------------------------------*/
/*
  Start emulation
*/
void Main_UnPauseEmulation(void)
{
  if( !bEmulationActive )
  {
    Sound_ResetBufferIndex();
    Audio_EnableAudio(ConfigureParams.Sound.bEnableSound);
    Screen_SetFullUpdate();       /* Cause full screen update (to clear all) */

    bEmulationActive = TRUE;
  }
}


/*-----------------------------------------------------------------------*/
/*
  This function waits on each emulated VBL to synchronize the real time
  with the emulated ST.
  Unfortunately SDL_Delay and other sleep functions like usleep or nanosleep
  are very inaccurate on some systems like Linux 2.4 or Mac OS X (they can only
  wait for a multiple of 10ms due to the scheduler on these systems), so we have
  to "busy wait" there to get an accurate timing.
*/
void Main_WaitOnVbl(void)
{
  int nCurrentMilliTicks;
  static int nDestMilliTicks = 0;
  int nFrameDuration;
  signed int nDelay;

  nCurrentMilliTicks = SDL_GetTicks();

  nFrameDuration = 1000/nScreenRefreshRate;
  nDelay = nDestMilliTicks - nCurrentMilliTicks;

  /* Do not wait if we are in max speed mode or if we are totally out of sync */
  if (ConfigureParams.System.nMinMaxSpeed == MINMAXSPEED_MAX
      || nDelay < -4*nFrameDuration)
  {
	/* Only update nDestMilliTicks for next VBL */
	nDestMilliTicks = nCurrentMilliTicks + nFrameDuration;
    return;
  }

  if (bAccurateDelays)
  {
    /* Accurate sleeping is possible -> use SDL_Delay to free the CPU */
    if (nDelay > 1)
      SDL_Delay(nDelay - 1);
  }
  else
  {
    /* No accurate SDL_Delay -> only wait if more than 5ms to go... */
    if (nDelay > 5)
      SDL_Delay(nDelay<10 ? nDelay-1 : 9);
  }

  /* Now busy-wait for the right tick: */
  while (nDelay > 0)
  {
    nCurrentMilliTicks = SDL_GetTicks();
    nDelay = nDestMilliTicks - nCurrentMilliTicks;
  }

  /* Update nDestMilliTicks for next VBL */
  nDestMilliTicks += nFrameDuration;
}


/*-----------------------------------------------------------------------*/
/*
  Since SDL_Delay and friends are very inaccurate on some systems, we have
  to check if we can rely on this delay function.
*/
static void Main_CheckForAccurateDelays(void)
{
  int nStartTicks, nEndTicks;

  /* Force a task switch now, so we have a longer timeslice afterwards */
  SDL_Delay(10);

  nStartTicks = SDL_GetTicks();
  SDL_Delay(1);
  nEndTicks = SDL_GetTicks();

  /* If the delay took longer than 10ms, we are on an inaccurate system! */
  bAccurateDelays = ((nEndTicks - nStartTicks) < 9);

  if (bAccurateDelays)
    Log_Printf(LOG_DEBUG, "Host system has accurate delays. (%d)\n", nEndTicks - nStartTicks);
  else
    Log_Printf(LOG_DEBUG, "Host system does not have accurate delays. (%d)\n", nEndTicks - nStartTicks);
}


/* ----------------------------------------------------------------------- */
/*
  Set mouse pointer to new coordinates and set flag to ignore the mouse event
  that is generated by SDL_WarpMouse().
*/
void Main_WarpMouse(int x, int y)
{
  SDL_WarpMouse(x, y);                  /* Set mouse pointer to new position */
  bIgnoreNextMouseMotion = TRUE;        /* Ignore mouse motion event from SDL_WarpMouse */
}


/* ----------------------------------------------------------------------- */
/*
  Handle mouse motion event.
*/
static void Main_HandleMouseMotion(SDL_Event *pEvent)
{
	int dx, dy;
	static int ax = 0, ay = 0;

	
	if (bIgnoreNextMouseMotion)
	{
		bIgnoreNextMouseMotion = FALSE;
		return;
	}

	dx = pEvent->motion.xrel;
	dy = pEvent->motion.yrel;

	/* In zoomed low res mode, we divide dx and dy by the zoom factor so that
	 * the ST mouse cursor stays in sync with the host mouse. However, we have
	 * to take care of lowest bit of dx and dy which will get lost when
	 * dividing. So we store these bits in ax and ay and add them to dx and dy
	 * the next time. */
	if (nScreenZoomX != 1)
	{
		dx += ax;
		ax = dx % nScreenZoomX;
		dx /= nScreenZoomX;
	}
	if (nScreenZoomY != 1)
	{
		dy += ay;
		ay = dy % nScreenZoomY;
		dy /= nScreenZoomY;
	}

	KeyboardProcessor.Mouse.dx += dx;
	KeyboardProcessor.Mouse.dy += dy;
}


/* ----------------------------------------------------------------------- */
/*
  Message handler
  Here we process the SDL events (keyboard, mouse, ...) and map it to
  Atari IKBD events.
*/
void Main_EventHandler(void)
{
  SDL_Event event;

  if( SDL_PollEvent(&event) )
   switch( event.type )
   {

    case SDL_QUIT:
       bQuitProgram = TRUE;
       set_special(SPCFLAG_BRK);        /* Assure that CPU core shuts down */
       break;

    case SDL_MOUSEMOTION:               /* Read/Update internal mouse position */
       Main_HandleMouseMotion(&event);
       break;

    case SDL_MOUSEBUTTONDOWN:
       if (event.button.button == SDL_BUTTON_LEFT)
       {
         if (Keyboard.LButtonDblClk == 0)
           Keyboard.bLButtonDown |= BUTTON_MOUSE;  /* Set button down flag */
       }
       else if (event.button.button == SDL_BUTTON_RIGHT)
       {
         Keyboard.bRButtonDown |= BUTTON_MOUSE;
       }
       else if (event.button.button == SDL_BUTTON_MIDDLE)
       {
         /* Start double-click sequence in emulation time */
         Keyboard.LButtonDblClk = 1;
       }
       else if (event.button.button == SDL_BUTTON_WHEELDOWN)
       {
         /* Simulate pressing the "cursor down" key */
         IKBD_PressSTKey(0x50, TRUE);
       }
       else if (event.button.button == SDL_BUTTON_WHEELUP)
       {
         /* Simulate pressing the "cursor up" key */
         IKBD_PressSTKey(0x48, TRUE);
       }
       break;

    case SDL_MOUSEBUTTONUP:
       if (event.button.button == SDL_BUTTON_LEFT)
       {
         Keyboard.bLButtonDown &= ~BUTTON_MOUSE;
       }
       else if (event.button.button == SDL_BUTTON_RIGHT)
       {
         Keyboard.bRButtonDown &= ~BUTTON_MOUSE;
       }
       else if (event.button.button == SDL_BUTTON_WHEELDOWN)
       {
         /* Simulate releasing the "cursor down" key */
         IKBD_PressSTKey(0x50, FALSE);
       }
       else if (event.button.button == SDL_BUTTON_WHEELUP)
       {
         /* Simulate releasing the "cursor up" key */
         IKBD_PressSTKey(0x48, FALSE);
       }
       break;

    case SDL_KEYDOWN:
       Keymap_KeyDown(&event.key.keysym);
       break;

    case SDL_KEYUP:
       Keymap_KeyUp(&event.key.keysym);
       break;
   }
}


/*-----------------------------------------------------------------------*/
/*
  Initialise emulation
*/
static void Main_Init(void)
{
  /* Open debug log file */
  Log_Init();
  Log_Printf(LOG_INFO, PROG_NAME ", compiled on:  " __DATE__ ", " __TIME__ "\n");

  /* Init SDL's video subsystem. Note: Audio and joystick subsystems
     will be initialized later (failures there are not fatal). */
  if(SDL_Init(SDL_INIT_VIDEO) < 0)
  {
    fprintf(stderr, "Could not initialize the SDL library:\n %s\n", SDL_GetError() );
    exit(-1);
  }

  SDLGui_Init();
  Printer_Init();
  RS232_Init();
  Midi_Init();
  Screen_Init();
#if ENABLE_FALCON
  HostScreen_Init();
  if (ConfigureParams.System.bDSP) {
    DSP_Init();
  }
#endif
  Floppy_Init();
  Init680x0();                  /* Init CPU emulation */
  Audio_Init();
  Keymap_Init();

  /* Init HD emulation */
  if (ConfigureParams.HardDisk.bUseHardDiskImage)
  {
    char *szHardDiskImage = ConfigureParams.HardDisk.szHardDiskImage;
    if (HDC_Init(szHardDiskImage))
      printf("Hard drive image %s mounted.\n", szHardDiskImage);
    else
      printf("Couldn't open HD file: %s, or no partitions\n", szHardDiskImage);
  }
  GemDOS_Init();
  if(ConfigureParams.HardDisk.bUseHardDiskDirectories)
  {
    GemDOS_InitDrives();
  }

  if(Reset_Cold())              /* Reset all systems, load TOS image */
  {
    /* If loading of the TOS failed, we bring up the GUI to let the
     * user choose another TOS ROM file. */
    Dialog_DoProperty();
  }
  if(!bTosImageLoaded || bQuitProgram)
  {
    fprintf(stderr, "Failed to load TOS image!\n");
    SDL_Quit();
    exit(-2);
  }

  IoMem_Init();
  NvRam_Init();
  Joy_Init();
  Sound_Init();

  /* Check passed disk image parameter, boot directly into emulator */
  if (strlen(szBootDiskImage) > 0)
  {
    Floppy_InsertDiskIntoDrive(0, szBootDiskImage);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Un-Initialise emulation
*/
static void Main_UnInit(void)
{
  Screen_ReturnFromFullScreen();
  Floppy_UnInit();
  HDC_UnInit();
  Midi_UnInit();
  RS232_UnInit();
  Printer_UnInit();
  IoMem_UnInit();
  NvRam_UnInit();
  GemDOS_UnInitDrives();
  Joy_UnInit();
  if(Sound_AreWeRecording())
    Sound_EndRecording();
  Audio_UnInit();
  YMFormat_FreeRecording();
  SDLGui_UnInit();
#if ENABLE_FALCON
  if (ConfigureParams.System.bDSP) {
    DSP_UnInit();
  }
  HostScreen_UnInit();
#endif
  Screen_UnInit();
  Exit680x0();

  /* SDL uninit: */
  SDL_Quit();

  /* Close debug log file */
  Log_UnInit();
}


/*-----------------------------------------------------------------------*/
/*
  Main
*/
int main(int argc, char *argv[])
{
  /* Generate random seed */
  srand(time(NULL));

  /* Get working directory */
  getcwd(szWorkingDir, FILENAME_MAX);

  /* no boot disk image */
  szBootDiskImage[0] = 0;

  /* Set default configuration values: */
  Configuration_SetDefault();

  /* Now load the values from the configuration file */
  Configuration_Load(CONFDIR"/hatari.cfg");     /* Try the global configuration file first */
  Configuration_Load(NULL);                     /* Now try the users configuration file */

  /* Check for any passed parameters, get boot disk */
  Opt_ParseParameters(argc, argv, szBootDiskImage, sizeof(szBootDiskImage));
  /* monitor type option might require "reset" -> TRUE */
  Configuration_Apply(TRUE);

#ifdef WIN32
  Win_OpenCon();
#endif

  /* Needed on N770 but useful also with other X11 window managers
   * for window grouping when you have multiple SDL windows open
   */
#ifndef WIN32
  setenv("SDL_VIDEO_X11_WMCLASS", "hatari", 1);
#endif

  /* Init emulator system */
  Main_Init();

  /* Check if SDL_Delay is accurate */
  Main_CheckForAccurateDelays();

  /* Switch immediately to fullscreen if user wants to */
  if (ConfigureParams.Screen.bFullScreen)
    Screen_EnterFullScreen();

  /* Run emulation */
  Main_UnPauseEmulation();
  Start680x0();                 /* Start emulation */

  /* Un-init emulation system */
  Main_UnInit();

  return 0;
}
