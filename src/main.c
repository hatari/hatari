/*
  Hatari
*/

#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

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
#include "hdc.h"
#include "ikbd.h"
#include "intercept.h"
#include "reset.h"
#include "keymap.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "printer.h"
#include "rs232.h"
#include "screen.h"
#include "sdlgui.h"
#include "shortcut.h"
#include "sound.h"
#include "timer.h"
#include "tos.h"
#include "video.h"
#include "ymFormat.h"
#include "debugui.h"

#include "uae-cpu/hatari-glue.h"


#define FORCE_WORKING_DIR                 /* Set default directory to cwd */


SDL_TimerID hSoundTimer;                  /* Handle to sound playback */

BOOL bQuitProgram=FALSE;                  /* Flag to quit program cleanly */
BOOL bUseFullscreen=FALSE;
BOOL bEmulationActive=TRUE;               /* Run emulation when started */
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

  /* Show message */
  fprintf(stderr,"Message from %s: %s\n", lpCaption, lpText);

  return(Ret);
}

/*-----------------------------------------------------------------------*/
/*
  Pause emulation, stop sound
*/
void Main_PauseEmulation(void)
{
  if( bEmulationActive )
  {
    SDL_PauseAudio(TRUE);
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
    SDL_PauseAudio(FALSE);
    bFullScreenHold = FALSE;      /* Release hold  */
    Screen_SetFullUpdate();       /* Cause full screen update(to clear all) */

    bEmulationActive = TRUE;
    Audio_ResetBuffer();
  }
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
       bQuitProgram = TRUE;
       break;
    case SDL_MOUSEMOTION:               /* Read/Update internal mouse position */
       KeyboardProcessor.Mouse.dx += event.motion.xrel;
       KeyboardProcessor.Mouse.dy += event.motion.yrel;
       break;
    case SDL_MOUSEBUTTONDOWN:
       if( event.button.button==SDL_BUTTON_LEFT )
       {
         if(Keyboard.LButtonDblClk==0)
           Keyboard.bLButtonDown |= BUTTON_MOUSE;  /* Set button down flag */
       }
       else if( event.button.button==SDL_BUTTON_RIGHT )
         Keyboard.bRButtonDown |= BUTTON_MOUSE;
       else if( event.button.button==SDL_BUTTON_MIDDLE )
         Keyboard.LButtonDblClk = 1;    /* Start double-click sequence in emulation time */
       break;
    case SDL_MOUSEBUTTONUP:
       if( event.button.button==SDL_BUTTON_LEFT )
         Keyboard.bLButtonDown &= ~BUTTON_MOUSE;
       else if( event.button.button==SDL_BUTTON_RIGHT )
         Keyboard.bRButtonDown &= ~BUTTON_MOUSE;;
       break;
     case SDL_KEYDOWN:
       Keymap_KeyDown( event.key.keysym.sym, event.key.keysym.mod );
       break;
     case SDL_KEYUP:
       Keymap_KeyUp( event.key.keysym.sym, event.key.keysym.mod );
       break;
   }
}



/*-----------------------------------------------------------------------*/
/*
  This thread runs at 50fps and passes sound samples to the sound interface and also
  set the counter/events to govern emulation speed to match the two together.
*/
Uint32 Main_SoundTimerFunc(Uint32 interval, void *param)
{
  /* Advance frame counter, used to draw screen to window at 50fps */
  VBLCounter++;
  return(interval);
}


/*-----------------------------------------------------------------------*/
/*
  Create sound timer to handle sound
*/
void Main_CreateSoundTimer(void)
{
  /* Create thread to run every 20ms (50fps) to handle emulation samples */
  hSoundTimer = SDL_AddTimer(20, Main_SoundTimerFunc, NULL);
}

/*-----------------------------------------------------------------------*/
/*
  Delete sound timer
*/
void Main_RemoveSoundTimer(void)
{
  SDL_RemoveTimer(hSoundTimer);
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
               "  --help or -h          Print this help text and exit.\n"
               "  --version or -v       Print version number and exit.\n"
               "  --mono or -m          Start in monochrome mode instead of color.\n"
               "  --fullscreen or -f    Try to use fullscreen mode.\n"
               "  --joystick or -j      Emulate a ST joystick with the cursor keys.\n"
               "  --nosound             Disable sound (faster!).\n"
               "  --frameskip           Skip every second frame (speeds up emulation!).\n"
               "  --debug or -D         Allow debug interface.\n"
               "  --harddrive <dir>     Emulate an ST harddrive\n"
               "     or -d <dir>         (<dir> = root directory).\n"
               "  --hdimage <imagename> Emulate an ST harddrive with an image.\n"
               "  --tos <file>          Use TOS image <file>.\n"
               "  --cpulevel x          Set the CPU type (x => 680x0) (TOS 2.06 only!).\n"
               "  --compatible          Use a more compatible (but slower) 68000 CPU mode.\n"
               "  --blitter             Enable blitter emulation (unstable!)\n"
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
        ConfigureParams.Screen.bUseHighRes=TRUE;
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
      else if ( !strcmp(argv[i],"--nosound") )
      {
        bDisableSound=TRUE;
        ConfigureParams.Sound.bEnableSound = FALSE;
      }
      else if ( !strcmp(argv[i],"--frameskip") )
      {
        ConfigureParams.Screen.Advanced.bFrameSkip = TRUE;
      }
      else if (!strcmp(argv[i],"--debug") || !strcmp(argv[i],"-D"))
      {
        bEnableDebug=TRUE;
      }
      else if (!strcmp(argv[i],"--hdimage"))
      {
	if( argc>i+1 && strlen(argv[i+1])<=MAX_FILENAME_LENGTH )
        {
          if( HDC_Init(argv[i+1]) == TRUE)
          {
            strcpy(ConfigureParams.HardDisc.szHardDiscImage, argv[i+1]);
            printf("Hard drive image %s mounted.\n", argv[i+1]);
            i++;
          }
          else
          {
            printf("Couldn't open file: %s, or no partitions\n", argv[i+1]);
          }
        }
      }
      else if (!strcmp(argv[i],"--harddrive") || !strcmp(argv[i],"-d"))
      {
        if( i+1<argc && strlen(argv[i+1])<=MAX_PATH )  /* both parameters exist */
        {
          strcpy(ConfigureParams.HardDisc.szHardDiscDirectories[0], argv[i+1]);
          GemDOS_InitDrives();
          i += 1;
        }
      }
      else if (!strcmp(argv[i],"--tos"))
      {
        if(i+1>=argc)
          fprintf(stderr,"Missing argument for --tos.\n");
        else
          strncpy(ConfigureParams.TOSGEM.szTOSImageFileName, argv[++i], MAX_FILENAME_LENGTH);
      }
      else if (!strcmp(argv[i],"--cpulevel"))
      {
        if(i+1>=argc)
          fprintf(stderr,"Missing argument for --cpulevel.\n");
         else
          cpu_level = atoi(argv[++i]);
        if(cpu_level<0 || cpu_level>4)
          cpu_level = 0;
        ConfigureParams.Cpu.level = cpu_level;
      }
      else if (!strcmp(argv[i],"--compatible") || !strcmp(argv[i],"-d"))
      {
        cpu_compatible = TRUE;
        ConfigureParams.Cpu.compatible = TRUE;
      }
      else if (!strcmp(argv[i],"--blitter"))
      {
        bEnableBlitter = TRUE;
      }
      else
      {
        /* Possible passed disc image filename, ie starts with character other than '-' */
        if (argv[i][0]!='-')
          strcpy(szBootDiscImage,argv[i]);
        else
          fprintf(stderr,"Illegal parameter: %s\n",argv[i]);
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
  /* Init SDL's video and timer subsystem. Note: Audio and joystick subsystems
     will be initialized later (failures there are not fatal). */
  if( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) < 0 )
  {
    fprintf(stderr, "Could not initialize the SDL library:\n %s\n", SDL_GetError() );
    exit(-1);
  }

  Misc_SeedRandom(1043618);
  Configuration_Init();
  SDLGui_Init();
  Printer_Init();
  RS232_Init();
  Timer_Init();
  Screen_Init();
  Floppy_Init();
  Init680x0();         /* Init CPU emulation */
  Reset_Cold();        /* Reset all systems */
  GemDOS_Init();
  Intercept_Init();
  Joy_Init();
  Audio_Init();
  Sound_Init();
  Main_CreateSoundTimer();

  /* Check passed disc image parameter, boot directly into emulator */
  if (strlen(szBootDiscImage)>0) {
    Floppy_InsertDiscIntoDrive(0,szBootDiscImage);
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
  HDC_UnInit();
  RS232_UnInit();
  Printer_UnInit();
  Intercept_UnInit();
  GemDOS_UnInitDrives();
  Audio_UnInit();
  YMFormat_FreeRecording();
  SDLGui_UnInit();
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
  Misc_FindWorkingDirectory(argv[0]);
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

  /* Run emulation */
  Main_UnPauseEmulation();
  Start680x0();                 /* Start emulation */

  /* Un-init emulation system */
//FM  KillTimer(hWnd,TimerID);
  Main_UnInit();  

  /* Close debug files */
  ErrLog_CloseFile();
  Debug_CloseFiles();

  return(0);
}




