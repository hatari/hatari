/*
  Hatari
*/

typedef struct {
  int XPos,YPos;                /* -32768...0...32767 */
  int Buttons;                  /* JOY_BUTTON1 */
} JOYREADING;

enum {
  JOYSTICK_SPACE_NULL,          /* Not up/down */
  JOYSTICK_SPACE_DOWN,
  JOYSTICK_SPACE_UP
};

#define  USE_FIREBUTTON_2_AS_SPACE      /* Enable PC Joystick button 2 to mimick space bar (For XenonII, Flying Shark etc...) */

#define JOYRANGE_UP_VALUE     -16384     /* Joystick ranges in XY */
#define JOYRANGE_DOWN_VALUE    16383
#define JOYRANGE_LEFT_VALUE   -16384
#define JOYRANGE_RIGHT_VALUE   16383

extern BOOL bJoystickWorking[2];
extern int JoystickSpaceBar;
extern int cursorJoyEmu;

extern void Joy_Init(void);
extern void Joy_PreventBothUsingCursorEmulation(void);
extern unsigned char Joy_GetStickData(unsigned int JoystickID);
extern void Joy_ToggleCursorEmulation(void);
