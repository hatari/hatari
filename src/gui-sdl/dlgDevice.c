/*
  Hatari - dlgDevice.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
static char rcsid[] = "Hatari $Id: dlgDevice.c,v 1.1 2003-08-04 19:37:31 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


/* The devices dialog: */
static SGOBJ devicedlg[] =
{
  { SGBOX, 0, 0, 0,0, 30,8, NULL },
  { SGTEXT, 0, 0, 8,2, 13,1, "Devices setup" },
  { SGTEXT, 0, 0, 2,4, 25,1, "Sorry, not yet supported." },
  { SGBUTTON, 0, 0, 5,6, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the "Keyboard" dialog.
*/
void Dialog_DeviceDlg(void)
{
  SDLGui_CenterDlg(devicedlg);
  SDLGui_DoDialog(devicedlg);
}
