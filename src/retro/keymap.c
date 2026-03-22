/*
  Hatari - keymap.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Map key events to ST scancodes and send them to IKBD as
  pressed/released keys.

  See https://tho-otto.de/keyboards/ for the Atari ST keyboard layouts.
*/
const char Keymap_fileid[] = "Hatari keymap.c";

#include <libretro.h>

#include "main.h"
#include "main_retro.h"
#include "keymap.h"
#include "configuration.h"
#include "ikbd.h"
#include "log.h"

/* if not able to map */
#define ST_NO_SCANCODE 0xff


/**
 * Default function for mapping host keycode to ST scan code.
 * This contains the ST keycode used by the majority of TOS regions for
 * that semantic symbol.
 */
static uint8_t Keymap_SymbolicToStScanCode_default(unsigned int hostkey)
{
	uint8_t code;

	switch (hostkey)
	{
	 case RETROK_BACKSPACE: code = 0x0E; break;
	 case RETROK_TAB: code = 0x0F; break;
	 case RETROK_CLEAR: code = 0x47; break;
	 case RETROK_RETURN: code = 0x1C; break;
	 case RETROK_ESCAPE: code = ST_ESC; break;
	 case RETROK_SPACE: code = 0x39; break;
	 case RETROK_EXCLAIM: code = 0x09; break;     /* on azerty? */
	 case RETROK_QUOTEDBL: code = 0x04; break;    /* on azerty? */
	 case RETROK_HASH: code = 0x2B; break;        /* DE, UK host only, for FR/UK/DK/NL TOS */
	 case RETROK_DOLLAR: code = 0x1b; break;      /* on azerty */
	 case RETROK_AMPERSAND: code = 0x02; break;   /* on azerty? */
	 case RETROK_QUOTE: code = 0x28; break;
	 case RETROK_LEFTPAREN: code = 0x63; break;
	 case RETROK_RIGHTPAREN: code = 0x64; break;
	 case RETROK_ASTERISK: code = 0x66; break;
	 case RETROK_PLUS: code = 0x4e; break;
	 case RETROK_COMMA: code = 0x33; break;
	 case RETROK_MINUS: code = 0x35; break;       /* default for DE/IT/SE/CH/FI/NO/DK/CZ */
	 case RETROK_PERIOD: code = 0x34; break;
	 case RETROK_SLASH: code = 0x35; break;
	 case RETROK_0: code = 0x0B; break;
	 case RETROK_1: code = 0x02; break;
	 case RETROK_2: code = 0x03; break;
	 case RETROK_3: code = 0x04; break;
	 case RETROK_4: code = 0x05; break;
	 case RETROK_5: code = 0x06; break;
	 case RETROK_6: code = 0x07; break;
	 case RETROK_7: code = 0x08; break;
	 case RETROK_8: code = 0x09; break;
	 case RETROK_9: code = 0x0A; break;
	 case RETROK_COLON: code = 0x34; break;
	 case RETROK_SEMICOLON: code = 0x27; break;
	 case RETROK_LESS: code = 0x60; break;
	 case RETROK_EQUALS: code = 0x0D; break;
	 case RETROK_GREATER : code = 0x34; break;
	 case RETROK_QUESTION: code = 0x35; break;
	 case RETROK_AT: code = 0x28; break;
	 case RETROK_LEFTBRACKET: code = 0x1A; break;
	 case RETROK_BACKSLASH: code = 0x2B; break;
	 case RETROK_RIGHTBRACKET: code = 0x1B; break;
	 case RETROK_CARET: code = 0x2B; break;
	 case RETROK_UNDERSCORE: code = 0x0C; break;
	 case RETROK_BACKQUOTE: code = 0x29; break;
	 case RETROK_a: code = 0x1E; break;
	 case RETROK_b: code = 0x30; break;
	 case RETROK_c: code = 0x2E; break;
	 case RETROK_d: code = 0x20; break;
	 case RETROK_e: code = 0x12; break;
	 case RETROK_f: code = 0x21; break;
	 case RETROK_g: code = 0x22; break;
	 case RETROK_h: code = 0x23; break;
	 case RETROK_i: code = 0x17; break;
	 case RETROK_j: code = 0x24; break;
	 case RETROK_k: code = 0x25; break;
	 case RETROK_l: code = 0x26; break;
	 case RETROK_m: code = 0x32; break;
	 case RETROK_n: code = 0x31; break;
	 case RETROK_o: code = 0x18; break;
	 case RETROK_p: code = 0x19; break;
	 case RETROK_q: code = 0x10; break;
	 case RETROK_r: code = 0x13; break;
	 case RETROK_s: code = 0x1F; break;
	 case RETROK_t: code = 0x14; break;
	 case RETROK_u: code = 0x16; break;
	 case RETROK_v: code = 0x2F; break;
	 case RETROK_w: code = 0x11; break;
	 case RETROK_x: code = 0x2D; break;
	 case RETROK_y: code = 0x15; break;
	 case RETROK_z: code = 0x2C; break;
	 case RETROK_DELETE: code = 0x53; break;
	 /* Numeric keypad: */
	 case RETROK_KP0: code = 0x70; break;
	 case RETROK_KP1: code = 0x6D; break;
	 case RETROK_KP2: code = 0x6E; break;
	 case RETROK_KP3: code = 0x6F; break;
	 case RETROK_KP4: code = 0x6A; break;
	 case RETROK_KP5: code = 0x6B; break;
	 case RETROK_KP6: code = 0x6C; break;
	 case RETROK_KP7: code = 0x67; break;
	 case RETROK_KP8: code = 0x68; break;
	 case RETROK_KP9: code = 0x69; break;
	 case RETROK_KP_PERIOD: code = 0x71; break;
	 case RETROK_KP_DIVIDE: code = 0x65; break;
	 case RETROK_KP_MULTIPLY: code = 0x66; break;
	 case RETROK_KP_MINUS: code = 0x4A; break;
	 case RETROK_KP_PLUS: code = 0x4E; break;
	 case RETROK_KP_ENTER: code = 0x72; break;
	 case RETROK_KP_EQUALS: code = 0x61; break;
	 /* Arrows + Home/End pad */
	 case RETROK_UP: code = 0x48; break;
	 case RETROK_DOWN: code = 0x50; break;
	 case RETROK_RIGHT: code = 0x4D; break;
	 case RETROK_LEFT: code = 0x4B; break;
	 case RETROK_INSERT: code = 0x52; break;
	 case RETROK_HOME: code = 0x47; break;
	 case RETROK_END: code = 0x61; break;         /* ST Undo */
	 case RETROK_PAGEUP: code = 0x63; break;      /* ST ( */
	 case RETROK_PAGEDOWN: code = 0x64; break;    /* ST ) */
	 /* Function keys */
	 case RETROK_F1: code = 0x3B; break;
	 case RETROK_F2: code = 0x3C; break;
	 case RETROK_F3: code = 0x3D; break;
	 case RETROK_F4: code = 0x3E; break;
	 case RETROK_F5: code = 0x3F; break;
	 case RETROK_F6: code = 0x40; break;
	 case RETROK_F7: code = 0x41; break;
	 case RETROK_F8: code = 0x42; break;
	 case RETROK_F9: code = 0x43; break;
	 case RETROK_F10: code = 0x44; break;
	 case RETROK_F11: code = 0x62; break;         /* ST Help */
	 case RETROK_F12: code = 0x61; break;         /* ST Undo */
	 case RETROK_F13: code = 0x62; break;         /* ST Help */
	 /* Key state modifier keys */
	 case RETROK_CAPSLOCK: code = ST_CAPSLOCK; break;
	 case RETROK_SCROLLOCK: code = 0x61; break;  /* ST Undo */
	 case RETROK_RSHIFT: code = ST_RSHIFT; break;
	 case RETROK_LSHIFT: code = ST_LSHIFT; break;
	 case RETROK_RCTRL: code = ST_CONTROL; break;
	 case RETROK_LCTRL: code = ST_CONTROL; break;
	 case RETROK_RALT: code = ST_ALTERNATE; break;
	 case RETROK_LALT: code = ST_ALTERNATE; break;
	 /* Miscellaneous function keys */
	 case RETROK_HELP: code = 0x62; break;
	 case RETROK_PRINT: code = 0x62; break; /* ST Help */
	 case RETROK_UNDO: code = 0x61; break;
	 default: code = ST_NO_SCANCODE;
	}

	return code;
}


static RETRO_CALLCONV
void Keymap_Callback(bool down, unsigned int keycode, uint32_t character,
                     uint16_t modifiers)
{
	uint8_t STScanCode;

	STScanCode = Keymap_SymbolicToStScanCode_default(keycode);
	if (STScanCode != ST_NO_SCANCODE)
	{
		if (down)
		{
			if (!Keyboard.KeyStates[STScanCode])
			{
				Keyboard.KeyStates[STScanCode] = true;
				IKBD_PressSTKey(STScanCode, true);
			}
		}
		else
		{
			if (Keyboard.KeyStates[STScanCode])
			{
				IKBD_PressSTKey(STScanCode, false);
				Keyboard.KeyStates[STScanCode] = false;
			}
		}
	}
}

/**
 * Initialization.
 */
void Keymap_Init(void)
{
	static struct retro_keyboard_callback kb_cb =
	{
		Keymap_Callback
	};
	environment_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, (void*)&kb_cb);
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
