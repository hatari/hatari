/*
  Hatari
*/

#define SCREEN_AREA_STARTX  1    /* Start coords of client area */
#define SCREEN_AREA_STARTY  27

#define  BORDER_WIDTH  2         /* Speed/Status bar width and height */
#define  BORDER_HEIGHT  (27+18)  /* Height of Speed/Status bar */


// List of resource bitmaps used to draw view
enum {
  BITMAP_TOOLBAR_ICONS,
  BITMAP_GRILL,
  BITMAP_RESIZE,
  BITMAP_TOOLBAR_MENU,
  BITMAP_TOOLBAR_SEPARATOR,
  BITMAP_STATUSBAR_ICONS,

  MAX_BITMAPS
};

// List of cursors used (store original to restore when exit)
enum {
  CURSOR_ORIGINAL,
  CURSOR_ARROW,
  CURSOR_NULL,
  CURSOR_HOURGLASS,

  MAX_CURSORS
};

// Values for View_ToggleWindowsMouse()
enum {                // Parameter for windows mouse
  MOUSE_TOGGLE,
  MOUSE_ST,
  MOUSE_WINDOWS
};

#define NUM_FLOPPYA_MENU_ITEMS  4            // Insert previous disc images name AFTER this item
#define NUM_FLOPPYB_MENU_ITEMS  3
#define MAX_FLOPPY_MENU_IMAGES  4            // Show last 4 disc image filenames loaded

//extern HBITMAP Bitmaps[MAX_BITMAPS];
//extern HCURSOR Cursors[MAX_CURSORS];
extern BOOL bWindowsMouseMode;
//extern RECT WindowInitRect;
//extern RECT MinWindowBorderSizes[2][3];
//extern HMENU hFullScreenMenu;
extern char szPreviousImageFilenames[2][MAX_FLOPPY_MENU_IMAGES][MAX_FILENAME_LENGTH];
extern int nPreviousImageFilenames[2];

extern void View_DefaultWindowPos(void);
extern void View_CreateWindow(void);
extern void View_CloseWindow(void);
extern void View_ShowWindow(void);
extern void View_LoadBitmaps(void);
extern void View_FreeBitmaps(void);
extern void View_CheckMenuItem(int Control,BOOL bState);
extern void View_SetMenuChecks(void);
//extern void View_SetMenuFileNames(HMENU hMenu, int InitMenuItem,int Drive);
extern void View_AddMenuFileName(int Drive,char *pszFileName);
extern void View_SetFullScreenMenu(void);
//extern void View_DrawBackgroundRect(HDC hDC,RECT *pRect);
//extern void View_DrawBackgroundLineLight(HDC hDC,int x,int y,int x2,int y2);
//extern void View_DrawBackgroundLineShadow(HDC hDC,int x,int y,int x2,int y2);
extern void View_DrawMenu(void);
extern void View_LimitCursorToScreen(void);
extern void View_LimitCursorToClient(void);
extern BOOL View_ToggleWindowsMouse(int ForceToMode);
extern void View_DebounceAllKeys(void);
//extern void View_CheckMouseAtEdgeOfScreen(HWND hWnd,int MouseX,int MouseY);
extern void View_UpdateSTMousePosition(void);
extern void View_KeyDown(unsigned int sdlkey, unsigned int sdlmod);
extern void View_KeyUp(unsigned int sdlkey, unsigned int sdlmod);
//extern void View_MouseMove(HWND hWnd,UINT wParam,LONG lParam);
extern void View_LeftMouseButtonDown(void);
extern void View_LeftMouseButtonUp(void);
extern void View_RightMouseButtonDown(void);
extern void View_RightMouseButtonUp(void);
