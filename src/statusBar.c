/*
  Hatari

  Status Bar icons and text
*/

#include "main.h"
#include "screen.h"
#include "statusBar.h"
#include "view.h"

#define  DEFAULT_STATUS_ICON_TIME  15    // Time to show status bar icon in frames
#define  ON_STATUS_ICON_TIME      -1     // Force icon to show all the time

STATUSICON StatusIcons[STATUS_ICON_COUNT] = {
  { STATUS_ICONS_X_OFFSET,112,0,16,13, 0 },    // STATUS_ICON_FRAMERATE
  { STATUS_ICONS_X_OFFSET-16,16,0,12,13, 0 },  // STATUS_ICON_FLOPPY
  { STATUS_ICONS_X_OFFSET-28,32,0,13,13, 0 },  // STATUS_ICON_HARDDRIVE
  { STATUS_ICONS_X_OFFSET-41,48,0,16,13, 0 },  // STATUS_ICON_PRINTER
  { STATUS_ICONS_X_OFFSET-41,64,0,16,13, 0 },  // STATUS_ICON_RS232
  { STATUS_ICONS_X_OFFSET-57,80,0,14,13, 0 },  // STATUS_ICON_SOUND
  { STATUS_ICONS_X_OFFSET-71,96,0,16,13, 0 },  // STATUS_ICON_SCREEN
};
char *pszStatusBarHelpText = { "Press F1 for Help." };
static char szStatusBarText[256];        // Text in status bar

//-----------------------------------------------------------------------
/*
  Set to display icon on status bar for at least 'x' frames
*/
void StatusBar_SetIcon(int StatusIcon, int IconState)
{
  // Check state as ON, OFF or UPDATE
  switch (IconState) {
    case ICONSTATE_ON:
      // Set icon to display
      StatusIcons[StatusIcon].DisplayCount = ON_STATUS_ICON_TIME;
      // Draw icon
      StatusBar_DrawIcon(/*NULL,NULL,*/StatusIcon);
      break;
    case ICONSTATE_OFF:
      // Set icon to off
      StatusIcons[StatusIcon].DisplayCount = 0;
      // Draw icon
      StatusBar_DrawIcon(/*NULL,NULL,*/StatusIcon);
      break;
    case ICONSTATE_UPDATE:
      // Is icon already on? No
      if (StatusIcons[StatusIcon].DisplayCount==0) {
        // Set icon to display for 'x' frames
        StatusIcons[StatusIcon].DisplayCount = DEFAULT_STATUS_ICON_TIME;
        // Draw icon
        StatusBar_DrawIcon(/*NULL,NULL,*/StatusIcon);
      }
      else  // Keep count high
        StatusIcons[StatusIcon].DisplayCount = DEFAULT_STATUS_ICON_TIME;
      break;
  }
}

//-----------------------------------------------------------------------
/*
  Update icon on status bar, call once per display frame
*/
void StatusBar_UpdateIcons(void)
{
  int i;

  // Adjust timers on icons and update if needed
  for(i=0; i<STATUS_ICON_COUNT; i++) {
    // Is on? Decrement count to zero(off)
    if (StatusIcons[i].DisplayCount>0) {
      StatusIcons[i].DisplayCount--;

      // Is now off? Update status bar
      if (StatusIcons[i].DisplayCount==0)
        StatusBar_DrawIcon(/*NULL,NULL,*/i);
    }
  }
}

//-----------------------------------------------------------------------
/*
  Draw all icons on status bar
*/
void StatusBar_DrawAllIcons(void)
{
  int i;
/* FIXME */
/*
  HDC hDC,MemDC;

  // Select bitmap for icons
  hDC = GetDC(hWnd);
  MemDC = CreateCompatibleDC(hDC);
  SelectObject(MemDC,Bitmaps[BITMAP_STATUSBAR_ICONS]);
*/
  // Draw all icons with current state
  for(i=0; i<STATUS_ICON_COUNT; i++)
    StatusBar_DrawIcon(/*hDC,MemDC,*/i);
/*
  // Release icon bitmap
  ReleaseDC(hWnd,hDC);
  DeleteDC(MemDC);
*/
}

//-----------------------------------------------------------------------
/*
  Draw a single icon on status bar
*/
void StatusBar_DrawIcon(/*HDC hDC, HDC MemDC,*/int StatusIcon)
{
/* FIXME */
/*
  HDC DrawDC,DrawMemDC;
  XYWH SrcBitmapPos;
  RECT Rect;

  if (!bInFullScreen) {  // Only draw when in Window, else DirectX may draw! Doh!
    DrawDC = hDC;
    if (hDC==NULL)
      DrawDC = GetDC(hWnd);
    DrawMemDC = MemDC;
    if (MemDC==NULL) {
      DrawMemDC = CreateCompatibleDC(DrawDC);
      SelectObject(DrawMemDC,Bitmaps[BITMAP_STATUSBAR_ICONS]);
    }

    // So, are we on or off?
    if (StatusIcons[StatusIcon].DisplayCount) {  // On
      SrcBitmapPos.x = StatusIcons[StatusIcon].SrcBitmapPos.x;
      SrcBitmapPos.y = StatusIcons[StatusIcon].SrcBitmapPos.y;
    }
    else                    // Off(grey out)
      SrcBitmapPos.x = SrcBitmapPos.y = 0;
    // And draw icon from bottom-right corner
    GetClientRect(hWnd,&Rect);
    BitBlt(DrawDC,Rect.right-StatusIcons[StatusIcon].x,Rect.bottom-STATUS_ICONS_Y_OFFSET,StatusIcons[StatusIcon].SrcBitmapPos.w,StatusIcons[StatusIcon].SrcBitmapPos.h,DrawMemDC,SrcBitmapPos.x,SrcBitmapPos.y,SRCCOPY);

    // Delete bitmap object
    if (hDC==NULL)
      ReleaseDC(hWnd,DrawDC);
    if (MemDC==NULL)
      DeleteDC(DrawMemDC);
  }
*/
}

//-----------------------------------------------------------------------
/*
  Set status bar text and update window
*/
void StatusBar_SetText(char *pString)
{
  // Set text
  strcpy(szStatusBarText,pString);
  // And redraw, clear background
  StatusBar_DrawText(TRUE);
}

//-----------------------------------------------------------------------
/*
  Draw status bar
*/
void StatusBar_Draw(void)
{
/* FIXME */
/*
  RECT Rect;
  HDC hDC,MemDC;

  // Setup
  hDC = GetDC(hWnd);
  MemDC = CreateCompatibleDC(hDC);

  // Draw status bar, with resize icon
  GetClientRect(hWnd,&Rect);
  Rect.top = Rect.bottom-17;
  View_DrawBackgroundRect(hDC,&Rect);
  SelectObject(MemDC,Bitmaps[BITMAP_RESIZE]);
  BitBlt(hDC,Rect.right-16,Rect.bottom-14,14,14,MemDC,0,0,SRCCOPY);
  
  // Complete
  ReleaseDC(hWnd,hDC);
  DeleteDC(MemDC);

  // And draw text, don't clear background as already done
  StatusBar_DrawText(FALSE);
  // And icons
  StatusBar_DrawAllIcons();
*/
}

//-----------------------------------------------------------------------
/*
  Draw text into status bar
*/
void StatusBar_DrawText(BOOL bClearBackground)
{
/* FIXME */
/*
  HFONT OldFont;
  RECT Rect,ClearRect;
  HDC hDC;
  int x,y;

  if (!bInFullScreen) {  // Only draw when in Window, else DirectX may draw! Doh!
    hDC = GetDC(hWnd);

    // Find where status bar is!
    GetClientRect(hWnd,&Rect);

    // Clear status bar - just text area
    ClearRect = Rect;
    ClearRect.left = STATUS_TEXT_X_OFFSET;
    ClearRect.right -= STATUS_ICONS_X_OFFSET;
    ClearRect.top = ClearRect.bottom - STATUS_ICONS_Y_OFFSET;
    ClearRect.bottom -= 1;
    View_DrawBackgroundRect(hDC,&ClearRect);

    // Find coords to place text
    x = Rect.left + STATUS_TEXT_X_OFFSET;
    y = Rect.bottom - STATUS_ICONS_Y_OFFSET;

    // Draw new text string
    SetBkMode(hDC,TRANSPARENT);          // Font
    OldFont = (HFONT)SelectObject(hDC,(HFONT)GetStockObject(ANSI_VAR_FONT));
    TextOut(hDC,x,y,szStatusBarText,strlen(szStatusBarText));
    SelectObject(hDC,OldFont);

    ReleaseDC(hWnd,hDC);
  }
*/
}
