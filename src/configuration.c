/*
  Hatari - configuration.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Configuration File

  The configuration file is now stored in an ASCII format to allow the user
  to edit the file manually.
*/
char Configuration_rcsid[] = "Hatari $Id: configuration.c,v 1.28 2004-04-06 16:20:15 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "video.h"
#include "vdi.h"
#include "screen.h"
#include "shortcut.h"
#include "memAlloc.h"
#include "file.h"
#include "uae-cpu/hatari-glue.h"
#include "intercept.h"
#include "gemdos.h"
#include "cfgopts.h"


BOOL bFirstTimeInstall = FALSE;             /* Has been run before? Used to set default joysticks etc... */
CNF_PARAMS ConfigureParams;                 /* List of configuration for the emulator */
char sConfigFileName[FILENAME_MAX];         /* Stores the name of the configuration file */


/* Used to load/save screen options */
struct Config_Tag configs_Screen[] =
{
  { "bFullScreen", Bool_Tag, &ConfigureParams.Screen.bFullScreen },
  { "bDoubleSizeWindow", Bool_Tag, &ConfigureParams.Screen.bDoubleSizeWindow },
  { "bAllowOverscan", Bool_Tag, &ConfigureParams.Screen.bAllowOverscan },
  { "bInterlacedScreen", Bool_Tag, &ConfigureParams.Screen.bInterlacedScreen },
  /*{ "bSyncToRetrace", Bool_Tag, &ConfigureParams.Screen.bSyncToRetrace },*/
  { "bFrameSkip", Bool_Tag, &ConfigureParams.Screen.bFrameSkip },
  { "ChosenDisplayMode", Int_Tag, &ConfigureParams.Screen.ChosenDisplayMode },
  { "bCaptureChange", Bool_Tag, &ConfigureParams.Screen.bCaptureChange },
  { "nFramesPerSecond", Int_Tag, &ConfigureParams.Screen.nFramesPerSecond },
  { "bUseHighRes", Bool_Tag, &ConfigureParams.Screen.bUseHighRes },
  { NULL , Error_Tag, NULL }
};

/* Used to load/save joystick options */
struct Config_Tag configs_Joystick0[] =
{
  { "bCursorEmulation", Bool_Tag, &ConfigureParams.Joysticks.Joy[0].bCursorEmulation },
  { "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[0].bEnableAutoFire },
  { NULL , Error_Tag, NULL }
};
struct Config_Tag configs_Joystick1[] =
{
  { "bCursorEmulation", Bool_Tag, &ConfigureParams.Joysticks.Joy[1].bCursorEmulation },
  { "bEnableAutoFire", Bool_Tag, &ConfigureParams.Joysticks.Joy[1].bEnableAutoFire },
  { NULL , Error_Tag, NULL }
};

/* Used to load/save keyboard options */
struct Config_Tag configs_Keyboard[] =
{
  { "bDisableKeyRepeat", Bool_Tag, &ConfigureParams.Keyboard.bDisableKeyRepeat },
  { "nKeymapType", Int_Tag, &ConfigureParams.Keyboard.nKeymapType },
  { "szMappingFileName", String_Tag, ConfigureParams.Keyboard.szMappingFileName },
  { NULL , Error_Tag, NULL }
};

/* Used to load/save sound options */
struct Config_Tag configs_Sound[] =
{
  { "bEnableSound", Bool_Tag, &ConfigureParams.Sound.bEnableSound },
  { "nPlaybackQuality", Int_Tag, &ConfigureParams.Sound.nPlaybackQuality },
  { "szYMCaptureFileName", String_Tag, ConfigureParams.Sound.szYMCaptureFileName },
  { NULL , Error_Tag, NULL }
};

/* Used to load/save memory options */
struct Config_Tag configs_Memory[] =
{
  { "nMemorySize", Int_Tag, &ConfigureParams.Memory.nMemorySize },
  { "szMemoryCaptureFileName", String_Tag, ConfigureParams.Memory.szMemoryCaptureFileName },
  { NULL , Error_Tag, NULL }
};


/* Used to load/save floppy options */
struct Config_Tag configs_Floppy[] =
{
  { "bAutoInsertDiscB", Bool_Tag, &ConfigureParams.DiscImage.bAutoInsertDiscB },
  { "szDiscImageDirectory", String_Tag, ConfigureParams.DiscImage.szDiscImageDirectory },
  { NULL , Error_Tag, NULL }
};

/* Used to load/save HD options */
struct Config_Tag configs_HardDisc[] =
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

/* Used to load/save TOS/GEM options */
struct Config_Tag configs_TosGem[] =
{
  { "szTOSImageFileName", String_Tag, ConfigureParams.TOSGEM.szTOSImageFileName },
  { "bUseExtGEMResolutions", Bool_Tag, &ConfigureParams.TOSGEM.bUseExtGEMResolutions },
  { "nGEMResolution", Int_Tag, &ConfigureParams.TOSGEM.nGEMResolution },
  { "nGEMColours", Int_Tag, &ConfigureParams.TOSGEM.nGEMColours },
  { NULL , Error_Tag, NULL }
};

/* Used to load/save RS232 options */
struct Config_Tag configs_Rs232[] =
{
  { "bEnableRS232", Bool_Tag, &ConfigureParams.RS232.bEnableRS232 },
  { "szDeviceFileName", String_Tag, ConfigureParams.RS232.szDeviceFileName },
  { NULL , Error_Tag, NULL }
};

/* Used to load/save printer options */
struct Config_Tag configs_Printer[] =
{
  { "bEnablePrinting", Bool_Tag, &ConfigureParams.Printer.bEnablePrinting },
  { "bPrintToFile", Bool_Tag, &ConfigureParams.Printer.bPrintToFile },
  { "szPrintToFileName", String_Tag, ConfigureParams.Printer.szPrintToFileName },
  { NULL , Error_Tag, NULL }
};

/* Used to load/save MIDI options */
struct Config_Tag configs_Midi[] =
{
  { "bEnableMidi", Bool_Tag, &ConfigureParams.Midi.bEnableMidi },
  { "szMidiOutFileName", String_Tag, ConfigureParams.Midi.szMidiOutFileName },
  { NULL , Error_Tag, NULL }
};

/* Used to load/save system options */
struct Config_Tag configs_System[] =
{
  { "nMinMaxSpeed", Int_Tag, &ConfigureParams.System.nMinMaxSpeed },
  { "nCpuLevel", Int_Tag, &ConfigureParams.System.nCpuLevel },
  { "bCompatibleCpu", Bool_Tag, &ConfigureParams.System.bCompatibleCpu },
  { "bBlitter", Bool_Tag, &ConfigureParams.System.bBlitter },
  { "bPatchTimerD", Bool_Tag, &ConfigureParams.System.bPatchTimerD },
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

  /* Clear parameters */
  Memory_Clear(&ConfigureParams, sizeof(CNF_PARAMS));

  /* Set defaults for Disc Image */
  ConfigureParams.DiscImage.bAutoInsertDiscB = TRUE;
  strcpy(ConfigureParams.DiscImage.szDiscImageDirectory, szWorkingDir);
  File_AddSlashToEndFileName(ConfigureParams.DiscImage.szDiscImageDirectory);

  /* Set defaults for Hard Disc */
  ConfigureParams.HardDisc.nDriveList = DRIVELIST_NONE;
  ConfigureParams.HardDisc.bBootFromHardDisc = FALSE;
  ConfigureParams.HardDisc.nHardDiscDir = DRIVE_C;
  ConfigureParams.HardDisc.bUseHardDiscDirectories = FALSE;
  for(i=0; i<MAX_HARDDRIVES; i++)
  {
    strcpy(ConfigureParams.HardDisc.szHardDiscDirectories[i], szWorkingDir);
    File_CleanFileName(ConfigureParams.HardDisc.szHardDiscDirectories[i]);
  }
  ConfigureParams.HardDisc.bUseHardDiscImage = FALSE;
  strcpy(ConfigureParams.HardDisc.szHardDiscImage, szWorkingDir);

  /* Set defaults for Joysticks */
  for(i=0; i<2; i++)
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
  sprintf(ConfigureParams.RS232.szDeviceFileName, "%s/hatari.ser", szWorkingDir);

  /* Set defaults for MIDI */
  ConfigureParams.Midi.bEnableMidi = FALSE;
  strcpy(ConfigureParams.Midi.szMidiOutFileName, "/dev/midi00");

  /* Set defaults for Screen */
  ConfigureParams.Screen.bFullScreen = FALSE;
  ConfigureParams.Screen.bDoubleSizeWindow = FALSE;
  ConfigureParams.Screen.bAllowOverscan = TRUE;
  ConfigureParams.Screen.bInterlacedScreen = FALSE;
  ConfigureParams.Screen.bSyncToRetrace = FALSE;
  ConfigureParams.Screen.bFrameSkip = FALSE;
  ConfigureParams.Screen.ChosenDisplayMode = DISPLAYMODE_HICOL_LOWRES;
  ConfigureParams.Screen.bCaptureChange = FALSE;
  ConfigureParams.Screen.nFramesPerSecond = 25;
  ConfigureParams.Screen.bUseHighRes = FALSE;

  /* Set defaults for Sound */
  ConfigureParams.Sound.bEnableSound = TRUE;
  ConfigureParams.Sound.nPlaybackQuality = PLAYBACK_MEDIUM;
  sprintf(ConfigureParams.Sound.szYMCaptureFileName, "%s/hatari.wav", szWorkingDir);

  /* Set defaults for TOSGEM */
  sprintf(ConfigureParams.TOSGEM.szTOSImageFileName, "%s/tos.img", DATADIR);
  ConfigureParams.TOSGEM.bUseExtGEMResolutions = FALSE;
  ConfigureParams.TOSGEM.nGEMResolution = GEMRES_640x480;
  ConfigureParams.TOSGEM.nGEMColours = GEMCOLOUR_16;

  /* Set defaults for System */
  ConfigureParams.System.nCpuLevel = 0;
  ConfigureParams.System.bCompatibleCpu = FALSE;
  ConfigureParams.System.bAddressSpace24 = TRUE;
  ConfigureParams.System.bBlitter = FALSE;
  ConfigureParams.System.bPatchTimerD = TRUE;
  ConfigureParams.System.nMinMaxSpeed = MINMAXSPEED_MIN;

  /* Initialize the configuration file name */
  homeDir = getenv("HOME");
  if(homeDir != NULL && homeDir[0] != 0 && strlen(homeDir) < sizeof(sConfigFileName)-13)
    sprintf(sConfigFileName, "%s/.hatari.cfg", homeDir);
  else
    strcpy(sConfigFileName, "hatari.cfg");
}


/*-----------------------------------------------------------------------*/
/*
  Load a settings section from the configuration file.
*/
static int Configuration_LoadSection(const char *pFilename, struct Config_Tag configs[], char *pSection)
{
  int ret;

  ret = input_config(pFilename, configs, pSection);

   if(ret < 0)
     fprintf(stderr, "Can not load configuration file %s (section %s).\n",
             sConfigFileName, pSection);

  return ret;
}


/*-----------------------------------------------------------------------*/
/*
  Load program setting from configuration file
*/
void Configuration_Load(void)
{
  char sVersionString[VERSION_STRING_SIZE];
  int i,j;

  if (!File_Exists(sConfigFileName))
  {
    /* No configuration file, assume first-time install */
    bFirstTimeInstall = TRUE;
    fprintf(stderr, "Configuration file %s not found.\n", sConfigFileName);
    return;
  }

  bFirstTimeInstall = FALSE;

  Configuration_LoadSection(sConfigFileName, configs_Screen, "[Screen]");
  Configuration_LoadSection(sConfigFileName, configs_Joystick0, "[Joystick0]");
  Configuration_LoadSection(sConfigFileName, configs_Joystick1, "[Joystick1]");
  Configuration_LoadSection(sConfigFileName, configs_Keyboard, "[Keyboard]");
  Configuration_LoadSection(sConfigFileName, configs_Sound, "[Sound]");
  Configuration_LoadSection(sConfigFileName, configs_Memory, "[Memory]");
  Configuration_LoadSection(sConfigFileName, configs_Floppy, "[Floppy]");
  Configuration_LoadSection(sConfigFileName, configs_HardDisc, "[HardDisc]");
  Configuration_LoadSection(sConfigFileName, configs_TosGem, "[TOS-GEM]");
  Configuration_LoadSection(sConfigFileName, configs_Rs232, "[RS232]");
  Configuration_LoadSection(sConfigFileName, configs_Printer, "[Printer]");
  Configuration_LoadSection(sConfigFileName, configs_Midi, "[Midi]");
  Configuration_LoadSection(sConfigFileName, configs_System, "[System]");

  /* Copy details to global variables */
  bEnableBlitter = ConfigureParams.System.bBlitter;
  cpu_level = ConfigureParams.System.nCpuLevel;
  cpu_compatible = ConfigureParams.System.bCompatibleCpu;

  bUseVDIRes = ConfigureParams.TOSGEM.bUseExtGEMResolutions;
  bUseHighRes = ConfigureParams.Screen.bUseHighRes || (bUseVDIRes && (ConfigureParams.TOSGEM.nGEMColours==GEMCOLOUR_2));

  Dialog_CopyDetailsFromConfiguration(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Save a settings section to configuration file
*/
static int Configuration_SaveSection(const char *pFilename, struct Config_Tag configs[], char *pSection)
{
  int ret;

  ret = update_config(pFilename, configs, pSection);

   if(ret < 0)
     fprintf(stderr, "Error while updating section %s\n", pSection);

  return ret;
}


/*-----------------------------------------------------------------------*/
/*
  Save program setting to configuration file
*/
void Configuration_Save(void)
{
  int i,j;

  if(Configuration_SaveSection(sConfigFileName, configs_Screen, "[Screen]") < 0)
  {
    fprintf(stderr, "Error saving config file.\n");
    return;
  }
  Configuration_SaveSection(sConfigFileName, configs_Joystick0, "[Joystick0]");
  Configuration_SaveSection(sConfigFileName, configs_Joystick1, "[Joystick1]");
  Configuration_SaveSection(sConfigFileName, configs_Keyboard, "[Keyboard]");
  Configuration_SaveSection(sConfigFileName, configs_Sound, "[Sound]");
  Configuration_SaveSection(sConfigFileName, configs_Memory, "[Memory]");
  Configuration_SaveSection(sConfigFileName, configs_Floppy, "[Floppy]");
  Configuration_SaveSection(sConfigFileName, configs_HardDisc, "[HardDisc]");
  Configuration_SaveSection(sConfigFileName, configs_TosGem, "[TOS-GEM]");
  Configuration_SaveSection(sConfigFileName, configs_Rs232, "[RS232]");
  Configuration_SaveSection(sConfigFileName, configs_Printer, "[Printer]");
  Configuration_SaveSection(sConfigFileName, configs_Midi, "[Midi]");
  Configuration_SaveSection(sConfigFileName, configs_System, "[System]");
}

