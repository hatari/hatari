/*
  Hatari - dlgMain.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  The main dialog.
*/
const char DlgMain_rcsid[] = "Hatari $Id: dlgMain.c,v 1.10 2006-07-03 20:36:28 clafou Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "screen.h"


#define MAINDLG_ABOUT    2
#define MAINDLG_DISKS    3
#define MAINDLG_ROM      4
#define MAINDLG_SCREEN   5
#define MAINDLG_SOUND    6
#define MAINDLG_CPU      7
#define MAINDLG_MEMORY   8
#define MAINDLG_JOY      9
#define MAINDLG_KEYBD    10
#define MAINDLG_DEVICES  11
#define MAINDLG_LOADCFG  12
#define MAINDLG_SAVECFG  13
#define MAINDLG_NORESET  14
#define MAINDLG_RESET    15
#define MAINDLG_OK       16
#define MAINDLG_CANCEL   17
#define MAINDLG_QUIT     18


/* The main dialog: */
static SGOBJ maindlg[] =
{
  { SGBOX, 0, 0, 0,0, 36,22, NULL },
  { SGTEXT, 0, 0, 10,1, 16,1, "Hatari main menu" },
  { SGBUTTON, 0, 0, 4,4, 12,1, "About" },
  { SGBUTTON, 0, 0, 4,6, 12,1, "Disks" },
  { SGBUTTON, 0, 0, 4,8, 12,1, "ROM" },
  { SGBUTTON, 0, 0, 4,10, 12,1, "Screen" },
  { SGBUTTON, 0, 0, 4,12, 12,1, "Sound" },
  { SGBUTTON, 0, 0, 20,4, 12,1, "System" },
  { SGBUTTON, 0, 0, 20,6, 12,1, "Memory" },
  { SGBUTTON, 0, 0, 20,8, 12,1, "Joysticks" },
  { SGBUTTON, 0, 0, 20,10, 12,1, "Keyboard" },
  { SGBUTTON, 0, 0, 20,12, 12,1, "Devices" },
  { SGBUTTON, 0, 0, 3,15, 14,1, "Load config." },
  { SGBUTTON, 0, 0, 19,15, 14,1, "Save config." },
  { SGRADIOBUT, 0, 0, 2,18, 10,1, "No Reset" },
  { SGRADIOBUT, 0, 0, 2,20, 10,1, "Reset ST" },
  { SGBUTTON, 0, 0, 14,18, 8,3, "Okay" },
  { SGBUTTON, 0, 0, 25,20, 8,1, "Cancel" },
  { SGBUTTON, 0, 0, 25,18, 8,1, "Quit" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  This functions sets up the actual font and then displays the main dialog.
*/
int Dialog_MainDlg(BOOL *bReset)
{
  int retbut;
  BOOL bOldMouseVisibility;
  int nOldMouseX, nOldMouseY;

  if(SDLGui_SetScreen(sdlscrn))
    return FALSE;

  SDL_GetMouseState(&nOldMouseX, &nOldMouseY);
  bOldMouseVisibility = SDL_ShowCursor(SDL_QUERY);
  SDL_ShowCursor(SDL_ENABLE);

  SDLGui_CenterDlg(maindlg);

  maindlg[MAINDLG_NORESET].state |= SG_SELECTED;
  maindlg[MAINDLG_RESET].state &= ~SG_SELECTED;

  do
  {
    retbut = SDLGui_DoDialog(maindlg, NULL);
    switch(retbut)
    {
      case MAINDLG_ABOUT:
        Dialog_AboutDlg();
        break;
      case MAINDLG_DISKS:
        Dialog_DiskDlg();
        break;
      case MAINDLG_ROM:
        DlgRom_Main();
        break;
      case MAINDLG_SCREEN:
        Dialog_ScreenDlg();
        break;
      case MAINDLG_SOUND:
        Dialog_SoundDlg();
        break;
      case MAINDLG_CPU:
        Dialog_SystemDlg();
        break;
      case MAINDLG_MEMORY:
        Dialog_MemDlg();
        break;
      case MAINDLG_JOY:
        Dialog_JoyDlg();
        break;
      case MAINDLG_KEYBD:
        Dialog_KeyboardDlg();
        break;
      case MAINDLG_DEVICES:
        Dialog_DeviceDlg();
        break;
      case MAINDLG_LOADCFG:
		Dialog_LoadParams();
        break;
      case MAINDLG_SAVECFG:
		Dialog_SaveParams();
        break;
      case MAINDLG_QUIT:
        bQuitProgram = TRUE;
        break;
    }
  }
  while (retbut!=MAINDLG_OK && retbut!=MAINDLG_CANCEL && retbut!=SDLGUI_QUIT && !bQuitProgram);


  if( maindlg[MAINDLG_RESET].state & SG_SELECTED )
    *bReset = TRUE;
  else
    *bReset = FALSE;

  SDL_ShowCursor(bOldMouseVisibility);
  Main_WarpMouse(nOldMouseX, nOldMouseY);

  return(retbut == MAINDLG_OK);
}
