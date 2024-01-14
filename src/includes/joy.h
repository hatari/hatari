/*
  Hatari - joy.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_JOY_H
#define HATARI_JOY_H

enum
{
	JOYSTICK_SPACE_NULL,          /* Not up/down */
	JOYSTICK_SPACE_DOWN,
	JOYSTICK_SPACE_DOWNED,
	JOYSTICK_SPACE_UP
};

#define JOYRANGE_UP_VALUE     -16384     /* Joystick ranges in XY */
#define JOYRANGE_DOWN_VALUE    16383
#define JOYRANGE_LEFT_VALUE   -16384
#define JOYRANGE_RIGHT_VALUE   16383

#define ATARIJOY_BITMASK_UP    0x01
#define ATARIJOY_BITMASK_DOWN  0x02
#define ATARIJOY_BITMASK_LEFT  0x04
#define ATARIJOY_BITMASK_RIGHT 0x08
#define ATARIJOY_BITMASK_FIRE  0x80

extern int JoystickSpaceBar;

extern void Joy_Init(void);
extern void Joy_UnInit(void);
extern const char *Joy_GetName(int id);
extern int Joy_GetMaxId(void);
extern bool Joy_ValidateJoyId(int i);
extern uint8_t Joy_GetStickData(int nStJoyId);
extern bool Joy_SetCursorEmulation(int port);
extern void Joy_ToggleCursorEmulation(void);
extern bool Joy_SwitchMode(int port);
extern bool Joy_KeyDown(int symkey, int modkey);
extern bool Joy_KeyUp(int symkey, int modkey);
extern void Joy_StePadButtons_DIPSwitches_ReadWord(void);
extern void Joy_StePadButtons_DIPSwitches_WriteWord(void);
extern void Joy_StePadMulti_ReadWord(void);
extern void Joy_StePadMulti_WriteWord(void);
void Joy_SteLightpenX_ReadWord(void);
void Joy_SteLightpenY_ReadWord(void);
void Joy_StePadAnalog0X_ReadByte(void);
void Joy_StePadAnalog0Y_ReadByte(void);
void Joy_StePadAnalog1X_ReadByte(void);
void Joy_StePadAnalog1Y_ReadByte(void);

#endif /* ifndef HATARI_JOY_H */
