/*
  Hatari - dlgKeyboard.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
const char DlgKeyboard_rcsid[] = "Hatari $Id: dlgKeyboard.c,v 1.8 2006-12-19 10:55:34 thothy Exp $";

#include <unistd.h>

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "screen.h"


#define DLGKEY_SYMBOLIC  3
#define DLGKEY_SCANCODE  4
#define DLGKEY_FROMFILE  5
#define DLGKEY_MAPNAME   7
#define DLGKEY_MAPBROWSE 8
#define DLGKEY_EXIT      9


/* The keyboard dialog: */
static SGOBJ keyboarddlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,12, NULL },
  { SGTEXT, 0, 0, 13,1, 14,1, "Keyboard setup" },
  { SGTEXT, 0, 0, 2,3, 17,1, "Keyboard mapping:" },
  { SGRADIOBUT, 0, 0, 3,5, 10,1, "Symbolic" },
  { SGRADIOBUT, 0, 0, 15,5, 10,1, "Scancode" },
  { SGRADIOBUT, 0, 0, 27,5, 11,1, "From file" },
  { SGTEXT, 0, 0, 2,7, 13,1, "Mapping file:" },
  { SGTEXT, 0, 0, 2,8, 36,1, NULL },
  { SGBUTTON, 0, 0, 32,7, 6,1, "Browse" },
  { SGBUTTON, 0, 0, 10,10, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the "Keyboard" dialog.
*/
void Dialog_KeyboardDlg(void)
{
  int i, but;
  char dlgmapfile[40];
  char *tmpname;

  tmpname = malloc(FILENAME_MAX);
  if (!tmpname)
  {
    perror("Dialog_KeyboardDlg");
    return;
  }

  SDLGui_CenterDlg(keyboarddlg);

  /* Set up dialog from actual values: */
  for(i = DLGKEY_SYMBOLIC; i <= DLGKEY_FROMFILE; i++)
  {
    keyboarddlg[i].state &= ~SG_SELECTED;
  }
  keyboarddlg[DLGKEY_SYMBOLIC+DialogParams.Keyboard.nKeymapType].state |= SG_SELECTED;

  File_ShrinkName(dlgmapfile, DialogParams.Keyboard.szMappingFileName, keyboarddlg[DLGKEY_MAPNAME].w);
  keyboarddlg[DLGKEY_MAPNAME].txt = dlgmapfile;

  /* Show the dialog: */
  do
  {
    but = SDLGui_DoDialog(keyboarddlg, NULL);

    if(but == DLGKEY_MAPBROWSE)
    {
      strcpy(tmpname, DialogParams.Keyboard.szMappingFileName);
      if(!tmpname[0])
      {
        getcwd(tmpname, FILENAME_MAX);
        File_AddSlashToEndFileName(tmpname);
      }
      if( SDLGui_FileSelect(tmpname, NULL, FALSE) )
      {
        strcpy(DialogParams.Keyboard.szMappingFileName, tmpname);
        if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          File_ShrinkName(dlgmapfile, tmpname, keyboarddlg[DLGKEY_MAPNAME].w);
        else
          dlgmapfile[0] = 0;
      }
    }

  }
  while (but != DLGKEY_EXIT && but != SDLGUI_QUIT
         && but != SDLGUI_ERROR && !bQuitProgram);

  /* Read values from dialog: */
  if(keyboarddlg[DLGKEY_SYMBOLIC].state & SG_SELECTED)
    DialogParams.Keyboard.nKeymapType = KEYMAP_SYMBOLIC;
  else if(keyboarddlg[DLGKEY_SCANCODE].state & SG_SELECTED)
    DialogParams.Keyboard.nKeymapType = KEYMAP_SCANCODE;
  else
    DialogParams.Keyboard.nKeymapType = KEYMAP_LOADED;

  free(tmpname);
}
