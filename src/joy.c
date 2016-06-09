/*
  Hatari - joy.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Joystick routines.

  NOTE: The ST uses the joystick port 1 as the default controller.
*/
const char Joy_fileid[] = "Hatari joy.c : " __DATE__ " " __TIME__;

#include <SDL.h>

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "joy.h"
#include "log.h"
#include "screen.h"
#include "video.h"
#include "statusbar.h"

#define JOY_DEBUG 0
#if JOY_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif

#define JOY_BUTTON1  1
#define JOY_BUTTON2  2

typedef struct
{
	int XPos,YPos;                /* the actually read axis values in range of -32768...0...32767 */
	int XAxisID,YAxisID;          /* the IDs of the physical PC joystick's axis to be used to gain ST joystick axis input */
	int Buttons;                  /* JOY_BUTTON1 */
} JOYREADING;

typedef struct
{
    const char *SDLJoystickName;
    int XAxisID,YAxisID;           /* the IDs associated with a certain SDL joystick */
} JOYAXISMAPPING;

static SDL_Joystick *sdlJoystick[ JOYSTICK_COUNT ] =		/* SDL's joystick structures */
{
	NULL, NULL, NULL, NULL, NULL, NULL
};

/* Further explanation see JoyInit() */
static JOYAXISMAPPING const *sdlJoystickMapping[ JOYSTICK_COUNT ] =	/* references which axis are actually in use by the selected SDL joystick */
{
	NULL, NULL, NULL, NULL, NULL, NULL
};

static bool bJoystickWorking[ JOYSTICK_COUNT ] =		/* Is joystick plugged in and working? */
{
	false, false, false, false, false, false
};

int JoystickSpaceBar = false;           /* State of space-bar on joystick button 2 */
static Uint8 nJoyKeyEmu[ JOYSTICK_COUNT ];
static Uint16 nSteJoySelect;


/**
 * Get joystick name
 */
const char *Joy_GetName(int id)
{
#if WITH_SDL2
	return SDL_JoystickName(sdlJoystick[id]);
#else
	return SDL_JoystickName(id);
#endif
}

/**
 * Return maximum available real joystick ID
 */
int Joy_GetMaxId(void)
{
	int count = SDL_NumJoysticks();
	if (count > JOYSTICK_COUNT)
		count = JOYSTICK_COUNT;
	return count - 1;
}

/**
 * Make sure real Joystick ID is valid, and if not, disable it & return false
 */
bool Joy_ValidateJoyId(int i)
{
	/* Unavailable joystick ID -> disable it if necessary */
	if (ConfigureParams.Joysticks.Joy[i].nJoystickMode == JOYSTICK_REALSTICK &&
	    !bJoystickWorking[ConfigureParams.Joysticks.Joy[i].nJoyId])
	{
		Log_Printf(LOG_WARN, "Selected real Joystick %d unavailable, disabling ST joystick %d\n", ConfigureParams.Joysticks.Joy[i].nJoyId, i);
		ConfigureParams.Joysticks.Joy[i].nJoystickMode = JOYSTICK_DISABLED;
		ConfigureParams.Joysticks.Joy[i].nJoyId = 0;
		return false;
	}
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * This function initialises the (real) joysticks.
 */
void Joy_Init(void)
{
	/* Joystick axis mapping table				*/
	/* Matthias Arndt <marndt@asmsoftware.de>		*/
	/* Somehow, not all SDL joysticks are created equal.	*/
	/* Not all pads or sticks use axis 0 for x and axis 1	*/
	/* for y information.					*/
	/* This table allows to remap the axis to used.		*/
	/* A joystick is identified by its SDL name and 	*/
	/* followed by the X axis to use and the Y axis.	*/
	/* Find out the axis number with the tool jstest.	*/

	/* FIXME: Read those settings from a configuration file and make them tunable from the GUI. */
	static const JOYAXISMAPPING AxisMappingTable [] =
	{
		/* Example entry for mapping joystick axis for a certain device: */
		/* USB game pad with ID ID 0079:0011, sold by Speedlink with axis 3 and 4 used */
		/*{"USB Gamepad" , 3, 4}, */
		/* Default entry used if no other SDL joystick name does match (should be last of this list) */
		{"*DEFAULT*" , 0, 1},
	};

	int i, j, nPadsConnected;

	/* Initialise SDL's joystick subsystem: */
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
	{
		Log_Printf(LOG_ERROR, "Could not init joysticks: %s\n", SDL_GetError());
		return;
	}

	/* Scan joystick connection array for working joysticks */
	nPadsConnected = SDL_NumJoysticks();
	for (i = 0; i < nPadsConnected && i < JOYSTICK_COUNT ; i++)
	{
		/* Open the joystick for use */
		sdlJoystick[i] = SDL_JoystickOpen(i);
		/* Is joystick ok? */
		if (sdlJoystick[i] != NULL)
		{
			/* Set as working */
			bJoystickWorking[i] = true;
			Log_Printf(LOG_DEBUG, "Joystick %i: %s\n", i, Joy_GetName(i));
			/* determine joystick axis mapping for given SDL joystick name, last is default: */
			for (j = 0; j < ARRAYSIZE(AxisMappingTable)-1; j++) {
				/* check if ID string matches the one reported by SDL: */
				if(strncmp(AxisMappingTable[j].SDLJoystickName, Joy_GetName(i), strlen(AxisMappingTable[j].SDLJoystickName)) == 0)
					break;
			}

			sdlJoystickMapping[i] = &(AxisMappingTable[j]);
			Log_Printf(LOG_DEBUG, "Joystick %i maps axis %d and %d (%s)\n", i, sdlJoystickMapping[i]->XAxisID, sdlJoystickMapping[i]->YAxisID,
					sdlJoystickMapping[i]->SDLJoystickName );
		}
	}

	for (i = 0; i < JOYSTICK_COUNT ; i++)
		Joy_ValidateJoyId(i);

	JoystickSpaceBar = false;
}


/*-----------------------------------------------------------------------*/
/**
 * Close the (real) joysticks.
 */
void Joy_UnInit(void)
{
	int i, nPadsConnected;

	nPadsConnected = SDL_NumJoysticks();

	for (i = 0; i < nPadsConnected && i < JOYSTICK_COUNT ; i++)
	{
		if (bJoystickWorking[i] == true)
		{
			SDL_JoystickClose(sdlJoystick[i]);
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Read details from joystick using SDL calls
 * NOTE ID is that of SDL
 */
static bool Joy_ReadJoystick(int nSdlJoyID, JOYREADING *pJoyReading)
{
	/* Joystick is OK, read position from the configured joystick axis */
	pJoyReading->XPos = SDL_JoystickGetAxis(sdlJoystick[nSdlJoyID], pJoyReading->XAxisID);
	pJoyReading->YPos = SDL_JoystickGetAxis(sdlJoystick[nSdlJoyID], pJoyReading->YAxisID);
	/* Sets bit #0 if button #1 is pressed: */
	pJoyReading->Buttons = SDL_JoystickGetButton(sdlJoystick[nSdlJoyID], 0);
	/* Sets bit #1 if button #2 is pressed: */
	if (SDL_JoystickGetButton(sdlJoystick[nSdlJoyID], 1))
		pJoyReading->Buttons |= JOY_BUTTON2;

	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Read PC joystick and return ST format byte, i.e. lower 4 bits direction
 * and top bit fire.
 * NOTE : ID 0 is Joystick 0/Mouse and ID 1 is Joystick 1 (default),
 *        ID 2 and 3 are STE joypads and ID 4 and 5 are parport joysticks.
 */
Uint8 Joy_GetStickData(int nStJoyId)
{
	Uint8 nData = 0;
	JOYREADING JoyReading;
	int nSdlJoyId;
	int nAxes; /* how many joystick axes are on the current selected SDL joystick? */

	nSdlJoyId = ConfigureParams.Joysticks.Joy[nStJoyId].nJoyId;
	nAxes = SDL_JoystickNumAxes(sdlJoystick[nSdlJoyId]);

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
		/* get joystick axis from configuration settings and make them plausible */
		JoyReading.XAxisID = sdlJoystickMapping[nSdlJoyId]->XAxisID;
		JoyReading.YAxisID = sdlJoystickMapping[nSdlJoyId]->YAxisID;

		/* make selected axis IDs plausible */
		if(  (JoyReading.XAxisID == JoyReading.YAxisID) /* same joystick axis for two directions? */
		   ||(JoyReading.XAxisID > nAxes)               /* ID for x axis beyond nr of existing axes? */
		   ||(JoyReading.YAxisID > nAxes)               /* ID for y axis beyond nr of existing axes? */
		  )
		{
			/* define sane SDL joystick axis defaults and prepare them for saving back to the config file: */
			JoyReading.XAxisID = 0;
			JoyReading.YAxisID = 1;
		}

		/* Read real joystick and map to emulated ST joystick for emulation */
		if (!Joy_ReadJoystick(nSdlJoyId, &JoyReading))
		{
			/* Something is wrong, we cannot read the joystick from SDL */
			bJoystickWorking[nSdlJoyId] = false;
			return 0;
		}

		/* Directions */
		if (JoyReading.YPos <= JOYRANGE_UP_VALUE)
			nData |= ATARIJOY_BITMASK_UP;
		else if (JoyReading.YPos >= JOYRANGE_DOWN_VALUE)
			nData |= ATARIJOY_BITMASK_DOWN;
		if (JoyReading.XPos <= JOYRANGE_LEFT_VALUE)
			nData |= ATARIJOY_BITMASK_LEFT;
		else if (JoyReading.XPos >= JOYRANGE_RIGHT_VALUE)
			nData |= ATARIJOY_BITMASK_RIGHT;

		/* PC Joystick button 1 is set as ST joystick button */
		if (JoyReading.Buttons & JOY_BUTTON1)
			nData |= ATARIJOY_BITMASK_FIRE;

		/* Enable PC Joystick button 2 to mimick space bar (For XenonII, Flying Shark etc...) */
		if (nStJoyId == JOYID_JOYSTICK1 && (JoyReading.Buttons & JOY_BUTTON2))
		{
			if (ConfigureParams.Joysticks.Joy[nStJoyId].bEnableJumpOnFire2)
			{
				/* If "Jump on Button 2" is enabled, PC Joystick button 2 acts as "ST Joystick up" */
				nData |= ATARIJOY_BITMASK_UP;
			} else {
				/* If "Jump on Button 2" is not enabled, PC Joystick button 2 acts as pressing SPACE on the ST keyboard */
				/* Only press 'space bar' if not in NULL state */
				if (!JoystickSpaceBar)
				{
					/* Press, ikbd will send packets and de-press */
					JoystickSpaceBar = JOYSTICK_SPACE_DOWN;
				}
			}
		}
	}

	/* Ignore fire button every 8 frames if enabled autofire (for both cursor emulation and joystick) */
	if (ConfigureParams.Joysticks.Joy[nStJoyId].bEnableAutoFire)
	{
		if ((nVBLs&0x7)<4)
			nData &= ~ATARIJOY_BITMASK_FIRE;          /* Remove top bit! */
	}

	return nData;
}


/*-----------------------------------------------------------------------*/
/**
 * Get the fire button states.
 * Note: More than one fire buttons are only supported for real joystick,
 * not for keyboard emulation!
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
/**
 * Set joystick cursor emulation for given port.  This assumes that
 * if the same keys have been defined for "cursor key emulation" in
 * other ports, the emulation for them has been switched off. Returns
 * 1 if the port number was OK, zero for error.
 */
bool Joy_SetCursorEmulation(int port)
{
	if (port < 0 || port >= JOYSTICK_COUNT) {
		return false;
	}
	ConfigureParams.Joysticks.Joy[port].nJoystickMode = JOYSTICK_KEYBOARD;
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Toggle joystick cursor emulation between port 0, port 1 and being off
 * from them. When it's turned off from them, the port's previous state
 * is restored
 */
void Joy_ToggleCursorEmulation(void)
{
	static JOYSTICKMODE saved[2] = { JOYSTICK_DISABLED, JOYSTICK_DISABLED };
	JOYSTICKMODE state;
	int i, port = 2;
	for (i = 0; i < 2; i++) {
		state = ConfigureParams.Joysticks.Joy[i].nJoystickMode;
		if (state == JOYSTICK_KEYBOARD) {
			port = i;
		} else {
			saved[i] = state;
		}
	}
	switch (port) {
	case 0:  /* (only) in port 0, disable cursor emu */
		ConfigureParams.Joysticks.Joy[0].nJoystickMode = saved[0];
		break;
	case 1:  /* (at least) in port 1, switch cursor emu to port 0 */
		ConfigureParams.Joysticks.Joy[1].nJoystickMode = saved[1];
		ConfigureParams.Joysticks.Joy[0].nJoystickMode = JOYSTICK_KEYBOARD;
		break;
	default:  /* neither in port 0 or 1, enable cursor emu to port 1 */
		ConfigureParams.Joysticks.Joy[1].nJoystickMode = JOYSTICK_KEYBOARD;
	}
	Statusbar_UpdateInfo();
}


/*-----------------------------------------------------------------------*/
/**
 * Switch between joystick types in given joyport
 */
bool Joy_SwitchMode(int port)
{
	int mode;
	if (port < 0 || port >= JOYSTICK_COUNT) {
		return false;
	}
	mode = (ConfigureParams.Joysticks.Joy[port].nJoystickMode + 1) % JOYSTICK_MODES;
	ConfigureParams.Joysticks.Joy[port].nJoystickMode = mode;
	Statusbar_UpdateInfo();
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * A key has been pressed down, check if we use it for joystick emulation
 * via keyboard.
 */
bool Joy_KeyDown(int symkey, int modkey)
{
	int i;

	for (i = 0; i < JOYSTICK_COUNT; i++)
	{
		if (ConfigureParams.Joysticks.Joy[i].nJoystickMode == JOYSTICK_KEYBOARD
		    && !(modkey & KMOD_SHIFT))
		{
			if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeUp)
			{
				nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_DOWN;   /* Disable down */
				nJoyKeyEmu[i] |= ATARIJOY_BITMASK_UP;    /* Enable up */
				return true;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeDown)
			{
				nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_UP;   /* Disable up */
				nJoyKeyEmu[i] |= ATARIJOY_BITMASK_DOWN;    /* Enable down */
				return true;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeLeft)
			{
				nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_RIGHT;   /* Disable right */
				nJoyKeyEmu[i] |= ATARIJOY_BITMASK_LEFT;    /* Enable left */
				return true;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeRight)
			{
				nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_LEFT;   /* Disable left */
				nJoyKeyEmu[i] |= ATARIJOY_BITMASK_RIGHT;    /* Enable right */
				return true;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeFire)
			{
				nJoyKeyEmu[i] |= ATARIJOY_BITMASK_FIRE;
				return true;
			}
		}
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * A key has been released, check if we use it for joystick emulation
 * via keyboard.
 */
bool Joy_KeyUp(int symkey, int modkey)
{
	int i;

	for (i = 0; i < JOYSTICK_COUNT; i++)
	{
		if (ConfigureParams.Joysticks.Joy[i].nJoystickMode == JOYSTICK_KEYBOARD
		    && !(modkey & KMOD_SHIFT))
		{
			if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeUp)
			{
				nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_UP;
				return true;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeDown)
			{
				nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_DOWN;
				return true;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeLeft)
			{
				nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_LEFT;
				return true;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeRight)
			{
				nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_RIGHT;
				return true;
			}
			else if (symkey == ConfigureParams.Joysticks.Joy[i].nKeyCodeFire)
			{
				nJoyKeyEmu[i] &= ~ATARIJOY_BITMASK_FIRE;
				return true;
			}
		}
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Read from STE joypad buttons register (0xff9201)
 */
void Joy_StePadButtons_ReadByte(void)
{
	Uint8 nData = 0xff;

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
			if (nButtons & 0x08)  /* Fire button OPTION pressed? */
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

	Dprintf(("0xff9201 -> 0x%04x\n", nData));
	IoMem_WriteByte(0xff9201, nData);
}


/*-----------------------------------------------------------------------*/
/**
 * Read from STE joypad direction/buttons register (0xff9202)
 *
 * This is used e.g. by Reservoir Gods' Tautology II
 */
void Joy_StePadMulti_ReadWord(void)
{
	Uint16 nData = 0xff;

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
			nData |= ~(Joy_GetFireButtons(JOYID_STEPADB) >> (13-4)) & 0xf0;
		}
		else if (!(nSteJoySelect & 0x40))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_STEPADB) >> (9-4)) & 0xf0;
		}
		else if (!(nSteJoySelect & 0x80))
		{
			nData |= ~(Joy_GetFireButtons(JOYID_STEPADB) >> (5-4)) & 0xf0;
		}
	}

	nData = (nData << 8) | 0x0ff;
	Dprintf(("0xff9202 -> 0x%04x\n", nData));
	IoMem_WriteWord(0xff9202, nData);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to STE joypad selection register (0xff9202)
 */
void Joy_StePadMulti_WriteWord(void)
{
	nSteJoySelect = IoMem_ReadWord(0xff9202);
	Dprintf(("0xff9202 <- 0x%04x\n", nSteJoySelect));
}
