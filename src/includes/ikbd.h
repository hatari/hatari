/*
  Hatari
*/

// Keyboard Command
typedef struct {
  unsigned char Command;
  unsigned char NumParameters;
  void *pCallFunction;
} IKBD_COMMAND_PARAMS;

// Keyboard processor details
typedef struct {
  int X,Y;                      // Relative position of mouse
  int PrevX,PrevY;              // Previous position
} REL_MOUSE;

typedef struct {
  int X,Y;                      // Position of mouse
  int MaxX,MaxY;                // Max limits of mouse
  unsigned char PrevReadAbsMouseButtons;    // Previous button mask for 'IKBD_Cmd_ReadAbsMousePos'
} ABS_MOUSE;

typedef struct {
  int DeltaX,DeltaY;              // XY mouse position delta after scale according to resoution
  int XScale,YScale;              // Scale of mouse
  int XThreshold,YThreshold;      // Threshold
  unsigned char KeyCodeDeltaX,KeyCodeDeltaY;  // Delta X,Y for mouse keycode mode
  int YAxis;                      // Y-Axis direction
  unsigned char Action;           // Bit 0-Report abs position of press, Bit 1-Report abs on release
} MOUSE;

typedef struct {
  unsigned char JoyData[2];       // Joystick details
  unsigned char PrevJoyData[2];   // Previous joystick details, used to check for 'IKBD_SelAutoJoysticks'
} JOY;

typedef struct {
  REL_MOUSE  Rel;
  ABS_MOUSE  Abs;
  MOUSE    Mouse;
  JOY      Joy;
  int MouseMode;                  // AUTOMODE_xxxx
  int JoystickMode;               // AUTOMODE_xxxx
  BOOL bReset;                    // Set to TRUE is keyboard 'RESET' and now active
} KEYBOARD_PROCESSOR;

// Keyboard state
#define SIZE_KEYBOARD_BUFFER    1024            // Allow this many bytes to be stored in buffer(waiting to send to ACIA)
#define KEYBOARD_BUFFER_MASK    (SIZE_KEYBOARD_BUFFER-1)
#define SIZE_KEYBOARDINPUT_BUFFER  8
typedef struct {
  unsigned char KeyStates[256];                 // State of PC's keys, TRUE is down
  unsigned char Buffer[SIZE_KEYBOARD_BUFFER];   // Keyboard buffer
  int BufferHead,BufferTail;                    // Pointers into above buffer
  unsigned char InputBuffer[SIZE_KEYBOARDINPUT_BUFFER];  // Buffer for data send from CPU to keyboard processor(commands)
  int nBytesInInputBuffer;                      // Number of command bytes in above buffer

  int bLButtonDown,bRButtonDown;                // Mouse states in emulation system, BUTTON_xxxx
  int bOldLButtonDown,bOldRButtonDown;
  int LButtonDblClk,RButtonDblClk;
  int LButtonHistory,RButtonHistory;
} KEYBOARD;

// Button states, a bit mask so can mimick joystick/right mouse button duplication
#define BUTTON_NULL      0x00    // Button states, so can OR together mouse/joystick buttons
#define BUTTON_MOUSE    0x01
#define BUTTON_JOYSTICK    0x02

// Mouse/Joystick modes
#define AUTOMODE_OFF      0
#define AUTOMODE_MOUSEREL    1
#define AUTOMODE_MOUSEABS    2
#define AUTOMODE_MOUSECURSOR  3
#define AUTOMODE_JOYSTICK    4

// 0xfffc00(read status from ACIA)
#define ACIA_STATUS_REGISTER__RX_BUFFER_FULL  0x01
#define ACIA_STATUS_REGISTER__TX_BUFFER_EMPTY  0x02
#define ACIA_STATUS_REGISTER__OVERRUN_ERROR    0x40
#define ACIA_STATUS_REGISTER__INTERRUPT_REQUEST  0x80

extern unsigned char ACIAControlRegister;
extern unsigned char ACIAStatusRegister;
extern unsigned char ACIAByte;

extern KEYBOARD_PROCESSOR KeyboardProcessor;
extern KEYBOARD Keyboard;

extern void IKBD_Reset(BOOL bCold);
extern void IKBD_MemorySnapShot_Capture(BOOL bSave);
extern void IKBD_SendAutoKeyboardCommands();
extern void IKBD_InterruptHandler_ResetTimer(void);

extern void IKBD_Cmd_NullFunction(void);
extern void IKBD_Cmd_Reset(void);
extern void IKBD_Cmd_MouseAction(void);
extern void IKBD_Cmd_RelMouseMode(void);
extern void IKBD_Cmd_AbsMouseMode(void);
extern void IKBD_Cmd_MouseCursorKeycodes(void);
extern void IKBD_Cmd_SetMouseThreshold(void);
extern void IKBD_Cmd_SetMouseScale(void);
extern void IKBD_Cmd_ReadAbsMousePos(void);
extern void IKBD_Cmd_SetInternalMousePos(void);
extern void IKBD_Cmd_SetYAxisDown(void);
extern void IKBD_Cmd_SetYAxisUp(void);
extern void IKBD_Cmd_StartKeyboardTransfer(void);
extern void IKBD_Cmd_TurnMouseOff(void);
extern void IKBD_Cmd_StopKeyboardTransfer(void);
extern void IKBD_Cmd_ReturnJoystickAuto(void);
extern void IKBD_Cmd_StopJoystick(void);
extern void IKBD_Cmd_ReturnJoystick(void);
extern void IKBD_Cmd_SetJoystickDuration(void);
extern void IKBD_Cmd_SetJoystickFireDuration(void);
extern void IKBD_Cmd_SetCursorForJoystick(void);
extern void IKBD_Cmd_DisableJoysticks(void);
extern void IKBD_Cmd_SetClock(void);
extern void IKBD_Cmd_ReadClock(void);
extern void IKBD_Cmd_LoadMemory(void);
extern void IKBD_Cmd_ReadMemory(void);
extern void IKBD_Cmd_Execute(void);

extern void IKBD_SendByteToKeyboardProcessor(unsigned short bl);
extern unsigned short IKBD_GetByteFromACIA(void);
extern void IKBD_InterruptHandler_ACIA(void);
extern void IKBD_SendByteToACIA(void);
extern void IKBD_AddKeyToKeyboardBuffer(unsigned char Data);
extern void IKBD_PressSTKey(unsigned char ScanCode,BOOL bPress);
