/*
  Hatari - configuration.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Configuration File

  The configuration file is stored in a binary format to prevent tampering.
  We also store the version number in the file to prevent people from
  copying old .cfg files between versions.
*/
static char rcsid[] = "Hatari $Id: configuration.c,v 1.14 2003-03-23 23:07:28 thothy Exp $";

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


static FILE *ConfigFile;
BOOL bFirstTimeInstall=FALSE;    /* Has been run before? Used to set default joysticks etc... */


/*-----------------------------------------------------------------------*/
/*
  Set default configuration values.
*/
void Configuration_SetDefault(void)
{
  int i;

  /* Clear parameters */
  Memory_Clear(&ConfigureParams, sizeof(DLG_PARAMS));

  /* Set defaults for CPU dialog */
  ConfigureParams.Configure.nMinMaxSpeed = ConfigureParams.Configure.nPrevMinMaxSpeed = MINMAXSPEED_MIN;

  /* Set defaults for Disc Image */
  ConfigureParams.DiscImage.bAutoInsertDiscB = TRUE;
  strcpy(ConfigureParams.DiscImage.szDiscImageDirectory, szWorkingDir);
  File_AddSlashToEndFileName(ConfigureParams.DiscImage.szDiscImageDirectory);

  /* Set defaults for Hard Disc */
  ConfigureParams.HardDisc.nDriveList = DRIVELIST_NONE;
  ConfigureParams.HardDisc.bBootFromHardDisc = FALSE;
  ConfigureParams.HardDisc.nHardDiscDir = DRIVE_C;
  for(i=0; i<MAX_HARDDRIVES; i++)
  {
    strcpy(ConfigureParams.HardDisc.szHardDiscDirectories[i], szWorkingDir);
    File_CleanFileName(ConfigureParams.HardDisc.szHardDiscDirectories[i]);
  }
  strcpy(ConfigureParams.HardDisc.szHardDiscImage, szWorkingDir);

  /* Set defaults for Joysticks */
  for(i=0; i<2; i++)
  {
    ConfigureParams.Joysticks.Joy[i].bCursorEmulation = FALSE;
    ConfigureParams.Joysticks.Joy[i].bEnableAutoFire = FALSE;
  }

  /* Set defaults for Keyboard */
  ConfigureParams.Keyboard.bDisableKeyRepeat = TRUE;
  ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F11][SHORT_CUT_KEY] = SHORTCUT_FULLSCREEN;
  ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F11][SHORT_CUT_SHIFT] = SHORTCUT_NOTASSIGNED;
  ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F11][SHORT_CUT_CTRL] = SHORTCUT_NOTASSIGNED;
  ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F12][SHORT_CUT_KEY] = SHORTCUT_NOTASSIGNED;
  ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F12][SHORT_CUT_SHIFT] = SHORTCUT_NOTASSIGNED;
  ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F12][SHORT_CUT_CTRL] = SHORTCUT_NOTASSIGNED;
  strcpy(ConfigureParams.Keyboard.szMappingFileName, "");

  /* Set defaults for Memory */
  ConfigureParams.Memory.nMemorySize = MEMORY_SIZE_1Mb;
  strcpy(ConfigureParams.Memory.szMemoryCaptureFileName, "");

  /* Set defaults for Printer */
  ConfigureParams.Printer.bEnablePrinting = FALSE;
  ConfigureParams.Printer.bPrintToFile = FALSE;
  strcpy(ConfigureParams.Printer.szPrintToFileName,"");

  /* Set defaults for RS232 */
  ConfigureParams.RS232.bEnableRS232 = FALSE;
  ConfigureParams.RS232.nCOMPort = COM_PORT_1;

  /* Set defaults for Screen */
  ConfigureParams.Screen.bFullScreen = FALSE;
  ConfigureParams.Screen.Advanced.bDoubleSizeWindow = FALSE;
  ConfigureParams.Screen.Advanced.bAllowOverscan = TRUE;
  ConfigureParams.Screen.Advanced.bInterlacedFullScreen = TRUE;
  ConfigureParams.Screen.Advanced.bSyncToRetrace = FALSE;
  ConfigureParams.Screen.Advanced.bFrameSkip = FALSE;	
  ConfigureParams.Screen.ChosenDisplayMode = DISPLAYMODE_HICOL_LOWRES;
  ConfigureParams.Screen.bCaptureChange = FALSE;
  ConfigureParams.Screen.nFramesPerSecond = 1;
  ConfigureParams.Screen.bUseHighRes = FALSE;

  /* Set defaults for Sound */
  ConfigureParams.Sound.bEnableSound = TRUE;
  ConfigureParams.Sound.nPlaybackQuality = PLAYBACK_MEDIUM;
  strcpy(ConfigureParams.Sound.szYMCaptureFileName, "");

  /* Set defaults for TOSGEM */
  sprintf(ConfigureParams.TOSGEM.szTOSImageFileName, "%s/tos.img", DATADIR);
  ConfigureParams.TOSGEM.bUseTimeDate = FALSE;
  ConfigureParams.TOSGEM.bAccGEMGraphics = FALSE;
  ConfigureParams.TOSGEM.bUseExtGEMResolutions = FALSE;
  ConfigureParams.TOSGEM.nGEMResolution = GEMRES_640x480;
  ConfigureParams.TOSGEM.nGEMColours = GEMCOLOUR_16;

  /* Set defaults for System */
  ConfigureParams.System.nCpuLevel = 0;
  ConfigureParams.System.bCompatibleCpu = FALSE;
  ConfigureParams.System.bAddressSpace24 = TRUE;
  ConfigureParams.System.bBlitter = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Load program setting from configuration file
*/
void Configuration_Init(void)
{

  char sVersionString[VERSION_STRING_SIZE];
  int i,j;

  /* Open configuration file */
  if (Configuration_OpenFileToRead()) {
    /* Version, check matches */
    Configuration_ReadFromFile(sVersionString,VERSION_STRING_SIZE);
    if (memcmp(sVersionString,VERSION_STRING,VERSION_STRING_SIZE)==0) {
      /* Configure */
      Configuration_ReadFromFile(&ConfigureParams.Configure.nMinMaxSpeed,4);
      /* Screen */
      Configuration_ReadFromFile(&ConfigureParams.Screen.bFullScreen,4);
      Configuration_ReadFromFile(&ConfigureParams.Screen.Advanced.bDoubleSizeWindow,4);
      Configuration_ReadFromFile(&ConfigureParams.Screen.Advanced.bAllowOverscan,4);
      Configuration_ReadFromFile(&ConfigureParams.Screen.Advanced.bInterlacedFullScreen,4);
      Configuration_ReadFromFile(&ConfigureParams.Screen.Advanced.bSyncToRetrace,4);
      Configuration_ReadFromFile(&ConfigureParams.Screen.ChosenDisplayMode,4);
      Configuration_ReadFromFile(&ConfigureParams.Screen.bCaptureChange,4);
      Configuration_ReadFromFile(&ConfigureParams.Screen.nFramesPerSecond,4);
      Configuration_ReadFromFile(&ConfigureParams.Screen.bUseHighRes,4);
      /* Joysticks */
      Configuration_ReadFromFile(&ConfigureParams.Joysticks.Joy[0].bCursorEmulation,4);
      Configuration_ReadFromFile(&ConfigureParams.Joysticks.Joy[0].bEnableAutoFire,4);
      Configuration_ReadFromFile(&ConfigureParams.Joysticks.Joy[1].bCursorEmulation,4);
      Configuration_ReadFromFile(&ConfigureParams.Joysticks.Joy[1].bEnableAutoFire,4);
      /* Keyboard */
      Configuration_ReadFromFile(&ConfigureParams.Keyboard.bDisableKeyRepeat,4);
      Configuration_ReadFromFile(&ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F11][SHORT_CUT_SHIFT],4);
      Configuration_ReadFromFile(&ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F11][SHORT_CUT_CTRL],4);
      Configuration_ReadFromFile(&ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F12][SHORT_CUT_SHIFT],4);
      Configuration_ReadFromFile(&ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F12][SHORT_CUT_CTRL],4);
      Configuration_ReadFromFile(ConfigureParams.Keyboard.szMappingFileName,sizeof(ConfigureParams.Keyboard.szMappingFileName));
      /* Sound */
      Configuration_ReadFromFile(&ConfigureParams.Sound.bEnableSound,4);
      Configuration_ReadFromFile(&ConfigureParams.Sound.nPlaybackQuality,4);
      Configuration_ReadFromFile(ConfigureParams.Sound.szYMCaptureFileName,sizeof(ConfigureParams.Sound.szYMCaptureFileName));
      /* Memory */
      Configuration_ReadFromFile(&ConfigureParams.Memory.nMemorySize,4);
      Configuration_ReadFromFile(ConfigureParams.Memory.szMemoryCaptureFileName,sizeof(ConfigureParams.Memory.szMemoryCaptureFileName));
      /* DiscImage */
      Configuration_ReadFromFile(&ConfigureParams.DiscImage.bAutoInsertDiscB,4);
      Configuration_ReadFromFile(ConfigureParams.DiscImage.szDiscImageDirectory,sizeof(ConfigureParams.DiscImage.szDiscImageDirectory));
      /* HardDisc */
      Configuration_ReadFromFile(&ConfigureParams.HardDisc.nDriveList,4);
      Configuration_ReadFromFile(&ConfigureParams.HardDisc.bBootFromHardDisc,4);
    /* The hard disk configuration is not saved, because it seems to crash
       some games which boot from a floppy, and because having a mounted hard
       disk makes the boot much slower for now (in tos 2.x) */

      // fprintf(stderr,"drive C before %s size %d\n",ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_C], sizeof(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_C]));
/*       Configuration_ReadFromFile(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_C],sizeof(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_C])); */
/*       Configuration_ReadFromFile(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_D],sizeof(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_D])); */
/*       Configuration_ReadFromFile(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_E],sizeof(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_E])); */
/*       Configuration_ReadFromFile(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_F],sizeof(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_F])); */
/*       fprintf(stderr,"drive C after %s\n",ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_C]); */
      // GemDOS_InitDrives();

      /* TOSGEM */
      Configuration_ReadFromFile(ConfigureParams.TOSGEM.szTOSImageFileName,sizeof(ConfigureParams.TOSGEM.szTOSImageFileName));
      Configuration_ReadFromFile(&ConfigureParams.TOSGEM.bUseTimeDate,4);
      Configuration_ReadFromFile(&ConfigureParams.TOSGEM.bAccGEMGraphics,4);
      Configuration_ReadFromFile(&ConfigureParams.TOSGEM.bUseExtGEMResolutions,4);
      Configuration_ReadFromFile(&ConfigureParams.TOSGEM.nGEMResolution,4);
      Configuration_ReadFromFile(&ConfigureParams.TOSGEM.nGEMColours,4);
      /* RS232 */
      Configuration_ReadFromFile(&ConfigureParams.RS232.bEnableRS232,4);
      Configuration_ReadFromFile(&ConfigureParams.RS232.nCOMPort,4);
      /* Printer */
      Configuration_ReadFromFile(&ConfigureParams.Printer.bEnablePrinting,4);
      Configuration_ReadFromFile(&ConfigureParams.Printer.bPrintToFile,4);
      Configuration_ReadFromFile(ConfigureParams.Printer.szPrintToFileName,sizeof(ConfigureParams.Printer.szPrintToFileName));
      /* System */
      Configuration_ReadFromFile(&ConfigureParams.System.nCpuLevel,4);
      Configuration_ReadFromFile(&ConfigureParams.System.bCompatibleCpu,4);
      Configuration_ReadFromFile(&ConfigureParams.System.bBlitter,4);

      bEnableBlitter = ConfigureParams.System.bBlitter;
      cpu_level = ConfigureParams.System.nCpuLevel;
      cpu_compatible = ConfigureParams.System.bCompatibleCpu;

      bUseVDIRes = ConfigureParams.TOSGEM.bUseExtGEMResolutions;
      bUseHighRes = ConfigureParams.Screen.bUseHighRes || (bUseVDIRes && (ConfigureParams.TOSGEM.nGEMColours==GEMCOLOUR_2));
    }

    /* And close up */
    Configuration_CloseFile();
  }
  else {
    /* No configuration file, assume first-time install */
    bFirstTimeInstall = TRUE;
  }

  /* Copy details to globals, TRUE */
  Dialog_CopyDetailsFromConfiguration(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Save program setting to configuration file
*/
void Configuration_UnInit(void)
{
  int i,j;

  /* Open configuration file */
  if (Configuration_OpenFileToWrite()) {
    /* Version */
    Configuration_WriteToFile(VERSION_STRING,VERSION_STRING_SIZE);
    /* Configure */
    Configuration_WriteToFile(&ConfigureParams.Configure.nMinMaxSpeed,4);
    ConfigureParams.Configure.nPrevMinMaxSpeed = ConfigureParams.Configure.nMinMaxSpeed;
    /* Screen */
    Configuration_WriteToFile(&ConfigureParams.Screen.bFullScreen,4);
    Configuration_WriteToFile(&ConfigureParams.Screen.Advanced.bDoubleSizeWindow,4);
    Configuration_WriteToFile(&ConfigureParams.Screen.Advanced.bAllowOverscan,4);
    Configuration_WriteToFile(&ConfigureParams.Screen.Advanced.bInterlacedFullScreen,4);
    Configuration_WriteToFile(&ConfigureParams.Screen.Advanced.bSyncToRetrace,4);
    Configuration_WriteToFile(&ConfigureParams.Screen.ChosenDisplayMode,4);
    Configuration_WriteToFile(&ConfigureParams.Screen.bCaptureChange,4);
    Configuration_WriteToFile(&ConfigureParams.Screen.nFramesPerSecond,4);
    Configuration_WriteToFile(&ConfigureParams.Screen.bUseHighRes,4);
    /* Joysticks */
    Configuration_WriteToFile(&ConfigureParams.Joysticks.Joy[0].bCursorEmulation,4);
    Configuration_WriteToFile(&ConfigureParams.Joysticks.Joy[0].bEnableAutoFire,4);
    Configuration_WriteToFile(&ConfigureParams.Joysticks.Joy[1].bCursorEmulation,4);
    Configuration_WriteToFile(&ConfigureParams.Joysticks.Joy[1].bEnableAutoFire,4);
    /* Keyboard */
    Configuration_WriteToFile(&ConfigureParams.Keyboard.bDisableKeyRepeat,4);
    Configuration_WriteToFile(&ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F11][SHORT_CUT_SHIFT],4);
    Configuration_WriteToFile(&ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F11][SHORT_CUT_CTRL],4);
    Configuration_WriteToFile(&ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F12][SHORT_CUT_SHIFT],4);
    Configuration_WriteToFile(&ConfigureParams.Keyboard.ShortCuts[SHORT_CUT_F12][SHORT_CUT_CTRL],4);
    Configuration_WriteToFile(ConfigureParams.Keyboard.szMappingFileName,sizeof(ConfigureParams.Keyboard.szMappingFileName));
    /* Sound */
    Configuration_WriteToFile(&ConfigureParams.Sound.bEnableSound,4);
    Configuration_WriteToFile(&ConfigureParams.Sound.nPlaybackQuality,4);
    Configuration_WriteToFile(ConfigureParams.Sound.szYMCaptureFileName,sizeof(ConfigureParams.Sound.szYMCaptureFileName));
    /* Memory */
    Configuration_WriteToFile(&ConfigureParams.Memory.nMemorySize,4);
    Configuration_WriteToFile(ConfigureParams.Memory.szMemoryCaptureFileName,sizeof(ConfigureParams.Memory.szMemoryCaptureFileName));
    /* DiscImage */
    Configuration_WriteToFile(&ConfigureParams.DiscImage.bAutoInsertDiscB,4);
    Configuration_WriteToFile(ConfigureParams.DiscImage.szDiscImageDirectory,sizeof(ConfigureParams.DiscImage.szDiscImageDirectory));
    /* HardDisc */
    Configuration_WriteToFile(&ConfigureParams.HardDisc.nDriveList,4);
    Configuration_WriteToFile(&ConfigureParams.HardDisc.bBootFromHardDisc,4);
/*     Configuration_WriteToFile(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_C],sizeof(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_C])); */
/*     Configuration_WriteToFile(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_D],sizeof(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_D])); */
/*     Configuration_WriteToFile(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_E],sizeof(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_E])); */
/*     Configuration_WriteToFile(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_F],sizeof(ConfigureParams.HardDisc.szHardDiscDirectories[DRIVE_F])); */
    /* TOSGEM */
    Configuration_WriteToFile(ConfigureParams.TOSGEM.szTOSImageFileName,sizeof(ConfigureParams.TOSGEM.szTOSImageFileName));
    Configuration_WriteToFile(&ConfigureParams.TOSGEM.bUseTimeDate,4);
    Configuration_WriteToFile(&ConfigureParams.TOSGEM.bAccGEMGraphics,4);
    Configuration_WriteToFile(&ConfigureParams.TOSGEM.bUseExtGEMResolutions,4);
    Configuration_WriteToFile(&ConfigureParams.TOSGEM.nGEMResolution,4);
    Configuration_WriteToFile(&ConfigureParams.TOSGEM.nGEMColours,4);
    /* RS232 */
    Configuration_WriteToFile(&ConfigureParams.RS232.bEnableRS232,4);
    Configuration_WriteToFile(&ConfigureParams.RS232.nCOMPort,4);
    /* Printer */
    Configuration_WriteToFile(&ConfigureParams.Printer.bEnablePrinting,4);
    Configuration_WriteToFile(&ConfigureParams.Printer.bPrintToFile,4);
    Configuration_WriteToFile(ConfigureParams.Printer.szPrintToFileName,sizeof(ConfigureParams.Printer.szPrintToFileName));
    /* System */
    Configuration_WriteToFile(&ConfigureParams.System.nCpuLevel,4);
    Configuration_WriteToFile(&ConfigureParams.System.bCompatibleCpu,4);
    Configuration_WriteToFile(&ConfigureParams.System.bBlitter,4);

    /* And close up */
    Configuration_CloseFile();
  }
}


/*-----------------------------------------------------------------------*/
/*
  Open configuration file to write to
*/
BOOL Configuration_OpenFileToWrite(void)
{
  char szString[MAX_FILENAME_LENGTH];

  /* Create file */
  sprintf(szString,"%s/hatari.cfg",szWorkingDir);
  ConfigFile = fopen(szString, "wb");
  if (ConfigFile!=NULL)
    return(TRUE);

  /* Whoops, error */
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Open configuration file for reading
*/
BOOL Configuration_OpenFileToRead(void)
{
  char szString[MAX_FILENAME_LENGTH];

  /* Create file */
  sprintf(szString,"%s/hatari.cfg",szWorkingDir);
  ConfigFile = fopen(szString, "rb");
  if (ConfigFile!=NULL)
    return(TRUE);

  /* Whoops, error */
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Close configuration
*/
void Configuration_CloseFile(void)
{
  fclose(ConfigFile);
}


/*-----------------------------------------------------------------------*/
/*
  Write entry to configuration file
*/
void Configuration_WriteToFile(void *pData,int nBytes)
{
  fwrite(pData, 1, nBytes, ConfigFile);
}


/*-----------------------------------------------------------------------*/
/*
  Read entry from configuration file
*/
void Configuration_ReadFromFile(void *pData,int nBytes)
{
  fread(pData, 1, nBytes, ConfigFile);
}
