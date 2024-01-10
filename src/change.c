/*
  Hatari - change.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This code handles run-time configuration changes. We keep all our
  configuration details in a structure called 'ConfigureParams'.  Before
  doing he changes, a backup copy is done of this structure. When
  the changes are done, these are compared to see whether emulator
  needs to be rebooted
*/
const char Change_fileid[] = "Hatari change.c";

#include <ctype.h>
#include "main.h"
#include "configuration.h"
#include "audio.h"
#include "change.h"
#include "dialog.h"
#include "floppy.h"
#include "fdc.h"
#include "gemdos.h"
#include "hdc.h"
#include "ide.h"
#include "ioMem.h"
#include "joy.h"
#include "keymap.h"
#include "m68000.h"
#include "midi.h"
#include "ncr5380.h"
#include "options.h"
#include "printer.h"
#include "reset.h"
#include "rs232.h"
#include "scc.h"
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

#define DEBUG 0
#if DEBUG
#define Dprintf(...) printf(__VA_ARGS__)
#else
#define Dprintf(...)
#endif

/*-----------------------------------------------------------------------*/
/**
 * Check if user needs to be warned that changes will take place after reset.
 * Return true if wants to reset.
 */
bool Change_DoNeedReset(CNF_PARAMS *current, CNF_PARAMS *changed)
{
	int i;

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
	for (i = 0; i < MAX_ACSI_DEVS; i++)
	{
		if (changed->Acsi[i].bUseDevice != current->Acsi[i].bUseDevice
		    || (strcmp(changed->Acsi[i].sDeviceFile, current->Acsi[i].sDeviceFile)
		        && changed->Acsi[i].bUseDevice))
			return true;
	}

	/* Did change SCSI hard disk image? */
	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		if (changed->Scsi[i].bUseDevice != current->Scsi[i].bUseDevice
		    || (strcmp(changed->Scsi[i].sDeviceFile, current->Scsi[i].sDeviceFile)
		        && changed->Scsi[i].bUseDevice))
			return true;
	}

	/* Did change IDE hard disk image? */
	for (i = 0; i < MAX_IDE_DEVS; i++)
	{
		if (changed->Ide[i].bUseDevice != current->Ide[i].bUseDevice
		    || changed->Ide[i].nByteSwap != current->Ide[i].nByteSwap
		    || (strcmp(changed->Ide[i].sDeviceFile, current->Ide[i].sDeviceFile)
		        && changed->Ide[i].bUseDevice))
			return true;
	}

	/* Did change GEMDOS drive Atari/host location or enabling? */
	if (changed->HardDisk.nGemdosDrive != current->HardDisk.nGemdosDrive
	    || changed->HardDisk.bUseHardDiskDirectories != current->HardDisk.bUseHardDiskDirectories
	    || (strcmp(changed->HardDisk.szHardDiskDirectories[0], current->HardDisk.szHardDiskDirectories[0])
	        && changed->HardDisk.bUseHardDiskDirectories))
		return true;

	/* Did change machine type? */
	if (changed->System.nMachineType != current->System.nMachineType)
		return true;
	/* did change ST Blitter? */
	else if (current->System.nMachineType == MACHINE_ST &&
		 current->System.bBlitter != changed->System.bBlitter)
		return true;

#if ENABLE_DSP_EMU
	/* enabling DSP needs reset (disabling it not) */
	if (current->System.nDSPType != DSP_TYPE_EMU &&
	    changed->System.nDSPType == DSP_TYPE_EMU)
		return true;
#endif

	/* did change CPU type? */
	if (changed->System.nCpuLevel != current->System.nCpuLevel)
		return true;

	/* Did change CPU address mode? */
	if (changed->System.bAddressSpace24 != current->System.bAddressSpace24)
		return true;

	/* Did change CPU prefetch mode? */
	if (changed->System.bCompatibleCpu != current->System.bCompatibleCpu)
		return true;

	/* Did change CPU cycle exact? */
	if (changed->System.bCycleExactCpu != current->System.bCycleExactCpu)
		return true;

	/* Did change MMU? */
	if (changed->System.bMMU != current->System.bMMU)
		return true;
 
	/* Did change FPU? */
	if (changed->System.n_FPUType != current->System.n_FPUType)
		return true;

	/* Did change size of TT-RAM? */
	if (current->Memory.TTRamSize_KB != changed->Memory.TTRamSize_KB)
		return true;

	/* Did change size of memory? */
	if (current->Memory.STRamSize_KB != changed->Memory.STRamSize_KB)
		return true;

	/* MIDI related IRQs start/stop needs reset */
	if (current->Midi.bEnableMidi != changed->Midi.bEnableMidi)
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
	bool bReInitScsiEmu = false;
	bool bReInitHdcEmu = false;
	bool bReInitIDEEmu = false;
	bool bReInitIoMem = false;
	bool bScreenModeChange = false;
	bool bReInitMidi = false;
	bool bReInitPrinter = false;
	bool bFloppyInsert[MAX_FLOPPYDRIVES];
	int i;

	Dprintf("Changes for:\n");
	/* Do we need to warn user that changes will only take effect after reset? */
	if (bForceReset)
		NeedReset = bForceReset;
	else
		NeedReset = Change_DoNeedReset(current, changed);

	/* Do need to change resolution? Need if change display/overscan settings
	 * (if switch between Colour/Mono cause reset later) or toggle statusbar
	 */
	if (!NeedReset &&
	    (changed->Screen.bAspectCorrect != current->Screen.bAspectCorrect
	     || changed->Screen.nMaxWidth != current->Screen.nMaxWidth
	     || changed->Screen.nMaxHeight != current->Screen.nMaxHeight
	     || changed->Screen.bAllowOverscan != current->Screen.bAllowOverscan
	     || changed->Screen.bShowStatusbar != current->Screen.bShowStatusbar
	     || changed->Screen.bUseSdlRenderer != current->Screen.bUseSdlRenderer
	     || changed->Screen.bResizable != current->Screen.bResizable
	     || changed->Screen.bUseVsync != current->Screen.bUseVsync
	    ))
	{
		Dprintf("- screenmode>\n");
		bScreenModeChange = true;
	}

	/* Did set new printer parameters? */
	if (changed->Printer.bEnablePrinting != current->Printer.bEnablePrinting
	    || strcmp(changed->Printer.szPrintToFileName,current->Printer.szPrintToFileName))
	{
		Dprintf("- printer>\n");
		Printer_UnInit();
		bReInitPrinter = true;
	}

	/* Did set new RS232 parameters? */
	if (changed->RS232.bEnableRS232 != current->RS232.bEnableRS232
	    || strcmp(changed->RS232.szOutFileName, current->RS232.szOutFileName)
	    || strcmp(changed->RS232.szInFileName, current->RS232.szInFileName))
	{
		Dprintf("- RS-232>\n");
		RS232_UnInit();
	}

	/* Did set new SCC parameters? */
	if (changed->RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL] != current->RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL]
	    || strcmp(changed->RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL], current->RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL])
	    || strcmp(changed->RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL], current->RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL])
	    || changed->RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN] != current->RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN]
	    || strcmp(changed->RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN], current->RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN])
	    || strcmp(changed->RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN], current->RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN])
	    || changed->RS232.EnableScc[CNF_SCC_CHANNELS_B] != current->RS232.EnableScc[CNF_SCC_CHANNELS_B]
	    || strcmp(changed->RS232.SccInFileName[CNF_SCC_CHANNELS_B], current->RS232.SccInFileName[CNF_SCC_CHANNELS_B])
	    || strcmp(changed->RS232.SccOutFileName[CNF_SCC_CHANNELS_B], current->RS232.SccOutFileName[CNF_SCC_CHANNELS_B])
	    || (SCC_IsAvailable(current) && !SCC_IsAvailable(changed)))
	{
		Dprintf("- SCC>\n");
		SCC_UnInit();
	}

	/* Did stop sound? Or change playback Hz. If so, also stop sound recording */
	if (!changed->Sound.bEnableSound || changed->Sound.nPlaybackFreq != current->Sound.nPlaybackFreq)
	{
		Dprintf("- sound>\n");
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

	if ( changed->DiskImage.EnableDriveA != current->DiskImage.EnableDriveA )
		FDC_Drive_Set_Enable ( 0 , changed->DiskImage.EnableDriveA );
	if ( changed->DiskImage.EnableDriveB != current->DiskImage.EnableDriveB )
		FDC_Drive_Set_Enable ( 1 , changed->DiskImage.EnableDriveB );

	if ( changed->DiskImage.DriveA_NumberOfHeads != current->DiskImage.DriveA_NumberOfHeads )
		FDC_Drive_Set_NumberOfHeads ( 0 , changed->DiskImage.DriveA_NumberOfHeads );
	if ( changed->DiskImage.DriveB_NumberOfHeads != current->DiskImage.DriveB_NumberOfHeads )
		FDC_Drive_Set_NumberOfHeads ( 1 , changed->DiskImage.DriveB_NumberOfHeads );

	/* Did change GEMDOS drive Atari/host location or enabling? */
	if (changed->HardDisk.nGemdosDrive != current->HardDisk.nGemdosDrive
	    || changed->HardDisk.bUseHardDiskDirectories != current->HardDisk.bUseHardDiskDirectories
	    || (strcmp(changed->HardDisk.szHardDiskDirectories[0], current->HardDisk.szHardDiskDirectories[0])
	        && changed->HardDisk.bUseHardDiskDirectories))
	{
		Dprintf("- gemdos HD>\n");
		GemDOS_UnInitDrives();
		bReInitGemdosDrive = true;
	}

	/* Did change ACSI images? */
	for (i = 0; i < MAX_ACSI_DEVS; i++)
	{
		if (changed->Acsi[i].bUseDevice != current->Acsi[i].bUseDevice
		    || (strcmp(changed->Acsi[i].sDeviceFile, current->Acsi[i].sDeviceFile)
		        && changed->Acsi[i].bUseDevice))
		{
			Dprintf("- ACSI image %i>\n", i);
			bReInitHdcEmu = true;
		}
	}
	if (bReInitHdcEmu)
		HDC_UnInit();

	/* Did change SCSI images? */
	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		if (changed->Scsi[i].bUseDevice != current->Scsi[i].bUseDevice
		    || (strcmp(changed->Scsi[i].sDeviceFile, current->Scsi[i].sDeviceFile)
		        && changed->Scsi[i].bUseDevice))
		{
			Dprintf("- SCSI image %i>\n", i);
			bReInitScsiEmu = true;
		}
	}
	if (bReInitScsiEmu)
		Ncr5380_UnInit();

	/* Did change IDE HD images or their settings? */
	for (i = 0; i < MAX_IDE_DEVS; i++)
	{
		if (changed->Ide[i].bUseDevice != current->Ide[i].bUseDevice
		    || changed->Ide[i].nByteSwap != current->Ide[i].nByteSwap
		    || (strcmp(changed->Ide[i].sDeviceFile, current->Ide[i].sDeviceFile)
		        && changed->Ide[i].bUseDevice))
		{
			Dprintf("- IDE image %i>\n", i);
			bReInitIDEEmu = true;
		}
	}
	/* Falcon has always an IDE controller */
	if (!bReInitIDEEmu && changed->System.nMachineType == MACHINE_FALCON &&
	    (current->System.nMachineType != MACHINE_FALCON ||
	     current->System.bFastBoot != changed->System.bFastBoot))
	{
		Dprintf("- IDE subsystem>\n");
		bReInitIDEEmu = true;
	}
	if (bReInitIDEEmu)
		Ide_UnInit();

	/* Did change blitter, DSP or system type? */
	if (changed->System.bBlitter != current->System.bBlitter
#if ENABLE_DSP_EMU
	    || changed->System.nDSPType != current->System.nDSPType
#endif
	    || changed->System.nMachineType != current->System.nMachineType)
	{
		Dprintf("- blitter/dsp/machine>\n");
		IoMem_UnInit();
		bReInitIoMem = true;
	}
	
#if ENABLE_DSP_EMU
	/* Disabled DSP? */
	if (current->System.nDSPType == DSP_TYPE_EMU &&
	    changed->System.nDSPType != DSP_TYPE_EMU)
	{
		Dprintf("- DSP>\n");
		DSP_Disable();
	}
#endif

	/* Did change MIDI settings? */
	if (current->Midi.bEnableMidi != changed->Midi.bEnableMidi
#ifdef HAVE_PORTMIDI
	    || strcmp(changed->Midi.sMidiOutPortName, current->Midi.sMidiOutPortName)
	    || strcmp(changed->Midi.sMidiInPortName, current->Midi.sMidiInPortName)
#else
	    || strcmp(changed->Midi.sMidiOutFileName, current->Midi.sMidiOutFileName)
	    || strcmp(changed->Midi.sMidiInFileName, current->Midi.sMidiInFileName)
#endif
	    )
	{
		Dprintf("- midi>\n");
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
		Dprintf("- DSP<\n");
		DSP_Enable();
	}
#endif

	/* Set keyboard remap file */
	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_LOADED)
	{
		Dprintf("- keymap<\n");
		Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);
	}

	/* Mount new ACSI HD images: */
	if (bReInitHdcEmu)
	{
		Dprintf("- ACSI<\n");
		HDC_Init();
	}
	/* Mount new SCSI HD images: */
	if (bReInitScsiEmu)
	{
		Dprintf("- SCSI<\n");
		Ncr5380_Init();
	}
	/* Mount new IDE HD images: */
	if (bReInitIDEEmu && Ide_IsAvailable())
	{
		Dprintf("- IDE<\n");
		Ide_Init();
	}

	/* Insert floppies? */
	for (i = 0; i < MAX_FLOPPYDRIVES; i++)
	{
		if (bFloppyInsert[i])
		{
			Dprintf("- floppy<\n");
			Floppy_InsertDiskIntoDrive(i);
		}
	}

	/* Mount a new GEMDOS drive? */
	if (bReInitGemdosDrive && ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
		Dprintf("- gemdos HD<\n");
		GemDOS_InitDrives();
	}

	/* Restart audio sub system if necessary: */
	if (ConfigureParams.Sound.bEnableSound && !bSoundWorking)
	{
		Dprintf("- audio<\n");
		Audio_Init();
	}

	/* Re-initialize the RS232 emulation: */
	if (ConfigureParams.RS232.bEnableRS232)
	{
		Dprintf("- RS-232<\n");
		RS232_Init();
	}

	/* Re-initialize the SCC emulation: */
	if ( ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL] || ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN] || ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_B] )
	{
		Dprintf("- SCC<\n");
		SCC_Init();
	}

	/* Re-init IO memory map? */
	if (bReInitIoMem)
	{
		Dprintf("- IO mem<\n");
		IoMem_Init();
	}

	/* Re-init Printer emulation? */
	if (bReInitPrinter)
	{
		Dprintf("- printer<\n");
		Printer_Init();
	}

	/* Re-init MIDI emulation? */
	if (bReInitMidi)
	{
		Dprintf("- midi<\n");
		Midi_Init();
		if (!NeedReset)
		{
			/* Restart MIDI IRQ stopped on Midi_UnInit() */
			Midi_Reset();
		}
	}

	/* Force things associated with screen change */
	if (bScreenModeChange)
	{
		Dprintf("- screenmode<\n");
		Screen_ModeChanged(true);
	}

	/* Do we need to perform reset? */
	if (NeedReset)
	{
		Dprintf("- reset\n");
		Reset_Cold();
	}

	/* Go into/return from full screen if flagged */
	if (!bInFullScreen && ConfigureParams.Screen.bFullScreen)
		Screen_EnterFullScreen();
	else if (bInFullScreen && !ConfigureParams.Screen.bFullScreen)
		Screen_ReturnFromFullScreen();

	/* update statusbar info (CPU, MHz, mem etc) */
	Statusbar_UpdateInfo();
	Dprintf("done.\n");
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
	    && current.Log.nAlertDlgLogLevel > LOG_FATAL) {
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
 * Parse given command line and change Hatari options accordingly.
 * Given string must be stripped and not empty.
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
		if (isspace((unsigned char)cmdline[i]) && cmdline[i-1] != '\\')
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
		if (isspace((unsigned char)cmdline[i]))
		{
			if (cmdline[i-1] != '\\')
			{
				cmdline[i] = '\0';
				if (inarg)
				{
					fprintf(stderr, "- '%s'\n", argv[argc-1]);
				}
				inarg = 0;
				continue;
			}
			else
			{
				/* remove quote for space */
				memcpy(cmdline+i-1, cmdline+i, strlen(cmdline+i)+1);
				i--;
			}
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
