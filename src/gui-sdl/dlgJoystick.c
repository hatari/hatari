/*
  Hatari - dlgJoystick.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgJoystick_rcsid[] = "Hatari $Id: dlgJoystick.c,v 1.2 2004-04-19 08:53:48 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


#define DLGJOY_J1CURSOR    4
#define DLGJOY_J1AUTOFIRE  5
#define DLGJOY_J0CURSOR    8
#define DLGJOY_J0AUTOFIRE  9
#define DLGJOY_EXIT        10


/* The joysticks dialog: */
static SGOBJ joystickdlg[] =
{
  { SGBOX, 0, 0, 0,0, 30,19, NULL },
  { SGTEXT, 0, 0, 7,1, 15,1, "Joysticks setup" },
  { SGBOX, 0, 0, 1,3, 28,6, NULL },
  { SGTEXT, 0, 0, 2,4, 11,1, "Joystick 1:" },
  { SGCHECKBOX, 0, 0, 5,6, 22,1, "Use cursor emulation" },
  { SGCHECKBOX, 0, 0, 5,7, 17,1, "Enable autofire" },
  { SGBOX, 0, 0, 1,10, 28,6, NULL },
  { SGTEXT, 0, 0, 2,11, 11,1, "Joystick 0:" },
  { SGCHECKBOX, 0, 0, 5,13, 22,1, "Use cursor emulation" },
  { SGCHECKBOX, 0, 0, 5,14, 17,1, "Enable autofire" },
  { SGBUTTON, 0, 0, 5,17, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the joystick dialog.
*/
void Dialog_JoyDlg(void)
{
  int but;

  SDLGui_CenterDlg(joystickdlg);

  /* Set up dialog from actual values: */

  if( DialogParams.Joysticks.Joy[1].bCursorEmulation )
    joystickdlg[DLGJOY_J1CURSOR].state |= SG_SELECTED;
  else
    joystickdlg[DLGJOY_J1CURSOR].state &= ~SG_SELECTED;

  if( DialogParams.Joysticks.Joy[1].bEnableAutoFire )
    joystickdlg[DLGJOY_J1AUTOFIRE].state |= SG_SELECTED;
  else
    joystickdlg[DLGJOY_J1AUTOFIRE].state &= ~SG_SELECTED;

  if( DialogParams.Joysticks.Joy[0].bCursorEmulation )
    joystickdlg[DLGJOY_J0CURSOR].state |= SG_SELECTED;
  else
    joystickdlg[DLGJOY_J0CURSOR].state &= ~SG_SELECTED;

  if( DialogParams.Joysticks.Joy[0].bEnableAutoFire )
    joystickdlg[DLGJOY_J0AUTOFIRE].state |= SG_SELECTED;
  else
    joystickdlg[DLGJOY_J0AUTOFIRE].state &= ~SG_SELECTED;

  do
  {
    but = SDLGui_DoDialog(joystickdlg);
  }
  while( but!=DLGJOY_EXIT && !bQuitProgram );

  /* Read values from dialog */
  DialogParams.Joysticks.Joy[1].bCursorEmulation = (joystickdlg[DLGJOY_J1CURSOR].state & SG_SELECTED);
  DialogParams.Joysticks.Joy[1].bEnableAutoFire = (joystickdlg[DLGJOY_J1AUTOFIRE].state & SG_SELECTED);
  DialogParams.Joysticks.Joy[0].bCursorEmulation = (joystickdlg[DLGJOY_J0CURSOR].state & SG_SELECTED);
  DialogParams.Joysticks.Joy[0].bEnableAutoFire = (joystickdlg[DLGJOY_J0AUTOFIRE].state & SG_SELECTED);
}
