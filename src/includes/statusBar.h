/*
  Hatari
*/

/* List of possible status bar icons */
enum {
  STATUS_ICON_FRAMERATE,
  STATUS_ICON_FLOPPY,
  STATUS_ICON_HARDDRIVE,
  STATUS_ICON_PRINTER,
  STATUS_ICON_RS232,
  STATUS_ICON_SOUND,
  STATUS_ICON_SCREEN,

  STATUS_ICON_COUNT
};

enum {
  ICONSTATE_ON,       /* Turn icon on, can only be turn off by... */
  ICONSTATE_OFF,      /* Off, see above */
  ICONSTATE_UPDATE    /* Show icons for 'x' frames */
};

typedef struct {
  int x,y;
  int w,h;
} XYWH;

typedef struct {
  int x;               // X offset from right edge of window
  XYWH SrcBitmapPos;   // XYWH on source icon bitmap
  int DisplayCount;    // Frames to display for(0=not displayed)
} STATUSICON;

#define STATUS_ICONS_X_OFFSET  106  // Offset from edge of Window
#define  STATUS_ICONS_Y_OFFSET  15
#define STATUS_TEXT_X_OFFSET  8  // Offset from left of Window

extern char *pszStatusBarHelpText;

extern void StatusBar_SetIcon(int StatusIcon, int IconState);
extern void StatusBar_UpdateIcons(void);
extern void StatusBar_DrawAllIcons(void);
extern void StatusBar_DrawIcon(/*HDC hDC, HDC MemDC,*/int StatusIcon);

extern void StatusBar_SetText(char *pString);
extern void StatusBar_Draw(void);
extern void StatusBar_DrawText(BOOL bClearBackground);
