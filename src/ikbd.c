/*
  Hatari - ikbd.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  The keyboard processor(6301) handles any joystick/mouse task and sends bytes
  to the ACIA(6850). When a byte arrives in the ACIA (which takes just over
  7000 CPU cycles) an MFP interrupt is flagged. The CPU can now read the byte
  from the ACIA by reading address $fffc02.
  An annoying bug can be found in Dungeon Master. This, when run, turns off the
  mouse input - but of course then you are unable to play the game! A bodge
  flag has been added so we need to be told twice to turn off the mouse input
  (although I think this causes errors in other games...).
  Also, the ACIA_CYCLES time is very important for games such as Carrier
  Command. The keyboard handler in this game has a bug in it, which corrupts
  its own registers if more than one byte is queued up. This value was found by
  a test program on a real ST and has correctly emulated the behaviour.
*/
const char IKBD_rcsid[] = "Hatari $Id: ikbd.c,v 1.41 2008-07-16 21:12:11 thothy Exp $";

/* 2007/09/29	[NP]	Use the new int.c to add interrupts with INT_CPU_CYCLE / INT_MFP_CYCLE.		*/
/* 2007/12/09	[NP]	If reset is written to ACIA control register, we must call ACIA_Reset to reset	*/
/*			RX/TX status. Reading the control register fffc00 just after a reset should	*/
/*			return the value 0x02 (used in Transbeauce 2 demo loader).			*/
/* 2008/07/06	[NP]	Add support for executing 8 bit code sent to the 6301 processor.		*/
/*			Instead of implementing a full 6301 emulator, we compute a checksum for each	*/
/*			program sent to the 6301 RAM. If the checksum is recognized, we call some	*/
/*			functions to emulate the behaviour of the 6301 in that case.			*/
/*			When the 6301 is in 'Execute' mode (command 0x22), we must stop the normal	*/
/*			reporting of key/mouse/joystick and use our custom handlers for each read or	*/
/*			write to $fffc02.								*/
/*			After a reset command, returns $F1 after $F0 (needed by Dragonnels Demo).	*/
/*			This fixes the Transbeauce 2 demo menu, the Dragonnels demo menu and the	*/
/*			Froggies Over The Fence demo menu (yeah ! enjoy this master piece of demo !).	*/


#include <time.h>

#include "main.h"
#include "ikbd.h"
#include "int.h"
#include "ioMem.h"
#include "joy.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "video.h"
#include "utils.h"


#define DBL_CLICK_HISTORY  0x07     /* Number of frames since last click to see if need to send one or two clicks */
#define ACIA_CYCLES    7200         /* Cycles (Multiple of 4) between sent to ACIA from keyboard along serial line - 500Hz/64, (approx' 6920-7200cycles from test program) */

#define IKBD_RESET_CYCLES  400000   /* Cycles after RESET before complete */
#define IKBD_INIT_RESET_CYCLES 3000000  /* Cycles after a cold reset before IKBD starts */

#define ABS_X_ONRESET    0          /* Initial XY for absolute mouse position after RESET command */
#define ABS_Y_ONRESET    0
#define ABS_MAX_X_ONRESET  320      /* Initial absolute mouse limits after RESET command */
#define ABS_MAY_Y_ONRESET  200      /* These values are never actually used as user MUST call 'IKBD_Cmd_AbsMouseMode' before ever using them */

#define ABS_PREVBUTTONS  (0x02|0x8) /* Don't report any buttons up on first call to 'IKBD_Cmd_ReadAbsMousePos' */


/* Keyboard state */
KEYBOARD Keyboard;

/* Keyboard processor */
KEYBOARD_PROCESSOR KeyboardProcessor;   /* Keyboard processor details */

/* Pattern of mouse button up/down in ST frames (run off a double-click message) */
static const uint8_t DoubleClickPattern[] =
{
	BUTTON_MOUSE,BUTTON_MOUSE,BUTTON_MOUSE,BUTTON_MOUSE,
	0,0,0,0,BUTTON_MOUSE,BUTTON_MOUSE,BUTTON_MOUSE,BUTTON_MOUSE
};

static bool bMouseDisabled, bJoystickDisabled;
static bool bDuringResetCriticalTime, bBothMouseAndJoy;

/* ACIA */
static Uint8 ACIAControlRegister = 0;
static Uint8 ACIAStatusRegister = ACIA_STATUS_REGISTER__TX_BUFFER_EMPTY;  /* Pass when read 0xfffc00 */
static Uint8 ACIAByte;				/* When a byte has arrived at the ACIA (from the keyboard) it is stored here */
static bool bByteInTransitToACIA = FALSE;	/* Is a byte being sent to the ACIA from the keyboard? */

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
static void IKBD_Cmd_Reset(void);
static void IKBD_Cmd_MouseAction(void);
static void IKBD_Cmd_RelMouseMode(void);
static void IKBD_Cmd_AbsMouseMode(void);
static void IKBD_Cmd_MouseCursorKeycodes(void);
static void IKBD_Cmd_SetMouseThreshold(void);
static void IKBD_Cmd_SetMouseScale(void);
static void IKBD_Cmd_ReadAbsMousePos(void);
static void IKBD_Cmd_SetInternalMousePos(void);
static void IKBD_Cmd_SetYAxisDown(void);
static void IKBD_Cmd_SetYAxisUp(void);
static void IKBD_Cmd_StartKeyboardTransfer(void);
static void IKBD_Cmd_TurnMouseOff(void);
static void IKBD_Cmd_StopKeyboardTransfer(void);
static void IKBD_Cmd_ReturnJoystickAuto(void);
static void IKBD_Cmd_StopJoystick(void);
static void IKBD_Cmd_ReturnJoystick(void);
static void IKBD_Cmd_SetJoystickDuration(void);
static void IKBD_Cmd_SetJoystickFireDuration(void);
static void IKBD_Cmd_SetCursorForJoystick(void);
static void IKBD_Cmd_DisableJoysticks(void);
static void IKBD_Cmd_SetClock(void);
static void IKBD_Cmd_ReadClock(void);
static void IKBD_Cmd_LoadMemory(void);
static void IKBD_Cmd_ReadMemory(void);
static void IKBD_Cmd_Execute(void);
static void IKBD_Cmd_NullFunction(void);

static const IKBD_COMMAND_PARAMS KeyboardCommands[] =
{
	/* Known messages, counts include command byte */
	{ 0x80,2,  IKBD_Cmd_Reset },
	{ 0x07,2,  IKBD_Cmd_MouseAction },
	{ 0x08,1,  IKBD_Cmd_RelMouseMode },
	{ 0x09,5,  IKBD_Cmd_AbsMouseMode },
	{ 0x0A,3,  IKBD_Cmd_MouseCursorKeycodes },
	{ 0x0B,3,  IKBD_Cmd_SetMouseThreshold },
	{ 0x0C,3,  IKBD_Cmd_SetMouseScale },
	{ 0x0D,1,  IKBD_Cmd_ReadAbsMousePos },
	{ 0x0E,6,  IKBD_Cmd_SetInternalMousePos },
	{ 0x0F,1,  IKBD_Cmd_SetYAxisDown },
	{ 0x10,1,  IKBD_Cmd_SetYAxisUp },
	{ 0x11,1,  IKBD_Cmd_StartKeyboardTransfer },
	{ 0x12,1,  IKBD_Cmd_TurnMouseOff },
	{ 0x13,1,  IKBD_Cmd_StopKeyboardTransfer },
	{ 0x14,1,  IKBD_Cmd_ReturnJoystickAuto },
	{ 0x15,1,  IKBD_Cmd_StopJoystick },
	{ 0x16,1,  IKBD_Cmd_ReturnJoystick },
	{ 0x17,2,  IKBD_Cmd_SetJoystickDuration },
	{ 0x18,1,  IKBD_Cmd_SetJoystickFireDuration },
	{ 0x19,7,  IKBD_Cmd_SetCursorForJoystick },
	{ 0x1A,1,  IKBD_Cmd_DisableJoysticks },
	{ 0x1B,7,  IKBD_Cmd_SetClock },
	{ 0x1C,1,  IKBD_Cmd_ReadClock },
	{ 0x20,4,  IKBD_Cmd_LoadMemory },
	{ 0x21,3,  IKBD_Cmd_ReadMemory },
	{ 0x22,3,  IKBD_Cmd_Execute },

	/* Report message (top bit set) - ignore for now... */
	{ 0x88,1,  IKBD_Cmd_NullFunction },
	{ 0x89,1,  IKBD_Cmd_NullFunction },
	{ 0x8A,1,  IKBD_Cmd_NullFunction },
	{ 0x8B,1,  IKBD_Cmd_NullFunction },
	{ 0x8C,1,  IKBD_Cmd_NullFunction },
	{ 0x8F,1,  IKBD_Cmd_NullFunction },
	{ 0x90,1,  IKBD_Cmd_NullFunction },
	{ 0x92,1,  IKBD_Cmd_NullFunction },
	{ 0x94,1,  IKBD_Cmd_NullFunction },
	{ 0x95,1,  IKBD_Cmd_NullFunction },
	{ 0x99,1,  IKBD_Cmd_NullFunction },

	{ 0xFF,0,  NULL }  /* Term */
};


static void IKBD_SendByteToKeyboardProcessor(Uint16 bl);
static Uint16 IKBD_GetByteFromACIA(void);
static void IKBD_SendByteToACIA(void);
static void IKBD_AddKeyToKeyboardBuffer(Uint8 Data);
static void IKBD_AddKeyToKeyboardBuffer_Real(Uint8 Data);


/* Belows part is used to emulate the behaviour of custom 6301 programs */
/* sent to the ikbd RAM. */

static void IKBD_LoadMemoryByte ( Uint8 aciabyte );

static void IKBD_CustomCodeHandler_CommonBoot ( Uint8 aciabyte );

static void IKBD_CustomCodeHandler_FroggiesMenu_Read ( void );
static void IKBD_CustomCodeHandler_FroggiesMenu_Write ( Uint8 aciabyte );
static void IKBD_CustomCodeHandler_Transbeauce2Menu_Read ( void );
static void IKBD_CustomCodeHandler_Transbeauce2Menu_Write ( Uint8 aciabyte );
static void IKBD_CustomCodeHandler_DragonnelsMenu_Read ( void );
static void IKBD_CustomCodeHandler_DragonnelsMenu_Write ( Uint8 aciabyte );


int	MemoryLoadNbBytesTotal = 0;			/* total number of bytes to send with the command 0x20 */
int	MemoryLoadNbBytesLeft = 0;			/* number of bytes that remain to be sent  */
Uint32	MemoryLoadCrc = 0xffffffff;			/* CRC of the bytes sent to the ikbd */
int	MemoryExeNbBytes = 0;				/* current number of bytes sent to the ikbd when IKBD_ExeMode is TRUE */

void	(*pIKBD_CustomCodeHandler_Read) ( void );
void	(*pIKBD_CustomCodeHandler_Write) ( Uint8 );
bool	IKBD_ExeMode = FALSE;

Uint8	ScanCodeState[ 128 ];				/* state of each key : 0=released 1=pressed */

/* This array contains all known custom 6301 programs, with their CRC */
struct
{
	Uint32		LoadMemCrc;			/* CRC of the bytes sent using the command 0x20 */
	void		(*ExeBootHandler) ( Uint8 );	/* function handling write to $fffc02 during the 'boot' mode */
	int		MainProgNbBytes;		/* number of bytes of the main 6301 program */
	Uint32		MainProgCrc;			/* CRC of the main 6301 program */
	void		(*ExeMainHandler_Read) ( void );/* function handling read to $fffc02 in the main 6301 program */
	void		(*ExeMainHandler_Write) ( Uint8 ); /* funciton handling write to $fffc02 in the main 6301 program */
	const char	*Name;
}
CustomCodeDefinitions[] =
{
	{
		0x2efb11b1 ,
		IKBD_CustomCodeHandler_CommonBoot ,
		167,
		0xe7110b6d ,
		IKBD_CustomCodeHandler_FroggiesMenu_Read ,
		IKBD_CustomCodeHandler_FroggiesMenu_Write ,
		"Froggies Over The Fence Main Menu"
	} ,
	{
		0xadb6b503 ,
		IKBD_CustomCodeHandler_CommonBoot ,
		165,
		0x5617c33c ,
		IKBD_CustomCodeHandler_Transbeauce2Menu_Read ,
		IKBD_CustomCodeHandler_Transbeauce2Menu_Write ,
		"Transbeauce 2 Main Menu"
	} ,
	{
		0x33c23cdf ,
		IKBD_CustomCodeHandler_CommonBoot ,
		83 ,
		0xdf3e5a88 ,
		IKBD_CustomCodeHandler_DragonnelsMenu_Read ,
		IKBD_CustomCodeHandler_DragonnelsMenu_Write ,
		"Dragonnels Main Menu"
	}
};



/*-----------------------------------------------------------------------*/
/**
 * Reset the ACIA
 */
void ACIA_Reset(void)
{
	bByteInTransitToACIA = FALSE;
	ACIAControlRegister = 0;
	ACIAStatusRegister = ACIA_STATUS_REGISTER__TX_BUFFER_EMPTY;
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the IKBD processor
 */

/* Cancel execution of any program that was uploaded to the 6301's RAM */
/* This function is also called when performing a 68000 'reset' ; in that */
/* case we need to return $F0 and $F1. */

void IKBD_Reset_ExeMode ( void )
{
	HATARI_TRACE ( HATARI_TRACE_IKBD_EXEC, "ikbd custom exe off\n" );

	/* Reset any custom code run with the Execute command 0x22 */
	MemoryLoadNbBytesLeft = 0;
	pIKBD_CustomCodeHandler_Read = NULL;
	pIKBD_CustomCodeHandler_Write = NULL;
	IKBD_ExeMode = FALSE;
		
	Keyboard.BufferHead = Keyboard.BufferTail = 0;	/* flush all queued bytes that would be read in $fffc02 */
	bByteInTransitToACIA = FALSE;
	IKBD_AddKeyToKeyboardBuffer(0xF0);		/* Assume OK, return correct code */
	IKBD_AddKeyToKeyboardBuffer(0xF1);		/* [NP] Dragonnels demo needs this */
}


void IKBD_Reset(bool bCold)
{
	int	i;


	/* Reset internal keyboard processor details */
	if (bCold)
	{
		KeyboardProcessor.bReset = FALSE;
		if (Int_InterruptActive(INTERRUPT_IKBD_RESETTIMER))
			Int_RemovePendingInterrupt(INTERRUPT_IKBD_RESETTIMER);
	}

	KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
	KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK;

	KeyboardProcessor.Abs.X = ABS_X_ONRESET;
	KeyboardProcessor.Abs.Y = ABS_Y_ONRESET;
	KeyboardProcessor.Abs.MaxX = ABS_MAX_X_ONRESET;
	KeyboardProcessor.Abs.MaxY = ABS_MAY_Y_ONRESET;
	KeyboardProcessor.Abs.PrevReadAbsMouseButtons = ABS_PREVBUTTONS;

	KeyboardProcessor.Mouse.DeltaX = KeyboardProcessor.Mouse.DeltaY = 0;
	KeyboardProcessor.Mouse.XScale = KeyboardProcessor.Mouse.YScale = 0;
	KeyboardProcessor.Mouse.XThreshold = KeyboardProcessor.Mouse.YThreshold = 1;
	KeyboardProcessor.Mouse.YAxis = 1;          /* Y origin at top */
	KeyboardProcessor.Mouse.Action = 0;

	KeyboardProcessor.Joy.PrevJoyData[0] = KeyboardProcessor.Joy.PrevJoyData[1] = 0;

	for ( i=0 ; i<128 ; i++ )
		ScanCodeState[ i ] = 0;				/* key is released */

	/* Reset our ACIA status */
	ACIA_Reset();
	/* And our keyboard states and clear key state table */
	Keyboard.BufferHead = Keyboard.BufferTail = 0;
	Keyboard.nBytesInInputBuffer = 0;
	memset(Keyboard.KeyStates, 0, sizeof(Keyboard.KeyStates));
	Keyboard.bLButtonDown = BUTTON_NULL;
	Keyboard.bRButtonDown = BUTTON_NULL;
	Keyboard.bOldLButtonDown = Keyboard.bOldRButtonDown = BUTTON_NULL;
	Keyboard.LButtonDblClk = Keyboard.RButtonDblClk = 0;
	Keyboard.LButtonHistory = Keyboard.RButtonHistory = 0;

	/* Store bool for when disable mouse or joystick */
	bMouseDisabled = bJoystickDisabled = FALSE;
	/* do emulate hardware 'quirk' where if disable both with 'x' time
	 * of a RESET command they are ignored! */
	bDuringResetCriticalTime = bBothMouseAndJoy = FALSE;

	/* Remove any custom handlers used to emulate code loaded to the 6301's RAM */
	IKBD_Reset_ExeMode ();
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables
 * ('MemorySnapShot_Store' handles type)
 */
void IKBD_MemorySnapShot_Capture(bool bSave)
{
	unsigned int i;

	/* Save/Restore details */
	MemorySnapShot_Store(&Keyboard, sizeof(Keyboard));
	MemorySnapShot_Store(&KeyboardProcessor, sizeof(KeyboardProcessor));
	MemorySnapShot_Store(&ACIAControlRegister, sizeof(ACIAControlRegister));
	MemorySnapShot_Store(&ACIAStatusRegister, sizeof(ACIAStatusRegister));
	MemorySnapShot_Store(&ACIAByte, sizeof(ACIAByte));
	MemorySnapShot_Store(&bByteInTransitToACIA, sizeof(bByteInTransitToACIA));
	MemorySnapShot_Store(&bMouseDisabled, sizeof(bMouseDisabled));
	MemorySnapShot_Store(&bJoystickDisabled, sizeof(bJoystickDisabled));
	MemorySnapShot_Store(&bDuringResetCriticalTime, sizeof(bDuringResetCriticalTime));
	MemorySnapShot_Store(&bBothMouseAndJoy, sizeof(bBothMouseAndJoy));

	/* restore custom 6301 program if needed */
	MemorySnapShot_Store(&IKBD_ExeMode, sizeof(IKBD_ExeMode));
	MemorySnapShot_Store(&MemoryLoadCrc, sizeof(MemoryLoadCrc));
	if ( ( bSave == FALSE ) && ( IKBD_ExeMode == TRUE ) )	/* restoring a snapshot with active 6301 emulation */
	{
		for ( i = 0 ; i < sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ); i++ )
			if ( CustomCodeDefinitions[ i ].MainProgCrc == MemoryLoadCrc )
			{
				pIKBD_CustomCodeHandler_Read = CustomCodeDefinitions[ i ].ExeMainHandler_Read;
				pIKBD_CustomCodeHandler_Write = CustomCodeDefinitions[ i ].ExeMainHandler_Write;
				Keyboard.BufferHead = Keyboard.BufferTail = 0;	/* flush all queued bytes that would be read in $fffc02 */
//				(*pIKBD_CustomCodeHandler_Read) ();		/* initialize ACIAByte */
				ACIAByte = 0;			/* initialize ACIAByte, don't call IKBD_AddKeyToKeyboardBuffer_Real now */
				break;
			}

		if ( i >= sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ) )	/* not found (should not happen) */
			IKBD_ExeMode = FALSE;			/* turn off exe mode */
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Calculate out 'delta' that mouse has moved by each frame, and add this to our internal keyboard position
 */
static void IKBD_UpdateInternalMousePosition(void)
{

	KeyboardProcessor.Mouse.DeltaX = KeyboardProcessor.Mouse.dx;
	KeyboardProcessor.Mouse.DeltaY = KeyboardProcessor.Mouse.dy;
	KeyboardProcessor.Mouse.dx = 0;
	KeyboardProcessor.Mouse.dy = 0;

	/* Update internal mouse coords - Y axis moves according to YAxis setting(up/down) */
	/* Limit to Max X/Y(inclusive) */
	KeyboardProcessor.Abs.X += KeyboardProcessor.Mouse.DeltaX;
	if (KeyboardProcessor.Abs.X < 0)
		KeyboardProcessor.Abs.X = 0;
	if (KeyboardProcessor.Abs.X > KeyboardProcessor.Abs.MaxX)
		KeyboardProcessor.Abs.X = KeyboardProcessor.Abs.MaxX;

	KeyboardProcessor.Abs.Y += KeyboardProcessor.Mouse.DeltaY*KeyboardProcessor.Mouse.YAxis;  /* Needed '+' for Falcon... */
	if (KeyboardProcessor.Abs.Y < 0)
		KeyboardProcessor.Abs.Y = 0;
	if (KeyboardProcessor.Abs.Y > KeyboardProcessor.Abs.MaxY)
		KeyboardProcessor.Abs.Y = KeyboardProcessor.Abs.MaxY;

}


/*-----------------------------------------------------------------------*/
/**
 * When running in maximum speed the emulation will not see 'double-clicks'
 * of the mouse as it is running so fast. In this case, we check for a
 * double-click and pass the 'up'/'down' messages in emulation time to
 * simulate the double-click effect!
 */
static void IKBD_CheckForDoubleClicks(void)
{
	/*
	  Things get a little complicated when running max speed as a normal
	  double-click is a load of 1's, followed by 0's, 1's and 0's - But the
	  ST does not see this as a double click as the space in 'ST' time
	  between changes is so great.
	  Now, when we see a real double-click in max speed we actually send
	  the down/up/down/up in ST time. To get this correct (and not send
	  three clicks) we look in a history buffer and start at an index which
	  gives the correct number of clicks! Phew!
	*/

	/* Handle double clicks!!! */
	if (Keyboard.LButtonDblClk)
	{
		if (Keyboard.LButtonDblClk == 1)              /* First pressed! */
		{
			if ((Keyboard.LButtonHistory&0x3f) == 0)  /* If not pressed button in long time do full dbl-click pattern */
				Keyboard.LButtonDblClk = 1;
			else
			{
				Keyboard.LButtonDblClk = 4;           /* Otherwise, check where to begin to give 1111000011110000 pattern */
				if ((Keyboard.LButtonHistory&0x7) == 0)
					Keyboard.LButtonDblClk = 8;
				else if ((Keyboard.LButtonHistory&0x3) == 0)
					Keyboard.LButtonDblClk = 7;
				else if ((Keyboard.LButtonHistory&0x1) == 0)
					Keyboard.LButtonDblClk = 6;
			}
		}

		Keyboard.bLButtonDown = DoubleClickPattern[Keyboard.LButtonDblClk];
		Keyboard.LButtonDblClk++;
		if (Keyboard.LButtonDblClk >= 13)             /* Check for end of sequence */
		{
			Keyboard.LButtonDblClk = 0;
			Keyboard.bLButtonDown = FALSE;
		}
	}
	if (Keyboard.RButtonDblClk)
	{
		if (Keyboard.RButtonDblClk == 1)              /* First pressed! */
		{
			if ((Keyboard.RButtonHistory&0x3f) == 0)  /* If not pressed button in long time do full dbl-click pattern */
				Keyboard.RButtonDblClk = 1;
			else
			{
				Keyboard.RButtonDblClk = 4;           /* Otherwise, check where to begin to give 1111000011110000 pattern */
				if ((Keyboard.RButtonHistory&0x7) == 0)
					Keyboard.RButtonDblClk = 8;
				else if ((Keyboard.RButtonHistory&0x3) == 0)
					Keyboard.RButtonDblClk = 7;
				else if ((Keyboard.RButtonHistory&0x1) == 0)
					Keyboard.RButtonDblClk = 6;
			}
		}

		Keyboard.bRButtonDown = DoubleClickPattern[Keyboard.RButtonDblClk];
		Keyboard.RButtonDblClk++;
		if (Keyboard.RButtonDblClk >= 13)             /* Check for end of sequence */
		{
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
/**
 * Convert button to bool value
 */
static bool IKBD_ButtonBool(int Button)
{
	/* Button pressed? */
	if (Button)
		return TRUE;
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Return TRUE if buttons match, use this as buttons are a mask and not boolean
 */
static bool IKBD_ButtonsEqual(int Button1,int Button2)
{
	/* Return bool compare */
	return (IKBD_ButtonBool(Button1) == IKBD_ButtonBool(Button2));
}


/*-----------------------------------------------------------------------*/
/**
 * According to if the mouse if enabled or not the joystick 1 fire button/right mouse button
 * will become the same button, ie pressing one will also press the other and vise-versa
 */
static void IKBD_DuplicateMouseFireButtons(void)
{
	/* Don't duplicate fire button when program tries to use both! */
	if (bBothMouseAndJoy)  return;

	/* If mouse is off then joystick fire button goes to joystick */
	if (KeyboardProcessor.MouseMode==AUTOMODE_OFF)
	{
		/* If pressed right mouse button, should go to joystick 1 */
		if (Keyboard.bRButtonDown&BUTTON_MOUSE)
			KeyboardProcessor.Joy.JoyData[1] |= 0x80;
		/* And left mouse button, should go to joystick 0 */
		if (Keyboard.bLButtonDown&BUTTON_MOUSE)
			KeyboardProcessor.Joy.JoyData[0] |= 0x80;
	}
	/* If mouse if on, joystick 1 fire button goes to mouse not to the joystick */
	else
	{
		/* Is fire button pressed? */
		if (KeyboardProcessor.Joy.JoyData[1]&0x80)
		{
			KeyboardProcessor.Joy.JoyData[1] &= 0x7f;  /* Clear fire button bit */
			Keyboard.bRButtonDown |= BUTTON_JOYSTICK;  /* Mimick on mouse right button */
		}
		else
			Keyboard.bRButtonDown &= ~BUTTON_JOYSTICK;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Send 'relative' mouse position
 */
static void IKBD_SendRelMousePacket(void)
{
	int ByteRelX,ByteRelY;
	Uint8 Header;

	if ( (KeyboardProcessor.Mouse.DeltaX!=0) || (KeyboardProcessor.Mouse.DeltaY!=0)
	        || (!IKBD_ButtonsEqual(Keyboard.bOldLButtonDown,Keyboard.bLButtonDown)) || (!IKBD_ButtonsEqual(Keyboard.bOldRButtonDown,Keyboard.bRButtonDown)) )
	{
		/* Send packet to keyboard process */
		while (TRUE)
		{
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
/**
 * Send 'joysticks' bit masks
 */
static void IKBD_SelAutoJoysticks(void)
{
	Uint8 JoyData;

	/* Did joystick 0/mouse change? */
	JoyData = KeyboardProcessor.Joy.JoyData[0];
	if (JoyData!=KeyboardProcessor.Joy.PrevJoyData[0])
	{
		IKBD_AddKeyToKeyboardBuffer(0xFE);    /* Joystick 0/Mouse */
		IKBD_AddKeyToKeyboardBuffer(JoyData);

		KeyboardProcessor.Joy.PrevJoyData[0] = JoyData;
	}

	/* Did joystick 1(default) change? */
	JoyData = KeyboardProcessor.Joy.JoyData[1];
	if (JoyData!=KeyboardProcessor.Joy.PrevJoyData[1])
	{
		IKBD_AddKeyToKeyboardBuffer(0xFF);    /* Joystick 1 */
		IKBD_AddKeyToKeyboardBuffer(JoyData);

		KeyboardProcessor.Joy.PrevJoyData[1] = JoyData;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Send packets which are generated from the mouse action settings
 * If relative mode is on, still generate these packets
 */
static void IKBD_SendOnMouseAction(void)
{
	bool bReportPosition = FALSE;

	/* Report buttons as keys? Do in relative/absolute mode */
	if (KeyboardProcessor.Mouse.Action&0x4)
	{
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
	if (KeyboardProcessor.Mouse.Action&0x3)
	{
		/* Check for 'press'? */
		if (KeyboardProcessor.Mouse.Action&0x1)
		{
			/* Did 'press' mouse buttons? */
			if ( (IKBD_ButtonBool(Keyboard.bLButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldLButtonDown))) )
			{
				bReportPosition = TRUE;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x04;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x02;
			}
			if ( (IKBD_ButtonBool(Keyboard.bRButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldRButtonDown))) )
			{
				bReportPosition = TRUE;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x01;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x08;
			}
		}
		/* Check for 'release'? */
		if (KeyboardProcessor.Mouse.Action&0x2)
		{
			/* Did 'release' mouse buttons? */
			if ( (IKBD_ButtonBool(Keyboard.bOldLButtonDown) && (!IKBD_ButtonBool(Keyboard.bLButtonDown))) )
			{
				bReportPosition = TRUE;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x08;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x01;
			}
			if ( (IKBD_ButtonBool(Keyboard.bOldRButtonDown) && (!IKBD_ButtonBool(Keyboard.bRButtonDown))) )
			{
				bReportPosition = TRUE;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x02;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x04;
			}
		}

		/* Do need to report? */
		if (bReportPosition)
		{
			/* Only report if mouse in absolute mode */
			if (KeyboardProcessor.MouseMode==AUTOMODE_MOUSEABS)
			{
				HATARI_TRACE(HATARI_TRACE_IKBD_ALL, "Report ABS on MouseAction\n");
				IKBD_Cmd_ReadAbsMousePos();
			}
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Send mouse movements as cursor keys
 */
static void IKBD_SendCursorMousePacket(void)
{
	int i=0;

	/* Run each 'Delta' as cursor presses */
	/* Limit to '10' loops as host mouse cursor might have a VERY poor quality. */
	/* Eg, a single mouse movement on and ST gives delta's of '1', mostly, */
	/* but host mouse might go as high as 20+! */
	while ( (i<10) && ((KeyboardProcessor.Mouse.DeltaX!=0) || (KeyboardProcessor.Mouse.DeltaY!=0)
	                   || (!IKBD_ButtonsEqual(Keyboard.bOldLButtonDown,Keyboard.bLButtonDown)) || (!IKBD_ButtonsEqual(Keyboard.bOldRButtonDown,Keyboard.bRButtonDown))) )
	{
		/* Left? */
		if (KeyboardProcessor.Mouse.DeltaX<0)
		{
			IKBD_AddKeyToKeyboardBuffer(75);    /* Left cursor */
			IKBD_AddKeyToKeyboardBuffer(75|0x80);
			KeyboardProcessor.Mouse.DeltaX++;
		}
		/* Right? */
		if (KeyboardProcessor.Mouse.DeltaX>0)
		{
			IKBD_AddKeyToKeyboardBuffer(77);    /* Right cursor */
			IKBD_AddKeyToKeyboardBuffer(77|0x80);
			KeyboardProcessor.Mouse.DeltaX--;
		}
		/* Up? */
		if (KeyboardProcessor.Mouse.DeltaY<0)
		{
			IKBD_AddKeyToKeyboardBuffer(72);    /* Up cursor */
			IKBD_AddKeyToKeyboardBuffer(72|0x80);
			KeyboardProcessor.Mouse.DeltaY++;
		}
		/* Down? */
		if (KeyboardProcessor.Mouse.DeltaY>0)
		{
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
/**
 * Return packets from keyboard for auto, rel mouse, joystick etc...
 */
void IKBD_SendAutoKeyboardCommands(void)
{
	/* Don't do anything until processor is first reset */
	if (!KeyboardProcessor.bReset)
		return;

	/* Read joysticks for this frame */
	KeyboardProcessor.Joy.JoyData[1] = Joy_GetStickData(1);
	/* If mouse is on, joystick 0 is not connected */
	if (KeyboardProcessor.MouseMode==AUTOMODE_OFF
	        || (bBothMouseAndJoy && KeyboardProcessor.MouseMode==AUTOMODE_MOUSEREL))
		KeyboardProcessor.Joy.JoyData[0] = Joy_GetStickData(0);
	else
		KeyboardProcessor.Joy.JoyData[0] = 0x00;

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
	if (JoystickSpaceBar)
	{
		/* As we simulating space bar? */
		if (JoystickSpaceBar==JOYSTICK_SPACE_DOWN)
		{
			IKBD_PressSTKey(57,TRUE);         /* Press */
			JoystickSpaceBar = JOYSTICK_SPACE_UP;
		}
		else   //if (JoystickSpaceBar==JOYSTICK_SPACE_UP) {
		{
			IKBD_PressSTKey(57,FALSE);        /* Release */
			JoystickSpaceBar = FALSE;         /* Complete */
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * On ST if disable Mouse AND Joystick with a set time of a RESET command they are
 * actually turned back on! (A number of games do this so can get mouse and joystick
 * packets at the same time)
 */
static void IKBD_CheckResetDisableBug(void)
{
	/* Have disabled BOTH mouse and joystick? */
	if (bMouseDisabled && bJoystickDisabled)
	{
		/* And in critical time? */
		if (bDuringResetCriticalTime)
		{
			/* Emulate relative mouse and joystick reports being turned back on */
			KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
			KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK;
			bBothMouseAndJoy = TRUE;

			HATARI_TRACE(HATARI_TRACE_IKBD_ALL, "IKBD Mouse+Joystick disabled "
			             "during RESET. Revert.\n");
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Start timer after keyboard RESET command to emulate 'quirk'
 * If some IKBD commands are sent during time after a RESET they may be ignored
 */
void IKBD_InterruptHandler_ResetTimer(void)
{
	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Turn processor on; can now process commands */
	KeyboardProcessor.bReset = TRUE;

	/* Critical timer is over */
	bDuringResetCriticalTime = FALSE;
}



/*-----------------------------------------------------------------------*/
/*
  List of keyboard commands
*/


/*-----------------------------------------------------------------------*/
/**
 * Blank function for some keyboard commands - this can be used to find errors
 */
static void IKBD_Cmd_NullFunction(void)
{
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_NullFunction\n");
}


/*-----------------------------------------------------------------------*/
/**
 * RESET
 *
 * 0x80
 * 0x01
 *
 * Performs self test and checks for stuck (closed) keys, if OK returns 0xF0. Otherwise
 * returns break codes for keys
 */
static void IKBD_Cmd_Reset(void)
{
	/* Check for error series of bytes, eg 0x80,0x01 */
	if (Keyboard.InputBuffer[1] == 0x01)
	{
		HATARI_TRACE(HATARI_TRACE_IKBD_ALL, "KEYBOARD ON\n");

		/* Set defaults */
		KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
		KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK;
		KeyboardProcessor.Abs.X = ABS_X_ONRESET;
		KeyboardProcessor.Abs.Y = ABS_Y_ONRESET;
		KeyboardProcessor.Abs.MaxX = ABS_MAX_X_ONRESET;
		KeyboardProcessor.Abs.MaxY = ABS_MAY_Y_ONRESET;
		KeyboardProcessor.Abs.PrevReadAbsMouseButtons = ABS_PREVBUTTONS;

		Keyboard.BufferHead = Keyboard.BufferTail = 0;	/* flush all queued bytes that would be read in $fffc02 */
		IKBD_AddKeyToKeyboardBuffer(0xF0);		/* Assume OK, return correct code */
		IKBD_AddKeyToKeyboardBuffer(0xF1);		/* [NP] Dragonnels demo needs this */

		/* Start timer - some commands are send during this time they may be ignored (see real ST!) */
		if (!KeyboardProcessor.bReset)
			Int_AddRelativeInterrupt(IKBD_INIT_RESET_CYCLES, INT_CPU_CYCLE, INTERRUPT_IKBD_RESETTIMER, 0);
		else
			Int_AddRelativeInterrupt(IKBD_RESET_CYCLES, INT_CPU_CYCLE, INTERRUPT_IKBD_RESETTIMER, 0);

		/* Set this 'critical' flag, gets reset when timer expires */
		bDuringResetCriticalTime = TRUE;
		bMouseDisabled = bJoystickDisabled = FALSE;
		bBothMouseAndJoy = FALSE;
	}
	/* else if not 0x80,0x01 just ignore */
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_Reset\n");
}


/*-----------------------------------------------------------------------*/
/**
 * SET MOUSE BUTTON ACTION
 *
 * 0x07
 * %00000mss  ; mouse button action
 *       ;  (m is presumed =1 when in MOUSE KEYCODE mode)
 *       ; mss=0xy, mouse button press or release causes mouse
 *       ;  position report
 *       ;  where y=1, mouse key press causes absolute report
 *       ;  and x=1, mouse key release causes absolute report
 *       ; mss=100, mouse buttons act like keys
 */
static void IKBD_Cmd_MouseAction(void)
{
	KeyboardProcessor.Mouse.Action = Keyboard.InputBuffer[1];
	KeyboardProcessor.Abs.PrevReadAbsMouseButtons = ABS_PREVBUTTONS;

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_MouseAction %d\n",
	             (unsigned int)KeyboardProcessor.Mouse.Action);
}


/*-----------------------------------------------------------------------*/
/**
 * SET RELATIVE MOUSE POSITION REPORTING
 *
 * 0x08
 */
static void IKBD_Cmd_RelMouseMode(void)
{
	KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_RelMouseMode\n");
}


/*-----------------------------------------------------------------------*/
/**
 * SET ABSOLUTE MOUSE POSITIONING
 *
 * 0x09
 * XMSB      ;X maximum (in scaled mouse clicks)
 * XLSB
 * YMSB      ;Y maximum (in scaled mouse clicks)
 * YLSB
 */
static void IKBD_Cmd_AbsMouseMode(void)
{
	/* These maximums are 'inclusive' */
	KeyboardProcessor.MouseMode = AUTOMODE_MOUSEABS;
	KeyboardProcessor.Abs.MaxX = (((unsigned int)Keyboard.InputBuffer[1])<<8) | (unsigned int)Keyboard.InputBuffer[2];
	KeyboardProcessor.Abs.MaxY = (((unsigned int)Keyboard.InputBuffer[3])<<8) | (unsigned int)Keyboard.InputBuffer[4];

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_AbsMouseMode %d,%d\n",
	             KeyboardProcessor.Abs.MaxX, KeyboardProcessor.Abs.MaxY);
}


/*-----------------------------------------------------------------------*/
/**
 * SET MOUSE KEYCODE MODE
 *
 * 0x0A
 * deltax      ; distance in X clicks to return (LEFT) or (RIGHT)
 * deltay      ; distance in Y clicks to return (UP) or (DOWN)
 */
static void IKBD_Cmd_MouseCursorKeycodes(void)
{
	KeyboardProcessor.MouseMode = AUTOMODE_MOUSECURSOR;
	KeyboardProcessor.Mouse.KeyCodeDeltaX = Keyboard.InputBuffer[1];
	KeyboardProcessor.Mouse.KeyCodeDeltaY = Keyboard.InputBuffer[2];

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_MouseCursorKeycodes %d,%d\n",
	             (int)KeyboardProcessor.Mouse.KeyCodeDeltaX,
	             (int)KeyboardProcessor.Mouse.KeyCodeDeltaY);
}


/*-----------------------------------------------------------------------*/
/**
 * SET MOUSE THRESHOLD
 *
 * 0x0B
 * X      ; x threshold in mouse ticks (positive integers)
 * Y      ; y threshold in mouse ticks (positive integers)
 */
static void IKBD_Cmd_SetMouseThreshold(void)
{
	KeyboardProcessor.Mouse.XThreshold = (unsigned int)Keyboard.InputBuffer[1];
	KeyboardProcessor.Mouse.YThreshold = (unsigned int)Keyboard.InputBuffer[2];

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_SetMouseThreshold %d,%d\n",
	             KeyboardProcessor.Mouse.XThreshold, KeyboardProcessor.Mouse.YThreshold);
}


/*-----------------------------------------------------------------------*/
/**
 * SET MOUSE SCALE
 *
 * 0x0C
 * X      ; horizontal mouse ticks per internel X
 * Y      ; vertical mouse ticks per internel Y
 */
static void IKBD_Cmd_SetMouseScale(void)
{
	KeyboardProcessor.Mouse.XScale = (unsigned int)Keyboard.InputBuffer[1];
	KeyboardProcessor.Mouse.YScale = (unsigned int)Keyboard.InputBuffer[2];

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_SetMouseScale %d,%d\n",
	             KeyboardProcessor.Mouse.XScale, KeyboardProcessor.Mouse.YScale);
}


/*-----------------------------------------------------------------------*/
/**
 * INTERROGATE MOUSE POSITION
 *
 * 0x0D
 *   Returns:  0xF7  ; absolute mouse position header
 *     BUTTONS
 *       0000dcba
 *       where a is right button down since last interrogation
 *       b is right button up since last
 *       c is left button down since last
 *       d is left button up since last
 *     XMSB      ; X coordinate
 *     XLSB
 *     YMSB      ; Y coordinate
 *     YLSB
 */
static void IKBD_Cmd_ReadAbsMousePos(void)
{
	Uint8 Buttons,PrevButtons;

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

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_ReadAbsMousePos %d,%d 0x%X\n",
	             KeyboardProcessor.Abs.X, KeyboardProcessor.Abs.Y, Buttons);
}


/*-----------------------------------------------------------------------*/
/**
 * LOAD MOUSE POSITION
 *
 * 0x0E
 * 0x00      ; filler
 * XMSB      ; X coordinate
 * XLSB      ; (in scaled coordinate system)
 * YMSB      ; Y coordinate
 * YLSB
 */
static void IKBD_Cmd_SetInternalMousePos(void)
{
	/* Setting these do not clip internal position(this happens on next update) */
	KeyboardProcessor.Abs.X = (((unsigned int)Keyboard.InputBuffer[2])<<8) | (unsigned int)Keyboard.InputBuffer[3];
	KeyboardProcessor.Abs.Y = (((unsigned int)Keyboard.InputBuffer[4])<<8) | (unsigned int)Keyboard.InputBuffer[5];

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_SetInternalMousePos %d,%d\n",
	             KeyboardProcessor.Abs.X, KeyboardProcessor.Abs.Y);
}


/*-----------------------------------------------------------------------*/
/**
 * SET Y=0 AT BOTTOM
 *
 * 0x0F
 */
static void IKBD_Cmd_SetYAxisDown(void)
{
	KeyboardProcessor.Mouse.YAxis = -1;

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_SetYAxisDown\n");
}


/*-----------------------------------------------------------------------*/
/**
 * SET Y=0 AT TOP
 *
 * 0x10
 */
static void IKBD_Cmd_SetYAxisUp(void)
{
	KeyboardProcessor.Mouse.YAxis = 1;

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_SetYAxisUp\n");
}


/*-----------------------------------------------------------------------*/
/**
 *  RESUME
 *
 * 0x11
 */
static void IKBD_Cmd_StartKeyboardTransfer(void)
{
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_StartKeyboardTransfer\n");
}


/*-----------------------------------------------------------------------*/
/**
 * DISABLE MOUSE
 *
 * 0x12
 */
static void IKBD_Cmd_TurnMouseOff(void)
{
	KeyboardProcessor.MouseMode = AUTOMODE_OFF;
	bMouseDisabled = TRUE;

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_TurnMouseOff\n");

	IKBD_CheckResetDisableBug();
}


/*-----------------------------------------------------------------------*/
/**
 * PAUSE OUTPUT
 *
 * 0x13
 */
static void IKBD_Cmd_StopKeyboardTransfer(void)
{
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_StopKeyboardTransfer\n");
}


/*-----------------------------------------------------------------------*/
/**
 * SET JOYSTICK EVENT REPORTING
 *
 * 0x14
 */
static void IKBD_Cmd_ReturnJoystickAuto(void)
{
	KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK;
	KeyboardProcessor.MouseMode = AUTOMODE_OFF;

	/* Again, if try to disable mouse within time of a reset it isn't disabled! */
	if (bDuringResetCriticalTime)
	{
		KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
		bBothMouseAndJoy = TRUE;
	}

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_ReturnJoystickAuto\n");
}


/*-----------------------------------------------------------------------*/
/**
 * SET JOYSTICK INTERROGATION MODE
 *
 * 0x15
 */
static void IKBD_Cmd_StopJoystick(void)
{
	KeyboardProcessor.JoystickMode = AUTOMODE_OFF;
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_StopJoystick\n");
}


/*-----------------------------------------------------------------------*/
/**
 * JOYSTICK INTERROGATE
 *
 * 0x16
 */
static void IKBD_Cmd_ReturnJoystick(void)
{
	IKBD_AddKeyToKeyboardBuffer(0xFD);
	IKBD_AddKeyToKeyboardBuffer(Joy_GetStickData(0));
	IKBD_AddKeyToKeyboardBuffer(Joy_GetStickData(1));

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_ReturnJoystick\n");
}


/*-----------------------------------------------------------------------*/
/**
 * SET JOYSTICK MONITORING
 *
 * 0x17
 * rate      ; time between samples in hundreths of a second
 *   Returns: (in packets of two as long as in mode)
 *     %000000xy  where y is JOYSTICK1 Fire button
 *         and x is JOYSTICK0 Fire button
 *     %nnnnmmmm  where m is JOYSTICK1 state
 *         and n is JOYSTICK0 state
 */
static void IKBD_Cmd_SetJoystickDuration(void)
{
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_SetJoystickDuration\n");
}


/*-----------------------------------------------------------------------*/
/**
 * SET FIRE BUTTON MONITORING
 *
 * 0x18
 *   Returns: (as long as in mode)
 *     %bbbbbbbb  ; state of the JOYSTICK1 fire button packed
 *           ; 8 bits per byte, the first sample if the MSB
 */
static void IKBD_Cmd_SetJoystickFireDuration(void)
{
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_SetJoystickFireDuration\n");
}


/*-----------------------------------------------------------------------*/
/**
 * SET JOYSTICK KEYCODE MODE
 *
 * 0x19
 * RX        ; length of time (in tenths of seconds) until
 *         ; horizontal velocity breakpoint is reached
 * RY        ; length of time (in tenths of seconds) until
 *         ; vertical velocity breakpoint is reached
 * TX        ; length (in tenths of seconds) of joystick closure
 *         ; until horizontal cursor key is generated before RX
 *         ; has elapsed
 * TY        ; length (in tenths of seconds) of joystick closure
 *         ; until vertical cursor key is generated before RY
 *         ; has elapsed
 * VX        ; length (in tenths of seconds) of joystick closure
 *         ; until horizontal cursor keystokes are generated after RX
 *         ; has elapsed
 * VY        ; length (in tenths of seconds) of joystick closure
 *         ; until vertical cursor keystokes are generated after RY
 *         ; has elapsed
 */
static void IKBD_Cmd_SetCursorForJoystick(void)
{
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_SetCursorForJoystick\n");
}


/*-----------------------------------------------------------------------*/
/**
 * DISABLE JOYSTICKS
 *
 * 0x1A
 */
static void IKBD_Cmd_DisableJoysticks(void)
{
	KeyboardProcessor.JoystickMode = AUTOMODE_OFF;
	bJoystickDisabled = TRUE;

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_DisableJoysticks\n");

	IKBD_CheckResetDisableBug();
}


/*-----------------------------------------------------------------------*/
/**
 * TIME-OF-DAY CLOCK SET
 *
 * 0x1B
 * YY        ; year (2 least significant digits)
 * MM        ; month
 * DD        ; day
 * hh        ; hour
 * mm        ; minute
 * ss        ; second
 */
static void IKBD_Cmd_SetClock(void)
{
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_SetClock\n");
}


/*-----------------------------------------------------------------------*/
/**
 * Convert value to 2-digit BCD
 */
static unsigned char IKBD_ConvertToBCD(unsigned short int Value)
{
	return (((Value/10))<<4) | (Value%10);
}


/*-----------------------------------------------------------------------*/
/**
 * INTERROGATE TIME-OF-DAT CLOCK
 *
 * 0x1C
 *   Returns:
 *     0xFC  ; time-of-day event header
 *     YY    ; year (2 least significant digits)
 *     There seems to be a problem with the bcd conversion of the year
 *     when year/10 >= 10. So the bcd conversion keeps the part > 10.
 *     If you put year%100 here (as says the doc), and put a real bcd
 *     conversion function in misc.c, then you end up with year 2031
 *     instead of 2003...
 *
 *     MM    ; month
 *     DD    ; day
 *     hh    ; hour
 *     mm    ; minute
 *     ss    ; second
 */
static void IKBD_Cmd_ReadClock(void)
{
	struct tm *SystemTime;
	time_t nTimeTicks;

	/* Get system time */
	nTimeTicks = time(NULL);
	SystemTime = localtime(&nTimeTicks);

	/* Return packet */
	IKBD_AddKeyToKeyboardBuffer(0xFC);
	/* Return time-of-day clock as yy-mm-dd-hh-mm-ss as BCD */
	IKBD_AddKeyToKeyboardBuffer(IKBD_ConvertToBCD(SystemTime->tm_year));  /* yy - year (2 least significant digits) */
	IKBD_AddKeyToKeyboardBuffer(IKBD_ConvertToBCD(SystemTime->tm_mon+1)); /* mm - Month */
	IKBD_AddKeyToKeyboardBuffer(IKBD_ConvertToBCD(SystemTime->tm_mday));  /* dd - Day */
	IKBD_AddKeyToKeyboardBuffer(IKBD_ConvertToBCD(SystemTime->tm_hour));  /* hh - Hour */
	IKBD_AddKeyToKeyboardBuffer(IKBD_ConvertToBCD(SystemTime->tm_min));   /* mm - Minute */
	IKBD_AddKeyToKeyboardBuffer(IKBD_ConvertToBCD(SystemTime->tm_sec));   /* ss - Second */

	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_ReadClock\n");
}


/*-----------------------------------------------------------------------*/
/**
 * MEMORY LOAD
 *
 * 0x20
 * ADRMSB      ; address in controller
 * ADRLSB      ; memory to be loaded
 * NUM        ; number of bytes (0-128)
 * { data }
 */
static void IKBD_Cmd_LoadMemory(void)
{
	HATARI_TRACE ( HATARI_TRACE_IKBD_CMDS , "IKBD_Cmd_LoadMemory addr 0x%x count %d\n" ,
		( Keyboard.InputBuffer[1]<<8 ) + Keyboard.InputBuffer[2] , Keyboard.InputBuffer[3] );

	MemoryLoadNbBytesTotal = Keyboard.InputBuffer[3];
	MemoryLoadNbBytesLeft = MemoryLoadNbBytesTotal;
	crc32_reset ( &MemoryLoadCrc );
}


/*-----------------------------------------------------------------------*/
/**
 * MEMORY READ
 *
 * 0x21
 * ADRMSB        ; address in controller
 * ADRLSB        ; memory to be read
 *   Returns:
 *     0xF6    ; status header
 *     0x20    ; memory access
 *     { data }  ; 6 data bytes starting at ADR
 */
static void IKBD_Cmd_ReadMemory(void)
{
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_ReadMemory\n");
}


/*-----------------------------------------------------------------------*/
/**
 * CONTROLLER EXECUTE
 *
 * 0x22
 * ADRMSB      ; address of subroutine in
 * ADRLSB      ; controller memory to be called
 */
static void IKBD_Cmd_Execute(void)
{
	HATARI_TRACE(HATARI_TRACE_IKBD_CMDS, "IKBD_Cmd_Execute\n");
	
	if ( pIKBD_CustomCodeHandler_Write )
	{
		HATARI_TRACE ( HATARI_TRACE_IKBD_EXEC, "ikbd execute addr 0x%x using custom handler\n" ,
			( Keyboard.InputBuffer[1]<<8 ) + Keyboard.InputBuffer[2] );

		IKBD_ExeMode = TRUE;				/* turn 6301's custom mode ON */
	}
	else							/* unknown code uploaded to ikbd RAM */
	{
		HATARI_TRACE ( HATARI_TRACE_IKBD_EXEC, "ikbd execute addr 0x%x ignored, no custom handler found\n" ,
			( Keyboard.InputBuffer[1]<<8 ) + Keyboard.InputBuffer[2] );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Send data to keyboard processor via ACIA by writing to address 0xfffc02.
 * For our emulation we bypass the ACIA (I've yet to see anything check for this)
 * and add the byte directly into the keyboard input buffer.
 */
static void IKBD_RunKeyboardCommand(Uint16 aciabyte)
{
	int i=0;

	/* Write into our keyboard input buffer */
	Keyboard.InputBuffer[Keyboard.nBytesInInputBuffer++] = aciabyte;

	/* Now check bytes to see if we have a valid/in-valid command string set */
	while (KeyboardCommands[i].Command!=0xff)
	{
		/* Found command? */
		if (KeyboardCommands[i].Command==Keyboard.InputBuffer[0])
		{
			/* Is string complete, then can execute? */
			if (KeyboardCommands[i].NumParameters==Keyboard.nBytesInInputBuffer)
			{
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
/**
 * Send byte to our keyboard processor, and execute
 */
static void IKBD_SendByteToKeyboardProcessor(Uint16 bl)
{
	/* If IKBD is executing custom code, send the byte to the function handling this code */
	if ( IKBD_ExeMode && pIKBD_CustomCodeHandler_Write )
	{
		(*pIKBD_CustomCodeHandler_Write) ( (Uint8) bl );
		return;
	}

	if ( MemoryLoadNbBytesLeft == 0 )		/* No pending MemoryLoad command */
		IKBD_RunKeyboardCommand ( bl );		/* check for known commands */

	else						/* MemoryLoad command is not finished yet */
		IKBD_LoadMemoryByte ( (Uint8) bl );	/* process bytes sent to the ikbd RAM */
}


/*-----------------------------------------------------------------------*/
/**
 * The byte stored in the ACIA 'ACIAByte' has been read by the CPU by reading from
 * address $fffc02. We clear the status flag and set the GPIP register to signal read.
 */
Uint16 IKBD_GetByteFromACIA(void)
{
	/* ACIA is now reset */
	ACIAStatusRegister &= ~(ACIA_STATUS_REGISTER__RX_BUFFER_FULL | ACIA_STATUS_REGISTER__INTERRUPT_REQUEST | ACIA_STATUS_REGISTER__OVERRUN_ERROR);

	/* GPIP I4 - General Purpose Pin Keyboard/MIDI interrupt */
	MFP_GPIP |= 0x10;
	return ACIAByte;  /* Return byte from keyboard */
}


/*-----------------------------------------------------------------------*/
/**
 * Byte received in the ACIA from the keyboard processor. Store byte for read from $fffc02
 * and clear the GPIP I4 register. This register will be remain low until byte has been
 * read from ACIA.
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
	MFP_InputOnChannel(MFP_ACIA_BIT, MFP_IERB, &MFP_IPRB);

	/* Clear flag so can allow another byte to be sent along serial line */
	bByteInTransitToACIA = FALSE;
	/* If another key is waiting, start sending from keyboard processor now */
	if (Keyboard.BufferHead!=Keyboard.BufferTail)
		IKBD_SendByteToACIA();
}


/*-----------------------------------------------------------------------*/
/**
 * Send a byte from the keyboard buffer to the ACIA. On a real ST this takes some time to send
 * so we must be as accurate in the timing as possible - bytes do not appear to the 68000 instantly!
 * We do this via an internal interrupt - neat!
 */
static void IKBD_SendByteToACIA(void)
{
	/* Transmit byte from keyboard processor to ACIA. This takes approx ACIA_CYCLES CPU clock cycles to complete */
	if (!bByteInTransitToACIA)
	{
		/* Send byte to ACIA */
		Int_AddRelativeInterrupt(ACIA_CYCLES, INT_CPU_CYCLE, INTERRUPT_IKBD_ACIA, 0);
		/* Set flag so only transmit one byte at a time */
		bByteInTransitToACIA = TRUE;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Add character to our internal keyboard buffer. These bytes are then sent one at a time to the ACIA.
 * This is done via a delay to mimick the STs internal workings, as this is needed for games such
 * as Carrier Command.
 */
static void IKBD_AddKeyToKeyboardBuffer(Uint8 Data)
{
	if ( IKBD_ExeMode )					/* if IKBD is executing custom code, don't add */
		return;						/* anything to the buffer */

	IKBD_AddKeyToKeyboardBuffer_Real ( Data );
}


static void IKBD_AddKeyToKeyboardBuffer_Real(Uint8 Data)
{
	/* Is keyboard initialised yet? Ignore any bytes until it is */
	if (!KeyboardProcessor.bReset)
		return;

	/* Check we have space to add byte */
	if (Keyboard.BufferHead!=((Keyboard.BufferTail+1)&KEYBOARD_BUFFER_MASK))
	{
		/* Add byte to our buffer */
		Keyboard.Buffer[Keyboard.BufferTail++] = Data;
		Keyboard.BufferTail &= KEYBOARD_BUFFER_MASK;

		/* We have character ready to transmit from the ACIA - see if can send it now */
		IKBD_SendByteToACIA();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * When press/release key under host OS, execute this function.
 */
void IKBD_PressSTKey(Uint8 ScanCode, bool bPress)
{
	/* Store the state of each ST scancode : 1=pressed 0=released */	
	if ( bPress )		ScanCodeState[ ScanCode & 0x7f ] = 1;
	else			ScanCodeState[ ScanCode & 0x7f ] = 0;	
	
	if (!bPress)
		ScanCode |= 0x80;    /* Set top bit if released key */
	IKBD_AddKeyToKeyboardBuffer(ScanCode);  /* And send to keyboard processor */
}


/*-----------------------------------------------------------------------*/
/**
 * Handle read from keyboard control ACIA register (0xfffc00)
 */
void IKBD_KeyboardControl_ReadByte(void)
{
	/* ACIA registers need wait states - but the value seems to vary in certain cases */
	M68000_WaitState(8);

	/* For our emulation send is immediate so acknowledge buffer is empty */
	IoMem[0xfffc00] = ACIAStatusRegister | ACIA_STATUS_REGISTER__TX_BUFFER_EMPTY;

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_IKBD_ACIA ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "ikbd read fffc00 ctrl=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
		                     IoMem[0xfffc00], nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from keyboard data ACIA register (0xfffc02)
 */
void IKBD_KeyboardData_ReadByte(void)
{
	/* ACIA registers need wait states - but the value seems to vary in certain cases */
	M68000_WaitState(8);

	/* If IKBD is executing custom code, call the function to update the byte read in $fffc02 */
	if ( IKBD_ExeMode && pIKBD_CustomCodeHandler_Read )
	{
		(*pIKBD_CustomCodeHandler_Read) ();
	}


	IoMem[0xfffc02] = IKBD_GetByteFromACIA();  /* Return our byte from keyboard processor */

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_IKBD_ACIA ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "ikbd read fffc02 data=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
		                     IoMem[0xfffc02], nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle write to keyboard control ACIA register (0xfffc00)
 */
void IKBD_KeyboardControl_WriteByte(void)
{
	/* ACIA registers need wait states - but the value seems to vary in certain cases */
	M68000_WaitState(8);

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_IKBD_ACIA ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "ikbd write fffc00 ctrl=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
		                     IoMem[0xfffc00], nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}

	/* [NP] We only handle reset of the ACIA */
	if ( ( IoMem[0xfffc00] & 0x03 ) == 0x03 )
		ACIA_Reset();

	/* Nothing... */
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to keyboard data ACIA register (0xfffc02)
 */
void IKBD_KeyboardData_WriteByte(void)
{
	/* ACIA registers need wait states - but the value seems to vary in certain cases */
	M68000_WaitState(8);

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_IKBD_ACIA ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "ikbd write fffc02 data=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
		                     IoMem[0xfffc02], nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}

	IKBD_SendByteToKeyboardProcessor(IoMem[0xfffc02]);  /* Pass our byte to the keyboard processor */
}


/*************************************************************************/
/**
 * Below part is for emulating custom 6301 program sent to the ikbd RAM
 * Specific read/write functions for each demo/game should be added here,
 * after being defined in the CustomCodeDefinitions[] array.
 *
 * The 6301 has 256 bytes of RAM, but only 128 bytes are available to
 * put a program (from $80 to $ff).
 *
 * Executing a program in the 6301 is a 2 steps process :
 *	1) a very small program is sent to the RAM using the 0x20 command.
 *	   This is often loaded at address $b0.
 * 	   This program will stop interruptions in the 6301 and will accept
 *	   a second small program that will relocate itself to $80.
 *	2) the relocated program at address $80 will accept a third (main)
 *	   program and will execute it once reception is complete.
 *
 * Writes during step 1 are handled with the ExeBootHandler matching the
 * LoadMemory CRC.
 * ExeBootHandler will compute a 2nd CRC for the writes corresponding to
 * the 2nd and 3rd programs sent to the 6301's RAM.
 *
 * If a match is found for this 2nd CRC, we will override default ikbd's behaviour
 * for reading/writing to $fffc02 with ExeMainHandler_Read / ExeMainHandler_Write
 * (once the Execute command 0x22 is received).
 *
 * When using custom program (ExeMode==TRUE), we must ignore all keyboard/mouse/joystick
 * events sent to IKBD_AddKeyToKeyboardBuffer. Only our functions can add bytes
 * to the keyboard buffer.
 *
 * To exit 6301's execution mode, we can use the 68000 'reset' instruction.
 * Some 6301's programs also handle a write to $fffc02 as an exit signal.
 */


/*-----------------------------------------------------------------------*/
/**
 * Handle writes to $fffc02 when loading bytes in the ikbd RAM.
 * We compute a CRC of the bytes that are sent until MemoryLoadNbBytesLeft
 * reaches 0.
 * When all bytes are loaded, we look for a matching CRC ; if found, we
 * use the ExeBootHandler defined for this CRC to process the next writes
 * that will occur in $fffc02.
 * LoadMemory is often used to load a small boot code into the 6301's RAM.
 * This small program will be executed later using the command 0x22.
 */

static void IKBD_LoadMemoryByte ( Uint8 aciabyte )
{
	unsigned int i;

	crc32_add_byte ( &MemoryLoadCrc , aciabyte );

	MemoryLoadNbBytesLeft--;
	if ( MemoryLoadNbBytesLeft == 0 )				/* all bytes were received */
	{
		/* Search for a match amongst the known custom routines */
		for ( i = 0 ; i < sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ) ; i++ )
			if ( CustomCodeDefinitions[ i ].LoadMemCrc == MemoryLoadCrc )
				break;

		if ( i < sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ) )	/* found */
		{
			HATARI_TRACE ( HATARI_TRACE_IKBD_EXEC, "ikbd loadmemory %d bytes crc=0x%x matches <%s>\n" ,
				MemoryLoadNbBytesTotal , MemoryLoadCrc , CustomCodeDefinitions[ i ].Name );

			crc32_reset ( &MemoryLoadCrc );
			MemoryExeNbBytes = 0;
			pIKBD_CustomCodeHandler_Read = NULL;
			pIKBD_CustomCodeHandler_Write = CustomCodeDefinitions[ i ].ExeBootHandler;
		}

		else							/* unknown code uploaded to ikbd RAM */
		{
			HATARI_TRACE ( HATARI_TRACE_IKBD_EXEC, "ikbd loadmemory %d bytes crc=0x%x : unknown code\n" ,
				MemoryLoadNbBytesTotal , MemoryLoadCrc );

			pIKBD_CustomCodeHandler_Read = NULL;
			pIKBD_CustomCodeHandler_Write = NULL;
		}
	}
}



/*-----------------------------------------------------------------------*/
/**
 * Handle writes to $fffc02 when executing custom code in the ikbd RAM.
 * This is used to send the small ikdb program that will handle keyboard/mouse/joystick
 * input.
 * We compute a CRC of the bytes that are sent until we found a match
 * with a known custom ikbd program.
 */

static void IKBD_CustomCodeHandler_CommonBoot ( Uint8 aciabyte )
{
	unsigned int i;

	crc32_add_byte ( &MemoryLoadCrc , aciabyte );
	MemoryExeNbBytes++;

	HATARI_TRACE ( HATARI_TRACE_IKBD_EXEC, "ikbd custom exe common boot write 0x%02x count %d crc=0x%x\n" ,
		aciabyte , MemoryExeNbBytes , MemoryLoadCrc );

	/* Search for a match amongst the known custom routines */
	for ( i = 0 ; i < sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ) ; i++ )
		if ( ( CustomCodeDefinitions[ i ].MainProgNbBytes == MemoryExeNbBytes )
			&& ( CustomCodeDefinitions[ i ].MainProgCrc == MemoryLoadCrc ) )
			break;

	if ( i < sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ) )	/* found */
	{
		HATARI_TRACE ( HATARI_TRACE_IKBD_EXEC, "ikbd custom exe common boot, uploaded code matches <%s>\n" ,
			CustomCodeDefinitions[ i ].Name );

		pIKBD_CustomCodeHandler_Read = CustomCodeDefinitions[ i ].ExeMainHandler_Read;
		pIKBD_CustomCodeHandler_Write = CustomCodeDefinitions[ i ].ExeMainHandler_Write;
		
		Keyboard.BufferHead = Keyboard.BufferTail = 0;	/* flush all queued bytes that would be read in $fffc02 */
		(*pIKBD_CustomCodeHandler_Read) ();		/* initialize ACIAByte */
	}

	/* If not found, we keep on accumulating bytes until we find a matching crc */
}



/*----------------------------------------------------------------------*/
/* Froggies Over The Fence menu.					*/
/* Returns 2 bytes with the mouse position, keyboard can be used too.	*/
/* Writing 0xff to $fffc02 will cause the 6301 to exit custom exe mode.	*/
/*----------------------------------------------------------------------*/

static void IKBD_CustomCodeHandler_FroggiesMenu_Read ( void )
{
	Uint8		res1 = 0;
	Uint8		res2 = 0;
	
	if ( KeyboardProcessor.Mouse.DeltaX < 0 )	res1 = 0x7a;	/* mouse left */
	if ( KeyboardProcessor.Mouse.DeltaX > 0 )	res1 = 0x06;	/* mouse right */
	if ( KeyboardProcessor.Mouse.DeltaY < 0 )	res2 = 0x7a;	/* mouse up */
	if ( KeyboardProcessor.Mouse.DeltaY > 0 )	res2 = 0x06;	/* mouse down */
	if ( Keyboard.bLButtonDown & BUTTON_MOUSE )	res1 |= 0x80;	/* left mouse button */

	if ( ScanCodeState[ 0x4b ] )			res1 |= 0x7a;	/* left */
	if ( ScanCodeState[ 0x4d ] )			res1 |= 0x06;	/* right */
	if ( ScanCodeState[ 0x48 ] )			res2 |= 0x7a;	/* up */
	if ( ScanCodeState[ 0x50 ] )			res2 |= 0x06;	/* down */
	if ( ScanCodeState[ 0x70 ] )			res1 |= 0x80;	/* keypad 0 */

	IKBD_AddKeyToKeyboardBuffer_Real ( res1 );
	IKBD_AddKeyToKeyboardBuffer_Real ( res2 );
}

static void IKBD_CustomCodeHandler_FroggiesMenu_Write ( Uint8 aciabyte )
{
	/* When writing 0xff to $fffc02, Froggies ikbd's program will terminate itself */
	/* and leave Execution mode */
	if ( aciabyte == 0xff )
		IKBD_Reset_ExeMode ();
}



/*----------------------------------------------------------------------*/
/* Transbeauce II menu.							*/
/* Returns 1 byte with the joystick position, keyboard can be used too.	*/
/*----------------------------------------------------------------------*/

static void IKBD_CustomCodeHandler_Transbeauce2Menu_Read ( void )
{
	Uint8		res = 0;

	/* keyboard emulation */
	if ( ScanCodeState[ 0x48 ] )	res |= 0x01;		/* up */
	if ( ScanCodeState[ 0x50 ] )	res |= 0x02;		/* down */
	if ( ScanCodeState[ 0x4b ] )	res |= 0x04;		/* left */
	if ( ScanCodeState[ 0x4d ] )	res |= 0x08;		/* right */
	if ( ScanCodeState[ 0x62 ] )	res |= 0x40;		/* help */
	if ( ScanCodeState[ 0x39 ] )	res |= 0x80;		/* space */

	/* joystick emulation (bit mapping is same as cursor above, with bit 7 = fire button */
	res |= ( Joy_GetStickData(1) & 0x8f ) ;			/* keep bits 0-3 and 7 */
	
	IKBD_AddKeyToKeyboardBuffer_Real ( res );
}

static void IKBD_CustomCodeHandler_Transbeauce2Menu_Write ( Uint8 aciabyte )
{
  /* Ignore write */
}



/*----------------------------------------------------------------------*/
/* Dragonnels demo menu.						*/
/* Returns 1 byte with the Y position of the mouse.			*/
/*----------------------------------------------------------------------*/

static void IKBD_CustomCodeHandler_DragonnelsMenu_Read ( void )
{
	Uint8		res = 0;
	
	if ( KeyboardProcessor.Mouse.DeltaY < 0 )	res = 0xfc;	/* mouse up */
	if ( KeyboardProcessor.Mouse.DeltaY > 0 )	res = 0x04;	/* mouse down */

	if ( Keyboard.bLButtonDown & BUTTON_MOUSE )	res = 0x80;	/* left mouse button */

	IKBD_AddKeyToKeyboardBuffer_Real ( res );
}

static void IKBD_CustomCodeHandler_DragonnelsMenu_Write ( Uint8 aciabyte )
{
  /* Ignore write */
}

