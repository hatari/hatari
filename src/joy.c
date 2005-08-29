/*
  Hatari - joy.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Joystick routines.

  NOTE Also the ST uses the joystick port 1 as the default controller
         - so we allocate our joysticks  with index 1 and then 0 so these match.
*/
char Joy_rcsid[] = "Hatari $Id: joy.c,v 1.6 2005-08-29 20:13:43 thothy Exp $";

#include <SDL.h>

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "joy.h"
#include "log.h"
#include "video.h"

#define JOY_BUTTON1  1
#define JOY_BUTTON2  2


SDL_Joystick *sdlJoystick[2] = { NULL,NULL };  /* SDL's joystick structures */
BOOL bJoystickWorking[2] = { FALSE,FALSE };  /* Is joystick plugged in and working? */
int JoystickSpaceBar = FALSE;                /* State of space-bar on joystick button 2 */
int cursorJoyEmu;                            /* set in view.c */


/*-----------------------------------------------------------------------*/
/*
  This function initialises the (real) joysticks.
*/
void Joy_Init(void)
{
  int i, nPadsConnected, JoyID=1;            /* Store in ST joystick slot 1 then 0 */

  /* Initialise SDL's joystick subsystem: */
  if( SDL_InitSubSystem(SDL_INIT_JOYSTICK)<0 )
  {
    fprintf(stderr, "Could not init joysticks: %s\n", SDL_GetError() );
    return;
  }

  /* Scan joystick connection array for first two working joysticks */
  nPadsConnected = SDL_NumJoysticks();
  for(i=0; (i<nPadsConnected) && (JoyID>=0); i++)
  {
    /* Open the joystick for use */
    sdlJoystick[JoyID] = SDL_JoystickOpen(i);
    /* Is pad ok? */
    if(sdlJoystick[JoyID]!=NULL)
    {
      /* Set as working (NOTE we assign ST joysticks 1 first), and store ID */
      Log_Printf(LOG_DEBUG, "Joystick: %d found\n", JoyID);
      bJoystickWorking[JoyID] = TRUE;
      JoyID--;
    }
  }


  /* OK, do we have any working joysticks? */
  if (!bJoystickWorking[1]) {
    /* No, so if first time install need to set cursor emulation */
    if (bFirstTimeInstall)
      ConfigureParams.Joysticks.Joy[1].bCursorEmulation = TRUE;
  }
  /* Make sure we only have one in cursor emulation mode */
  Joy_PreventBothUsingCursorEmulation();

  JoystickSpaceBar = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Make sure only one joystick is assigned as cursor emulation mode
*/
void Joy_PreventBothUsingCursorEmulation(void)
{
  /* Make sure we cannot have both joysticks assigned as cursor emulation */
  if (ConfigureParams.Joysticks.Joy[0].bCursorEmulation && ConfigureParams.Joysticks.Joy[0].bCursorEmulation) {
    ConfigureParams.Joysticks.Joy[0].bCursorEmulation = FALSE;
    ConfigureParams.Joysticks.Joy[1].bCursorEmulation = TRUE;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Read details from joystick using SDL calls
  NOTE ID is that of ST (ie 1 is default)
*/
static BOOL Joy_ReadJoystick(int JoystickID, JOYREADING *pJoyReading)
{
  /* Joystick is OK, read position */
  pJoyReading->XPos = SDL_JoystickGetAxis(sdlJoystick[JoystickID], 0);
  pJoyReading->YPos = SDL_JoystickGetAxis(sdlJoystick[JoystickID], 1);
  /* Sets bit #0 if button #1 is pressed: */
  pJoyReading->Buttons = SDL_JoystickGetButton(sdlJoystick[JoystickID], 0);
  /* Sets bit #1 if button #2 is pressed: */
  if( SDL_JoystickGetButton(sdlJoystick[JoystickID], 1) )
    pJoyReading->Buttons |= JOY_BUTTON2;

  return(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Read PC joystick and return ST format byte, ie lower 4 bits direction and top bit fire
  NOTE : ID 0 is Joystick 0/Mouse and ID 1 is Joystick 1 (default)
*/
unsigned char Joy_GetStickData(unsigned int JoystickID)
{
  unsigned char Data = 0;

  JOYREADING JoyReading;

  /* Are we emulating the joystick via the cursor key? */
  if (ConfigureParams.Joysticks.Joy[JoystickID].bCursorEmulation) {
    /* If holding 'SHIFT' we actually want cursor key movement, so ignore any of this */
    if ( !(SDL_GetModState()&(KMOD_LSHIFT|KMOD_RSHIFT)) ) {
      Data = cursorJoyEmu;        /* cursorJoyEmu is set in keymap.c */
    }
  }
  else if (bJoystickWorking[JoystickID]) {
    if (!Joy_ReadJoystick(JoystickID,&JoyReading)) {
      /* Something is wrong, we cannot read the joystick */
      bJoystickWorking[JoystickID] = FALSE;
    }

    /* So, did read joysyick OK? */
    if (bJoystickWorking[JoystickID]) {
      /* Directions */
      if (JoyReading.YPos<=JOYRANGE_UP_VALUE)
        Data |= 0x01;
      if (JoyReading.YPos>=JOYRANGE_DOWN_VALUE)
        Data |= 0x02;
      if (JoyReading.XPos<=JOYRANGE_LEFT_VALUE)
        Data |= 0x04;
      if (JoyReading.XPos>=JOYRANGE_RIGHT_VALUE)
        Data |= 0x08;

      /* Buttons - I've made fire button 2 to simulate the pressing of the space bar (for Xenon II etc...) */
#ifdef USE_FIREBUTTON_2_AS_SPACE
      /* PC Joystick button 1 is set as ST joystick button and PC button 2 is the space bar */
      if (JoyReading.Buttons&JOY_BUTTON1)
        Data |= 0x80;
      if (JoyReading.Buttons&JOY_BUTTON2) {
        /* Only press 'space bar' if not in NULL state */
        if (!JoystickSpaceBar) {
          /* Press, ikbd will send packets and de-press */
          JoystickSpaceBar = JOYSTICK_SPACE_DOWN;
        }
      }
#else   /*USE_FIREBUTTON_2_AS_SPACE*/
      /* PC Joystick buttons 1+2 are set as ST joystick button */
      if ( (JoyReading.Buttons&JOY_BUTTON1) || (JoyReading.Buttons&JOY_BUTTON2) )
        Data |= 0x80;
#endif  /*USE_FIREBUTTON_2_AS_SPACE*/
    }
  }

  /* Ignore fire button every 8 frames if enabled autofire (for both cursor emulation and joystick) */
  if (ConfigureParams.Joysticks.Joy[JoystickID].bEnableAutoFire) {
    if ((nVBLs&0x7)<4)
      Data &= 0x7f;          /* Remove top bit! */
  }

  return(Data);
}


/*-----------------------------------------------------------------------*/
/*
  Toggle cursor emulation
*/
void Joy_ToggleCursorEmulation(void)
{
  /* Toggle joystick 1 cursor emulation */
  ConfigureParams.Joysticks.Joy[1].bCursorEmulation ^= TRUE;
  /* Prevent both having emulation */
  Joy_PreventBothUsingCursorEmulation();
}

