/*
  Hatari - dialog.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This is normal 'C' code to handle our options dialog. We keep all our configuration details
  in a variable 'ConfigureParams'. When we open our dialog we copy this and then when we 'OK'
  or 'Cancel' the dialog we can compare and makes the necessary changes.
*/
char Dialog_rcsid[] = "Hatari $Id: dialog.c,v 1.40 2004-07-05 20:06:20 thothy Exp $";

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


CNF_PARAMS DialogParams;   /* List of configuration for dialogs (so the user can also choose 'Cancel') */


/*-----------------------------------------------------------------------*/
/*
  Check if need to warn user that changes will take place after reset
  Return TRUE if wants to reset
*/
static BOOL Dialog_DoNeedReset(void)
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
static void Dialog_CopyDialogParamsToConfiguration(BOOL bForceReset)
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
     || (strcmp(DialogParams.RS232.szOutFileName, ConfigureParams.RS232.szOutFileName))
     || (strcmp(DialogParams.RS232.szInFileName, ConfigureParams.RS232.szInFileName)) )
    RS232_UnInit();

  /* Did stop sound? Or change playback Hz. If so, also stop sound recording */
  if (!DialogParams.Sound.bEnableSound || DialogParams.Sound.nPlaybackQuality != ConfigureParams.Sound.nPlaybackQuality)
  {
    if (Sound_AreWeRecording())
      Sound_EndRecording();
    Audio_UnInit();
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

  /* Copy details to global, if we reset copy them all */
  Dialog_CopyDetailsFromConfiguration(NeedReset);

  /* Set keyboard remap file */
  if(ConfigureParams.Keyboard.nKeymapType == KEYMAP_LOADED)
    Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);

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

  /* Restart audio sub system if necessary: */
  if (ConfigureParams.Sound.bEnableSound && !bSoundWorking)
  {
    Audio_Init();
  }

  /* Re-initialize the RS232 emulation: */
  if (ConfigureParams.RS232.bEnableRS232 && !bConnectedRS232)
  {
    RS232_Init();
  }

  /* Do we need to perform reset? */
  if (NeedReset)
  {
    Reset_Cold();
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
  /* Set resolution change */
  if (bReset)
  {
    bUseVDIRes = ConfigureParams.TOSGEM.bUseExtGEMResolutions;
    bUseHighRes = (!bUseVDIRes && ConfigureParams.Screen.bUseHighRes)
                   || (bUseVDIRes && ConfigureParams.TOSGEM.nGEMColours==GEMCOLOUR_2);
    VDI_SetResolution(ConfigureParams.TOSGEM.nGEMResolution, ConfigureParams.TOSGEM.nGEMColours);
  }

  /* Set playback frequency */
  if (ConfigureParams.Sound.bEnableSound)
    Audio_SetOutputAudioFreq(ConfigureParams.Sound.nPlaybackQuality);

  /* Remove slashes, etc.. from names */
  File_CleanFileName(ConfigureParams.TOSGEM.szTOSImageFileName);
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

  bForceReset = FALSE;

  bOKDialog = Dialog_MainDlg(&bForceReset);

  /* Copy details to configuration, and ask user if wishes to reset */
  if (bOKDialog)
    Dialog_CopyDialogParamsToConfiguration(bForceReset);

  Main_UnPauseEmulation();

  if (bQuitProgram)
    set_special(SPCFLAG_BRK);           /* Assure that CPU core shuts down */

  return(bOKDialog);
}
