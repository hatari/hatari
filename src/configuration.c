/*
  Hatari - configuration.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Configuration File

  The configuration file is now stored in an ASCII format to allow the user
  to edit the file manually.
*/
char Configuration_rcsid[] = "Hatari $Id: configuration.c,v 1.43 2005-06-05 14:19:39 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "cfgopts.h"
#include "audio.h"
#include "file.h"
#include "log.h"
#include "m68000.h"
#include "screen.h"
#include "vdi.h"
#include "video.h"
#include "uae-cpu/hatari-glue.h"


BOOL bFirstTimeInstall = FALSE;             /* Has been run before? Used to set default joysticks etc... */
CNF_PARAMS ConfigureParams;                 /* List of configuration for the emulator */
char sConfigFileName[FILENAME_MAX];         /* Stores the name of the configuration file */


/* Used to load/save logging options */
static const struct Config_Tag configs_Log[] =
{
	{ "sLogFileName", String_Tag, ConfigureParams.Log.sLogFileName },
	{ "nTextLogLevel", Int_Tag, &ConfigureParams.Log.nTextLogLevel },
	{ "nAlertDlgLogLevel", Int_Tag, &ConfigureParams.Log.nAlertDlgLogLevel },
	{ NULL , Error_Tag, NULL }
};


/* Used to load/save screen options */
static const struct Config_Tag configs_Screen[] =
{
	{ "bFullScreen", Bool_Tag, &ConfigureParams.Screen.bFullScreen },
	{ "bFrameSkip", Bool_Tag, &ConfigureParams.Screen.bFrameSkip },
	{ "bAllowOverscan", Bool_Tag, &ConfigureParams.Screen.bAllowOverscan },
	{ "bInterleavedScreen", Bool_Tag, &ConfigureParams.Screen.bInterleavedScreen },
	{ "ChosenDisplayMode", Int_Tag, &ConfigureParams.Screen.ChosenDisplayMode },
	{ "bUseHighRes", Bool_Tag, &ConfigureParams.Screen.bUseHighRes },
	{ "bUseExtVdiResolutions", Bool_Tag, &ConfigureParams.Screen.bUseExtVdiResolutions },
	{ "nVdiResolution", Int_Tag, &ConfigureParams.Screen.nVdiResolution },
	{ "nVdiColors", Int_Tag, &ConfigureParams.Screen.nVdiColors },
	{ "bCaptureChange", Bool_Tag, &ConfigureParams.Screen.bCaptureChange },
	{ "nFramesPerSecond", Int_Tag, &ConfigureParams.Screen.nFramesPerSecond },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save joystick options */
static const struct Config_Tag configs_Joystick0[] =
{
	{ "bCursorEmulation", Bool_Tag, &ConfigureParams.Joysticks.Joy[0].bCursorEmulation },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[0].bEnableAutoFire },
	{ NULL , Error_Tag, NULL }
};
static const struct Config_Tag configs_Joystick1[] =
{
	{ "bCursorEmulation", Bool_Tag, &ConfigureParams.Joysticks.Joy[1].bCursorEmulation },
	{ "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[1].bEnableAutoFire },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save keyboard options */
static const struct Config_Tag configs_Keyboard[] =
{
	{ "bDisableKeyRepeat", Bool_Tag, &ConfigureParams.Keyboard.bDisableKeyRepeat },
	{ "nKeymapType", Int_Tag, &ConfigureParams.Keyboard.nKeymapType },
	{ "szMappingFileName", String_Tag, ConfigureParams.Keyboard.szMappingFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save sound options */
static const struct Config_Tag configs_Sound[] =
{
	{ "bEnableSound", Bool_Tag, &ConfigureParams.Sound.bEnableSound },
	{ "nPlaybackQuality", Int_Tag, &ConfigureParams.Sound.nPlaybackQuality },
	{ "szYMCaptureFileName", String_Tag, ConfigureParams.Sound.szYMCaptureFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save memory options */
static const struct Config_Tag configs_Memory[] =
{
	{ "nMemorySize", Int_Tag, &ConfigureParams.Memory.nMemorySize },
	{ "szMemoryCaptureFileName", String_Tag, ConfigureParams.Memory.szMemoryCaptureFileName },
	{ NULL , Error_Tag, NULL }
};


/* Used to load/save floppy options */
static const struct Config_Tag configs_Floppy[] =
{
	{ "bAutoInsertDiscB", Bool_Tag, &ConfigureParams.DiscImage.bAutoInsertDiscB },
	{ "nWriteProtection", Int_Tag, &ConfigureParams.DiscImage.nWriteProtection },
	{ "szDiscImageDirectory", String_Tag, ConfigureParams.DiscImage.szDiscImageDirectory },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save HD options */
static const struct Config_Tag configs_HardDisc[] =
{
	/*{ "nDriveList", Int_Tag, &ConfigureParams.HardDisc.nDriveList },*/
	{ "bBootFromHardDisc", Bool_Tag, &ConfigureParams.HardDisc.bBootFromHardDisc },
	{ "bUseHardDiscDirectory", Bool_Tag, &ConfigureParams.HardDisc.bUseHardDiscDirectories },
	{ "szHardDiscDirectory", String_Tag, ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_C] },
	/*{ "szHardDiscDirD", String_Tag, ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_D] },*/
	/*{ "szHardDiscDirE", String_Tag, ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_E] },*/
	/*{ "szHardDiscDirF", String_Tag, ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_F] },*/
	{ "bUseHardDiscImage", Bool_Tag, &ConfigureParams.HardDisc.bUseHardDiscImage },
	{ "szHardDiscImage", String_Tag, ConfigureParams.HardDisc.szHardDiscImage },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save ROM options */
static const struct Config_Tag configs_Rom[] =
{
	{ "szTosImageFileName", String_Tag, ConfigureParams.Rom.szTosImageFileName },
	{ "szCartridgeImageFileName", String_Tag, ConfigureParams.Rom.szCartridgeImageFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save RS232 options */
static const struct Config_Tag configs_Rs232[] =
{
	{ "bEnableRS232", Bool_Tag, &ConfigureParams.RS232.bEnableRS232 },
	{ "szOutFileName", String_Tag, ConfigureParams.RS232.szOutFileName },
	{ "szInFileName", String_Tag, ConfigureParams.RS232.szInFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save printer options */
static const struct Config_Tag configs_Printer[] =
{
	{ "bEnablePrinting", Bool_Tag, &ConfigureParams.Printer.bEnablePrinting },
	{ "bPrintToFile", Bool_Tag, &ConfigureParams.Printer.bPrintToFile },
	{ "szPrintToFileName", String_Tag, ConfigureParams.Printer.szPrintToFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save MIDI options */
static const struct Config_Tag configs_Midi[] =
{
	{ "bEnableMidi", Bool_Tag, &ConfigureParams.Midi.bEnableMidi },
	{ "szMidiOutFileName", String_Tag, ConfigureParams.Midi.szMidiOutFileName },
	{ NULL , Error_Tag, NULL }
};

/* Used to load/save system options */
static const struct Config_Tag configs_System[] =
{
	{ "nMinMaxSpeed", Int_Tag, &ConfigureParams.System.nMinMaxSpeed },
	{ "nCpuLevel", Int_Tag, &ConfigureParams.System.nCpuLevel },
	{ "nCpuFreq", Int_Tag, &ConfigureParams.System.nCpuFreq },
	{ "bCompatibleCpu", Bool_Tag, &ConfigureParams.System.bCompatibleCpu },
	{ "nMachineType", Int_Tag, &ConfigureParams.System.nMachineType },
	{ "bBlitter", Bool_Tag, &ConfigureParams.System.bBlitter },
	{ "bRealTimeClock", Bool_Tag, &ConfigureParams.System.bRealTimeClock },
	{ "bPatchTimerD", Bool_Tag, &ConfigureParams.System.bPatchTimerD },
	{ "bSlowFDC", Bool_Tag, &ConfigureParams.System.bSlowFDC },
	{ NULL , Error_Tag, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Set default configuration values.
*/
void Configuration_SetDefault(void)
{
	int i;
	char *homeDir;

	/* Assume first-time install */
	bFirstTimeInstall = TRUE;

	/* Clear parameters */
	memset(&ConfigureParams, 0, sizeof(CNF_PARAMS));

	/* Set defaults for logging */
	strcpy(ConfigureParams.Log.sLogFileName, "stderr");
	ConfigureParams.Log.nTextLogLevel = LOG_INFO;
	ConfigureParams.Log.nAlertDlgLogLevel = LOG_INFO;

	/* Set defaults for (floppy) Disc Image */
	ConfigureParams.DiscImage.bAutoInsertDiscB = TRUE;
	ConfigureParams.DiscImage.nWriteProtection = WRITEPROT_OFF;
	strcpy(ConfigureParams.DiscImage.szDiscImageDirectory, szWorkingDir);
	File_AddSlashToEndFileName(ConfigureParams.DiscImage.szDiscImageDirectory);

	/* Set defaults for Hard Disc */
	ConfigureParams.HardDisc.nDriveList = DRIVELIST_NONE;
	ConfigureParams.HardDisc.bBootFromHardDisc = FALSE;
	ConfigureParams.HardDisc.nHardDiscDir = DRIVE_C;
	ConfigureParams.HardDisc.bUseHardDiscDirectories = FALSE;
	for (i=0; i<MAX_HARDDRIVES; i++)
	{
		strcpy(ConfigureParams.HardDisc.szHardDiscDirectories[i], szWorkingDir);
		File_CleanFileName(ConfigureParams.HardDisc.szHardDiscDirectories[i]);
	}
	ConfigureParams.HardDisc.bUseHardDiscImage = FALSE;
	strcpy(ConfigureParams.HardDisc.szHardDiscImage, szWorkingDir);

	/* Set defaults for Joysticks */
	for (i=0; i<2; i++)
	{
		ConfigureParams.Joysticks.Joy[i].bCursorEmulation = FALSE;
		ConfigureParams.Joysticks.Joy[i].bEnableAutoFire = FALSE;
	}

	/* Set defaults for Keyboard */
	ConfigureParams.Keyboard.bDisableKeyRepeat = TRUE;
	ConfigureParams.Keyboard.nKeymapType = KEYMAP_SYMBOLIC;
	strcpy(ConfigureParams.Keyboard.szMappingFileName, "");

	/* Set defaults for Memory */
	ConfigureParams.Memory.nMemorySize = MEMORY_SIZE_1Mb;
	sprintf(ConfigureParams.Memory.szMemoryCaptureFileName, "%s/hatari.sav", szWorkingDir);

	/* Set defaults for Printer */
	ConfigureParams.Printer.bEnablePrinting = FALSE;
	ConfigureParams.Printer.bPrintToFile = TRUE;
	sprintf(ConfigureParams.Printer.szPrintToFileName, "%s/hatari.prn", szWorkingDir);

	/* Set defaults for RS232 */
	ConfigureParams.RS232.bEnableRS232 = FALSE;
	strcpy(ConfigureParams.RS232.szOutFileName, "/dev/modem");
	strcpy(ConfigureParams.RS232.szInFileName, "/dev/modem");

	/* Set defaults for MIDI */
	ConfigureParams.Midi.bEnableMidi = FALSE;
	strcpy(ConfigureParams.Midi.szMidiOutFileName, "/dev/midi00");

	/* Set defaults for Screen */
	ConfigureParams.Screen.bFullScreen = FALSE;
	ConfigureParams.Screen.bFrameSkip = FALSE;
	ConfigureParams.Screen.bAllowOverscan = TRUE;
	ConfigureParams.Screen.bInterleavedScreen = FALSE;
	ConfigureParams.Screen.ChosenDisplayMode = DISPLAYMODE_HICOL_LOWRES;
	ConfigureParams.Screen.bUseHighRes = FALSE;
	ConfigureParams.Screen.bUseExtVdiResolutions = FALSE;
	ConfigureParams.Screen.nVdiResolution = GEMRES_640x480;
	ConfigureParams.Screen.nVdiColors = GEMCOLOUR_16;
	ConfigureParams.Screen.bCaptureChange = FALSE;
	ConfigureParams.Screen.nFramesPerSecond = 25;

	/* Set defaults for Sound */
	ConfigureParams.Sound.bEnableSound = TRUE;
	ConfigureParams.Sound.nPlaybackQuality = PLAYBACK_MEDIUM;
	sprintf(ConfigureParams.Sound.szYMCaptureFileName, "%s/hatari.wav", szWorkingDir);

	/* Set defaults for Rom */
	sprintf(ConfigureParams.Rom.szTosImageFileName, "%s/tos.img", DATADIR);
	strcpy(ConfigureParams.Rom.szCartridgeImageFileName, "");

	/* Set defaults for System */
	ConfigureParams.System.nCpuLevel = 0;
	ConfigureParams.System.nCpuFreq = 8;
	ConfigureParams.System.bCompatibleCpu = FALSE;
	/*ConfigureParams.System.bAddressSpace24 = TRUE;*/
	ConfigureParams.System.nMachineType = MACHINE_ST;
	ConfigureParams.System.bBlitter = FALSE;
	ConfigureParams.System.bPatchTimerD = TRUE;
	ConfigureParams.System.bRealTimeClock = TRUE;
	ConfigureParams.System.nMinMaxSpeed = MINMAXSPEED_MIN;
	ConfigureParams.System.bSlowFDC = FALSE;

	/* Initialize the configuration file name */
	homeDir = getenv("HOME");
	if (homeDir != NULL && homeDir[0] != 0 && strlen(homeDir) < sizeof(sConfigFileName)-13)
		sprintf(sConfigFileName, "%s/.hatari.cfg", homeDir);
	else
		strcpy(sConfigFileName, "hatari.cfg");
}


/*-----------------------------------------------------------------------*/
/*
  Copy details from configuration structure into global variables for system,
  clean file names, etc...
*/
void Configuration_WorkOnDetails(BOOL bReset)
{
	/* Set resolution change */
	if (bReset)
	{
		bUseVDIRes = ConfigureParams.Screen.bUseExtVdiResolutions;
		bUseHighRes = (!bUseVDIRes && ConfigureParams.Screen.bUseHighRes)
					  || (bUseVDIRes && ConfigureParams.Screen.nVdiColors==GEMCOLOUR_2);
		VDI_SetResolution(ConfigureParams.Screen.nVdiResolution, ConfigureParams.Screen.nVdiColors);
	}

	/* Set playback frequency */
	if (ConfigureParams.Sound.bEnableSound)
		Audio_SetOutputAudioFreq(ConfigureParams.Sound.nPlaybackQuality);

	/* CPU settings */
	if (ConfigureParams.System.nCpuFreq < 12)
	{
		ConfigureParams.System.nCpuFreq = 8;
		nCpuFreqShift = 0;
	}
	else if (ConfigureParams.System.nCpuFreq > 26)
	{
		ConfigureParams.System.nCpuFreq = 32;
		nCpuFreqShift = 2;
	}
	else
	{
		ConfigureParams.System.nCpuFreq = 16;
		nCpuFreqShift = 1;
	}

	/* Clean file and directory names */
	File_MakeAbsoluteName(ConfigureParams.Rom.szTosImageFileName);
	File_MakeAbsoluteName(ConfigureParams.Rom.szCartridgeImageFileName);
	File_MakeAbsoluteName(ConfigureParams.HardDisc.szHardDiscImage);
	File_CleanFileName(ConfigureParams.HardDisc.szHardDiscDirectories[0]);
	File_MakeAbsoluteName(ConfigureParams.HardDisc.szHardDiscDirectories[0]);
	File_MakeAbsoluteName(ConfigureParams.Midi.szMidiOutFileName);
	File_MakeAbsoluteName(ConfigureParams.RS232.szOutFileName);
	File_MakeAbsoluteName(ConfigureParams.RS232.szInFileName);
}


/*-----------------------------------------------------------------------*/
/*
  Load a settings section from the configuration file.
*/
static int Configuration_LoadSection(const char *pFilename, const struct Config_Tag configs[], const char *pSection)
{
	int ret;

	ret = input_config(pFilename, configs, pSection);

	if (ret < 0)
		fprintf(stderr, "Can not load configuration file %s (section %s).\n",
		        sConfigFileName, pSection);

	return ret;
}


/*-----------------------------------------------------------------------*/
/*
  Load program setting from configuration file. If psFileName is NULL, use
  the default (i.e. the users) configuration file.
*/
void Configuration_Load(const char *psFileName)
{
	if (psFileName == NULL)
		psFileName = sConfigFileName;

	if (!File_Exists(psFileName))
	{
		fprintf(stderr, "Configuration file %s not found.\n", psFileName);
		return;
	}

	bFirstTimeInstall = FALSE;

	Configuration_LoadSection(psFileName, configs_Log, "[Log]");
	Configuration_LoadSection(psFileName, configs_Screen, "[Screen]");
	Configuration_LoadSection(psFileName, configs_Joystick0, "[Joystick0]");
	Configuration_LoadSection(psFileName, configs_Joystick1, "[Joystick1]");
	Configuration_LoadSection(psFileName, configs_Keyboard, "[Keyboard]");
	Configuration_LoadSection(psFileName, configs_Sound, "[Sound]");
	Configuration_LoadSection(psFileName, configs_Memory, "[Memory]");
	Configuration_LoadSection(psFileName, configs_Floppy, "[Floppy]");
	Configuration_LoadSection(psFileName, configs_HardDisc, "[HardDisc]");
	Configuration_LoadSection(psFileName, configs_Rom, "[ROM]");
	Configuration_LoadSection(psFileName, configs_Rs232, "[RS232]");
	Configuration_LoadSection(psFileName, configs_Printer, "[Printer]");
	Configuration_LoadSection(psFileName, configs_Midi, "[Midi]");
	Configuration_LoadSection(psFileName, configs_System, "[System]");

	/* Copy details to global variables */
	cpu_level = ConfigureParams.System.nCpuLevel;
	cpu_compatible = ConfigureParams.System.bCompatibleCpu;

	Configuration_WorkOnDetails(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Save a settings section to configuration file
*/
static int Configuration_SaveSection(const char *pFilename, const struct Config_Tag configs[], const char *pSection)
{
	int ret;

	ret = update_config(pFilename, configs, pSection);

	if (ret < 0)
		fprintf(stderr, "Error while updating section %s\n", pSection);

	return ret;
}


/*-----------------------------------------------------------------------*/
/*
  Save program setting to configuration file
*/
void Configuration_Save(void)
{
	if (Configuration_SaveSection(sConfigFileName, configs_Log, "[Log]") < 0)
	{
		fprintf(stderr, "Error saving config file.\n");
		return;
	}
	Configuration_SaveSection(sConfigFileName, configs_Screen, "[Screen]");
	Configuration_SaveSection(sConfigFileName, configs_Joystick0, "[Joystick0]");
	Configuration_SaveSection(sConfigFileName, configs_Joystick1, "[Joystick1]");
	Configuration_SaveSection(sConfigFileName, configs_Keyboard, "[Keyboard]");
	Configuration_SaveSection(sConfigFileName, configs_Sound, "[Sound]");
	Configuration_SaveSection(sConfigFileName, configs_Memory, "[Memory]");
	Configuration_SaveSection(sConfigFileName, configs_Floppy, "[Floppy]");
	Configuration_SaveSection(sConfigFileName, configs_HardDisc, "[HardDisc]");
	Configuration_SaveSection(sConfigFileName, configs_Rom, "[ROM]");
	Configuration_SaveSection(sConfigFileName, configs_Rs232, "[RS232]");
	Configuration_SaveSection(sConfigFileName, configs_Printer, "[Printer]");
	Configuration_SaveSection(sConfigFileName, configs_Midi, "[Midi]");
	Configuration_SaveSection(sConfigFileName, configs_System, "[System]");
}

