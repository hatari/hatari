/*
  Hatari

  Here we process a key press and the remapping of the scancodes.
*/

#include <stdio.h>
#include <SDL_keysym.h>           /* Needed for SDLK_LAST */

#include "main.h"
#include "debug.h"
#include "keymap.h"
#include "memAlloc.h"
#include "misc.h"
#include "dialog.h"
#include "ikbd.h"
#include "joy.h"
#include "shortcut.h"
#include "screen.h"
#include "debugui.h"


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
char Default_KeyToSTScanCode[SDLK_LAST];

/* List of ST scan codes to NOT de-bounce when running in maximum speed */
char DebounceExtendedKeys[] = {
  0x1d,  /* CTRL */
  0x2a,  /* Left SHIFT */
  0x01,  /* ESC */
  0x38,  /* ALT */
  0x36,  /* Right SHIFT */
  0      /* term */
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
  else {
    char st_key = (Default_KeyToSTScanCode[Key]);
    //char st_key = (Key >= 9 ? Key-8 : Key);
    // fprintf(stderr,"returned pc=%d st=%d\n",Key,st_key);
    return st_key;
  }
}

static void numlock(int set) {
  int n;
  if (set & 0x1000) {
    for (n=79; n<=81; n++) // 7..9
      Default_KeyToSTScanCode[n] = n+24;
    for (n=83; n<=85; n++) // 4..6
      Default_KeyToSTScanCode[n] = n+23;
    for (n=87; n<=89; n++) // 1..3
      Default_KeyToSTScanCode[n] = n+22;
  } else {
    // arrow keys
    for (n=79; n<=81; n++) // 7..9
      Default_KeyToSTScanCode[n] = n-8;
    for (n=83; n<=85; n++) // 4..6
      Default_KeyToSTScanCode[n] = n-8;
    for (n=87; n<=89; n++) // 1..3
      Default_KeyToSTScanCode[n] = n-8;

    /* Dungeon master patch ! To be able to move with one hand
       on the numeric pad, we need 7 to send insert and 9 to send clr/home */
    Default_KeyToSTScanCode[79] = 82; // insert
    Default_KeyToSTScanCode[81] = 71; // home
  }
}

void Keymap_FromScancodes() {
  int n;
  // The st keyboard looks very much like the pc keyboard...
  for (n=9; n<=SDLK_LAST; n++)
    Default_KeyToSTScanCode[n] = n-8;

  // keypad
  numlock(SDL_GetModState());

  Default_KeyToSTScanCode[94] = 96; // <>
  Default_KeyToSTScanCode[100] = 75; // <-
  Default_KeyToSTScanCode[104] = 80; // bas
  Default_KeyToSTScanCode[102] = 77; // ->
  Default_KeyToSTScanCode[98] = 72; // haut

  Default_KeyToSTScanCode[63] = 102; // *
  Default_KeyToSTScanCode[112] = 101; // /
  Default_KeyToSTScanCode[108] = 0x72; // enter (28 is return it should be fixed later...)

  Default_KeyToSTScanCode[111] = 0x62; // help key (print screen)
  Default_KeyToSTScanCode[110] = 0x61; // undo key (pause)
  Default_KeyToSTScanCode[106] = 82; // insert
  Default_KeyToSTScanCode[107] = 83; // suppr
  Default_KeyToSTScanCode[97] = 71; // home
  Default_KeyToSTScanCode[103] = 79; // end (no end key on the st ?!!!)
  Default_KeyToSTScanCode[99] = 73; // pg up (no equivalent)
  Default_KeyToSTScanCode[105] = 81; // pg down
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



/*-----------------------------------------------------------------------*/
/*
  Scan list of keys to NOT de-bounce when running in maximum speed, eg ALT,SHIFT,CTRL etc...
  Return TRUE if key requires de-bouncing
*/
BOOL Keymap_DebounceSTKey(char STScanCode)
{
  int i=0;

  /* Are we in maximum speed, and have disabled key repeat? */
  if ( (ConfigureParams.Configure.nMinMaxSpeed!=MINMAXSPEED_MIN) && (ConfigureParams.Keyboard.bDisableKeyRepeat) ) {
    /* We should de-bounce all non extended keys, eg leave ALT,SHIFT,CTRL etc... held */
    while (DebounceExtendedKeys[i]) {
      if (STScanCode==DebounceExtendedKeys[i])
        return(FALSE);
      i++;
    }

    /* De-bounce key */
    return(TRUE);
  }

  /* Do not de-bounce key */
  return(FALSE);
}

void Keymap_KeyUp_from_scancode(int scan)
{
  /* Release key (only if was pressed) */
  // special version for debug
  int STScanCode = Keymap_RemapKeyToSTScanCode(scan);
  if (STScanCode != (char)-1)
  {
    if (Keyboard.KeyStates[scan]) {
      IKBD_PressSTKey(STScanCode,FALSE);
      Keyboard.KeyStates[scan] = FALSE;
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Debounce any PC key held down if running with key repeat disabled
  This is called each ST frame, so keys get held down for one VBL which is enough for 68000 code to scan
*/
void Keymap_DebounceAllKeys(void)
{
  unsigned int Key;
  char STScanCode;

  /* Are we in maximum speed, and have disabled key repeat? */
  if ( (ConfigureParams.Configure.nMinMaxSpeed!=MINMAXSPEED_MIN) && (ConfigureParams.Keyboard.bDisableKeyRepeat) )
  {
    /* Now run through each PC key looking for ones held down */
    for(Key=0; Key<SDLK_LAST; Key++)
    {
      /* Is key held? */
      if (Keyboard.KeyStates[Key])
      {
        /* Get scan code */
        STScanCode = Keymap_RemapKeyToSTScanCode(Key);
        if (STScanCode != (char)-1)
        {
          /* Does this require de-bouncing? */
          if (Keymap_DebounceSTKey(STScanCode))
            Keymap_KeyUp_from_scancode(Key);
        }
      }
    }
  }

}


/*-----------------------------------------------------------------------*/
/*
  User press key down
*/
void Keymap_KeyDown( SDL_keysym *sdlkey )
{
  BOOL bPreviousKeyState;
  char STScanCode;
  unsigned int Key;
  int sdlmod = sdlkey->mod;
  int scan = sdlkey->scancode;

  Key = sdlkey->sym;

  /* If using cursor emulation, DON'T send keys to keyboard processor!!! Some games use keyboard as pause! */
  if ( (ConfigureParams.Joysticks.Joy[0].bCursorEmulation || ConfigureParams.Joysticks.Joy[1].bCursorEmulation)
            && !(sdlmod&(KMOD_LSHIFT|KMOD_RSHIFT)) )
  {
    if( Key==SDLK_UP )         { cursorJoyEmu |= 1; return; }
    else if( Key==SDLK_DOWN )  { cursorJoyEmu |= 2; return; }
    else if( Key==SDLK_LEFT )  { cursorJoyEmu |= 4; return; }
    else if( Key==SDLK_RIGHT ) { cursorJoyEmu |= 8; return; }
    else if( Key==SDLK_RCTRL || Key==SDLK_KP0 )  { cursorJoyEmu |= 128; return; }
  }
  if (scan == 77) { // numlock
    numlock(sdlmod);
    return;
  } else if (scan == 113) { // alt-gr
    return;
  }

  /* Set down */
  bPreviousKeyState = Keyboard.KeyStates[scan];
  Keyboard.KeyStates[scan] = TRUE;

  /* Jump directly to the debugger? */
  if( Key==SDLK_PAUSE && bEnableDebug)
  {
    if(bInFullScreen)  Screen_ReturnFromFullScreen();
    DebugUI();
  }

  /* If pressed short-cut key, retain keypress until safe to execute (start of VBL) */
  if ( (sdlmod&KMOD_MODE) || (sdlmod==KMOD_LMETA)
       ||(Key==SDLK_F11) || (Key==SDLK_F12) )
  {
    ShortCutKey.Key = Key;
    if( sdlmod&(KMOD_LCTRL|KMOD_RCTRL) )  ShortCutKey.bCtrlPressed = TRUE;
    if( sdlmod&(KMOD_LSHIFT|KMOD_RSHIFT) )  ShortCutKey.bShiftPressed = TRUE;
  }
  else
  {
    STScanCode = Keymap_RemapKeyToSTScanCode(scan);
    if (STScanCode > 0)
    {
      if (!bPreviousKeyState || scan==66) // special case for caps lock
        IKBD_PressSTKey(STScanCode,TRUE);
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  User released key
*/
void Keymap_KeyUp(SDL_keysym *sdlkey)
{
  char STScanCode;
  unsigned int Key;
  int sdlmod = sdlkey->mod;
  int scan = sdlkey->scancode;
  Key = sdlkey->sym;


  /* If using cursor emulation, DON'T send keys to keyboard processor!!! Some games use keyboard as pause! */
  if ( (ConfigureParams.Joysticks.Joy[0].bCursorEmulation || ConfigureParams.Joysticks.Joy[1].bCursorEmulation)
            && !(sdlmod&(KMOD_LSHIFT|KMOD_RSHIFT)) )
  {
    if( Key==SDLK_UP )         { cursorJoyEmu &= ~1; return; }
    else if( Key==SDLK_DOWN )  { cursorJoyEmu &= ~2; return; }
    else if( Key==SDLK_LEFT )  { cursorJoyEmu &= ~4; return; }
    else if( Key==SDLK_RIGHT ) { cursorJoyEmu &= ~8; return; }
    else if( Key==SDLK_RCTRL || Key==SDLK_KP0 )  { cursorJoyEmu &= ~128; return; }
  }
  if (scan == 77) { // numlock
    numlock(sdlmod);
    return;
  } else if (scan == 113) { // alt-gr
    return;
  } else if (scan == 66) // caps lock
    Keymap_KeyDown(sdlkey);

  /* Release key (only if was pressed) */
  STScanCode = Keymap_RemapKeyToSTScanCode(scan);
  if (STScanCode > 0)
  {
    if (Keyboard.KeyStates[scan]) {
      IKBD_PressSTKey(STScanCode,FALSE);
      Keyboard.KeyStates[scan] = FALSE;
    }
  }
}

