/*
  Hatari - change.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This code handles run-time configuration changes. We keep all our
  configuration details in a structure called 'ConfigureParams'.  Before
  doing he changes, a backup copy is done of this structure. When
  the changes are done, these are compared to see whether emulator
  needs to be rebooted
*/
const char Change_fileid[] = "Hatari change.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include "main.h"
#include "configuration.h"
#include "audio.h"
#include "change.h"
#include "dialog.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "ide.h"
#include "ioMem.h"
#include "joy.h"
#include "keymap.h"
#include "m68000.h"
#include "midi.h"
#include "options.h"
#include "printer.h"
#include "reset.h"
#include "rs232.h"
#include "screen.h"
#include "sound.h"
#include "statusbar.h"
#include "tos.h"
#include "vdi.h"
#include "video.h"
#include "hatari-glue.h"
#if ENABLE_DSP_EMU
# include "falcon/dsp.h"
#endif


/*-----------------------------------------------------------------------*/
/**
 * Check if user needs to be warned that changes will take place after reset.
 * Return true if wants to reset.
 */
bool Change_DoNeedReset(CNF_PARAMS *current, CNF_PARAMS *changed)
{
	/* Did we change monitor type? If so, must reset */
	if (current->Screen.nMonitorType != changed->Screen.nMonitorType
	    && (changed->System.nMachineType == MACHINE_FALCON
	        || current->Screen.nMonitorType == MONITOR_TYPE_MONO
	        || changed->Screen.nMonitorType == MONITOR_TYPE_MONO))
		return true;

	/* Did change to GEM VDI display? */
	if (current->Screen.bUseExtVdiResolutions != changed->Screen.bUseExtVdiResolutions)
		return true;

	/* Did change GEM resolution or color depth? */
	if (changed->Screen.bUseExtVdiResolutions &&
	    (current->Screen.nVdiWidth != changed->Screen.nVdiWidth
	     || current->Screen.nVdiHeight != changed->Screen.nVdiHeight
	     || current->Screen.nVdiColors != changed->Screen.nVdiColors))
		return true;

	/* Did change TOS ROM image? */
	if (strcmp(changed->Rom.szTosImageFileName, current->Rom.szTosImageFileName))
		return true;

	/* Did change ACSI hard disk image? */
	if (changed->HardDisk.bUseHardDiskImage != current->HardDisk.bUseHardDiskImage
	    || (strcmp(changed->HardDisk.szHardDiskImage, current->HardDisk.szHardDiskImage)
	        && changed->HardDisk.bUseHardDiskImage))
		return true;

	/* Did change IDE hard disk image? */
	if (changed->HardDisk.bUseIdeHardDiskImage != current->HardDisk.bUseIdeHardDiskImage
	    || strcmp(changed->HardDisk.szIdeHardDiskImage, current->HardDisk.szIdeHardDiskImage))
		return true;

	/* Did change GEMDOS drive? */
	if (changed->HardDisk.bUseHardDiskDirectories != current->HardDisk.bUseHardDiskDirectories
	    || (strcmp(changed->HardDisk.szHardDiskDirectories[0], current->HardDisk.szHardDiskDirectories[0])
	        && changed->HardDisk.bUseHardDiskDirectories))
		return true;

	/* Did change machine type? */
	if (changed->System.nMachineType != current->System.nMachineType)
		return true;

	/* Did change size of memory? */
	if (current->Memory.nMemorySize != changed->Memory.nMemorySize)
		return true;

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Copy details back to configuration and perform reset.
 */
void Change_CopyChangedParamsToConfiguration(CNF_PARAMS *current, CNF_PARAMS *changed, bool bForceReset)
{
	bool NeedReset;
	bool bReInitGemdosDrive = false;
	bool bReInitAcsiEmu = false;
	bool bReInitIDEEmu = false;
	bool bReInitIoMem = false;
	bool bScreenModeChange = false;
	bool bReInitMidi = false;
	bool bFloppyInsert[MAX_FLOPPYDRIVES];
	int i;

	/* Do we need to warn user that changes will only take effect after reset? */
	if (bForceReset)
		NeedReset = bForceReset;
	else
		NeedReset = Change_DoNeedReset(current, changed);

	/* Do need to change resolution? Need if change display/overscan settings
	 * (if switch between Colour/Mono cause reset later) or toggle statusbar
	 */
	if (!NeedReset &&
	    (changed->Screen.nForceBpp != current->Screen.nForceBpp
	     || changed->Screen.bZoomLowRes != current->Screen.bZoomLowRes
	     || changed->Screen.bAllowOverscan != current->Screen.bAllowOverscan
	     || changed->Screen.bShowStatusbar != current->Screen.bShowStatusbar))
	{
		bScreenModeChange = true;
	}

	/* Did set new printer parameters? */
	if (changed->Printer.bEnablePrinting != current->Printer.bEnablePrinting
	    || changed->Printer.bPrintToFile != current->Printer.bPrintToFile
	    || strcmp(changed->Printer.szPrintToFileName,current->Printer.szPrintToFileName))
	{
		Printer_CloseAllConnections();
	}

	/* Did set new RS232 parameters? */
	if (changed->RS232.bEnableRS232 != current->RS232.bEnableRS232
	    || strcmp(changed->RS232.szOutFileName, current->RS232.szOutFileName)
	    || strcmp(changed->RS232.szInFileName, current->RS232.szInFileName))
	{
		RS232_UnInit();
	}

	/* Did stop sound? Or change playback Hz. If so, also stop sound recording */
	if (!changed->Sound.bEnableSound || changed->Sound.nPlaybackFreq != current->Sound.nPlaybackFreq)
	{
		if (Sound_AreWeRecording())
			Sound_EndRecording();
		Audio_UnInit();
	}

	/* Did change floppy (images)? */
	for (i = 0; i < MAX_FLOPPYDRIVES; i++)
	{
		/*
		Log_Printf(LOG_DEBUG, "Old and new disk %c:\n\t%s\n\t%s", 'A'+i,
			   current->DiskImage.szDiskFileName[i],
			   changed->DiskImage.szDiskFileName[i]);
		 */
		if (strcmp(changed->DiskImage.szDiskFileName[i],
			   current->DiskImage.szDiskFileName[i])
		    || strcmp(changed->DiskImage.szDiskZipPath[i],
			      current->DiskImage.szDiskZipPath[i]))
			bFloppyInsert[i] = true;
		else
			bFloppyInsert[i] = false;
	}

	/* Did change GEMDOS drive? */
	if (changed->HardDisk.bUseHardDiskDirectories != current->HardDisk.bUseHardDiskDirectories
	    || (strcmp(changed->HardDisk.szHardDiskDirectories[0], current->HardDisk.szHardDiskDirectories[0])
	        && changed->HardDisk.bUseHardDiskDirectories))
	{
		GemDOS_UnInitDrives();
		bReInitGemdosDrive = true;
	}

	/* Did change HD image? */
	if (changed->HardDisk.bUseHardDiskImage != current->HardDisk.bUseHardDiskImage
	    || (strcmp(changed->HardDisk.szHardDiskImage, current->HardDisk.szHardDiskImage)
	        && changed->HardDisk.bUseHardDiskImage))
	{
		HDC_UnInit();
		bReInitAcsiEmu = true;
	}
	
	/* Did change IDE HD image? */
	if (changed->HardDisk.bUseIdeHardDiskImage != current->HardDisk.bUseIdeHardDiskImage
	    || (strcmp(changed->HardDisk.szIdeHardDiskImage, current->HardDisk.szIdeHardDiskImage)
	        && changed->HardDisk.bUseIdeHardDiskImage))
	{
		Ide_UnInit();
		bReInitIDEEmu = true;
	}

	/* Did change blitter, rtc or system type? */
	if (changed->System.bBlitter != current->System.bBlitter
#if ENABLE_DSP_EMU
	    || changed->System.nDSPType != current->System.nDSPType
#endif
	    || changed->System.bRealTimeClock != current->System.bRealTimeClock
	    || changed->System.nMachineType != current->System.nMachineType)
	{
		IoMem_UnInit();
		bReInitIoMem = true;
	}
	
#if ENABLE_DSP_EMU
	/* Disabled DSP? */
	if (current->System.nDSPType == DSP_TYPE_EMU &&
	    changed->System.nDSPType != DSP_TYPE_EMU)
	{
		DSP_UnInit();
	}
#endif

	/* Did change MIDI settings? */
	if (current->Midi.bEnableMidi != changed->Midi.bEnableMidi
	    || ((strcmp(changed->Midi.sMidiOutFileName, current->Midi.sMidiOutFileName)
	         || strcmp(changed->Midi.sMidiInFileName, current->Midi.sMidiInFileName))
	        && changed->Midi.bEnableMidi))
	{
		Midi_UnInit();
		bReInitMidi = true;
	}

	/* Copy details to configuration,
	 * so it can be saved out or set on reset
	 */
	if (changed != &ConfigureParams)
	{
		ConfigureParams = *changed;
	}

	/* Copy details to global, if we reset copy them all */
	Configuration_Apply(NeedReset);

#if ENABLE_DSP_EMU
	if (current->System.nDSPType != DSP_TYPE_EMU &&
	    changed->System.nDSPType == DSP_TYPE_EMU)
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

	/* Mount a new IDE HD image: */
	if (bReInitIDEEmu && ConfigureParams.HardDisk.bUseIdeHardDiskImage)
	{
		Ide_Init();
	}

	/* Insert floppies? */
	for (i = 0; i < MAX_FLOPPYDRIVES; i++)
	{
		if (bFloppyInsert[i])
			Floppy_InsertDiskIntoDrive(i);
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

	/* Re-init MIDI emulation? */
	if (bReInitMidi)
	{
		Midi_Init();
	}

	/* Force things associated with screen change */
	if (bScreenModeChange)
	{
		Screen_ModeChanged();
	}

	/* Do we need to perform reset? */
	if (NeedReset)
	{
		Reset_Cold();
		/* reset needing changes may affect also info shown in statusbar */
		Statusbar_UpdateInfo();
	}

	/* Go into/return from full screen if flagged */
	if (!bInFullScreen && ConfigureParams.Screen.bFullScreen)
		Screen_EnterFullScreen();
	else if (bInFullScreen && !ConfigureParams.Screen.bFullScreen)
		Screen_ReturnFromFullScreen();
}


/*-----------------------------------------------------------------------*/
/**
 * Change given Hatari options
 * Return false if parsing failed, true otherwise
 */
static bool Change_Options(int argc, const char *argv[])
{
	bool bOK;
	CNF_PARAMS current;

	Main_PauseEmulation(false);

	/* get configuration changes */
	current = ConfigureParams;
	ConfigureParams.Screen.bFullScreen = bInFullScreen;
	bOK = Opt_ParseParameters(argc, argv);

	/* Check if reset is required and ask user if he really wants to continue */
	if (bOK && Change_DoNeedReset(&current, &ConfigureParams)
	    && current.Log.nAlertDlgLogLevel >= LOG_WARN) {
		bOK = DlgAlert_Query("The emulated system must be "
				     "reset to apply these changes. "
				     "Apply changes now and reset "
				     "the emulator?");
	}
	/* Copy details to configuration */
	if (bOK) {
		Change_CopyChangedParamsToConfiguration(&current, &ConfigureParams, false);
	} else {
		ConfigureParams = current;
	}

	Main_UnPauseEmulation();
	return bOK;
}


/*-----------------------------------------------------------------------*/
/**
 * Parse given command line and change Hatari options accordingly
 * Return false if parsing failed or there were no args, true otherwise
 */
bool Change_ApplyCommandline(char *cmdline)
{
	int i, argc, inarg;
	const char **argv;
	bool ret;

	/* count args */
	inarg = argc = 0;
	for (i = 0; cmdline[i]; i++)
	{
		if (isspace(cmdline[i]))
		{
			inarg = 0;
			continue;
		}
		if (!inarg)
		{
			inarg++;
			argc++;
		}
	}
	if (!argc)
	{
		return false;
	}
	/* 2 = "hatari" + NULL */
	argv = malloc((argc+2) * sizeof(char*));
	if (!argv)
	{
		perror("command line alloc");
		return false;
	}

	/* parse them to array */
	fprintf(stderr, "Command line with '%d' arguments:\n", argc);
	inarg = argc = 0;
	argv[argc++] = "hatari";
	for (i = 0; cmdline[i]; i++)
	{
		if (isspace(cmdline[i]))
		{
			cmdline[i] = '\0';
			if (inarg)
			{
				fprintf(stderr, "- '%s'\n", argv[argc-1]);
			}
			inarg = 0;
			continue;
		}
		if (!inarg)
		{
			argv[argc++] = &(cmdline[i]);
			inarg++;
		}
	}
	if (inarg)
	{
		fprintf(stderr, "- '%s'\n", argv[argc-1]);
	}
	argv[argc] = NULL;
	
	/* do args */
	ret = Change_Options(argc, argv);
	free((void *)argv);
	return ret;
}
