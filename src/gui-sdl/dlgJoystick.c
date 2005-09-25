/*
  Hatari - dlgJoystick.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgJoystick_rcsid[] = "Hatari $Id: dlgJoystick.c,v 1.4 2005-09-25 21:32:25 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


#define DLGJOY_DISABLED    3
#define DLGJOY_USEREALJOY  4
#define DLGJOY_USEKEYS     5
#define DLGJOY_SDLJOYNUM   7
#define DLGJOY_PREVSDLJOY  8
#define DLGJOY_NEXTSDLJOY  9
#define DLGJOY_DEFINEKEYS 10
#define DLGJOY_AUTOFIRE   11
#define DLGJOY_STJOYNAME  13
#define DLGJOY_PREVSTJOY  14  
#define DLGJOY_NEXTSTJOY  15
#define DLGJOY_EXIT       16


/* The joysticks dialog: */
static SGOBJ joydlg[] =
{
	{ SGBOX, 0, 0, 0,0, 32,16, NULL },
	{ SGTEXT, 0, 0, 8,1, 15,1, "Joysticks setup" },

	{ SGBOX, 0, 0, 1,4, 30,9, NULL },
	{ SGRADIOBUT, 0, 0, 2,5, 10,1, "disabled" },
	{ SGRADIOBUT, 0, 0, 2,7, 19,1, "use real joystick" },
	{ SGRADIOBUT, 0, 0, 2,9, 14,1, "use keyboard" },

	{ SGBOX, 0, 0, 25,7, 3,1, NULL },
	{ SGTEXT, 0, 0, 26,7, 1,1, NULL },
	{ SGBUTTON, 0, 0, 24,7, 1,1, "\x04" },        /* Arrow left */
	{ SGBUTTON, 0, 0, 28,7, 1,1, "\x03" },        /* Arrow right */
	{ SGBUTTON, 0, 0, 19,9, 11,1, "Define keys" },

	{ SGCHECKBOX, 0, 0, 2,11, 17,1, "Enable autofire" },

	{ SGBOX, 0, 0, 4,3, 24,1, NULL },
	{ SGTEXT, 0, 0, 5,3, 22,1, NULL },
	{ SGBUTTON, 0, 0, 1,3, 3,1, "\x04" },         /* Arrow left */
	{ SGBUTTON, 0, 0, 28,3, 3,1, "\x03" },        /* Arrow right */

	{ SGBUTTON, 0, 0, 6,14, 20,1, "Back to main menu" },
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
  Adapt dialog using the values from the configration structure.
*/
static void DlgJoystick_ReadValuesFromConf(int nActJoy, int nMaxId)
{
	int i;

	/* Check if joystick ID is available */
	if (DialogParams.Joysticks.Joy[nActJoy].nJoyId <= nMaxId)
	{
		joydlg[DLGJOY_SDLJOYNUM].txt[0] = '0' + DialogParams.Joysticks.Joy[nActJoy].nJoyId;
	}
	else
	{
		joydlg[DLGJOY_SDLJOYNUM].txt[0] = '0';
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
	DialogParams.Joysticks.Joy[nActJoy].nJoyId = joydlg[DLGJOY_SDLJOYNUM].txt[0] - '0';
}


/*-----------------------------------------------------------------------*/
/*
  Show and process the joystick dialog.
*/
void Dialog_JoyDlg(void)
{
	int but;
	static int nActJoy = 1;
	char sJoyNum[2];
	int nMaxJoyId;

	SDLGui_CenterDlg(joydlg);

	joydlg[DLGJOY_STJOYNAME].txt = sJoystickNames[nActJoy];
	strcpy(sJoyNum, "0");
	joydlg[DLGJOY_SDLJOYNUM].txt = sJoyNum;

	nMaxJoyId = SDL_NumJoysticks();
	if (nMaxJoyId > 5)
		nMaxJoyId = 5;

	/* Set up dialog from actual values: */
	DlgJoystick_ReadValuesFromConf(nActJoy, nMaxJoyId);

	do
	{
    	but = SDLGui_DoDialog(joydlg, NULL);
		switch (but)
		{
		 case DLGJOY_PREVSDLJOY:
			if (joydlg[DLGJOY_SDLJOYNUM].txt[0] > '0')
				joydlg[DLGJOY_SDLJOYNUM].txt[0] -= 1;
			break;
		 case DLGJOY_NEXTSDLJOY:
			if (joydlg[DLGJOY_SDLJOYNUM].txt[0] - '0' < nMaxJoyId-1)
				joydlg[DLGJOY_SDLJOYNUM].txt[0] += 1;
			break;
		 case DLGJOY_DEFINEKEYS:
			DlgAlert_Notice("Defining keys is not yet implemeted.");
			break;
		 case DLGJOY_PREVSTJOY:
			if (nActJoy > 0)
			{
				DlgJoystick_WriteValuesToConf(nActJoy);
				nActJoy -= 1;
				DlgJoystick_ReadValuesFromConf(nActJoy, nMaxJoyId);
				joydlg[DLGJOY_STJOYNAME].txt = sJoystickNames[nActJoy];
			}
			break;
		 case DLGJOY_NEXTSTJOY:
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
