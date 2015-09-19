/*
  Hatari - dlgKeyboard.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgKeyboard_fileid[] = "Hatari dlgKeyboard.c : " __DATE__ " " __TIME__;

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
#define DLGKEY_DISREPEAT 9
#define DLGKEY_EXIT      10


/* The keyboard dialog: */
static SGOBJ keyboarddlg[] =
{
	{ SGBOX, 0, 0, 0,0, 46,14, NULL },
	{ SGTEXT, 0, 0, 16,1, 14,1, "Keyboard setup" },
	{ SGTEXT, 0, 0, 2,3, 17,1, "Keyboard mapping:" },
	{ SGRADIOBUT, 0, 0,  4,5, 10,1, "_Symbolic" },
	{ SGRADIOBUT, 0, 0, 17,5, 10,1, "S_cancode" },
	{ SGRADIOBUT, 0, 0, 30,5, 11,1, "_From file" },
	{ SGTEXT, 0, 0, 2,7, 13,1, "Mapping file:" },
	{ SGTEXT, 0, 0, 2,8, 42,1, NULL },
	{ SGBUTTON,   0, 0, 36, 7,  8,1, "_Browse" },
	{ SGCHECKBOX, 0, 0,  2,10, 41,1, "_Disable key repeat in fast forward mode" },
	{ SGBUTTON, SG_DEFAULT, 0, 13,12, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "Keyboard" dialog.
 */
void Dialog_KeyboardDlg(void)
{
	int i, but;
	char dlgmapfile[44];

	SDLGui_CenterDlg(keyboarddlg);

	/* Set up dialog from actual values: */
	for (i = DLGKEY_SYMBOLIC; i <= DLGKEY_FROMFILE; i++)
	{
		keyboarddlg[i].state &= ~SG_SELECTED;
	}
	keyboarddlg[DLGKEY_SYMBOLIC+ConfigureParams.Keyboard.nKeymapType].state |= SG_SELECTED;

	File_ShrinkName(dlgmapfile, ConfigureParams.Keyboard.szMappingFileName,
	                keyboarddlg[DLGKEY_MAPNAME].w);
	keyboarddlg[DLGKEY_MAPNAME].txt = dlgmapfile;

	if (ConfigureParams.Keyboard.bDisableKeyRepeat)
		keyboarddlg[DLGKEY_DISREPEAT].state |= SG_SELECTED;
	else
		keyboarddlg[DLGKEY_DISREPEAT].state &= ~SG_SELECTED;

	/* Show the dialog: */
	do
	{
		but = SDLGui_DoDialog(keyboarddlg, NULL, false);

		if (but == DLGKEY_MAPBROWSE)
		{
			SDLGui_FileConfSelect("Keyboard mapping file:", dlgmapfile,
			                      ConfigureParams.Keyboard.szMappingFileName,
			                      keyboarddlg[DLGKEY_MAPNAME].w, false);
		}
	}
	while (but != DLGKEY_EXIT && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read values from dialog: */
	if (keyboarddlg[DLGKEY_SYMBOLIC].state & SG_SELECTED)
		ConfigureParams.Keyboard.nKeymapType = KEYMAP_SYMBOLIC;
	else if (keyboarddlg[DLGKEY_SCANCODE].state & SG_SELECTED)
		ConfigureParams.Keyboard.nKeymapType = KEYMAP_SCANCODE;
	else
		ConfigureParams.Keyboard.nKeymapType = KEYMAP_LOADED;

	ConfigureParams.Keyboard.bDisableKeyRepeat = (keyboarddlg[DLGKEY_DISREPEAT].state & SG_SELECTED);
}
