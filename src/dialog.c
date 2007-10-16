/*
  Hatari - dialog.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This is normal 'C' code to handle our options dialog. We keep all our
  configuration details in a structure called 'ConfigureParams'. When we
  open our dialog we make a backup of this structure. When the user finally
  clicks on 'OK', we can compare and makes the necessary changes.
*/
const char Dialog_rcsid[] = "Hatari $Id: dialog.c,v 1.63 2007-10-16 20:39:22 eerot Exp $";

#include "main.h"
#include "configuration.h"
#include "audio.h"
#include "dialog.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "ioMem.h"
#include "joy.h"
#include "keymap.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
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
#include "hatari-glue.h"
#if ENABLE_DSP_EMU
# include "falcon/dsp.h"
#endif


CNF_PARAMS DialogParams;   /* List of configuration for dialogs (so the user can also choose 'Cancel') */


/*-----------------------------------------------------------------------*/
/**
 * Check if need to warn user that changes will take place after reset.
 * Return TRUE if wants to reset.
 */
static BOOL Dialog_DoNeedReset(void)
{
	/* Did we change monitor type? If so, must reset */
	if (ConfigureParams.Screen.MonitorType != DialogParams.Screen.MonitorType
	    && (DialogParams.System.nMachineType == MACHINE_FALCON
	        || ConfigureParams.Screen.MonitorType == MONITOR_TYPE_MONO
	        || DialogParams.Screen.MonitorType == MONITOR_TYPE_MONO))
		return TRUE;

	/* Did change to GEM VDI display? */
	if (ConfigureParams.Screen.bUseExtVdiResolutions != DialogParams.Screen.bUseExtVdiResolutions)
		return TRUE;

	/* Did change GEM resolution or colour depth? */
	if (DialogParams.Screen.bUseExtVdiResolutions &&
	    (ConfigureParams.Screen.nVdiResolution != DialogParams.Screen.nVdiResolution
	     || ConfigureParams.Screen.nVdiColors != DialogParams.Screen.nVdiColors))
		return TRUE;

	/* Did change TOS ROM image? */
	if (strcmp(DialogParams.Rom.szTosImageFileName, ConfigureParams.Rom.szTosImageFileName))
		return TRUE;

	/* Did change HD image? */
	if (DialogParams.HardDisk.bUseHardDiskImage != ConfigureParams.HardDisk.bUseHardDiskImage
	    || (strcmp(DialogParams.HardDisk.szHardDiskImage, ConfigureParams.HardDisk.szHardDiskImage)
	        && DialogParams.HardDisk.bUseHardDiskImage))
		return TRUE;

	/* Did change GEMDOS drive? */
	if (DialogParams.HardDisk.bUseHardDiskDirectories != ConfigureParams.HardDisk.bUseHardDiskDirectories
	    || (strcmp(DialogParams.HardDisk.szHardDiskDirectories[0], ConfigureParams.HardDisk.szHardDiskDirectories[0])
	        && DialogParams.HardDisk.bUseHardDiskDirectories))
		return TRUE;

	/* Did change machine type? */
	if (DialogParams.System.nMachineType != ConfigureParams.System.nMachineType)
		return TRUE;

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Copy details back to configuration and perform reset.
 */
void Dialog_CopyDialogParamsToConfiguration(BOOL bForceReset)
{
	BOOL NeedReset;
	BOOL bReInitGemdosDrive = FALSE, bReInitAcsiEmu = FALSE;
	BOOL bReInitIoMem = FALSE;

	/* Do we need to warn user of that changes will only take effect after reset? */
	if (bForceReset)
		NeedReset = bForceReset;
	else
		NeedReset = Dialog_DoNeedReset();

	/* Do need to change resolution? Need if change display/overscan settings */
	/*(if switch between Colour/Mono cause reset later) */
	if (!NeedReset &&
	    (DialogParams.Screen.bForce8Bpp != ConfigureParams.Screen.bForce8Bpp
	     || DialogParams.Screen.bZoomLowRes != ConfigureParams.Screen.bZoomLowRes
	     || DialogParams.Screen.bAllowOverscan != ConfigureParams.Screen.bAllowOverscan))
	{
		ConfigureParams.Screen.bForce8Bpp = DialogParams.Screen.bForce8Bpp;
		ConfigureParams.Screen.bZoomLowRes = DialogParams.Screen.bZoomLowRes;
		ConfigureParams.Screen.bAllowOverscan = DialogParams.Screen.bAllowOverscan;

		Screen_ModeChanged();
	}

	/* Did set new printer parameters? */
	if (DialogParams.Printer.bEnablePrinting != ConfigureParams.Printer.bEnablePrinting
	    || DialogParams.Printer.bPrintToFile != ConfigureParams.Printer.bPrintToFile
	    || strcmp(DialogParams.Printer.szPrintToFileName,ConfigureParams.Printer.szPrintToFileName))
	{
		Printer_CloseAllConnections();
	}

	/* Did set new RS232 parameters? */
	if (DialogParams.RS232.bEnableRS232 != ConfigureParams.RS232.bEnableRS232
	    || strcmp(DialogParams.RS232.szOutFileName, ConfigureParams.RS232.szOutFileName)
	    || strcmp(DialogParams.RS232.szInFileName, ConfigureParams.RS232.szInFileName))
	{
		RS232_UnInit();
	}

	/* Did stop sound? Or change playback Hz. If so, also stop sound recording */
	if (!DialogParams.Sound.bEnableSound || DialogParams.Sound.nPlaybackQuality != ConfigureParams.Sound.nPlaybackQuality)
	{
		if (Sound_AreWeRecording())
			Sound_EndRecording();
		Audio_UnInit();
	}

	/* Did change GEMDOS drive? */
	if (DialogParams.HardDisk.bUseHardDiskDirectories != ConfigureParams.HardDisk.bUseHardDiskDirectories
	    || (strcmp(DialogParams.HardDisk.szHardDiskDirectories[0], ConfigureParams.HardDisk.szHardDiskDirectories[0])
	        && DialogParams.HardDisk.bUseHardDiskDirectories))
	{
		GemDOS_UnInitDrives();
		bReInitGemdosDrive = TRUE;
	}

	/* Did change HD image? */
	if (DialogParams.HardDisk.bUseHardDiskImage != ConfigureParams.HardDisk.bUseHardDiskImage
	    || (strcmp(DialogParams.HardDisk.szHardDiskImage, ConfigureParams.HardDisk.szHardDiskImage)
	        && DialogParams.HardDisk.bUseHardDiskImage))
	{
		HDC_UnInit();
		bReInitAcsiEmu = TRUE;
	}

	/* Did change blitter, rtc or system type? */
	if (DialogParams.System.bBlitter != ConfigureParams.System.bBlitter
#if ENABLE_DSP_EMU
	    || DialogParams.System.nDSPType != ConfigureParams.System.nDSPType
#endif
	    || DialogParams.System.bRealTimeClock != ConfigureParams.System.bRealTimeClock
	    || DialogParams.System.nMachineType != ConfigureParams.System.nMachineType)
	{
		IoMem_UnInit();
		bReInitIoMem = TRUE;
	}
	
#if ENABLE_DSP_EMU
	/* Disabled DSP? */
	if (DialogParams.System.nDSPType == DSP_TYPE_EMU &&
	    (DialogParams.System.nDSPType != ConfigureParams.System.nDSPType))
	{
		DSP_UnInit();
	}
#endif

	/* Copy details to configuration, so can be saved out or set on reset */
	ConfigureParams = DialogParams;

	/* Copy details to global, if we reset copy them all */
	Configuration_Apply(NeedReset);

#if ENABLE_DSP_EMU
	if (ConfigureParams.System.nDSPType == DSP_TYPE_EMU)
	{
		DSP_Init();
	}
#endif

	/* Set keyboard remap file */
	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_LOADED)
		Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);

	/* Mount a new HD image: */
	if (bReInitAcsiEmu && ConfigureParams.HardDisk.bUseHardDiskImage)
	{
		HDC_Init(ConfigureParams.HardDisk.szHardDiskImage);
	}

	/* Mount a new GEMDOS drive? */
	if (bReInitGemdosDrive && ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
		GemDOS_InitDrives();
	}

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

	/* Re-init IO memory map? */
	if (bReInitIoMem)
	{
		IoMem_Init();
	}

	/* Do we need to perform reset? */
	if (NeedReset)
	{
		Reset_Cold();
	}

	/* Go into/return from full screen if flagged */
	if (!bInFullScreen && DialogParams.Screen.bFullScreen)
		Screen_EnterFullScreen();
	else if (bInFullScreen && !DialogParams.Screen.bFullScreen)
		Screen_ReturnFromFullScreen();
}


/*-----------------------------------------------------------------------*/
/**
 * Open Property sheet Options dialog.
 * Return TRUE if user chooses OK, or FALSE if cancel!
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

	/* Check if reset is required and ask user if he really wants to continue then */
	if (bOKDialog && !bForceReset && Dialog_DoNeedReset()
	    && ConfigureParams.Log.nAlertDlgLogLevel >= LOG_INFO) {
		bOKDialog = DlgAlert_Query("The emulated system must be "
		                           "reset to apply these changes. "
		                           "Apply changes now and reset "
		                           "the emulator?");
	}

	/* Copy details to configuration */
	if (bOKDialog) {
		Dialog_CopyDialogParamsToConfiguration(bForceReset);
	}

	Main_UnPauseEmulation();

	if (bQuitProgram)
		Main_RequestQuit();

	return bOKDialog;
}


/*-----------------------------------------------------------------------*/
/**
 * Loads params from the configuration file into DialogParams
 */
void Dialog_LoadParams(void)
{
	CNF_PARAMS tmpParams;
	/* Configuration_Load uses the variables from ConfigureParams.
	 * That's why we have to temporarily back it up here */
	tmpParams = ConfigureParams;
	Configuration_Load(NULL);
	DialogParams = ConfigureParams;
	ConfigureParams = tmpParams;
}


/*-----------------------------------------------------------------------*/
/**
 * Saves params in DialogParams to the configuration file
 */
void Dialog_SaveParams(void)
{
	CNF_PARAMS tmpParams;
	/* Configuration_Save uses the variables from ConfigureParams.
	 * That's why we have to temporarily back it up here */
	tmpParams = ConfigureParams;
	ConfigureParams = DialogParams;
	Configuration_Save();
	ConfigureParams = tmpParams;
}
