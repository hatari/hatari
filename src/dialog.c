/*
  Hatari - dialog.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This is normal 'C' code to handle our options dialog. We keep all our configuration details
  in a variable 'ConfigureParams'. When we open our dialog we copy this and then when we 'OK'
  or 'Cancel' the dialog we can compare and makes the necessary changes.
*/
static char rcsid[] = "Hatari $Id: dialog.c,v 1.31 2003-04-28 17:48:55 thothy Exp $";

#include <unistd.h>

#include "main.h"
#include "configuration.h"
#include "audio.h"
#include "debug.h"
#include "dialog.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "joy.h"
#include "keymap.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "printer.h"
#include "reset.h"
#include "rs232.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "sound.h"
#include "tos.h"
#include "vdi.h"
#include "video.h"
#include "sdlgui.h"
#include "uae-cpu/hatari-glue.h"
#include "intercept.h"

extern void Screen_DidResolutionChange(void);


/* The main dialog: */
#define MAINDLG_ABOUT    2
#define MAINDLG_DISCS    3
#define MAINDLG_TOSGEM   4
#define MAINDLG_SCREEN   5
#define MAINDLG_SOUND    6
#define MAINDLG_CPU      7
#define MAINDLG_MEMORY   8
#define MAINDLG_JOY      9
#define MAINDLG_KEYBD    10
#define MAINDLG_DEVICES  11
#define MAINDLG_NORESET  12
#define MAINDLG_RESET    13
#define MAINDLG_OK       14
#define MAINDLG_CANCEL   15
#define MAINDLG_QUIT     16
SGOBJ maindlg[] =
{
  { SGBOX, 0, 0, 0,0, 36,20, NULL },
  { SGTEXT, 0, 0, 10,1, 16,1, "Hatari main menu" },
  { SGBUTTON, 0, 0, 4,4, 12,1, "About" },
  { SGBUTTON, 0, 0, 4,6, 12,1, "Discs" },
  { SGBUTTON, 0, 0, 4,8, 12,1, "TOS/GEM" },
  { SGBUTTON, 0, 0, 4,10, 12,1, "Screen" },
  { SGBUTTON, 0, 0, 4,12, 12,1, "Sound" },
  { SGBUTTON, 0, 0, 20,4, 12,1, "System" },
  { SGBUTTON, 0, 0, 20,6, 12,1, "Memory" },
  { SGBUTTON, 0, 0, 20,8, 12,1, "Joysticks" },
  { SGBUTTON, 0, 0, 20,10, 12,1, "Keyboard" },
  { SGBUTTON, 0, 0, 20,12, 12,1, "Devices" },
  { SGRADIOBUT, 0, 0, 2,16, 10,1, "No Reset" },
  { SGRADIOBUT, 0, 0, 2,18, 10,1, "Reset ST" },
  { SGBUTTON, 0, 0, 14,16, 8,3, "Okay" },
  { SGBUTTON, 0, 0, 25,18, 8,1, "Cancel" },
  { SGBUTTON, 0, 0, 25,16, 8,1, "Quit" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The "About"-dialog: */
SGOBJ aboutdlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGTEXT, 0, 0, 14,1, 12,1, PROG_NAME },
  { SGTEXT, 0, 0, 14,2, 12,1, "============" },
  { SGTEXT, 0, 0, 1,4, 38,1, "Hatari has been written by:  T. Huth," },
  { SGTEXT, 0, 0, 1,5, 38,1, "S. Marothy, S. Berndtsson, P. Bates," },
  { SGTEXT, 0, 0, 1,6, 38,1, "B. Schmidt and many others." },
  { SGTEXT, 0, 0, 1,7, 38,1, "Please see the docs for more info." },
  { SGTEXT, 0, 0, 1,9, 38,1, "This program is free software; you can" },
  { SGTEXT, 0, 0, 1,10, 38,1, "redistribute it and/or modify it under" },
  { SGTEXT, 0, 0, 1,11, 38,1, "the terms of the GNU General Public" },
  { SGTEXT, 0, 0, 1,12, 38,1, "License as published by the Free Soft-" },
  { SGTEXT, 0, 0, 1,13, 38,1, "ware Foundation; either version 2 of" },
  { SGTEXT, 0, 0, 1,14, 38,1, "the License, or (at your option) any" },
  { SGTEXT, 0, 0, 1,15, 38,1, "later version." },
  { SGTEXT, 0, 0, 1,17, 38,1, "This program is distributed in the" },
  { SGTEXT, 0, 0, 1,18, 38,1, "hope that it will be useful, but" },
  { SGTEXT, 0, 0, 1,19, 38,1, "WITHOUT ANY WARRANTY. See the GNU Ge-" },
  { SGTEXT, 0, 0, 1,20, 38,1, "neral Public License for more details." },
  { SGBUTTON, 0, 0, 16,23, 8,1, "Okay" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The discs dialog: */
#define DISCDLG_DISCA       4
#define DISCDLG_BROWSEA     5
#define DISCDLG_DISCB       7
#define DISCDLG_BROWSEB     8
#define DISCDLG_IMGDIR      10
#define DISCDLG_BROWSEIMG   11
#define DISCDLG_AUTOB       12
#define DISCDLG_CREATEIMG   13
#define DISCDLG_BROWSEHDIMG 17
#define DISCDLG_DISCHDIMG   18
#define DISCDLG_UNMOUNTGDOS 20
#define DISCDLG_BROWSEGDOS  21
#define DISCDLG_DISCGDOS    22
#define DISCDLG_BOOTHD      23
#define DISCDLG_EXIT        24
SGOBJ discdlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGBOX, 0, 0, 1,1, 38,11, NULL },
  { SGTEXT, 0, 0, 14,1, 12,1, "Floppy discs" },
  { SGTEXT, 0, 0, 2,3, 2,1, "A:" },
  { SGTEXT, 0, 0, 5,3, 26,1, NULL },
  { SGBUTTON, 0, 0, 32,3, 6,1, "Browse" },
  { SGTEXT, 0, 0, 2,5, 2,1, "B:" },
  { SGTEXT, 0, 0, 5,5, 26,1, NULL },
  { SGBUTTON, 0, 0, 32,5, 6,1, "Browse" },
  { SGTEXT, 0, 0, 2,7, 30,1, "Default disk images directory:" },
  { SGTEXT, 0, 0, 2,8, 28,1, NULL },
  { SGBUTTON, 0, 0, 32,8, 6,1, "Browse" },
  { SGCHECKBOX, 0, 0, 2,10, 18,1, "Auto insert B" },
  { SGTEXT/*SGBUTTON*/, 0, 0, 20,10, 18,1, ""/*"Create blank image"*/ }, /* Not yet supported */
  { SGBOX, 0, 0, 1,13, 38,9, NULL },
  { SGTEXT, 0, 0, 15,13, 10,1, "Hard discs" },
  { SGTEXT, 0, 0, 2,14, 9,1, "HD image:" },
  { SGBUTTON, 0, 0, 32,14, 6,1, "Browse" },
  { SGTEXT, 0, 0, 2,15, 36,1, NULL },
  { SGTEXT, 0, 0, 2,17, 13,1, "GEMDOS drive:" },
  { SGBUTTON, 0, 0, 30,17, 1,1, "\x01" },         /* Up-arrow button for unmounting */
  { SGBUTTON, 0, 0, 32,17, 6,1, "Browse" },
  { SGTEXT, 0, 0, 2,18, 36,1, NULL },
  { SGCHECKBOX, 0, 0, 2,20, 14,1, "Boot from HD" },
  { SGBUTTON, 0, 0, 10,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The TOS/GEM dialog: */
#define DLGTOSGEM_ROMNAME    4
#define DLGTOSGEM_ROMBROWSE  5
#define DLGTOSGEM_GEMRES     8
#define DLGTOSGEM_RES640     10
#define DLGTOSGEM_RES800     11
#define DLGTOSGEM_RES1024    12
#define DLGTOSGEM_BPP1       14
#define DLGTOSGEM_BPP2       15
#define DLGTOSGEM_BPP4       16
#define DLGTOSGEM_EXIT       17
SGOBJ tosgemdlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,24, NULL },
  { SGBOX, 0, 0, 1,1, 38,8, NULL },
  { SGTEXT, 0, 0, 16,2, 9,1, "TOS setup" },
  { SGTEXT, 0, 0, 2,5, 25,1, "ROM image (needs reset!):" },
  { SGTEXT, 0, 0, 2,7, 34,1, NULL },
  { SGBUTTON, 0, 0, 30,5, 8,1, "Browse" },
  { SGBOX, 0, 0, 1,10, 38,10, NULL },
  { SGTEXT, 0, 0, 16,11, 9,1, "GEM setup" },
  { SGCHECKBOX, 0, 0, 2,13, 25,1, "Use extended resolution" },
  { SGTEXT, 0, 0, 2,15, 11,1, "Resolution:" },
  { SGRADIOBUT, 0, 0, 4,16, 9,1, "640x480" },
  { SGRADIOBUT, 0, 0, 16,16, 9,1, "800x600" },
  { SGRADIOBUT, 0, 0, 28,16, 10,1, "1024x768" },
  { SGTEXT, 0, 0, 2,18, 6,1, "Depth:" },
  { SGRADIOBUT, 0, 0, 11,18, 6,1, "1bpp" },
  { SGRADIOBUT, 0, 0, 20,18, 6,1, "2bpp" },
  { SGRADIOBUT, 0, 0, 29,18, 6,1, "4bpp" },
  { SGBUTTON, 0, 0, 10,22, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The screen dialog: */
#define DLGSCRN_FULLSCRN   3
#define DLGSCRN_INTERLACE  4
#define DLGSCRN_FRAMESKIP  5
#define DLGSCRN_OVERSCAN   6
#define DLGSCRN_COLOR      8
#define DLGSCRN_MONO       9
#define DLGSCRN_8BPP       11
#define DLGSCRN_LOW320     12
#define DLGSCRN_LOW640     13
// #define DLGSCRN_LOW800     14
#define DLGSCRN_ONCHANGE   16
#define DLGSCRN_FPSPOPUP   18
#define DLGSCRN_CAPTURE    19
#define DLGSCRN_RECANIM    20
#define DLGSCRN_EXIT       21

/* This emulator is not supposed to display lowres in more than 640x480, so just remove
   800x600, which was here only for poor windows users who wanted a resolution with enough
   room to draw borders... */

SGOBJ screendlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGBOX, 0, 0, 1,1, 38,13, NULL },
  { SGTEXT, 0, 0, 13,2, 14,1, "Screen options" },
  { SGCHECKBOX, 0, 0, 4,4, 12,1, "Fullscreen" },
  { SGCHECKBOX, 0, 0, 4,5, 23,1, "Interlaced mode" },
  { SGCHECKBOX, 0, 0, 4,6, 10,1, "Frame skip" },
  { SGCHECKBOX, 0, 0, 4,7, 13,1, "Use borders" },
  { SGTEXT, 0, 0, 4,8, 8,1, "Monitor:" },
  { SGRADIOBUT, 0, 0, 15,8, 7,1, "Color" },
  { SGRADIOBUT, 0, 0, 25,8, 6,1, "Mono" },
  { SGTEXT, 0, 0, 4,10, 23,1, "ST-Low mode:" },
  { SGCHECKBOX, 0, 0, 30,10, 7,1, "8 bpp" },
  { SGRADIOBUT, 0, 0, 5,12, 9,1, "320x240" },
  { SGRADIOBUT, 0, 0, 16,12, 9,1, "640x480" },
/*   { SGRADIOBUT, 0, 0, 27,12, 9,1, "800x600" }, */
  { SGBOX, 0, 0, 1,15, 38,7, NULL },
  { SGTEXT, 0, 0, 13,16, 14,1, "Screen capture" },
  { SGCHECKBOX, 0, 0, 3,18, 27,1, "Only when display changes" },
  { SGTEXT, 0, 0, 31,18, 4,1, ""/*"FPS:"*/ },
  { SGTEXT/*SGPOPUP*/, 0, 0, 36,18, 3,1, ""/*"25"*/ },
  { SGBUTTON, 0, 0, 3,20, 16,1, "Capture screen" },
  { SGBUTTON, 0, 0, 20,20, 18,1, NULL },
  { SGBUTTON, 0, 0, 10,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The sound dialog: */
#define DLGSOUND_ENABLE  3
#define DLGSOUND_LOW     5
#define DLGSOUND_MEDIUM  6
#define DLGSOUND_HIGH    7
#define DLGSOUND_YM      10
#define DLGSOUND_WAV     11
#define DLGSOUND_RECORD  12
#define DLGSOUND_EXIT    13
SGOBJ sounddlg[] =
{
  { SGBOX, 0, 0, 0,0, 38,24, NULL },
  { SGBOX, 0, 0, 1,1, 36,11, NULL },
  { SGTEXT, 0, 0, 13,2, 13,1, "Sound options" },
  { SGCHECKBOX, 0, 0, 12,4, 14,1, "Enable sound" },
  { SGTEXT, 0, 0, 11,6, 14,1, "Playback quality:" },
  { SGRADIOBUT, 0, 0, 12,8, 15,1, "Low (11kHz)" },
  { SGRADIOBUT, 0, 0, 12,9, 19,1, "Medium (22kHz)" },
  { SGRADIOBUT, 0, 0, 12,10, 14,1, "High (44kHz)" },
  { SGBOX, 0, 0, 1,13, 36,7, NULL },
  { SGTEXT, 0, 0, 13,14, 14,1, "Capture YM/WAV" },
  { SGRADIOBUT, 0, SG_SELECTED, 7,16, 11,1, "hatari.ym" },
  { SGRADIOBUT, 0, 0, 20,16, 12,1, "hatari.wav" },
  { SGBUTTON, 0, 0, 12,18, 16,1, NULL },
  { SGBUTTON, 0, 0, 10,22, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The "System" dialog: */
#define DLGSYS_68000 3
#define DLGSYS_68010 4
#define DLGSYS_68020 5
#define DLGSYS_68030 6
#define DLGSYS_68040 7
#define DLGSYS_PREFETCH 8
#define DLGSYS_BLITTER 9
SGOBJ systemdlg[] =
{
  { SGBOX, 0, 0, 0,0, 30,17, NULL },
  { SGTEXT, 0, 0, 8,1, 14,1, "System options" },
  { SGTEXT, 0, 0, 3,4, 8,1, "CPU Type:" },
  { SGRADIOBUT, 0, 0, 16,4, 7,1, "68000" },
  { SGRADIOBUT, 0, 0, 16,5, 7,1, "68010" },
  { SGRADIOBUT, 0, 0, 16,6, 7,1, "68020" },
  { SGRADIOBUT, 0, 0, 16,7, 11,1, "68020+FPU" },
  { SGRADIOBUT, 0, 0, 16,8, 7,1, "68040" },
  { SGCHECKBOX, 0, 0, 3,10, 24,1, "Use CPU prefetch mode" },
  { SGCHECKBOX, 0, 0, 3,12, 20,1, "Blitter emulation" },
  { SGBUTTON, 0, 0, 5,15, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The memory dialog: */
#define DLGMEM_512KB   4
#define DLGMEM_1MB     5
#define DLGMEM_2MB     6
#define DLGMEM_4MB     7
#define DLGMEM_EXIT    8/*14*/
SGOBJ memorydlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,11/*21*/, NULL },
  { SGBOX, 0, 0, 1,1, 38,7, NULL },
  { SGTEXT, 0, 0, 15,2, 12,1, "Memory setup" },
  { SGTEXT, 0, 0, 4,4, 12,1, "ST-RAM size:" },
  { SGRADIOBUT, 0, 0, 19,4, 8,1, "512 kB" },
  { SGRADIOBUT, 0, 0, 30,4, 6,1, "1 MB" },
  { SGRADIOBUT, 0, 0, 19,6, 6,1, "2 MB" },
  { SGRADIOBUT, 0, 0, 30,6, 6,1, "4 MB" },
/*
  { SGBOX, 0, 0, 1,11, 38,7, NULL },
  { SGTEXT, 0, 0, 12,12, 17,1, "Memory state save" },
  { SGTEXT, 0, 0, 2,14, 28,1, "/Sorry/Not/yet/supported" },
  { SGBUTTON, 0, 0, 32,14, 6,1, "Browse" },
  { SGBUTTON, 0, 0, 8,16, 10,1, "Save" },
  { SGBUTTON, 0, 0, 22,16, 10,1, "Restore" },
*/
  { SGBUTTON, 0, 0, 10,9/*19*/, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The joysticks dialog: */
#define DLGJOY_J1CURSOR    4
#define DLGJOY_J1AUTOFIRE  5
#define DLGJOY_J0CURSOR    8
#define DLGJOY_J0AUTOFIRE  9
#define DLGJOY_EXIT        10
SGOBJ joystickdlg[] =
{
  { SGBOX, 0, 0, 0,0, 30,19, NULL },
  { SGTEXT, 0, 0, 7,1, 15,1, "Joysticks setup" },
  { SGBOX, 0, 0, 1,3, 28,6, NULL },
  { SGTEXT, 0, 0, 2,4, 11,1, "Joystick 1:" },
  { SGCHECKBOX, 0, 0, 5,6, 22,1, "Use cursor emulation" },
  { SGCHECKBOX, 0, 0, 5,7, 17,1, "Enable autofire" },
  { SGBOX, 0, 0, 1,10, 28,6, NULL },
  { SGTEXT, 0, 0, 2,11, 11,1, "Joystick 0:" },
  { SGCHECKBOX, 0, 0, 5,13, 22,1, "Use cursor emulation" },
  { SGCHECKBOX, 0, 0, 5,14, 17,1, "Enable autofire" },
  { SGBUTTON, 0, 0, 5,17, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The keyboard dialog: */
#define DLGKEY_SYMBOLIC 3
#define DLGKEY_SCANCODE 4
SGOBJ keyboarddlg[] =
{
  { SGBOX, 0, 0, 0,0, 30,10, NULL },
  { SGTEXT, 0, 0, 8,1, 14,1, "Keyboard setup" },
  { SGTEXT, 0, 0, 2,3, 17,1, "Keyboard mapping:" },
  { SGRADIOBUT, 0, 0, 4,5, 10,1, "Symbolic" },
  { SGRADIOBUT, 0, 0, 18,5, 10,1, "Scancode" },
  { SGBUTTON, 0, 0, 5,8, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The devices dialog: */
SGOBJ devicedlg[] =
{
  { SGBOX, 0, 0, 0,0, 30,8, NULL },
  { SGTEXT, 0, 0, 8,2, 13,1, "Devices setup" },
  { SGTEXT, 0, 0, 2,4, 25,1, "Sorry, not yet supported." },
  { SGBUTTON, 0, 0, 5,6, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};



CNF_PARAMS DialogParams;   /* List of configuration for dialogs (so the user can also choose 'Cancel') */



/*-----------------------------------------------------------------------*/
/*
  Check if need to warn user that changes will take place after reset
  Return TRUE if wants to reset
*/
BOOL Dialog_DoNeedReset(void)
{
  /* Did we change colour/mono monitor? If so, must reset */
  if (ConfigureParams.Screen.bUseHighRes!=DialogParams.Screen.bUseHighRes)
    return(TRUE);
  /* Did change to GEM VDI display? */
  if (ConfigureParams.TOSGEM.bUseExtGEMResolutions!=DialogParams.TOSGEM.bUseExtGEMResolutions)
    return(TRUE);
  /* Did change GEM resolution or colour depth? */
  if ( DialogParams.TOSGEM.bUseExtGEMResolutions &&
      ((ConfigureParams.TOSGEM.nGEMResolution!=DialogParams.TOSGEM.nGEMResolution)
       || (ConfigureParams.TOSGEM.nGEMColours!=DialogParams.TOSGEM.nGEMColours)) )
    return(TRUE);
  /* Did change TOS ROM image? */
  if (strcmp(DialogParams.TOSGEM.szTOSImageFileName, ConfigureParams.TOSGEM.szTOSImageFileName))
    return(TRUE);
  /* Did change HD image? */
  if (strcmp(DialogParams.HardDisc.szHardDiscImage, ConfigureParams.HardDisc.szHardDiscImage))
    return(TRUE);
  /* Did change GEMDOS drive? */
  if (strcmp(DialogParams.HardDisc.szHardDiscDirectories[0], ConfigureParams.HardDisc.szHardDiscDirectories[0]))
    return(TRUE);

  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Copy details back to configuration and perform reset
*/
void Dialog_CopyDialogParamsToConfiguration(BOOL bForceReset)
{
  BOOL NeedReset;
  BOOL newGemdosDrive;

  /* Do we need to warn user of that changes will only take effect after reset? */
  if (bForceReset)
    NeedReset = bForceReset;
  else
    NeedReset = Dialog_DoNeedReset();

  /* Do need to change resolution? Need if change display/overscan settings */
  /*(if switch between Colour/Mono cause reset later) */
  if( (DialogParams.Screen.ChosenDisplayMode!=ConfigureParams.Screen.ChosenDisplayMode)
      || (DialogParams.Screen.bAllowOverscan!=ConfigureParams.Screen.bAllowOverscan) ) {
    if(bInFullScreen) Screen_ReturnFromFullScreen();
    ConfigureParams.Screen.ChosenDisplayMode = DialogParams.Screen.ChosenDisplayMode;
    ConfigureParams.Screen.bAllowOverscan = DialogParams.Screen.bAllowOverscan;
    if(bInFullScreen)
      Screen_EnterFullScreen();
    else {
      PrevSTRes = -1;
      Screen_DidResolutionChange();
    }
  }

  /* Did set new printer parameters? */
  if( (DialogParams.Printer.bEnablePrinting!=ConfigureParams.Printer.bEnablePrinting)
     || (DialogParams.Printer.bPrintToFile!=ConfigureParams.Printer.bPrintToFile)
     || (strcmp(DialogParams.Printer.szPrintToFileName,ConfigureParams.Printer.szPrintToFileName)) )
    Printer_CloseAllConnections();

  /* Did set new RS232 parameters? */
  if( (DialogParams.RS232.bEnableRS232!=ConfigureParams.RS232.bEnableRS232)
     || (DialogParams.RS232.nCOMPort!=ConfigureParams.RS232.nCOMPort) )
    RS232_CloseCOMPort();

  /* Did stop sound? Or change playback Hz. If so, also stop sound recording */
  if( (!DialogParams.Sound.bEnableSound)
     || (DialogParams.Sound.nPlaybackQuality!=ConfigureParams.Sound.nPlaybackQuality) )
  {
    if(Sound_AreWeRecording())
      Sound_EndRecording(NULL);
  }

  /* Did change GEMDOS drive? */
  if( strcmp(DialogParams.HardDisc.szHardDiscDirectories[0], ConfigureParams.HardDisc.szHardDiscDirectories[0])!=0 )
  {
    GemDOS_UnInitDrives();
    newGemdosDrive = TRUE;
  }
  else
  {
    newGemdosDrive = FALSE;
  }

  /* Did change HD image? */
  if( strcmp(DialogParams.HardDisc.szHardDiscImage, ConfigureParams.HardDisc.szHardDiscImage)!=0
     && ACSI_EMU_ON )
  {
    HDC_UnInit();
  }

  /* Copy details to configuration, so can be saved out or set on reset */
  ConfigureParams = DialogParams;
  /* And write to configuration now, so don't loose */
  Configuration_Save();

  /* Copy details to global, if we reset copy them all */
  Dialog_CopyDetailsFromConfiguration(NeedReset);

  /* Set keyboard remap file */
  /*Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);*/

  /* Resize window if need */
  /*if(!ConfigureParams.TOSGEM.bUseExtGEMResolutions)
    View_ResizeWindowToFull();*/

  /* Did the user changed the CPU mode? */
  check_prefs_changed_cpu(DialogParams.System.nCpuLevel, DialogParams.System.bCompatibleCpu);

  /* Mount a new HD image: */
  if( !ACSI_EMU_ON && !File_DoesFileNameEndWithSlash(ConfigureParams.HardDisc.szHardDiscImage)
      && File_Exists(ConfigureParams.HardDisc.szHardDiscImage) )
  {
    HDC_Init(ConfigureParams.HardDisc.szHardDiscImage);
  }

  /* Mount a new GEMDOS drive? */
  if( newGemdosDrive )
  {
    GemDOS_InitDrives();
  }

  /* Did change blitter status? */
  Intercept_EnableBlitter(ConfigureParams.System.bBlitter);

  /* Do we need to perform reset? */
  if (NeedReset)
  {
    Reset_Cold();
    /*FM  View_ToggleWindowsMouse(MOUSE_ST);*/
  }

  /* Go into/return from full screen if flagged */
  if ( (!bInFullScreen) && (DialogParams.Screen.bFullScreen) )
    Screen_EnterFullScreen();
  else if ( bInFullScreen && (!DialogParams.Screen.bFullScreen) )
    Screen_ReturnFromFullScreen();
}



/*-----------------------------------------------------------------------*/
/*
  Copy details from configuration structure into global variables for system
*/
void Dialog_CopyDetailsFromConfiguration(BOOL bReset)
{
  /* Set new timer thread */
/*FM  Main_SetSpeedThreadTimer(ConfigureParams.Configure.nMinMaxSpeed);*/
  /* Set resolution change */
  if (bReset)
  {
    bUseVDIRes = ConfigureParams.TOSGEM.bUseExtGEMResolutions;
    bUseHighRes = (!bUseVDIRes && ConfigureParams.Screen.bUseHighRes)
                   || (bUseVDIRes && ConfigureParams.TOSGEM.nGEMColours==GEMCOLOUR_2);
    VDI_SetResolution(ConfigureParams.TOSGEM.nGEMResolution, ConfigureParams.TOSGEM.nGEMColours);
  }

  /* Set playback frequency */
  if( ConfigureParams.Sound.bEnableSound )
    Audio_SetOutputAudioFreq(ConfigureParams.Sound.nPlaybackQuality);

  /* Remove slashes, etc.. from names */
  File_CleanFileName(ConfigureParams.TOSGEM.szTOSImageFileName);
}



/*-----------------------------------------------------------------------*/
/*
  Show and process the disc image dialog.
*/
void Dialog_DiscDlg(void)
{
  int but;
  char tmpname[MAX_FILENAME_LENGTH];
  char dlgnamea[40], dlgnameb[40], dlgdiscdir[40];
  char dlgnamegdos[40], dlgnamehdimg[40];
  char *zip_path = Memory_Alloc(MAX_FILENAME_LENGTH);

  SDLGui_CenterDlg(discdlg);

  /* Set up dialog to actual values: */

  /* Disc name A: */
  if( EmulationDrives[0].bDiscInserted )
    File_ShrinkName(dlgnamea, EmulationDrives[0].szFileName, discdlg[DISCDLG_DISCA].w);
  else
    dlgnamea[0] = 0;
  discdlg[DISCDLG_DISCA].txt = dlgnamea;

  /* Disc name B: */
  if( EmulationDrives[1].bDiscInserted )
    File_ShrinkName(dlgnameb, EmulationDrives[1].szFileName, discdlg[DISCDLG_DISCB].w);
  else
    dlgnameb[0] = 0;
  discdlg[DISCDLG_DISCB].txt = dlgnameb;

  /* Default image directory: */
  File_ShrinkName(dlgdiscdir, DialogParams.DiscImage.szDiscImageDirectory, discdlg[DISCDLG_IMGDIR].w);
  discdlg[DISCDLG_IMGDIR].txt = dlgdiscdir;

  /* Auto insert disc B: */
  if( DialogParams.DiscImage.bAutoInsertDiscB )
    discdlg[DISCDLG_AUTOB].state |= SG_SELECTED;
   else
    discdlg[DISCDLG_AUTOB].state &= ~SG_SELECTED;

  /* Boot from harddisk? */
  if( DialogParams.HardDisc.bBootFromHardDisc )
    discdlg[DISCDLG_BOOTHD].state |= SG_SELECTED;
   else
    discdlg[DISCDLG_BOOTHD].state &= ~SG_SELECTED;

  /* GEMDOS Hard disc directory: */
  if( strcmp(DialogParams.HardDisc.szHardDiscDirectories[0], ConfigureParams.HardDisc.szHardDiscDirectories[0])!=0
      || GEMDOS_EMU_ON )
    File_ShrinkName(dlgnamegdos, DialogParams.HardDisc.szHardDiscDirectories[0], discdlg[DISCDLG_DISCGDOS].w);
  else
    dlgnamegdos[0] = 0;
  discdlg[DISCDLG_DISCGDOS].txt = dlgnamegdos;

  /* Hard disc image: */
  if( ACSI_EMU_ON )
    File_ShrinkName(dlgnamehdimg, DialogParams.HardDisc.szHardDiscImage, discdlg[DISCDLG_DISCHDIMG].w);
  else
    dlgnamehdimg[0] = 0;
  discdlg[DISCDLG_DISCHDIMG].txt = dlgnamehdimg;

  /* Draw and process the dialog */
  do
  {
    but = SDLGui_DoDialog(discdlg);
    switch(but)
    {
      case DISCDLG_BROWSEA:                       /* Choose a new disc A: */
        if( EmulationDrives[0].bDiscInserted )
          strcpy(tmpname, EmulationDrives[0].szFileName);
         else
          strcpy(tmpname, DialogParams.DiscImage.szDiscImageDirectory);
        if( SDLGui_FileSelect(tmpname, zip_path) )
        {
          if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          {
            Floppy_ZipInsertDiscIntoDrive(0, tmpname, zip_path); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            File_ShrinkName(dlgnamea, tmpname, discdlg[DISCDLG_DISCA].w);
          }
          else
          {
            Floppy_EjectDiscFromDrive(0, FALSE); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            dlgnamea[0] = 0;
          }
        }
        break;
      case DISCDLG_BROWSEB:                       /* Choose a new disc B: */
        if( EmulationDrives[1].bDiscInserted )
          strcpy(tmpname, EmulationDrives[1].szFileName);
         else
          strcpy(tmpname, DialogParams.DiscImage.szDiscImageDirectory);
        if( SDLGui_FileSelect(tmpname, zip_path) )
        {
          if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          {
            Floppy_ZipInsertDiscIntoDrive(1, tmpname, zip_path); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            File_ShrinkName(dlgnameb, tmpname, discdlg[DISCDLG_DISCB].w);
          }
          else
          {
            Floppy_EjectDiscFromDrive(1, FALSE); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            dlgnameb[0] = 0;
          }
        }
        break;
      case DISCDLG_BROWSEIMG:
        strcpy(tmpname, DialogParams.DiscImage.szDiscImageDirectory);
        if( SDLGui_FileSelect(tmpname, NULL) )
        {
          char *ptr;
          ptr = strrchr(tmpname, '/');
          if( ptr!=NULL )  ptr[1]=0;
          strcpy(DialogParams.DiscImage.szDiscImageDirectory, tmpname);
          File_ShrinkName(dlgdiscdir, DialogParams.DiscImage.szDiscImageDirectory, discdlg[DISCDLG_IMGDIR].w);
        }
        break;
      case DISCDLG_CREATEIMG:
        fprintf(stderr,"Sorry, creating disc images not yet supported\n");
        break;
      case DISCDLG_UNMOUNTGDOS:
        GemDOS_UnInitDrives();   /* FIXME: This shouldn't be done here but it's the only quick solution I could think of */
        strcpy(DialogParams.HardDisc.szHardDiscDirectories[0], ConfigureParams.HardDisc.szHardDiscDirectories[0]);
        dlgnamegdos[0] = 0;
        break;
      case DISCDLG_BROWSEGDOS:
        strcpy(tmpname, DialogParams.HardDisc.szHardDiscDirectories[0]);
        if( SDLGui_FileSelect(tmpname, NULL) )
        {
          char *ptr;
          ptr = strrchr(tmpname, '/');
          if( ptr!=NULL )  ptr[1]=0;        /* Remove file name from path */
          strcpy(DialogParams.HardDisc.szHardDiscDirectories[0], tmpname);
          File_ShrinkName(dlgnamegdos, DialogParams.HardDisc.szHardDiscDirectories[0], discdlg[DISCDLG_DISCGDOS].w);
        }
        break;
      case DISCDLG_BROWSEHDIMG:
        strcpy(tmpname, DialogParams.HardDisc.szHardDiscImage);
        if( SDLGui_FileSelect(tmpname, NULL) )
        {
          strcpy(DialogParams.HardDisc.szHardDiscImage, tmpname);
          if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          {
            File_ShrinkName(dlgnamehdimg, tmpname, discdlg[DISCDLG_DISCHDIMG].w);
          }
          else
          {
            dlgnamehdimg[0] = 0;
          }
        }
        break;
    }
  }
  while(but!=DISCDLG_EXIT && !bQuitProgram);

  /* Read values from dialog */
  DialogParams.DiscImage.bAutoInsertDiscB = (discdlg[DISCDLG_AUTOB].state & SG_SELECTED);
  DialogParams.HardDisc.bBootFromHardDisc = (discdlg[DISCDLG_BOOTHD].state & SG_SELECTED);

  Memory_Free(zip_path);
}


/*-----------------------------------------------------------------------*/
/*
  Show and process the TOS/GEM dialog.
*/
void Dialog_TosGemDlg(void)
{
  char tmpname[MAX_FILENAME_LENGTH];
  char dlgromname[35];
  int but;
  int i;

  SDLGui_CenterDlg(tosgemdlg);
  File_ShrinkName(dlgromname, DialogParams.TOSGEM.szTOSImageFileName, 34);
  tosgemdlg[DLGTOSGEM_ROMNAME].txt = dlgromname;

  if( DialogParams.TOSGEM.bUseExtGEMResolutions )
    tosgemdlg[DLGTOSGEM_GEMRES].state |= SG_SELECTED;
   else
    tosgemdlg[DLGTOSGEM_GEMRES].state &= ~SG_SELECTED;

  for(i=0; i<3; i++)
  {
    tosgemdlg[DLGTOSGEM_RES640 + i].state &= ~SG_SELECTED;
    tosgemdlg[DLGTOSGEM_BPP1 + i].state &= ~SG_SELECTED;
  }
  tosgemdlg[DLGTOSGEM_RES640+DialogParams.TOSGEM.nGEMResolution-GEMRES_640x480].state |= SG_SELECTED;
  tosgemdlg[DLGTOSGEM_BPP1+DialogParams.TOSGEM.nGEMColours-GEMCOLOUR_2].state |= SG_SELECTED;

  do
  {
    but = SDLGui_DoDialog(tosgemdlg);
    switch( but )
    {
      case DLGTOSGEM_ROMBROWSE:
        strcpy(tmpname, DialogParams.TOSGEM.szTOSImageFileName);
        if(tmpname[0]=='.' && tmpname[1]=='/')  /* Is it in the actual working directory? */
        {
          getcwd(tmpname, MAX_FILENAME_LENGTH);
          File_AddSlashToEndFileName(tmpname);
          strcat(tmpname, &DialogParams.TOSGEM.szTOSImageFileName[2]);
        }
        if( SDLGui_FileSelect(tmpname, NULL) )        /* Show and process the file selection dlg */
        {
          strcpy(DialogParams.TOSGEM.szTOSImageFileName, tmpname);
          File_ShrinkName(dlgromname, DialogParams.TOSGEM.szTOSImageFileName, 34);
        }
        Screen_SetFullUpdate();
        Screen_Draw();
        break;
    }
  }
  while(but!=DLGTOSGEM_EXIT && !bQuitProgram);

  DialogParams.TOSGEM.bUseExtGEMResolutions = (tosgemdlg[DLGTOSGEM_GEMRES].state & SG_SELECTED);
  for(i=0; i<3; i++)
  {
    if(tosgemdlg[DLGTOSGEM_RES640 + i].state & SG_SELECTED)
      DialogParams.TOSGEM.nGEMResolution = GEMRES_640x480 + i;
    if(tosgemdlg[DLGTOSGEM_BPP1 + i].state & SG_SELECTED)
      DialogParams.TOSGEM.nGEMColours = GEMCOLOUR_2 + i;
  }

}


/*-----------------------------------------------------------------------*/
/*
  Show and process the screen dialog.
*/
void Dialog_ScreenDlg(void)
{
  int but, i;

  SDLGui_CenterDlg(screendlg);

  /* Set up dialog from actual values: */

  if( DialogParams.Screen.bFullScreen )
    screendlg[DLGSCRN_FULLSCRN].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_FULLSCRN].state &= ~SG_SELECTED;

  if( DialogParams.Screen.bInterlacedScreen )
    screendlg[DLGSCRN_INTERLACE].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_INTERLACE].state &= ~SG_SELECTED;

  if( DialogParams.Screen.bFrameSkip )
    screendlg[DLGSCRN_FRAMESKIP].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_FRAMESKIP].state &= ~SG_SELECTED;

  if( DialogParams.Screen.bAllowOverscan )
    screendlg[DLGSCRN_OVERSCAN].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_OVERSCAN].state &= ~SG_SELECTED;


  if( DialogParams.Screen.bUseHighRes )
  {
    screendlg[DLGSCRN_COLOR].state &= ~SG_SELECTED;
    screendlg[DLGSCRN_MONO].state |= SG_SELECTED;
  }
  else
  {
    screendlg[DLGSCRN_COLOR].state |= SG_SELECTED;
    screendlg[DLGSCRN_MONO].state &= ~SG_SELECTED;
  }

  for(i=0; i<2; i++)
    screendlg[DLGSCRN_LOW320 + i].state &= ~SG_SELECTED;

  if(DialogParams.Screen.ChosenDisplayMode <= DISPLAYMODE_16COL_FULL)
  {
    screendlg[DLGSCRN_8BPP].state |= SG_SELECTED;
    screendlg[DLGSCRN_LOW320 + DialogParams.Screen.ChosenDisplayMode].state |= SG_SELECTED;
  }
  else
  {
    screendlg[DLGSCRN_8BPP].state &= ~SG_SELECTED;
    screendlg[DLGSCRN_LOW320 + DialogParams.Screen.ChosenDisplayMode
              - DISPLAYMODE_HICOL_LOWRES].state |= SG_SELECTED;
  }

  if( DialogParams.Screen.bCaptureChange )
    screendlg[DLGSCRN_ONCHANGE].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_ONCHANGE].state &= ~SG_SELECTED;

  if( ScreenSnapShot_AreWeRecording() )
    screendlg[DLGSCRN_RECANIM].txt = "Stop recording";
  else
    screendlg[DLGSCRN_RECANIM].txt = "Record animation";

  /* The screen dialog main loop */
  do
  {
    but = SDLGui_DoDialog(screendlg);
    switch( but )
    {
      case DLGSCRN_FPSPOPUP:
        fprintf(stderr,"Sorry, popup menus don't work yet\n");
        break;
      case DLGSCRN_CAPTURE:
        Screen_SetFullUpdate();
        Screen_Draw();
        ScreenSnapShot_SaveScreen();
        break;
      case DLGSCRN_RECANIM:
        if( ScreenSnapShot_AreWeRecording() )
        {
          screendlg[DLGSCRN_RECANIM].txt = "Record animation";
          ScreenSnapShot_EndRecording();
        }
        else
        {
          screendlg[DLGSCRN_RECANIM].txt = "Stop recording";
          DialogParams.Screen.bCaptureChange = (screendlg[DLGSCRN_ONCHANGE].state & SG_SELECTED);
          ScreenSnapShot_BeginRecording(DialogParams.Screen.bCaptureChange, 25);
        }
        break;
    }
  }
  while( but!=DLGSCRN_EXIT && !bQuitProgram );

  /* Read values from dialog */
  DialogParams.Screen.bFullScreen = (screendlg[DLGSCRN_FULLSCRN].state & SG_SELECTED);
  DialogParams.Screen.bInterlacedScreen = (screendlg[DLGSCRN_INTERLACE].state & SG_SELECTED);
  DialogParams.Screen.bFrameSkip = (screendlg[DLGSCRN_FRAMESKIP].state & SG_SELECTED);
  DialogParams.Screen.bAllowOverscan = (screendlg[DLGSCRN_OVERSCAN].state & SG_SELECTED);
  DialogParams.Screen.bUseHighRes = (screendlg[DLGSCRN_MONO].state & SG_SELECTED);
  DialogParams.Screen.bCaptureChange = (screendlg[DLGSCRN_ONCHANGE].state & SG_SELECTED);

  for(i=0; i<2; i++)
  {
    if(screendlg[DLGSCRN_LOW320 + i].state & SG_SELECTED)
    {
      DialogParams.Screen.ChosenDisplayMode = DISPLAYMODE_16COL_LOWRES + i
        + ((screendlg[DLGSCRN_8BPP].state&SG_SELECTED) ? 0 : DISPLAYMODE_HICOL_LOWRES);
      break;
    }
  }

}


/*-----------------------------------------------------------------------*/
/*
  Show and process the sound dialog.
*/
void Dialog_SoundDlg(void)
{
  int but;

  SDLGui_CenterDlg(sounddlg);

  /* Set up dialog from actual values: */

  if( DialogParams.Sound.bEnableSound )
    sounddlg[DLGSOUND_ENABLE].state |= SG_SELECTED;
  else
    sounddlg[DLGSOUND_ENABLE].state &= ~SG_SELECTED;

  sounddlg[DLGSOUND_LOW].state &= ~SG_SELECTED;
  sounddlg[DLGSOUND_MEDIUM].state &= ~SG_SELECTED;
  sounddlg[DLGSOUND_HIGH].state &= ~SG_SELECTED;
  if( DialogParams.Sound.nPlaybackQuality==PLAYBACK_LOW )
    sounddlg[DLGSOUND_LOW].state |= SG_SELECTED;
  else if( DialogParams.Sound.nPlaybackQuality==PLAYBACK_MEDIUM )
    sounddlg[DLGSOUND_MEDIUM].state |= SG_SELECTED;
  else
    sounddlg[DLGSOUND_HIGH].state |= SG_SELECTED;

  if( Sound_AreWeRecording() )
    sounddlg[DLGSOUND_RECORD].txt = "Stop recording";
  else
    sounddlg[DLGSOUND_RECORD].txt = "Record sound";

  /* The sound dialog main loop */
  do
  {
    but = SDLGui_DoDialog(sounddlg);
    if(but == DLGSOUND_RECORD)
    {
      if(Sound_AreWeRecording())
      {
        sounddlg[DLGSOUND_RECORD].txt = "Record sound";
        Sound_EndRecording();
      }
      else
      {
        sounddlg[DLGSOUND_RECORD].txt = "Stop recording";
        if(sounddlg[DLGSOUND_YM].state & SG_SELECTED)
        {
          strcpy(DialogParams.Sound.szYMCaptureFileName, "hatari.ym");
          Sound_BeginRecording("hatari.ym");
        }
        else
        {
          Sound_BeginRecording("hatari.wav");
        }
      }
    }
  }
  while( but!=DLGSOUND_EXIT && !bQuitProgram );

  /* Read values from dialog */
  DialogParams.Sound.bEnableSound = (sounddlg[DLGSOUND_ENABLE].state & SG_SELECTED);
  if( sounddlg[DLGSOUND_LOW].state & SG_SELECTED )
    DialogParams.Sound.nPlaybackQuality = PLAYBACK_LOW;
  else if( sounddlg[DLGSOUND_MEDIUM].state & SG_SELECTED )
    DialogParams.Sound.nPlaybackQuality = PLAYBACK_MEDIUM;
  else
    DialogParams.Sound.nPlaybackQuality = PLAYBACK_HIGH;

}


/*-----------------------------------------------------------------------*/
/*
  Show and process the memory dialog.
*/
void Dialog_MemDlg(void)
{
  int but;

  SDLGui_CenterDlg(memorydlg);

  memorydlg[DLGMEM_512KB].state &= ~SG_SELECTED;
  memorydlg[DLGMEM_1MB].state &= ~SG_SELECTED;
  memorydlg[DLGMEM_2MB].state &= ~SG_SELECTED;
  memorydlg[DLGMEM_4MB].state &= ~SG_SELECTED;
  if( DialogParams.Memory.nMemorySize == MEMORY_SIZE_512Kb )
    memorydlg[DLGMEM_512KB].state |= SG_SELECTED;
  else if( DialogParams.Memory.nMemorySize == MEMORY_SIZE_1Mb )
    memorydlg[DLGMEM_1MB].state |= SG_SELECTED;
  else if( DialogParams.Memory.nMemorySize == MEMORY_SIZE_2Mb )
    memorydlg[DLGMEM_2MB].state |= SG_SELECTED;
  else
    memorydlg[DLGMEM_4MB].state |= SG_SELECTED;

  do
  {
    but = SDLGui_DoDialog(memorydlg);
  }
  while( but!=DLGMEM_EXIT && !bQuitProgram );

  if( memorydlg[DLGMEM_512KB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = MEMORY_SIZE_512Kb;
  else if( memorydlg[DLGMEM_1MB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = MEMORY_SIZE_1Mb;
  else if( memorydlg[DLGMEM_2MB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = MEMORY_SIZE_2Mb;
  else
    DialogParams.Memory.nMemorySize = MEMORY_SIZE_4Mb;

}


/*-----------------------------------------------------------------------*/
/*
  Show and process the joystick dialog.
*/
void Dialog_JoyDlg(void)
{
  int but;

  SDLGui_CenterDlg(joystickdlg);

  /* Set up dialog from actual values: */

  if( DialogParams.Joysticks.Joy[1].bCursorEmulation )
    joystickdlg[DLGJOY_J1CURSOR].state |= SG_SELECTED;
  else
    joystickdlg[DLGJOY_J1CURSOR].state &= ~SG_SELECTED;

  if( DialogParams.Joysticks.Joy[1].bEnableAutoFire )
    joystickdlg[DLGJOY_J1AUTOFIRE].state |= SG_SELECTED;
  else
    joystickdlg[DLGJOY_J1AUTOFIRE].state &= ~SG_SELECTED;

  if( DialogParams.Joysticks.Joy[0].bCursorEmulation )
    joystickdlg[DLGJOY_J0CURSOR].state |= SG_SELECTED;
  else
    joystickdlg[DLGJOY_J0CURSOR].state &= ~SG_SELECTED;

  if( DialogParams.Joysticks.Joy[0].bEnableAutoFire )
    joystickdlg[DLGJOY_J0AUTOFIRE].state |= SG_SELECTED;
  else
    joystickdlg[DLGJOY_J0AUTOFIRE].state &= ~SG_SELECTED;

  do
  {
    but = SDLGui_DoDialog(joystickdlg);
  }
  while( but!=DLGJOY_EXIT && !bQuitProgram );

  /* Read values from dialog */
  DialogParams.Joysticks.Joy[1].bCursorEmulation = (joystickdlg[DLGJOY_J1CURSOR].state & SG_SELECTED);
  DialogParams.Joysticks.Joy[1].bEnableAutoFire = (joystickdlg[DLGJOY_J1AUTOFIRE].state & SG_SELECTED);
  DialogParams.Joysticks.Joy[0].bCursorEmulation = (joystickdlg[DLGJOY_J0CURSOR].state & SG_SELECTED);
  DialogParams.Joysticks.Joy[0].bEnableAutoFire = (joystickdlg[DLGJOY_J0AUTOFIRE].state & SG_SELECTED);
}


/*-----------------------------------------------------------------------*/
/*
  Show and process the "System" dialog.
*/
void Dialog_SystemDlg(void)
{
  int i;

  SDLGui_CenterDlg(systemdlg);

  /* Set up dialog from actual values: */

  for(i=DLGSYS_68000; i<=DLGSYS_68040; i++)
  {
    systemdlg[i].state &= ~SG_SELECTED;
  }

  systemdlg[DLGSYS_68000+DialogParams.System.nCpuLevel].state |= SG_SELECTED;

  if( DialogParams.System.bCompatibleCpu )
    systemdlg[DLGSYS_PREFETCH].state |= SG_SELECTED;
  else
    systemdlg[DLGSYS_PREFETCH].state &= ~SG_SELECTED;

  if( DialogParams.System.bBlitter )
    systemdlg[DLGSYS_BLITTER].state |= SG_SELECTED;
  else
    systemdlg[DLGSYS_BLITTER].state &= ~SG_SELECTED;

  /* Show the dialog: */
  SDLGui_DoDialog(systemdlg);

  /* Read values from dialog: */

  for(i=DLGSYS_68000; i<=DLGSYS_68040; i++)
  {
    if( systemdlg[i].state&SG_SELECTED )
    {
      DialogParams.System.nCpuLevel = i-DLGSYS_68000;
      break;
    }
  }

  DialogParams.System.bCompatibleCpu = (systemdlg[DLGSYS_PREFETCH].state & SG_SELECTED);
  DialogParams.System.bBlitter = ( systemdlg[DLGSYS_BLITTER].state & SG_SELECTED );
}


/*-----------------------------------------------------------------------*/
/*
  Show and process the "Keyboard" dialog.
*/
void Dialog_KeyboardDlg(void)
{
  int i;

  SDLGui_CenterDlg(keyboarddlg);

  /* Set up dialog from actual values: */
  if(DialogParams.Keyboard.nKeymapType == KEYMAP_SYMBOLIC)
  {
    keyboarddlg[DLGKEY_SYMBOLIC].state |= SG_SELECTED;
    keyboarddlg[DLGKEY_SCANCODE].state &= ~SG_SELECTED;
  }
  else
  {
    keyboarddlg[DLGKEY_SYMBOLIC].state &= ~SG_SELECTED;
    keyboarddlg[DLGKEY_SCANCODE].state |= SG_SELECTED;
  }

  /* Show the dialog: */
  SDLGui_DoDialog(keyboarddlg);

  /* Read values from dialog: */
  if(keyboarddlg[DLGKEY_SYMBOLIC].state & SG_SELECTED)
    DialogParams.Keyboard.nKeymapType = KEYMAP_SYMBOLIC;
  else
    DialogParams.Keyboard.nKeymapType = KEYMAP_SCANCODE;
}



/*-----------------------------------------------------------------------*/
/*
  This functions sets up the actual font and then displays the main dialog.
*/
int Dialog_MainDlg(BOOL *bReset)
{
  int retbut;

  if(SDLGui_PrepareFont())
    return FALSE;

  SDLGui_CenterDlg(maindlg);
  SDL_ShowCursor(SDL_ENABLE);

  maindlg[MAINDLG_NORESET].state |= SG_SELECTED;
  maindlg[MAINDLG_RESET].state &= ~SG_SELECTED;

  do
  {
    retbut = SDLGui_DoDialog(maindlg);
    switch(retbut)
    {
      case MAINDLG_ABOUT:
        SDLGui_CenterDlg(aboutdlg);
        SDLGui_DoDialog(aboutdlg);
        break;
      case MAINDLG_DISCS:
        Dialog_DiscDlg();
        break;
      case MAINDLG_TOSGEM:
        Dialog_TosGemDlg();
        break;
      case MAINDLG_SCREEN:
        Dialog_ScreenDlg();
        break;
      case MAINDLG_SOUND:
        Dialog_SoundDlg();
        break;
      case MAINDLG_CPU:
        Dialog_SystemDlg();
        break;
      case MAINDLG_MEMORY:
        Dialog_MemDlg();
        break;
      case MAINDLG_JOY:
        Dialog_JoyDlg();
        break;
      case MAINDLG_KEYBD:
        SDLGui_CenterDlg(keyboarddlg);
        Dialog_KeyboardDlg();
        break;
      case MAINDLG_DEVICES:
        SDLGui_CenterDlg(devicedlg);
        SDLGui_DoDialog(devicedlg);
        break;
      case MAINDLG_QUIT:
        bQuitProgram = TRUE;
        break;
    }
    Screen_SetFullUpdate();
    Screen_Draw();
  }
  while(retbut!=MAINDLG_OK && retbut!=MAINDLG_CANCEL && !bQuitProgram);

  SDL_ShowCursor(SDL_DISABLE);

  if( maindlg[MAINDLG_RESET].state & SG_SELECTED )
    *bReset = TRUE;
  else
    *bReset = FALSE;

  return(retbut==MAINDLG_OK);
}


/*-----------------------------------------------------------------------*/
/*
  Open Property sheet Options dialog
  Return TRUE if user choses OK, or FALSE if cancel!
*/
BOOL Dialog_DoProperty(void)
{
  BOOL bOKDialog;  /* Did user 'OK' dialog? */
  BOOL bForceReset;

  Main_PauseEmulation();

  /* Copy details to DialogParams (this is so can restore if 'Cancel' dialog) */
  ConfigureParams.Screen.bFullScreen = bInFullScreen;
  DialogParams = ConfigureParams;

  bSaveMemoryState = FALSE;
  bRestoreMemoryState = FALSE;
  bForceReset = FALSE;

  bOKDialog = Dialog_MainDlg(&bForceReset);

  /* Copy details to configuration, and ask user if wishes to reset */
  if (bOKDialog)
    Dialog_CopyDialogParamsToConfiguration(bForceReset);
  /* Did want to save/restore memory save? If did, need to re-enter emulation mode so can save in 'safe-zone' */
  if (bSaveMemoryState || bRestoreMemoryState) {
    /* Back into emulation mode, when next VBL occurs state will be safed - otherwise registers are unknown */
    /*FM  View_ToggleWindowsMouse(MOUSE_ST);*/
  }

  Main_UnPauseEmulation();

  return(bOKDialog);
}

