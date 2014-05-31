/*
  Hatari - ikbd.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_IKBD_H
#define HATARI_IKBD_H

/* Keyboard processor details */

typedef struct {
  int X,Y;                        /* Position of mouse */
  int MaxX,MaxY;                  /* Max limits of mouse */
  Uint8 PrevReadAbsMouseButtons;  /* Previous button mask for 'IKBD_Cmd_ReadAbsMousePos' */
} ABS_MOUSE;

typedef struct {
  int dx, dy;                     /* Mouse delta to be added */
  int DeltaX,DeltaY;              /* Final XY mouse position delta */
  int XScale,YScale;              /* Scale of mouse */
  int XThreshold,YThreshold;      /* Threshold */
  Uint8 KeyCodeDeltaX,KeyCodeDeltaY;    /* Delta X,Y for mouse keycode mode */
  int YAxis;                      /* Y-Axis direction */
  Uint8 Action;                   /* Bit 0-Report abs position of press, Bit 1-Report abs on release */
} MOUSE;

typedef struct {
  Uint8 JoyData[2];               /* Joystick details */
  Uint8 PrevJoyData[2];           /* Previous joystick details, used to check for 'IKBD_SendAutoJoysticks' */
} JOY;

typedef struct {
  ABS_MOUSE  Abs;
  MOUSE    Mouse;
  JOY      Joy;
  int MouseMode;                  /* AUTOMODE_xxxx */
  int JoystickMode;               /* AUTOMODE_xxxx */
} KEYBOARD_PROCESSOR;

/* Keyboard state */
#define KBD_MAX_SCANCODE          0x72
#define SIZE_KEYBOARD_BUFFER      1024    /* Allow this many bytes to be stored in buffer (waiting to send to ACIA) */
#define KEYBOARD_BUFFER_MASK      (SIZE_KEYBOARD_BUFFER-1)
#define SIZE_KEYBOARDINPUT_BUFFER 8
typedef struct {
  Uint8 KeyStates[KBD_MAX_SCANCODE + 1];	/* State of ST keys, TRUE is down */

  Uint8 Buffer[SIZE_KEYBOARD_BUFFER];		/* Keyboard output buffer */
  int BufferHead,BufferTail;			/* Pointers into above buffer */
  int NbBytesInOutputBuffer;			/* Number of bytes in output buffer */
  bool PauseOutput;				/* If true, don't send bytes anymore (see command 0x13) */

  Uint8 InputBuffer[SIZE_KEYBOARDINPUT_BUFFER];	/* Buffer for data send from CPU to keyboard processor (commands) */
  int nBytesInInputBuffer;			/* Number of command bytes in above buffer */

  int bLButtonDown,bRButtonDown;                /* Mouse states in emulation system, BUTTON_xxxx */
  int bOldLButtonDown,bOldRButtonDown;
  int LButtonDblClk,RButtonDblClk;
  int LButtonHistory,RButtonHistory;

  int AutoSendCycles;				/* Number of cpu cycles to call INTERRUPT_IKBD_AUTOSEND */
} KEYBOARD;

/* Button states, a bit mask so can mimick joystick/right mouse button duplication */
#define BUTTON_NULL      0x00     /* Button states, so can OR together mouse/joystick buttons */
#define BUTTON_MOUSE     0x01
#define BUTTON_JOYSTICK  0x02

/* Mouse/Joystick modes */
#define AUTOMODE_OFF			0
#define AUTOMODE_MOUSEREL		1
#define AUTOMODE_MOUSEABS		2
#define AUTOMODE_MOUSECURSOR		3
#define AUTOMODE_JOYSTICK		4
#define AUTOMODE_JOYSTICK_MONITORING	5

/* 0xfffc00 (read status from ACIA) */
#define ACIA_STATUS_REGISTER__RX_BUFFER_FULL  0x01
#define ACIA_STATUS_REGISTER__TX_BUFFER_EMPTY  0x02
#define ACIA_STATUS_REGISTER__OVERRUN_ERROR    0x20
#define ACIA_STATUS_REGISTER__INTERRUPT_REQUEST  0x80

extern KEYBOARD_PROCESSOR KeyboardProcessor;
extern KEYBOARD Keyboard;

extern void IKBD_Init ( void );
extern void IKBD_Reset(bool bCold);
extern void IKBD_MemorySnapShot_Capture(bool bSave);

extern void IKBD_InterruptHandler_ResetTimer(void);
extern void IKBD_InterruptHandler_AutoSend(void);

extern void IKBD_UpdateClockOnVBL ( void );


extern void IKBD_PressSTKey(Uint8 ScanCode, bool bPress);

#endif  /* HATARI_IKBD_H */
