/*
  Hatari

  Shortcut keys
*/

#include <SDL.h>

#include "main.h"
#include "dialog.h"
#include "audio.h"
#include "joy.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "reset.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "shortcut.h"
#include "sound.h"



/* List of possible short-cuts(MUST match SHORTCUT_xxxx) */
char *pszShortCutTextStrings[NUM_SHORTCUTS+1] = {
  "(not assigned)",
  "Full Screen",
  "Mouse Mode",
  "Record YM/WAV",
  "Record Animation",
  "Joystick Emulation",
  "Sound On/Off",
  "Maximum Speed",
  "'Cold' Reset",
  "'Warm' Reset",
  "'Boss' Key",
  NULL  /*term*/
};

char *pszShortCutF11TextString[] = {
  "Full Screen",
  NULL  /*term*/
};

char *pszShortCutF12TextString[] = {
  "Mouse Mode",
  NULL  /*term*/
};

ShortCutFunction_t pShortCutFunctions[NUM_SHORTCUTS] = {
  NULL,
  ShortCut_FullScreen,
  ShortCut_MouseMode,
  ShortCut_RecordSound,
  ShortCut_RecordAnimation,
  ShortCut_JoystickCursorEmulation,
  ShortCut_SoundOnOff,
  ShortCut_MaximumSpeed,
  ShortCut_ColdReset,
  ShortCut_WarmReset,
  ShortCut_BossKey
};

SHORTCUT_KEY ShortCutKey;

/*-----------------------------------------------------------------------*/
/*
  Clear shortkey structure
*/
void ShortCut_ClearKeys(void)
{
  /* Clear short-cut key structure */
  Memory_Clear(&ShortCutKey,sizeof(SHORTCUT_KEY));
}

/*-----------------------------------------------------------------------*/
/*
  Check to see if pressed any shortcut keys, and call handling function
*/
void ShortCut_CheckKeys(void)
{
  ShortCutFunction_t pShortCutFunction;
  int PressedKey=SHORT_CUT_NONE;

  /* Check for F11 or F12 */
  /*
  if (ShortCutKey.Key==SDLK_F11)
    PressedKey = SHORT_CUT_F11;
  else if (ShortCutKey.Key==SDLK_F12)
    PressedKey = SHORT_CUT_F12;
  */

  /* Check for supported keys: */
  switch(ShortCutKey.Key) {
     case SDLK_F12:                  /* Show options dialog */
       Dialog_DoProperty();
       break;
     case SDLK_F11:                  /* Switch between fullscreen/windowed mode */
       if( !bInFullScreen )
         Screen_EnterFullScreen();
        else
         Screen_ReturnFromFullScreen();
       break;
     case SDLK_q:                    /* Quit program */
       bQuitProgram = TRUE;
       break;
     case SDLK_g:                    /* Grab screenshot */
       ScreenSnapShot_SaveScreen();
       break;
     case SDLK_r:                    /* Warm reset */
       ShortCut_WarmReset();
       break;
     case SDLK_c:                    /* Cold reset */
       ShortCut_ColdReset();
       break;
  }

/* Winston originally supported remapable shortcuts... perhaps we will do so, too, one day... */
/*
  // Did press key?
  if (PressedKey!=SHORT_CUT_NONE) {
    // Find which short-cut to do, eg do have Ctrl or Shift held down?
    if (ShortCutKey.bCtrlPressed)
      pShortCutFunction = pShortCutFunctions[ ConfigureParams.Keyboard.ShortCuts[PressedKey][SHORT_CUT_CTRL] ];
    else if (ShortCutKey.bShiftPressed) {
      pShortCutFunction = pShortCutFunctions[ ConfigureParams.Keyboard.ShortCuts[PressedKey][SHORT_CUT_SHIFT] ];
      // If we don't have a SHIFT short-cut assigned, set to normal key(to allow for SHIFT+F11 to bring up Floppy A)
      if (pShortCutFunction==NULL)
        pShortCutFunction = pShortCutFunctions[ ConfigureParams.Keyboard.ShortCuts[PressedKey][SHORT_CUT_KEY] ];
    }
    else
      pShortCutFunction = pShortCutFunctions[ ConfigureParams.Keyboard.ShortCuts[PressedKey][SHORT_CUT_KEY] ];

    // And call short-cut, if have one
    if (pShortCutFunction)
      pShortCutFunction();
*/
    /* And clear */
    ShortCut_ClearKeys();
  /*}*/

}

/*-----------------------------------------------------------------------*/
/*
  Shortcut to toggle full-screen
*/
void ShortCut_FullScreen(void)
{
/* FIXME */
/*
  // Is working?
  if (bDirectDrawWorking) {
    // Toggle full screen
    if (bInFullScreen) {
      // Did hold down SHIFT? Bring up insert floppy dialog
      if (ShortCutKey.bShiftPressed) {
        // Flip to Windows full-screen GDI surface(or back to Windows if fails)
        DSurface_FlipToGDI();

        // Open dialog(ignore SHIFT key)
        PostMessage(hWnd,WM_COMMAND,ID_FLOPPYA_INSERTDISC,0);

        // Back if sent user to Windows
        if (!bFullScreenHold) {
          // Return back to full-screen
          Screen_EnterFullScreen();
        }
      }
      else
        Screen_ReturnFromFullScreen();
    }
    else {
      // Did hold down SHIFT? Bring up insert floppy dialog
      if (ShortCutKey.bShiftPressed) {
        // Back to Windows mouse
        View_ToggleWindowsMouse(MOUSE_WINDOWS);
        // Open dialog(ignore SHIFT key)
        ToolBar_Activate_FloppyA(TRUE);
      }
      else {
        // Just pressed F11, so go to full-screen
        Screen_EnterFullScreen();
      }
    }
  }
*/
}

//-----------------------------------------------------------------------
/*
  Shortcut to toggle mouse mode
*/
void ShortCut_MouseMode(void)
{
/* FIXME */
/*
  if (bInFullScreen) {
    // Were we in our full-screen menu/Windows mouse mode?
    if (bFullScreenHold && bWindowsMouseMode) {
      SetMenu(hWnd,NULL);
      bFullScreenHold = FALSE;            // Release screen hold
      
      View_ToggleWindowsMouse(MOUSE_ST);

      return;
    }
    else {
      // Flip to Windows full-screen GDI surface(or back to Windows if fails)
      if (DSurface_FlipToGDI()) {
        // Did hold down SHIFT? Bring up insert floppy dialog
        if (ShortCutKey.bShiftPressed) {
          // Open dialog(ignore SHIFT key)
          PostMessage(hWnd,WM_COMMAND,ID_FLOPPYB_INSERTDISC_NORESET,0);
        }
      }
    }
    
    // Do need to return to Windows to handle F12? Ie, low resolution, or GDI flip failed?
    if (!bFullScreenHold) {
      Screen_ReturnFromFullScreen();

      // Did hold down SHIFT? Bring up insert floppy dialog
      if (ShortCutKey.bShiftPressed) {
        // Back to Windows mouse
        View_ToggleWindowsMouse(MOUSE_WINDOWS);
        // Open dialog(ignore SHIFT key)
        ToolBar_Activate_FloppyB(TRUE);
        // Return back to full-screen
        Screen_EnterFullScreen();
      }
      else {
        View_ToggleWindowsMouse(MOUSE_TOGGLE);
      }
    }
  }
  else {
    // Did hold down SHIFT? Bring up insert floppy dialog
    if (ShortCutKey.bShiftPressed) {
      // Back to Windows mouse
      View_ToggleWindowsMouse(MOUSE_WINDOWS);
      // Open dialog(ignore SHIFT key)
      ToolBar_Activate_FloppyB(TRUE);
    }
    else
      View_ToggleWindowsMouse(MOUSE_TOGGLE);
  }
*/
}

//-----------------------------------------------------------------------
/*
  Shortcut to toggle YM/WAV sound recording
*/
void ShortCut_RecordSound(void)
{
/* FIXME */
/*
  // Is working?
  if (bSoundWorking) {
    // Are we currently recording? If so stop
    if (Sound_AreWeRecording()) {
      // Stop, and save
      Sound_EndRecording(NULL);
    }
    else {
      // Being recording
      Sound_BeginRecording(NULL,ConfigureParams.Sound.szYMCaptureFileName);
    }
  }
*/
}

//-----------------------------------------------------------------------
/*
  Shortcut to toggle screen animation recording
*/
void ShortCut_RecordAnimation(void)
{
/* FIXME */
/*
  // Are we in a Window?
  if (!bInFullScreen) {
    // Are we currently recording? If so stop
    if (ScreenSnapShot_AreWeRecording()) {
      // Stop
      ScreenSnapShot_EndRecording(NULL);
    }
    else {
      // Start animation
      ScreenSnapShot_BeginRecording(NULL,ConfigureParams.Screen.bCaptureChange,ConfigureParams.Screen.nFramesPerSecond);
    }
  }
*/
}

//-----------------------------------------------------------------------
/*
  Shortcut to toggle joystick cursor emulation
*/
void ShortCut_JoystickCursorEmulation(void)
{
  // Toggle it on/off
  Joy_ToggleCursorEmulation();
}

//-----------------------------------------------------------------------
/*
  Shortcut to sound on/off
*/
void ShortCut_SoundOnOff(void)
{
/* FIXME */
/*
  // Toggle sound on/off
  ConfigureParams.Sound.bEnableSound ^= TRUE;
  // And start/stop if need to
  if (!ConfigureParams.Sound.bEnableSound) {
    if (Sound_AreWeRecording())
      Sound_EndRecording(NULL);
    DAudio_StopBuffer();
  }
  else
    DAudio_ResetBuffer();
*/
}

//-----------------------------------------------------------------------
/*
  Shortcut to maximum speed
*/
void ShortCut_MaximumSpeed(void)
{
/* FIXME */
/*
  // If already on max speed, restore
  if (ConfigureParams.Configure.nMinMaxSpeed==MINMAXSPEED_MAX) {
    // Restore
    ConfigureParams.Configure.nMinMaxSpeed = ConfigureParams.Configure.nPrevMinMaxSpeed;
  }
  else {
    // Set maximum speed
    ConfigureParams.Configure.nPrevMinMaxSpeed = ConfigureParams.Configure.nMinMaxSpeed;
    ConfigureParams.Configure.nMinMaxSpeed = MINMAXSPEED_MAX;
  }

  // Set new timer thread
  Main_SetSpeedThreadTimer(ConfigureParams.Configure.nMinMaxSpeed);
*/
}

//-----------------------------------------------------------------------
/*
  Shortcut to 'Boss' key, ie minmize Window and switch to another application
*/
void ShortCut_BossKey(void)
{
/* FIXME */
/*
  // If we are in full-screen, then return to a Window
  Screen_ReturnFromFullScreen();
  // Restore a few things
  View_ToggleWindowsMouse(MOUSE_ST);      // Put mouse into ST mode
  View_LimitCursorToScreen();        // Free mouse from Window constraints
  // Minimize Window and give up processing to next one!
  ShowWindow(hWnd,SW_MINIMIZE);
*/
}

//-----------------------------------------------------------------------
/*
  Shortcut to 'Cold' reset
*/
void ShortCut_ColdReset(void)
{
  Reset_Cold();                 /* Reset emulator with 'cold' (clear all) */
}

//-----------------------------------------------------------------------
/*
  Shortcut to 'Warm' reset
*/
void ShortCut_WarmReset(void)
{
  Reset_Warm();                 /* Emulator 'warm' reset */
}
