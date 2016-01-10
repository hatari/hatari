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
#include "str.h"
#include "keymap.h"

#define DLGKEY_SYMBOLIC  4
#define DLGKEY_SCANCODE  5
#define DLGKEY_FROMFILE  6
#define DLGKEY_MAPNAME   8
#define DLGKEY_MAPBROWSE 9
#define DLGKEY_SCPREV    13
#define DLGKEY_SCNAME    14
#define DLGKEY_SCNEXT    15
#define DLGKEY_SCMODVAL  17
#define DLGKEY_SCMODDEF  18
#define DLGKEY_SCNOMODVAL 20
#define DLGKEY_SCNOMODDEF 21
#define DLGKEY_DISREPEAT 22
#define DLGKEY_EXIT      23

static char sc_modval[16];
static char sc_nomodval[16];

/* The keyboard dialog: */
static SGOBJ keyboarddlg[] =
{
	{ SGBOX, 0, 0, 0,0, 46,24, NULL },
	{ SGTEXT, 0, 0, 16,1, 14,1, "Keyboard setup" },

	{ SGBOX, 0, 0, 1,3, 44,7, NULL },
	{ SGTEXT, 0, 0, 2,3, 17,1, "Keyboard mapping:" },
	{ SGRADIOBUT, 0, 0,  4,5, 10,1, "_Symbolic" },
	{ SGRADIOBUT, 0, 0, 17,5, 10,1, "S_cancode" },
	{ SGRADIOBUT, 0, 0, 30,5, 11,1, "_From file" },
	{ SGTEXT, 0, 0, 2,7, 13,1, "Mapping file:" },
	{ SGTEXT, 0, 0, 2,8, 42,1, NULL },
	{ SGBUTTON,   0, 0, 36, 7,  8,1, "_Browse" },

	{ SGBOX, 0, 0, 1,11, 44,8, NULL },
	{ SGTEXT, 0, 0, 2,11, 12,1, "Shortcuts:" },
	{ SGBOX, 0, 0, 2,13, 42,1, NULL },
	{ SGBUTTON, 0, 0, 2,13, 1,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGTEXT, 0, 0, 4,13, 20,1, NULL },
	{ SGBUTTON, 0, 0, 43,13, 1,1, "\x03", SG_SHORTCUT_RIGHT },
	{ SGTEXT, 0, 0, 2,15, 17,1, "With modifier:" },
	{ SGTEXT, 0, 0, 20,15, 12,1, sc_modval },
	{ SGBUTTON, 0, 0, 36,15, 8,1, "_Define" },
	{ SGTEXT, 0, 0, 2,17, 17,1, "Without modifier:" },
	{ SGTEXT, 0, 0, 20,17, 12,1, sc_nomodval },
	{ SGBUTTON, 0, 0, 36,17, 8,1, "D_efine" },

	{ SGCHECKBOX, 0, 0,  2,20, 41,1, "Disable key _repeat in fast forward mode" },
	{ SGBUTTON, SG_DEFAULT, 0, 13,22, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


static char *sc_names[SHORTCUT_KEYS] = {
	"Edit settings",
	"Toggle fullscreen",
	"Grab mouse",
	"Cold reset",
	"Warm reset",
	"Take screenshot",
	"Boss key",
	"Joystick cursor emulation",
	"Fast forward",
	"Record animation",
	"Record sound",
	"Toggle sound",
	"Enter debugger",
	"Pause emulation",
	"Quit emulator",
	"Load memory snapshot",
	"Save memory snapshot",
	"Insert disk A:",
	"Toggle joystick 0",
	"Toggle joystick 1",
	"Toggle joypad A",
	"Toggle joypad B"
};

static char sScKeyType[28];
static char sScKeyName[28];

static SGOBJ sckeysdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 30,6, NULL },
	{ SGTEXT, 0, 0, 2,1, 28,1, "Press key for:" },
	{ SGTEXT, 0, 0, 2,2, 28,1, sScKeyType },
	{ SGTEXT, 0, 0, 2,4, 28,1, sScKeyName },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/**
 * Show dialogs for defining shortcut keys and wait for a key press.
 */
static void DlgKbd_DefineShortcutKey(int sc, bool withMod)
{
	SDL_Event sdlEvent;
	int *pscs;
	int i;

	if (bQuitProgram)
		return;

	SDLGui_CenterDlg(sckeysdlg);

	if (withMod)
		pscs = ConfigureParams.Shortcut.withModifier;
	else
		pscs = ConfigureParams.Shortcut.withoutModifier;

	snprintf(sScKeyType, sizeof(sScKeyType), "'%s'", sc_names[sc]);
	snprintf(sScKeyName, sizeof(sScKeyName), "(was: '%s')", Keymap_GetKeyName(pscs[sc]));

	SDLGui_DrawDialog(sckeysdlg);

	/* drain buffered key events */
	SDL_Delay(200);
	while (SDL_PollEvent(&sdlEvent))
	{
		if (sdlEvent.type == SDL_KEYUP || sdlEvent.type == SDL_KEYDOWN)
			break;
	}

	/* get the real key */
	do
	{
		SDL_WaitEvent(&sdlEvent);
		switch (sdlEvent.type)
		{
		 case SDL_KEYDOWN:
			pscs[sc] = sdlEvent.key.keysym.sym;
			snprintf(sScKeyName, sizeof(sScKeyName), "(now: '%s')",
			         Keymap_GetKeyName(sdlEvent.key.keysym.sym));
			SDLGui_DrawDialog(sckeysdlg);
			break;
		 case SDL_MOUSEBUTTONDOWN:
			if (sdlEvent.button.button == SDL_BUTTON_RIGHT)
			{
				pscs[sc] = 0;
				return;
			}
			else if (sdlEvent.button.button == SDL_BUTTON_LEFT)
			{
				return;
			}
			break;
		 case SDL_QUIT:
			bQuitProgram = true;
			return;
		}
	} while (sdlEvent.type != SDL_KEYUP);

	/* Make sure that no other shortcut key has the same value */
	for (i = 0; i < SHORTCUT_KEYS; i++)
	{
		if (i == sc)
			continue;
		if (pscs[i] == pscs[sc])
		{
			pscs[i] = 0;
			DlgAlert_Notice("Removing key from other shortcut!");
		}
	}
}


/**
 * Set name for given sortcut, or show it's unset
 */
static void DlgKbd_SetName(char *str, size_t maxlen, int keysym)
{
	if (keysym)
		strlcpy(str, Keymap_GetKeyName(keysym), maxlen);
	else
		strlcpy(str, "<not set>", maxlen);
}


/**
 * Refresh the shortcut texts in the dialog
 */
static void DlgKbd_RefreshShortcuts(int sc)
{
	int keysym;

	/* with modifier */
	keysym = ConfigureParams.Shortcut.withModifier[sc];
	DlgKbd_SetName(sc_modval, sizeof(sc_modval), keysym);

	/* without modifier */
	keysym = ConfigureParams.Shortcut.withoutModifier[sc];
	DlgKbd_SetName(sc_nomodval, sizeof(sc_nomodval), keysym);

	keyboarddlg[DLGKEY_SCNAME].txt = sc_names[sc];
}

/**
 * Show and process the "Keyboard" dialog.
 */
void Dialog_KeyboardDlg(void)
{
	int i, but;
	char dlgmapfile[44];
	int cur_sc = 0;

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

	DlgKbd_RefreshShortcuts(cur_sc);

	if (ConfigureParams.Keyboard.bDisableKeyRepeat)
		keyboarddlg[DLGKEY_DISREPEAT].state |= SG_SELECTED;
	else
		keyboarddlg[DLGKEY_DISREPEAT].state &= ~SG_SELECTED;

	/* Show the dialog: */
	do
	{
		but = SDLGui_DoDialog(keyboarddlg, NULL, false);

		switch (but)
		{
		 case DLGKEY_MAPBROWSE:
			SDLGui_FileConfSelect("Keyboard mapping file:", dlgmapfile,
			                      ConfigureParams.Keyboard.szMappingFileName,
			                      keyboarddlg[DLGKEY_MAPNAME].w, false);
			break;
		 case DLGKEY_SCPREV:
			if (cur_sc > 0)
			{
				--cur_sc;
				DlgKbd_RefreshShortcuts(cur_sc);
			}
			break;
		 case DLGKEY_SCNEXT:
			if (cur_sc < SHORTCUT_KEYS-1)
			{
				++cur_sc;
				DlgKbd_RefreshShortcuts(cur_sc);
			}
			break;
		 case DLGKEY_SCMODDEF:
			DlgKbd_DefineShortcutKey(cur_sc, true);
			DlgKbd_RefreshShortcuts(cur_sc);
			break;
		 case DLGKEY_SCNOMODDEF:
			DlgKbd_DefineShortcutKey(cur_sc, false);
			DlgKbd_RefreshShortcuts(cur_sc);
			break;
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
