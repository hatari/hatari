/* Hatari */


/*  Dialog TOS/GEM  */
typedef struct {
  char szTOSImageFileName[MAX_FILENAME_LENGTH];
  BOOL bUseTimeDate;
  BOOL bAccGEMGraphics;
  BOOL bUseExtGEMResolutions;
  int nGEMResolution;
  int nGEMColours;
} DLG_TOSGEM;

enum {
  GEMRES_640x480,
  GEMRES_800x600,
  GEMRES_1024x768
};

enum {
  GEMCOLOUR_2,
  GEMCOLOUR_4,
  GEMCOLOUR_16
};


/* Dialog Sound */
typedef struct {
  BOOL bEnableSound;
  int nPlaybackQuality;
  char szYMCaptureFileName[MAX_FILENAME_LENGTH];
} DLG_SOUND;

enum {
  PLAYBACK_LOW,
  PLAYBACK_MEDIUM,
  PLAYBACK_HIGH
};


/* Dialog RS232 */
typedef struct {
  BOOL bEnableRS232;
  int nCOMPort;
} DLG_RS232;

enum {
  COM_PORT_1,
  COM_PORT_2,
  COM_PORT_3,
  COM_PORT_4
};


/* Dialog Keyboard */
typedef struct {
  BOOL bDisableKeyRepeat;
  int ShortCuts[2][3];                // F11+F12, NORMAL+SHIFT+CTRL
  char szMappingFileName[MAX_FILENAME_LENGTH];
} DLG_KEYBOARD;

enum {
  SHORT_CUT_F11,
  SHORT_CUT_F12,
  SHORT_CUT_NONE
};

enum {
  SHORT_CUT_KEY,
  SHORT_CUT_SHIFT,
  SHORT_CUT_CTRL
};


/* Dialog Memory */
typedef struct {
  int nMemorySize;
  char szMemoryCaptureFileName[MAX_FILENAME_LENGTH];
} DLG_MEMORY;

enum {
  MEMORY_SIZE_512Kb,
  MEMORY_SIZE_1Mb,
  MEMORY_SIZE_2Mb,
  MEMORY_SIZE_4Mb
};


/* Dialog Joystick */
typedef struct {
  BOOL bCursorEmulation;
  BOOL bEnableAutoFire;
} JOYSTICK;

typedef struct {
  BOOL bUseDirectInput;
  JOYSTICK Joy[2];
} DLG_JOYSTICKS;


/* Dialog Discimage */
typedef struct {
  BOOL bAutoInsertDiscB;
  char szDiscImageDirectory[MAX_FILENAME_LENGTH];
} DLG_DISCIMAGE;


/* Dialog configure */
typedef struct {
  int nMinMaxSpeed;
  int nPrevMinMaxSpeed;
} DLG_CONFIGURE;

enum {
  MINMAXSPEED_MIN,
  MINMAXSPEED_1,
  MINMAXSPEED_2,
  MINMAXSPEED_3,
  MINMAXSPEED_MAX
};


/* Dialog hard disc image */
#define MAX_HARDDRIVES  4
#define DRIVELIST_TO_DRIVE_INDEX(DriveList)  (DriveList+1)
typedef struct {
  int nDriveList;
  BOOL bBootFromHardDisc;
  int nHardDiscDir;
  char szHardDiscDirectories[MAX_HARDDRIVES][MAX_FILENAME_LENGTH];
} DLG_HARDDISC;

enum {
  DRIVELIST_NONE,
  DRIVELIST_C,
  DRIVELIST_CD,
  DRIVELIST_CDE,
  DRIVELIST_CDEF
};

enum {
  DRIVE_C,
  DRIVE_D,
  DRIVE_E,
  DRIVE_F
};


/* Dialog screen */
typedef struct {
  BOOL bDoubleSizeWindow;
  BOOL bAllowOverscan;
  BOOL bInterlacedFullScreen;
  BOOL bSyncToRetrace;
  BOOL bFrameSkip;
} DLG_SCREEN_ADVANCED;

typedef struct {
  BOOL bFullScreen;
  DLG_SCREEN_ADVANCED Advanced;
  int ChosenDisplayMode;

  BOOL bCaptureChange;
  int nFramesPerSecond;
  BOOL bUseHighRes;
} DLG_SCREEN;


/* Dialog Printer */
typedef struct {
  BOOL bEnablePrinting;
  BOOL bPrintToFile;
  char szPrintToFileName[MAX_FILENAME_LENGTH];
} DLG_PRINTER;


/* State of system is stored in this structure */
/* On reset, variables are copied into system globals and used. */
typedef struct {
  // Configure
  DLG_CONFIGURE Configure;
  DLG_SCREEN Screen;
  DLG_JOYSTICKS Joysticks;
  DLG_KEYBOARD Keyboard;
  DLG_SOUND Sound;
  DLG_MEMORY Memory;
  DLG_DISCIMAGE DiscImage;
  DLG_HARDDISC HardDisc;
  DLG_TOSGEM TOSGEM;
  DLG_RS232 RS232;
  DLG_PRINTER Printer;
} DLG_PARAMS;

enum {
  DIALOG_PAGE_CONFIGURE,
  DIALOG_PAGE_SCREEN,
  DIALOG_PAGE_JOYSTICKS,
  DIALOG_PAGE_KEYBOARD,
  DIALOG_PAGE_SOUND,
  DIALOG_PAGE_MEMORY,
  DIALOG_PAGE_DISCIMAGE,
  DIALOG_PAGE_HARDDISC,
  DIALOG_PAGE_TOSGEM,
  DIALOG_PAGE_RS232,
  DIALOG_PAGE_PRINTER
};

extern DLG_PARAMS ConfigureParams,DialogParams;
extern BOOL bOKDialog;
extern int nLastOpenPage;

extern void Dialog_DefaultConfigurationDetails(void);
extern void Dialog_CopyDetailsFromConfiguration(BOOL bReset);
extern BOOL Dialog_DoProperty(int StartingPage,BOOL bForceReset);
//extern void Dialog_SetButton(HWND hDlg,int ButtonID,int Flag);
//extern BOOL Dialog_ReadButton(HWND hDlg,int ButtonID);
//extern void Dialog_EnableItem(HWND hDlg,int ButtonID,int State);
//extern void Dialog_EnableItems(HWND hDlg,int *pButtonIDs,int State);
//extern void Dialog_ShowItemRange(HWND hDlg,int LowButtonID,int HighButtonID,int Show);
//extern void Dialog_SetText(HWND hDlg,int ButtonID,char *szString);
#if 0
extern void Dialog_ReadText(HWND hDlg,int ButtonID,char *szString);
extern void Dialog_SetTrackBar(HWND hDlg, int nTrackBarID, int nMin, int nMax, int nSelected);
extern int Dialog_GetTrackBar(HWND hDlg, int nTrackBarID);
extern void Dialog_SetComboBoxItems(HWND hDlg, int ComboBoxID, char *pComboBoxStrings[], int nSelectedItem);
extern void Dialog_ComboBoxSelectString(HWND hDlg, int ComboBoxID, char *pszSelectedString);
extern int Dialog_GetSelectedComboBoxItem(HWND hDlg, int ComboBoxID);
extern void Dialog_SetListBoxItems(HWND hDlg, int ListBoxID, char *pListBoxStrings[], int nSelectedItem);
extern int Dialog_GetSelectedListBoxItem(HWND hDlg, int ListBoxID);
extern int Dialog_SetSpinList(HWND hDlg, int nEditBoxID, int nSpinID, char *pSpinStrings[], int nItems, int nSelectedItem);
extern int Dialog_GetSpinList(HWND hDlg, int nSpinID);
extern int Dialog_UpdateSpinList(HWND hDlg, int nEditBoxID, char *pSpinStrings[], int nNumSpinItems, int nNewSelectedItem);
extern void Dialog_SetRadioButtons(HWND hDlg,int StartButtonID,int EndButtonID,int nSelectedItem);
extern int Dialog_ReadRadioButtons(HWND hDlg,int StartButtonID,int EndButtonID);
extern void Dialog_AddListViewColumn(HWND hDlg, int ListViewID, int Order, char *pString, int Width);
#endif
