/*
  Hatari

  This is normal 'C' code to handle our options dialog. We keep all our configuration details
  in a variable 'ConfigureParams'. When we open our dialog we copy this and then when we 'OK'
  or 'Cancel' the dialog we can compare and makes the necessary changes.
*/

#include "main.h"
#include "configuration.h"
#include "audio.h"
#include "debug.h"
#include "dialog.h"
#include "file.h"
#include "floppy.h"
#include "reset.h"
#include "joy.h"
#include "keymap.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "printer.h"
#include "rs232.h"
#include "screen.h"
#include "sound.h"
#include "tos.h"
#include "vdi.h"
#include "video.h"
#include "view.h"
#include "sdlgui.h"


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
#define MAINDLG_OK       12
#define MAINDLG_CANCEL   13
SGOBJ maindlg[] =
{
  { SGBOX, 0, 0,0, 36,18, NULL },
  { SGTEXT, 0, 10,1, 16,1, "Hatari main menu" },
  { SGBUTTON, 0, 4,4, 12,1, "About" },
  { SGBUTTON, 0, 4,6, 12,1, "Discs" },
  { SGBUTTON, 0, 4,8, 12,1, "TOS/GEM" },
  { SGBUTTON, 0, 4,10, 12,1, "Screen" },
  { SGBUTTON, 0, 4,12, 12,1, "Sound" },
  { SGBUTTON, 0, 20,4, 12,1, "CPU" },
  { SGBUTTON, 0, 20,6, 12,1, "Memory" },
  { SGBUTTON, 0, 20,8, 12,1, "Joysticks" },
  { SGBUTTON, 0, 20,10, 12,1, "Keyboard" },
  { SGBUTTON, 0, 20,12, 12,1, "Devices" },
  { SGBUTTON, 0, 7,16, 8,1, "Okay" },
  { SGBUTTON, 0, 21,16, 8,1, "Cancel" },
  { -1, 0, 0,0, 0,0, NULL }
};


/* The "About"-dialog: */
SGOBJ aboutdlg[] =
{
  { SGBOX, 0, 0,0, 40,25, NULL },
  { SGTEXT, 0, 14,1, 12,1, PROG_NAME },
  { SGTEXT, 0, 1,3, 38,1, "Hatari has been written by:  T. Huth," },
  { SGTEXT, 0, 1,4, 38,1, "S. Marothy, S. Berndtsson, P. Bates," },
  { SGTEXT, 0, 1,5, 38,1, "B. Schmidt and many others." },
  { SGTEXT, 0, 1,6, 38,1, "Please see the docs for more info." },
  { SGTEXT, 0, 1,8, 38,1, "This program is free software; you can" },
  { SGTEXT, 0, 1,9, 38,1, "redistribute it and/or modify it under" },
  { SGTEXT, 0, 1,10, 38,1, "the terms of the GNU General Public" },
  { SGTEXT, 0, 1,11, 38,1, "License as published by the Free Soft-" },
  { SGTEXT, 0, 1,12, 38,1, "ware Foundation; either version 2 of" },
  { SGTEXT, 0, 1,13, 38,1, "the License, or (at your option) any" },
  { SGTEXT, 0, 1,14, 38,1, "later version." },
  { SGTEXT, 0, 1,16, 38,1, "This program is distributed in the" },
  { SGTEXT, 0, 1,17, 38,1, "hope that it will be useful, but" },
  { SGTEXT, 0, 1,18, 38,1, "WITHOUT ANY WARRANTY. See the GNU Ge-" },
  { SGTEXT, 0, 1,19, 38,1, "neral Public License for more details." },
  { SGBUTTON, 0, 16,23, 8,1, "Okay" },
  { -1, 0, 0,0, 0,0, NULL }
};


/* The screen dialog: */
SGOBJ screendlg[] =
{
  { SGBOX, 0, 0,0, 36,18, NULL },
  { SGTEXT, 0, 11,1, 14,1, "Screen options" },
  { SGTEXT, 0, 4,4, 25,1, "Sorry, does not work yet." },
  { SGCHECKBOX, 0, 4,6, 12,1, "Fullscreen" },
  { SGCHECKBOX, 1, 4,7, 13,1, "Use borders" },
  { SGTEXT, 0, 4,14, 8,1, "Monitor:" },
  { SGRADIOBUT, 1, 14,14, 7,1, "Color" },
  { SGRADIOBUT, 0, 22,14, 6,1, "Mono" },
  { SGBUTTON, 0, 8,16, 20,1, "Back to main menu" },
  { -1, 0, 0,0, 0,0, NULL }
};



DLG_PARAMS ConfigureParams, DialogParams;   /* List of configuration for system and dialog (so can choose 'Cancel') */



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
  if ( (ConfigureParams.TOSGEM.nGEMResolution!=DialogParams.TOSGEM.nGEMResolution)
   || (ConfigureParams.TOSGEM.nGEMColours!=DialogParams.TOSGEM.nGEMColours) )
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

  /* Do we need to warn user of that changes will only take effect after reset? */
  if (bForceReset)
    NeedReset = bForceReset;
  else
    NeedReset = Dialog_DoNeedReset();

  /* Do need to change resolution? Need if change display/overscan settings */
  /*(if switch between Colour/Mono cause reset later) */
  if(bInFullScreen)
  {
    if( (DialogParams.Screen.ChosenDisplayMode!=ConfigureParams.Screen.ChosenDisplayMode)
       || (DialogParams.Screen.Advanced.bAllowOverscan!=ConfigureParams.Screen.Advanced.bAllowOverscan) )
    {
      Screen_ReturnFromFullScreen();
      ConfigureParams.Screen.ChosenDisplayMode = DialogParams.Screen.ChosenDisplayMode;
      ConfigureParams.Screen.Advanced.bAllowOverscan = DialogParams.Screen.Advanced.bAllowOverscan;
      Screen_EnterFullScreen();
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
  if( (!DialogParams.Sound.bEnableSound) || (DialogParams.Sound.nPlaybackQuality!=ConfigureParams.Sound.nPlaybackQuality) )
  {
    if(Sound_AreWeRecording())
      Sound_EndRecording(NULL);
  }

  /* Copy details to configuration, so can be saved out or set on reset */
  ConfigureParams = DialogParams;
  /* And write to configuration now, so don't loose */
  Configuration_UnInit();

  /* Copy details to global, if we reset copy them all */
  Dialog_CopyDetailsFromConfiguration(NeedReset);
  /* Set keyboard remap file */
  Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);
  /* Set new sound playback rate */
  Audio_ReCreateSoundBuffer();
  /* Resize window if need */
  /*if(!ConfigureParams.TOSGEM.bUseExtGEMResolutions)
    View_ResizeWindowToFull();*/

  /* Do we need to perform reset? */
  if (NeedReset)
  {
    Reset_Cold();
    Main_UnPauseEmulation();
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
  if (bReset) {
    bUseVDIRes = ConfigureParams.TOSGEM.bUseExtGEMResolutions;
    bUseHighRes = ConfigureParams.Screen.bUseHighRes || (bUseVDIRes && (ConfigureParams.TOSGEM.nGEMColours==GEMCOLOUR_2));
/*FM    VDI_SetResolution(VDIModeOptions[ConfigureParams.TOSGEM.nGEMResolution],ConfigureParams.TOSGEM.nGEMColours);*/
  }
  /* Set playback frequency */
  Audio_SetOutputAudioFreq(ConfigureParams.Sound.nPlaybackQuality);

  /* Remove back-slashes, etc.. from names */
  File_CleanFileName(ConfigureParams.TOSGEM.szTOSImageFileName);
}


/*-----------------------------------------------------------------------*/
/*
  Open Property sheet Options dialog
  Return TRUE if user choses OK, or FALSE if cancel!
*/
BOOL Dialog_DoProperty(BOOL bForceReset)
{
  BOOL bOKDialog;  /* Did user 'OK' dialog? */

  /* Copy details to DialogParams (this is so can restore if 'Cancel' dialog) */
  ConfigureParams.Screen.bFullScreen = bInFullScreen;
  DialogParams = ConfigureParams;

  bSaveMemoryState = FALSE;
  bRestoreMemoryState = FALSE;

  bOKDialog = Dialog_MainDialog();

  /* Copy details to configuration, and ask user if wishes to reset */
  if (bOKDialog)
    Dialog_CopyDialogParamsToConfiguration(bForceReset);
  /* Did want to save/restore memory save? If did, need to re-enter emulation mode so can save in 'safe-zone' */
  if (bSaveMemoryState || bRestoreMemoryState) {
    /* Back into emulation mode, when next VBL occurs state will be safed - otherwise registers are unknown */
    /*FM  View_ToggleWindowsMouse(MOUSE_ST);*/
  }

  return(bOKDialog);
}


/*-----------------------------------------------------------------------*/
/*
  This functions sets up the actual font and then display the main dialog.
*/
int Dialog_MainDialog()
{
  int retbut;

  SDLGui_PrepareFont();

  SDL_ShowCursor(SDL_ENABLE);

  do
  {
    retbut = SDLGui_DoDialog(maindlg);
    switch(retbut)
    {
      case MAINDLG_ABOUT:
        if( SDLGui_DoDialog(aboutdlg)<0 )
          retbut=-1;
        break;
      case MAINDLG_DISCS:
        break;
      case MAINDLG_TOSGEM:
        break;
      case MAINDLG_SCREEN:
        SDLGui_DoDialog(screendlg);
        break;
      case MAINDLG_SOUND:
        break;
      case MAINDLG_CPU:
        break;
      case MAINDLG_MEMORY:
        break;
      case MAINDLG_JOY:
        break;
      case MAINDLG_KEYBD:
        break;
      case MAINDLG_DEVICES:
        break;
    }
    Screen_SetFullUpdate();
    Screen_Draw();
  }
  while(retbut>0 && retbut!=MAINDLG_OK && retbut!=MAINDLG_CANCEL);

  SDL_ShowCursor(SDL_DISABLE);

  return(retbut==MAINDLG_OK);
}
