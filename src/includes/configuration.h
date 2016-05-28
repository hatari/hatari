/*
  Hatari - configuration.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CONFIGURATION_H
#define HATARI_CONFIGURATION_H

/* if header's struct contents depend on configuration options, header must include config.h */
#include "config.h"

/* Logging and tracing */
typedef struct
{
  char sLogFileName[FILENAME_MAX];
  char sTraceFileName[FILENAME_MAX];
  int nExceptionDebugMask;
  int nTextLogLevel;
  int nAlertDlgLogLevel;
  bool bConfirmQuit;
  bool bNatFeats;
  bool bConsoleWindow;	/* for now, used just for Windows */
} CNF_LOG;


/* debugger */
typedef struct
{
  int nNumberBase;
  int nDisasmLines;
  int nMemdumpLines;
  int nDisasmOptions;
  bool bDisasmUAE;
} CNF_DEBUGGER;


/* ROM (TOS + cartridge) configuration */
typedef struct
{
  char szTosImageFileName[FILENAME_MAX];
  bool bPatchTos;
  char szCartridgeImageFileName[FILENAME_MAX];
} CNF_ROM;


/* Sound configuration */
typedef struct
{
  bool bEnableMicrophone;
  bool bEnableSound;
  bool bEnableSoundSync;
  int nPlaybackFreq;
  int SdlAudioBufferSize;
  char szYMCaptureFileName[FILENAME_MAX];
  int YmVolumeMixing;
} CNF_SOUND;



/* RS232 configuration */
typedef struct
{
  bool bEnableRS232;
  char szOutFileName[FILENAME_MAX];
  char szInFileName[FILENAME_MAX];
} CNF_RS232;


/* Dialog Keyboard */
typedef enum
{
  KEYMAP_SYMBOLIC,  /* Use keymapping with symbolic (ASCII) key codes */
  KEYMAP_SCANCODE,  /* Use keymapping with PC keyboard scancodes */
  KEYMAP_LOADED     /* Use keymapping with a map configuration file */
} KEYMAPTYPE;

typedef struct
{
  bool bDisableKeyRepeat;
  KEYMAPTYPE nKeymapType;
  char szMappingFileName[FILENAME_MAX];
} CNF_KEYBOARD;


typedef enum {
  SHORTCUT_OPTIONS,
  SHORTCUT_FULLSCREEN,
  SHORTCUT_MOUSEGRAB,
  SHORTCUT_COLDRESET,
  SHORTCUT_WARMRESET,
  SHORTCUT_SCREENSHOT,
  SHORTCUT_BOSSKEY,
  SHORTCUT_CURSOREMU,
  SHORTCUT_FASTFORWARD,
  SHORTCUT_RECANIM,
  SHORTCUT_RECSOUND,
  SHORTCUT_SOUND,
  SHORTCUT_DEBUG,
  SHORTCUT_PAUSE,
  SHORTCUT_QUIT,
  SHORTCUT_LOADMEM,
  SHORTCUT_SAVEMEM,
  SHORTCUT_INSERTDISKA,
  SHORTCUT_JOY_0,
  SHORTCUT_JOY_1,
  SHORTCUT_PAD_A,
  SHORTCUT_PAD_B,
  SHORTCUT_KEYS,  /* number of shortcuts */
  SHORTCUT_NONE
} SHORTCUTKEYIDX;

typedef struct
{
  int withModifier[SHORTCUT_KEYS];
  int withoutModifier[SHORTCUT_KEYS];
} CNF_SHORTCUT;


typedef struct
{
  int nMemorySize;
  int nTTRamSize;
  bool bAutoSave;
  char szMemoryCaptureFileName[FILENAME_MAX];
  char szAutoSaveFileName[FILENAME_MAX];
} CNF_MEMORY;


/* Joystick configuration */
typedef enum
{
  JOYSTICK_DISABLED,
  JOYSTICK_REALSTICK,
  JOYSTICK_KEYBOARD
} JOYSTICKMODE;
#define JOYSTICK_MODES 3

typedef struct
{
  JOYSTICKMODE nJoystickMode;
  bool bEnableAutoFire;
  bool bEnableJumpOnFire2;
  int nJoyId;
  int nKeyCodeUp, nKeyCodeDown, nKeyCodeLeft, nKeyCodeRight, nKeyCodeFire;
} JOYSTICK;

enum
{
	JOYID_JOYSTICK0,
	JOYID_JOYSTICK1,
	JOYID_STEPADA,
	JOYID_STEPADB,
	JOYID_PARPORT1,
	JOYID_PARPORT2,
	JOYSTICK_COUNT
};

typedef struct
{
  JOYSTICK Joy[JOYSTICK_COUNT];
} CNF_JOYSTICKS;


/* Disk image configuration */

typedef enum
{
  WRITEPROT_OFF,
  WRITEPROT_ON,
  WRITEPROT_AUTO
} WRITEPROTECTION;

#define MAX_FLOPPYDRIVES 2

typedef struct
{
  bool bAutoInsertDiskB;
  bool FastFloppy;			/* true to speed up FDC emulation */
  bool EnableDriveA;
  bool EnableDriveB;
  int  DriveA_NumberOfHeads;
  int  DriveB_NumberOfHeads;
  WRITEPROTECTION nWriteProtection;
  char szDiskZipPath[MAX_FLOPPYDRIVES][FILENAME_MAX];
  char szDiskFileName[MAX_FLOPPYDRIVES][FILENAME_MAX];
  char szDiskImageDirectory[FILENAME_MAX];
} CNF_DISKIMAGE;


/* Hard drives configuration: C: - Z: */
#define MAX_HARDDRIVES  24
#define DRIVE_C 0
#define DRIVE_SKIP -1

typedef enum
{
  GEMDOS_NOP,
  GEMDOS_UPPER,
  GEMDOS_LOWER
} GEMDOS_CHR_CONV;

typedef struct
{
  int nGemdosDrive;
  bool bUseHardDiskDirectories;
  bool bUseIdeMasterHardDiskImage;
  bool bUseIdeSlaveHardDiskImage;
  WRITEPROTECTION nWriteProtection;
  GEMDOS_CHR_CONV nGemdosCase;
  bool bFilenameConversion;
  bool bBootFromHardDisk;
  char szHardDiskDirectories[MAX_HARDDRIVES][FILENAME_MAX];
  char szIdeMasterHardDiskImage[FILENAME_MAX];
  char szIdeSlaveHardDiskImage[FILENAME_MAX];
} CNF_HARDDISK;

/* SCSI/ACSI configuration */
#define MAX_ACSI_DEVS 8
#define MAX_SCSI_DEVS 8

typedef struct
{
  bool bUseDevice;
  char sDeviceFile[FILENAME_MAX];
} CNF_SCSIDEV;

/* Falcon register $FFFF8006 bits 6 & 7 (mirrored in $FFFF82C0 bits 0 & 1):
 * 00 Monochrome
 * 01 RGB - Colormonitor
 * 10 VGA - Colormonitor
 * 11 TV
 */
#define FALCON_MONITOR_MONO 0x00  /* SM124 */
#define FALCON_MONITOR_RGB  0x40
#define FALCON_MONITOR_VGA  0x80
#define FALCON_MONITOR_TV   0xC0

typedef enum
{
  MONITOR_TYPE_MONO,
  MONITOR_TYPE_RGB,
  MONITOR_TYPE_VGA,
  MONITOR_TYPE_TV
} MONITORTYPE;

/* Screen configuration */
typedef struct
{
  MONITORTYPE nMonitorType;
  int nFrameSkips;
  bool bFullScreen;
  bool bKeepResolution;
#if !WITH_SDL2
  bool bKeepResolutionST;
#endif
  bool bAllowOverscan;
  bool bAspectCorrect;
  bool bUseExtVdiResolutions;
  int nSpec512Threshold;
  int nForceBpp;
  int nVdiColors;
  int nVdiWidth;
  int nVdiHeight;
  bool bMouseWarp;
  bool bShowStatusbar;
  bool bShowDriveLed;
  bool bCrop;
  bool bForceMax;
  int nMaxWidth;
  int nMaxHeight;
#if WITH_SDL2
  int nRenderScaleQuality;
  bool bUseVsync;
#endif
} CNF_SCREEN;


/* Printer configuration */
typedef struct
{
  bool bEnablePrinting;
  char szPrintToFileName[FILENAME_MAX];
} CNF_PRINTER;


/* Midi configuration */
typedef struct
{
  bool bEnableMidi;
  char sMidiInFileName[FILENAME_MAX];
  char sMidiOutFileName[FILENAME_MAX];
} CNF_MIDI;


/* Dialog System */
typedef enum
{
  MACHINE_ST,
  MACHINE_MEGA_ST,
  MACHINE_STE,
  MACHINE_MEGA_STE,
  MACHINE_TT,
  MACHINE_FALCON
} MACHINETYPE;

typedef enum
{
  DSP_TYPE_NONE,
  DSP_TYPE_DUMMY,
  DSP_TYPE_EMU
} DSPTYPE;

#if ENABLE_WINUAE_CPU
typedef enum
{
  FPU_NONE = 0,
  FPU_68881 = 68881,
  FPU_68882 = 68882,
  FPU_CPU = 68040
} FPUTYPE;
#endif

typedef enum
{
  VIDEO_TIMING_MODE_RANDOM = 0,
  VIDEO_TIMING_MODE_WS1,
  VIDEO_TIMING_MODE_WS2,
  VIDEO_TIMING_MODE_WS3,
  VIDEO_TIMING_MODE_WS4,
} VIDEOTIMINGMODE;

typedef struct
{
  int nCpuLevel;
  int nCpuFreq;
  bool bCompatibleCpu;            /* Prefetch mode */
  MACHINETYPE nMachineType;
  bool bBlitter;                  /* TRUE if Blitter is enabled */
  DSPTYPE nDSPType;               /* how to "emulate" DSP */
  bool bPatchTimerD;
  bool bFastBoot;                 /* Enable to patch TOS for fast boot */
  bool bFastForward;
  bool bAddressSpace24;           /* Always set to true with old UAE cpu */
  VIDEOTIMINGMODE VideoTimingMode;

#if ENABLE_WINUAE_CPU
  bool bCycleExactCpu;
  FPUTYPE n_FPUType;
  bool bCompatibleFPU;            /* More compatible FPU */
  bool bMMU;                      /* TRUE if MMU is enabled */
#endif
} CNF_SYSTEM;

typedef struct
{
  int AviRecordVcodec;
  int AviRecordFps;
  char AviRecordFile[FILENAME_MAX];
} CNF_VIDEO;

/* State of system is stored in this structure */
/* On reset, variables are copied into system globals and used. */
typedef struct
{
  /* Configure */
  CNF_LOG Log;
  CNF_DEBUGGER Debugger;
  CNF_SCREEN Screen;
  CNF_JOYSTICKS Joysticks;
  CNF_KEYBOARD Keyboard;
  CNF_SHORTCUT Shortcut;
  CNF_SOUND Sound;
  CNF_MEMORY Memory;
  CNF_DISKIMAGE DiskImage;
  CNF_HARDDISK HardDisk;
  CNF_SCSIDEV Acsi[MAX_ACSI_DEVS];
  CNF_SCSIDEV Scsi[MAX_SCSI_DEVS];
  CNF_ROM Rom;
  CNF_RS232 RS232;
  CNF_PRINTER Printer;
  CNF_MIDI Midi;
  CNF_SYSTEM System;
  CNF_VIDEO Video;
} CNF_PARAMS;


extern CNF_PARAMS ConfigureParams;
extern char sConfigFileName[FILENAME_MAX];

static inline bool Config_IsMachineST(void)
{
	return ConfigureParams.System.nMachineType == MACHINE_ST ||
	       ConfigureParams.System.nMachineType == MACHINE_MEGA_ST;
}

static inline bool Config_IsMachineSTE(void)
{
	return ConfigureParams.System.nMachineType == MACHINE_STE ||
	       ConfigureParams.System.nMachineType == MACHINE_MEGA_STE;
}

extern void Configuration_SetDefault(void);
extern void Configuration_Apply(bool bReset);
extern void Configuration_Load(const char *psFileName);
extern void Configuration_Save(void);
extern void Configuration_MemorySnapShot_Capture(bool bSave);

#endif
