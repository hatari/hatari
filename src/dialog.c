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
  { SGBOX, 0, 0, 0,0, 36,18, NULL },
  { SGTEXT, 0, 0, 10,1, 16,1, "Hatari main menu" },
  { SGBUTTON, 0, 0, 4,4, 12,1, "About" },
  { SGBUTTON, 0, 0, 4,6, 12,1, "Discs" },
  { SGBUTTON, 0, 0, 4,8, 12,1, "TOS/GEM" },
  { SGBUTTON, 0, 0, 4,10, 12,1, "Screen" },
  { SGBUTTON, 0, 0, 4,12, 12,1, "Sound" },
  { SGBUTTON, 0, 0, 20,4, 12,1, "CPU" },
  { SGBUTTON, 0, 0, 20,6, 12,1, "Memory" },
  { SGBUTTON, 0, 0, 20,8, 12,1, "Joysticks" },
  { SGBUTTON, 0, 0, 20,10, 12,1, "Keyboard" },
  { SGBUTTON, 0, 0, 20,12, 12,1, "Devices" },
  { SGBUTTON, 0, 0, 7,16, 8,1, "Okay" },
  { SGBUTTON, 0, 0, 21,16, 8,1, "Cancel" },
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
#define DISCDLG_BOOTHD      16
#define DISCDLG_DISCC       18
#define DISCDLG_BROWSEC     19
#define DISCDLG_EXIT        20
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
  { SGBUTTON, 0, 0, 20,10, 18,1, "Create blank image" },
  { SGBOX, 0, 0, 1,13, 38,9, NULL },
  { SGTEXT, 0, 0, 14,13, 13,1, "Hard discs" },
  { SGCHECKBOX, 0, 0, 2,15, 14,1, "Boot from HD" },
  { SGTEXT, 0, 0, 2,17, 2,1, "C:" },
  { SGTEXT, 0, 0, 5,17, 24,1, NULL },
  { SGBUTTON, 0, 0, 32,17, 6,1, "Browse" },
  { SGBUTTON, 0, 0, 10,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The TOS/GEM dialog: */
SGOBJ tosgemdlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,19, NULL },
  { SGTEXT, 0, 0, 14,1, 13,1, "TOS/GEM setup" },
  { SGTEXT, 0, 0, 2,4, 10,1, "ROM image:" },
  { SGTEXT, 0, 0, 2,6, 34,1, "/path/to/tos.img" },
  { SGBUTTON, 0, 0, 30,4, 8,1, "Browse" },
  { SGTEXT, 0, 0, 2,10, 4,1, "GEM:" },
  { SGCHECKBOX, 0, 0, 2,12, 25,1, "Use extended resolution" },
  { SGTEXT, 0, 0, 2,14, 11,1, "Resolution:" },
  { SGPOPUP, 0, 0, 14,14, 10,1, "800x600" },
  { SGBUTTON, 0, 0, 10,17, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The screen dialog: */
SGOBJ screendlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGBOX, 0, 0, 1,1, 38,11, NULL },
  { SGTEXT, 0, 0, 13,2, 14,1, "Screen options" },
  { SGTEXT, 0, 0, 5,4, 13,1, "Display mode:" },
  { SGPOPUP, 0, 0, 20,4, 18,1, "Hi-Color, Lo-Res" },
  { SGCHECKBOX, 0, 0, 5,6, 12,1, "Fullscreen" },
  { SGCHECKBOX, 0, 0, 5,7, 23,1, "Interlaced mode (in fullscreen)" },
  { SGCHECKBOX, 0, 0, 5,8, 10,1, "Frame skip" },
  /*{ SGCHECKBOX, 0, 0, 22,8, 13,1, "Use borders" },*/
  /*{ SGCHECKBOX, 0, 0, 5,9, 13,1, "Sync to retrace (in fullscreen)" },*/
  { SGTEXT, 0, 0, 5,10, 8,1, "Monitor:" },
  { SGRADIOBUT, 0, 0, 16,10, 7,1, "Color" },
  { SGRADIOBUT, 0, 0, 24,10, 6,1, "Mono" },
  { SGBOX, 0, 0, 1,13, 38,9, NULL },
  { SGTEXT, 0, 0, 13,14, 14,1, "Screen capture" },
  { SGCHECKBOX, 0, 0, 5,16, 12,1, "Only when display changes" },
  { SGTEXT, 0, 0, 5,18, 18,1, "Frames per second:" },
  { SGPOPUP, 0, 0, 24,18, 3,1, "1" },
  { SGBUTTON, 0, 0, 3,20, 16,1, "Capture screen" },
  { SGBUTTON, 0, 0, 20,20, 18,1, "Record animation" },
  { SGBUTTON, 0, 0, 10,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The sound dialog: */
SGOBJ sounddlg[] =
{
  { SGBOX, 0, 0, 0,0, 38,24, NULL },
  { SGBOX, 0, 0, 1,1, 36,11, NULL },
  { SGTEXT, 0, 0, 13,2, 13,1, "Sound options" },
  { SGCHECKBOX, 0, 1, 12,4, 14,1, "Enable sound" },
  { SGTEXT, 0, 0, 11,6, 14,1, "Playback quality:" },
  { SGRADIOBUT, 0, 0, 12,8, 15,1, "Low (11kHz)" },
  { SGRADIOBUT, 0, 0, 12,9, 19,1, "Medium (22kHz)" },
  { SGRADIOBUT, 0, 0, 12,10, 14,1, "High (44kHz)" },
  { SGBOX, 0, 0, 1,13, 36,7, NULL },
  { SGTEXT, 0, 0, 13,14, 14,1, "Capture YM/WAV" },
  { SGBUTTON, 0, 0, 9,18, 8,1, "Record" },
  { SGBUTTON, 0, 0, 23,18, 8,1, "Stop" },
  { SGBUTTON, 0, 0, 10,22, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The cpu dialog: */
SGOBJ cpudlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGTEXT, 0, 0, 14,2, 11,1, "CPU options" },
  { SGTEXT, 0, 0, 8,12, 13,1, "Sorry, not yet supported." },
  { SGBUTTON, 0, 0, 10,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The memory dialog: */
SGOBJ memorydlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,19, NULL },
  { SGBOX, 0, 0, 1,1, 38,7, NULL },
  { SGTEXT, 0, 0, 15,2, 12,1, "Memory setup" },
  { SGTEXT, 0, 0, 4,4, 12,1, "ST-RAM size:" },
  { SGRADIOBUT, 0, 0, 19,4, 8,1, "512 kB" },
  { SGRADIOBUT, 0, 0, 30,4, 6,1, "1 MB" },
  { SGRADIOBUT, 0, 0, 19,6, 6,1, "2 MB" },
  { SGRADIOBUT, 0, 0, 30,6, 6,1, "4 MB" },
  { SGBOX, 0, 0, 1,9, 38,7, NULL },
  { SGTEXT, 0, 0, 12,10, 17,1, "Memory state save" },
  { SGTEXT, 0, 0, 2,12, 28,1, "/path/to/image" },
  { SGBUTTON, 0, 0, 32,12, 6,1, "Browse" },
  { SGBUTTON, 0, 0, 8,14, 10,1, "Save" },
  { SGBUTTON, 0, 0, 22,14, 10,1, "Restore" },
  { SGBUTTON, 0, 0, 10,17, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The joysticks dialog: */
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
SGOBJ keyboarddlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGTEXT, 0, 0, 13,2, 14,1, "Keyboard setup" },
  { SGTEXT, 0, 0, 8,12, 13,1, "Sorry, not yet supported." },
  { SGBUTTON, 0, 0, 10,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/* The devices dialog: */
SGOBJ devicedlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGTEXT, 0, 0, 13,2, 13,1, "Devices setup" },
  { SGTEXT, 0, 0, 8,12, 13,1, "Sorry, not yet supported." },
  { SGBUTTON, 0, 0, 10,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
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

  Main_PauseEmulation();

  /* Copy details to DialogParams (this is so can restore if 'Cancel' dialog) */
  ConfigureParams.Screen.bFullScreen = bInFullScreen;
  DialogParams = ConfigureParams;

  bSaveMemoryState = FALSE;
  bRestoreMemoryState = FALSE;

  bOKDialog = Dialog_MainDlg();

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


/*-----------------------------------------------------------------------*/
/*
  Show and process the disc image dialog.
*/
void Dialog_DiscDlg(void)
{
  int but;
  char tmpdiscname[MAX_FILENAME_LENGTH];
  char dlgnamea[27], dlgnameb[27], dlgdiscdir[29], dlgnamec[27];

  SDLGui_CenterDlg(discdlg);

  /* Set up dialog to actual values: */

  /* Disc name A: */
  if( EmulationDrives[0].bDiscInserted )
    File_ShrinkName(dlgnamea, EmulationDrives[0].szFileName, 26);
  else
    dlgnamea[0] = 0;
  discdlg[DISCDLG_DISCA].txt = dlgnamea;
  /* Disc name B: */
  if( EmulationDrives[1].bDiscInserted )
    File_ShrinkName(dlgnameb, EmulationDrives[1].szFileName, 26);
  else
    dlgnameb[0] = 0;
  discdlg[DISCDLG_DISCB].txt = dlgnameb;
  /* Default image directory: */
  File_ShrinkName(dlgdiscdir, DialogParams.DiscImage.szDiscImageDirectory, 28);
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
  /* Hard disc directory: */
  if( DialogParams.HardDisc.nDriveList!=DRIVELIST_NONE )
    File_ShrinkName(dlgnamec, DialogParams.HardDisc.szHardDiscDirectories[0], 24);
  else
    dlgnamec[0] = 0;
  discdlg[DISCDLG_DISCC].txt = dlgnamec;

  /* Draw and process the dialog */
  do
  {
    but = SDLGui_DoDialog(discdlg);
    switch(but)
    {
      case DISCDLG_BROWSEA:                       /* Choose a new disc A: */
        if( EmulationDrives[0].bDiscInserted )
          strcpy(tmpdiscname, EmulationDrives[0].szFileName);
         else
          strcpy(tmpdiscname, DialogParams.DiscImage.szDiscImageDirectory);
        if( SDLGui_FileSelect(tmpdiscname) )
        {
          if( File_Exists(tmpdiscname) )
          {
            Floppy_InsertDiscIntoDrive(0, tmpdiscname); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            File_ShrinkName(dlgnamea, tmpdiscname, 26);
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
          strcpy(tmpdiscname, EmulationDrives[1].szFileName);
         else
          strcpy(tmpdiscname, DialogParams.DiscImage.szDiscImageDirectory);
        if( SDLGui_FileSelect(tmpdiscname) )
        {
          if( File_Exists(tmpdiscname) )
          {
            Floppy_InsertDiscIntoDrive(1, tmpdiscname); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            File_ShrinkName(dlgnameb, tmpdiscname, 26);
          }
          else
          {
            Floppy_EjectDiscFromDrive(1, FALSE); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            dlgnameb[0] = 0;
          }
        }
        break;
      case DISCDLG_IMGDIR:
        fprintf(stderr,"Sorry, chosing default disc directory not yet supported\n");
        break;
      case DISCDLG_CREATEIMG:
        fprintf(stderr,"Sorry, creating disc images not yet supported\n");
        break;
      case DISCDLG_BROWSEC:
        fprintf(stderr,"Sorry, chosing a hard disc is not yet supported\n");
        break;
    }
  }
  while(but!=DISCDLG_EXIT && !bQuitProgram);

  /* Read values from dialog */
  DialogParams.DiscImage.bAutoInsertDiscB = (discdlg[DISCDLG_AUTOB].state & SG_SELECTED);
  DialogParams.HardDisc.bBootFromHardDisc = (discdlg[DISCDLG_BOOTHD].state & SG_SELECTED);
}


/*-----------------------------------------------------------------------*/
/*
  This functions sets up the actual font and then displays the main dialog.
*/
int Dialog_MainDlg(void)
{
  int retbut;

  SDLGui_PrepareFont();
  SDLGui_CenterDlg(maindlg);
  SDL_ShowCursor(SDL_ENABLE);

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
        SDLGui_CenterDlg(tosgemdlg);
        SDLGui_DoDialog(tosgemdlg);
        break;
      case MAINDLG_SCREEN:
        SDLGui_CenterDlg(screendlg);
        SDLGui_DoDialog(screendlg);
        break;
      case MAINDLG_SOUND:
        SDLGui_CenterDlg(sounddlg);
        SDLGui_DoDialog(sounddlg);
        break;
      case MAINDLG_CPU:
        SDLGui_CenterDlg(cpudlg);
        SDLGui_DoDialog(cpudlg);
        break;
      case MAINDLG_MEMORY:
        SDLGui_CenterDlg(memorydlg);
        SDLGui_DoDialog(memorydlg);
        break;
      case MAINDLG_JOY:
        SDLGui_CenterDlg(joystickdlg);
        SDLGui_DoDialog(joystickdlg);
        break;
      case MAINDLG_KEYBD:
        SDLGui_CenterDlg(keyboarddlg);
        SDLGui_DoDialog(keyboarddlg);
        break;
      case MAINDLG_DEVICES:
        SDLGui_CenterDlg(devicedlg);
        SDLGui_DoDialog(devicedlg);
        break;
    }
    Screen_SetFullUpdate();
    Screen_Draw();
  }
  while(retbut!=MAINDLG_OK && retbut!=MAINDLG_CANCEL && !bQuitProgram);

  SDL_ShowCursor(SDL_DISABLE);

  return(retbut==MAINDLG_OK);
}
