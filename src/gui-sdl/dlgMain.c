/*
  Hatari - dlgMain.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  The main dialog.
*/
static char rcsid[] = "Hatari $Id: dlgMain.c,v 1.1 2003-08-04 19:37:31 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "screen.h"

/* Prototypes of the dialog functions: */
void Dialog_AboutDlg(void);
void Dialog_DiscDlg(void);
void Dialog_TosGemDlg(void);
void Dialog_ScreenDlg(void);
void Dialog_SoundDlg(void);
void Dialog_SystemDlg(void);
void Dialog_MemDlg(void);
void Dialog_JoyDlg(void);
void Dialog_KeyboardDlg(void);
void Dialog_DeviceDlg(void);


#define MAINDLG_ABOUT    2
#define MAINDLG_DISCS    3
#define MAINDLG_TOSGEM   4
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
  { SGBUTTON, 0, 0, 4,6, 12,1, "Discs" },
  { SGBUTTON, 0, 0, 4,8, 12,1, "TOS/GEM" },
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

  if(SDLGui_PrepareFont())
    return FALSE;

  SDLGui_CenterDlg(maindlg);
  SDL_ShowCursor(SDL_ENABLE);

  maindlg[MAINDLG_NORESET].state |= SG_SELECTED;
  maindlg[MAINDLG_RESET].state &= ~SG_SELECTED;

  do
  {
    retbut = SDLGui_DoDialog(maindlg);
    switch(retbut)
    {
      case MAINDLG_ABOUT:
        Dialog_AboutDlg();
        break;
      case MAINDLG_DISCS:
        Dialog_DiscDlg();
        break;
      case MAINDLG_TOSGEM:
        Dialog_TosGemDlg();
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
        {
          CNF_PARAMS tmpParams;
          /* Configuration_Load uses the variables from ConfigureParams.
           * That's why we have to temporarily back it up here */
          tmpParams = ConfigureParams;
          Configuration_Load();
          DialogParams = ConfigureParams;
          ConfigureParams = tmpParams;
        }
        break;
      case MAINDLG_SAVECFG:
        {
          CNF_PARAMS tmpParams;
          /* Configuration_Save uses the variables from ConfigureParams.
           * That's why we have to temporarily back it up here */
          tmpParams = ConfigureParams;
          ConfigureParams = DialogParams;
          Configuration_Save();
          ConfigureParams = tmpParams;
        }
        break;
      case MAINDLG_QUIT:
        bQuitProgram = TRUE;
        break;
    }
    Screen_SetFullUpdate();
    Screen_Draw();
  }
  while(retbut!=MAINDLG_OK && retbut!=MAINDLG_CANCEL && !bQuitProgram);

  SDL_ShowCursor(SDL_DISABLE);

  if( maindlg[MAINDLG_RESET].state & SG_SELECTED )
    *bReset = TRUE;
  else
    *bReset = FALSE;

  return(retbut == MAINDLG_OK);
}
