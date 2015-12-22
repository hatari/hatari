/*
  Hatari - keymap.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Here we process a key press and the remapping of the scancodes.
*/
const char Keymap_fileid[] = "Hatari keymap.c : " __DATE__ " " __TIME__;

#include <ctype.h>
#include "main.h"
#include "keymap.h"
#include "configuration.h"
#include "file.h"
#include "ikbd.h"
#include "joy.h"
#include "shortcut.h"
#include "str.h"
#include "screen.h"
#include "debugui.h"
#include "log.h"


#if !WITH_SDL2
/* This table is used to translate a symbolic keycode to the (SDL) scancode */
static Uint8 SdlSymToSdlScan[SDLK_LAST];
#endif

/* Table for loaded keys: */
static int LoadedKeymap[KBD_MAX_SCANCODE][2];

/* List of ST scan codes to NOT de-bounce when running in maximum speed */
static const char DebounceExtendedKeys[] =
{
	0x1d,  /* CTRL */
	0x2a,  /* Left SHIFT */
	0x01,  /* ESC */
	0x38,  /* ALT */
	0x36,  /* Right SHIFT */
	0      /* term */
};



/*-----------------------------------------------------------------------*/
/**
 * Initialization.
 */
void Keymap_Init(void)
{
#if !WITH_SDL2
	memset(SdlSymToSdlScan, 0, sizeof(SdlSymToSdlScan));      /* Clear array */
#endif
	Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);
}

/**
 * Map SDL symbolic key to ST scan code
 */
static const char Keymap_SymbolicToStScanCode(SDL_keysym* pKeySym)
{
	char code;

	switch (pKeySym->sym)
	{
	 case SDLK_BACKSPACE: code = 0x0E; break;
	 case SDLK_TAB: code = 0x0F; break;
	 case SDLK_CLEAR: code = 0x47; break;
	 case SDLK_RETURN: code = 0x1C; break;
	 case SDLK_ESCAPE: code = 0x01; break;
	 case SDLK_SPACE: code = 0x39; break;
	 case SDLK_EXCLAIM: code = 0x09; break;     /* on azerty? */
	 case SDLK_QUOTEDBL: code = 0x04; break;    /* on azerty? */
	 case SDLK_HASH: code = 0x29; break;
	 case SDLK_DOLLAR: code = 0x1b; break;      /* on azerty */
	 case SDLK_AMPERSAND: code = 0x02; break;   /* on azerty? */
	 case SDLK_QUOTE: code = 0x28; break;
	 case SDLK_LEFTPAREN: code = 0x63; break;
	 case SDLK_RIGHTPAREN: code = 0x64; break;
	 case SDLK_ASTERISK: code = 0x66; break;
	 case SDLK_PLUS: code = 0x1B; break;
	 case SDLK_COMMA: code = 0x33; break;
	 case SDLK_MINUS: code = 0x35; break;
	 case SDLK_PERIOD: code = 0x34; break;
	 case SDLK_SLASH: code = 0x35; break;
	 case SDLK_0: code = 0x0B; break;
	 case SDLK_1: code = 0x02; break;
	 case SDLK_2: code = 0x03; break;
	 case SDLK_3: code = 0x04; break;
	 case SDLK_4: code = 0x05; break;
	 case SDLK_5: code = 0x06; break;
	 case SDLK_6: code = 0x07; break;
	 case SDLK_7: code = 0x08; break;
	 case SDLK_8: code = 0x09; break;
	 case SDLK_9: code = 0x0A; break;
	 case SDLK_COLON: code = 0x34; break;
	 case SDLK_SEMICOLON: code = 0x27; break;
	 case SDLK_LESS: code = 0x60; break;
	 case SDLK_EQUALS: code = 0x0D; break;
	 //case SDLK_GREATER : code = -1; break;
	 //case SDLK_QUESTION: code = -1; break;
	 //case SDLK_AT: code = -1; break;
	 case SDLK_LEFTBRACKET: code = 0x63; break;
	 case SDLK_BACKSLASH: code = 0x2B; break;     /* Might be 0x60 for UK keyboards */
	 case SDLK_RIGHTBRACKET: code = 0x64; break;
	 case SDLK_CARET: code = 0x2B; break;
	 //case SDLK_UNDERSCORE: code = -1; break;
	 case SDLK_BACKQUOTE: code = 0x52; break;
	 case SDLK_a: code = 0x1E; break;
	 case SDLK_b: code = 0x30; break;
	 case SDLK_c: code = 0x2E; break;
	 case SDLK_d: code = 0x20; break;
	 case SDLK_e: code = 0x12; break;
	 case SDLK_f: code = 0x21; break;
	 case SDLK_g: code = 0x22; break;
	 case SDLK_h: code = 0x23; break;
	 case SDLK_i: code = 0x17; break;
	 case SDLK_j: code = 0x24; break;
	 case SDLK_k: code = 0x25; break;
	 case SDLK_l: code = 0x26; break;
	 case SDLK_m: code = 0x32; break;
	 case SDLK_n: code = 0x31; break;
	 case SDLK_o: code = 0x18; break;
	 case SDLK_p: code = 0x19; break;
	 case SDLK_q: code = 0x10; break;
	 case SDLK_r: code = 0x13; break;
	 case SDLK_s: code = 0x1F; break;
	 case SDLK_t: code = 0x14; break;
	 case SDLK_u: code = 0x16; break;
	 case SDLK_v: code = 0x2F; break;
	 case SDLK_w: code = 0x11; break;
	 case SDLK_x: code = 0x2D; break;
	 case SDLK_y: code = 0x15; break;
	 case SDLK_z: code = 0x2C; break;
	 case SDLK_DELETE: code = 0x53; break;
	 /* End of ASCII mapped keysyms */
#if WITH_SDL2
	 case 223: code = 0x0C; break;
	 case 228: code = 0x28; break;
	 case 246: code = 0x27; break;
	 case 252: code = 0x1A; break;
#else
	 case SDLK_WORLD_0: code = 0x0d; break;
	 case SDLK_WORLD_1: code = 0x0c; break;
	 case SDLK_WORLD_2: code = 0x1a; break;
	 case SDLK_WORLD_3: code = 0x28; break;
	 case SDLK_WORLD_4: code = 0x27; break;
	 case SDLK_WORLD_20: code = 0x0D; break;
	 case SDLK_WORLD_63: code = 0x0C; break;
	 case SDLK_WORLD_68: code = 0x28; break;
	 case SDLK_WORLD_86: code = 0x27; break;
	 case SDLK_WORLD_92: code = 0x1A; break;
#endif
	 /* Numeric keypad: */
	 case SDLK_KP0: code = 0x70; break;
	 case SDLK_KP1: code = 0x6D; break;
	 case SDLK_KP2: code = 0x6E; break;
	 case SDLK_KP3: code = 0x6F; break;
	 case SDLK_KP4: code = 0x6A; break;
	 case SDLK_KP5: code = 0x6B; break;
	 case SDLK_KP6: code = 0x6C; break;
	 case SDLK_KP7: code = 0x67; break;
	 case SDLK_KP8: code = 0x68; break;
	 case SDLK_KP9: code = 0x69; break;
	 case SDLK_KP_PERIOD: code = 0x71; break;
	 case SDLK_KP_DIVIDE: code = 0x65; break;
	 case SDLK_KP_MULTIPLY: code = 0x66; break;
	 case SDLK_KP_MINUS: code = 0x4A; break;
	 case SDLK_KP_PLUS: code = 0x4E; break;
	 case SDLK_KP_ENTER: code = 0x72; break;
	 case SDLK_KP_EQUALS: code = 0x61; break;
	 /* Arrows + Home/End pad */
	 case SDLK_UP: code = 0x48; break;
	 case SDLK_DOWN: code = 0x50; break;
	 case SDLK_RIGHT: code = 0x4D; break;
	 case SDLK_LEFT: code = 0x4B; break;
	 case SDLK_INSERT: code = 0x52; break;
	 case SDLK_HOME: code = 0x47; break;
	 case SDLK_END: code = 0x61; break;
	 case SDLK_PAGEUP: code = 0x63; break;
	 case SDLK_PAGEDOWN: code = 0x64; break;
	 /* Function keys */
	 case SDLK_F1: code = 0x3B; break;
	 case SDLK_F2: code = 0x3C; break;
	 case SDLK_F3: code = 0x3D; break;
	 case SDLK_F4: code = 0x3E; break;
	 case SDLK_F5: code = 0x3F; break;
	 case SDLK_F6: code = 0x40; break;
	 case SDLK_F7: code = 0x41; break;
	 case SDLK_F8: code = 0x42; break;
	 case SDLK_F9: code = 0x43; break;
	 case SDLK_F10: code = 0x44; break;
	 case SDLK_F13: code = 0x62; break;
	 /* Key state modifier keys */
	 case SDLK_CAPSLOCK: code = 0x3A; break;
	 case SDLK_SCROLLOCK: code = 0x61; break;
	 case SDLK_RSHIFT: code = 0x36; break;
	 case SDLK_LSHIFT: code = 0x2A; break;
	 case SDLK_RCTRL: code = 0x1D; break;
	 case SDLK_LCTRL: code = 0x1D; break;
	 case SDLK_RALT: code = 0x38; break;
	 case SDLK_LALT: code = 0x38; break;
	 /* Miscellaneous function keys */
	 case SDLK_HELP: code = 0x62; break;
	 case SDLK_PRINT: code = 0x62; break;
	 case SDLK_UNDO: code = 0x61; break;
	 default: code = -1;
	}

	return code;
}


#if WITH_SDL2

/**
 * Remap SDL scancode key to ST Scan code - this is the version for SDL2
 */
static char Keymap_PcToStScanCode(SDL_keysym* pKeySym)
{
	switch (pKeySym->scancode)
	{
	 case SDL_SCANCODE_A: return 0x1e;
	 case SDL_SCANCODE_B: return 0x30;
	 case SDL_SCANCODE_C: return 0x2e;
	 case SDL_SCANCODE_D: return 0x20;
	 case SDL_SCANCODE_E: return 0x12;
	 case SDL_SCANCODE_F: return 0x21;
	 case SDL_SCANCODE_G: return 0x22;
	 case SDL_SCANCODE_H: return 0x23;
	 case SDL_SCANCODE_I: return 0x17;
	 case SDL_SCANCODE_J: return 0x24;
	 case SDL_SCANCODE_K: return 0x25;
	 case SDL_SCANCODE_L: return 0x26;
	 case SDL_SCANCODE_M: return 0x32;
	 case SDL_SCANCODE_N: return 0x31;
	 case SDL_SCANCODE_O: return 0x18;
	 case SDL_SCANCODE_P: return 0x19;
	 case SDL_SCANCODE_Q: return 0x10;
	 case SDL_SCANCODE_R: return 0x13;
	 case SDL_SCANCODE_S: return 0x1f;
	 case SDL_SCANCODE_T: return 0x14;
	 case SDL_SCANCODE_U: return 0x16;
	 case SDL_SCANCODE_V: return 0x2f;
	 case SDL_SCANCODE_W: return 0x11;
	 case SDL_SCANCODE_X: return 0x2d;
	 case SDL_SCANCODE_Y: return 0x15;
	 case SDL_SCANCODE_Z: return 0x2c;
	 case SDL_SCANCODE_1: return 0x02;
	 case SDL_SCANCODE_2: return 0x03;
	 case SDL_SCANCODE_3: return 0x04;
	 case SDL_SCANCODE_4: return 0x05;
	 case SDL_SCANCODE_5: return 0x06;
	 case SDL_SCANCODE_6: return 0x07;
	 case SDL_SCANCODE_7: return 0x08;
	 case SDL_SCANCODE_8: return 0x09;
	 case SDL_SCANCODE_9: return 0x0a;
	 case SDL_SCANCODE_0: return 0x0b;
	 case SDL_SCANCODE_RETURN: return 0x1c;
	 case SDL_SCANCODE_ESCAPE: return 0x01;
	 case SDL_SCANCODE_BACKSPACE: return 0x0e;
	 case SDL_SCANCODE_TAB: return 0x0f;
	 case SDL_SCANCODE_SPACE: return 0x39;
	 case SDL_SCANCODE_MINUS: return 0x0c;
	 case SDL_SCANCODE_EQUALS: return 0x0d;
	 case SDL_SCANCODE_LEFTBRACKET: return 0x1a;
	 case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
	 case SDL_SCANCODE_BACKSLASH: return 0x60;
	 case SDL_SCANCODE_NONUSHASH: return 0x2b;
	 case SDL_SCANCODE_SEMICOLON: return 0x27;
	 case SDL_SCANCODE_APOSTROPHE: return 0x28;
	 case SDL_SCANCODE_GRAVE: return 0x2b;      /* ok? */
	 case SDL_SCANCODE_COMMA: return 0x33;
	 case SDL_SCANCODE_PERIOD: return 0x34;
	 case SDL_SCANCODE_SLASH: return 0x35;
	 case SDL_SCANCODE_CAPSLOCK: return 0x3a;
	 case SDL_SCANCODE_F1: return 0x3b;
	 case SDL_SCANCODE_F2: return 0x3c;
	 case SDL_SCANCODE_F3: return 0x3d;
	 case SDL_SCANCODE_F4: return 0x3e;
	 case SDL_SCANCODE_F5: return 0x3f;
	 case SDL_SCANCODE_F6: return 0x40;
	 case SDL_SCANCODE_F7: return 0x41;
	 case SDL_SCANCODE_F8: return 0x42;
	 case SDL_SCANCODE_F9: return 0x43;
	 case SDL_SCANCODE_F10: return 0x44;
	 case SDL_SCANCODE_PRINTSCREEN: return 0x62;
	 case SDL_SCANCODE_SCROLLLOCK: return 0x61;
	 case SDL_SCANCODE_PAUSE: return 0x61;
	 case SDL_SCANCODE_INSERT: return 0x52;
	 case SDL_SCANCODE_HOME: return 0x47;
	 case SDL_SCANCODE_PAGEUP: return 0x62;
	 case SDL_SCANCODE_DELETE: return 0x53;
	 case SDL_SCANCODE_END: return 0x2b;
	 case SDL_SCANCODE_PAGEDOWN: return 0x61;
	 case SDL_SCANCODE_RIGHT: return 0x4d;
	 case SDL_SCANCODE_LEFT: return 0x4b;
	 case SDL_SCANCODE_DOWN: return 0x50;
	 case SDL_SCANCODE_UP: return 0x48;
	 case SDL_SCANCODE_NUMLOCKCLEAR: return 0x64;
	 case SDL_SCANCODE_KP_DIVIDE: return 0x65;
	 case SDL_SCANCODE_KP_MULTIPLY: return 0x66;
	 case SDL_SCANCODE_KP_MINUS: return 0x4a;
	 case SDL_SCANCODE_KP_PLUS: return 0x4e;
	 case SDL_SCANCODE_KP_ENTER: return 0x72;
	 case SDL_SCANCODE_KP_1: return 0x6d;
	 case SDL_SCANCODE_KP_2: return 0x6e;
	 case SDL_SCANCODE_KP_3: return 0x6f;
	 case SDL_SCANCODE_KP_4: return 0x6a;
	 case SDL_SCANCODE_KP_5: return 0x6b;
	 case SDL_SCANCODE_KP_6: return 0x6c;
	 case SDL_SCANCODE_KP_7: return 0x67;
	 case SDL_SCANCODE_KP_8: return 0x68;
	 case SDL_SCANCODE_KP_9: return 0x69;
	 case SDL_SCANCODE_KP_0: return 0x70;
	 case SDL_SCANCODE_KP_PERIOD: return 0x71;
	 case SDL_SCANCODE_NONUSBACKSLASH: return 0x60;
	 //case SDL_SCANCODE_APPLICATION: return ;
	 case SDL_SCANCODE_KP_EQUALS: return 0x63;
	 case SDL_SCANCODE_F13: return 0x63;
	 case SDL_SCANCODE_F14: return 0x64;
	 case SDL_SCANCODE_HELP: return 0x62;
	 case SDL_SCANCODE_UNDO: return 0x61;
	 case SDL_SCANCODE_KP_COMMA: return 0x71;
	 case SDL_SCANCODE_CLEAR: return 0x47;
	 case SDL_SCANCODE_RETURN2: return 0x1c;
	 case SDL_SCANCODE_KP_LEFTPAREN: return 0x63;
	 case SDL_SCANCODE_KP_RIGHTPAREN: return 0x64;
	 case SDL_SCANCODE_KP_LEFTBRACE: return 0x63;
	 case SDL_SCANCODE_KP_RIGHTBRACE: return 0x64;
	 case SDL_SCANCODE_KP_TAB: return 0x0f;
	 case SDL_SCANCODE_KP_BACKSPACE: return 0x0e;
	 case SDL_SCANCODE_KP_COLON: return 0x33;
	 case SDL_SCANCODE_KP_HASH: return 0x0c;
	 case SDL_SCANCODE_KP_SPACE: return 0x39;
	 case SDL_SCANCODE_KP_CLEAR: return 0x47;
	 case SDL_SCANCODE_LCTRL: return 0x1d;
	 case SDL_SCANCODE_LSHIFT: return 0x2a;
	 case SDL_SCANCODE_LALT: return 0x38;
	 case SDL_SCANCODE_RCTRL: return 0x1d;
	 case SDL_SCANCODE_RSHIFT: return 0x36;
	 default:
		fprintf(stderr, "Warning: Unhandled scancode 0x%x!\n", pKeySym->scancode);
		return -1;
	}
}

#else

/**
 * Heuristic analysis to find out the obscure scancode offset.
 * Some keys like 'z' can't be used for detection since they are on different
 * locations on "qwertz" and "azerty" keyboards.
 * This clever code has originally been taken from the emulator Aranym. (cheers!)
 */
static int Keymap_FindScanCodeOffset(SDL_keysym* keysym)
{
	int offset = -1;    /* uninitialized scancode offset */
	int scanPC = keysym->scancode;

	if (scanPC == 0)  return -1; /* Ignore illegal scancode */

	switch (keysym->sym)
	{
	 case SDLK_ESCAPE: offset = scanPC - 0x01; break;
	 case SDLK_1:  offset = scanPC - 0x02; break;
	 case SDLK_2:  offset = scanPC - 0x03; break;
	 case SDLK_3:  offset = scanPC - 0x04; break;
	 case SDLK_4:  offset = scanPC - 0x05; break;
	 case SDLK_5:  offset = scanPC - 0x06; break;
	 case SDLK_6:  offset = scanPC - 0x07; break;
	 case SDLK_7:  offset = scanPC - 0x08; break;
	 case SDLK_8:  offset = scanPC - 0x09; break;
	 case SDLK_9:  offset = scanPC - 0x0a; break;
	 case SDLK_0:  offset = scanPC - 0x0b; break;
	 case SDLK_BACKSPACE: offset = scanPC - 0x0e; break;
	 case SDLK_TAB:    offset = scanPC - 0x0f; break;
	 case SDLK_RETURN: offset = scanPC - 0x1c; break;
	 case SDLK_SPACE:  offset = scanPC - 0x39; break;
	 /*case SDLK_q:  offset = scanPC - 0x10; break;*/  /* different on azerty */
	 /*case SDLK_w:  offset = scanPC - 0x11; break;*/  /* different on azerty */
	 case SDLK_e:  offset = scanPC - 0x12; break;
	 case SDLK_r:  offset = scanPC - 0x13; break;
	 case SDLK_t:  offset = scanPC - 0x14; break;
	 /*case SDLK_y:  offset = scanPC - 0x15; break;*/  /* different on qwertz */
	 case SDLK_u:  offset = scanPC - 0x16; break;
	 case SDLK_i:  offset = scanPC - 0x17; break;
	 case SDLK_o:  offset = scanPC - 0x18; break;
	 case SDLK_p:  offset = scanPC - 0x19; break;
	 /*case SDLK_a:  offset = scanPC - 0x1e; break;*/  /* different on azerty */
	 case SDLK_s:  offset = scanPC - 0x1f; break;
	 case SDLK_d:  offset = scanPC - 0x20; break;
	 case SDLK_f:  offset = scanPC - 0x21; break;
	 case SDLK_g:  offset = scanPC - 0x22; break;
	 case SDLK_h:  offset = scanPC - 0x23; break;
	 case SDLK_j:  offset = scanPC - 0x24; break;
	 case SDLK_k:  offset = scanPC - 0x25; break;
	 case SDLK_l:  offset = scanPC - 0x26; break;
	 /*case SDLK_z:  offset = scanPC - 0x2c; break;*/  /* different on qwertz and azerty */
	 case SDLK_x:  offset = scanPC - 0x2d; break;
	 case SDLK_c:  offset = scanPC - 0x2e; break;
	 case SDLK_v:  offset = scanPC - 0x2f; break;
	 case SDLK_b:  offset = scanPC - 0x30; break;
	 case SDLK_n:  offset = scanPC - 0x31; break;
	 /*case SDLK_m:  offset = scanPC - 0x32; break;*/  /* different on azerty */
	 case SDLK_CAPSLOCK:  offset = scanPC - 0x3a; break;
	 case SDLK_LSHIFT: offset = scanPC - 0x2a; break;
	 case SDLK_LCTRL: offset = scanPC - 0x1d; break;
	 case SDLK_LALT: offset = scanPC - 0x38; break;
	 case SDLK_F1:  offset = scanPC - 0x3b; break;
	 case SDLK_F2:  offset = scanPC - 0x3c; break;
	 case SDLK_F3:  offset = scanPC - 0x3d; break;
	 case SDLK_F4:  offset = scanPC - 0x3e; break;
	 case SDLK_F5:  offset = scanPC - 0x3f; break;
	 case SDLK_F6:  offset = scanPC - 0x40; break;
	 case SDLK_F7:  offset = scanPC - 0x41; break;
	 case SDLK_F8:  offset = scanPC - 0x42; break;
	 case SDLK_F9:  offset = scanPC - 0x43; break;
	 case SDLK_F10: offset = scanPC - 0x44; break;
	 default:  break;
	}

	if (offset != -1)
	{
		fprintf(stderr, "Detected scancode offset = %d (key: '%s' with scancode $%02x)\n",
		        offset, SDL_GetKeyName(keysym->sym), scanPC);
	}

	return offset;
}


/**
 * Map PC scancode to ST scancode.
 * This code was heavily inspired by the emulator Aranym. (cheers!)
 */
static char Keymap_PcToStScanCode(SDL_keysym* keysym)
{
	static int offset = -1;    /* uninitialized scancode offset */

	/* We sometimes enter here with an illegal (=0) scancode, so we keep
	 * track of the right scancodes in a table and then use a value from there.
	 */
	if (keysym->scancode != 0)
	{
		SdlSymToSdlScan[keysym->sym] = keysym->scancode;
	}
	else
	{
		keysym->scancode = SdlSymToSdlScan[keysym->sym];
		if (keysym->scancode == 0)
			fprintf(stderr, "Warning: Key scancode is 0!\n");
	}

	switch (keysym->sym)
	{
	 /* Numeric Pad */
	 /* note that the numbers are handled in Keymap_GetKeyPadScanCode()! */
	 case SDLK_KP_DIVIDE:   return 0x65;  /* Numpad / */
	 case SDLK_KP_MULTIPLY: return 0x66;  /* NumPad * */
	 case SDLK_KP_MINUS:    return 0x4a;  /* NumPad - */
	 case SDLK_KP_PLUS:     return 0x4e;  /* NumPad + */
	 case SDLK_KP_PERIOD:   return 0x71;  /* NumPad . */
	 case SDLK_KP_ENTER:    return 0x72;  /* NumPad Enter */

	 /* Special Keys */
	 case SDLK_PAGEUP:   return 0x62;  /* F11 => Help */
	 case SDLK_PAGEDOWN: return 0x61;  /* F12 => Undo */
	 case SDLK_HOME:     return 0x47;  /* Home */
	 case SDLK_END:      return 0x60;  /* End => "<>" on German Atari kbd */
	 case SDLK_UP:       return 0x48;  /* Arrow Up */
	 case SDLK_LEFT:     return 0x4b;  /* Arrow Left */
	 case SDLK_RIGHT:    return 0x4d;  /* Arrow Right */
	 case SDLK_DOWN:     return 0x50;  /* Arrow Down */
	 case SDLK_INSERT:   return 0x52;  /* Insert */
	 case SDLK_DELETE:   return 0x53;  /* Delete */
	 case SDLK_LESS:     return 0x60;  /* "<" */

	 /* Map Right Alt/Alt Gr/Control to the Atari keys */
	 case SDLK_RCTRL:  return 0x1d;  /* Control */
	 case SDLK_RALT:   return 0x38;  /* Alternate */

	 default:
		/* Process remaining keys: assume that it's PC101 keyboard
		 * and that it is compatible with Atari ST keyboard (basically
		 * same scancodes but on different platforms with different
		 * base offset (framebuffer = 0, X11 = 8).
		 * Try to detect the offset using a little bit of black magic.
		 * If offset is known then simply pass the scancode. */
		if (offset == -1)
		{
			offset = Keymap_FindScanCodeOffset(keysym);
		}

		if (offset >= 0)
		{
			/* offset is defined so pass the scancode directly */
			return (keysym->scancode - offset);
		}
		else
		{
			/* Failed to detect offset, so use default value 8 */
			fprintf(stderr, "Offset detection failed with "
			        "key '%s', scancode = 0x%02x, symcode =  0x%02x\n",
			        SDL_GetKeyName(keysym->sym), keysym->scancode, keysym->sym);
			return (keysym->scancode - 8);
		}
	 	break;
	}
}

#endif	/* WITH_SDL2 */


/**
 * Remap a keypad key to ST scan code. We use a separate function for this
 * so that we can easily toggle between number and cursor mode with the
 * numlock key.
 */
static char Keymap_GetKeyPadScanCode(SDL_keysym* pKeySym)
{
	if (SDL_GetModState() & KMOD_NUM)
	{
		switch (pKeySym->sym)
		{
		 case SDLK_KP0:  return 0x70;  /* NumPad 0 */
		 case SDLK_KP1:  return 0x6d;  /* NumPad 1 */
		 case SDLK_KP2:  return 0x6e;  /* NumPad 2 */
		 case SDLK_KP3:  return 0x6f;  /* NumPad 3 */
		 case SDLK_KP4:  return 0x6a;  /* NumPad 4 */
		 case SDLK_KP5:  return 0x6b;  /* NumPad 5 */
		 case SDLK_KP6:  return 0x6c;  /* NumPad 6 */
		 case SDLK_KP7:  return 0x67;  /* NumPad 7 */
		 case SDLK_KP8:  return 0x68;  /* NumPad 8 */
		 case SDLK_KP9:  return 0x69;  /* NumPad 9 */
		 default:  break;
		}
	}
	else
	{
		switch (pKeySym->sym)
		{
		 case SDLK_KP0:  return 0x70;  /* NumPad 0 */
		 case SDLK_KP1:  return 0x6d;  /* NumPad 1 */
		 case SDLK_KP2:  return 0x50;  /* Cursor down */
		 case SDLK_KP3:  return 0x6f;  /* NumPad 3 */
		 case SDLK_KP4:  return 0x4b;  /* Cursor left */
		 case SDLK_KP5:  return 0x50;  /* Cursor down (again?) */
		 case SDLK_KP6:  return 0x4d;  /* Cursor right */
		 case SDLK_KP7:  return 0x52;  /* Insert - good for Dungeon Master */
		 case SDLK_KP8:  return 0x48;  /* Cursor up */
		 case SDLK_KP9:  return 0x47;  /* Home - again for Dungeon Master */
		 default:  break;
		}
	}

	return -1;
}


/**
 * Remap SDL Key to ST Scan code
 */
static char Keymap_RemapKeyToSTScanCode(SDL_keysym* pKeySym)
{
	/* Check for keypad first so we can handle numlock */
	if (ConfigureParams.Keyboard.nKeymapType != KEYMAP_LOADED)
	{
		if (pKeySym->sym >= SDLK_KP0 && pKeySym->sym <= SDLK_KP9)
		{
			return Keymap_GetKeyPadScanCode(pKeySym);
		}
	}

	/* Remap from PC scancodes? */
	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_SCANCODE)
	{
		return Keymap_PcToStScanCode(pKeySym);
	}

	/* Use loaded keymap? */
	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_LOADED)
	{
		int i;
		for (i = 0; i < KBD_MAX_SCANCODE && LoadedKeymap[i][1] != 0; i++)
		{
			if (pKeySym->sym == (SDLKey)LoadedKeymap[i][0])
				return LoadedKeymap[i][1];
		}
	}

	/* Use symbolic mapping */
	return Keymap_SymbolicToStScanCode(pKeySym);
}


/*-----------------------------------------------------------------------*/
/**
 * Load keyboard remap file
 */
void Keymap_LoadRemapFile(char *pszFileName)
{
	char szString[1024];
	int STScanCode, PCKeyCode;
	FILE *in;
	int idx = 0;

	/* Initialize table with default values */
	memset(LoadedKeymap, 0, sizeof(LoadedKeymap));

	if (!*pszFileName)
		return;
	
	/* Attempt to load file */
	if (!File_Exists(pszFileName))
	{
		Log_Printf(LOG_DEBUG, "Keymap_LoadRemapFile: '%s' not a file\n", pszFileName);
		return;
	}
	in = fopen(pszFileName, "r");
	if (!in)
	{
		Log_Printf(LOG_ERROR, "Keymap_LoadRemapFile: failed to "
			   " open keymap file '%s'\n", pszFileName);
		return;
	}
	
	while (!feof(in) && idx < KBD_MAX_SCANCODE)
	{
		/* Read line from file */
		if (fgets(szString, sizeof(szString), in) == NULL)
			break;
		/* Remove white-space from start of line */
		Str_Trim(szString);
		if (strlen(szString)>0)
		{
			char *p;
			/* Is a comment? */
			if (szString[0] == ';' || szString[0] == '#')
				continue;
			/* Cut out the values */
			p = strtok(szString, ",");
			if (!p)
				continue;
			Str_Trim(szString);
			PCKeyCode = atoi(szString);    /* Direct key code? */
			if (PCKeyCode < 10)
			{
				/* If it's not a valid number >= 10, then
				 * assume we've got a symbolic key name */
				PCKeyCode = Keymap_GetKeyFromName(szString);
			}
			p = strtok(NULL, "\n");
			if (!p)
				continue;
			STScanCode = atoi(p);
			/* Store into remap table, check both value within range */
			if (STScanCode > 0 && STScanCode <= KBD_MAX_SCANCODE
			    && PCKeyCode >= 8)
			{
				LOG_TRACE(TRACE_KEYMAP,
				          "keymap from file: sym=%i --> scan=%i\n",
				          PCKeyCode, STScanCode);
				LoadedKeymap[idx][0] = PCKeyCode;
				LoadedKeymap[idx][1] = STScanCode;
				idx += 1;
			}
			else
			{
				Log_Printf(LOG_WARN, "Could not parse keymap file:"
				           " '%s', '%s'\n", szString, p);
			}
		}
	}

	fclose(in);
}


/*-----------------------------------------------------------------------*/
/**
 * Scan list of keys to NOT de-bounce when running in maximum speed, eg ALT,SHIFT,CTRL etc...
 * @return true if key requires de-bouncing
 */
static bool Keymap_DebounceSTKey(char STScanCode)
{
	int i=0;

	/* Are we in fast forward, and have disabled key repeat? */
	if ((ConfigureParams.System.bFastForward == true)
	    && (ConfigureParams.Keyboard.bDisableKeyRepeat))
	{
		/* We should de-bounce all non extended keys,
		 * e.g. leave ALT, SHIFT, CTRL etc... held */
		while (DebounceExtendedKeys[i])
		{
			if (STScanCode == DebounceExtendedKeys[i])
				return false;
			i++;
		}

		/* De-bounce key */
		return true;
	}

	/* Do not de-bounce key */
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Debounce any PC key held down if running with key repeat disabled.
 * This is called each ST frame, so keys get held down for one VBL which
 * is enough for 68000 code to scan.
 */
void Keymap_DebounceAllKeys(void)
{
	uint8_t nScanCode;

	/* Return if we aren't in fast forward or have not disabled key repeat */
	if ((ConfigureParams.System.bFastForward == false)
	        || (!ConfigureParams.Keyboard.bDisableKeyRepeat))
	{
		return;
	}

	/* Now run through each key looking for ones held down */
	for (nScanCode = 1; nScanCode <= KBD_MAX_SCANCODE; nScanCode++)
	{
		/* Is key held? */
		if (Keyboard.KeyStates[nScanCode])
		{
			/* Does this require de-bouncing? */
			if (Keymap_DebounceSTKey(nScanCode))
			{
				IKBD_PressSTKey(nScanCode, false);
				Keyboard.KeyStates[nScanCode] = false;
			}
		}
	}

}


/*-----------------------------------------------------------------------*/
/**
 * User press key down
 */
void Keymap_KeyDown(SDL_keysym *sdlkey)
{
	uint8_t STScanCode;
	int symkey = sdlkey->sym;
	int modkey = sdlkey->mod;

	LOG_TRACE(TRACE_KEYMAP, "key down: sym=%i scan=%i mod=0x%x\n", symkey,
	          sdlkey->scancode, modkey);

	if (ShortCut_CheckKeys(modkey, symkey, 1))
		return;

	/* If using joystick emulation via keyboard, DON'T send keys to keyboard processor!!!
	 * Some games use keyboard as pause! */
	if (Joy_KeyDown(symkey, modkey))
		return;

	/* Handle special keys */
	if (symkey == SDLK_RALT || symkey == SDLK_LMETA || symkey == SDLK_RMETA
	        || symkey == SDLK_MODE || symkey == SDLK_NUMLOCK)
	{
		/* Ignore modifier keys that aren't passed to the ST */
		return;
	}

	STScanCode = Keymap_RemapKeyToSTScanCode(sdlkey);
	LOG_TRACE(TRACE_KEYMAP, "key map: sym=0x%x to ST-scan=0x%02x\n", symkey, STScanCode);
	if (STScanCode != (uint8_t)-1)
	{
		if (!Keyboard.KeyStates[STScanCode])
		{
			/* Set down */
			Keyboard.KeyStates[STScanCode] = true;
			IKBD_PressSTKey(STScanCode, true);
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * User released key
 */
void Keymap_KeyUp(SDL_keysym *sdlkey)
{
	uint8_t STScanCode;
	int symkey = sdlkey->sym;
	int modkey = sdlkey->mod;

	LOG_TRACE(TRACE_KEYMAP, "key up: sym=%i scan=%i mod=$%x\n", symkey,
	          sdlkey->scancode, modkey);

	/* Ignore short-cut keys here */
	if (ShortCut_CheckKeys(modkey, symkey, 0))
		return;

	/* If using keyboard emulation, DON'T send keys to keyboard processor!!!
	 * Some games use keyboard as pause! */
	if (Joy_KeyUp(symkey, modkey))
		return;

	/* Handle special keys */
	if (symkey == SDLK_RALT || symkey == SDLK_LMETA || symkey == SDLK_RMETA
	        || symkey == SDLK_MODE || symkey == SDLK_NUMLOCK)
	{
		/* Ignore modifier keys that aren't passed to the ST */
		return;
	}

	STScanCode = Keymap_RemapKeyToSTScanCode(sdlkey);
	/* Release key (only if was pressed) */
	if (STScanCode != (uint8_t)-1)
	{
		if (Keyboard.KeyStates[STScanCode])
		{
			IKBD_PressSTKey(STScanCode, false);
			Keyboard.KeyStates[STScanCode] = false;
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Simulate press or release of a key corresponding to given character
 */
void Keymap_SimulateCharacter(char asckey, bool press)
{
	SDL_keysym sdlkey;

	sdlkey.mod = KMOD_NONE;
	sdlkey.scancode = 0;
	if (isupper((unsigned char)asckey)) {
		if (press) {
			sdlkey.sym = SDLK_LSHIFT;
			Keymap_KeyDown(&sdlkey);
		}
		sdlkey.sym = tolower((unsigned char)asckey);
		sdlkey.mod = KMOD_LSHIFT;
	} else {
		sdlkey.sym = asckey;
	}
	if (press) {
		Keymap_KeyDown(&sdlkey);
	} else {
		Keymap_KeyUp(&sdlkey);
		if (isupper((unsigned char)asckey)) {
			sdlkey.sym = SDLK_LSHIFT;
			Keymap_KeyUp(&sdlkey);
		}
	}
}


#if WITH_SDL2

int Keymap_GetKeyFromName(const char *name)
{
	return SDL_GetKeyFromName(name);
}

const char *Keymap_GetKeyName(int keycode)
{
	if (!keycode)
		return "";

	return SDL_GetKeyName(keycode);
}

#else	/* WITH_SDL2 */

static struct {
	int code;
	const char *name;
} const sdl_keytab[] = {
	{ SDLK_BACKSPACE, "Backspace" },
	{ SDLK_TAB, "Tab" },
	{ SDLK_CLEAR, "Clear" },
	{ SDLK_RETURN, "Return" },
	{ SDLK_PAUSE, "Pause" },
	{ SDLK_ESCAPE, "Escape" },
	{ SDLK_SPACE, "Space" },
	{ SDLK_EXCLAIM, "!" },
	{ SDLK_QUOTEDBL, "\"" },
	{ SDLK_HASH, "#" },
	{ SDLK_DOLLAR, "$" },
	{ SDLK_AMPERSAND, "&" },
	{ SDLK_QUOTE, "'" },
	{ SDLK_LEFTPAREN, "(" },
	{ SDLK_RIGHTPAREN, ")" },
	{ SDLK_ASTERISK, "*" },
	{ SDLK_PLUS, "+" },
	{ SDLK_COMMA, "," },
	{ SDLK_MINUS, "-" },
	{ SDLK_PERIOD, "." },
	{ SDLK_SLASH, "/" },
	{ SDLK_0, "0" },
	{ SDLK_1, "1" },
	{ SDLK_2, "2" },
	{ SDLK_3, "3" },
	{ SDLK_4, "4" },
	{ SDLK_5, "5" },
	{ SDLK_6, "6" },
	{ SDLK_7, "7" },
	{ SDLK_8, "8" },
	{ SDLK_9, "9" },
	{ SDLK_COLON, ":" },
	{ SDLK_SEMICOLON, ";" },
	{ SDLK_LESS, "<" },
	{ SDLK_EQUALS, "=" },
	{ SDLK_GREATER, ">" },
	{ SDLK_QUESTION, "?" },
	{ SDLK_AT, "@" },
	{ SDLK_LEFTBRACKET, "[" },
	{ SDLK_BACKSLASH, "\\" },
	{ SDLK_RIGHTBRACKET, "]" },
	{ SDLK_CARET, "^" },
	{ SDLK_UNDERSCORE, "_" },
	{ SDLK_BACKQUOTE, "`" },
	{ SDLK_a, "A" },
	{ SDLK_b, "B" },
	{ SDLK_c, "C" },
	{ SDLK_d, "D" },
	{ SDLK_e, "E" },
	{ SDLK_f, "F" },
	{ SDLK_g, "G" },
	{ SDLK_h, "H" },
	{ SDLK_i, "I" },
	{ SDLK_j, "J" },
	{ SDLK_k, "K" },
	{ SDLK_l, "L" },
	{ SDLK_m, "M" },
	{ SDLK_n, "N" },
	{ SDLK_o, "O" },
	{ SDLK_p, "P" },
	{ SDLK_q, "Q" },
	{ SDLK_r, "R" },
	{ SDLK_s, "S" },
	{ SDLK_t, "T" },
	{ SDLK_u, "U" },
	{ SDLK_v, "V" },
	{ SDLK_w, "W" },
	{ SDLK_x, "X" },
	{ SDLK_y, "Y" },
	{ SDLK_z, "Z" },
	{ SDLK_DELETE, "Delete" },
	{ SDLK_KP0, "Keypad 0" },
	{ SDLK_KP1, "Keypad 1" },
	{ SDLK_KP2, "Keypad 2" },
	{ SDLK_KP3, "Keypad 3" },
	{ SDLK_KP4, "Keypad 4" },
	{ SDLK_KP5, "Keypad 5" },
	{ SDLK_KP6, "Keypad 6" },
	{ SDLK_KP7, "Keypad 7" },
	{ SDLK_KP8, "Keypad 8" },
	{ SDLK_KP9, "Keypad 9" },
	{ SDLK_KP_PERIOD, "Keypad ." },
	{ SDLK_KP_DIVIDE, "Keypad /" },
	{ SDLK_KP_MULTIPLY, "Keypad *" },
	{ SDLK_KP_MINUS, "Keypad -" },
	{ SDLK_KP_PLUS, "Keypad +" },
	{ SDLK_KP_ENTER, "Keypad Enter" },
	{ SDLK_KP_EQUALS, "Keypad =" },
	{ SDLK_UP, "Up" },
	{ SDLK_DOWN, "Down" },
	{ SDLK_RIGHT, "Right" },
	{ SDLK_LEFT, "Left" },
	{ SDLK_INSERT, "Insert" },
	{ SDLK_HOME, "Home" },
	{ SDLK_END, "End" },
	{ SDLK_PAGEUP, "PageUp" },
	{ SDLK_PAGEDOWN, "PageDown" },
	{ SDLK_F1, "F1" },
	{ SDLK_F2, "F2" },
	{ SDLK_F3, "F3" },
	{ SDLK_F4, "F4" },
	{ SDLK_F5, "F5" },
	{ SDLK_F6, "F6" },
	{ SDLK_F7, "F7" },
	{ SDLK_F8, "F8" },
	{ SDLK_F9, "F9" },
	{ SDLK_F10, "F10" },
	{ SDLK_F11, "F11" },
	{ SDLK_F12, "F12" },
	{ SDLK_F13, "F13" },
	{ SDLK_F14, "F14" },
	{ SDLK_F15, "F15" },
	{ SDLK_NUMLOCK, "Numlock" },
	{ SDLK_CAPSLOCK, "CapsLock" },
	{ SDLK_SCROLLOCK, "ScrollLock" },
	{ SDLK_RSHIFT, "Right Shift" },
	{ SDLK_LSHIFT, "Left Shift" },
	{ SDLK_RCTRL, "Right Ctrl" },
	{ SDLK_LCTRL, "Left Ctrl" },
	{ SDLK_RALT, "Right Alt" },
	{ SDLK_LALT, "Left Alt" },
	{ SDLK_RMETA, "Right GUI" },
	{ SDLK_LMETA, "Left GUI" },
	{ SDLK_MODE, "ModeSwitch" },
	{ SDLK_HELP, "Help" },
	{ SDLK_PRINT, "PrintScreen" },
	{ SDLK_SYSREQ, "SysReq" },
	{ SDLK_BREAK, "Cancel" },
	{ SDLK_MENU, "Menu" },
	{ SDLK_POWER, "Power" },
	{ SDLK_UNDO, "Undo" },

	{ -1, NULL }
};

int Keymap_GetKeyFromName(const char *name)
{
	int i;

	if (!name[0])
		return 0;

	for (i = 0; sdl_keytab[i].name != NULL; i++)
	{
		if (strcasecmp(name, sdl_keytab[i].name) == 0)
			return sdl_keytab[i].code;
	}

	return 0;
}

const char *Keymap_GetKeyName(int keycode)
{
	int i;

	if (!keycode)
		return "";

	for (i = 0; sdl_keytab[i].name != NULL; i++)
	{
		if (keycode == sdl_keytab[i].code)
			return sdl_keytab[i].name;
	}

	return "";
}

#endif
