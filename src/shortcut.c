/*
  Hatari - shortcut.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Shortcut keys
*/
char ShortCut_rcsid[] = "Hatari $Id: shortcut.c,v 1.19 2005-02-13 16:18:49 thothy Exp $";

#include <SDL.h>

#include "main.h"
#include "dialog.h"
#include "audio.h"
#include "joy.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "reset.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "shortcut.h"
#include "sound.h"


SHORTCUT_KEY ShortCutKey;


/*-----------------------------------------------------------------------*/
/*
  Shortcut to toggle full-screen
*/
static void ShortCut_FullScreen(void)
{
  if(!bInFullScreen)
  {
    Screen_EnterFullScreen();
  }
  else
  {
    Screen_ReturnFromFullScreen();
  }
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to toggle mouse mode
*/
static void ShortCut_MouseMode(void)
{
  bGrabMouse = !bGrabMouse;        /* Toggle flag */

  /* If we are in windowed mode, toggle the mouse cursor mode now: */
  if(!bInFullScreen)
  {
    if(bGrabMouse)
    {
      SDL_WM_GrabInput(SDL_GRAB_ON);
    }
    else
    {
      SDL_WM_GrabInput(SDL_GRAB_OFF);
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to toggle YM/WAV sound recording
*/
static void ShortCut_RecordSound(void)
{
  /* Is working? */
  if (bSoundWorking)
  {
    /* Are we currently recording? If so stop */
    if (Sound_AreWeRecording())
    {
      /* Stop, and save */
      Sound_EndRecording();
    }
    else
    {
      /* Begin recording */
      Sound_BeginRecording(ConfigureParams.Sound.szYMCaptureFileName);
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to toggle screen animation recording
*/
static void ShortCut_RecordAnimation(void)
{
  /* Are we currently recording? If so stop */
  if (ScreenSnapShot_AreWeRecording())
  {
    /* Stop */
    ScreenSnapShot_EndRecording();
  }
  else
  {
    /* Start animation */
    ScreenSnapShot_BeginRecording(ConfigureParams.Screen.bCaptureChange, ConfigureParams.Screen.nFramesPerSecond);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to toggle joystick cursor emulation
*/
static void ShortCut_JoystickCursorEmulation(void)
{
  /* Toggle it on/off */
  Joy_ToggleCursorEmulation();
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to sound on/off
*/
static void ShortCut_SoundOnOff(void)
{
  /* Toggle sound on/off */
  ConfigureParams.Sound.bEnableSound ^= TRUE;
  /* And start/stop if need to */
  if (!ConfigureParams.Sound.bEnableSound)
  {
    if (Sound_AreWeRecording())
      Sound_EndRecording();
    Audio_UnInit();
  }
  else
  {
    Audio_Init();
  }
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to maximum speed
*/
static void ShortCut_MaximumSpeed(void)
{
  /* If already on max speed, switch back to normal */
  if (ConfigureParams.System.nMinMaxSpeed == MINMAXSPEED_MAX)
  {
    /* Restore */
    ConfigureParams.System.nMinMaxSpeed = MINMAXSPEED_MIN;
    
    /* Reset the sound emulation variables: */
    Sound_Reset();
  }
  else
  {
    /* Set maximum speed */
    ConfigureParams.System.nMinMaxSpeed = MINMAXSPEED_MAX;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to 'Boss' key, ie minmize Window and switch to another application
*/
static void ShortCut_BossKey(void)
{
  /* If we are in full-screen, then return to a window */
  Screen_ReturnFromFullScreen();

  if(bGrabMouse)
  {
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    bGrabMouse = FALSE;
  }

  /* Minimize Window and give up processing to next one! */
  SDL_WM_IconifyWindow();
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to 'Cold' reset
*/
static void ShortCut_ColdReset(void)
{
  Reset_Cold();                 /* Reset emulator with 'cold' (clear all) */
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to 'Warm' reset
*/
static void ShortCut_WarmReset(void)
{
  Reset_Warm();                 /* Emulator 'warm' reset */
}


/*-----------------------------------------------------------------------*/
/*
  Clear shortkey structure
*/
static void ShortCut_ClearKeys(void)
{
  /* Clear short-cut key structure */
  memset(&ShortCutKey, 0, sizeof(SHORTCUT_KEY));
}


/*-----------------------------------------------------------------------*/
/*
  Check to see if pressed any shortcut keys, and call handling function
*/
void ShortCut_CheckKeys(void)
{
  /* Check for supported keys: */
  switch(ShortCutKey.Key)
  {
   case SDLK_F11:                  /* Switch between fullscreen/windowed mode */
    ShortCut_FullScreen();
    break;
   case SDLK_F12:                  /* Show options dialog */
    Dialog_DoProperty();
    break;
   case SDLK_a:                    /* Record animation */
    ShortCut_RecordAnimation();
    break;
   case SDLK_c:                    /* Cold reset */
    ShortCut_ColdReset();
    break;
   case SDLK_g:                    /* Grab screenshot */
    ScreenSnapShot_SaveScreen();
    break;
   case SDLK_i:                    /* Boss key */
    ShortCut_BossKey		();
    break;
   case SDLK_j:                    /* Toggle cursor-joystick emulation */
    ShortCut_JoystickCursorEmulation();
    break;
   case SDLK_m:                    /* Toggle mouse mode */
    ShortCut_MouseMode();
    break;
   case SDLK_r:                    /* Warm reset */
    ShortCut_WarmReset();
    break;
   case SDLK_s:                    /* Enable/disable sound */
    ShortCut_SoundOnOff();
    break;
   case SDLK_q:                    /* Quit program */
    bQuitProgram = TRUE;
    set_special(SPCFLAG_BRK);
    break;
   case SDLK_x:                    /* Toggle Min/Max speed */
    ShortCut_MaximumSpeed();
    break;
   case SDLK_y:                    /* Toggle sound recording */
    ShortCut_RecordSound();
    break;
  }

  /* And clear */
  ShortCut_ClearKeys();
}
