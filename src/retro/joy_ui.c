/*
  Hatari - joy_ui.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Handling of the real joysticks/-pads from the host.
*/

#include "main.h"

#include "configuration.h"
#include "ioMem.h"
#include "joy.h"
#include "joy_ui.h"
#include "keymap.h"
#include "log.h"
#include "video.h"



/**
 * Get joystick name
 */
const char *JoyUI_GetName(int id)
{
	return "n/a";
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
	return 0;
}


/**
 * Make sure real Joystick ID is valid, and if not, disable it & return false
 */
bool JoyUI_ValidateJoyId(int i)
{
	return false;
}


/**
 * This function initialises the (real) joysticks.
 */
void JoyUI_Init(void)
{
}


/**
 * Close the (real) joysticks.
 */
void JoyUI_UnInit(void)
{
}


/**
 * Set default keys for joystick emulation
 */
void JoyUI_SetDefaultKeys(int stjoy_id)
{
}


/**
 * Read details from joystick
 */
bool JoyUI_ReadJoystick(int nStJoyId, JOYREADING *pJoyReading)
{
	return false;
}


/**
 * Get the fire button states from a real joystick on the host.
 */
int JoyUI_GetRealFireButtons(int nStJoyId)
{
	return 0;
}
