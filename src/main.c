/*
  Hatari
*/

#include <time.h>
#include <signal.h>
#include <sys/time.h>

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
#include "debugui.h"

#include "uae-cpu/hatari-glue.h"


extern int quit_program;                  /* Declared in newcpu.c */

SDL_TimerID hSoundTimer;                  /* Handle to sound playback */

BOOL bQuitProgram=FALSE;                  /* Flag to quit program cleanly */
BOOL bUseFullscreen=FALSE;
BOOL bEmulationActive=EMULATION_ACTIVE;   /* Run emulation when started (we'll be in window mouse mode!) */
BOOL bAppActive = FALSE;
BOOL bEnableDebug=FALSE;                  /* Enable debug UI? */
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
  fprintf(stderr,"%s : %s\n",Title,Error);
}


/*-----------------------------------------------------------------------*/
/*
  Bring up message(handles full-screen as well as Window)
*/
int Main_Message(char *lpText, char *lpCaption/*,unsigned int uType*/)
{
  int Ret=0;

  /* Are we in full-screen? */
  if (bInFullScreen)
    Screen_ReturnFromFullScreen();

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
  SDL_PauseAudio(1);
  bEmulationActive = EMULATION_INACTIVE;
}

/*-----------------------------------------------------------------------*/
/*
  Start emulation
*/
void Main_UnPauseEmulation(void)
{
  SDL_PauseAudio(0);
  bFullScreenHold = FALSE;      /* Release hold  */
  Screen_SetFullUpdate();       /* Cause full screen update(to clear all) */

  bEmulationActive = EMULATION_ACTIVE;
  Audio_ResetBuffer();
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
       View_KeyUp( event.key.keysym.sym, event.key.keysym.mod );
       break;
   }
}



/*-----------------------------------------------------------------------*/
/*
  This thread runs at 50fps and passes sound samples to direct sound and also also
  set the counter/events to govern emulation speed to match the two together.
  When running at a speed other than standard ST speed the VBL event is set by 'Main_SpeedThreadFunc'
  which occurs at differing speeds.
*/
void /*Uint32*/ Main_SoundTimerFunc(int v/*Uint32 interval, void *param*/)
{
  struct itimerval mytimerval;
  /* Advance frame counter, used to draw screen to window at 50fps */
  VBLCounter++;
}


/*-----------------------------------------------------------------------*/
/*
  Create sound timer to handle sound
*/
void Main_CreateSoundTimer(void)
{
  struct itimerval mytimerval;
  /* Create thread to run every 20ms(50fps) to handle emulation samples */
  //hSoundTimer = SDL_AddTimer(10,Main_SoundTimerFunc,NULL);

  signal(SIGALRM, Main_SoundTimerFunc);
  mytimerval.it_interval.tv_sec=0;  mytimerval.it_interval.tv_usec=20000;
  mytimerval.it_value.tv_sec=0;     mytimerval.it_value.tv_usec=20000;
  setitimer(ITIMER_REAL, &mytimerval, NULL);
}

/*-----------------------------------------------------------------------*/
/*
  Delete sound timer
*/
void Main_RemoveSoundTimer(void)
{
  /*SDL_RemoveTimer(hSoundTimer);*/
  signal(SIGALRM,SIG_IGN);
}



/*-----------------------------------------------------------------------*/
/*
  Check for any passed parameters
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
              "  --help or -h                    Print this help text and exit.\n"
              "  --version or -v                 Print version number and exit.\n"
              "  --mono or -m                    Start in monochrome mode instead of color.\n"
              "  --fullscreen or -f              Try to use fullscreen mode.\n"
              "  --joystick or -j                Emulate a ST joystick with the cursor keys\n"
              "  --sound or -s                   Enable sound (does not yet work right!)\n"
              "  --frameskip                     Skip every second frame (speeds up emulation!)\n"
              "  --debug or -d                   Allow debug interface.\n"
              "  --harddrive <dir> or -e <dir>   Emulate an ST harddrive <dir> = root directory\n"
             );
       exit(0);
      }
      else if (!strcmp(argv[i],"--version") || !strcmp(argv[i],"-v"))
      {
       printf("This is %s.\n", PROG_NAME);
       printf("This program is free software licensed under the GNU GPL.\n");
       exit(0);
      }
      else if (!strcmp(argv[i],"--mono") || !strcmp(argv[i],"-m"))
      {
       bUseHighRes=TRUE;
       STRes=PrevSTRes=ST_HIGH_RES;
      }
      else if (!strcmp(argv[i],"--fullscreen") || !strcmp(argv[i],"-f"))
      {
       bUseFullscreen=TRUE;
      }
      else if (!strcmp(argv[i],"--joystick") || !strcmp(argv[i],"-j"))
      {
       ConfigureParams.Joysticks.Joy[1].bCursorEmulation=TRUE;
      }
      else if (!strcmp(argv[i],"--sound") || !strcmp(argv[i],"-s"))
      {
       bDisableSound=FALSE;
       ConfigureParams.Sound.bEnableSound = TRUE;
      }
      else if ( !strcmp(argv[i],"--frameskip") )
      {
       ConfigureParams.Screen.Advanced.bFrameSkip = TRUE;
      }
      else if (!strcmp(argv[i],"--debug") || !strcmp(argv[i],"-d"))
      {
       bEnableDebug=TRUE;
      }
      else if (!strcmp(argv[i],"--harddrive") || !strcmp(argv[i],"-e"))
      {
	if(i + 1 < argc){ /* both parameters exist */
	  /* only 1 emulated drive allowed, as of yet.  */
	  emudrives = malloc( sizeof(EMULATEDDRIVE *) );
	  emudrives[0] = malloc( sizeof(EMULATEDDRIVE) );
	  ConnectedDriveMask = 0x7; /* set the connected drive mask */
	  
	  /* set emulation directory string */
	  if( argv[i+1][0] != '.' && argv[i+1][0] != '/' )
	    sprintf( emudrives[0]->hd_emulation_dir, "./%s", argv[i+1]);
	  else
	    sprintf( emudrives[0]->hd_emulation_dir, "%s", argv[i+1]);
	  
	  fprintf(stderr, "Hard drive emulation, C: <-> %s\n", emudrives[0]->hd_emulation_dir);
	  i ++;
	  if(i + 1 >= argc) return; /* end of parameters? */
	}
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


/*-----------------------------------------------------------------------*/
/*
  Initialise emulation
*/
void Main_Init(void)
{
  /* SDL init: */
  if( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO/*|SDL_INIT_TIMER*/) < 0 )
   {
    fprintf(stderr, "Could not initialize the SDL library:\n %s\n", SDL_GetError() );
    exit(-1);
   }

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
  Audio_Init();
  Sound_Init();
  Main_CreateSoundTimer();

  /* Check passed disc image parameter, boot directly into emulator */
  if (strlen(szBootDiscImage)>0) {
    Floppy_InsertDiscIntoDrive(0,szBootDiscImage);
//FM    View_ToggleWindowsMouse(MOUSE_ST);
  }
}

/*-----------------------------------------------------------------------*/
/*
  Un-Initialise emulation
*/
void Main_UnInit(void)
{
  Screen_ReturnFromFullScreen();
  Main_RemoveSoundTimer();
  Floppy_EjectBothDrives();
  Floppy_UnInit();
  RS232_UnInit();
  Printer_UnInit();
  Intercept_UnInit();
  Audio_UnInit();
  YMFormat_FreeRecording();
//FM  View_LimitCursorToScreen();
  Screen_UnInit();

#ifdef USE_DEBUGGER
  FreeDebugDialog();
#endif
  Configuration_UnInit();

  /* SDL uninit: */
  SDL_Quit();
}

/*-----------------------------------------------------------------------*/
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

  /* Set default configuration values: */
  Configuration_SetDefault();

  /* Check for any passed parameters */
  Main_ReadParameters(argc, argv);

  /* Init emulator system */
  Main_Init();

  /* Switch immediately to fullscreen if user wants to */
  if( bUseFullscreen )
    Screen_EnterFullScreen();

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




