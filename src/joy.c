/*
  Hatari

  Joystick routines

  NOTE Also the ST uses the joystick port 1 as the default controller
         - so we allocate our joysticks  with index 1 and then 0 so these match.
*/

#include <SDL.h>

#include "main.h"
#include "configuration.h"
#include "debug.h"
#include "dialog.h"
#include "errlog.h"
#include "joy.h"
#include "screen.h"

#define REVERSE_ID(id)  ((id)^1)             /* ST joystick 1(default) is first PC joystick found */

BOOL bJoystickWorking[2] = { FALSE,FALSE };  /* Is joystick plugged in and working? */
int WindowsJoystickID[2];                    /* Windows Joystick ID of above joysticks */
int JoystickSpaceBar = FALSE;                /* State of space-bar on joystick button 2 */
int cursorJoyEmu;                            /* set in view.c */


/*-----------------------------------------------------------------------*/
/*
  Initialise joysticks, try to use DirectInput or, if this fails, standard Windows calls
*/
void Joy_Init(void)
{
  /* First, try for DirectInput(this sets bAllowDirectInput)*/
//  DJoy_Init();
  /* Also try standard Windows joystick functions */
  Joy_InitWindows();

  /* OK, do we have any working joysticks? */
  if (!bJoystickWorking[1]) {
    /* No, so if first time install need to set cursor emulation */
//FIXME    if (bFirstTimeInstall)
//FIXME      ConfigureParams.Joysticks.Joy[1].bCursorEmulation = TRUE;
  }
  /* Make sure we only have one in cursor emulation mode */
  Joy_PreventBothUsingCursorEmulation();

  JoystickSpaceBar = FALSE;
}

//-----------------------------------------------------------------------
/*
  Initialise connected joysticks using Windows library calls
*/
void Joy_InitWindows(void)
{
//FIXME
/*
   JOYINFO JoyInfo;
  int i,nPadsConnected,JoyID=1;            // Store in ST joystick slot 1 then 0

  // Scan joystick connection array for first two working joysticks
  nPadsConnected = joyGetNumDevs();
  for(i=0; (i<nPadsConnected) && (JoyID>=0); i++) {
    // Is pad ok?
    if (joyGetPos(i,&JoyInfo)==JOYERR_NOERROR) {
      // Set as working(NOTE we assign ST joysticks 1 first), and store Windows ID
      ErrLog_File("Joystick: %d found\n",JoyID);
      bJoystickWorking[JoyID] = TRUE;
      WindowsJoystickID[JoyID] = i;
      JoyID--;
    }
  }
*/
}

//-----------------------------------------------------------------------
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

//-----------------------------------------------------------------------
/*
  Read details from joystick using Windows calls
  NOTE ID is that of ST(ie 1 is default)
*/
BOOL Joy_ReadJoystick(int JoystickID,JOYREADING *pJoyReading)
{
//FIXME
/*
  JOYINFO JoyInfo;

  // Joystick is OK, read position
  if (joyGetPos(WindowsJoystickID[JoystickID],&JoyInfo)==JOYERR_NOERROR) {
    // Copy details
    pJoyReading->XPos = JoyInfo.wXpos;
    pJoyReading->YPos = JoyInfo.wYpos;
    pJoyReading->Buttons = JoyInfo.wButtons;

    return(TRUE);
  }
*/
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  Read PC joystick and return ST format byte, ie lower 4 bits direction and top bit fire
  NOTE : ID 0 is Joystick 0/Mouse and ID 1 is Joystick 1(default)
*/
unsigned char Joy_GetStickData(unsigned int JoystickID)
{
  unsigned char Data = 0;

//FIXME   JOYREADING JoyReading;

  /* Are we emulating the joystick via the cursor key? */
  if (ConfigureParams.Joysticks.Joy[JoystickID].bCursorEmulation) {
    /* If holding 'SHIFT' we actually want cursor key movement, so ignore any of this */
    if ( !(SDL_GetModState()&(KMOD_LSHIFT|KMOD_RSHIFT)) )
                  {
                   Data = cursorJoyEmu;    /* cursorJoyEmu is set in view.c */
      }
  }
/*
  else if (bJoystickWorking[JoystickID]) {
    // Read joystick information from DirectInput or Windows calls into standard format
    if (!DJoy_ReadJoystick(JoystickID,&JoyReading)) {
      if (!Joy_ReadJoystick(JoystickID,&JoyReading)) {
        // Something is wrong, we cannot read the joystick
        bJoystickWorking[JoystickID] = FALSE;
      }
    }

    // So, did read joysyick OK?
    if (bJoystickWorking[JoystickID]) {
      // Directions
      if (JoyReading.YPos<=JOYRANGE_UP_VALUE)
        Data |= 0x01;
      if (JoyReading.YPos>=JOYRANGE_DOWN_VALUE)
        Data |= 0x02;
      if (JoyReading.XPos<=JOYRANGE_LEFT_VALUE)
        Data |= 0x04;
      if (JoyReading.XPos>=JOYRANGE_RIGHT_VALUE)
        Data |= 0x08;

      // Buttons - I've made fire button 2 to simulate the pressing of the space bar(for Xenon II etc...)
#ifdef USE_FIREBUTTON_2_AS_SPACE
      // PC Joystick button 1 is set as ST joystick button and PC button 2 is the space bar
      if (JoyReading.Buttons&JOY_BUTTON1)
        Data |= 0x80;
      if (JoyReading.Buttons&JOY_BUTTON2) {
        // Only press 'space bar' if not in NULL state
        if (!JoystickSpaceBar) {
          // Press, ikbd will send packets and de-press
          JoystickSpaceBar = JOYSTICK_SPACE_DOWN;
        }
      }
#else  //USE_FIREBUTTON_2_AS_SPACE
      // PC Joystick buttons 1+2 are set as ST joystick button
      if ( (JoyReading.Buttons&JOY_BUTTON1) || (JoyReading.Buttons&JOY_BUTTON2) )
        Data |= 0x80;
#endif  //USE_FIREBUTTON_2_AS_SPACE
    }
  }

  // Ignore fire button every 8 frames if enabled autofire(for both cursor emulation and joystick)
  if (ConfigureParams.Joysticks.Joy[JoystickID].bEnableAutoFire) {
    if ((VBLCounter&0x7)<4)
      Data &= 0x7f;  // Remove top bit!
  }
*/
  return(Data);
}

//-----------------------------------------------------------------------
/*
  Toggle cursor emulation
*/
void Joy_ToggleCursorEmulation(void)
{
  // Toggle joystick 1 cursor emulation
  ConfigureParams.Joysticks.Joy[1].bCursorEmulation ^= TRUE;
  // Prevent both having emulation
  Joy_PreventBothUsingCursorEmulation();
}
