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
  int nSymbolLines;
  int nMemdumpLines;
  int nDisasmLines;
  int nBacktraceLines;
  int nExceptionDebugMask;
  int nDisasmOptions;
  bool bDisasmUAE;
  /* load and free symbols for GEMDOS HD loaded programs automatically */
  bool bSymbolsAutoLoad;
  /* whether to match all symbols or just types relevant for given command */
  bool bMatchAllSymbols;
} CNF_DEBUGGER;


/* ROM (TOS + cartridge) configuration */
typedef struct
{
  char szTosImageFileName[FILENAME_MAX];
  bool bPatchTos;
  char szCartridgeImageFileName[FILENAME_MAX];
} CNF_ROM;


/* LILO (Linux loader) configuration */
typedef struct
{
  char szCommandLine[256];	/* bootinfo CL_SIZE */
  char szKernelFileName[FILENAME_MAX];
  char szKernelSymbols[FILENAME_MAX];
  char szRamdiskFileName[FILENAME_MAX];
  bool bRamdiskToFastRam;
  bool bKernelToFastRam;
  bool bHaltOnReboot;
} CNF_LILO;


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



/* RS232 / SCC configuration */
#define CNF_SCC_CHANNELS_MAX		3
#define CNF_SCC_CHANNELS_A_SERIAL	0
#define CNF_SCC_CHANNELS_A_LAN		1
#define CNF_SCC_CHANNELS_B		2
typedef struct
{
  bool bEnableRS232;
  char szOutFileName[FILENAME_MAX];
  char szInFileName[FILENAME_MAX];
  bool EnableScc[CNF_SCC_CHANNELS_MAX];
  char SccInFileName[CNF_SCC_CHANNELS_MAX][FILENAME_MAX];
  char SccOutFileName[CNF_SCC_CHANNELS_MAX][FILENAME_MAX];
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
  int nCountryCode;
  int nKbdLayout;
  int nLanguage;
  char szMappingFileName[FILENAME_MAX];
} CNF_KEYBOARD;


typedef enum {
  SHORTCUT_OPTIONS,
  SHORTCUT_FULLSCREEN,
  SHORTCUT_BORDERS,
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
  int STRamSize_KB;
  int TTRamSize_KB;
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
#define JOYSTICK_BUTTONS 3

typedef struct
{
  JOYSTICKMODE nJoystickMode;
  bool bEnableAutoFire;
  bool bEnableJumpOnFire2;
  int nJoyId;
  int nJoyButMap[JOYSTICK_BUTTONS];
  int nKeyCodeUp, nKeyCodeDown, nKeyCodeLeft, nKeyCodeRight, nKeyCodeFire;
  int nKeyCodeB, nKeyCodeC, nKeyCodeOption, nKeyCodePause;
  int nKeyCodeStar, nKeyCodeHash, nKeyCodeNum[10];
} JOYSTICK;

enum
{
	JOYID_JOYSTICK0,
	JOYID_JOYSTICK1,
	JOYID_JOYPADA,
	JOYID_JOYPADB,
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
  WRITEPROTECTION nWriteProtection;
  GEMDOS_CHR_CONV nGemdosCase;
  bool bFilenameConversion;
  bool bGemdosHostTime;
  bool bBootFromHardDisk;
  char szHardDiskDirectories[MAX_HARDDRIVES][FILENAME_MAX];
} CNF_HARDDISK;

/* SCSI/ACSI/IDE configuration */
#define MAX_ACSI_DEVS 8
#define MAX_SCSI_DEVS 8
#define MAX_IDE_DEVS 2

typedef struct
{
  bool bUseDevice;
  char sDeviceFile[FILENAME_MAX];
  int nBlockSize;
} CNF_SCSIDEV;

typedef enum
{
  BYTESWAP_OFF,
  BYTESWAP_ON,
  BYTESWAP_AUTO
} BYTESWAPPING;

typedef struct
{
  bool bUseDevice;
  BYTESWAPPING nByteSwap;
  char sDeviceFile[FILENAME_MAX];
  int nBlockSize;
  int nDeviceType;
} CNF_IDEDEV;

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
  bool DisableVideo;
  bool bFullScreen;
  bool bAllowOverscan;
  bool bAspectCorrect;
  bool bShowStatusbar;
  bool bShowDriveLed;
  bool bMouseWarp;
  bool bCrop;
  bool bForceMax;
  bool bUseExtVdiResolutions;
  bool bKeepResolution;
  bool bResizable;
  bool bUseVsync;
  bool bUseSdlRenderer;
  int ScreenShotFormat;
  float nZoomFactor;
  int nSpec512Threshold;
  int nVdiColors;
  int nVdiWidth;
  int nVdiHeight;
  int nMaxWidth;
  int nMaxHeight;
  int nFrameSkips;
} CNF_SCREEN;


/* Printer configuration */
typedef struct
{
  bool bEnablePrinting;
  char szPrintToFileName[FILENAME_MAX];
} CNF_PRINTER;

#define MAX_MIDI_PORT_NAME 256 /* a guess */

/* Midi configuration */
typedef struct
{
  bool bEnableMidi;
  char sMidiInFileName[FILENAME_MAX];
  char sMidiOutFileName[FILENAME_MAX];
  char sMidiInPortName[MAX_MIDI_PORT_NAME];
  char sMidiOutPortName[MAX_MIDI_PORT_NAME];
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

typedef enum
{
  VME_TYPE_NONE,
  VME_TYPE_DUMMY
} VMETYPE;

typedef enum
{
  FPU_NONE = 0,
  FPU_68881 = 68881,
  FPU_68882 = 68882,
  FPU_CPU = 68040
} FPUTYPE;

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
  VMETYPE nVMEType;               /* how to "emulate" SCU/VME */
  int nRtcYear;
  bool bPatchTimerD;
  bool bFastBoot;                 /* Enable to patch TOS for fast boot */
  bool bFastForward;
  bool bAddressSpace24;           /* true if using a 24-bit address bus */
  VIDEOTIMINGMODE VideoTimingMode;

  bool bCycleExactCpu;
  FPUTYPE n_FPUType;
  bool bCompatibleFPU;            /* More compatible FPU */
  bool bSoftFloatFPU;
  bool bMMU;                      /* TRUE if MMU is enabled */
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
  CNF_IDEDEV Ide[MAX_IDE_DEVS];
  CNF_ROM Rom;
  CNF_LILO Lilo;
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

static inline bool Config_IsMachineMegaSTE(void)
{
	return ConfigureParams.System.nMachineType == MACHINE_MEGA_STE;
}

static inline bool Config_IsMachineTT(void)
{
	return ConfigureParams.System.nMachineType == MACHINE_TT;
}

static inline bool Config_IsMachineFalcon(void)
{
	return ConfigureParams.System.nMachineType == MACHINE_FALCON;
}

extern void Configuration_SetDefault(void);
extern void Configuration_Apply(bool bReset);
extern void Configuration_Load(const char *psFileName);
extern void Configuration_Save(void);
extern void Configuration_MemorySnapShot_Capture(bool bSave);
extern void Configuration_ChangeCpuFreq ( int CpuFreq_new );
#ifdef EMSCRIPTEN
extern void Configuration_ChangeMemory(int RamSizeKb);
extern void Configuration_ChangeSystem(int nMachineType);
extern void Configuration_ChangeTos(const char* szTosImageFileName);
extern void Configuration_ChangeUseHardDiskDirectories(bool bUseHardDiskDirectories);
extern void Configuration_ChangeFastForward(bool bFastForwardActive);
#endif

#endif
