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



DLG_PARAMS ConfigureParams,DialogParams;    /* List of configuration for system and dialog (so can choose 'Cancel') */
BOOL bOKDialog;                             /* Did user 'OK' dialog? */
int nLastOpenPage = 0;                      /* Last property page opened (so can re-open on this next time) */


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
BOOL Dialog_DoProperty(int StartingPage, BOOL bForceReset)
{
  /* Copy details to DialogParams (this is so can restore if 'Cancel' dialog) */
  ConfigureParams.Screen.bFullScreen = bInFullScreen;
  DialogParams = ConfigureParams;

  bOKDialog = FALSE;                // Reset OK flag
  bSaveMemoryState = FALSE;
  bRestoreMemoryState = FALSE;

  //SDLGui_DoDialog(SDLGUI_DLGMAIN);

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

