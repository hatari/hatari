/*
  Hatari - configuration.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CONFIGURATION_H
#define HATARI_CONFIGURATION_H

/* Logging */
typedef struct
{
  char sLogFileName[FILENAME_MAX];
  int nTextLogLevel;
  int nAlertDlgLogLevel;
} CNF_LOG;


/* ROM (TOS + cartridge) configuration */
typedef struct
{
  char szTosImageFileName[FILENAME_MAX];
  char szCartridgeImageFileName[FILENAME_MAX];
} CNF_ROM;


/* Sound configuration */
typedef enum
{
  PLAYBACK_LOW,
  PLAYBACK_MEDIUM,
  PLAYBACK_HIGH
} SOUND_QUALITIY;

typedef struct
{
  BOOL bEnableSound;
  SOUND_QUALITIY nPlaybackQuality;
  char szYMCaptureFileName[FILENAME_MAX];
} CNF_SOUND;



/* RS232 configuration */
typedef struct
{
  BOOL bEnableRS232;
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
  BOOL bDisableKeyRepeat;
  KEYMAPTYPE nKeymapType;
  char szMappingFileName[FILENAME_MAX];
} CNF_KEYBOARD;


typedef enum {
  SHORTCUT_OPTIONS,
  SHORTCUT_FULLSCREEN,
  SHORTCUT_MOUSEMODE,
  SHORTCUT_COLDRESET,
  SHORTCUT_WARMRESET,
  SHORTCUT_SCREENSHOT,
  SHORTCUT_BOSSKEY,
  SHORTCUT_CURSOREMU,
  SHORTCUT_MAXSPEED,
  SHORTCUT_RECANIM,
  SHORTCUT_RECSOUND,
  SHORTCUT_SOUND,
  SHORTCUT_QUIT,
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
  char szMemoryCaptureFileName[FILENAME_MAX];
} CNF_MEMORY;


/* Joystick configuration */
typedef enum
{
  JOYSTICK_DISABLED,
  JOYSTICK_REALSTICK,
  JOYSTICK_KEYBOARD
} JOYSTICKMODE;

typedef struct
{
  JOYSTICKMODE nJoystickMode;
  BOOL bEnableAutoFire;
  int nJoyId;
  int nKeyCodeUp, nKeyCodeDown, nKeyCodeLeft, nKeyCodeRight, nKeyCodeFire;
} JOYSTICK;

#define JOYSTICK_COUNT 6

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

typedef struct
{
  BOOL bAutoInsertDiskB;
  WRITEPROTECTION nWriteProtection;
  char szDiskImageDirectory[FILENAME_MAX];
} CNF_DISKIMAGE;


/* Hard drives configuration */
#define MAX_HARDDRIVES  1

typedef enum
{
  DRIVE_C,
  DRIVE_D,
  DRIVE_E,
  DRIVE_F
} DRIVELETTER;

typedef struct
{
  BOOL bBootFromHardDisk;
  int nHardDiskDir;
  BOOL bUseHardDiskDirectories;
  BOOL bUseHardDiskImage;
  char szHardDiskDirectories[MAX_HARDDRIVES][FILENAME_MAX];
  char szHardDiskImage[FILENAME_MAX];
  BOOL bUseIdeHardDiskImage;
  char szIdeHardDiskImage[FILENAME_MAX];
} CNF_HARDDISK;

/* Falcon register $FFFF8006 bits 6 & 7 (mirrored in $FFFF82C0 bits 0 & 1):
 * 00 Monochrome
 * 01 RGB - Colormonitor
 * 10 VGA - Colormonitor
 * 11 TV
 */
#define FALCON_MONITOR_MASK 0x3F
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
  MONITORTYPE MonitorType;
  int FrameSkips;
  BOOL bFullScreen;
  BOOL bAllowOverscan;
  BOOL bInterleavedScreen;
  BOOL bForce8Bpp;
  BOOL bZoomLowRes;
  BOOL bUseExtVdiResolutions;
  int nVdiResolution;
  int nVdiColors;
  BOOL bCaptureChange;
  int nFramesPerSecond;
} CNF_SCREEN;


/* Printer configuration */
typedef struct
{
  BOOL bEnablePrinting;
  BOOL bPrintToFile;
  char szPrintToFileName[FILENAME_MAX];
} CNF_PRINTER;


/* Midi configuration */
typedef struct
{
  BOOL bEnableMidi;
  char szMidiOutFileName[FILENAME_MAX];
} CNF_MIDI;


/* Dialog System */
typedef enum
{
  MINMAXSPEED_MIN,
  MINMAXSPEED_1,
  MINMAXSPEED_2,
  MINMAXSPEED_3,
  MINMAXSPEED_MAX
} MINMAXSPEED_TYPE;

typedef enum
{
  MACHINE_ST,
  MACHINE_STE,
  MACHINE_TT,
  MACHINE_FALCON
} MACHINETYPE;

typedef struct
{
  int nCpuLevel;
  int nCpuFreq;
  BOOL bCompatibleCpu;
  /*BOOL bAddressSpace24;*/
  MACHINETYPE nMachineType;
  BOOL bBlitter;                  /* TRUE if blitter is enabled */
  BOOL bRealTimeClock;
  BOOL bPatchTimerD;
  BOOL bSlowFDC;                  /* TRUE to slow down FDC emulation */
  MINMAXSPEED_TYPE nMinMaxSpeed;
} CNF_SYSTEM;


/* State of system is stored in this structure */
/* On reset, variables are copied into system globals and used. */
typedef struct
{
  /* Configure */
  CNF_LOG Log;
  CNF_SCREEN Screen;
  CNF_JOYSTICKS Joysticks;
  CNF_KEYBOARD Keyboard;
  CNF_SHORTCUT Shortcut;
  CNF_SOUND Sound;
  CNF_MEMORY Memory;
  CNF_DISKIMAGE DiskImage;
  CNF_HARDDISK HardDisk;
  CNF_ROM Rom;
  CNF_RS232 RS232;
  CNF_PRINTER Printer;
  CNF_MIDI Midi;
  CNF_SYSTEM System;
} CNF_PARAMS;


extern BOOL bFirstTimeInstall;
extern CNF_PARAMS ConfigureParams;
extern char sConfigFileName[FILENAME_MAX];

extern void Configuration_SetDefault(void);
extern void Configuration_Apply(BOOL bReset);
extern void Configuration_Load(const char *psFileName);
extern void Configuration_Save(void);

#endif
