/*
  Hatari - keymap.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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


/*-----------------------------------------------------------------------*/
/*
  Remap table of PC keys to ST scan codes, -ve is invalid key (ie doesn't occur on ST)

  PC Keyboard:-

    Esc  F1  F2  F3  F4    F5  F6  F7  F8    F9  F10 F11 F12       Print Scroll Pause

     1   59  60  61  62    63  64  65  66    67  68  87  88                70     69


   !   "       $   %   ^   &   *   (   )   _   +                                 Page
`  1   2   3   4   5   6   7   8   9   0   -   =   <-               Ins   Home    Up

41 2   3   4   5   6   7   8   9   10  11  12  13  14               82     71     73
                                                                    --     --     --
                                                       |
                                             {   }     |                         Page
Tab  Q   W   E   R   T   Y   U   I   O   P   [   ] <----            Del   End    Down

15   16  17  18  19  20  21  22  23  24  25  26  27  28             83     79     81
                                                                    --     --     --

                                           :   @   ~                       ^
Caps   A   S   D   F   G   H   J   K   L   ;   '   #                       |

58     30  31  32  33  34  35  36  37  38  39  40  43                      72
                                                                           --

^   |                               <   >   ?   ^
|   \   Z   X   C   V   B   N   M   ,   .   /   |                     <-   |   ->

42  86  44  45  46  47  48  49  50  51  52  53  54                    75   80  77
                                                                      --   --  --

Ctrl  Alt          SPACE             Alt Gr      Ctrl

 29    56            57                56         29
                                                  --

  And:-

Num
Lock   /    *    -

69     53   55   74
--     --

 7    8     9    +
Home  ^   Pg Up

71    72    73   78


4     5     6
<-          ->

75    76    77


1     2     3
End   |   Pg Dn  Enter

79    70    81    28
                  --

0     .
Ins   Del

82    83


*/


/* SDL symbolic key to ST scan code mapping table */
static const char SymbolicKeyToSTScanCode[SDLK_LAST] =
{
	/* ST,  PC Code */
	-1,    /* 0 */
	-1,    /* 1 */
	-1,    /* 2 */
	-1,    /* 3 */
	-1,    /* 4 */
	-1,    /* 5 */
	-1,    /* 6 */
	-1,    /* 7 */
	0x0E,  /* SDLK_BACKSPACE=8 */
	0x0F,  /* SDLK_TAB=9 */
	-1,    /* 10 */
	-1,    /* 11 */
	0x47,  /* SDLK_CLEAR = 12 */
	0x1C,  /* SDLK_RETURN = 13 */
	-1,    /* 14 */
	-1,    /* 15 */
	-1,    /* 16 */
	-1,    /* 17 */
	-1,    /* 18 */
	-1,    /* SDLK_PAUSE = 19 */
	-1,    /* 20 */
	-1,    /* 21 */
	-1,    /* 22 */
	-1,    /* 23 */
	-1,    /* 24 */
	-1,    /* 25 */
	-1,    /* 26 */
	0x01,  /* SDLK_ESCAPE = 27 */
	-1,    /* 28 */
	-1,    /* 29 */
	-1,    /* 30 */
	-1,    /* 31 */
	0x39,  /* SDLK_SPACE = 32 */
	-1,    /* SDLK_EXCLAIM = 33 */
	-1,    /* SDLK_QUOTEDBL = 34 */
	0x29,  /* SDLK_HASH = 35 */
	-1,    /* SDLK_DOLLAR = 36 */
	-1,    /* 37 */
	-1,    /* SDLK_AMPERSAND = 38 */
	0x28,  /* SDLK_QUOTE = 39 */
	0x63,  /* SDLK_LEFTPAREN = 40 */
	0x64,  /* SDLK_RIGHTPAREN = 41 */
	-1,    /* SDLK_ASTERISK = 42 */
	0x1B,  /* SDLK_PLUS = 43 */
	0x33,  /* SDLK_COMMA = 44 */
	0x35,  /* SDLK_MINUS = 45 */
	0x34,  /* SDLK_PERIOD = 46 */
	0x35,  /* SDLK_SLASH = 47 */
	0x0B,  /* SDLK_0 = 48 */
	0x02,  /* SDLK_1 = 49 */
	0x03,  /* SDLK_2 = 50 */
	0x04,  /* SDLK_3 = 51 */
	0x05,  /* SDLK_4 = 52 */
	0x06,  /* SDLK_5 = 53 */
	0x07,  /* SDLK_6 = 54 */
	0x08,  /* SDLK_7 = 55 */
	0x09,  /* SDLK_8 = 56 */
	0x0A,  /* SDLK_9 = 57 */
	-1,    /* SDLK_COLON = 58 */
	0x27,  /* SDLK_SEMICOLON = 59 */
	0x60,  /* SDLK_LESS = 60 */
	0x0D,  /* SDLK_EQUALS = 61 */
	-1,    /* SDLK_GREATER  = 62 */
	-1,    /* SDLK_QUESTION = 63 */
	-1,    /* SDLK_AT = 64 */
	-1,    /* 65 */  /* Skip uppercase letters */
	-1,    /* 66 */
	-1,    /* 67 */
	-1,    /* 68 */
	-1,    /* 69 */
	-1,    /* 70 */
	-1,    /* 71 */
	-1,    /* 72 */
	-1,    /* 73 */
	-1,    /* 74 */
	-1,    /* 75 */
	-1,    /* 76 */
	-1,    /* 77 */
	-1,    /* 78 */
	-1,    /* 79 */
	-1,    /* 80 */
	-1,    /* 81 */
	-1,    /* 82 */
	-1,    /* 83 */
	-1,    /* 84 */
	-1,    /* 85 */
	-1,    /* 86 */
	-1,    /* 87 */
	-1,    /* 88 */
	-1,    /* 89 */
	-1,    /* 90 */
	0x63,  /* SDLK_LEFTBRACKET = 91 */
	0x2B,  /* SDLK_BACKSLASH = 92 */     /* Might be 0x60 for UK keyboards */
	0x64,  /* SDLK_RIGHTBRACKET = 93 */
	0x2B,  /* SDLK_CARET = 94 */
	-1,    /* SDLK_UNDERSCORE = 95 */
	0x52,  /* SDLK_BACKQUOTE = 96 */
	0x1E,  /* SDLK_a = 97 */
	0x30,  /* SDLK_b = 98 */
	0x2E,  /* SDLK_c = 99 */
	0x20,  /* SDLK_d = 100 */
	0x12,  /* SDLK_e = 101 */
	0x21,  /* SDLK_f = 102 */
	0x22,  /* SDLK_g = 103 */
	0x23,  /* SDLK_h = 104 */
	0x17,  /* SDLK_i = 105 */
	0x24,  /* SDLK_j = 106 */
	0x25,  /* SDLK_k = 107 */
	0x26,  /* SDLK_l = 108 */
	0x32,  /* SDLK_m = 109 */
	0x31,  /* SDLK_n = 110 */
	0x18,  /* SDLK_o = 111 */
	0x19,  /* SDLK_p = 112 */
	0x10,  /* SDLK_q = 113 */
	0x13,  /* SDLK_r = 114 */
	0x1F,  /* SDLK_s = 115 */
	0x14,  /* SDLK_t = 116 */
	0x16,  /* SDLK_u = 117 */
	0x2F,  /* SDLK_v = 118 */
	0x11,  /* SDLK_w = 119 */
	0x2D,  /* SDLK_x = 120 */
	0x15,  /* SDLK_y = 121 */
	0x2C,  /* SDLK_z = 122 */
	-1,    /* 123 */
	-1,    /* 124 */
	-1,    /* 125 */
	-1,    /* 126 */
	0x53,  /* SDLK_DELETE = 127 */
	/* End of ASCII mapped keysyms */
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 128-143*/
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 144-159*/
	0x0d, 0x0c, 0x1a, 0x28, 0x27, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 160-175*/
	-1, -1, -1, -1, 0x0D, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 176-191*/
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192-207*/
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0x0C, /* 208-223*/
	-1, -1, -1, -1, 0x28, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 224-239*/
	-1, -1, -1, -1, -1, -1, 0x27, -1, -1, -1, -1, -1, 0x1A, -1, -1, -1, /* 240-255*/
	/* Numeric keypad: */
	0x70,    /* SDLK_KP0 = 256 */
	0x6D,    /* SDLK_KP1 = 257 */
	0x6E,    /* SDLK_KP2 = 258 */
	0x6F,    /* SDLK_KP3 = 259 */
	0x6A,    /* SDLK_KP4 = 260 */
	0x6B,    /* SDLK_KP5 = 261 */
	0x6C,    /* SDLK_KP6 = 262 */
	0x67,    /* SDLK_KP7 = 263 */
	0x68,    /* SDLK_KP8 = 264 */
	0x69,    /* SDLK_KP9 = 265 */
	0x71,    /* SDLK_KP_PERIOD = 266 */
	0x65,    /* SDLK_KP_DIVIDE = 267 */
	0x66,    /* SDLK_KP_MULTIPLY = 268 */
	0x4A,    /* SDLK_KP_MINUS = 269 */
	0x4E,    /* SDLK_KP_PLUS = 270 */
	0x72,    /* SDLK_KP_ENTER = 271 */
	0x61,    /* SDLK_KP_EQUALS = 272 */
	/* Arrows + Home/End pad */
	0x48,    /* SDLK_UP = 273 */
	0x50,    /* SDLK_DOWN = 274 */
	0x4D,    /* SDLK_RIGHT = 275 */
	0x4B,    /* SDLK_LEFT = 276 */
	0x52,    /* SDLK_INSERT = 277 */
	0x47,    /* SDLK_HOME = 278 */
	0x61,    /* SDLK_END = 279 */
	0x63,    /* SDLK_PAGEUP = 280 */
	0x64,    /* SDLK_PAGEDOWN = 281 */
	/* Function keys */
	0x3B,    /* SDLK_F1 = 282 */
	0x3C,    /* SDLK_F2 = 283 */
	0x3D,    /* SDLK_F3 = 284 */
	0x3E,    /* SDLK_F4 = 285 */
	0x3F,    /* SDLK_F5 = 286 */
	0x40,    /* SDLK_F6 = 287 */
	0x41,    /* SDLK_F7 = 288 */
	0x42,    /* SDLK_F8 = 289 */
	0x43,    /* SDLK_F9 = 290 */
	0x44,    /* SDLK_F10 = 291 */
	-1,      /* SDLK_F11 = 292 */
	-1,      /* SDLK_F12 = 293 */
	0x62,    /* SDLK_F13 = 294 */
	-1,      /* SDLK_F14 = 295 */
	-1,      /* SDLK_F15 = 296 */
	-1,      /* 297 */
	-1,      /* 298 */
	-1,      /* 299 */
	/* Key state modifier keys */
	-1,      /* SDLK_NUMLOCK = 300 */
	0x3A,    /* SDLK_CAPSLOCK = 301 */
	0x61,    /* SDLK_SCROLLOCK = 302 */
	0x36,    /* SDLK_RSHIFT = 303 */
	0x2A,    /* SDLK_LSHIFT = 304 */
	0x1D,    /* SDLK_RCTRL = 305 */
	0x1D,    /* SDLK_LCTRL = 306 */
	0x38,    /* SDLK_RALT = 307 */
	0x38,    /* SDLK_LALT = 308 */
	-1,      /* SDLK_RMETA = 309 */
	-1,      /* SDLK_LMETA = 310 */
	-1,      /* SDLK_LSUPER = 311 */
	-1,      /* SDLK_RSUPER = 312 */
	-1,      /* SDLK_MODE = 313 */     /* "Alt Gr" key */
	-1,      /* SDLK_COMPOSE = 314 */
	/* Miscellaneous function keys */
	0x62,    /* SDLK_HELP = 315 */
	0x62,    /* SDLK_PRINT = 316 */
	-1,      /* SDLK_SYSREQ = 317 */
	-1,      /* SDLK_BREAK = 318 */
	-1,      /* SDLK_MENU = 319 */
	-1,      /* SDLK_POWER = 320 */
	-1,      /* SDLK_EURO = 321 */
	0x61     /* SDLK_UNDO = 322 */
};

/* Table for loaded keys: */
static char LoadedKeyToSTScanCode[SDLK_LAST];

/* This table is used to translate a symbolic keycode to the (SDL) scancode */
static Uint8 SdlSymToSdlScan[SDLK_LAST];


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
	memset(SdlSymToSdlScan, 0, sizeof(SdlSymToSdlScan));      /* Clear array */
	Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);
}


/*-----------------------------------------------------------------------*/
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


/*-----------------------------------------------------------------------*/
/**
 * Map PC scancode to ST scancode.
 * This code was heavily inspired by the emulator Aranym. (cheers!)
 */
static char Keymap_PcToStScanCode(SDL_keysym* keysym)
{
	static int offset = -1;    /* uninitialized scancode offset */

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


/*-----------------------------------------------------------------------*/
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


/*-----------------------------------------------------------------------*/
/**
 * Remap SDL Key to ST Scan code
 */
char Keymap_RemapKeyToSTScanCode(SDL_keysym* pKeySym)
{
	if (pKeySym->sym >= SDLK_LAST)  return -1; /* Avoid illegal keys */

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
		/* We sometimes enter here with an illegal (=0) scancode, so we keep
		 * track of the right scancodes in a table and then use a value from there.
		 */
		if (pKeySym->scancode != 0)
		{
			SdlSymToSdlScan[pKeySym->sym] = pKeySym->scancode;
		}
		else
		{
			pKeySym->scancode = SdlSymToSdlScan[pKeySym->sym];
			if (pKeySym->scancode == 0)
				fprintf(stderr, "Warning: Key scancode is 0!\n");
		}

		return Keymap_PcToStScanCode(pKeySym);
	}

	/* Use default or loaded? */
	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_LOADED)
		return LoadedKeyToSTScanCode[pKeySym->sym];
	else
		return SymbolicKeyToSTScanCode[pKeySym->sym];
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

	/* Initialize table with default values */
	memcpy(LoadedKeyToSTScanCode, SymbolicKeyToSTScanCode, sizeof(LoadedKeyToSTScanCode));

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
		Log_Printf(LOG_DEBUG, "Keymap_LoadRemapFile: failed to "
			   " open keymap file '%s'\n", pszFileName);
		return;
	}
	
	while (!feof(in))
	{
		/* Read line from file */
		if (fgets(szString, sizeof(szString), in) == NULL)
			break;
		/* Remove white-space from start of line */
		Str_Trim(szString);
		if (strlen(szString)>0)
		{
			/* Is a comment? */
			if ( (szString[0]==';') || (szString[0]=='#') )
				continue;
			/* Read values */
			sscanf(szString, "%d,%d", &PCKeyCode, &STScanCode);
			/* Store into remap table, check both value within range */
			if ( (PCKeyCode>=0) && (PCKeyCode<SDLK_LAST) &&
			     (STScanCode>=0) && (STScanCode<256) )
				LoadedKeyToSTScanCode[PCKeyCode] = STScanCode;
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
	SDLKey key;
	char STScanCode;
	SDL_keysym tmpKeySym;

	/* Return if we aren't in fast forward or have not disabled key repeat */
	if ((ConfigureParams.System.bFastForward == false)
	        || (!ConfigureParams.Keyboard.bDisableKeyRepeat))
	{
		return;
	}

	tmpKeySym.mod = KMOD_NONE;

	/* Now run through each PC key looking for ones held down */
	for (key = SDLK_FIRST; key < SDLK_LAST; key++)
	{
		/* Is key held? */
		if (Keyboard.KeyStates[key])
		{
			tmpKeySym.sym = key;
			tmpKeySym.scancode = 0;

			/* Get scan code */
			STScanCode = Keymap_RemapKeyToSTScanCode(&tmpKeySym);
			if (STScanCode != (char)-1)
			{
				/* Does this require de-bouncing? */
				if (Keymap_DebounceSTKey(STScanCode))
					Keymap_KeyUp(&tmpKeySym);
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
	bool bPreviousKeyState;
	char STScanCode;
	int symkey = sdlkey->sym;
	int modkey = sdlkey->mod;

	/*fprintf(stderr, "keydown: sym=%i scan=%i mod=$%x\n",symkey, sdlkey->scancode, modkey);*/

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

	/* Set down */
	bPreviousKeyState = Keyboard.KeyStates[symkey];
	Keyboard.KeyStates[symkey] = true;

	STScanCode = Keymap_RemapKeyToSTScanCode(sdlkey);
	if (STScanCode != (char)-1)
	{
		if (!bPreviousKeyState)
			IKBD_PressSTKey(STScanCode, true);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * User released key
 */
void Keymap_KeyUp(SDL_keysym *sdlkey)
{
	char STScanCode;
	int symkey = sdlkey->sym;
	int modkey = sdlkey->mod;

	/*fprintf(stderr, "keyup: sym=%i scan=%i mod=$%x\n",symkey, sdlkey->scancode, modkey);*/

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
	else if (symkey == SDLK_CAPSLOCK)
	{
		/* Simulate another capslock key press */
		IKBD_PressSTKey(0x3A, true);
	}

	/* Release key (only if was pressed) */
	if (Keyboard.KeyStates[symkey])
	{
		STScanCode = Keymap_RemapKeyToSTScanCode(sdlkey);
		if (STScanCode != (char)-1)
		{
			IKBD_PressSTKey(STScanCode, false);
		}
	}

	Keyboard.KeyStates[symkey] = false;
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
	if (isupper(asckey)) {
		if (press) {
			sdlkey.sym = SDLK_LSHIFT;
			Keymap_KeyDown(&sdlkey);
		}
		sdlkey.sym = tolower(asckey);
		sdlkey.mod = KMOD_LSHIFT;
	} else {
		sdlkey.sym = asckey;
	}
	if (press) {
		Keymap_KeyDown(&sdlkey);
	} else {
		Keymap_KeyUp(&sdlkey);
		if (isupper(asckey)) {
			sdlkey.sym = SDLK_LSHIFT;
			Keymap_KeyUp(&sdlkey);
		}
	}
}
