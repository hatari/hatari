/*
  Hatari

  The keyboard processor(6301) handles any joystick/mouse task and sends bytes to the ACIA(6850)
  When a byte arrives in the ACIA (which takes just over 7000 CPU cycles) an MFP interrupt is flagged.
  The CPU can now read the byte from the ACIA by reading address $fffc02.
  An annoying bug can be found in Dungeon Master. This, when run, turns off the mouse input - but of course
  then you are unable to play the game! A bodge flag has been added so we need to be told twice to turn off
  the mouse input(although I think this causes errors in other games...)
  Also, the ACIA_CYCLES time is very important for games such as Carrier Command. The keyboard handler
  in this game has a bug in it, which corrupts its own registers if more than one byte is queued up. This
  value was found by a test program on a real ST and has correctly emulated the behaviour.
*/

#include "main.h"
#include "debug.h"
#include "decode.h"
#include "gemdos.h"
#include "ikbd.h"
#include "int.h"
#include "joy.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "misc.h"
#include "screen.h"
#include "video.h"
#include "vdi.h"

#define DBL_CLICK_HISTORY  0x07     /* Number of frames since last click to see if need to send one or two clicks */
#define ACIA_CYCLES    7200         /* Cycles(Multiple of 4) between sent to ACIA from keyboard along serial line - 500Hz/64, (approx' 6920-7200cycles from test program) */

#define IKBD_RESET_CYCLES  800000   /* Cycles after RESET before complete */

#define ABS_X_ONRESET    0          /* Initial XY for absolute mouse position after RESET command */
#define ABS_Y_ONRESET    0
#define ABS_MAX_X_ONRESET  320      /* Initial absolute mouse limits after RESET command */
#define ABS_MAY_Y_ONRESET  200      /* These values are never actually used as user MUST call 'IKBD_Cmd_AbsMouseMode' before ever using them */

#define ABS_PREVBUTTONS    (0x02|0x8)  /* Don't report any buttons up on first call to 'IKBD_Cmd_ReadAbsMousePos' */

#define SCALE_MOUSE_INPUT           /* Scale mouse so correct aspect ratio when in 320x200, 640x200, 640x400 */

/* Keyboard state */
KEYBOARD Keyboard;

/* Keyboard processor */
KEYBOARD_PROCESSOR KeyboardProcessor;   /* Keyboard processor details */
BOOL DoubleClickPattern[] = {           /* Pattern of mouse button up/down in ST frames (run off a double-click message) */
 BUTTON_MOUSE,BUTTON_MOUSE,BUTTON_MOUSE,BUTTON_MOUSE,
 0,0,0,0,BUTTON_MOUSE,BUTTON_MOUSE,BUTTON_MOUSE,BUTTON_MOUSE };
BOOL bMouseDisabled, bJoystickDisabled, bDuringResetCriticalTime;

/* ACIA */
unsigned char ACIAControlRegister = 0;
unsigned char ACIAStatusRegister = ACIA_STATUS_REGISTER__TX_BUFFER_EMPTY;  /* Pass when read 0xfffc00 */
unsigned char ACIAByte;                 /* When a byte has arrived at the ACIA(from the keyboard) it is stored here */
BOOL bByteInTransitToACIA = FALSE;      /* Is a byte being sent to the ACIA from the keyboard? */

/*
  6850 ACIA (Asynchronous Communications Inferface Apdater)
  Page 41, ST Internals. Also ST Update Magazine, February 1989 (I glad I kept that!)

  Pins:-
    Vss
    RX DATA Receive Data
    RX CLK Receive Clock
    TX CLK Transmitter Clock
    RTS Request To Send
    TX DATA Transmitter Data
    IRQ Interrupt Request
    CS 0,1,2 Chip Select
    RS Register Select
    Vcc Voltage
    R/W Read/Write
    E Enable
    D0-D7 Data
    DCD Data Carrier Detect
    CTS Clear To Send

  Registers:-
    0xfffc00 Keyboard ACIA Control (write)/Status(read)
    0xfffc02 Keyboard ACIA Data
    0xfffc04 MIDI ACIA Control (write)/Status(read)
    0xfffc06 MIDI ACIA Data

  Control Register (0xfffc00 write):-
    Bits 0,1 - These bits determine by which factor the transmitter and receiver
      clock will be divided. These bits also are joined with a master reset
      function. The 6850 has no separate reset line, so it must be
      accomplished though software.
        0 0    RXCLK/TXCLK without division
        0 1    RXCLK/TXCLK by 16 (MIDI)
        1 0    RXCLK/TXCLK by 64 (Keyboard)
        1 1    Master RESET
    Bits 2,3,4 - These so-called Word Select bits tell whether 7 or 8 data-bits are
      involved; whether 1 or 2 stop-bits are transferred; and the type of parity
    Bits 5,6 - These Transmitter Control bits set the RTS output pin, and allow or prevent
      an interrupt through the ACIA when the send register is emptied. Also, BREAK signals
      can be sent over the serial output by this line. A BREAK signal is nothing more than
      a long seqence of null bits
        0 0    RTS low, transmitter IRQ disabled
        0 1    RTS low, transmitter IRQ enabled
        1 0    RTS high, transmitter IRQ disabled
        1 1    RTS low, transmitter IRQ disabled, BREAK sent
    Bit 7 - The Receiver Interrupt Enable bit determines whether the receiver interrupt
      will be on. An interrupt can be caused by the DCD line chaning from low to high, or
      by the receiver data buffer filling. Besides that, an interrupt can occur from an
      OVERRUN ( a received character isn't properly read from the processior).
        0 Interrupt disabled
        1 Interrupt enabled

  Status Register (0xfffc00 read):-
    Bit 0 - When this bit is high, the RX data register is full. The byte must be read
      before a new character is received (otherwise an OVERRUN happens)
    Bit 1 - This bit reflects the status of the TX data buffer. An empty register
      set the bit.
    Bit 2 - A low-high change in pin DCD sets bit 2. If the receiver interrupt is allowable, the IRQ
      is cancelled. The bit is cleared when the status register and the receiver register are
      read. This also cancels the IRQ. Bit 2 register remains highis the signal on the DCD pin
      is still high; Bit 2 register low if DCD becomes low.
    Bit 3 - This line shows the status of CTS. This signal cannot be altered by a mater reset,
      or by ACIA programming.
    Bit 4 - Shows 'Frame Errors'. Frame errors are when no stop-bit is recognized in receiver
      switching. It can be set with every new character.
    Bit 5 - This bit display the previously mentioned OVERRUN condition. Bit 5 is reset when the
      RX buffer is read.
    Bit 6 - This bit recognizes whether the parity of a received character is correct. The bit is
      set on an error.
    Bit 7 - This signals the state of the IRQ pins; this bit make it possible to switch several
      IRQ lines on one interrupt input. In cases where an interrupt is program-generated, bit 7
      can tell which IC cut off the interrupt.
        
  ST ACIA:-
    Note CTS,DCD and RTS are not connected! Phew!
    The keyboard ACIA are address 0xfffc000 and 0xfffc02.
    Default parameters are :- 8-bit word, 1 stopbit, no parity, 77812.5 baud; 500KHz/64 (keyboard clock div)
    Default MIDI parameters are are above but :- 31250 baud; 500KHz/16 (MIDI clock div)
*/

/* List of possible keyboard commands, others are seen as NOPs by keyboard processor */
IKBD_COMMAND_PARAMS KeyboardCommands[] = {
  /* Known messages, counts include command byte */
  0x80,2,  IKBD_Cmd_Reset,
  0x07,2,  IKBD_Cmd_MouseAction,
  0x08,1,  IKBD_Cmd_RelMouseMode,
  0x09,5,  IKBD_Cmd_AbsMouseMode,
  0x0A,3,  IKBD_Cmd_MouseCursorKeycodes,
  0x0B,3,  IKBD_Cmd_SetMouseThreshold,
  0x0C,3,  IKBD_Cmd_SetMouseScale,
  0x0D,1,  IKBD_Cmd_ReadAbsMousePos,
  0x0E,6,  IKBD_Cmd_SetInternalMousePos,
  0x0F,1,  IKBD_Cmd_SetYAxisDown,
  0x10,1,  IKBD_Cmd_SetYAxisUp,
  0x11,1,  IKBD_Cmd_StartKeyboardTransfer,
  0x12,1,  IKBD_Cmd_TurnMouseOff,
  0x13,1,  IKBD_Cmd_StopKeyboardTransfer,
  0x14,1,  IKBD_Cmd_ReturnJoystickAuto,
  0x15,1,  IKBD_Cmd_StopJoystick,
  0x16,1,  IKBD_Cmd_ReturnJoystick,
  0x17,2,  IKBD_Cmd_SetJoystickDuration,
  0x18,1,  IKBD_Cmd_SetJoystickFireDuration,
  0x19,7,  IKBD_Cmd_SetCursorForJoystick,
  0x1A,1,  IKBD_Cmd_DisableJoysticks,
  0x1B,7,  IKBD_Cmd_SetClock,
  0x1C,1,  IKBD_Cmd_ReadClock,
  0x20,4,  IKBD_Cmd_LoadMemory,
  0x21,3,  IKBD_Cmd_ReadMemory,
  0x22,3,  IKBD_Cmd_Execute,

  /* Report message (top bit set) - ignore for now... */
  0x88,1,  IKBD_Cmd_NullFunction,
  0x89,1,  IKBD_Cmd_NullFunction,
  0x8A,1,  IKBD_Cmd_NullFunction,
  0x8B,1,  IKBD_Cmd_NullFunction,
  0x8C,1,  IKBD_Cmd_NullFunction,
  0x8F,1,  IKBD_Cmd_NullFunction,
  0x90,1,  IKBD_Cmd_NullFunction,
  0x92,1,  IKBD_Cmd_NullFunction,
  0x94,1,  IKBD_Cmd_NullFunction,
  0x95,1,  IKBD_Cmd_NullFunction,
  0x99,1,  IKBD_Cmd_NullFunction,

  0xFF  /* Term */
};


/*-----------------------------------------------------------------------*/
/*
  Reset the IKBD processor
*/
void IKBD_Reset(BOOL bCold)
{
  /* Reset internal keyboard processor details */
  if (bCold)
    KeyboardProcessor.bReset = FALSE;
  KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
  KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK;

  KeyboardProcessor.Rel.X =  KeyboardProcessor.Rel.Y = 0;
  KeyboardProcessor.Rel.PrevX = KeyboardProcessor.Rel.PrevY = 0;
  KeyboardProcessor.Abs.X = ABS_X_ONRESET;  KeyboardProcessor.Abs.Y = ABS_Y_ONRESET;
  KeyboardProcessor.Abs.MaxX = ABS_MAX_X_ONRESET;  KeyboardProcessor.Abs.MaxY = ABS_MAY_Y_ONRESET;
  KeyboardProcessor.Abs.PrevReadAbsMouseButtons = ABS_PREVBUTTONS;

  KeyboardProcessor.Mouse.DeltaX = KeyboardProcessor.Mouse.DeltaY = 0;
  KeyboardProcessor.Mouse.XScale = KeyboardProcessor.Mouse.YScale = 0;
  KeyboardProcessor.Mouse.XThreshold = KeyboardProcessor.Mouse.YThreshold = 1;
  KeyboardProcessor.Mouse.YAxis = 1;          /* Y origin at top */
  KeyboardProcessor.Mouse.Action = 0;

  KeyboardProcessor.Joy.PrevJoyData[0] = KeyboardProcessor.Joy.PrevJoyData[1] = 0;

  /* Reset our ACIA status */
  bByteInTransitToACIA = FALSE;
  ACIAControlRegister = 0;
  ACIAStatusRegister = ACIA_STATUS_REGISTER__TX_BUFFER_EMPTY;
  /* And our keyboard states and clear key state table */
  Keyboard.BufferHead = Keyboard.BufferTail = 0;
  Keyboard.nBytesInInputBuffer = 0;
  Memory_Clear(Keyboard.KeyStates,sizeof(Keyboard.KeyStates));
  Keyboard.bLButtonDown = BUTTON_NULL;
  Keyboard.bRButtonDown = BUTTON_NULL;
  Keyboard.bOldLButtonDown = Keyboard.bOldRButtonDown = BUTTON_NULL;
  Keyboard.LButtonDblClk = Keyboard.RButtonDblClk = 0;
  Keyboard.LButtonHistory = Keyboard.RButtonHistory = 0;

  /* Store BOOL for when disable mouse or joystick - do emulate hardware 'quirk' where */
  /* if disable both with 'x' time of a RESET command they are ignored! */
  bMouseDisabled = bJoystickDisabled = bDuringResetCriticalTime = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void IKBD_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  MemorySnapShot_Store(&Keyboard,sizeof(Keyboard));
  MemorySnapShot_Store(&KeyboardProcessor,sizeof(KeyboardProcessor));
  MemorySnapShot_Store(&ACIAControlRegister,sizeof(ACIAControlRegister));
  MemorySnapShot_Store(&ACIAStatusRegister,sizeof(ACIAStatusRegister));
  MemorySnapShot_Store(&ACIAByte,sizeof(ACIAByte));
  MemorySnapShot_Store(&bByteInTransitToACIA,sizeof(bByteInTransitToACIA));
}


/*-----------------------------------------------------------------------*/
/*
  Calculate out 'delta' that mouse has moved by each frame, and add this to our internal keyboard position
*/
void IKBD_UpdateInternalMousePosition(void)
{
  BOOL bHalveX=FALSE,bHalveY=FALSE;

#ifdef SCALE_MOUSE_INPUT
  /* According to chosen resolution, halve XY axis to give smoother mouse movement! */
  /* When using VDI or mono leave mouse as is */
  if (!bUseVDIRes) {
    if (STRes==ST_LOW_RES) {
      bHalveX = bHalveY = TRUE;
    }
    if ( (STRes==ST_MEDIUM_RES) || (STRes==ST_LOWMEDIUM_MIX_RES) ) {
      bHalveY = TRUE;
    }
  }

  if (bHalveX)
    KeyboardProcessor.Mouse.DeltaX = (KeyboardProcessor.Rel.X-KeyboardProcessor.Rel.PrevX)>>1;
  else
    KeyboardProcessor.Mouse.DeltaX = KeyboardProcessor.Rel.X-KeyboardProcessor.Rel.PrevX;
  if (bHalveY)
    KeyboardProcessor.Mouse.DeltaY = (KeyboardProcessor.Rel.Y-KeyboardProcessor.Rel.PrevY)>>1;
  else
    KeyboardProcessor.Mouse.DeltaY = KeyboardProcessor.Rel.Y-KeyboardProcessor.Rel.PrevY;
#else
  KeyboardProcessor.Mouse.DeltaX = KeyboardProcessor.Rel.X-KeyboardProcessor.Rel.PrevX;
  KeyboardProcessor.Mouse.DeltaY = KeyboardProcessor.Rel.Y-KeyboardProcessor.Rel.PrevY;
#endif
        /* Accellerating mouse in ST-Low - is this a good idea? */
        if(!bUseHighRes) {  KeyboardProcessor.Mouse.DeltaX*=2; KeyboardProcessor.Mouse.DeltaY*=2;  }

  /* Retain fraction for next time around!?? */
  if (bHalveX)
    KeyboardProcessor.Rel.PrevX = KeyboardProcessor.Rel.X&~0x1;
  else
    KeyboardProcessor.Rel.PrevX = KeyboardProcessor.Rel.X;
  if (bHalveY)
    KeyboardProcessor.Rel.PrevY = KeyboardProcessor.Rel.Y&~0x1;
  else
    KeyboardProcessor.Rel.PrevY = KeyboardProcessor.Rel.Y;

  /* Update internal mouse coords - Y axis moves according to YAxis setting(up/down) */
  /* Limit to Max X/Y(inclusive) */
  KeyboardProcessor.Abs.X += KeyboardProcessor.Mouse.DeltaX;
  if (KeyboardProcessor.Abs.X<0)
    KeyboardProcessor.Abs.X = 0;
  if (KeyboardProcessor.Abs.X>KeyboardProcessor.Abs.MaxX)
    KeyboardProcessor.Abs.X = KeyboardProcessor.Abs.MaxX;
  KeyboardProcessor.Abs.Y += KeyboardProcessor.Mouse.DeltaY*KeyboardProcessor.Mouse.YAxis;  /* Needed '+' for Falcon... */
  if (KeyboardProcessor.Abs.Y<0)
    KeyboardProcessor.Abs.Y = 0;
  if (KeyboardProcessor.Abs.Y>KeyboardProcessor.Abs.MaxY)
    KeyboardProcessor.Abs.Y = KeyboardProcessor.Abs.MaxY;
}


/*-----------------------------------------------------------------------*/
/*
  When running in maximum speed the emulation will not see 'double-clicks' of the mouse
  as it is running so fast. In this case, we check for a Windows double-click and pass
  the 'up'/'down' messages in emulation time to simulate the double-click effect!
*/
void IKBD_CheckForDoubleClicks(void)
{
  // OK, our Window responds to double-clicks but this sends the WM_xxxxx messages:-
  // WM_LBUTTONDOWN,WM_LBUTTONUP,WM_LBUTTONDBLCLK,WM_LBUTTONUP. When running emulation
  // in normal speed we simply interpret the WM_LBUTTONDBLCLK as a WM_LBUTTONDOWN and
  // all runs well. Things get a little complicated when running max speed as a normal
  // double-click is a load of 1's, followed by 0's, 1's and 0's - But the ST does
  // not see this as a double click as the space in 'ST' time between changes is so great.
  // Now, when we see a WM_LBUTTONDBLCLK in max speed we actually send the down/up/down/up
  // in ST time. To get this correct(and not send three clicks) we look in a history buffer
  // and start at an index which gives the correct number of clicks! Phew!

  /* Handle double clicks!!! */
  if (Keyboard.LButtonDblClk) {
    if (Keyboard.LButtonDblClk==1) {          /* First pressed! */
      if ((Keyboard.LButtonHistory&0x3f)==0)  /* If not pressed button in long time do full dbl-click pattern */
        Keyboard.LButtonDblClk = 1;
      else {
        Keyboard.LButtonDblClk = 4;           /* Otherwise, check where to begin to give 1111000011110000 pattern */
        if ((Keyboard.LButtonHistory&0x7)==0) 
          Keyboard.LButtonDblClk = 8;
        else if ((Keyboard.LButtonHistory&0x3)==0) 
          Keyboard.LButtonDblClk = 7;
        else if ((Keyboard.LButtonHistory&0x1)==0) 
          Keyboard.LButtonDblClk = 6;
      }
    }

    Keyboard.bLButtonDown = DoubleClickPattern[Keyboard.LButtonDblClk];
    Keyboard.LButtonDblClk++;
    if (Keyboard.LButtonDblClk>=13) {         /* Check for end of sequence */
      Keyboard.LButtonDblClk = 0;
      Keyboard.bLButtonDown = FALSE;
    }
  }
  if (Keyboard.RButtonDblClk) {
    if (Keyboard.RButtonDblClk==1) {          /* First pressed! */
      if ((Keyboard.RButtonHistory&0x3f)==0)  /* If not pressed button in long time do full dbl-click pattern */
        Keyboard.RButtonDblClk = 1;
      else {
        Keyboard.RButtonDblClk = 4;           /* Otherwise, check where to begin to give 1111000011110000 pattern */
        if ((Keyboard.RButtonHistory&0x7)==0) 
          Keyboard.RButtonDblClk = 8;
        else if ((Keyboard.RButtonHistory&0x3)==0) 
          Keyboard.RButtonDblClk = 7;
        else if ((Keyboard.RButtonHistory&0x1)==0) 
          Keyboard.RButtonDblClk = 6;
      }
    }

    Keyboard.bRButtonDown = DoubleClickPattern[Keyboard.RButtonDblClk];
    Keyboard.RButtonDblClk++;
    if (Keyboard.RButtonDblClk>=13) {         /* Check for end of sequence */
      Keyboard.RButtonDblClk = 0;
      Keyboard.bRButtonDown = FALSE;
    }
  }

  /* Store presses into history */
  Keyboard.LButtonHistory = (Keyboard.LButtonHistory<<1);
  if (Keyboard.bLButtonDown)
    Keyboard.LButtonHistory |= 0x1;
  Keyboard.RButtonHistory = (Keyboard.RButtonHistory<<1);
  if (Keyboard.bRButtonDown)
    Keyboard.RButtonHistory |= 0x1;
}


/*-----------------------------------------------------------------------*/
/*
  Convert button to BOOL value
*/
BOOL IKBD_ButtonBool(int Button)
{
  /* Button pressed? */
  if (Button)
    return(TRUE);
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Return TRUE if buttons match, use this as buttons are a mask and not BOOLean
*/
BOOL IKBD_ButtonsEqual(int Button1,int Button2)
{
  /* Return BOOL compare */
  return(IKBD_ButtonBool(Button1)==IKBD_ButtonBool(Button2));
}


/*-----------------------------------------------------------------------*/
/*
  According to if the mouse if enabled or not the joystick 1 fire button/right mouse button
  will become the same button, ie pressing one will also press the other and vise-versa
*/
void IKBD_DuplicateMouseFireButtons(void)
{
  /* If mouse is off then joystick fire button goes to joystick */
  if (KeyboardProcessor.MouseMode==AUTOMODE_OFF) {
    /* If pressed right mouse button, should go to joystick 1 */
    if (Keyboard.bRButtonDown&BUTTON_MOUSE)
      KeyboardProcessor.Joy.JoyData[1] |= 0x80;
    /* And left mouse button, should go to joystick 0 */
    if (Keyboard.bLButtonDown&BUTTON_MOUSE)
      KeyboardProcessor.Joy.JoyData[0] |= 0x80;
  }
  /* If mouse if on, joystick 1 fire button goes to mouse not to the joystick */
  else {
    /* Is fire button pressed? */
    if (KeyboardProcessor.Joy.JoyData[1]&0x80) {
      KeyboardProcessor.Joy.JoyData[1] &= 0x7f;  /* Clear fire button bit */
      Keyboard.bRButtonDown |= BUTTON_JOYSTICK;  /* Mimick on mouse right button */
    }
    else
      Keyboard.bRButtonDown &= ~BUTTON_JOYSTICK;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Send 'relative' mouse position
*/
void IKBD_SendRelMousePacket(void)
{
  int ByteRelX,ByteRelY;
  unsigned char Header;

  if ( (KeyboardProcessor.Mouse.DeltaX!=0) || (KeyboardProcessor.Mouse.DeltaY!=0)
   || (!IKBD_ButtonsEqual(Keyboard.bOldLButtonDown,Keyboard.bLButtonDown)) || (!IKBD_ButtonsEqual(Keyboard.bOldRButtonDown,Keyboard.bRButtonDown)) ) {
    /* Send packet to keyboard process */
    while(TRUE) {
      ByteRelX = KeyboardProcessor.Mouse.DeltaX;
      if (ByteRelX>127)  ByteRelX = 127;
      if (ByteRelX<-128)  ByteRelX = -128;
      ByteRelY = KeyboardProcessor.Mouse.DeltaY;
      if (ByteRelY>127)  ByteRelY = 127;
      if (ByteRelY<-128)  ByteRelY = -128;

      Header = 0xf8;
      if (Keyboard.bLButtonDown)
        Header |= 0x02;
      if (Keyboard.bRButtonDown)
        Header |= 0x01;
      IKBD_AddKeyToKeyboardBuffer(Header);
      IKBD_AddKeyToKeyboardBuffer(ByteRelX);
      IKBD_AddKeyToKeyboardBuffer(ByteRelY*KeyboardProcessor.Mouse.YAxis);

      KeyboardProcessor.Mouse.DeltaX -= ByteRelX;
      KeyboardProcessor.Mouse.DeltaY -= ByteRelY;

      if ( (KeyboardProcessor.Mouse.DeltaX==0) && (KeyboardProcessor.Mouse.DeltaY==0) )
        break;

      /* Store buttons for next time around */
      Keyboard.bOldLButtonDown = Keyboard.bLButtonDown;
      Keyboard.bOldRButtonDown = Keyboard.bRButtonDown;
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Send 'joysticks' bit masks
*/
void IKBD_SelAutoJoysticks(void)
{
  unsigned char JoyData;

  /* Did joystick 0/mouse change? */
  JoyData = KeyboardProcessor.Joy.JoyData[0];
  if (JoyData!=KeyboardProcessor.Joy.PrevJoyData[0]) {
    IKBD_AddKeyToKeyboardBuffer(0xFE);    /* Joystick 0/Mouse */
    IKBD_AddKeyToKeyboardBuffer(JoyData);

    KeyboardProcessor.Joy.PrevJoyData[0] = JoyData;
  }

  /* Did joystick 1(default) change? */
  JoyData = KeyboardProcessor.Joy.JoyData[1];
  if (JoyData!=KeyboardProcessor.Joy.PrevJoyData[1]) {
    IKBD_AddKeyToKeyboardBuffer(0xFF);    /* Joystick 1 */
    IKBD_AddKeyToKeyboardBuffer(JoyData);

    KeyboardProcessor.Joy.PrevJoyData[1] = JoyData;
  }
}

/*-----------------------------------------------------------------------*/
/*
  Send packets which are generated from the mouse action settings
  If relative mode is on, still generate these packets
*/
void IKBD_SendOnMouseAction(void)
{
  BOOL bReportPosition = FALSE;

  /* Report buttons as keys? Do in relative/absolute mode */
  if (KeyboardProcessor.Mouse.Action&0x4) {
    /* Left button? */
    if ( (IKBD_ButtonBool(Keyboard.bLButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldLButtonDown))) )
      IKBD_AddKeyToKeyboardBuffer(0x74);    /* Left */
    else if ( (IKBD_ButtonBool(Keyboard.bOldLButtonDown) && (!IKBD_ButtonBool(Keyboard.bLButtonDown))) )
      IKBD_AddKeyToKeyboardBuffer(0x74|0x80);
    /* Right button? */
    if ( (IKBD_ButtonBool(Keyboard.bRButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldRButtonDown))) )
      IKBD_AddKeyToKeyboardBuffer(0x75);    /* Right */
    else if ( (IKBD_ButtonBool(Keyboard.bOldRButtonDown) && (!IKBD_ButtonBool(Keyboard.bRButtonDown))) )
      IKBD_AddKeyToKeyboardBuffer(0x75|0x80);

    /* Ignore bottom two bits, so return now */
    return;
  }

  /* Check MouseAction - report position on press/release */
  /* MUST do this before update relative positions as buttons get reset */
  if (KeyboardProcessor.Mouse.Action&0x3) {
    /* Check for 'press'? */
    if (KeyboardProcessor.Mouse.Action&0x1) {
      /* Did 'press' mouse buttons? */
      if ( (IKBD_ButtonBool(Keyboard.bLButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldLButtonDown))) ) {
        bReportPosition = TRUE;
        KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x04;
        KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x02;
      }
      if ( (IKBD_ButtonBool(Keyboard.bRButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldRButtonDown))) ) {
        bReportPosition = TRUE;
        KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x01;
        KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x08;
      }
    }
    /* Check for 'release'? */
    if (KeyboardProcessor.Mouse.Action&0x2) {
      /* Did 'release' mouse buttons? */
      if ( (IKBD_ButtonBool(Keyboard.bOldLButtonDown) && (!IKBD_ButtonBool(Keyboard.bLButtonDown))) ) {
        bReportPosition = TRUE;
        KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x08;
        KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x01;
      }
      if ( (IKBD_ButtonBool(Keyboard.bOldRButtonDown) && (!IKBD_ButtonBool(Keyboard.bRButtonDown))) ) {
        bReportPosition = TRUE;
        KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x02;
        KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x04;
      }
    }

    /* Do need to report? */
    if (bReportPosition) {
      /* Only report if mouse in absolute mode */
      if (KeyboardProcessor.MouseMode==AUTOMODE_MOUSEABS) {
#ifdef DEBUG_OUTPUT_IKBD
        Debug_IKBD("Report ABS on MouseAction\n");
#endif
        IKBD_Cmd_ReadAbsMousePos();
      }
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Send mouse movements as cursor keys
*/
void IKBD_SendCursorMousePacket(void)
{
  int i=0;

  /* Run each 'Delta' as cursor presses */
  /* Limit to '10' loops as Windows cursor is a VERY poor quality. Eg, a single mouse movement */
  /* on and ST gives delta's of '1', mostly, but Windows goes as high as 20+! */
  while ( (i<10) && ((KeyboardProcessor.Mouse.DeltaX!=0) || (KeyboardProcessor.Mouse.DeltaY!=0)
   || (!IKBD_ButtonsEqual(Keyboard.bOldLButtonDown,Keyboard.bLButtonDown)) || (!IKBD_ButtonsEqual(Keyboard.bOldRButtonDown,Keyboard.bRButtonDown))) ) {
    /* Left? */
    if (KeyboardProcessor.Mouse.DeltaX<0) {
      IKBD_AddKeyToKeyboardBuffer(75);    /* Left cursor */
      IKBD_AddKeyToKeyboardBuffer(75|0x80);
      KeyboardProcessor.Mouse.DeltaX++;
    }
    /* Right? */
    if (KeyboardProcessor.Mouse.DeltaX>0) {
      IKBD_AddKeyToKeyboardBuffer(77);    /* Right cursor */
      IKBD_AddKeyToKeyboardBuffer(77|0x80);
      KeyboardProcessor.Mouse.DeltaX--;
    }
    /* Up? */
    if (KeyboardProcessor.Mouse.DeltaY<0) {
      IKBD_AddKeyToKeyboardBuffer(72);    /* Up cursor */
      IKBD_AddKeyToKeyboardBuffer(72|0x80);
      KeyboardProcessor.Mouse.DeltaY++;
    }
    /* Down? */
    if (KeyboardProcessor.Mouse.DeltaY>0) {
      IKBD_AddKeyToKeyboardBuffer(80);    /* Down cursor */
      IKBD_AddKeyToKeyboardBuffer(80|0x80);
      KeyboardProcessor.Mouse.DeltaY--;
    }

    /* Left button? */
    if ( (IKBD_ButtonBool(Keyboard.bLButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldLButtonDown))) )
      IKBD_AddKeyToKeyboardBuffer(0x74);    /* Left */
    else if ( (IKBD_ButtonBool(Keyboard.bOldLButtonDown) && (!IKBD_ButtonBool(Keyboard.bLButtonDown))) )
      IKBD_AddKeyToKeyboardBuffer(0x74|0x80);
    /* Right button? */
    if ( (IKBD_ButtonBool(Keyboard.bRButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldRButtonDown))) )
      IKBD_AddKeyToKeyboardBuffer(0x75);    /* Right */
    else if ( (IKBD_ButtonBool(Keyboard.bOldRButtonDown) && (!IKBD_ButtonBool(Keyboard.bRButtonDown))) )
      IKBD_AddKeyToKeyboardBuffer(0x75|0x80);

    Keyboard.bOldLButtonDown = Keyboard.bLButtonDown;
    Keyboard.bOldRButtonDown = Keyboard.bRButtonDown;

    /* Count, so exit if try too many times! */
    i++;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Return packets from keyboard for auto, rel mouse, joystick etc...
*/
void IKBD_SendAutoKeyboardCommands(void)
{
  /* Ignore anything until we've redirected our GEM handlers */
//FM  if (!bInitGemDOS)
//FIXME    return;

  /* Do not send auto commands directly after a reset command. */
  /* I hope that it is okay that I added this here - Thothy */
  if( bDuringResetCriticalTime )
    return;

  /* Don't do anything until processor is first reset */
  if (!KeyboardProcessor.bReset)
    return;

  /* Read joysticks for this frame */
  /* If mouse is on, joystick 0 is not connected */
  KeyboardProcessor.Joy.JoyData[0] = (KeyboardProcessor.MouseMode==AUTOMODE_OFF) ? Joy_GetStickData(0):0x00;
  KeyboardProcessor.Joy.JoyData[1] = Joy_GetStickData(1);

  /* Check for double-clicks in maximum speed mode */
  IKBD_CheckForDoubleClicks();

  /* Handle Joystick/Mouse fire buttons */
  IKBD_DuplicateMouseFireButtons();
  
  /* Send any packets which are to be reported by mouse action */
  IKBD_SendOnMouseAction();

  /* Update internal mouse absolute position by find 'delta' of mouse movement */
  IKBD_UpdateInternalMousePosition();

  /* Send automatic joystick packets */
  if (KeyboardProcessor.JoystickMode==AUTOMODE_JOYSTICK)
    IKBD_SelAutoJoysticks();
  /* Send automatic relative mouse positions(absolute are not send automatically) */
  if (KeyboardProcessor.MouseMode==AUTOMODE_MOUSEREL)
    IKBD_SendRelMousePacket();
  /* Send cursor key directions for movements */
  else if (KeyboardProcessor.MouseMode==AUTOMODE_MOUSECURSOR)
    IKBD_SendCursorMousePacket();

  /* Store buttons for next time around */
  Keyboard.bOldLButtonDown = Keyboard.bLButtonDown;
  Keyboard.bOldRButtonDown = Keyboard.bRButtonDown;

  /* Send joystick button '2' as 'Space bar' key - MUST do here so does not get mixed up in middle of joystick packets! */
  if (JoystickSpaceBar) {
    /* As we simulating space bar? */
    if (JoystickSpaceBar==JOYSTICK_SPACE_DOWN) {
      IKBD_PressSTKey(57,TRUE);         /* Press */
      JoystickSpaceBar = JOYSTICK_SPACE_UP;
    }
    else { //if (JoystickSpaceBar==JOYSTICK_SPACE_UP) {
      IKBD_PressSTKey(57,FALSE);        /* Release */
      JoystickSpaceBar = FALSE;         /* Complete */
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  On ST if disable Mouse AND Joystick with a set time of a RESET command they are
  actually turned back on! (A number of games do this so can get mouse and joystick
  packets at the same time)
*/
void IKBD_CheckResetDisableBug(void)
{
  /* Have disabled BOTH mouse and joystick? */
  if (bMouseDisabled && bJoystickDisabled) {
    /* And in critical time? */
    if (bDuringResetCriticalTime) {
      /* Emulate relative mouse and joystick reports being turned back on */
      KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
      KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK;
#ifdef DEBUG_OUTPUT_IKBD
      Debug_IKBD("IKBD Mouse+Joystick disabled during RESET. Revert.\n");
      Debugger_TabIKBD_AddListViewItem("( Mouse+Joystick disabled during RESET. Revert. )");
#endif
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Start timer after keyboard RESET command to emulate 'quirk'
  If some IKBD commands are sent during time after a RESET they may be ignored
*/
void IKBD_InterruptHandler_ResetTimer(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  /* Critical timer is over */
  bDuringResetCriticalTime = FALSE;
}



/*-----------------------------------------------------------------------*/
/*
  List of keyboard commands
*/


/*-----------------------------------------------------------------------*/
/*
  Blank function for some keyboard commands - this can be used to find errors
*/
void IKBD_Cmd_NullFunction(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_NullFunction\n");
  Debugger_TabIKBD_AddListViewItem("( NullFunction )");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  RESET

  0x80
  0x01

  Performs self test and checks for stuck (closed) keys, if OK returns 0xF0. Otherwise
  returns break codes for keys
*/
void IKBD_Cmd_Reset(void)
{
  /* Check for error series of bytes, eg 0x80,0x01 */
  if (Keyboard.InputBuffer[1]==0x01) {
#ifdef DEBUG_OUTPUT_IKBD
    Debug_IKBD("KEYBOARD ON\n");
#endif
    KeyboardProcessor.bReset = TRUE;      /* Turn processor on; can now process commands */

    /* Set defaults */
    KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
    KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK;
    KeyboardProcessor.Abs.X = ABS_X_ONRESET;  KeyboardProcessor.Abs.Y = ABS_Y_ONRESET;
    KeyboardProcessor.Abs.MaxX = ABS_MAX_X_ONRESET;  KeyboardProcessor.Abs.MaxY = ABS_MAY_Y_ONRESET;
    KeyboardProcessor.Abs.PrevReadAbsMouseButtons = ABS_PREVBUTTONS;

    IKBD_AddKeyToKeyboardBuffer(0xF0);    /* Assume OK, return correct code */

    /* Start timer - some commands are send during this time they may be ignored(see real ST!) */
    Int_AddRelativeInterrupt(IKBD_RESET_CYCLES,INTERRUPT_IKBD_RESETTIMER);
    /* Set this 'critical' flag, gets reset when timer expires */
    bMouseDisabled = bJoystickDisabled = FALSE;
    bDuringResetCriticalTime = TRUE;
  }
  /* else if not 0x80,0x01 just ignore */
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_Reset\n");
  Debugger_TabIKBD_AddListViewItem("RESET");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET MOUSE BUTTON ACTION

  0x07
  %00000mss  ; mouse button action
        ;  (m is presumed =1 when in MOUSE KEYCODE mode)
        ; mss=0xy, mouse button press or release causes mouse
        ;  position report
        ;  where y=1, mouse key press causes absolute report
        ;  and x=1, mouse key release causes absolute report
        ; mss=100, mouse buttons act like keys 
*/
void IKBD_Cmd_MouseAction(void)
{
  KeyboardProcessor.Mouse.Action = Keyboard.InputBuffer[1];
  KeyboardProcessor.Abs.PrevReadAbsMouseButtons = ABS_PREVBUTTONS;
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_MouseAction %d\n",(unsigned int)KeyboardProcessor.Mouse.Action);
  Debugger_TabIKBD_AddListViewItem("MouseAction %d",(unsigned int)KeyboardProcessor.Mouse.Action);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET RELATIVE MOUSE POSITION REPORTING

  0x08
*/
void IKBD_Cmd_RelMouseMode(void)
{
  KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_RelMouseMode\n");
  Debugger_TabIKBD_AddListViewItem("RelMouseMode");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET ABSOLUTE MOUSE POSITIONING

  0x09
  XMSB      ;X maximum (in scaled mouse clicks)
  XLSB
  YMSB      ;Y maximum (in scaled mouse clicks)
  YLSB
*/
void IKBD_Cmd_AbsMouseMode(void)
{
  /* These maximums are 'inclusive' */
  KeyboardProcessor.MouseMode = AUTOMODE_MOUSEABS;
  KeyboardProcessor.Abs.MaxX = (((unsigned int)Keyboard.InputBuffer[1])<<8) | (unsigned int)Keyboard.InputBuffer[2];
  KeyboardProcessor.Abs.MaxY = (((unsigned int)Keyboard.InputBuffer[3])<<8) | (unsigned int)Keyboard.InputBuffer[4];
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_AbsMouseMode %d,%d\n",KeyboardProcessor.Abs.MaxX,KeyboardProcessor.Abs.MaxY);
  Debugger_TabIKBD_AddListViewItem("AbsMouseMode %d,%d",KeyboardProcessor.Abs.MaxX,KeyboardProcessor.Abs.MaxY);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET MOUSE KEYCODE MODE

  0x0A
  deltax      ; distance in X clicks to return (LEFT) or (RIGHT)
  deltay      ; distance in Y clicks to return (UP) or (DOWN)
*/
void IKBD_Cmd_MouseCursorKeycodes(void)
{
  KeyboardProcessor.MouseMode = AUTOMODE_MOUSECURSOR;
  KeyboardProcessor.Mouse.KeyCodeDeltaX = Keyboard.InputBuffer[1];
  KeyboardProcessor.Mouse.KeyCodeDeltaY = Keyboard.InputBuffer[2];  
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_MouseCursorKeycodes %d,%d\n",(int)KeyboardProcessor.Mouse.KeyCodeDeltaX,(int)KeyboardProcessor.Mouse.KeyCodeDeltaY);
  Debugger_TabIKBD_AddListViewItem("MouseCursorKeycodes %d,%d",(int)KeyboardProcessor.Mouse.KeyCodeDeltaX,(int)KeyboardProcessor.Mouse.KeyCodeDeltaY);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET MOUSE THRESHOLD

  0x0B
  X      ; x threshold in mouse ticks (positive integers)
  Y      ; y threshold in mouse ticks (positive integers)
*/
void IKBD_Cmd_SetMouseThreshold(void)
{
  KeyboardProcessor.Mouse.XThreshold = (unsigned int)Keyboard.InputBuffer[1];
  KeyboardProcessor.Mouse.YThreshold = (unsigned int)Keyboard.InputBuffer[2];
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_SetMouseThreshold %d,%d\n",KeyboardProcessor.Mouse.XThreshold,KeyboardProcessor.Mouse.YThreshold);
  Debugger_TabIKBD_AddListViewItem("SetMouseThreshold %d,%d",KeyboardProcessor.Mouse.XThreshold,KeyboardProcessor.Mouse.YThreshold);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET MOUSE SCALE

  0x0C
  X      ; horizontal mouse ticks per internel X
  Y      ; vertical mouse ticks per internel Y
*/
void IKBD_Cmd_SetMouseScale(void)
{
  KeyboardProcessor.Mouse.XScale = (unsigned int)Keyboard.InputBuffer[1];
  KeyboardProcessor.Mouse.YScale = (unsigned int)Keyboard.InputBuffer[2];
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_SetMouseScale %d,%d\n",KeyboardProcessor.Mouse.XScale,KeyboardProcessor.Mouse.YScale);
  Debugger_TabIKBD_AddListViewItem("SetMouseScale %d,%d",KeyboardProcessor.Mouse.XScale,KeyboardProcessor.Mouse.YScale);
#endif
}

/*-----------------------------------------------------------------------*/
/*
  INTERROGATE MOUSE POSITION

  0x0D
    Returns:  0xF7  ; absolute mouse position header
      BUTTONS
        0000dcba
        where a is right button down since last interrogation
        b is right button up since last
        c is left button down since last
        d is left button up since last
      XMSB      ; X coordinate
      XLSB
      YMSB      ; Y coordinate
      YLSB
*/
void IKBD_Cmd_ReadAbsMousePos(void)
{
  unsigned char Buttons,PrevButtons;

  /* Test buttons */
  Buttons = 0;
  /* Set buttons to show if up/down */
  if (Keyboard.bRButtonDown)
    Buttons |= 0x01;
  else
    Buttons |= 0x02;
  if (Keyboard.bLButtonDown)
    Buttons |= 0x04;
  else
    Buttons |= 0x08;
  /* Mask off it didn't send last time */
  PrevButtons = KeyboardProcessor.Abs.PrevReadAbsMouseButtons;
  KeyboardProcessor.Abs.PrevReadAbsMouseButtons = Buttons;
  Buttons &= ~PrevButtons;

  /* And send packet */
  IKBD_AddKeyToKeyboardBuffer(0xf7);
  IKBD_AddKeyToKeyboardBuffer(Buttons);
  IKBD_AddKeyToKeyboardBuffer((unsigned int)KeyboardProcessor.Abs.X>>8);
  IKBD_AddKeyToKeyboardBuffer((unsigned int)KeyboardProcessor.Abs.X&0xff);
  IKBD_AddKeyToKeyboardBuffer((unsigned int)KeyboardProcessor.Abs.Y>>8);
  IKBD_AddKeyToKeyboardBuffer((unsigned int)KeyboardProcessor.Abs.Y&0xff);

#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_ReadAbsMousePos %d,%d 0x%X\n",KeyboardProcessor.Abs.X,KeyboardProcessor.Abs.Y,Buttons);
  Debugger_TabIKBD_AddListViewItem("ReadAbsMousePos %d,%d 0x%X",KeyboardProcessor.Abs.X,KeyboardProcessor.Abs.Y,Buttons);
#endif
}

/*-----------------------------------------------------------------------*/
/*
  LOAD MOUSE POSITION

  0x0E
  0x00      ; filler
  XMSB      ; X coordinate
  XLSB      ; (in scaled coordinate system)
  YMSB      ; Y coordinate
  YLSB
*/
void IKBD_Cmd_SetInternalMousePos(void)
{  
  /* Setting these do not clip internal position(this happens on next update) */
  KeyboardProcessor.Abs.X = (((unsigned int)Keyboard.InputBuffer[2])<<8) | (unsigned int)Keyboard.InputBuffer[3];
  KeyboardProcessor.Abs.Y = (((unsigned int)Keyboard.InputBuffer[4])<<8) | (unsigned int)Keyboard.InputBuffer[5];
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_SetInternalMousePos %d,%d\n",KeyboardProcessor.Abs.X,KeyboardProcessor.Abs.Y);
  Debugger_TabIKBD_AddListViewItem("SetInternalMousePos %d,%d",KeyboardProcessor.Abs.X,KeyboardProcessor.Abs.Y);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET Y=0 AT BOTTOM

  0x0F
*/
void IKBD_Cmd_SetYAxisDown(void)
{
  KeyboardProcessor.Mouse.YAxis = -1;
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_SetYAxisDown\n");
  Debugger_TabIKBD_AddListViewItem("SetYAxisDown");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET Y=0 AT TOP

  0x10
*/
void IKBD_Cmd_SetYAxisUp(void)
{
  KeyboardProcessor.Mouse.YAxis = 1;
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_SetYAxisUp\n");
  Debugger_TabIKBD_AddListViewItem("SetYAxisUp");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  RESUME

  0x11
*/
void IKBD_Cmd_StartKeyboardTransfer(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_StartKeyboardTransfer\n");
  Debugger_TabIKBD_AddListViewItem("StartKeyboardTransfer");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  DISABLE MOUSE

  0x12
*/
void IKBD_Cmd_TurnMouseOff(void)
{
  KeyboardProcessor.MouseMode = AUTOMODE_OFF;
  bMouseDisabled = TRUE;
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_TurnMouseOff\n");
  Debugger_TabIKBD_AddListViewItem("TurnMouseOff");
#endif

  IKBD_CheckResetDisableBug();
}


/*-----------------------------------------------------------------------*/
/*
  PAUSE OUTPUT

  0x13
*/
void IKBD_Cmd_StopKeyboardTransfer(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_StopKeyboardTransfer\n");
  Debugger_TabIKBD_AddListViewItem("StopKeyboardTransfer");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET JOYSTICK EVENT REPORTING

  0x14
*/
void IKBD_Cmd_ReturnJoystickAuto(void)
{
 KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK;
 KeyboardProcessor.MouseMode = AUTOMODE_OFF;
 /* Again, if try to disable mouse within time of a reset it isn't disabled! */
 if (bDuringResetCriticalTime)
   KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
#ifdef DEBUG_OUTPUT_IKBD
 Debug_IKBD("IKBD_Cmd_ReturnJoystickAuto\n");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET JOYSTICK INTERROGATION MODE

  0x15
*/
void IKBD_Cmd_StopJoystick(void)
{
  KeyboardProcessor.JoystickMode = AUTOMODE_OFF;
//  Debug_IKBD("IKBD_Cmd_StopJoystick\n");
}


/*-----------------------------------------------------------------------*/
/*
  JOYSTICK INTERROGATE

  0x16
*/
void IKBD_Cmd_ReturnJoystick(void)
{
  IKBD_AddKeyToKeyboardBuffer(0xFD);
  IKBD_AddKeyToKeyboardBuffer(Joy_GetStickData(0));
  IKBD_AddKeyToKeyboardBuffer(Joy_GetStickData(1));
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_ReturnJoystick\n");
  Debugger_TabIKBD_AddListViewItem("ReturnJoystick");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET JOYSTICK MONITORING

  0x17
  rate      ; time between samples in hundreths of a second
    Returns: (in packets of two as long as in mode)
      %000000xy  where y is JOYSTICK1 Fire button
          and x is JOYSTICK0 Fire button
      %nnnnmmmm  where m is JOYSTICK1 state
          and n is JOYSTICK0 state
*/
void IKBD_Cmd_SetJoystickDuration(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_SetJoystickDuration\n");
  Debugger_TabIKBD_AddListViewItem("SetJoystickDuration");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET FIRE BUTTON MONITORING

  0x18
    Returns: (as long as in mode)
      %bbbbbbbb  ; state of the JOYSTICK1 fire button packed
            ; 8 bits per byte, the first sample if the MSB
*/
void IKBD_Cmd_SetJoystickFireDuration(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_SetJoystickFireDuration\n");
  Debugger_TabIKBD_AddListViewItem("SetJoystickFireDuration");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  SET JOYSTICK KEYCODE MODE

  0x19
  RX        ; length of time (in tenths of seconds) until
          ; horizontal velocity breakpoint is reached
  RY        ; length of time (in tenths of seconds) until
          ; vertical velocity breakpoint is reached
  TX        ; length (in tenths of seconds) of joystick closure
          ; until horizontal cursor key is generated before RX
          ; has elapsed
  TY        ; length (in tenths of seconds) of joystick closure
          ; until vertical cursor key is generated before RY
          ; has elapsed
  VX        ; length (in tenths of seconds) of joystick closure
          ; until horizontal cursor keystokes are generated after RX
          ; has elapsed
  VY        ; length (in tenths of seconds) of joystick closure
          ; until vertical cursor keystokes are generated after RY
          ; has elapsed
*/
void IKBD_Cmd_SetCursorForJoystick(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_SetCursorForJoystick\n");
  Debugger_TabIKBD_AddListViewItem("SetCursorForJoystick");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  DISABLE JOYSTICKS

  0x1A
*/
void IKBD_Cmd_DisableJoysticks(void)
{
  KeyboardProcessor.JoystickMode = AUTOMODE_OFF;
  bJoystickDisabled = TRUE;
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_DisableJoysticks\n");
  Debugger_TabIKBD_AddListViewItem("DisableJoysticks");
#endif

  IKBD_CheckResetDisableBug();
}


/*-----------------------------------------------------------------------*/
/*
  TIME-OF-DAY CLOCK SET

  0x1B
  YY        ; year (2 least significant digits)
  MM        ; month
  DD        ; day
  hh        ; hour
  mm        ; minute
  ss        ; second
*/
void IKBD_Cmd_SetClock(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_SetClock\n");
  Debugger_TabIKBD_AddListViewItem("SetClock");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  INTERROGATE TIME-OF-DAT CLOCK

  0x1C
    Returns:
      0xFC  ; time-of-day event header
      YY    ; year (2 least significant digits)
      MM    ; month
      DD    ; day
      hh    ; hour
      mm    ; minute
      ss    ; second
*/
void IKBD_Cmd_ReadClock(void)
{
/* FIXME */
/*
  SYSTEMTIME SystemTime;

  // Get windows time
  GetSystemTime(&SystemTime);

  // Return packet
  IKBD_AddKeyToKeyboardBuffer(0xFC);
  // Return time-of-day clock as yy-mm-dd-hh-mm-ss as BCD
  IKBD_AddKeyToKeyboardBuffer(Misc_ConvertToBCD(SystemTime.wYear%100));  // yy - year(2 least significant digits)
  IKBD_AddKeyToKeyboardBuffer(Misc_ConvertToBCD(SystemTime.wMonth));     // mm - Month
  IKBD_AddKeyToKeyboardBuffer(Misc_ConvertToBCD(SystemTime.wDay));       // dd - Day
  IKBD_AddKeyToKeyboardBuffer(Misc_ConvertToBCD(SystemTime.wHour));      // hh - Hour
  IKBD_AddKeyToKeyboardBuffer(Misc_ConvertToBCD(SystemTime.wMinute));    // mm - Minute
  IKBD_AddKeyToKeyboardBuffer(Misc_ConvertToBCD(SystemTime.wSecond));    // ss - Second
*/
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_ReadClock\n");
  Debugger_TabIKBD_AddListViewItem("ReadClock");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  MEMORY LOAD

  0x20
  ADRMSB      ; address in controller
  ADRLSB      ; memory to be loaded
  NUM        ; number of bytes (0-128)
  { data }
*/
void IKBD_Cmd_LoadMemory(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_LoadMemory\n");
  Debugger_TabIKBD_AddListViewItem("LoadMemory");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  MEMORY READ

  0x21
  ADRMSB        ; address in controller
  ADRLSB        ; memory to be read
    Returns:
      0xF6    ; status header
      0x20    ; memory access
      { data }  ; 6 data bytes starting at ADR
*/
void IKBD_Cmd_ReadMemory(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_ReadMemory\n");
  Debugger_TabIKBD_AddListViewItem("ReadMemory");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  CONTROLLER EXECUTE

  0x22
  ADRMSB      ; address of subroutine in
  ADRLSB      ; controller memory to be called
*/
void IKBD_Cmd_Execute(void)
{
#ifdef DEBUG_OUTPUT_IKBD
  Debug_IKBD("IKBD_Cmd_Execute\n");
  Debugger_TabIKBD_AddListViewItem("Execute");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Send data to keyboard processor via ACIA by writing to address 0xfffc02.
  For our emulation we bypass the ACIA (I've yet to see anything check for this)
  and add the byte directly into the keyboard input buffer.
*/
void IKBD_RunKeyboardCommand(void)
{
  int i=0;

  /* Write into our keyboard input buffer */
  Keyboard.InputBuffer[Keyboard.nBytesInInputBuffer++] = ACIAByte;

  /* Now check bytes to see if we have a valid/in-valid command string set */
  while(KeyboardCommands[i].Command!=0xff) {
    /* Found command? */
    if (KeyboardCommands[i].Command==Keyboard.InputBuffer[0]) {
      /* Is string complete, then can execute? */
      if (KeyboardCommands[i].NumParameters==Keyboard.nBytesInInputBuffer) {
        CALL_VAR(KeyboardCommands[i].pCallFunction);
        Keyboard.nBytesInInputBuffer = 0;
      }

      return;
    }
    
    i++;
  }

  /* Command not known, reset buffer(IKBD assumes a NOP) */
  Keyboard.nBytesInInputBuffer = 0;
}


/*-----------------------------------------------------------------------*/
/*
  Send byte to our keyboard processor, and execute
*/
void IKBD_SendByteToKeyboardProcessor(unsigned short bl)
{
  ACIAByte = bl;              /* Store byte to pass */
  IKBD_RunKeyboardCommand();  /* And send */
}


/*-----------------------------------------------------------------------*/
/*
  The byte stored in the ACIA 'ACIAByte' has been read by the CPU by reading from
  address $fffc02. We clear the status flag and set the GPIP register to signal read.
*/
unsigned short IKBD_GetByteFromACIA(void)
{
  /* ACIA is now reset */
  ACIAStatusRegister &= ~ACIA_STATUS_REGISTER__RX_BUFFER_FULL;
  ACIAStatusRegister &= ~ACIA_STATUS_REGISTER__INTERRUPT_REQUEST;
  ACIAStatusRegister &= ~ACIA_STATUS_REGISTER__OVERRUN_ERROR;

  /* GPIP I4 - General Purpose Pin Keyboard/MIDI interrupt */
  MFP_GPIP |= 0x10;
  return ACIAByte;  /* Return byte from keyboard */
}


/*-----------------------------------------------------------------------*/
/*
  Byte received in the ACIA from the keyboard processor. Store byte for read from $fffc02
  and clear the GPIP I4 register. This register will be remain low until byte has been
  read from ACIA.
*/
void IKBD_InterruptHandler_ACIA(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  /* Copy keyboard byte, ready for read from $fffc02 */
  ACIAByte = Keyboard.Buffer[Keyboard.BufferHead++];
  Keyboard.BufferHead &= KEYBOARD_BUFFER_MASK;
  
  /* Did we get an over-run? Ie byte has arrived from keyboard processor BEFORE CPU has read previous one from ACIA */
  if (ACIAStatusRegister&ACIA_STATUS_REGISTER__RX_BUFFER_FULL)
    ACIAStatusRegister |= ACIA_STATUS_REGISTER__OVERRUN_ERROR;  /* Set over-run */

  /* ACIA buffer is now full */
  ACIAStatusRegister |= ACIA_STATUS_REGISTER__RX_BUFFER_FULL;
  /* Signal interrupt pending */
  ACIAStatusRegister |= ACIA_STATUS_REGISTER__INTERRUPT_REQUEST;
  /* GPIP I4 - General Purpose Pin Keyboard/MIDI interrupt */
  /* NOTE: GPIP will remain low(0) until keyboard data is read from $fffc02. */
  MFP_GPIP &= ~0x10;

  /* Acknowledge in MFP circuit, pass bit,enable,pending */
  MFP_InputOnChannel(MFP_KEYBOARD_BIT,MFP_IERB,&MFP_IPRB);

  /* Clear flag so can allow another byte to be sent along serial line */
  bByteInTransitToACIA = FALSE;
  /* If another key is waiting, start sending from keyboard processor now */
  if (Keyboard.BufferHead!=Keyboard.BufferTail)
    IKBD_SendByteToACIA();
}


/*-----------------------------------------------------------------------*/
/*
  Send a byte from the keyboard buffer to the ACIA. On a real ST this takes some time to send
  so we must be as accurate in the timing as possible - bytes do not appear to the 68000 instantly!
  We do this via an internal interrupt - neat!
*/
void IKBD_SendByteToACIA(void)
{
  /* Transmit byte from keyboard processor to ACIA. This takes approx ACIA_CYCLES CPU clock cycles to complete */
  if (!bByteInTransitToACIA) {
    /* Send byte to ACIA */
    Int_AddRelativeInterrupt(ACIA_CYCLES,INTERRUPT_IKBD_ACIA);
    /* Set flag so only transmit one byte at a time */
    bByteInTransitToACIA = TRUE;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Add characer our internal keyboard buffer. These bytes are then sent one at a time to the ACIA.
  This is done via a delay to mimick the STs internal workings, as this is needed for games such
  as Carrier Command.
*/
void IKBD_AddKeyToKeyboardBuffer(unsigned char Data)
{
  /* Is keyboard initialised yet? Ignore any bytes until it is */
  if (!KeyboardProcessor.bReset)
    return;

  /* Check we have space to add byte */
  if (Keyboard.BufferHead!=((Keyboard.BufferTail+1)&KEYBOARD_BUFFER_MASK)) {
    /* Add byte to our buffer */
    Keyboard.Buffer[Keyboard.BufferTail++] = Data;
    Keyboard.BufferTail &= KEYBOARD_BUFFER_MASK;
    
    /* We have character ready to transmit from the ACIA - see if can send it now */
    IKBD_SendByteToACIA();
  }
}


/*-----------------------------------------------------------------------*/
/*
  When press/release key under Windows, execute this function
*/
void IKBD_PressSTKey(unsigned char ScanCode,BOOL bPress)
{
  /* Ignore anything until we've redirected our GEM handlers */
//FM  if (!bInitGemDOS)
//FIXME    return;
  if (!bPress)
    ScanCode |= 0x80;    /* Set top bit if released key */
  IKBD_AddKeyToKeyboardBuffer(ScanCode);  /* And send to keyboard processor */
}
