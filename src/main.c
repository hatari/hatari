/*
  Hatari - main.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Main initialization and event handling routines.
*/
static char rcsid[] = "Hatari $Id: main.c,v 1.45 2003-08-15 16:09:50 thothy Exp $";

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
#include "midi.h"
#include "printer.h"
#include "rs232.h"
#include "screen.h"
#include "sdlgui.h"
#include "shortcut.h"
#include "sound.h"
#include "tos.h"
#include "vdi.h"
#include "video.h"
#include "ymFormat.h"
#include "debugui.h"

#include "uae-cpu/hatari-glue.h"


#define FORCE_WORKING_DIR                 /* Set default directory to cwd */


BOOL bQuitProgram=FALSE;                  /* Flag to quit program cleanly */
BOOL bUseFullscreen=FALSE;
BOOL bEmulationActive=TRUE;               /* Run emulation when started */
BOOL bAppActive = FALSE;
BOOL bEnableDebug=FALSE;                  /* Enable debug UI? */
char szBootDiscImage[MAX_FILENAME_LENGTH] = { "" };

char szWorkingDir[MAX_FILENAME_LENGTH] = { "" };
char szCurrentDir[MAX_FILENAME_LENGTH] = { "" };

unsigned char STRam[16*1024*1024];        /* This is our ST Ram, includes all TOS/hardware areas for ease */



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
    nBytes = STRamEnd;  /* was: STRamEnd_BusErr */
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
  fprintf(stderr,"%s: %s\n", lpCaption, lpText);

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
    Audio_EnableAudio(ConfigureParams.Sound.bEnableSound);
    bFullScreenHold = FALSE;      /* Release hold  */
    Screen_SetFullUpdate();       /* Cause full screen update(to clear all) */

    bEmulationActive = TRUE;
  }
}


/* ----------------------------------------------------------------------- */
/*
  Message handler
  Here we process the SDL events (keyboard, mouse, ...) and map it to
  Atari IKBD events.
*/
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
       Keymap_KeyDown(&event.key.keysym);
       break;
    case SDL_KEYUP:
       Keymap_KeyUp(&event.key.keysym);
       break;
   }
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
               "  --printer             Enable printer support (experimental).\n"
               "  --midi <filename>     Enable midi support and write midi data to <filename>.\n"
               "  --frameskip           Skip every second frame (speeds up emulation!).\n"
               "  --debug or -D         Allow debug interface.\n"
               "  --harddrive <dir>     Emulate an ST harddrive\n"
               "     or -d <dir>         (<dir> = root directory).\n"
               "  --hdimage <imagename> Emulate an ST harddrive with an image.\n"
               "  --tos <file>          Use TOS image <file>.\n"
               "  --cpulevel <x>        Set the CPU type (x => 680x0) (TOS 2.06 only!).\n"
               "  --compatible          Use a more compatible (but slower) 68000 CPU mode.\n"
               "  --blitter             Enable blitter emulation (unstable!)\n"
               "  --vdi                 Use extended VDI resolution\n"
               "  --memsize <x>         Memory size in MB (x = 0, 1, 2 or 4; 0 for 512kB)\n"
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
      else if ( !strcmp(argv[i],"--printer") )
      {
        /* FIXME: add more commandline configuration for printing */
        ConfigureParams.Printer.bEnablePrinting = TRUE;
      }
      else if (!strcmp(argv[i], "--midi"))
      {
        if(i+1 >= argc)
          fprintf(stderr, "Missing argument for --midi\n");
        else
        {
          if(strlen(argv[i+1]) <= MAX_FILENAME_LENGTH)
          {
            ConfigureParams.Midi.bEnableMidi = TRUE;
            strcpy(ConfigureParams.Midi.szMidiOutFileName, argv[i+1]);
          }
          else fprintf(stderr, "Midi file name too long!\n");
          i += 1;
        }
      }
      else if (!strcmp(argv[i],"--debug") || !strcmp(argv[i],"-D"))
      {
        bEnableDebug=TRUE;
      }
      else if (!strcmp(argv[i],"--hdimage"))
      {
        if(i+1 >= argc)
          fprintf(stderr, "Missing argument for --hdimage\n");
        else
        {
          if(strlen(argv[i+1]) <= MAX_FILENAME_LENGTH)
          {
            ConfigureParams.HardDisc.bUseHardDiscImage = TRUE;
            strcpy(ConfigureParams.HardDisc.szHardDiscImage, argv[i+1]);
          }
          else fprintf(stderr, "HD image file name too long!\n");
          i += 1;
        }
      }
      else if (!strcmp(argv[i],"--harddrive") || !strcmp(argv[i],"-d"))
      {
        if(i+1 >= argc)
          fprintf(stderr, "Missing argument for --harddrive\n");
        else
        {
          if(strlen(argv[i+1]) <= MAX_PATH )
          {
            ConfigureParams.HardDisc.bUseHardDiscDirectories = TRUE;
            ConfigureParams.HardDisc.bBootFromHardDisc = TRUE;
            strcpy(ConfigureParams.HardDisc.szHardDiscDirectories[0], argv[i+1]);
          }
          else fprintf(stderr, "HD directory name too long!\n");
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
        ConfigureParams.System.nCpuLevel = cpu_level;
      }
      else if (!strcmp(argv[i],"--compatible") || !strcmp(argv[i],"-d"))
      {
        cpu_compatible = TRUE;
        ConfigureParams.System.bCompatibleCpu = TRUE;
      }
      else if (!strcmp(argv[i],"--blitter"))
      {
        bEnableBlitter = TRUE;
        ConfigureParams.System.bBlitter = TRUE;
      }
      else if (!strcmp(argv[i], "--vdi"))
      {
        bUseVDIRes = ConfigureParams.TOSGEM.bUseExtGEMResolutions = TRUE;
      }
      else if (!strcmp(argv[i], "--memsize"))
      {
        int memorysize = MEMORY_SIZE_1Mb;
        if(i+1 >= argc)
          fprintf(stderr,"Missing argument for --memsize.\n");
        else
          memorysize = atoi(argv[++i]);
        if(memorysize == 0)
          ConfigureParams.Memory.nMemorySize = MEMORY_SIZE_512Kb;
        else if(memorysize == 2)
          ConfigureParams.Memory.nMemorySize = MEMORY_SIZE_2Mb;
        else if(memorysize == 4)
          ConfigureParams.Memory.nMemorySize = MEMORY_SIZE_4Mb;
        else  /* Use 1MB as default */
          ConfigureParams.Memory.nMemorySize = MEMORY_SIZE_1Mb;
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
  /* Init SDL's video subsystem. Note: Audio and joystick subsystems
     will be initialized later (failures there are not fatal). */
  if(SDL_Init(SDL_INIT_VIDEO) < 0)
  {
    fprintf(stderr, "Could not initialize the SDL library:\n %s\n", SDL_GetError() );
    exit(-1);
  }

  Misc_SeedRandom(1043618);
  SDLGui_Init();
  Printer_Init();
  RS232_Init();
  Midi_Init();
  Screen_Init();
  Floppy_Init();
  Init680x0();                  /* Init CPU emulation */
  Audio_Init();
  Keymap_Init();

  /* Init HD emulation */
  if(ConfigureParams.HardDisc.bUseHardDiscImage)
  {
    char *szHardDiscImage = ConfigureParams.HardDisc.szHardDiscImage;
    if( HDC_Init(szHardDiscImage) )
      printf("Hard drive image %s mounted.\n", szHardDiscImage);
    else
      printf("Couldn't open HD file: %s, or no partitions\n", szHardDiscImage);
  }
  GemDOS_Init();
  if(ConfigureParams.HardDisc.bUseHardDiscDirectories)
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

  Intercept_Init();
  Joy_Init();
  Sound_Init();

  /* Check passed disc image parameter, boot directly into emulator */
  if(strlen(szBootDiscImage) > 0)
  {
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
  Floppy_EjectBothDrives();
  Floppy_UnInit();
  HDC_UnInit();
  Midi_UnInit();
  RS232_UnInit();
  Printer_UnInit();
  Intercept_UnInit();
  GemDOS_UnInitDrives();
  if(Sound_AreWeRecording())
    Sound_EndRecording();
  Audio_UnInit();
  YMFormat_FreeRecording();
  SDLGui_UnInit();
  Screen_UnInit();

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

  /* Now load the values from the configuration file */
  Configuration_Load();

  /* Check for any passed parameters */
  Main_ReadParameters(argc, argv);

  /* Init emulator system */
  Main_Init();

  /* Switch immediately to fullscreen if user wants to */
  if( bUseFullscreen )
    Screen_EnterFullScreen();

  /* Run emulation */
  Main_UnPauseEmulation();
  Start680x0();                 /* Start emulation */

  /* Un-init emulation system */
  Main_UnInit();

  /* Close debug files */
  ErrLog_CloseFile();
  Debug_CloseFiles();

  return(0);
}


