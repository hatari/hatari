/*
  Hatari - joy.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_JOY_H
#define HATARI_JOY_H

typedef struct
{
	int XPos,YPos;                /* -32768...0...32767 */
	int Buttons;                  /* JOY_BUTTON1 */
} JOYREADING;

enum
{
	JOYSTICK_SPACE_NULL,          /* Not up/down */
	JOYSTICK_SPACE_DOWN,
	JOYSTICK_SPACE_UP
};

enum
{
	JOYID_JOYSTICK0,
	JOYID_JOYSTICK1,
	JOYID_STEPADA,
	JOYID_STEPADB,
	JOYID_PARPORT1,
	JOYID_PARPORT2,
};

#define  USE_FIREBUTTON_2_AS_SPACE      /* Enable PC Joystick button 2 to mimick space bar (For XenonII, Flying Shark etc...) */

#define JOYRANGE_UP_VALUE     -16384     /* Joystick ranges in XY */
#define JOYRANGE_DOWN_VALUE    16383
#define JOYRANGE_LEFT_VALUE   -16384
#define JOYRANGE_RIGHT_VALUE   16383

extern int JoystickSpaceBar;

extern void Joy_Init(void);
extern Uint8 Joy_GetStickData(int nStJoyId);
extern void Joy_ToggleCursorEmulation(void);
extern BOOL Joy_KeyDown(int symkey, int modkey);
extern BOOL Joy_KeyUp(int symkey, int modkey);
extern void Joy_StePadButtons_ReadWord(void);
extern void Joy_StePadMulti_ReadWord(void);
extern void Joy_StePadMulti_WriteWord(void);

#endif /* ifndef HATARI_JOY_H */
