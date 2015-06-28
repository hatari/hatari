/*
  Hatari - dlgJoystick.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgJoystick_fileid[] = "Hatari dlgJoystick.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "joy.h"

#define DLGJOY_STJOYNAME   3
#define DLGJOY_PREVSTJOY   4
#define DLGJOY_NEXTSTJOY   5
#define DLGJOY_DEFINEKEYS  7
#define DLGJOY_DISABLED    8
#define DLGJOY_USEKEYS     9
#define DLGJOY_USEREALJOY 10
#define DLGJOY_SDLJOYNAME 12
#define DLGJOY_PREVSDLJOY 13
#define DLGJOY_NEXTSDLJOY 14
#define DLGJOY_AUTOFIRE   15
#define DLGJOY_EXIT       16

/* The joysticks dialog: */

static char sSdlStickName[20];

static SGOBJ joydlg[] =
{
	{ SGBOX, 0, 0, 0,0, 32,18, NULL },
	{ SGTEXT, 0, 0, 8,1, 15,1, "Joysticks setup" },

	{ SGBOX, 0, 0, 4,3, 24,1, NULL },
	{ SGTEXT, 0, 0, 5,3, 22,1, NULL },
	{ SGBUTTON, 0, 0,  1,3, 3,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGBUTTON, 0, 0, 28,3, 3,1, "\x03", SG_SHORTCUT_RIGHT },

	{ SGBOX, 0, 0, 1,4, 30,11, NULL },
	{ SGBUTTON,   0, 0, 19,7, 11,1, "D_efine keys" },

	{ SGRADIOBUT, 0, 0, 2,5, 10,1, "_disabled" },
	{ SGRADIOBUT, 0, 0, 2,7, 14,1, "use _keyboard" },
	{ SGRADIOBUT, 0, 0, 2,9, 20,1, "use real _joystick:" },

	{ SGBOX, 0, 0, 5,11, 22,1, NULL },
	{ SGTEXT, 0, 0, 6,11, 20,1, sSdlStickName },
	{ SGBUTTON, 0, 0,  4,11, 1,1, "\x04", SG_SHORTCUT_UP },
	{ SGBUTTON, 0, 0, 27,11, 1,1, "\x03", SG_SHORTCUT_DOWN },

	{ SGCHECKBOX, 0, 0, 2,13, 17,1, "Enable _autofire" },

	{ SGBUTTON, SG_DEFAULT, 0, 6,16, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/* The joystick keys setup dialog: */

static char sKeyInstruction[24];
static char sKeyName[24];

static SGOBJ joykeysdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 28,5, NULL },
	{ SGTEXT, 0, 0, 2,1, 24,1, sKeyInstruction },
	{ SGTEXT, 0, 0, 2,3, 24,1, sKeyName },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


static char *sJoystickNames[JOYSTICK_COUNT] =
{
	"ST Joystick 0",
	"ST Joystick 1",
	"STE Joypad A",
	"STE Joypad B",
	"Parallel port stick 1",
	"Parallel port stick 2"
};


/*-----------------------------------------------------------------------*/
/**
 * Show dialogs for defining joystick keys and wait for a key press.
 */
static void DlgJoystick_DefineOneKey(char *pType, int *pKey)
{
	SDL_Event sdlEvent;

	if (bQuitProgram)
		return;

	snprintf(sKeyInstruction, sizeof(sKeyInstruction), "Press key for '%s'...", pType);
	snprintf(sKeyName, sizeof(sKeyName), "(was: '%s')", SDL_GetKeyName(*pKey));

	SDLGui_DrawDialog(joykeysdlg);

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
		if (sdlEvent.type == SDL_KEYDOWN)
		{
			*pKey = sdlEvent.key.keysym.sym;
			snprintf(sKeyName, sizeof(sKeyName), "(now: '%s')", SDL_GetKeyName(*pKey));
			SDLGui_DrawDialog(joykeysdlg);
		}
		else if (sdlEvent.type == SDL_QUIT)
		{
			bQuitProgram = true;
			return;
		}
	} while (sdlEvent.type != SDL_KEYUP);
}


/*-----------------------------------------------------------------------*/
/**
 * Let the user define joystick keys.
 */
static void DlgJoystick_DefineKeys(int nActJoy)
{

	SDLGui_CenterDlg(joykeysdlg);
	DlgJoystick_DefineOneKey("up", &ConfigureParams.Joysticks.Joy[nActJoy].nKeyCodeUp);
	DlgJoystick_DefineOneKey("down", &ConfigureParams.Joysticks.Joy[nActJoy].nKeyCodeDown);
	DlgJoystick_DefineOneKey("left", &ConfigureParams.Joysticks.Joy[nActJoy].nKeyCodeLeft);
	DlgJoystick_DefineOneKey("right", &ConfigureParams.Joysticks.Joy[nActJoy].nKeyCodeRight);
	DlgJoystick_DefineOneKey("fire", &ConfigureParams.Joysticks.Joy[nActJoy].nKeyCodeFire);
}


/*-----------------------------------------------------------------------*/
/**
 * Adapt dialog using the values from the configration structure.
 */
static void DlgJoystick_ReadValuesFromConf(int nActJoy)
{
	int i;

	/* Check if joystick ID is available */
	if (SDL_NumJoysticks() == 0)
	{
		strcpy(sSdlStickName, "0: (none available)");
	}
	else if (Joy_ValidateJoyId(nActJoy))
	{
		snprintf(sSdlStickName, 20, "%i: %s", ConfigureParams.Joysticks.Joy[nActJoy].nJoyId,
		         Joy_GetName(ConfigureParams.Joysticks.Joy[nActJoy].nJoyId));
	}
	else
	{
		snprintf(sSdlStickName, 20, "0: %s", Joy_GetName(0));
	}

	for (i = DLGJOY_DISABLED; i <= DLGJOY_USEREALJOY; i++)
		joydlg[i].state &= ~SG_SELECTED;
	switch (ConfigureParams.Joysticks.Joy[nActJoy].nJoystickMode)
	{
	 case JOYSTICK_DISABLED:
		joydlg[DLGJOY_DISABLED].state |= SG_SELECTED;
		break;
	 case JOYSTICK_KEYBOARD:
		joydlg[DLGJOY_USEKEYS].state |= SG_SELECTED;
		break;
	 case JOYSTICK_REALSTICK:
		joydlg[DLGJOY_USEREALJOY].state |= SG_SELECTED;
		break;
	}

	if (ConfigureParams.Joysticks.Joy[nActJoy].bEnableAutoFire)
		joydlg[DLGJOY_AUTOFIRE].state |= SG_SELECTED;
	else
		joydlg[DLGJOY_AUTOFIRE].state &= ~SG_SELECTED;
}


/*-----------------------------------------------------------------------*/
/**
 * Read values from dialog and write them to the configuration structure.
 */
static void DlgJoystick_WriteValuesToConf(int nActJoy)
{
	if (joydlg[DLGJOY_DISABLED].state & SG_SELECTED)
		ConfigureParams.Joysticks.Joy[nActJoy].nJoystickMode = JOYSTICK_DISABLED;
	else if (joydlg[DLGJOY_USEKEYS].state & SG_SELECTED)
		ConfigureParams.Joysticks.Joy[nActJoy].nJoystickMode = JOYSTICK_KEYBOARD;
	else
		ConfigureParams.Joysticks.Joy[nActJoy].nJoystickMode = JOYSTICK_REALSTICK;

	ConfigureParams.Joysticks.Joy[nActJoy].bEnableAutoFire = (joydlg[DLGJOY_AUTOFIRE].state & SG_SELECTED);
	ConfigureParams.Joysticks.Joy[nActJoy].nJoyId = joydlg[DLGJOY_SDLJOYNAME].txt[0] - '0';
}


/*-----------------------------------------------------------------------*/
/**
 * Show and process the joystick dialog.
 */
void Dialog_JoyDlg(void)
{
	int but;
	static int nActJoy = 1;
	int nMaxId;

	SDLGui_CenterDlg(joydlg);

	joydlg[DLGJOY_STJOYNAME].txt = sJoystickNames[nActJoy];

	nMaxId = Joy_GetMaxId();

	/* Set up dialog from actual values: */
	DlgJoystick_ReadValuesFromConf(nActJoy);

	do
	{
		but = SDLGui_DoDialog(joydlg, NULL, false);
		switch (but)
		{
		 case DLGJOY_PREVSDLJOY:        // Select the previous SDL joystick
			if (ConfigureParams.Joysticks.Joy[nActJoy].nJoyId > 0)
			{
				ConfigureParams.Joysticks.Joy[nActJoy].nJoyId -= 1;
				snprintf(sSdlStickName, 20, "%i: %s", ConfigureParams.Joysticks.Joy[nActJoy].nJoyId,
				         Joy_GetName(ConfigureParams.Joysticks.Joy[nActJoy].nJoyId));
			}
			break;
		 case DLGJOY_NEXTSDLJOY:        // Select the next SDL joystick
			if (ConfigureParams.Joysticks.Joy[nActJoy].nJoyId < nMaxId)
			{
				ConfigureParams.Joysticks.Joy[nActJoy].nJoyId += 1;
				snprintf(sSdlStickName, 20, "%i: %s", ConfigureParams.Joysticks.Joy[nActJoy].nJoyId,
				         Joy_GetName(ConfigureParams.Joysticks.Joy[nActJoy].nJoyId));
			}
			break;
		 case DLGJOY_DEFINEKEYS:        // Define new keys for keyboard emulation
			DlgJoystick_DefineKeys(nActJoy);
			break;
		 case DLGJOY_PREVSTJOY:         // Switch to the previous ST joystick setup tab
			if (nActJoy > 0)
			{
				DlgJoystick_WriteValuesToConf(nActJoy);
				nActJoy -= 1;
				DlgJoystick_ReadValuesFromConf(nActJoy);
				joydlg[DLGJOY_STJOYNAME].txt = sJoystickNames[nActJoy];
			}
			break;
		 case DLGJOY_NEXTSTJOY:         // Switch to the next ST joystick setup tab
			if (nActJoy < 5)
			{
				DlgJoystick_WriteValuesToConf(nActJoy);
				nActJoy += 1;
				DlgJoystick_ReadValuesFromConf(nActJoy);
				joydlg[DLGJOY_STJOYNAME].txt = sJoystickNames[nActJoy];
			}
			break;
		}
	}
	while (but != DLGJOY_EXIT && but != SDLGUI_QUIT
	       && but != SDLGUI_ERROR && !bQuitProgram);

	DlgJoystick_WriteValuesToConf(nActJoy);
}
