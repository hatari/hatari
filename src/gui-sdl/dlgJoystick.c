/*
  Hatari - dlgJoystick.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgJoystick_fileid[] = "Hatari dlgJoystick.c";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "joy.h"
#include "str.h"

#define DLGJOY_STJOYNAME     3
#define DLGJOY_PREVSTJOY     4
#define DLGJOY_NEXTSTJOY     5
#define DLGJOY_DEFINEKEYS    7
#define DLGJOY_DISABLED      8
#define DLGJOY_USEKEYS       9
#define DLGJOY_USEREALJOY   10
#define DLGJOY_SDLJOYNAME   12
#define DLGJOY_PREVSDLJOY   13
#define DLGJOY_NEXTSDLJOY   14
#define DLGJOY_AUTOFIRE     15
#define DLGJOY_BUT2_SPACE   17
#define DLGJOY_BUT2_JUMP    18
#define DLGJOY_REMAPBUTTONS 19
#define DLGJOY_EXIT         20

/* The joysticks dialog: */

static char sSdlStickName[23];

static SGOBJ joydlg[] =
{
	{ SGBOX, 0, 0, 0,0, 32,23, NULL },
	{ SGTEXT, 0, 0, 8,1, 15,1, "Joysticks setup" },

	{ SGBOX, 0, 0, 4,3, 24,1, NULL },
	{ SGTEXT, 0, 0, 5,3, 22,1, NULL },
	{ SGBUTTON, 0, 0,  1,3, 3,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGBUTTON, 0, 0, 28,3, 3,1, "\x03", SG_SHORTCUT_RIGHT },

	{ SGBOX, 0, 0, 1,4, 30,16, NULL },
	{ SGBUTTON,   0, 0, 17,7, 13,1, "D_efine keys" },

	{ SGRADIOBUT, 0, 0, 2,5, 10,1, "_disabled" },
	{ SGRADIOBUT, 0, 0, 2,7, 14,1, "use _keyboard" },
	{ SGRADIOBUT, 0, 0, 2,9, 20,1, "use real _joystick:" },

	{ SGBOX, 0, 0, 5,11, 24,1, NULL },
	{ SGTEXT, 0, 0, 6,11, 22,1, sSdlStickName },
	{ SGBUTTON, 0, 0,  4,11, 1,1, "\x04", SG_SHORTCUT_UP },
	{ SGBUTTON, 0, 0, 29,11, 1,1, "\x03", SG_SHORTCUT_DOWN },

	{ SGCHECKBOX, 0, 0, 2,13, 17,1, "Enable _autofire" },

	{ SGTEXT, 0, 0, 4,15, 9,1, "Button 2:" },
	{ SGRADIOBUT, 0, 0, 2,16, 10,1, "_space key" },
	{ SGRADIOBUT, 0, 0, 15,16, 10,1, "_up / jump" },
	{ SGBUTTON, 0, 0, 4,18, 24,1, "Re_map joystick buttons" },

	{ SGBUTTON, SG_DEFAULT, 0, 6,21, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/* The joystick keys setup dialog: */

static char sKeyInstruction[27];
static char sKeyName[27];

static SGOBJ joykeysdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 30,5, NULL },
	{ SGTEXT, 0, 0, 2,1, 26,1, sKeyInstruction },
	{ SGTEXT, 0, 0, 2,3, 26,1, sKeyName },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

/* The joystick button remapping setup dialog: */

static SGOBJ joybuttondlg[] =
{
	{ SGBOX, 0, 0, 0,0, 25,7, NULL },
	{ SGTEXT, 0, 0, 2,1, 21,1, "Press joystick button" },
	{ SGTEXT, 0, 0, 5,2, 15,1, sKeyInstruction },
	{ SGTEXT, 0, 0, 2,3, 18,1, "or ESC for none..." },
	{ SGTEXT, 0, 0, 2,5, 15,1, sKeyName },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
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
	JOYSTICK *joycnf = &ConfigureParams.Joysticks.Joy[nActJoy];

	SDLGui_CenterDlg(joykeysdlg);
	DlgJoystick_DefineOneKey("up", &joycnf->nKeyCodeUp);
	DlgJoystick_DefineOneKey("down", &joycnf->nKeyCodeDown);
	DlgJoystick_DefineOneKey("left", &joycnf->nKeyCodeLeft);
	DlgJoystick_DefineOneKey("right", &joycnf->nKeyCodeRight);

	if (nActJoy != JOYID_JOYPADA && nActJoy != JOYID_JOYPADB)
	{
		DlgJoystick_DefineOneKey("fire", &joycnf->nKeyCodeFire);
	}
	else
	{
		/* Handle the joypad buttons */
		DlgJoystick_DefineOneKey("A", &joycnf->nKeyCodeFire);
		DlgJoystick_DefineOneKey("B", &joycnf->nKeyCodeB);
		DlgJoystick_DefineOneKey("C", &joycnf->nKeyCodeC);
		DlgJoystick_DefineOneKey("option", &joycnf->nKeyCodeOption);
		DlgJoystick_DefineOneKey("pause", &joycnf->nKeyCodePause);

		if (DlgAlert_Query("Do you also want to set up the number pad?"))
		{
			DlgJoystick_DefineOneKey("0", &joycnf->nKeyCodeNum[0]);
			DlgJoystick_DefineOneKey("1", &joycnf->nKeyCodeNum[1]);
			DlgJoystick_DefineOneKey("2", &joycnf->nKeyCodeNum[2]);
			DlgJoystick_DefineOneKey("3", &joycnf->nKeyCodeNum[3]);
			DlgJoystick_DefineOneKey("4", &joycnf->nKeyCodeNum[4]);
			DlgJoystick_DefineOneKey("5", &joycnf->nKeyCodeNum[5]);
			DlgJoystick_DefineOneKey("6", &joycnf->nKeyCodeNum[6]);
			DlgJoystick_DefineOneKey("7", &joycnf->nKeyCodeNum[7]);
			DlgJoystick_DefineOneKey("8", &joycnf->nKeyCodeNum[8]);
			DlgJoystick_DefineOneKey("9", &joycnf->nKeyCodeNum[9]);
			DlgJoystick_DefineOneKey("*", &joycnf->nKeyCodeStar);
			DlgJoystick_DefineOneKey("#", &joycnf->nKeyCodeHash);
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Show dialogs for remapping joystick buttons and wait for a button press.
 */
static void DlgJoystick_MapOneButton(const char *name, int *pButton)
{
	SDL_Event sdlEvent;
	bool bDone = false;
	bool bSet = false;

	if (bQuitProgram)
		return;

	Str_Copy(sKeyInstruction, name, sizeof(sKeyInstruction));
	if (*pButton >= 0)
	{
		snprintf(sKeyName, sizeof(sKeyName), "(was: id %d)", *pButton);
	}
	else
	{
		Str_Copy(sKeyName, "(was: none)", sizeof(sKeyName));
	}

	SDLGui_DrawDialog(joybuttondlg);

	do
	{
		SDL_WaitEvent(&sdlEvent);
		switch (sdlEvent.type)
		{
			case SDL_JOYBUTTONDOWN:
				*pButton = sdlEvent.jbutton.button;
				bSet = true;
				snprintf(sKeyName, sizeof(sKeyName), "(now: id %d)", *pButton);
				SDLGui_DrawDialog(joybuttondlg);
				break;
			case SDL_JOYBUTTONUP:
				bDone = bSet;
				break;
			case SDL_KEYDOWN:
				if ((sdlEvent.key.keysym.sym == SDLK_ESCAPE) && (sdlEvent.key.repeat == 0))
				{
					*pButton = -1;
					bSet = true;
					Str_Copy(sKeyName, "(now: none)", sizeof(sKeyName));
					SDLGui_DrawDialog(joybuttondlg);
				}
				break;
			case SDL_KEYUP:
				if (sdlEvent.key.keysym.sym == SDLK_ESCAPE)
				{
					bDone = bSet;
				}
				break;
			case SDL_QUIT:
				bQuitProgram = true;
				bDone = true;
				break;
		}
	} while (!bDone);
}


/*-----------------------------------------------------------------------*/
/**
 * Let the user remap joystick buttons.
 */
static void DlgJoystick_RemapButtons(int nActJoy)
{
	int *map = ConfigureParams.Joysticks.Joy[nActJoy].nJoyButMap;
	static const char *names[JOYSTICK_BUTTONS] =
	{
		"1: fire",
		"2: space / jump",
		"3: autofire"
	};

	SDLGui_CenterDlg(joybuttondlg);
	for (int i = 0; i < JOYSTICK_BUTTONS; i++)
		DlgJoystick_MapOneButton(names[i], &map[i]);
}


/*-----------------------------------------------------------------------*/
/**
 * Adapt dialog using the values from the configuration structure.
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
		snprintf(sSdlStickName, sizeof(sSdlStickName), "%i: %s",
			 ConfigureParams.Joysticks.Joy[nActJoy].nJoyId,
		         Joy_GetName(ConfigureParams.Joysticks.Joy[nActJoy].nJoyId));
	}
	else
	{
		snprintf(sSdlStickName, sizeof(sSdlStickName), "0: %s",
			 Joy_GetName(0));
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

	if (ConfigureParams.Joysticks.Joy[nActJoy].bEnableJumpOnFire2)
	{
		joydlg[DLGJOY_BUT2_JUMP].state |= SG_SELECTED;
		joydlg[DLGJOY_BUT2_SPACE].state &= ~SG_SELECTED;
	}
	else
	{
		joydlg[DLGJOY_BUT2_SPACE].state |= SG_SELECTED;
		joydlg[DLGJOY_BUT2_JUMP].state &= ~SG_SELECTED;
	}
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
	ConfigureParams.Joysticks.Joy[nActJoy].bEnableJumpOnFire2 = (joydlg[DLGJOY_BUT2_JUMP].state & SG_SELECTED);

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
		but = SDLGui_DoDialog(joydlg);
		switch (but)
		{
		 case DLGJOY_PREVSDLJOY:        // Select the previous SDL joystick
			if (ConfigureParams.Joysticks.Joy[nActJoy].nJoyId > 0)
			{
				ConfigureParams.Joysticks.Joy[nActJoy].nJoyId -= 1;
				snprintf(sSdlStickName, sizeof(sSdlStickName), "%i: %s",
					 ConfigureParams.Joysticks.Joy[nActJoy].nJoyId,
				         Joy_GetName(ConfigureParams.Joysticks.Joy[nActJoy].nJoyId));
			}
			break;
		 case DLGJOY_NEXTSDLJOY:        // Select the next SDL joystick
			if (ConfigureParams.Joysticks.Joy[nActJoy].nJoyId < nMaxId)
			{
				ConfigureParams.Joysticks.Joy[nActJoy].nJoyId += 1;
				snprintf(sSdlStickName, sizeof(sSdlStickName), "%i: %s",
					 ConfigureParams.Joysticks.Joy[nActJoy].nJoyId,
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
		 case DLGJOY_REMAPBUTTONS:        // Remap joystick buttons
			DlgJoystick_RemapButtons(nActJoy);
			break;
		}
	}
	while (but != DLGJOY_EXIT && but != SDLGUI_QUIT
	       && but != SDLGUI_ERROR && !bQuitProgram);

	/* Tell ikbd to release joystick button 2 emulated
	 * space bar in the theoritical case it has gotten stuck
	 * down, and to avoid that case, prevent it also going
	 * down if user pressed the button when invoking GUI
	 */
	if (JoystickSpaceBar == JOYSTICK_SPACE_DOWNED)
		JoystickSpaceBar = JOYSTICK_SPACE_UP;
	else if (JoystickSpaceBar == JOYSTICK_SPACE_DOWN)
		JoystickSpaceBar = JOYSTICK_SPACE_NULL;

	DlgJoystick_WriteValuesToConf(nActJoy);
}
