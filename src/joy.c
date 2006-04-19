/*
  Hatari - joy.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Joystick routines.

  NOTE: The ST uses the joystick port 1 as the default controller.
*/
const char Joy_rcsid[] = "Hatari $Id: joy.c,v 1.11 2006-04-19 21:46:13 thothy Exp $";

#include <SDL.h>

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "ioMem.h"
#include "joy.h"
#include "log.h"
#include "video.h"

#define JOY_BUTTON1  1
#define JOY_BUTTON2  2


static SDL_Joystick *sdlJoystick[6] =   /* SDL's joystick structures */
{
	NULL, NULL, NULL, NULL, NULL, NULL
};

static BOOL bJoystickWorking[6] =       /* Is joystick plugged in and working? */
{
	FALSE, FALSE, FALSE, FALSE, FALSE, FALSE
}; 

int JoystickSpaceBar = FALSE;           /* State of space-bar on joystick button 2 */
static Uint8 nJoyKeyEmu[6];
static Uint16 nSteJoySelect;


/*-----------------------------------------------------------------------*/
/*
  This function initialises the (real) joysticks.
*/
void Joy_Init(void)
{
	int i, nPadsConnected;

	/* Initialise SDL's joystick subsystem: */
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
	{
		Log_Printf(LOG_ERROR, "Could not init joysticks: %s\n", SDL_GetError());
		return;
	}

	/* Scan joystick connection array for working joysticks */
	nPadsConnected = SDL_NumJoysticks();
	for (i = 0; i < nPadsConnected && i < 6; i++)
	{
		/* Open the joystick for use */
		sdlJoystick[i] = SDL_JoystickOpen(i);
		/* Is joystick ok? */
		if (sdlJoystick[i] != NULL)
		{
			/* Set as working */
			bJoystickWorking[i] = TRUE;
			Log_Printf(LOG_DEBUG, "Joystick %i: %s\n", i, SDL_JoystickName(i));
		}
	}

	/* OK, do we have any working joysticks? */
	if (!bJoystickWorking[0])
	{
		/* No, so if first time install need to set cursor emulation */
		if (bFirstTimeInstall)
			ConfigureParams.Joysticks.Joy[1].nJoystickMode = JOYSTICK_KEYBOARD;
	}

	JoystickSpaceBar = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Close the (real) joysticks.
*/
void Joy_UnInit(void)
{
	int i, nPadsConnected;

	nPadsConnected = SDL_NumJoysticks();

	for (i = 0; i < nPadsConnected && i < 6; i++)
	{
		if (bJoystickWorking[i] == TRUE)
		{
			SDL_JoystickClose(sdlJoystick[i]);
		}
	}
}


/*-----------------------------------------------------------------------*/
/*
  Read details from joystick using SDL calls
  NOTE ID is that of SDL
*/
static BOOL Joy_ReadJoystick(int nSdlJoyID, JOYREADING *pJoyReading)
{
	/* Joystick is OK, read position */
	pJoyReading->XPos = SDL_JoystickGetAxis(sdlJoystick[nSdlJoyID], 0);
	pJoyReading->YPos = SDL_JoystickGetAxis(sdlJoystick[nSdlJoyID], 1);
	/* Sets bit #0 if button #1 is pressed: */
	pJoyReading->Buttons = SDL_JoystickGetButton(sdlJoystick[nSdlJoyID], 0);
	/* Sets bit #1 if button #2 is pressed: */
	if (SDL_JoystickGetButton(sdlJoystick[nSdlJoyID], 1))
		pJoyReading->Buttons |= JOY_BUTTON2;

	return TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Read PC joystick and return ST format byte, i.e. lower 4 bits direction
  and top bit fire.
  NOTE : ID 0 is Joystick 0/Mouse and ID 1 is Joystick 1 (default),
         ID 2 and 3 are STE joypads and ID 4 and 5 are parport joysticks.
*/
Uint8 Joy_GetStickData(int nStJoyId)
{
	Uint8 nData = 0;
	JOYREADING JoyReading;
	int nSdlJoyId;

	nSdlJoyId = ConfigureParams.Joysticks.Joy[nStJoyId].nJoyId;

	/* Are we emulating the joystick via the keyboard? */
	if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_KEYBOARD)
	{
		/* If holding 'SHIFT' we actually want cursor key movement, so ignore any of this */
		if ( !(SDL_GetModState()&(KMOD_LSHIFT|KMOD_RSHIFT)) )
		{
			nData = nJoyKeyEmu[nStJoyId];
		}
	}
	else if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_REALSTICK
	         && bJoystickWorking[nSdlJoyId])
	{
		/* Use real joystick for emulation */
		if (!Joy_ReadJoystick(nSdlJoyId, &JoyReading))
		{
			/* Something is wrong, we cannot read the joystick */
			bJoystickWorking[nSdlJoyId] = FALSE;
			return 0;
		}

		/* Directions */
		if (JoyReading.YPos <= JOYRANGE_UP_VALUE)
			nData |= 0x01;
		else if (JoyReading.YPos >= JOYRANGE_DOWN_VALUE)
			nData |= 0x02;
		if (JoyReading.XPos <= JOYRANGE_LEFT_VALUE)
			nData |= 0x04;
		else if (JoyReading.XPos >= JOYRANGE_RIGHT_VALUE)
			nData |= 0x08;

		/* PC Joystick button 1 is set as ST joystick button */
		if (JoyReading.Buttons & JOY_BUTTON1)
			nData |= 0x80;

		/* Enable PC Joystick button 2 to mimick space bar (For XenonII, Flying Shark etc...) */
		if (nStJoyId == JOYID_JOYSTICK1 && (JoyReading.Buttons & JOY_BUTTON2))
		{
			/* Only press 'space bar' if not in NULL state */
			if (!JoystickSpaceBar)
			{
				/* Press, ikbd will send packets and de-press */
				JoystickSpaceBar = JOYSTICK_SPACE_DOWN;
			}
		}
	}

	/* Ignore fire button every 8 frames if enabled autofire (for both cursor emulation and joystick) */
	if (ConfigureParams.Joysticks.Joy[nStJoyId].bEnableAutoFire)
	{
		if ((nVBLs&0x7)<4)
			nData &= ~0x80;          /* Remove top bit! */
	}

	return nData;
}


/*-----------------------------------------------------------------------*/
/*
  Get the fire button states.
  Note: More than one fire buttons are only supported for real joystick,
  not for keyboard emulation!
*/
static int Joy_GetFireButtons(int nStJoyId)
{
	int nButtons = 0;
	int nSdlJoyId;
	int i, nMaxButtons;

	nSdlJoyId = ConfigureParams.Joysticks.Joy[nStJoyId].nJoyId;

	/* Are we emulating the joystick via the keyboard? */
	if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_KEYBOARD)
	{
		if (nJoyKeyEmu[nStJoyId] & 0x80)
		{
			nButtons |= 1;
		}
	}
	else if (ConfigureParams.Joysticks.Joy[nStJoyId].nJoystickMode == JOYSTICK_REALSTICK
	         && bJoystickWorking[nSdlJoyId])
	{
		nMaxButtons = SDL_JoystickNumButtons(sdlJoystick[nSdlJoyId]);
		if (nMaxButtons > 17)
			nMaxButtons = 17;
		/* Now read all fire buttons and set a bit for each pressed button: */
		for (i = 0; i < nMaxButtons; i++)
		{
			if (SDL_JoystickGetButton(sdlJoystick[nSdlJoyId], i))
			{
				nButtons |= (1 << i);
			}
		}
	}

	return nButtons;
}


/*-----------------------------------------------------------------------*/
/*
  Toggle cursor emulation
*/
void Joy_ToggleCursorEmulation(void)
{
	/* Toggle joystick 1 keyboard emulation */
	if (ConfigureParams.Joysticks.Joy[1].nJoystickMode != JOYSTICK_DISABLED)
	{
		if (ConfigureParams.Joysticks.Joy[1].nJoystickMode == JOYSTICK_REALSTICK)
			ConfigureParams.Joysticks.Joy[1].nJoystickMode = JOYSTICK_KEYBOARD;
		else
			ConfigureParams.Joysticks.Joy[1].nJoystickMode = JOYSTICK_REALSTICK;
	}
}


/*-----------------------------------------------------------------------*/
/*
  A key has been pressed down, check if we use it for joystick emulation
  via keyboard.
*/
BOOL Joy_KeyDown(int symkey, int modkey)
{
	int i;

	for (i = 0; i < 6; i++)
	{
		if (ConfigureParams.Joysticks.Joy[i].nJoystickMode == JOYSTICK_KEYBOARD
		    && !(modkey & KMOD_SHIFT))
		{
			if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeUp)
			{
				nJoyKeyEmu[i] |= 1;
				return TRUE;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeDown)
			{
				nJoyKeyEmu[i] |= 2; 
				return TRUE;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeLeft)
			{
				nJoyKeyEmu[i] |= 4;
				return TRUE;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeRight)
			{
				nJoyKeyEmu[i] |= 8;
				return TRUE;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeFire)
			{
				nJoyKeyEmu[i] |= 0x80;
				return TRUE;
			}
		}
	}

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  A key has been released, check if we use it for joystick emulation
  via keyboard.
*/
BOOL Joy_KeyUp(int symkey, int modkey)
{
	int i;

	for (i = 0; i < 6; i++)
	{
		if (ConfigureParams.Joysticks.Joy[i].nJoystickMode == JOYSTICK_KEYBOARD
		    && !(modkey & KMOD_SHIFT))
		{
			if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeUp)
			{
				nJoyKeyEmu[i] &= ~1;
				return TRUE;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeDown)
			{
				nJoyKeyEmu[i] &= ~2;
				return TRUE;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeLeft)
			{
				nJoyKeyEmu[i] &= ~4;
				return TRUE;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeRight)
			{
				nJoyKeyEmu[i] &= ~8;
				return TRUE;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeFire)
			{
				nJoyKeyEmu[i] &= ~0x80;
				return TRUE;
			}
		}
	}

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Read from STE joypad buttons register (0xff9200)
*/
void Joy_StePadButtons_ReadWord(void)
{
	Uint16 nData = 0xffff;

	if (ConfigureParams.Joysticks.Joy[JOYID_STEPADA].nJoystickMode != JOYSTICK_DISABLED
	    && (nSteJoySelect & 0x0f) != 0x0f)
	{
		int nButtons = Joy_GetFireButtons(JOYID_STEPADA);
		if (!(nSteJoySelect & 0x1))
		{
			if (nButtons & 0x01)  /* Fire button A pressed? */
				nData &= ~2;
			if (nButtons & 0x10)  /* Fire button PAUSE pressed? */
				nData &= ~1;
		}
		else if (!(nSteJoySelect & 0x2))
		{
			if (nButtons & 0x02)  /* Fire button B pressed? */
				nData &= ~2;
		}
		else if (!(nSteJoySelect & 0x4))
		{
			if (nButtons & 0x04)  /* Fire button C pressed? */
				nData &= ~2;
		}
		else if (!(nSteJoySelect & 0x8))
		{
			if (nButtons & 0x01)  /* Fire button OPTION pressed? */
				nData &= ~2;
		}
	}

	if (ConfigureParams.Joysticks.Joy[JOYID_STEPADB].nJoystickMode != JOYSTICK_DISABLED
	    && (nSteJoySelect & 0xf0) != 0xf0)
	{
		int nButtons = Joy_GetFireButtons(JOYID_STEPADB);
		if (!(nSteJoySelect & 0x10))
		{
			if (nButtons & 0x01)  /* Fire button A pressed? */
				nData &= ~8;
			if (nButtons & 0x10)  /* Fire button PAUSE pressed? */
				nData &= ~4;
		}
		else if (!(nSteJoySelect & 0x20))
		{
			if (nButtons & 0x02)  /* Fire button B pressed? */
				nData &= ~8;
		}
		else if (!(nSteJoySelect & 0x40))
		{
			if (nButtons & 0x04)  /* Fire button C pressed? */
				nData &= ~8;
		}
		else if (!(nSteJoySelect & 0x80))
		{
			if (nButtons & 0x08)  /* Fire button OPTION pressed? */
				nData &= ~8;
		}
	}

	IoMem_WriteWord(0xff9200, nData);
}


/*-----------------------------------------------------------------------*/
/*
  Read from STE joypad direction/buttons register (0xff9202)
*/
void Joy_StePadMulti_ReadWord(void)
{
	Uint8 nData = 0xff;

	if (ConfigureParams.Joysticks.Joy[JOYID_STEPADA].nJoystickMode != JOYSTICK_DISABLED
	    && (nSteJoySelect & 0x0f) != 0x0f)
	{
		nData &= 0xf0;
		if (!(nSteJoySelect & 0x1))
		{
			nData |= ~Joy_GetStickData(JOYID_STEPADA) & 0x0f;
		}
		else if (!(nSteJoySelect & 0x2))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_STEPADA) >> 13) & 0x0f;
		}
		else if (!(nSteJoySelect & 0x4))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_STEPADA) >> 9) & 0x0f;
		}
		else if (!(nSteJoySelect & 0x8))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_STEPADA) >> 5) & 0x0f;
		}
	}

	if (ConfigureParams.Joysticks.Joy[JOYID_STEPADB].nJoystickMode != JOYSTICK_DISABLED
	    && (nSteJoySelect & 0xf0) != 0xf0)
	{
		nData &= 0x0f;
		if (!(nSteJoySelect & 0x10))
		{
			nData |= ~Joy_GetStickData(JOYID_STEPADB) << 4;
		}
		else if (!(nSteJoySelect & 0x20))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_STEPADB) >> 13) & 0x0f;
		}
		else if (!(nSteJoySelect & 0x40))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_STEPADB) >> 9) & 0x0f;
		}
		else if (!(nSteJoySelect & 0x80))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_STEPADB) >> 5) & 0x0f;
		}
	}

	IoMem_WriteWord(0xff9202, (nData << 8) | 0x0ff);
}


/*-----------------------------------------------------------------------*/
/*
  Write to STE joypad selection register (0xff9202)
*/
void Joy_StePadMulti_WriteWord(void)
{
	nSteJoySelect = IoMem_ReadWord(0xff9202);
}
