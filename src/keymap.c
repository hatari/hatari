/*
  Hatari

  Key remapping functions
*/

#include <stdio.h>
#include <SDL_keysym.h>           /* Needed for SDLK_LAST */

#include "main.h"
#include "debug.h"
#include "keymap.h"
#include "memAlloc.h"
#include "misc.h"


/*-----------------------------------------------------------------------*/
/*
  Remap table of Windows keys to ST Scan codes, -ve is invalid key(ie doesn't occur on ST)

  PC Keyboard:-

    Esc  F1  F2  F3  F4    F5  F6  F7  F8    F9  F10 F11 F12       Print Scroll Pause

     1   59  60  61  62    63  64  65  66    67  68  87  88                70     69


¬  !   "   £   $   %   ^   &   *   (   )   _   +                                 Page
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


/* SDL Key to ST scan code map table */
char Default_KeyToSTScanCode[SDLK_LAST] = {
/* ST Code,  PC Code */
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
  -1,    /* SDLK_CLEAR = 12 */
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
  -1,    /* SDLK_HASH = 35 */
  -1,    /* SDLK_DOLLAR = 36 */
  -1,    /* 37 */
  -1,    /* SDLK_AMPERSAND = 38 */
  -1,    /* SDLK_QUOTE = 39 */
  0x63,  /* SDLK_LEFTPAREN = 40 */
  0x64,  /* SDLK_RIGHTPAREN = 41 */
  -1,    /* SDLK_ASTERISK = 42 */
  0x1B,  /* SDLK_PLUS = 43 */
  0x33,  /* SDLK_COMMA = 44 */
  0x35,  /* SDLK_MINUS = 45 */
  0x34,  /* SDLK_PERIOD = 46 */
  -1,    /* SDLK_SLASH = 47 */
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
  -1,    /* SDLK_SEMICOLON = 59 */
  0x60,  /* SDLK_LESS = 60 */
  -1,    /* SDLK_EQUALS = 61 */
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
  -1,    /* SDLK_LEFTBRACKET = 91 */
  -1,    /* SDLK_BACKSLASH = 92 */
  -1,    /* SDLK_RIGHTBRACKET = 93 */
  -1,    /* SDLK_CARET = 94 */
  -1,    /* SDLK_UNDERSCORE = 95 */
  -1,    /* SDLK_BACKQUOTE = 96 */
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
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 160-175*/
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 176-191*/
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 192-207*/
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 208-223*/
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 224-239*/
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 240-255*/
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
  -1,      /* SDLK_KP_EQUALS = 272 */
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
  -1,      /* SDLK_F13 = 294 */
  -1,      /* SDLK_F14 = 295 */
  -1,      /* SDLK_F15 = 296 */
  -1,      /* 297 */
  -1,      /* 298 */
  -1,      /* 299 */
  /* Key state modifier keys */
  -1,    /* SDLK_NUMLOCK = 300 */
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
  -1,      /* SDLK_LSUPER = 311 */  /* Left "Windows" key */
  -1,      /* SDLK_RSUPER = 312 */  /* Right "Windows" key */
  -1,      /* SDLK_MODE = 313 */  /* "Alt Gr" key */
  -1,      /* SDLK_COMPOSE = 314 */ /* Multi-key compose key */
  /* Miscellaneous function keys */
  0x62,    /* SDLK_HELP = 315 */
  0x63,    /* SDLK_PRINT = 316 */
  -1,      /* SDLK_SYSREQ = 317 */
  -1,      /* SDLK_BREAK = 318 */
  -1,      /* SDLK_MENU = 319 */
};


char Loaded_KeyToSTScanCode[SDLK_LAST];
BOOL bRemapKeyLoaded=FALSE;


/*-----------------------------------------------------------------------*/
/*
  Remap SDL Key to ST Scan code
*/
char Keymap_RemapKeyToSTScanCode(unsigned int Key)
{
  if( Key >= SDLK_LAST )  return -1;  /* Avoid illegal keys */
  /* Use default or loaded? */
  if (bRemapKeyLoaded)
    return(Loaded_KeyToSTScanCode[Key]);
  else
    return(Default_KeyToSTScanCode[Key]);
}


/*-----------------------------------------------------------------------*/
/*
  Load keyboard remap file
*/
void Keymap_LoadRemapFile(char *pszFileName)
{
  char szString[1024];
  unsigned int STScanCode,PCScanCode;
  FILE *in;

  /* Default to not loaded */
  bRemapKeyLoaded = FALSE;
  Memory_Set(Loaded_KeyToSTScanCode,-1,sizeof(Loaded_KeyToSTScanCode));

  /* Attempt to load file */
  if (strlen(pszFileName)>0) {
    /* Open file */
    in = fopen(pszFileName, "r");
    if (in) {
      while(!feof(in)) {
        /* Read line from file */
        fgets(szString, 1024, in);
        /* Remove white-space from start of line */
        Misc_RemoveWhiteSpace(szString,sizeof(szString));
        if (strlen(szString)>0) {
          /* Is a comment? */
          if ( (szString[0]==';') || (szString[0]=='#') )
            continue;
          /* Read values */
          sscanf(szString,"%d,%d",&STScanCode,&PCScanCode);
          /* Store into remap table, check both value within range */
          if ( (PCScanCode>=0) && (PCScanCode<SDLK_LAST) && (STScanCode>=0) && (STScanCode<256) )
            Loaded_KeyToSTScanCode[PCScanCode] = STScanCode;
        }
      }
      /* Loaded OK */
      bRemapKeyLoaded = TRUE;

      fclose(in);
    }
  }
}
