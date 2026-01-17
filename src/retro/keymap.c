/*
  Hatari - keymap.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Map key events to ST scancodes and send them to IKBD as
  pressed/released keys.  Based on Hatari configuration options,
  several different ways can be used to map key events.

  See https://tho-otto.de/keyboards/ for the Atari ST keyboard layouts.
*/
const char Keymap_fileid[] = "Hatari keymap.c";

#include <ctype.h>
#include "main.h"
#include "keymap.h"
#include "configuration.h"
#include "file.h"
#include "ikbd.h"
#include "nvram.h"
#include "joy.h"
#include "shortcut.h"
#include "str.h"
#include "tos.h"
#include "debugui.h"
#include "log.h"

/* if not able to map */
#define ST_NO_SCANCODE 0xff

/**
 * Initialization.
 */
void Keymap_Init(void)
{
}


/**
 * Set defaults for shortcut keys
 */
void Keymap_InitShortcutDefaultKeys(void)
{
}


/**
 * Load keyboard remap file
 */
void Keymap_LoadRemapFile(const char *pszFileName)
{
}


/**
 * Simulate press or release of a key corresponding to given character
 */
void Keymap_SimulateCharacter(char asckey, bool press)
{
}


/**
 * Maps a key name to its keycode
 */
int Keymap_GetKeyFromName(const char *name)
{
	return 0;
}


/**
 * Maps an keycode to a name
 */
const char *Keymap_GetKeyName(int keycode)
{
	if (!keycode)
		return "";

	return "n/a";
}


/**
 * Informs symbolic keymap of loaded TOS country.
 */
void Keymap_SetCountry(int countrycode)
{
}


/**
 * Check whether one of the shift keys is hold down
 */
bool Keymap_IsShiftPressed(void)
{
	return false;
}
