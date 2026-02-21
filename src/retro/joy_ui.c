/*
  Hatari - joy_ui.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Handling of the real joysticks/-pads from the host.
*/

#include <libretro.h>

#include "main.h"
#include "main_retro.h"
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
void JoyUI_SetDefaultKeys(int id)
{
	/* We don't directly emulate the joystick via keys in libretro, but
	 * use the "RetroPad" instead */
	ConfigureParams.Joysticks.Joy[id].nJoystickMode = JOYSTICK_REALSTICK;
}


/**
 * Read details from joystick
 */
bool JoyUI_ReadJoystick(int id, JOYREADING *joyread)
{
	/* Swap ports 0 and 1, since port 1 is the default in most games.
	 * TODO: Make the mapping of all ports configurable! */
	if (id == 0 || id == 1)
		id ^= 1;

	joyread->XPos = input_state_cb(id, RETRO_DEVICE_ANALOG,
	                               RETRO_DEVICE_INDEX_ANALOG_LEFT,
	                               RETRO_DEVICE_ID_ANALOG_X);
	joyread->YPos = input_state_cb(id, RETRO_DEVICE_ANALOG,
	                               RETRO_DEVICE_INDEX_ANALOG_LEFT,
	                               RETRO_DEVICE_ID_ANALOG_Y);

	/* Override axis readings with hats */
	if (input_state_cb(id, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
		joyread->XPos = -32768;
	if (input_state_cb(id, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
		joyread->XPos = 32767;
	if (input_state_cb(id, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
		joyread->YPos = -32768;
	if (input_state_cb(id, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
		joyread->YPos = 32767;

	/* Buttons */
	joyread->Buttons = 0;
	if (input_state_cb(id, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
		joyread->Buttons |= JOYREADING_BUTTON1;
	if (input_state_cb(id, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))
		joyread->Buttons |= JOYREADING_BUTTON2;
	if (input_state_cb(id, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
		joyread->Buttons |= JOYREADING_BUTTON3;

	return true;
}


/**
 * Get the fire button states from a real joystick on the host.
 */
int JoyUI_GetRealFireButtons(int nStJoyId)
{
	return 0;
}
