/*
  Hatari - configuration.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CONFIGURATION_H
#define HATARI_CONFIGURATION_H


/* TOS/GEM configuration */
typedef struct
{
  char szTOSImageFileName[FILENAME_MAX];
  BOOL bUseExtGEMResolutions;
  int nGEMResolution;
  int nGEMColours;
} CNF_TOSGEM;


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


/* Memory configuration */
typedef enum
{
  MEMORY_SIZE_512Kb,
  MEMORY_SIZE_1Mb,
  MEMORY_SIZE_2Mb,
  MEMORY_SIZE_4Mb
} MEMORY_SIZE;

typedef struct
{
  MEMORY_SIZE nMemorySize;
  char szMemoryCaptureFileName[FILENAME_MAX];
} CNF_MEMORY;


/* Joystick configuration */
typedef struct
{
  BOOL bCursorEmulation;
  BOOL bEnableAutoFire;
} JOYSTICK;

typedef struct
{
  JOYSTICK Joy[2];
} CNF_JOYSTICKS;


/* Discimage configuration */
typedef struct
{
  BOOL bAutoInsertDiscB;
  char szDiscImageDirectory[FILENAME_MAX];
} CNF_DISCIMAGE;


/* Hard discs configuration */
#define MAX_HARDDRIVES  1
#define DRIVELIST_TO_DRIVE_INDEX(DriveList)  (DriveList+1)

typedef enum
{
  DRIVELIST_NONE,
  DRIVELIST_C,
  DRIVELIST_CD,
  DRIVELIST_CDE,
  DRIVELIST_CDEF
} DRIVELIST;

typedef enum
{
  DRIVE_C,
  DRIVE_D,
  DRIVE_E,
  DRIVE_F
} DRIVELETTER;

typedef struct
{
  int nDriveList;
  BOOL bBootFromHardDisc;
  int nHardDiscDir;
  BOOL bUseHardDiscDirectories;
  BOOL bUseHardDiscImage;
  char szHardDiscDirectories[MAX_HARDDRIVES][FILENAME_MAX];
  char szHardDiscImage[FILENAME_MAX];
} CNF_HARDDISC;


/* Screen configuration */
typedef struct
{
  BOOL bFullScreen;
  BOOL bDoubleSizeWindow;
  BOOL bAllowOverscan;
  BOOL bInterlacedScreen;
  BOOL bSyncToRetrace;
  BOOL bFrameSkip;
  int ChosenDisplayMode;
  BOOL bCaptureChange;
  int nFramesPerSecond;
  BOOL bUseHighRes;
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

typedef struct
{
  int nCpuLevel;
  BOOL bCompatibleCpu;
  BOOL bAddressSpace24;
  BOOL bBlitter;                  /* TRUE if blitter is enabled */
  BOOL bPatchTimerD;
  BOOL bSlowFDC;                  /* TRUE to slow down FDC emulation */
  MINMAXSPEED_TYPE nMinMaxSpeed;
} CNF_SYSTEM;


/* State of system is stored in this structure */
/* On reset, variables are copied into system globals and used. */
typedef struct
{
  /* Configure */
  CNF_SCREEN Screen;
  CNF_JOYSTICKS Joysticks;
  CNF_KEYBOARD Keyboard;
  CNF_SOUND Sound;
  CNF_MEMORY Memory;
  CNF_DISCIMAGE DiscImage;
  CNF_HARDDISC HardDisc;
  CNF_TOSGEM TOSGEM;
  CNF_RS232 RS232;
  CNF_PRINTER Printer;
  CNF_MIDI Midi;
  CNF_SYSTEM System;
} CNF_PARAMS;


extern BOOL bFirstTimeInstall;
extern CNF_PARAMS ConfigureParams;
extern char sConfigFileName[FILENAME_MAX];

extern void Configuration_SetDefault(void);
extern void Configuration_Load(void);
extern void Configuration_Save(void);

#endif
