/*
  Hatari - joy_ui.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Handling of the real joysticks/-pads from the host.
*/

#include "main.h"

#if ENABLE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "configuration.h"
#include "ioMem.h"
#include "joy.h"
#include "joy_ui.h"
#include "keymap.h"
#include "log.h"
#include "video.h"

#define JOY_DEBUG 0
#if JOY_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


static int nPadsConnected;

#if ENABLE_SDL3
static SDL_JoystickID *joy_ids;
#endif

static SDL_Joystick *sdlJoystick[ JOYSTICK_COUNT ] =		/* SDL's joystick structures */
{
	NULL, NULL, NULL, NULL, NULL, NULL
};

static bool bJoystickWorking[ JOYSTICK_COUNT ] =		/* Is joystick plugged in and working? */
{
	false, false, false, false, false, false
};


/**
 * Get joystick name
 */
const char *JoyUI_GetName(int id)
{
	return SDL_JoystickName(sdlJoystick[id]);
}

/**
 * Return maximum available real joystick ID, or
 * zero on error or no joystick (to avoid invalid array accesses)
 */
int JoyUI_GetMaxId(void)
{
	int count = JoyUI_NumJoysticks();
	if (count > JOYSTICK_COUNT)
		count = JOYSTICK_COUNT;
	if (count > 0)
		return count - 1;
	return 0;
}

int JoyUI_NumJoysticks(void)
{
#if ENABLE_SDL3
	return nPadsConnected;
#else
	return SDL_NumJoysticks();
#endif
}


/**
 * Make sure real Joystick ID is valid, and if not, disable it & return false
 */
bool JoyUI_ValidateJoyId(int i)
{
	int joyid = ConfigureParams.Joysticks.Joy[i].nJoyId;

	/* Unavailable joystick ID -> disable it if necessary */
	if (ConfigureParams.Joysticks.Joy[i].nJoystickMode == JOYSTICK_REALSTICK &&
	    !bJoystickWorking[joyid])
	{
		Log_Printf(LOG_WARN, "Selected real Joystick %d unavailable, disabling ST joystick %d\n", joyid, i);
		ConfigureParams.Joysticks.Joy[i].nJoystickMode = JOYSTICK_DISABLED;
		ConfigureParams.Joysticks.Joy[i].nJoyId = 0;
		return false;
	}
	return true;
}


/**
 * This function initialises the (real) joysticks.
 */
void JoyUI_Init(void)
{
	int i;

	/* Initialise SDL's joystick subsystem: */
#if ENABLE_SDL3
	if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK))
#else
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
#endif
	{
		Log_Printf(LOG_ERROR, "Could not init joysticks: %s\n", SDL_GetError());
		return;
	}

	/* Scan joystick connection array for working joysticks */
#if ENABLE_SDL3
	/* FIXME: The joy_ids handling likely needs some more works... */
	joy_ids = SDL_GetJoysticks(&nPadsConnected);
#else
	nPadsConnected = SDL_NumJoysticks();
#endif
	for (i = 0; i < nPadsConnected && i < JOYSTICK_COUNT ; i++)
	{
		/* Open the joystick for use */
		sdlJoystick[i] = SDL_JoystickOpen(i);
		/* Is joystick ok? */
		if (sdlJoystick[i] != NULL)
		{
			/* Set as working */
			bJoystickWorking[i] = true;
			Log_Printf(LOG_DEBUG, "Joystick %i: %s\n", i, JoyUI_GetName(i));
		}
	}

	for (i = 0; i < JOYSTICK_COUNT ; i++)
		JoyUI_ValidateJoyId(i);

	JoystickSpaceBar = JOYSTICK_SPACE_NULL;
}


/**
 * Close the (real) joysticks.
 */
void JoyUI_UnInit(void)
{
	for (int i = 0; i < nPadsConnected && i < JOYSTICK_COUNT ; i++)
	{
		if (bJoystickWorking[i] == true)
		{
			bJoystickWorking[i] = false;
			SDL_JoystickClose(sdlJoystick[i]);
		}
		sdlJoystick[i] = NULL;
	}

#if ENABLE_SDL3
	SDL_free(joy_ids);
#endif
	nPadsConnected = 0;
}


/**
 * Set default keys for joystick emulation
 */
void JoyUI_SetDefaultKeys(int stjoy_id)
{
	int i;

	ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeUp = SDLK_UP;
	ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeDown = SDLK_DOWN;
	ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeLeft = SDLK_LEFT;
	ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeRight = SDLK_RIGHT;
	ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeFire = SDLK_RCTRL;

	if (stjoy_id == JOYID_JOYPADA)
	{
		for (i = 0; i <= 9; i++)
			ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeNum[i] = SDLK_0 + i;
		ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeB = SDLK_b;
		ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeC = SDLK_c;
		ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeOption = SDLK_o;
		ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodePause = SDLK_p;
		ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeHash = SDLK_HASH;
		ConfigureParams.Joysticks.Joy[stjoy_id].nKeyCodeStar = SDLK_k;
	}
}


/**
 * Read details from joystick using SDL calls
 */
bool JoyUI_ReadJoystick(int nStJoyId, JOYREADING *pJoyReading)
{
	int nSdlJoyID = ConfigureParams.Joysticks.Joy[nStJoyId].nJoyId;
	unsigned hat;

	if (nSdlJoyID < 0 || !bJoystickWorking[nSdlJoyID])
		return false;

	hat = SDL_JoystickGetHat(sdlJoystick[nSdlJoyID], 0);

	/* Joystick is OK, read position from the configured joystick axis. */
	/* TODO: Make axis IDs configurable in the config file! */
	pJoyReading->XPos = SDL_JoystickGetAxis(sdlJoystick[nSdlJoyID], 0);
	pJoyReading->YPos = SDL_JoystickGetAxis(sdlJoystick[nSdlJoyID], 1);
	/* Similarly to other emulators that support hats, override axis readings with hats */
	if (hat & SDL_HAT_LEFT)
		pJoyReading->XPos = -32768;
	if (hat & SDL_HAT_RIGHT)
		pJoyReading->XPos = 32767;
	if (hat & SDL_HAT_UP)
		pJoyReading->YPos = -32768;
	if (hat & SDL_HAT_DOWN)
		pJoyReading->YPos = 32767;

	pJoyReading->Buttons = 0;
	/* Sets bits based on pressed buttons */
	for (int i = 0; i < JOYSTICK_BUTTONS; i++)
	{
		int button = ConfigureParams.Joysticks.Joy[nStJoyId].nJoyButMap[i];
		if (button >= 0 && SDL_JoystickGetButton(sdlJoystick[nSdlJoyID], button))
			pJoyReading->Buttons |= 1 << i;
	}
	return true;
}


/**
 * Get the fire button states from a real joystick on the host.
 */
int JoyUI_GetRealFireButtons(int nStJoyId)
{
	int nSdlJoyId;
	int i, nMaxButtons;
	int buttons = 0;

	nSdlJoyId = ConfigureParams.Joysticks.Joy[nStJoyId].nJoyId;

	if (!bJoystickWorking[nSdlJoyId])
		return 0;

	nMaxButtons = SDL_JoystickNumButtons(sdlJoystick[nSdlJoyId]);
	if (nMaxButtons > 17)
		nMaxButtons = 17;

	/* Now read all fire buttons and set a bit for each pressed button: */
	for (i = 0; i < nMaxButtons; i++)
	{
		if (SDL_JoystickGetButton(sdlJoystick[nSdlJoyId], i))
		{
			buttons |= (1 << i);
		}
	}

	return buttons;
}
