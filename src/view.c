/*
  Hatari

  Main viewing window display. This involves redrawing of the Window and also of directing input
  to the various functions. Parts of this code control the relative mouse movement, debouncing
  of input keys and the sizing of the view Window.
*/

#include <SDL.h>

#include "main.h"
#include "debug.h"
#include "dialog.h"
#include "floppy.h"
#include "ikbd.h"
#include "keymap.h"
#include "reset.h"
#include "screen.h"
#include "shortcut.h"
#include "statusBar.h"
#include "vdi.h"
#include "joy.h"
#include "view.h"


//HBITMAP Bitmaps[MAX_BITMAPS];             /* Handles to Bitmaps used to draw view window */
//HCURSOR Cursors[MAX_CURSORS];             /* Cursors used to hide/show during view */

BOOL bWindowsMouseMode = FALSE/*TRUE*/;     /* TRUE if mouse in Windows mode, FALSE if ST mode */
BOOL bCursorOn = TRUE;                      /* TRUE if we are showing the standard windows arrow cursor */

//RECT WindowInitRect;
//HMENU hFullScreenMenu;                    /* Menu handle for full-screen options access */

char szPreviousImageFilenames[2][MAX_FLOPPY_MENU_IMAGES][MAX_FILENAME_LENGTH] = {
  { "","","","" },
  { "","","","" }
};
int nPreviousImageFilenames[2]= { 0,0 };    // Drive A, B

// List of ST scan codes to NOT de-bounce when running in maximum speed
char DebounceExtendedKeys[] = {
  0x1d,  // CTRL
  0x2a,  // Left SHIFT
  0x01,  // ESC
  0x38,  // ALT
  0x36,  // Right SHIFT
  0      //term
};

// Size of screen area for Windows according to resolution and options settings
// Index ONLY using 'View_GetWindowBorderSizeIndex()'
/*
RECT MinWindowBorderSizes[2][3] = {
  { // Non-Overscan
    0,0, 320,200,  // ST_LOW_RES
    0,0, 640,400,  // ST_MEDIUM_RES
    0,0, 640,400,  // ST_HIGH_RES
  },
  { // Overscan
    0,0, 320+OVERSCAN_LEFT+OVERSCAN_RIGHT,200+OVERSCAN_TOP+OVERSCAN_BOTTOM,      // ST_LOW_RES
    0,0, 640+OVERSCAN_LEFT*2+OVERSCAN_RIGHT*2,400+OVERSCAN_TOP*2+OVERSCAN_BOTTOM*2,  // ST_MEDIUM_RES
    0,0, 640,400,  // ST_HIGH_RES
  }
};

RECT WindowBorderSizes[2][3] = {
  { // Non-Overscan
    0,0, 320,200,  // ST_LOW_RES
    0,0, 640,400,  // ST_MEDIUM_RES
    0,0, 640,400,  // ST_HIGH_RES
  },
  { // Overscan
    0,0, 320+OVERSCAN_LEFT+OVERSCAN_RIGHT,200+OVERSCAN_TOP+OVERSCAN_BOTTOM,      // ST_LOW_RES
    0,0, 640+OVERSCAN_LEFT*2+OVERSCAN_RIGHT*2,400+OVERSCAN_TOP*2+OVERSCAN_BOTTOM*2,  // ST_MEDIUM_RES
    0,0, 640,400,  // ST_HIGH_RES
  }
};
*/

//-----------------------------------------------------------------------
/*
  Set default Window init position(CW_USEDEFAULT)
*/
/*
void View_DefaultWindowPos(void)
{
  WindowInitRect.left =
  WindowInitRect.top =
  WindowInitRect.right =
  WindowInitRect.bottom = CW_USEDEFAULT;
}
*/

//-----------------------------------------------------------------------
/*
  Create our view window with toolbar/status bar and client area
*/
/*
void View_CreateWindow(void)
{
  // Clear our short-cut keys
  ShortCut_ClearKeys();

  // Get size of window (320x200 default), make sure is on screen
  View_SizeWindow();

  // Create window, get global DC
  hWnd = CreateWindow(szName,PROG_NAME,WS_OVERLAPPEDWINDOW
   ,WindowInitRect.left,WindowInitRect.top,WindowInitRect.right,WindowInitRect.bottom
   ,NULL,NULL,hInst,NULL);
  MainDC = GetDC(hWnd);
  // Call Windows to allow drag'n'drop of disc images
  DragAcceptFiles(hWnd,TRUE);
  // Create menu for full-screen
  hFullScreenMenu = LoadMenu(hInst,MAKEINTRESOURCE(IDR_MENU_FULLSCREEN));

  // Load bitmaps needed for tool/status bars
  memset(Bitmaps,0x00,sizeof(HBITMAP)*MAX_BITMAPS);
  View_LoadBitmaps();

  // Load cursors used, and store off default
  Cursors[CURSOR_ORIGINAL] = SetCursor(Cursors[CURSOR_ARROW]);
  Cursors[CURSOR_ARROW] = LoadCursor(NULL,IDC_ARROW);
  Cursors[CURSOR_NULL] = LoadCursor(hInst,MAKEINTRESOURCE(IDC_CURSOR1));
  Cursors[CURSOR_HOURGLASS] = LoadCursor(NULL,IDC_WAIT);

  // Create pop-up menus for toolbar
  ToolBar_CreateMenus();
}
*/

//-----------------------------------------------------------------------
/*
  Close our view Window
*/
/*
void View_CloseWindow(void)
{
  // Free tool/status bar bitmaps
  View_FreeBitmaps();

  // Restore cursor back to normal
  SetCursor(Cursors[CURSOR_ORIGINAL]);

  // Free menus
  ToolBar_FreeMenus();
  // Free full-screen menu
  DestroyMenu(hFullScreenMenu);
}
*/

//-----------------------------------------------------------------------
/*
  Show our view Window(create at correct size)
*/
/*
void View_ShowWindow(void)
{
  // Set VDI before create window
   VDI_SetResolution(VDIModeOptions[ConfigureParams.TOSGEM.nGEMResolution],ConfigureParams.TOSGEM.nGEMColours);
}
*/

//-----------------------------------------------------------------------
/*
  Load Bitmaps, using in ToolBar/StatusBar. Delete any existing ones, and remap to Windows colours
*/
/*
void View_LoadBitmaps(void)
{
  // Free first
  View_FreeBitmaps();

  // Load bitmaps used from resource(remap colours to Windows using 'LoadImage')
  Bitmaps[BITMAP_TOOLBAR_ICONS] = (HBITMAP)LoadImage(hInst,MAKEINTRESOURCE(IDB_BITMAP_TOOLBAR_ICONS),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE|LR_LOADMAP3DCOLORS);
  Bitmaps[BITMAP_GRILL] = (HBITMAP)LoadImage(hInst,MAKEINTRESOURCE(IDB_BITMAP_GRILL),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE|LR_LOADMAP3DCOLORS);
  Bitmaps[BITMAP_RESIZE] = (HBITMAP)LoadImage(hInst,MAKEINTRESOURCE(IDB_BITMAP_RESIZE),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE|LR_LOADMAP3DCOLORS);
  Bitmaps[BITMAP_TOOLBAR_MENU] = (HBITMAP)LoadImage(hInst,MAKEINTRESOURCE(IDB_BITMAP_TOOLBAR_MENU),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE|LR_LOADMAP3DCOLORS);
  Bitmaps[BITMAP_TOOLBAR_SEPARATOR] = (HBITMAP)LoadImage(hInst,MAKEINTRESOURCE(IDB_BITMAP9),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE|LR_LOADMAP3DCOLORS);
  Bitmaps[BITMAP_STATUSBAR_ICONS] = (HBITMAP)LoadImage(hInst,MAKEINTRESOURCE(IDB_BITMAP_STATUSBAR_ICONS),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE|LR_LOADMAP3DCOLORS);
}
*/

//-----------------------------------------------------------------------
/*
  Free Bitmaps used in View window
*/
/*
void View_FreeBitmaps(void)
{
  int i;

  // Free any existing bitmaps
  for(i=0; i<MAX_BITMAPS; i++) {
    if (Bitmaps[i]) {
      DeleteObject(Bitmaps[i]);
      Bitmaps[i] = NULL;
    }
  }
}
*/

//-----------------------------------------------------------------------
/*
  Set menu 'tick's - do to full-screen and pop-up versions
*/
/*
void View_CheckMenuItem(int Control,BOOL bState)
{
  CheckMenuItem(hFullScreenMenu,Control,bState ? MF_CHECKED:MF_UNCHECKED);
  CheckMenuItem(hToolBarMenus[POPUP_MENU_OPTIONS][1],Control,bState ? MF_CHECKED:MF_UNCHECKED);    
}
*/

//-----------------------------------------------------------------------
/*
  Set all items on menu bar
*/
/*
void View_SetMenuChecks(void)
{
  View_CheckMenuItem(ID_OPTIONS_SOUND,ConfigureParams.Sound.bEnableSound);
  View_CheckMenuItem(ID_OPTIONS_MAXSPEED,ConfigureParams.Configure.nMinMaxSpeed==MINMAXSPEED_MAX);
  View_CheckMenuItem(ID_OPTIONS_CURSOREMU,ConfigureParams.Joysticks.Joy[1].bCursorEmulation);
}
*/

//-----------------------------------------------------------------------
/*
  
*/
/*
void View_SetMenuFileNames(HMENU hMenu, int InitMenuItem,int Drive)
{
  MENUITEMINFO MenuInfo;
  int i;

  // Delete any old menu items for filenames, including separator
  for(i=0; i<(MAX_FLOPPY_MENU_IMAGES+1); i++)
    RemoveMenu(hMenu,InitMenuItem,MF_BYPOSITION);

  // Do we have any filenames to add?
  if (nPreviousImageFilenames[Drive]!=0) {
    // Add separator
    memset(&MenuInfo,0x0,sizeof(MENUITEMINFO));
    MenuInfo.cbSize = sizeof(MENUITEMINFO);
    MenuInfo.fMask = MIIM_TYPE;
    MenuInfo.fType = MFT_SEPARATOR;
    InsertMenuItem(hMenu,InitMenuItem,TRUE,&MenuInfo);
    // Add filenames
    for(i=0; i<nPreviousImageFilenames[Drive]; i++) {
      memset(&MenuInfo,0x0,sizeof(MENUITEMINFO));
      MenuInfo.cbSize = sizeof(MENUITEMINFO);
      MenuInfo.fMask = MIIM_TYPE|MIIM_ID;
      MenuInfo.wID = (Drive==0) ? (ID_FLOPPYA_INSERT_SLOT1+i):(ID_FLOPPYB_INSERT_SLOT1+i);
      MenuInfo.fType = MFT_STRING;
      MenuInfo.dwTypeData = szPreviousImageFilenames[Drive][i];
      InsertMenuItem(hMenu,InitMenuItem+1+i,TRUE,&MenuInfo);
    }
  }
}
*/

//-----------------------------------------------------------------------
/*
  Add filename to Floppy menu, move item to top if already on menu or just add a fresh
*/
/*
void View_AddMenuFileName(int Drive,char *pszFileName)
{
  int i,j;

  // Is already in list?
  for(i=0; i<MAX_FLOPPY_MENU_IMAGES; i++) {
    if (!stricmp(szPreviousImageFilenames[Drive][i],pszFileName)) {
      // Found in list, move to top
      for(j=i; j>0; j--)
        strcpy(szPreviousImageFilenames[Drive][j],szPreviousImageFilenames[Drive][j-1]);
      // Copy to top
      strcpy(szPreviousImageFilenames[Drive][0],pszFileName);

      return;
    }
  }

  // Could not find in list, add to top

  // Move all down
  for(i=(MAX_FLOPPY_MENU_IMAGES-1); i>0; i--)
    strcpy(szPreviousImageFilenames[Drive][i],szPreviousImageFilenames[Drive][i-1]);
  // Add entry
  strcpy(szPreviousImageFilenames[Drive][0],pszFileName);
  nPreviousImageFilenames[Drive]++;
  if (nPreviousImageFilenames[Drive]>=MAX_FLOPPY_MENU_IMAGES)
    nPreviousImageFilenames[Drive] = MAX_FLOPPY_MENU_IMAGES;
}
*/

//-----------------------------------------------------------------------
/*
  Create and populate full-screen menu
*/
/*
void View_SetFullScreenMenu(void)
{
  SetMenu(hWnd,hFullScreenMenu);            // Set for menu display
  View_SetMenuChecks();
  // Set filenames on menu Floppy A,B
  View_SetMenuFileNames(GetSubMenu(hFullScreenMenu,1),NUM_FLOPPYA_MENU_ITEMS,0);
  View_SetMenuFileNames(GetSubMenu(hFullScreenMenu,2),NUM_FLOPPYB_MENU_ITEMS,1);

  DrawMenuBar(hWnd);
}
*/

//-----------------------------------------------------------------------
/*
  Draw background rectangle in correct Window colour
*/
/*
void View_DrawBackgroundRect(HDC hDC,RECT *pRect)
{
  HBRUSH hBrush;

  // Create brush for background
  hBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
  // Fill
  FillRect(hDC,pRect,hBrush);
  // Clean up
  DeleteObject(hBrush);
}
*/

//-----------------------------------------------------------------------
/*
  Draw line Window hi-light colour
*/
/*
void View_DrawBackgroundLineLight(HDC hDC,int x,int y,int x2,int y2)
{
  HPEN hPen,hOldPen;

  // Create pen for hi-light
  hPen = CreatePen(PS_SOLID,0,GetSysColor(COLOR_3DHILIGHT));
  hOldPen = (HPEN)SelectObject(hDC,hPen);
  MoveToEx(hDC,x,y,NULL);
  LineTo(hDC,x2,y2);
  // Clean up
  DeleteObject(SelectObject(hDC,hOldPen));
}
*/

//-----------------------------------------------------------------------
/*
  Draw line Window shadow colour
*/
/*
void View_DrawBackgroundLineShadow(HDC hDC,int x,int y,int x2,int y2)
{
  HPEN hPen,hOldPen;

  // Create pen for shadow
  hPen = CreatePen(PS_SOLID,0,GetSysColor(COLOR_3DSHADOW));
  hOldPen = (HPEN)SelectObject(hDC,hPen);
  MoveToEx(hDC,x,y,NULL);
  LineTo(hDC,x2,y2);
  // Clean up
  DeleteObject(SelectObject(hDC,hOldPen));
}
*/

//-----------------------------------------------------------------------
/*
  Draw view window, with ToolBar and StatusBar
*/
/*
void View_DrawWindow(HDC hDC)
{
  HDC MemDC;
  RECT Rect,RectCopy;

  if (!bInFullScreen) {  // Only draw when in Window, else DirectX may draw! Doh!
    MemDC = CreateCompatibleDC(hDC);
    GetClientRect(hWnd,&Rect);

    // Draw speed bar
    View_DrawBackgroundLineLight(hDC,0,0, Rect.right,0);
    RectCopy = Rect;
    RectCopy.top = 1;
    RectCopy.bottom = 26;
    View_DrawBackgroundRect(hDC,&RectCopy);
    // Draw icons
    SelectObject(MemDC,Bitmaps[BITMAP_TOOLBAR_MENU]);       // Win98 Menu bar
    BitBlt(hDC,Rect.left+1,2,6,23,MemDC,0,0,SRCCOPY);
    SelectObject(MemDC,Bitmaps[BITMAP_TOOLBAR_SEPARATOR]);  // Win98 Icon space lines
    BitBlt(hDC,Rect.left+1+42,2,2,23,MemDC,0,0,SRCCOPY);
    BitBlt(hDC,Rect.left+1+114,2,2,23,MemDC,0,0,SRCCOPY);
    BitBlt(hDC,Rect.left+1+175,2,2,23,MemDC,0,0,SRCCOPY);
    ToolBar_DrawButtons(hDC);
    SelectObject(MemDC,Bitmaps[BITMAP_GRILL]);              // ST 'Grill'
    BitBlt(hDC,Rect.right-136,1,132,17,MemDC,0,0,SRCCOPY);

    // Draw outline
    View_DrawBackgroundLineShadow(hDC,Rect.right-1,Rect.top+26, 0,Rect.top+26);
    View_DrawBackgroundLineShadow(hDC,0,Rect.top+26, 0,Rect.bottom-17);
    View_DrawBackgroundLineLight(hDC,Rect.right-1,Rect.top+26, Rect.right-1,Rect.bottom-18);
    View_DrawBackgroundLineLight(hDC,Rect.right-1,Rect.bottom-18, 0,Rect.bottom-18);

    DeleteDC(MemDC);
    
    // Draw our status bar, with text and icons
    StatusBar_Draw();
  }
}
*/


//-----------------------------------------------------------------------
/*
  Draw our full-screen menu
*/
/*
void View_DrawMenu(void)
{
  // Update menu
  if (bFullScreenHold) {
    DrawMenuBar(hWnd);
  }
}
*/


//-----------------------------------------------------------------------
/*
  Limit Windows cursor whole physical screen
*/
/*
void View_LimitCursorToScreen(void)
{
  SetCursor(Cursors[CURSOR_ARROW]);          // Restore cursor
  ClipCursor(NULL);
}
*/

//-----------------------------------------------------------------------
/*
  Set IKBD relative delta to zero
*/
void View_ResetRelativeMouseDelta(void)
{
  int x,y;

  /* Get cursor point */
  SDL_GetMouseState(&x, &y);
  /* And set so we have a zero delta */
  KeyboardProcessor.Rel.X = KeyboardProcessor.Rel.PrevX = x;
  KeyboardProcessor.Rel.Y = KeyboardProcessor.Rel.PrevY = y;  
}

//-----------------------------------------------------------------------
/*
  Limit Windows cursor to area of ST screen display
*/
/*
void View_LimitCursorToClient(void)
{
  RECT Rect;

  if (!bWindowsMouseMode) {
    // Find area of client area
    GetClientRect(hWnd,&Rect);
    // Make to screen coords, and limit cursor to this area
    ClientToScreen(hWnd,(POINT *)&Rect.left);
    ClientToScreen(hWnd,(POINT *)&Rect.right);

    // Limit mouse to client area
    ClipCursor(&Rect);
    // And make sure delta isn't passed back to emulation
    View_ResetRelativeMouseDelta();
  }
  else {
    // Allow cursor to move to all of desktop screen
    View_LimitCursorToScreen();
  }
}
*/

//-----------------------------------------------------------------------
/*
  Set our mouse mode, by passing MOUSE_xxxx. This allows for a direct set to WINDOWS or ST
  and also the ability to TOGGLE the mode between the two
  Returns TRUE if was previously in MOUSE_WINDOWS mode(used to revert mode)
*/
/*
BOOL View_ToggleWindowsMouse(int ForceToMode)
{
  int SetMode, OldWindowsMouse;

  // Store old mode
  OldWindowsMouse = bWindowsMouseMode;

  // Set which mouse mode to use
  if (ForceToMode==MOUSE_TOGGLE) {          // Toggle?
    if (bWindowsMouseMode)
      SetMode = MOUSE_ST;
    else
      SetMode = MOUSE_WINDOWS;
  }
  else                                      // Or force
    SetMode = ForceToMode;

  // Clear status bar text/reset toolbar, or show Help message if in Windows mode
  if (SetMode==MOUSE_ST)
    StatusBar_SetText("");
  else {
    ToolBar_UpdateOnMoveMove(-1,-1);
    StatusBar_SetText(pszStatusBarHelpText);
  }

  // Toggle windows mouse
  if (SetMode==MOUSE_ST) {                  // Go to ST mouse mode
    bWindowsMouseMode = FALSE;
    View_LimitCursorToClient();
    if (bCursorOn) {                        // 'ShowCursor' uses count, so make sure only turn on/off when need to!
      SetCursor(Cursors[CURSOR_NULL]);      // Use 'blank' cursor, as some graphics cards still display when turn off! Doh!
      ShowCursor(FALSE);
      bCursorOn = FALSE;
    }

    Main_UnPauseEmulation();
  }
  else {                                   // Return to Windows mouse mode!
    bWindowsMouseMode = TRUE;
    View_LimitCursorToScreen();
    if (!bCursorOn) {
      SetCursor(Cursors[CURSOR_ARROW]);    // Restore cursor
      ShowCursor(TRUE);
      bCursorOn = TRUE;
    }
  
    Main_PauseEmulation();
  }

  return(OldWindowsMouse);
}
*/


//-----------------------------------------------------------------------
/*
  Scan list of keys to NOT de-bounce when running in maximum speed, eg ALT,SHIFT,CTRL etc...
  Return TRUE if key requires de-bouncing
*/
BOOL View_DebounceSTKey(char STScanCode)
{
  int i=0;

  /* Are we in maximum speed, and have disabled key repeat? */
/*
  if ( (ConfigureParams.Configure.nMinMaxSpeed!=MINMAXSPEED_MIN) && (ConfigureParams.Keyboard.bDisableKeyRepeat) ) {
    // We should de-bounce all non extended keys, eg leave ALT,SHIFT,CTRL etc... held
    while (DebounceExtendedKeys[i]) {
      if (STScanCode==DebounceExtendedKeys[i])
        return(FALSE);
      i++;
    }

    // De-bounce key
    return(TRUE);
  }
*/
  /* Do not de-bounce key */
  return(FALSE);
}


//-----------------------------------------------------------------------
/*
  Debounce any PC key held down if running with key repeat disabled
  This is called each ST frame, so keys get held down for one VBL which is enough for 68000 code to scan
*/
void View_DebounceAllKeys(void)
{
  unsigned int Key;
  char STScanCode;
/*
  // Are we in maximum speed, and have disabled key repeat?
  if ( (ConfigureParams.Configure.nMinMaxSpeed!=MINMAXSPEED_MIN) && (ConfigureParams.Keyboard.bDisableKeyRepeat) ) {
    // Now run through each PC key looking for ones held down
    for(Key=0; Key<256; Key++) {
      // Is key held?
      if (Keyboard.KeyStates[Key]) {
        // Get scan code
        STScanCode = Keymap_RemapWindowsKeyToSTScanCode(Key);
        if (STScanCode!=-1) {
          // Does this require de-bouncing?
          if (View_DebounceSTKey(STScanCode))
            View_KeyUp(hWnd,0,MAKELONG(0,(Key&0x80) ? KF_EXTENDED|Key:Key));
        }
      }
    }
  }
*/
}

//-----------------------------------------------------------------------
/*
  Check if mouse is at edges of our Window and move back to middle to allow us to emulate
  the relative mouse mode.
  As Windows does not allow 'relative' mouse movement (on all versions of Windows) we need to
  emulate this by finding the difference between this mouse position and the previous one.
  However, Windows will limit the mouse to the edge of the screen/client area and so we need
  to move the mouse back to the middle of the window when it gets close to the edges to give
  constant relative values. Phew!
*/
void View_CheckMouseAtEdgeOfScreen(/*HWND hWnd,*/int MouseX,int MouseY)
{
/*
  RECT RectCapture;
  POINT MousePoint;

  // Get mouse point as client coordinates
  MousePoint.x = MouseX;
  MousePoint.y = MouseY;
  if (!bInFullScreen)
    ScreenToClient(hWnd,&MousePoint);

  // Calculate edges along client/full screen bounds
  View_GetMinBorderRect(&RectCapture);

  // Is mouse towards edge of client area? If so, need to reset to keep relative
  if ( (MousePoint.x<(RectCapture.left+20)) || (MousePoint.y<(RectCapture.top+20)) || (MousePoint.x>(RectCapture.right-20)) || (MousePoint.y>(RectCapture.bottom-20)) ) {
    // This is the middle of the window
    MousePoint.x = (RectCapture.right/2);
    MousePoint.y = (RectCapture.bottom/2);
    // Put back into screen coordinates for setting position back
    if (!bInFullScreen)
      ClientToScreen(hWnd,&MousePoint);
    SetCursorPos(MousePoint.x,MousePoint.y);

    // And make sure delta isn't passed back to emulation
    View_ResetRelativeMouseDelta();
  }
*/
}

//-----------------------------------------------------------------------
/*
  Store current mouse position and check for edges of window(to create relative movement)
*/
void View_UpdateSTMousePosition(void)
{
  int mx, my;

  /* Only update if in mouse emulation mode */
  if (!bWindowsMouseMode) {
    /* Get cursor position */
    SDL_GetMouseState(&mx, &my);
    KeyboardProcessor.Rel.X = mx;
    KeyboardProcessor.Rel.Y = my;
    /* Move Windows cursor back from edges of screen (creates relative movement) */
    View_CheckMouseAtEdgeOfScreen(mx, my);
  }
}


//-----------------------------------------------------------------------
/*
  User press key down
*/
void View_KeyDown( unsigned int sdlkey, unsigned int sdlmod )
{
  BOOL bPreviousKeyState;
  char STScanCode;
  unsigned int Key;

  Key = sdlkey;

  /* If using cursor emulation, DON'T send keys to keyboard processor!!! Some games use keyboard as pause! */
  if ( (ConfigureParams.Joysticks.Joy[0].bCursorEmulation || ConfigureParams.Joysticks.Joy[1].bCursorEmulation)
            && !(sdlmod&(KMOD_LSHIFT|KMOD_RSHIFT)) )
   {
    if( Key==SDLK_UP )         { cursorJoyEmu |= 1; return; }
    else if( Key==SDLK_DOWN )  { cursorJoyEmu |= 2; return; }
    else if( Key==SDLK_LEFT )  { cursorJoyEmu |= 4; return; }
    else if( Key==SDLK_RIGHT ) { cursorJoyEmu |= 8; return; }
    else if( Key==SDLK_RCTRL || Key==SDLK_KP0 )  { cursorJoyEmu |= 128; return; }
   }

  /* Set down */
  bPreviousKeyState = Keyboard.KeyStates[Key];
  Keyboard.KeyStates[Key] = TRUE;

  /*fprintf(stderr,"sdlkey=%i, sdlmod=%x\n",sdlkey,sdlmod);*/

  /* If pressed short-cut key, retain keypress until safe to execute (start of VBL) */
  if ( (sdlmod&KMOD_MODE) || (sdlkey==SDLK_F11) || (sdlkey==SDLK_F12) || (sdlkey==SDLK_PAUSE) )
   {
    ShortCutKey.Key = sdlkey;
    if( sdlmod&(KMOD_LCTRL|KMOD_RCTRL) )  ShortCutKey.bCtrlPressed = TRUE;
    if( sdlmod&(KMOD_LSHIFT|KMOD_RSHIFT) )  ShortCutKey.bShiftPressed = TRUE;
   }
  else
   {
    STScanCode = Keymap_RemapKeyToSTScanCode(Key);
    if (STScanCode!=-1)
     {
      if (!bPreviousKeyState)
        IKBD_PressSTKey(STScanCode,TRUE);
     }
   }

  /* If not running emulator check keys here and not on VBL */
  if (bWindowsMouseMode)
    ShortCut_CheckKeys();
}


//-----------------------------------------------------------------------
/*
  User released key
*/
void View_KeyUp(unsigned int sdlkey, unsigned int sdlmod)
{
  char STScanCode;
  unsigned int Key;

  Key = sdlkey;
  

  /* If using cursor emulation, DON'T send keys to keyboard processor!!! Some games use keyboard as pause! */
  if ( (ConfigureParams.Joysticks.Joy[0].bCursorEmulation || ConfigureParams.Joysticks.Joy[1].bCursorEmulation)
            && !(sdlmod&(KMOD_LSHIFT|KMOD_RSHIFT)) )
   {
    if( Key==SDLK_UP )         { cursorJoyEmu &= ~1; return; }
    else if( Key==SDLK_DOWN )  { cursorJoyEmu &= ~2; return; }
    else if( Key==SDLK_LEFT )  { cursorJoyEmu &= ~4; return; }
    else if( Key==SDLK_RIGHT ) { cursorJoyEmu &= ~8; return; }
    else if( Key==SDLK_RCTRL || Key==SDLK_KP0 )  { cursorJoyEmu &= ~128; return; }
   }

  /* Release key (only if was pressed) */
  STScanCode = Keymap_RemapKeyToSTScanCode(Key);
  if (STScanCode!=-1) {
    if (Keyboard.KeyStates[Key])
      IKBD_PressSTKey(STScanCode,FALSE);
  }

  Keyboard.KeyStates[Key] = FALSE;
}


//-----------------------------------------------------------------------
/*
  User moved mouse
*/
/*
void View_MouseMove(HWND hWnd,UINT wParam,LONG lParam)
{
  int MouseX,MouseY;

  // Are we in Windows mouse mode?
  if (bWindowsMouseMode) {
    // Get mouse coords
    MouseX = LOWORD(lParam);
    MouseY = HIWORD(lParam);

    // Restore cursor
    SetCursor(Cursors[CURSOR_ARROW]);
    // Update toolbar
    ToolBar_UpdateOnMoveMove(MouseX,MouseY);

    return;
  }
}
*/

//-----------------------------------------------------------------------
/*
  User pressed left mouse button
*/
void View_LeftMouseButtonDown()
{
  int MouseX,MouseY;

  // Are we in Windows mouse mode?
/*
  if (bWindowsMouseMode)
   {
    // Get mouse coords
    MouseX = LOWORD(lParam);
    MouseY = HIWORD(lParam);

    // Activate toolbar button and execute
    ToolBar_ActivateOnLeftButtonDown(MouseX,MouseY);
   }
  else
*/
   {
    if (Keyboard.LButtonDblClk==0)
      Keyboard.bLButtonDown |= BUTTON_MOUSE;  // Set button down flag
   }
}


//-----------------------------------------------------------------------
/*
  User released left mouse button
*/
void View_LeftMouseButtonUp()
{
  /* Are we in Windows mouse mode? */
/*
  if (!bWindowsMouseMode) {
    if (Keyboard.LButtonDblClk==0)
      Keyboard.bLButtonDown &= ~BUTTON_MOUSE;  // Button is released
  }
  else
*/
    Keyboard.bLButtonDown &= ~BUTTON_MOUSE;
}


//-----------------------------------------------------------------------
/*
  User pressed right mouse button
*/
void View_RightMouseButtonDown()
{
  int MouseX,MouseY;

  /* Are we in Windows mouse mode? */
/*
  if (bWindowsMouseMode)
   {
    // Get mouse coords
    MouseX = LOWORD(lParam);
    MouseY = HIWORD(lParam);

    // Activate toolbar button and execute
    ToolBar_ActivateOnRightButtonDown(MouseX,MouseY);
   }
  else
*/
   {
    Keyboard.bRButtonDown |= BUTTON_MOUSE;
   }
}


//-----------------------------------------------------------------------
/*
  User released right mouse button
*/
void View_RightMouseButtonUp()
{
  // Are we in Windows mouse mode?
  if (bWindowsMouseMode)
    return;

  Keyboard.bRButtonDown &= ~BUTTON_MOUSE;
}

