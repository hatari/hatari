/*
  Hatari
*/

#include <time.h>

#include <SDL.h>

#include "main.h"
#include "configuration.h"
#include "decode.h"
#include "dialog.h"
#include "createDiscImage.h"
#include "audio.h"
#include "debug.h"
#include "joy.h"
#include "errlog.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "ikbd.h"
#include "intercept.h"
#include "reset.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "printer.h"
#include "rs232.h"
#include "screen.h"
#include "shortcut.h"
#include "sound.h"
#include "timer.h"
#include "tos.h"
#include "video.h"
#include "view.h"
#include "ymFormat.h"

#include "uae-cpu/hatari-glue.h"


extern int quit_program;                  /* Declared in newcpu.c */


BOOL bQuitProgram=FALSE;                  /* Flag to quit program cleanly */
BOOL bUseFullscreen=FALSE;
BOOL bEmulationActive=EMULATION_ACTIVE;   /* Run emulation when started (we'll be in window mouse mode!) */
BOOL bAppActive = FALSE;
unsigned int TimerID;                     /* Timer ID for main window */
char szName[] = { "Hatari" };
char szBootDiscImage[MAX_FILENAME_LENGTH] = { "" };

char szWorkingDir[MAX_FILENAME_LENGTH] = { "" };
char szCurrentDir[MAX_FILENAME_LENGTH] = { "" };

unsigned char STRam[16*1024*1024];        /* This is our ST Ram, includes all TOS/hardware areas for ease */

int STSpeedMilliSeconds[] = {             /* Convert option 'nMinMaxSpeed' into milliseconds */
  1000/50,          /* MINMAXSPEED_MIN(20ms) */
  1000/66,          /* MINMAXSPEED_1(15ms) */
  1000/100,         /* MINMAXSPEED_2(10ms) */
  1000/200,         /* MINMAXSPEED_3(5ms) */
  1,                /* MINMAXSPEED_MAX(1ms) */
};




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
    nBytes = STRamEnd_BusErr;
    MemorySnapShot_Store(&nBytes,sizeof(nBytes));
    MemorySnapShot_Store(STRam,nBytes);
  }
  else {
    MemorySnapShot_Store(&nBytes,sizeof(nBytes));
    MemorySnapShot_Store(STRam,nBytes);
  }
  /* And Cart/TOS/Hardware area */
  MemorySnapShot_Store(&STRam[0xE00000],0x200000);
  MemorySnapShot_Store(szBootDiscImage,sizeof(szBootDiscImage));
  MemorySnapShot_Store(szWorkingDir,sizeof(szWorkingDir));
  MemorySnapShot_Store(szCurrentDir,sizeof(szCurrentDir));
}


/*-----------------------------------------------------------------------*/
/*
  Error handler
*/
void Main_SysError(char *Error,char *Title)
{
//FIXME   MessageBox(hWnd,Error,Title,MB_OK | MB_ICONSTOP);
}

/*-----------------------------------------------------------------------*/
/*
  Bring up message(handles full-screen as well as Window)
*/
int Main_Message(char *lpText, char *lpCaption/*,unsigned int uType*/)
{
  int Ret=0;

  /* Are we in full-screen? */
  /*if (bInFullScreen)
    Screen_ReturnFromFullScreen();*/

  /* Show message */
  fprintf(stderr,"Message (%s):\n %s\n", lpCaption, lpText);

  return(Ret);
}

/*-----------------------------------------------------------------------*/
/*
  Pause emulation, stop sound
*/
void Main_PauseEmulation(void)
{
  bEmulationActive = EMULATION_INACTIVE;
}

//-----------------------------------------------------------------------
/*
  Start emulation
*/
void Main_UnPauseEmulation(void)
{
//  SetMenu(hWnd,NULL);         // Remove any menu's!
  bFullScreenHold = FALSE;      // Release hold  
  Screen_SetFullUpdate();       // Cause full screen update(to clear all)

  bEmulationActive = EMULATION_ACTIVE;
  DAudio_ResetBuffer();
}

/* ----------------------------------------------------------------------- */
/*
  Message handler  ( actually called from Video_InterruptHandler_VBL() )
  Here we process the SDL events (keyboard, mouse, ...) and map it to
  Atari IKBD events.
*/
#ifndef SDL_BUTTON_LEFT       /* Seems not to be defined in old SDL versions */
#define SDL_BUTTON_LEFT    1
#define SDL_BUTTON_MIDDLE  2
#define SDL_BUTTON_RIGHT  3
#endif
void Main_EventHandler()
{
 SDL_Event event;

 if( SDL_PollEvent(&event) )
  switch( event.type )
   {
    case SDL_QUIT:
       quit_program=1;
       bQuitProgram=1;
       break;
    case SDL_MOUSEMOTION:
       View_UpdateSTMousePosition();    /* Read/Update internal mouse position */
       break;
    case SDL_MOUSEBUTTONDOWN:
       if( event.button.button==SDL_BUTTON_LEFT )
         View_LeftMouseButtonDown();
       else if( event.button.button==SDL_BUTTON_RIGHT )
         View_RightMouseButtonDown();
       else if( event.button.button==SDL_BUTTON_MIDDLE )
         Keyboard.LButtonDblClk = 1;    /* Start double-click sequence in emulation time */
       break;
    case SDL_MOUSEBUTTONUP:
       if( event.button.button==SDL_BUTTON_LEFT )
         View_LeftMouseButtonUp();
       else if( event.button.button==SDL_BUTTON_RIGHT )
         View_RightMouseButtonUp();
       break;
     case SDL_KEYDOWN:
       View_KeyDown( event.key.keysym.sym, event.key.keysym.mod );
       break;
     case SDL_KEYUP:
       if(event.key.keysym.sym == SDLK_F12) {
        quit_program=1;
        bQuitProgram=1;
       } else {
        View_KeyUp( event.key.keysym.sym, event.key.keysym.mod );
       }
       break;
   }
}


/*-----------------------------------------------------------------------*/
/*
  Create an event which is times our VBL(50fps) to govern the speed of the emulator
  This changes to vary emulation speed according to user settings
*/
/* FIXME */
/*
void Main_CreateVBLEvent(void)
{
  hVBLHandle = CreateEvent(NULL,  // pointer to security attributes
        FALSE,  // flag for manual-reset event
        FALSE,  // flag for initial state
        NULL);  // pointer to event-object name
}
*/

//-----------------------------------------------------------------------
/*
  Delete VBL timer
*/
/* FIXME */
/*
void Main_DeleteVBLEvent(void)
{
  if (hVBLHandle) {
    CloseHandle(hVBLHandle);
    hVBLHandle = NULL;
  }
}
*/

//-----------------------------------------------------------------------
/*
  Signal VBL timer event - used in 'Main_WaitVBLEvent'
*/
/* FIXME */
/*
void Main_SetVBLEvent(void)
{
  if (hVBLHandle)
    SetEvent(hVBLHandle);
}
*/

//-----------------------------------------------------------------------
/*
  Wait for VBL counter to latch to next frame(called by sound callback @ 20ms, 50fps)
*/
/* FIXME */
/*
void Main_WaitVBLEvent(void)
{
  // Wait until event signalled by Sound VBL, this need to be more Windows friendly
  if (hVBLHandle) {
    if (WaitForSingleObject(hVBLHandle,50)==WAIT_TIMEOUT) {    // Suspend thread until VBL count increases
      // Something went wrong, reset, try again
      Main_SetSpeedThreadTimer(ConfigureParams.Configure.nMinMaxSpeed);
    }
  }
}
*/

//-----------------------------------------------------------------------
/*
  Check VBL event to see if already set, and return TRUE
*/
/* FIXME */
/*
BOOL Main_AlreadyWaitingVBLEvent(void)
{
  // Test event to see if already set
  if (WaitForSingleObject(hVBLHandle,0)==WAIT_TIMEOUT)
    return(FALSE);

  return(TRUE);
}
*/

//-----------------------------------------------------------------------
/*
  Create sound thread to handle passing of sound on to DirectSound
*/
/* FIXME */
/*
void Main_CreateSoundThread(void)
{
  // Create thread to run every 20ms(50fps) to send emulation samples to DirectSound
  hSoundTimer = timeSetEvent(20,1,Main_SoundThreadFunc,0,TIME_PERIODIC);
}
*/

//-----------------------------------------------------------------------
/*
  This thread runs at 50fps and passes sound samples to direct sound and also also
  set the counter/events to govern emulation speed to match the two together.
  When running at a speed other than standard ST speed the VBL event is set by 'Main_SpeedThreadFunc'
  which occurs at differing speeds.
*/
/* FIXME */
/*
void CALLBACK Main_SoundThreadFunc( UINT wTimerID, UINT msg, DWORD dwUsers, DWORD dw1, DWORD dw2 )
{
  // Advance frame counter, used to draw screen to window at 50fps
  VBLCounter++;

  // Set event so waiting screen draw routine will continue
  if (ConfigureParams.Configure.nMinMaxSpeed==MINMAXSPEED_MIN) {
    // Do wish to skip frames?
    if (ConfigureParams.Screen.Advanced.bFrameSkip) {
      if (VBLCounter&1)
        Main_SetVBLEvent();          // 25fps, with frame-skip
    }
    else
      Main_SetVBLEvent();            // 50fps
  }

  // And play sound through DirectSound, if enabled
  if ( (ConfigureParams.Sound.bEnableSound) && bAppActive )
    Sound_PassYMSamplesToDirectSound();
}
*/

//-----------------------------------------------------------------------
/*
  When running in non-standard ST speed this sets the VBL event to increase the
  running speed of the emulator
*/
/* FIXME */
/*
void CALLBACK Main_SpeedThreadFunc( UINT wTimerID, UINT msg, DWORD dwUsers, DWORD dw1, DWORD dw2 )
{
  // Advance screen update counter - ONLY if in max speed mode
  if (ConfigureParams.Configure.nMinMaxSpeed!=MINMAXSPEED_MIN)
    Main_SetVBLEvent();
}
*/

//-----------------------------------------------------------------------
/*
  Set timer for 'Main_SpeedThreadFunc' to govern fast than ST emulation speed
  When running in min/max speed this thread is ignored
*/
/* FIXME */
/*
void Main_SetSpeedThreadTimer(int nMinMaxSpeed)
{
  // Do we have an old timer? If so kill it
  if (hSpeedTimer) {
    timeKillEvent(hSpeedTimer);
    hSpeedTimer = NULL;
  }

  // Set new timer, according to MINMAXSPEED_xxxx
  if ( ( (nMinMaxSpeed!=MINMAXSPEED_MIN) && nMinMaxSpeed!=MINMAXSPEED_MAX) && (!bQuitProgram) )
    hSpeedTimer = timeSetEvent(STSpeedMilliSeconds[nMinMaxSpeed],1,Main_SpeedThreadFunc,0,TIME_PERIODIC);
}
*/

/*-----------------------------------------------------------------------*/
/*
  Check for any passed parameters
  Used to disable DirectDraw, DirectSound and DirectInput for machines with problems
*/
void Main_ReadParameters(int argc, char *argv[])
{
 int i;

 /* Scan for any which we can use */
 for(i=1; i<argc; i++)
  {
   if (strlen(argv[i])>0)
    {
     if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h"))
      {
       printf("Usage:\n hatari [options] [disk image name]\n"
              "Where options are:\n"
              "  --help or -h        Print this help text and exit.\n"
              "  --version or -v     Print version number and exit.\n"
              "  --color or -c       Start in color mode instead of mono.\n"
              "  --fullscreen or -f  Try to use fullscreen mode.\n"
              "  --joystick or -j    Emulate a ST joystick with the cursor keys\n"
             );
       exit(0);
      }
      else if (!strcmp(argv[i],"--version") || !strcmp(argv[i],"-v"))
      {
       printf("This is %s.\n", PROG_NAME);
       printf("This program is free software licensed under the GNU GPL.\n");
       exit(0);
      }
      else if (!strcmp(argv[i],"--color") || !strcmp(argv[i],"-c"))
      {
       bUseHighRes=FALSE;
      }
      else if (!strcmp(argv[i],"--fullscreen") || !strcmp(argv[i],"-f"))
      {
       bUseFullscreen=TRUE;
      }
      else if (!strcmp(argv[i],"--joystick") || !strcmp(argv[i],"-j"))
      {
       fprintf(stderr,"Joystate: %i\n",(int)ConfigureParams.Joysticks.Joy[1].bCursorEmulation);
       ConfigureParams.Joysticks.Joy[1].bCursorEmulation=TRUE;
      }
      else
      {
       /* Possible passed disc image filename, ie starts with character other than '-' */
       if (argv[i][0]!='-')
         strcpy(szBootDiscImage,argv[i]);
      }
    }
  }
}

//-----------------------------------------------------------------------
/*
  Initialise emulation
*/
void Main_Init(void)
{
  Misc_SeedRandom(1043618);
  Printer_Init();
  RS232_Init();
  Configuration_Init();
  Timer_Init();
  File_Init();
  Screen_Init();
  Floppy_Init();
  Reset_Cold();
  GemDOS_Init();
  Intercept_Init();
  Joy_Init();
  DAudio_Init();
  Sound_Init();
//FM  Main_CreateVBLEvent();
//FM  Main_CreateSoundThread();

  // Check passed disc image parameter, boot directly into emulator
  if (strlen(szBootDiscImage)>0) {
    Floppy_InsertDiscIntoDrive(0,szBootDiscImage);
//FM    View_ToggleWindowsMouse(MOUSE_ST);
  }
}

//-----------------------------------------------------------------------
/*
  Un-Initialise emulation
*/
void Main_UnInit(void)
{
  Screen_ReturnFromFullScreen();
//FM  Main_SetSpeedThreadTimer(-1);
//FM  Main_DeleteVBLEvent();
//FM  timeKillEvent(hSoundTimer);
  Floppy_EjectBothDrives();
  Floppy_UnInit();
  RS232_UnInit();
  Printer_UnInit();
//  DJoy_UnInit();
  Intercept_UnInit();
  DAudio_UnInit();
//  DSurface_UnInit();
  YMFormat_FreeRecording();
//FM  View_LimitCursorToScreen();
  Screen_UnInit();

#ifdef USE_DEBUGGER
  FreeDebugDialog();
#endif
  Configuration_UnInit();
}

//-----------------------------------------------------------------------
/*
  Main
*/
int main(int argc, char *argv[])
{

  /* Generate random seed */
  srand( time(NULL) );

  /* Get working directory, if in MSDev force */
  Misc_FindWorkingDirectory();
#ifdef FORCE_WORKING_DIR
  getcwd(szWorkingDir, MAX_FILENAME_LENGTH);
#endif

  /* Create debug files */
  Debug_OpenFiles();
  ErrLog_OpenFile();

  /* Check for any passed parameters */
   Main_ReadParameters(argc, argv);

  /* Init emulator system */
  Main_Init();

  /* Set timing threads to govern timing and debug display */
//FM  Main_SetSpeedThreadTimer(ConfigureParams.Configure.nMinMaxSpeed);
//FM  TimerID = SetTimer(hWnd,1,1000,NULL);

#ifdef USE_DEBUGGER
  /* Run our debugger */
  Debugger_Init();
  Main_UnPauseEmulation();
  /* Run messages until quit */
  for(;;) {
    if (Main_ExecuteWindowsMessage())
      break;
  }
#else
  /* Run release emulation */
  Main_UnPauseEmulation();
  //RunIntructions();
  Init680x0();         /* Init CPU emulation */
  Start680x0();        /* Start emulation */
#endif

  /* Un-init emulation system */
//FM  KillTimer(hWnd,TimerID);
  Main_UnInit();  

  /* Close debug files */
  ErrLog_CloseFile();
  Debug_CloseFiles();

  return(0);
}
