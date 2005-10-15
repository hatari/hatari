/*
  Hatari - dlgJoystick.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgJoystick_rcsid[] = "Hatari $Id: dlgJoystick.c,v 1.7 2005-10-15 14:00:10 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


#define DLGJOY_DISABLED    3
#define DLGJOY_USEREALJOY  4
#define DLGJOY_USEKEYS     5
#define DLGJOY_DEFINEKEYS  6
#define DLGJOY_SDLJOYNAME  8
#define DLGJOY_PREVSDLJOY  9
#define DLGJOY_NEXTSDLJOY 10
#define DLGJOY_AUTOFIRE   11
#define DLGJOY_STJOYNAME  13
#define DLGJOY_PREVSTJOY  14  
#define DLGJOY_NEXTSTJOY  15
#define DLGJOY_EXIT       16


/* The joysticks dialog: */

static char sSdlStickName[20];

static SGOBJ joydlg[] =
{
	{ SGBOX, 0, 0, 0,0, 32,18, NULL },
	{ SGTEXT, 0, 0, 8,1, 15,1, "Joysticks setup" },

	{ SGBOX, 0, 0, 1,4, 30,11, NULL },
	{ SGRADIOBUT, 0, 0, 2,5, 10,1, "disabled" },
	{ SGRADIOBUT, 0, 0, 2,9, 20,1, "use real joystick:" },
	{ SGRADIOBUT, 0, 0, 2,7, 14,1, "use keyboard" },

	{ SGBUTTON, 0, 0, 19,7, 11,1, "Define keys" },
	{ SGBOX, 0, 0, 5,11, 22,1, NULL },
	{ SGTEXT, 0, 0, 6,11, 20,1, sSdlStickName },
	{ SGBUTTON, 0, 0, 4,11, 1,1, "\x04" },         /* Arrow left */
	{ SGBUTTON, 0, 0, 27,11, 1,1, "\x03" },        /* Arrow right */

	{ SGCHECKBOX, 0, 0, 2,13, 17,1, "Enable autofire" },

	{ SGBOX, 0, 0, 4,3, 24,1, NULL },
	{ SGTEXT, 0, 0, 5,3, 22,1, NULL },
	{ SGBUTTON, 0, 0, 1,3, 3,1, "\x04" },         /* Arrow left */
	{ SGBUTTON, 0, 0, 28,3, 3,1, "\x03" },        /* Arrow right */

	{ SGBUTTON, 0, 0, 6,16, 20,1, "Back to main menu" },
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


static char *sJoystickNames[6] =
{
	"ST Joystick 0",
	"ST Joystick 1",
	"STE Joypad A",
	"STE Joypad B",
	"Parallel port stick 1",
	"Parallel port stick 2"
};


/*-----------------------------------------------------------------------*/
/*
  Show dialogs for defining joystick keys and wait for a key press.
*/
static void DlgJoystick_DefineOneKey(char *pType, int *pKey)
{
	SDL_Event sdlEvent;

	if (bQuitProgram)
		return;

	snprintf(sKeyInstruction, sizeof(sKeyInstruction), "Press key for '%s'...", pType);
	snprintf(sKeyName, sizeof(sKeyName), "(was: '%s')", SDL_GetKeyName(*pKey));

	SDLGui_DrawDialog(joykeysdlg);

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
			bQuitProgram = TRUE;
			return;
		}
	} while (sdlEvent.type != SDL_KEYUP);
	SDL_Delay(200);
}


/*-----------------------------------------------------------------------*/
/*
  Let the user define joystick keys.
*/
static void DlgJoystick_DefineKeys(int nActJoy)
{

	SDLGui_CenterDlg(joykeysdlg);
	DlgJoystick_DefineOneKey("up", &DialogParams.Joysticks.Joy[nActJoy].nKeyCodeUp);
	DlgJoystick_DefineOneKey("down", &DialogParams.Joysticks.Joy[nActJoy].nKeyCodeDown);
	DlgJoystick_DefineOneKey("left", &DialogParams.Joysticks.Joy[nActJoy].nKeyCodeLeft);
	DlgJoystick_DefineOneKey("right", &DialogParams.Joysticks.Joy[nActJoy].nKeyCodeRight);
	DlgJoystick_DefineOneKey("fire", &DialogParams.Joysticks.Joy[nActJoy].nKeyCodeFire);
}


/*-----------------------------------------------------------------------*/
/*
  Adapt dialog using the values from the configration structure.
*/
static void DlgJoystick_ReadValuesFromConf(int nActJoy, int nMaxId)
{
	int i;

	/* Check if joystick ID is available */
	if (SDL_NumJoysticks() == 0)
	{
		strcpy(sSdlStickName, "0: (none available)");
	}
	else if (DialogParams.Joysticks.Joy[nActJoy].nJoyId <= nMaxId)
	{
		snprintf(sSdlStickName, 20, "%i: %s", DialogParams.Joysticks.Joy[nActJoy].nJoyId,
		         SDL_JoystickName(DialogParams.Joysticks.Joy[nActJoy].nJoyId));
	}
	else
	{
		snprintf(sSdlStickName, 20, "0: %s", SDL_JoystickName(0));
		/* Unavailable joystick ID -> disable it if necessary*/
		if (DialogParams.Joysticks.Joy[nActJoy].nJoystickMode == JOYSTICK_REALSTICK)
			DialogParams.Joysticks.Joy[nActJoy].nJoystickMode = JOYSTICK_DISABLED;
	}

	for (i = DLGJOY_DISABLED; i <= DLGJOY_USEKEYS; i++)
		joydlg[i].state &= ~SG_SELECTED;
	joydlg[DLGJOY_DISABLED + DialogParams.Joysticks.Joy[nActJoy].nJoystickMode].state |= SG_SELECTED;

	if (DialogParams.Joysticks.Joy[nActJoy].bEnableAutoFire)
		joydlg[DLGJOY_AUTOFIRE].state |= SG_SELECTED;
	else
		joydlg[DLGJOY_AUTOFIRE].state &= ~SG_SELECTED;
}


/*-----------------------------------------------------------------------*/
/*
  Read values from dialog and write them to the configuration structure.
*/
static void DlgJoystick_WriteValuesToConf(int nActJoy)
{
	int i;
	for (i = DLGJOY_DISABLED; i <= DLGJOY_USEKEYS; i++)
	{
		if (joydlg[i].state & SG_SELECTED)
		{
			DialogParams.Joysticks.Joy[nActJoy].nJoystickMode = i - DLGJOY_DISABLED;
			break;
		}
	}

	DialogParams.Joysticks.Joy[nActJoy].bEnableAutoFire = (joydlg[DLGJOY_AUTOFIRE].state & SG_SELECTED);
	DialogParams.Joysticks.Joy[nActJoy].nJoyId = joydlg[DLGJOY_SDLJOYNAME].txt[0] - '0';
}


/*-----------------------------------------------------------------------*/
/*
  Show and process the joystick dialog.
*/
void Dialog_JoyDlg(void)
{
	int but;
	static int nActJoy = 1;
	int nMaxJoyId;

	SDLGui_CenterDlg(joydlg);

	joydlg[DLGJOY_STJOYNAME].txt = sJoystickNames[nActJoy];

	nMaxJoyId = SDL_NumJoysticks() - 1;
	if (nMaxJoyId > 5)
		nMaxJoyId = 5;

	/* Set up dialog from actual values: */
	DlgJoystick_ReadValuesFromConf(nActJoy, nMaxJoyId);

	do
	{
    	but = SDLGui_DoDialog(joydlg, NULL);
		switch (but)
		{
		 case DLGJOY_PREVSDLJOY:        // Select the previous SDL joystick
			if (DialogParams.Joysticks.Joy[nActJoy].nJoyId > 0)
			{
				DialogParams.Joysticks.Joy[nActJoy].nJoyId -= 1;
				snprintf(sSdlStickName, 20, "%i: %s", DialogParams.Joysticks.Joy[nActJoy].nJoyId,
				         SDL_JoystickName(DialogParams.Joysticks.Joy[nActJoy].nJoyId));
			}
			break;
		 case DLGJOY_NEXTSDLJOY:        // Select the next SDL joystick
			if (DialogParams.Joysticks.Joy[nActJoy].nJoyId < nMaxJoyId)
			{
				DialogParams.Joysticks.Joy[nActJoy].nJoyId += 1;
				snprintf(sSdlStickName, 20, "%i: %s", DialogParams.Joysticks.Joy[nActJoy].nJoyId,
				         SDL_JoystickName(DialogParams.Joysticks.Joy[nActJoy].nJoyId));
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
				DlgJoystick_ReadValuesFromConf(nActJoy, nMaxJoyId);
				joydlg[DLGJOY_STJOYNAME].txt = sJoystickNames[nActJoy];
			}
			break;
		 case DLGJOY_NEXTSTJOY:         // Switch to the next ST joystick setup tab
			if (nActJoy < 5)
			{
				DlgJoystick_WriteValuesToConf(nActJoy);
				nActJoy += 1;
				DlgJoystick_ReadValuesFromConf(nActJoy, nMaxJoyId);
				joydlg[DLGJOY_STJOYNAME].txt = sJoystickNames[nActJoy];
			}
			break;
		}
	}
	while (but != DLGJOY_EXIT && but != SDLGUI_QUIT && !bQuitProgram );

	DlgJoystick_WriteValuesToConf(nActJoy);
}
