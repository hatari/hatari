/*
  Hatari - ikbd.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  The keyboard processor(6301) handles any joystick/mouse/keyboard task
  and sends bytes to the ACIA(6850).
  The IKBD has a small ROM which is used to process various commands send
  by the main CPU to the THE IKBD.
  Due to lack of real HD6301 emulation, those commands are handled by
  functionally equivalent code that tries to be as close as possible
  to a real HD6301.

  For program using their own HD6301 code, we also use some custom
  handlers to emulate the expected result.
*/

const char IKBD_fileid[] = "Hatari ikbd.c";

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
/* 2011/05/11	[NP]	Add proper support for emulating TX buffer empty/full in status register bit 1	*/
/*			when writing to $fffc02 (using an internal timer).				*/
/* 2011/07/14	[NP]	Don't clear bytes in transit when ACIA_Reset is called ; if a byte is sent to	*/
/*			the ikbd it should not be cancelled ? FIXME : this would need more tests on a	*/
/*			real ST (fix Froggies Over The Fence's menu when selecting a demo).		*/
/* 2011/07/31	[NP]	Don't clear bytes in transit in the ACIA when the IKBD is reset (fix Overdrive	*/
/*			by Phalanx).									*/
/* 2011/12/27	[NP]	When sending new bytes while a byte is already in transfer from ACIA to IKBD,	*/
/*			don't restart the internal TX timer (fix 'Pandemonium Demos' Intro).		*/
/*			eg : .loop : move.b d0,$fc02.w    btst #1,$fc00.w    beq.s .loop		*/
/* 2012/01/22	[NP]	Enable both mouse and joystick reporting when commands 0x12 and 0x14 are	*/
/*			received during the IKBD reset.							*/
/* 2012/02/26	[NP]	Handle TX interrupt in the ACIA (eg by sending 0xb6 instead of 0x96 after	*/
/*			resetting the ACIA) (fix the game 'Hades Nebula').				*/
/* 2012/10/10	[NP]	Use the new ACIA emulation in acia.c ; add support for the IKBD's SCI, which	*/
/*			is similar to the ACIA, with fixed 8 data bits, 1 stop bit and no parity bit.	*/
/* 2012/12/23	[NP]	Fix timings for the commands $16, $1C, $87-$9A. The first byte is returned	*/
/*			between 'min' and 'max' cycles after receiving the full command. The delay	*/
/*			is not fixed to simulate the slight variations measured on a real ST.		*/
/* 2012/12/24	[NP]	Rewrite SetClock and ReadClock commands to behave like the real IKBD.		*/
/*			Instead of using time()/localtime() to handle the clock, we now increment it	*/
/*			on each VBL, taking care of the BCD data (overflows and such) like in the IKBD.	*/
/*			(this new code is based on the HD6301 disassembly of the IKBD's ROM)		*/
/* 2013/01/02	[NP]	- Use IKBD_OutputBuffer_CheckFreeCount to ensure there's enough room in the	*/
/*			output buffer before sending an IKBD packet. If there's not enough bytes to	*/
/*			transfer the whole packet, then the packet must be discarded.			*/
/*			- Don't ignore a new RDR byte if the output buffer is not empty yet. The IKBD	*/
/*			can handle new bytes asynchronously using some interrupt while still processing	*/
/*			another command. New RDR is discarded only if the input buffer is full.		*/
/* 2013/01/13	[NP]	For hardware and software reset, share the common code in IKBD_Boot_ROM().	*/
/* 2014/07/06	[NP]	Ignore command 0x13 IKBD_Cmd_StopKeyboardTransfer during ikbd's reset. This is	*/
/*			required for the loader of 'Just Bugging' by ACF which sends 0x11 and 0x13 just	*/
/*			after 0x80 0x01 (temporary fix, would need to be measured on a real STF to see	*/
/*			if it's always ignored or just during a specific delay)				*/
/* 2015/10/10	[NP]	When IKBD_Reset / IKBD_Boot_ROM are called, we should not restart the autosend	*/
/*			handler INTERRUPT_IKBD_AUTOSEND if it's already set, else we can loose keyboard	*/
/*			input if IKBD_Reset is called in a loop before any event could be processed	*/
/*			(this could happen if a program called the 'RESET' instruction in a loop, then	*/
/*			we lost the F12 key for example)  (fix endless RESET when doing a warm reset	*/
/*			(alt+r) during the 'Vodka Demo' by 'Equinox', only solution was to kill Hatari)	*/
/* 2017/10/27	[TS]	Add Audio Sculpture 6301 program checksum and emulation				*/


#include <inttypes.h>

#include "main.h"
#include "configuration.h"
#include "ikbd.h"
#include "cycles.h"
#include "cycInt.h"
#include "ioMem.h"
#include "joy.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "video.h"
#include "utils.h"
#include "acia.h"
#include "clocks_timings.h"


#define DBL_CLICK_HISTORY  0x07     /* Number of frames since last click to see if need to send one or two clicks */
#define ACIA_CYCLES    7200         /* Cycles (Multiple of 4) between sent to ACIA from keyboard along serial line - 500Hz/64, (approx' 6920-7200cycles from test program) */

#define ABS_X_ONRESET    0          /* Initial XY for absolute mouse position after RESET command */
#define ABS_Y_ONRESET    0
#define ABS_MAX_X_ONRESET  320      /* Initial absolute mouse limits after RESET command */
#define ABS_MAY_Y_ONRESET  200      /* These values are never actually used as user MUST call 'IKBD_Cmd_AbsMouseMode' before ever using them */

#define ABS_PREVBUTTONS  (0x02|0x8) /* Don't report any buttons up on first call to 'IKBD_Cmd_ReadAbsMousePos' */

#define IKBD_RESET_CYCLES  502000	/* Number of cycles (for a 68000 at 8 MHz) between sending the reset command and receiving $F1 */

#define	IKBD_ROM_VERSION	0xF1	/* On reset, the IKBD will return either 0xF0 or 0xF1, depending on the IKBD's ROM */
					/* version. Only very early ST returned 0xF0, so we use 0xF1 which is the most common case.*/
					/* Beside, some programs explicitly wait for 0xF1 after a reset (Dragonnels demo) */


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
static bool bMouseEnabledDuringReset;




/*
  HD6301 processor by Hitachi

  References :
   - HD6301V1, HD63A01V1, HD63B01V1 CMOS MCU datasheet by Hitachi

  The HD6301 is connected to the ACIA through TX and RX pins.
  Serial transfers are made with 8 bit word, 1 stop bit, no parity and 7812.5 baud

  The IKBD's ROM is using 2 buffers to handle input/output on the serial line
  in an asynchronous way, by using the SCI's interrupt at address $FEE2. This means
  the IKBD can execute a new command as soon as the current one is completed, as it is
  the interrupt function that will handle sending bytes to the ACIA.

  Input buffer : 8 bytes, located at $CD-$D4 in the IKBD's RAM.
	New bytes received in RDR are added to this buffer, until we have
	enough bytes to obtain a valid command (with its potential parameters)
	If the buffer already contains 8 bytes, new bytes are ignored (lost).
	This buffer is emptied if a valid command was processed or if the first
	byte in the buffer is not a valid command.

  Output buffer : 20 bytes as a ring buffer, located at $D9-$ED in the IKBD's RAM.
	When the IKBD automatically reports events or when a command returns some bytes,
	those 'n' bytes are added to the ring buffer.
	If the ring buffer doesn't have enough space to store 'n' new bytes, the 'n' bytes
	are ignored (lost).
	Each time a byte is correctly sent in TDR, a new byte is processed, until the ring
	buffer becomes empty.


  Special behaviours during the IKBD reset :
    If the following commands are received during the reset of the IKBD,
    the IKBD will go in a special mode and report both mouse and joystick at the same time :
	0x08 0x14		relative mouse on , joysticks auto
	0x08 0x0b 0x14		relative mouse on , mouse threshold , joysticks auto (eg Barbarian 1 by Psygnosis)
	0x12 0x14		disable mouse , joysticks auto (eg Hammerfist)
	0x12 0x1a		disable mouse , disable joysticks

    In that case mouse and joystick buttons will be reported in a "mouse report" packet
    and joystick actions (except buttons) will be reported in a "joystick report" packet.

*/


static void IKBD_RunKeyboardCommand(uint8_t aciabyte);


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
static void IKBD_Cmd_SetJoystickMonitoring(void);
static void IKBD_Cmd_SetJoystickFireDuration(void);
static void IKBD_Cmd_SetCursorForJoystick(void);
static void IKBD_Cmd_DisableJoysticks(void);
static void IKBD_Cmd_SetClock(void);
static void IKBD_Cmd_ReadClock(void);
static void IKBD_Cmd_LoadMemory(void);
static void IKBD_Cmd_ReadMemory(void);
static void IKBD_Cmd_Execute(void);
static void IKBD_Cmd_ReportMouseAction(void);
static void IKBD_Cmd_ReportMouseMode(void);
static void IKBD_Cmd_ReportMouseThreshold(void);
static void IKBD_Cmd_ReportMouseScale(void);
static void IKBD_Cmd_ReportMouseVertical(void);
static void IKBD_Cmd_ReportMouseAvailability(void);
static void IKBD_Cmd_ReportJoystickMode(void);
static void IKBD_Cmd_ReportJoystickAvailability(void);

/* Keyboard Command */
static const struct {
  uint8_t Command;
  uint8_t NumParameters;
  void (*pCallFunction)(void);
} KeyboardCommands[] =
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
	{ 0x17,2,  IKBD_Cmd_SetJoystickMonitoring },
	{ 0x18,1,  IKBD_Cmd_SetJoystickFireDuration },
	{ 0x19,7,  IKBD_Cmd_SetCursorForJoystick },
	{ 0x1A,1,  IKBD_Cmd_DisableJoysticks },
	{ 0x1B,7,  IKBD_Cmd_SetClock },
	{ 0x1C,1,  IKBD_Cmd_ReadClock },
	{ 0x20,4,  IKBD_Cmd_LoadMemory },
	{ 0x21,3,  IKBD_Cmd_ReadMemory },
	{ 0x22,3,  IKBD_Cmd_Execute },

	/* Report message (top bit set) */
	{ 0x87,1,  IKBD_Cmd_ReportMouseAction },
	{ 0x88,1,  IKBD_Cmd_ReportMouseMode },
	{ 0x89,1,  IKBD_Cmd_ReportMouseMode },
	{ 0x8A,1,  IKBD_Cmd_ReportMouseMode },
	{ 0x8B,1,  IKBD_Cmd_ReportMouseThreshold },
	{ 0x8C,1,  IKBD_Cmd_ReportMouseScale },
	{ 0x8F,1,  IKBD_Cmd_ReportMouseVertical },
	{ 0x90,1,  IKBD_Cmd_ReportMouseVertical },
	{ 0x92,1,  IKBD_Cmd_ReportMouseAvailability },
	{ 0x94,1,  IKBD_Cmd_ReportJoystickMode },
	{ 0x95,1,  IKBD_Cmd_ReportJoystickMode },
	{ 0x99,1,  IKBD_Cmd_ReportJoystickMode },
	{ 0x9A,1,  IKBD_Cmd_ReportJoystickAvailability },

	{ 0xFF,0,  NULL }  /* Term */
};




/*----------------------------------------------------------------------*/
/* Variables/defines/functions used to transfer data between the	*/
/* IKBD's SCI and the ACIA.						*/
/*----------------------------------------------------------------------*/

#define	IKBD_TRCSR_BIT_WU			0x01		/* Wake Up */
#define	IKBD_TRCSR_BIT_TE			0x02		/* Transmit Enable */
#define	IKBD_TRCSR_BIT_TIE			0x04		/* Transmit Interrupt Enable */
#define	IKBD_TRCSR_BIT_RE			0x08		/* Receive Enable */
#define	IKBD_TRCSR_BIT_RIE			0x10		/* Receive Interrupt Enable */
#define	IKBD_TRCSR_BIT_TDRE			0x20		/* Transmit Data Register Empty */
#define	IKBD_TRCSR_BIT_ORFE			0x40		/* Over Run Framing Error */
#define	IKBD_TRCSR_BIT_RDRF			0x80		/* Receive Data Register Full */



/* Possible states when handling TX/RX in the IKBD's Serial Communication Interface */
enum
{
	IKBD_SCI_STATE_IDLE = 0,
	IKBD_SCI_STATE_DATA_BIT,
	IKBD_SCI_STATE_STOP_BIT
};


typedef struct {
	/* IKBD's SCI internal registers */
	uint8_t		RMCR;					/* reg 0x10 : Rate and Mode Control Register */
	uint8_t		TRCSR;					/* reg 0x11 : Transmit/Receive Control and Status Register */
	uint8_t		TDR;					/* reg 0x12 : Transmit Data Register */
	uint8_t		RDR;					/* reg 0x13 : Receive Data Register */

	int		SCI_TX_State;
	uint8_t		TSR;					/* Transmit Shift Register */
	uint8_t		SCI_TX_Size;				/* How many data bits left to transmit in TSR (8 .. 0) */
	int		SCI_TX_Delay;				/* If >0, wait SCI_TX_Delay calls of IKBD_SCI_Set_Line_TX before */
								/* transferring a new byte in TDR (to simulate the time needed by */
								/* the IKBD to process a command and return the result) */

	int		SCI_RX_State;
	uint8_t		RSR;					/* Receive Shift Register */
	uint8_t		SCI_RX_Size;				/* How many bits left to receive in RSR (8 .. 0) */


	/* Date/Time is stored in the IKBD using 6 bytes in BCD format */
	/* Clock is cleared on cold reset, but keeps its values on warm reset */
	/* Original RAM location :  $82=year $83=month $84=day $85=hour $86=minute $87=second */
	uint8_t		Clock[ 6 ];
	int64_t		Clock_micro;				/* Incremented every VBL to update Clock[] every second */

} IKBD_STRUCT;


static IKBD_STRUCT	IKBD;
static IKBD_STRUCT	*pIKBD = &IKBD;




static void	IKBD_Init_Pointers ( ACIA_STRUCT *pACIA_IKBD );
static void	IKBD_Boot_ROM ( bool ClearAllRAM );

static void	IKBD_SCI_Get_Line_RX ( int rx_bit );
static uint8_t	IKBD_SCI_Set_Line_TX ( void );

static void	IKBD_Process_RDR ( uint8_t RDR );
static void	IKBD_Check_New_TDR ( void );

static bool	IKBD_OutputBuffer_CheckFreeCount ( int Nb );
static int	IKBD_Delay_Random ( int min , int max );
static void	IKBD_Cmd_Return_Byte ( uint8_t Data );
static void	IKBD_Cmd_Return_Byte_Delay ( uint8_t Data , int Delay_Cycles );
static void	IKBD_Send_Byte_Delay ( uint8_t Data , int Delay_Cycles );

static bool	IKBD_BCD_Check ( uint8_t val );
static uint8_t	IKBD_BCD_Adjust ( uint8_t val );


/*-----------------------------------------------------------------------*/
/* Belows part is used to emulate the behaviour of custom 6301 programs	*/
/* sent to the IKBD's RAM.						*/
/*-----------------------------------------------------------------------*/

static void IKBD_LoadMemoryByte ( uint8_t aciabyte );

static void IKBD_CustomCodeHandler_CommonBoot ( uint8_t aciabyte );

static void IKBD_CustomCodeHandler_FroggiesMenu_Read ( void );
static void IKBD_CustomCodeHandler_FroggiesMenu_Write ( uint8_t aciabyte );
static void IKBD_CustomCodeHandler_Transbeauce2Menu_Read ( void );
static void IKBD_CustomCodeHandler_Transbeauce2Menu_Write ( uint8_t aciabyte );
static void IKBD_CustomCodeHandler_DragonnelsMenu_Read ( void );
static void IKBD_CustomCodeHandler_DragonnelsMenu_Write ( uint8_t aciabyte );
static void IKBD_CustomCodeHandler_ChaosAD_Read ( void );
static void IKBD_CustomCodeHandler_ChaosAD_Write ( uint8_t aciabyte );
static void IKBD_CustomCodeHandler_AudioSculpture_Color_Read ( void );
static void IKBD_CustomCodeHandler_AudioSculpture_Mono_Read ( void );
static void IKBD_CustomCodeHandler_AudioSculpture_Read ( bool ColorMode );
static void IKBD_CustomCodeHandler_AudioSculpture_Write ( uint8_t aciabyte );


static int	MemoryLoadNbBytesTotal = 0;		/* total number of bytes to send with the command 0x20 */
static int	MemoryLoadNbBytesLeft = 0;		/* number of bytes that remain to be sent  */
static uint32_t	MemoryLoadCrc = 0xffffffff;		/* CRC of the bytes sent to the IKBD */
static int	MemoryExeNbBytes = 0;			/* current number of bytes sent to the IKBD when IKBD_ExeMode is true */

static void	(*pIKBD_CustomCodeHandler_Read) ( void );
static void	(*pIKBD_CustomCodeHandler_Write) ( uint8_t );
static bool	IKBD_ExeMode = false;

static uint8_t	ScanCodeState[ 128 ];			/* state of each key : 0=released 1=pressed */

/* This array contains all known custom 6301 programs, with their CRC */
static const struct
{
	uint32_t		LoadMemCrc;			/* CRC of the bytes sent using the command 0x20 */
	void		(*ExeBootHandler) ( uint8_t );	/* function handling write to $fffc02 during the 'boot' mode */
	int		MainProgNbBytes;		/* number of bytes of the main 6301 program */
	uint32_t		MainProgCrc;			/* CRC of the main 6301 program */
	void		(*ExeMainHandler_Read) ( void );/* function handling read to $fffc02 in the main 6301 program */
	void		(*ExeMainHandler_Write) ( uint8_t ); /* function handling write to $fffc02 in the main 6301 program */
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
	},
	{
		0x9ad7fcdf ,
		IKBD_CustomCodeHandler_CommonBoot ,
		109 ,
		0xa11d8be5 ,
		IKBD_CustomCodeHandler_ChaosAD_Read ,
		IKBD_CustomCodeHandler_ChaosAD_Write ,
		"Chaos A.D."
	},
	{
		0xbc0c206d,
		IKBD_CustomCodeHandler_CommonBoot ,
		91 ,
		0x119b26ed ,
		IKBD_CustomCodeHandler_AudioSculpture_Color_Read ,
		IKBD_CustomCodeHandler_AudioSculpture_Write ,
		"Audio Sculpture Color"
	},
	{
		0xbc0c206d ,
		IKBD_CustomCodeHandler_CommonBoot ,
		91 ,
		0x63b5f4df ,
		IKBD_CustomCodeHandler_AudioSculpture_Mono_Read ,
		IKBD_CustomCodeHandler_AudioSculpture_Write ,
		"Audio Sculpture Mono"
	}
};





/*-----------------------------------------------------------------------*/
/**
 * Init the IKBD processor.
 * Connect the IKBD RX/TX callback functions to the ACIA.
 * This is called only once, when the emulator starts.
 */
void	IKBD_Init ( void )
{
	LOG_TRACE ( TRACE_IKBD_ALL, "ikbd init\n" );

	/* Set the callback functions for RX/TX line */
	IKBD_Init_Pointers ( pACIA_IKBD );
}



/*-----------------------------------------------------------------------*/
/**
 * Init some functions/memory pointers for the IKBD.
 * This is called at Init and when restoring a memory snapshot.
 */
static void	IKBD_Init_Pointers ( ACIA_STRUCT *pACIA_IKBD )
{
	pACIA_IKBD->Get_Line_RX = IKBD_SCI_Set_Line_TX;			/* Connect ACIA's RX to IKBD SCI's TX */
	pACIA_IKBD->Set_Line_TX = IKBD_SCI_Get_Line_RX;			/* Connect ACIA's TX to IKBD SCI's RX */
}



/*-----------------------------------------------------------------------*/
/**
 * Reset the IKBD processor
 */

/* This function is called after a hardware reset of the IKBD.
 * Cold reset is when the computer is turned off/on.
 * Warm reset is when the reset button is pressed or the 68000
 * RESET instruction is used.
 * We clear the serial interface and we execute the function
 * that emulates booting the ROM at 0xF000.
 */
void	IKBD_Reset ( bool bCold )
{
	LOG_TRACE ( TRACE_IKBD_ALL , "ikbd reset mode=%s\n" , bCold?"cold":"warm" );

	/* Reset the SCI */
	pIKBD->TRCSR = IKBD_TRCSR_BIT_TDRE;

	pIKBD->SCI_TX_State = IKBD_SCI_STATE_IDLE;
	pIKBD->TSR = 0;
	pIKBD->SCI_TX_Size = 0;
	pIKBD->SCI_TX_Delay = 0;

	pIKBD->SCI_RX_State = IKBD_SCI_STATE_IDLE;
	pIKBD->RSR = 0;
	pIKBD->SCI_RX_Size = 0;


	/* On cold reset, clear the whole RAM (including clock data) */
	/* On warm reset, the clock data should be kept */
	if ( bCold )
		IKBD_Boot_ROM ( true );
	else
		IKBD_Boot_ROM ( false );
}



/* This function emulates the boot code stored in the ROM at address $F000.
 * This boot code is called either after a hardware reset, or when the
 * reset command ($80 $01) is received.
 * Depending on the conditions, we should clear the clock data or not (the
 * real IKBD will test+clear RAM either in range $80-$FF or in range $89-$FF)
 */
static void	IKBD_Boot_ROM ( bool ClearAllRAM )
{
	int	i;


	LOG_TRACE ( TRACE_IKBD_ALL , "ikbd boot rom clear_all=%s\n" , ClearAllRAM?"yes":"no" );

	/* Clear clock data when the 128 bytes of RAM are cleared */
	if ( ClearAllRAM )
	{
		/* Clear clock data on cold reset */
		for ( i=0 ; i<6 ; i++ )
			pIKBD->Clock[ i ] = 0;
		pIKBD->Clock_micro = 0;
	}

// pIKBD->Clock[ 0 ] = 0x99;
// pIKBD->Clock[ 1 ] = 0x12;
// pIKBD->Clock[ 2 ] = 0x31;
// pIKBD->Clock[ 3 ] = 0x23;
// pIKBD->Clock[ 4 ] = 0x59;
// pIKBD->Clock[ 5 ] = 0x57;

	/* Set default reporting mode for mouse/joysticks */
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
	KeyboardProcessor.Mouse.KeyCodeDeltaX = KeyboardProcessor.Mouse.KeyCodeDeltaY = 1;
	KeyboardProcessor.Mouse.YAxis = 1;          /* Y origin at top */
	KeyboardProcessor.Mouse.Action = 0;

	KeyboardProcessor.Joy.PrevJoyData[0] = KeyboardProcessor.Joy.PrevJoyData[1] = 0;

	for ( i=0 ; i<128 ; i++ )
		ScanCodeState[ i ] = 0;				/* key is released */


	/* Reset our keyboard states and clear key state table */
	Keyboard.BufferHead = Keyboard.BufferTail = 0;
	Keyboard.NbBytesInOutputBuffer = 0;
	Keyboard.nBytesInInputBuffer = 0;
	Keyboard.PauseOutput = false;

	memset(Keyboard.KeyStates, 0, sizeof(Keyboard.KeyStates));
	Keyboard.bLButtonDown = BUTTON_NULL;
	Keyboard.bRButtonDown = BUTTON_NULL;
	Keyboard.bOldLButtonDown = Keyboard.bOldRButtonDown = BUTTON_NULL;
	Keyboard.LButtonDblClk = Keyboard.RButtonDblClk = 0;
	Keyboard.LButtonHistory = Keyboard.RButtonHistory = 0;

	/* Store bool for when disable mouse or joystick */
	bMouseDisabled = bJoystickDisabled = false;
	/* do emulate hardware 'quirk' where if disable both with 'x' time
	 * of a RESET command they are ignored! */
	bDuringResetCriticalTime = true;
	bBothMouseAndJoy = false;
	bMouseEnabledDuringReset = false;


	/* Remove any custom handlers used to emulate code loaded to the 6301's RAM */
	if ( ( MemoryLoadNbBytesLeft != 0 ) || ( IKBD_ExeMode == true ) )
	{
		LOG_TRACE ( TRACE_IKBD_ALL , "ikbd stop memory load and turn off custom exe\n" );

		MemoryLoadNbBytesLeft = 0;
		pIKBD_CustomCodeHandler_Read = NULL;
		pIKBD_CustomCodeHandler_Write = NULL;
		IKBD_ExeMode = false;
	}
	

	/* During the boot, the IKBD will test all the keys to ensure no key */
	/* is stuck. We use a timer to emulate the time needed for this part */
	/* (eg Lotus Turbo Esprit 2 requires at least a delay of 50000 cycles */
	/* or it will crash during start up) */
	CycInt_AddRelativeInterrupt( IKBD_RESET_CYCLES , INT_CPU8_CYCLE , INTERRUPT_IKBD_RESETTIMER );


	/* Add auto-update function to the queue */
	/* We add it only if it was not active, else this can lead to unresponsive keyboard/input */
	/* when RESET instruction is called in a loop in less than 150000 cycles */
	Keyboard.AutoSendCycles = 150000;				/* approx every VBL */
	if ( CycInt_InterruptActive ( INTERRUPT_IKBD_AUTOSEND ) == false )
		CycInt_AddRelativeInterrupt ( Keyboard.AutoSendCycles, INT_CPU8_CYCLE, INTERRUPT_IKBD_AUTOSEND );

	LOG_TRACE ( TRACE_IKBD_ALL , "ikbd reset done, starting reset timer\n" );
}


/*-----------------------------------------------------------------------*/
/**
 * This timer is started by IKBD_Boot_ROM to emulate the time needed
 * to setup the IKBD in its default state after a reset.
 * If some IKBD commands are received during the boot phase they may be ignored.
 */
void IKBD_InterruptHandler_ResetTimer(void)
{
	LOG_TRACE(TRACE_IKBD_ALL, "ikbd reset timer completed, resuming ikbd processing VBLs=%i framecyc=%i\n",
	          nVBLs, Cycles_GetCounter(CYCLES_COUNTER_VIDEO));

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* Reset timer is over */
	bDuringResetCriticalTime = false;
	bMouseEnabledDuringReset = false;

	/* Return $F1 when IKBD's boot is complete */
	IKBD_Cmd_Return_Byte_Delay ( IKBD_ROM_VERSION , IKBD_Delay_Random ( 0 , 3000 ) );
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
	MemorySnapShot_Store(&bMouseDisabled, sizeof(bMouseDisabled));
	MemorySnapShot_Store(&bJoystickDisabled, sizeof(bJoystickDisabled));
	MemorySnapShot_Store(&bDuringResetCriticalTime, sizeof(bDuringResetCriticalTime));
	MemorySnapShot_Store(&bBothMouseAndJoy, sizeof(bBothMouseAndJoy));
	MemorySnapShot_Store(&bMouseEnabledDuringReset, sizeof(bMouseEnabledDuringReset));

	/* restore custom 6301 program if needed */
	MemorySnapShot_Store(&IKBD_ExeMode, sizeof(IKBD_ExeMode));
	MemorySnapShot_Store(&MemoryLoadCrc, sizeof(MemoryLoadCrc));
	if ((bSave == false) && (IKBD_ExeMode == true)) 	/* restoring a snapshot with active 6301 emulation */
	{
		for ( i = 0 ; i < sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ); i++ )
			if ( CustomCodeDefinitions[ i ].MainProgCrc == MemoryLoadCrc )
			{
				pIKBD_CustomCodeHandler_Read = CustomCodeDefinitions[ i ].ExeMainHandler_Read;
				pIKBD_CustomCodeHandler_Write = CustomCodeDefinitions[ i ].ExeMainHandler_Write;
				Keyboard.BufferHead = Keyboard.BufferTail = 0;	/* flush all queued bytes that would be read in $fffc02 */
				Keyboard.NbBytesInOutputBuffer = 0;
				break;
			}

		if ( i >= sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ) )	/* not found (should not happen) */
			IKBD_ExeMode = false;			/* turn off exe mode */
	}


	/* Save the IKBD's SCI part and restore the callback functions for RX/TX lines with the ACIA */
	MemorySnapShot_Store(&IKBD, sizeof(IKBD));
	if (!bSave)						/* Restoring a snapshot */
	{
		IKBD_Init_Pointers ( pACIA_IKBD );
	}
}




/************************************************************************/
/* This part emulates the IKBD's Serial Communication Interface.	*/
/* This is a simplified implementation that ignores the RMCR content,	*/
/* as we assume the IKBD and the ACIA will be using the same baud rate.	*/
/* The TX/RX baud rate is chosen at the ACIA level, and the IKBD will	*/
/* use the same rate.							*/
/* The SCI only supports 8 bits of data, with 1 start bit, 1 stop bit	*/
/* and no parity bit.							*/
/************************************************************************/


/*-----------------------------------------------------------------------*/
/**
 * Prepare a new transfer. Copy TDR to TSR and initialize data size.
 * Transfer will then start at the next call of IKBD_SCI_Set_Line_TX.
 */
static void	IKBD_SCI_Prepare_TX ( IKBD_STRUCT *pIKBD )
{
	pIKBD->TSR = pIKBD->TDR;
	pIKBD->SCI_TX_Size = 8;

	pIKBD->TRCSR |= IKBD_TRCSR_BIT_TDRE;				/* TDR was copied to TSR. TDR is now empty */

	LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd acia prepare tx tsr=0x%02x size=%d VBL=%d HBL=%d\n" , pIKBD->TSR , pIKBD->SCI_TX_Size , nVBLs , nHBL );
}




/*-----------------------------------------------------------------------*/
/**
 * Prepare a new reception. Initialize RSR and data size.
 */
static void	IKBD_SCI_Prepare_RX ( IKBD_STRUCT *pIKBD )
{
	pIKBD->RSR = 0;
	pIKBD->SCI_RX_Size = 8;

	LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd acia prepare rx size=%d VBL=%d HBL=%d\n" , pIKBD->SCI_RX_Size , nVBLs , nHBL );
}




/*-----------------------------------------------------------------------*/
/**
 * Receive a bit on the IKBD SCI's RX line (this is connected to the ACIA's TX)
 * This will fill RDR with bits received from the serial line, using RSR.
 * Incoming bits are stored in bit 7 of RSR, then RSR is shifted to the right.
 * This is similar to the ACIA's RX, but with fixed parameters : 8 data bits,
 * no parity bit and 1 stop bit.
 */
static void	IKBD_SCI_Get_Line_RX ( int rx_bit )
{
	int	StateNext;


	LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd acia rx_state=%d bit=%d VBL=%d HBL=%d\n" , pIKBD->SCI_RX_State , rx_bit , nVBLs , nHBL );

	StateNext = -1;
	switch ( pIKBD->SCI_RX_State )
	{
	  case IKBD_SCI_STATE_IDLE :
		if ( rx_bit == 0 )					/* Receive one "0" start bit */
		{
			IKBD_SCI_Prepare_RX ( pIKBD );
			StateNext = IKBD_SCI_STATE_DATA_BIT;
		}
		break;							/* If no start bit, we stay in idle state */

	  case IKBD_SCI_STATE_DATA_BIT :
		if ( rx_bit )
			pIKBD->RSR |= 0x80;
		pIKBD->SCI_RX_Size--;

		if ( pIKBD->SCI_RX_Size > 0 )				/* All bits were not received yet */
			pIKBD->RSR >>= 1;
		else
			StateNext = IKBD_SCI_STATE_STOP_BIT;
		break;

	  case IKBD_SCI_STATE_STOP_BIT :
		if ( rx_bit == 1 )					/* Wait for one "1" stop bit */
		{
			pIKBD->TRCSR &= ~IKBD_TRCSR_BIT_ORFE;
			
			if ( ( pIKBD->TRCSR & IKBD_TRCSR_BIT_RDRF ) == 0 )
			{
				pIKBD->RDR = pIKBD->RSR;
				pIKBD->TRCSR |= IKBD_TRCSR_BIT_RDRF;
				LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd acia get_rx received rsr=0x%02x VBL=%d HBL=%d\n" ,
					pIKBD->RDR , nVBLs , nHBL );

				IKBD_Process_RDR ( pIKBD->RDR );	/* Process this new byte */
			}
			else
			{
				pIKBD->TRCSR |= IKBD_TRCSR_BIT_ORFE;	/* Overrun Error */
				LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd acia get_rx received rsr=0x%02x : ignored, rdr=0x%02x and rdrf already set VBL=%d HBL=%d\n" ,
					pIKBD->RSR , pIKBD->RDR , nVBLs , nHBL );

				IKBD_Process_RDR ( pIKBD->RDR );	/* RSR is lost, try to process the current RDR which was not read yet */
			}
			StateNext = IKBD_SCI_STATE_IDLE;		/* Go to idle state and wait for start bit */
		}
		else							/* Not a valid stop bit */
		{
			LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd acia get_rx framing error VBL=%d HBL=%d\n" , nVBLs , nHBL );
			pIKBD->TRCSR |= IKBD_TRCSR_BIT_ORFE;		/* Framing Error */
			StateNext = IKBD_SCI_STATE_IDLE;		/* Go to idle state and wait for start bit */
		}
		break;
	}

	if ( StateNext >= 0 )
		pIKBD->SCI_RX_State = StateNext;			/* Go to a new state */
}




/*-----------------------------------------------------------------------*/
/**
 * Send a bit on the IKBD SCI's TX line (this is connected to the ACIA's RX)
 * When the SCI is idle, we send '1' stop bits.
 */
static uint8_t	IKBD_SCI_Set_Line_TX ( void )
{
	int	StateNext;
	uint8_t	tx_bit = 1;


	LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd acia tx_state=%d tx_delay=%d VBL=%d HBL=%d\n" , pIKBD->SCI_TX_State , pIKBD->SCI_TX_Delay ,
		nVBLs , nHBL );

	StateNext = -1;
	switch ( pIKBD->SCI_TX_State )
	{
	  case IKBD_SCI_STATE_IDLE :
		tx_bit = 1;						/* In idle state, default is to send '1' stop bits */

		if ( pIKBD->SCI_TX_Delay > 0 )				/* Should we delay the next TDR ? */
		{
			pIKBD->SCI_TX_Delay--;				/* Don't do anything for now, send a stop bit */
			break;
		}

		IKBD_Check_New_TDR ();					/* Do we have a byte to load in TDR ? */

		if ( ( pIKBD->TRCSR & IKBD_TRCSR_BIT_TDRE ) == 0 )	/* We have a new byte in TDR */
		{
			IKBD_SCI_Prepare_TX ( pIKBD );
			tx_bit = 0;					/* Send one '0' start bit */
			StateNext = IKBD_SCI_STATE_DATA_BIT;
		}
		break;

	  case IKBD_SCI_STATE_DATA_BIT :
		tx_bit = pIKBD->TSR & 1;				/* New bit to send */
		pIKBD->TSR >>= 1;
		pIKBD->SCI_TX_Size--;

		if ( pIKBD->SCI_TX_Size == 0 )
			StateNext = IKBD_SCI_STATE_STOP_BIT;
		break;


	  case IKBD_SCI_STATE_STOP_BIT :
		tx_bit = 1;						/* Send 1 stop bit */
		StateNext = IKBD_SCI_STATE_IDLE;			/* Go to idle state to see if a new TDR need to be sent */
		break;
	}

	if ( StateNext >= 0 )
		pIKBD->SCI_TX_State = StateNext;			/* Go to a new state */

	return tx_bit;
}




/*-----------------------------------------------------------------------*/
/**
 * Handle the byte that was received in the RDR from the ACIA.
 * Depending on the IKBD's emulation mode, we either pass it to the standard
 * ROM's emulation layer, or we pass it to the custom handlers.
 */
static void	IKBD_Process_RDR ( uint8_t RDR )
{
	pIKBD->TRCSR &= ~IKBD_TRCSR_BIT_RDRF;				/* RDR was read */


	/* If IKBD is executing custom code, send the byte to the function handling this code */
	if ( IKBD_ExeMode && pIKBD_CustomCodeHandler_Write )
	{
		(*pIKBD_CustomCodeHandler_Write) ( RDR );
		return;
	}

	if ( MemoryLoadNbBytesLeft == 0 )				/* No pending MemoryLoad command */
		IKBD_RunKeyboardCommand ( RDR );			/* Check for known commands */

	else								/* MemoryLoad command is not finished yet */
		IKBD_LoadMemoryByte ( RDR );				/* Process bytes sent to the IKBD's RAM */
}




/*-----------------------------------------------------------------------*/
/**
 * Check if we have a byte to copy to the IKBD's TDR, to send it to the ACIA.
 * We get new bytes from the buffer filled by IKBD_Send_Byte_Delay
 */
static void	IKBD_Check_New_TDR ( void )
{
//  fprintf(stderr , "check new tdr %d %d\n", Keyboard.BufferHead , Keyboard.BufferTail );

	if ( ( Keyboard.NbBytesInOutputBuffer > 0 )
	  && ( Keyboard.PauseOutput == false ) )
	{
		pIKBD->TDR = Keyboard.Buffer[ Keyboard.BufferHead++ ];
		Keyboard.BufferHead &= KEYBOARD_BUFFER_MASK;
		Keyboard.NbBytesInOutputBuffer--;
		pIKBD->TRCSR &= ~IKBD_TRCSR_BIT_TDRE;
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Return true if the output buffer can store 'Nb' new bytes,
 * else return false.
 * Some games like 'Downfall' or 'Fokker' are continually issuing the same
 * IKBD_Cmd_ReturnJoystick command without waiting for the returned bytes,
 * which will fill the output buffer faster than the CPU can empty it.
 * In that case, new messages must be discarded until the buffer has some room
 * again for a whole packet.
 */
static bool	IKBD_OutputBuffer_CheckFreeCount ( int Nb )
{
// fprintf ( stderr , "check %d %d head %d tail %d\n" , Nb , SIZE_KEYBOARD_BUFFER - Keyboard.NbBytesInOutputBuffer ,
//       Keyboard.BufferHead , Keyboard.BufferTail );

	if ( SIZE_KEYBOARD_BUFFER - Keyboard.NbBytesInOutputBuffer >= Nb )
		return true;

	else
	{
		LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd acia output buffer is full, can't send %d bytes VBL=%d HBL=%d\n" ,
			Nb, nVBLs , nHBL );
		return false;
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Return a random number between 'min' and 'max'.
 * This is used when the IKBD send bytes to the ACIA, to add some
 * randomness to the delay (on real hardware, the delay is not constant
 * when a command return some bytes).
 */
static int	IKBD_Delay_Random ( int min , int max )
{
	return min + Hatari_rand() % ( max - min );
}


/*-----------------------------------------------------------------------*/
/**
 * This function will buffer all the bytes returned by a specific
 * IKBD_Cmd_xxx command. If we're using a custom handler, we should filter
 * these bytes (keyboard, mouse, joystick) as they don't come from the custom handler.
 */
static void	IKBD_Cmd_Return_Byte ( uint8_t Data )
{
	if ( IKBD_ExeMode )					/* If IKBD is executing custom code, don't add */
		return;						/* anything to the buffer that comes from an IKBD's command */

	IKBD_Send_Byte_Delay ( Data , 0 );
}


/**
 * Same as IKBD_Cmd_Return_Byte, but with a delay before transmitting
 * the byte.
 */
static void	IKBD_Cmd_Return_Byte_Delay ( uint8_t Data , int Delay_Cycles )
{
	if ( IKBD_ExeMode )					/* If IKBD is executing custom code, don't add */
		return;						/* anything to the buffer that comes from an IKBD's command */

	IKBD_Send_Byte_Delay ( Data , Delay_Cycles );
}




/*-----------------------------------------------------------------------*/
/**
 * Send bytes from the IKBD to the ACIA. We store the bytes in a buffer
 * and we pull a new byte each time TDR needs to be re-filled.
 *
 * A possible delay can be specified to simulate the fact that some IKBD's
 * commands don't return immediately the first byte. This delay is given
 * in 68000 cycles at 8 MHz and should be converted to a number of bits
 * at the chosen baud rate.
 */
static void	IKBD_Send_Byte_Delay ( uint8_t Data , int Delay_Cycles )
{
//fprintf ( stderr , "send byte=0x%02x delay=%d\n" , Data , Delay_Cycles );
	/* Is keyboard initialised yet ? Ignore any bytes until it is */
	if ( bDuringResetCriticalTime )
	{
		LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd is resetting, can't send byte=0x%02x VBL=%d HBL=%d\n" , Data, nVBLs , nHBL );
		return;
	}

	/* Is ACIA's serial line initialised yet ? Ignore any bytes until it is */
	if ( pACIA_IKBD->Clock_Divider == 0 )
	{
		LOG_TRACE ( TRACE_IKBD_ACIA, "ikbd acia not initialized, can't send byte=0x%02x VBL=%d HBL=%d\n" , Data, nVBLs , nHBL );
		return;
	}

	if ( Delay_Cycles > 0 )
		pIKBD->SCI_TX_Delay = Delay_Cycles / 1024;	/* 1 bit at 7812.5 baud = 1024 cpu cycles at 8 MHz */


	/* Check we have space to add one byte */
	if ( IKBD_OutputBuffer_CheckFreeCount ( 1 ) )
	{
		/* Add byte to our buffer */
		Keyboard.Buffer[Keyboard.BufferTail++] = Data;
		Keyboard.BufferTail &= KEYBOARD_BUFFER_MASK;
		Keyboard.NbBytesInOutputBuffer++;
	}
	else
	{
		Log_Printf(LOG_ERROR, "IKBD buffer is full, can't send 0x%02x!\n" , Data );
	}
}






/************************************************************************/
/* End of the Serial Communication Interface				*/
/************************************************************************/


/**
 * Check that the value is a correctly encoded BCD number
 */
static bool	IKBD_BCD_Check ( uint8_t val )
{
	if ( ( ( val & 0x0f ) > 0x09 )
	  || ( ( val & 0xf0 ) > 0x90 ) )
		return false;

	return true;
}


/**
 * After adding an integer number to a BCD number, the result is no more
 * in BCD format. This function adjusts the value to be a valid BCD number again.
 * In the HD6301, this is done using the 'DAA' instruction (Decimal Adjust)
 * to "propagate" values 10-15 to the next 4 bits and keep each nibble
 * in the 0-9 range.
 */

static uint8_t	IKBD_BCD_Adjust ( uint8_t val )
{
	if ( ( val & 0x0f ) > 0x09 )	/* low nibble no more in BCD */
		val += 0x06;		/* clear bit 4 and add 1 to high nibble */
	if ( ( val & 0xf0 ) > 0x90 )	/* high nibble no more in BCD */
		val += 0x60;		/* propagate carry (but bits>7 will be lost) */

	return val;
}



/**
 * Update the IKBD's internal clock.
 *
 * This function is called on every VBL and we add the number of microseconds
 * per VBL. When we reach 1000000 microseconds (1 sec), we update the Clock[]
 * array by incrementing the 'second' byte.
 *
 * This code uses the same logic as the ROM version in the IKBD,
 * don't try to optimise/rewrite it in a different way, as the TOS
 * expects data to be handled this way.
 * This works directly with BCD numbers and propagates the increment
 * to the next byte each time the current byte reaches its maximum
 * value.
 *  - when SetClock is used, the IKBD doesn't check the range of each byte,
 *    just that it's BCD encoded. So it's possible to set month/day/... to
 *    invalid values beyond the maximum allowed. These values will not correctly
 *    propagate to the next byte until they reach 0x99 and start again at 0x00.
 *  - check leap year for the number of days in february if ( year & 3 == 0 )
 *  - there's no explicit max for year : if year is 99 and increments,
 *    next year will be 00 (due to the BCD overflow)
 *    (used in the game 'Captain Blood' which sets clock to "99 12 31 00 00 00"
 *    and ends the game when clock reaches "00 01 01 00 00 00")
 */
void	IKBD_UpdateClockOnVBL ( void )
{
	int64_t	FrameDuration_micro;
	int	i;
	uint8_t	val;
	uint8_t	max;
	uint8_t	year;
	uint8_t	month;

	/* Max value for year/month/day/hour/minute/second */
	uint8_t	val_max[ 6 ] = { 0xFF , 0x13 , 0x00 , 0x24 , 0x60 , 0x60 };
	/* Max number of days per month ; 18 entries, because the index for this array is a BCD coded month */
	uint8_t	day_max[ 18 ] = { 0x32, 0x29, 0x32, 0x31, 0x32, 0x31, 0x32, 0x32, 0x31, 0,0,0,0,0,0, 0x32, 0x31, 0x32 };


	/* Check if more than 1 second passed since last increment of date/time */
        FrameDuration_micro = ClocksTimings_GetVBLDuration_micro ( ConfigureParams.System.nMachineType , nScreenRefreshRate );
	pIKBD->Clock_micro += FrameDuration_micro;
	if ( pIKBD->Clock_micro < 1000000 )
		return;						/* Less than 1 second, don't increment date/time yet */
	pIKBD->Clock_micro -= 1000000;


	/* 1 second passed, we can increment the clock data */
// 	LOG_TRACE(TRACE_IKBD_CMDS,
// 		  "IKBD_UpdateClock: %02x %02x %02x %02x %02x %02x -> ", pIKBD->Clock[ 0 ] ,pIKBD->Clock[ 1 ] , pIKBD->Clock[ 2 ] ,
// 		  pIKBD->Clock[ 3 ] , pIKBD->Clock[ 4 ] , pIKBD->Clock[ 5 ] );

	for ( i=5 ; i>=0 ; i-- )
	{
		val = pIKBD->Clock[ i ] + 1;			/* Increment current value */
		val = IKBD_BCD_Adjust ( val );			/* Convert to BCD */

		if ( i != 2 )
			max = val_max[ i ];

		else						/* Special case for days per month */
		{
			/* WARNING : it's possible to set the IKBD with month > 0x12, but in that case */
			/* we would access day_max[] out of range. So, if month > 0x12, we limit to 31 days */
			/* (this test is not done in the IKBD, but results would not be correct anyway) */
			month = pIKBD->Clock[ 1 ];
			if ( month > 0x12 )			/* Hatari specific, check range */
				month = 0x12;
			max = day_max[ month - 1 ];		/* Number of days for current month */
			if ( pIKBD->Clock[ 1 ] == 2 )		/* For february, check leap year */
			{
				year = pIKBD->Clock[ 0 ];
				/* Leap year test comes from the IKBD's ROM */
				if ( year & 0x10 )
					year += 0x0a;
				if ( ( year & 0x03 ) == 0 )
					max = 0x30;		/* This is a leap year, 29 days */
			}
		}

		if ( val != max )
		{
			pIKBD->Clock[ i ] = val;		/* Max not reached, stop here */
			break;
		}
		else if ( ( i == 1 ) || ( i == 2 ) )
			pIKBD->Clock[ i ] = 1;			/* day/month start at 1 */
		else
			pIKBD->Clock[ i ] = 0;			/* hour/minute/second start at 0 */
	}

// 	LOG_TRACE(TRACE_IKBD_CMDS,
// 		  "%02x %02x %02x %02x %02x %02x\n", pIKBD->Clock[ 0 ] ,pIKBD->Clock[ 1 ] , pIKBD->Clock[ 2 ] ,
// 		  pIKBD->Clock[ 3 ] , pIKBD->Clock[ 4 ] , pIKBD->Clock[ 5 ] );
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
	if ( KeyboardProcessor.Mouse.XScale > 1 )
		KeyboardProcessor.Abs.X += KeyboardProcessor.Mouse.DeltaX * KeyboardProcessor.Mouse.XScale;
	else
		KeyboardProcessor.Abs.X += KeyboardProcessor.Mouse.DeltaX;
	if (KeyboardProcessor.Abs.X < 0)
		KeyboardProcessor.Abs.X = 0;
	if (KeyboardProcessor.Abs.X > KeyboardProcessor.Abs.MaxX)
		KeyboardProcessor.Abs.X = KeyboardProcessor.Abs.MaxX;

	if ( KeyboardProcessor.Mouse.YScale > 1 )
		KeyboardProcessor.Abs.Y += KeyboardProcessor.Mouse.DeltaY*KeyboardProcessor.Mouse.YAxis * KeyboardProcessor.Mouse.YScale;
	else
		KeyboardProcessor.Abs.Y += KeyboardProcessor.Mouse.DeltaY*KeyboardProcessor.Mouse.YAxis;
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
		if (Keyboard.LButtonDblClk >= ARRAY_SIZE(DoubleClickPattern))
		{
			Keyboard.LButtonDblClk = 0;
			Keyboard.bLButtonDown = false;
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
		if (Keyboard.RButtonDblClk >= ARRAY_SIZE(DoubleClickPattern))
		{
			Keyboard.RButtonDblClk = 0;
			Keyboard.bRButtonDown = false;
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
		return true;
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Return true if buttons match, use this as buttons are a mask and not boolean
 */
static bool IKBD_ButtonsEqual(int Button1,int Button2)
{
	/* Return bool compare */
	return (IKBD_ButtonBool(Button1) == IKBD_ButtonBool(Button2));
}


/*-----------------------------------------------------------------------*/
/**
 * According to if the mouse is enabled or not the joystick 1 fire
 * button/right mouse button will become the same button. That means
 * pressing one will also press the other and vice-versa.
 * If both mouse and joystick are enabled, report it as a mouse button
 * (needed by the game Big Run for example).
 */
static void IKBD_DuplicateMouseFireButtons(void)
{
	/* If mouse is off then joystick fire button goes to joystick */
	if (KeyboardProcessor.MouseMode == AUTOMODE_OFF)
	{
		/* If pressed right mouse button, should go to joystick 1 */
		if (Keyboard.bRButtonDown&BUTTON_MOUSE)
			KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK1] |= ATARIJOY_BITMASK_FIRE;
		/* And left mouse button, should go to joystick 0 */
		if (Keyboard.bLButtonDown&BUTTON_MOUSE)
			KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK0] |= ATARIJOY_BITMASK_FIRE;
	}
	/* If mouse is on, joystick 1 fire button goes to the mouse instead */
	else
	{
		/* Is fire button pressed? */
		if (KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK1]&ATARIJOY_BITMASK_FIRE)
		{
			KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK1] &= ~ATARIJOY_BITMASK_FIRE;  /* Clear fire button bit */
			Keyboard.bRButtonDown |= BUTTON_JOYSTICK;  /* Mimic right mouse button */
		}
		else
			Keyboard.bRButtonDown &= ~BUTTON_JOYSTICK;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Send 'relative' mouse position
 * In case DeltaX or DeltaY are more than 127 units, we send the position
 * using several packets (with a while loop)
 */
static void IKBD_SendRelMousePacket(void)
{
	int ByteRelX,ByteRelY;
	uint8_t Header;

	while ( true )
	{
		ByteRelX = KeyboardProcessor.Mouse.DeltaX;
		if ( ByteRelX > 127 )		ByteRelX = 127;
		if ( ByteRelX < -128 )		ByteRelX = -128;

		ByteRelY = KeyboardProcessor.Mouse.DeltaY;
		if ( ByteRelY > 127 )		ByteRelY = 127;
		if ( ByteRelY < -128 )		ByteRelY = -128;

		if ( ( ( ByteRelX < 0 ) && ( ByteRelX <= -KeyboardProcessor.Mouse.XThreshold ) )
		  || ( ( ByteRelX > 0 ) && ( ByteRelX >= KeyboardProcessor.Mouse.XThreshold ) )
		  || ( ( ByteRelY < 0 ) && ( ByteRelY <= -KeyboardProcessor.Mouse.YThreshold ) )
		  || ( ( ByteRelY > 0 ) && ( ByteRelY >= KeyboardProcessor.Mouse.YThreshold ) )
		  || ( !IKBD_ButtonsEqual(Keyboard.bOldLButtonDown,Keyboard.bLButtonDown ) )
		  || ( !IKBD_ButtonsEqual(Keyboard.bOldRButtonDown,Keyboard.bRButtonDown ) ) )
		{
			Header = 0xf8;
			if (Keyboard.bLButtonDown)
				Header |= 0x02;
			if (Keyboard.bRButtonDown)
				Header |= 0x01;

			if ( IKBD_OutputBuffer_CheckFreeCount ( 3 ) )
			{
				IKBD_Cmd_Return_Byte (Header);
				IKBD_Cmd_Return_Byte (ByteRelX);
				IKBD_Cmd_Return_Byte (ByteRelY*KeyboardProcessor.Mouse.YAxis);
			}

			KeyboardProcessor.Mouse.DeltaX -= ByteRelX;
			KeyboardProcessor.Mouse.DeltaY -= ByteRelY;

			/* Store buttons for next time around */
			Keyboard.bOldLButtonDown = Keyboard.bLButtonDown;
			Keyboard.bOldRButtonDown = Keyboard.bRButtonDown;
		}

		else
			break;					/* exit the while loop */
	}
}


/**
 * Get joystick data
 */
static void IKBD_GetJoystickData(void)
{
	/* Joystick 1 */
	KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK1] = Joy_GetStickData(JOYID_JOYSTICK1);

	/* If mouse is on, joystick 0 is not connected */
	if (KeyboardProcessor.MouseMode==AUTOMODE_OFF
	        || (bBothMouseAndJoy && KeyboardProcessor.MouseMode==AUTOMODE_MOUSEREL))
		KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK0] = Joy_GetStickData(JOYID_JOYSTICK0);
	else
		KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK0] = 0x00;
}


/*-----------------------------------------------------------------------*/
/**
 * Send 'joysticks' bit masks
 */
static void IKBD_SendAutoJoysticks(void)
{
	uint8_t JoyData;

	/* Did joystick 0/mouse change? */
	JoyData = KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK0];
	if (JoyData!=KeyboardProcessor.Joy.PrevJoyData[JOYID_JOYSTICK0])
	{
		if ( IKBD_OutputBuffer_CheckFreeCount ( 2 ) )
		{
			IKBD_Cmd_Return_Byte (0xFE);			/* Joystick 0 / Mouse */
			IKBD_Cmd_Return_Byte (JoyData);
		}
		KeyboardProcessor.Joy.PrevJoyData[JOYID_JOYSTICK0] = JoyData;
	}

	/* Did joystick 1(default) change? */
	JoyData = KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK1];
	if (JoyData!=KeyboardProcessor.Joy.PrevJoyData[JOYID_JOYSTICK1])
	{
		if ( IKBD_OutputBuffer_CheckFreeCount ( 2 ) )
		{
			IKBD_Cmd_Return_Byte (0xFF);			/* Joystick 1 */
			IKBD_Cmd_Return_Byte (JoyData);
		}
		KeyboardProcessor.Joy.PrevJoyData[JOYID_JOYSTICK1] = JoyData;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Send 'joysticks' bit masks when in monitoring mode
 *	%000000xy	; where y is JOYSTICK1 Fire button
 *			; and x is JOYSTICK0 Fire button
 *	%nnnnmmmm	; where m is JOYSTICK1 state
 *			; and n is JOYSTICK0 state
 */
static void IKBD_SendAutoJoysticksMonitoring(void)
{
	uint8_t Byte1;
	uint8_t Byte2;

	Byte1 = ( ( KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK0] & ATARIJOY_BITMASK_FIRE ) >> 6 )
		| ( ( KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK1] & ATARIJOY_BITMASK_FIRE ) >> 7 );

	Byte2 = ( ( KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK0] & 0x0f ) << 4 )
		| ( KeyboardProcessor.Joy.JoyData[JOYID_JOYSTICK1] & 0x0f );

	IKBD_Cmd_Return_Byte (Byte1);
	IKBD_Cmd_Return_Byte (Byte2);
//fprintf ( stderr , "joystick monitoring %x %x VBL=%d HBL=%d\n" , Byte1 , Byte2 , nVBLs , nHBL );
}

/*-----------------------------------------------------------------------*/
/**
 * Send packets which are generated from the mouse action settings
 * If relative mode is on, still generate these packets
 */
static void IKBD_SendOnMouseAction(void)
{
	bool bReportPosition = false;

	/* Report buttons as keys? Do in relative/absolute mode */
	if (KeyboardProcessor.Mouse.Action&0x4)
	{
		if ( IKBD_OutputBuffer_CheckFreeCount ( 2 ) )
		{
			/* Left button? */
			if ( (IKBD_ButtonBool(Keyboard.bLButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldLButtonDown))) )
				IKBD_Cmd_Return_Byte (0x74);		/* Left */
			else if ( (IKBD_ButtonBool(Keyboard.bOldLButtonDown) && (!IKBD_ButtonBool(Keyboard.bLButtonDown))) )
				IKBD_Cmd_Return_Byte (0x74|0x80);
			/* Right button? */
			if ( (IKBD_ButtonBool(Keyboard.bRButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldRButtonDown))) )
				IKBD_Cmd_Return_Byte (0x75);		/* Right */
			else if ( (IKBD_ButtonBool(Keyboard.bOldRButtonDown) && (!IKBD_ButtonBool(Keyboard.bRButtonDown))) )
				IKBD_Cmd_Return_Byte (0x75|0x80);
		}
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
				bReportPosition = true;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x04;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x02;
			}
			if ( (IKBD_ButtonBool(Keyboard.bRButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldRButtonDown))) )
			{
				bReportPosition = true;
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
				bReportPosition = true;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons &= ~0x08;
				KeyboardProcessor.Abs.PrevReadAbsMouseButtons |= 0x01;
			}
			if ( (IKBD_ButtonBool(Keyboard.bOldRButtonDown) && (!IKBD_ButtonBool(Keyboard.bRButtonDown))) )
			{
				bReportPosition = true;
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
				LOG_TRACE(TRACE_IKBD_CMDS, "Report ABS on MouseAction\n");
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
//fprintf ( stderr , "key %d %d %d %d %d %d\n" , KeyboardProcessor.Mouse.DeltaX , KeyboardProcessor.Mouse.DeltaY, Keyboard.bOldLButtonDown,Keyboard.bLButtonDown, Keyboard.bOldRButtonDown,Keyboard.bRButtonDown );
	while ( (i<10) && ((KeyboardProcessor.Mouse.DeltaX!=0) || (KeyboardProcessor.Mouse.DeltaY!=0)
	                   || (!IKBD_ButtonsEqual(Keyboard.bOldLButtonDown,Keyboard.bLButtonDown)) || (!IKBD_ButtonsEqual(Keyboard.bOldRButtonDown,Keyboard.bRButtonDown))) )
	{
		if ( KeyboardProcessor.Mouse.DeltaX != 0 )
		{
			/* Left? */
			if (KeyboardProcessor.Mouse.DeltaX <= -KeyboardProcessor.Mouse.KeyCodeDeltaX)
			{
				if ( IKBD_OutputBuffer_CheckFreeCount ( 2 ) )
				{
					IKBD_Cmd_Return_Byte (75);		/* Left cursor */
					IKBD_Cmd_Return_Byte (75|0x80);
				}
				KeyboardProcessor.Mouse.DeltaX += KeyboardProcessor.Mouse.KeyCodeDeltaX;
			}
			/* Right? */
			if (KeyboardProcessor.Mouse.DeltaX >= KeyboardProcessor.Mouse.KeyCodeDeltaX)
			{
				if ( IKBD_OutputBuffer_CheckFreeCount ( 2 ) )
				{
					IKBD_Cmd_Return_Byte (77);		/* Right cursor */
					IKBD_Cmd_Return_Byte (77|0x80);
				}
				KeyboardProcessor.Mouse.DeltaX -= KeyboardProcessor.Mouse.KeyCodeDeltaX;
			}
		}

		if ( KeyboardProcessor.Mouse.DeltaY != 0 )
		{
			/* Up? */
			if (KeyboardProcessor.Mouse.DeltaY <= -KeyboardProcessor.Mouse.KeyCodeDeltaY)
			{
				if ( IKBD_OutputBuffer_CheckFreeCount ( 2 ) )
				{
					IKBD_Cmd_Return_Byte (72);		/* Up cursor */
					IKBD_Cmd_Return_Byte (72|0x80);
				}
				KeyboardProcessor.Mouse.DeltaY += KeyboardProcessor.Mouse.KeyCodeDeltaY;
			}
			/* Down? */
			if (KeyboardProcessor.Mouse.DeltaY >= KeyboardProcessor.Mouse.KeyCodeDeltaY)
			{
				if ( IKBD_OutputBuffer_CheckFreeCount ( 2 ) )
				{
					IKBD_Cmd_Return_Byte (80);		/* Down cursor */
					IKBD_Cmd_Return_Byte (80|0x80);
				}
				KeyboardProcessor.Mouse.DeltaY -= KeyboardProcessor.Mouse.KeyCodeDeltaY;
			}
		}

		if ( IKBD_OutputBuffer_CheckFreeCount ( 2 ) )
		{
			/* Left button? */
			if ( (IKBD_ButtonBool(Keyboard.bLButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldLButtonDown))) )
				IKBD_Cmd_Return_Byte (0x74);		/* Left */
			else if ( (IKBD_ButtonBool(Keyboard.bOldLButtonDown) && (!IKBD_ButtonBool(Keyboard.bLButtonDown))) )
				IKBD_Cmd_Return_Byte (0x74|0x80);
			/* Right button? */
			if ( (IKBD_ButtonBool(Keyboard.bRButtonDown) && (!IKBD_ButtonBool(Keyboard.bOldRButtonDown))) )
				IKBD_Cmd_Return_Byte (0x75);		/* Right */
			else if ( (IKBD_ButtonBool(Keyboard.bOldRButtonDown) && (!IKBD_ButtonBool(Keyboard.bRButtonDown))) )
				IKBD_Cmd_Return_Byte (0x75|0x80);
		}
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
static void IKBD_SendAutoKeyboardCommands(void)
{
	/* Don't do anything until processor is first reset */
	if ( bDuringResetCriticalTime )
		return;

	/* Read joysticks for this frame */
	IKBD_GetJoystickData();

	/* Check for double-clicks in maximum speed mode */
	IKBD_CheckForDoubleClicks();

	/* Handle Joystick/Mouse fire buttons */
	IKBD_DuplicateMouseFireButtons();

	/* Send any packets which are to be reported by mouse action */
	IKBD_SendOnMouseAction();

	/* Update internal mouse absolute position by find 'delta' of mouse movement */
	IKBD_UpdateInternalMousePosition();

	/* If IKBD is monitoring only joysticks, don't report other events */
	if ( KeyboardProcessor.JoystickMode == AUTOMODE_JOYSTICK_MONITORING )
	{
		IKBD_SendAutoJoysticksMonitoring();
		return;
	}

	/* Send automatic joystick packets */
	if (KeyboardProcessor.JoystickMode==AUTOMODE_JOYSTICK)
		IKBD_SendAutoJoysticks();
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
	if (JoystickSpaceBar==JOYSTICK_SPACE_DOWN)
	{
		IKBD_PressSTKey(57, true);                /* Press */
		JoystickSpaceBar = JOYSTICK_SPACE_DOWNED; /* Pressed */
	}
	else if (JoystickSpaceBar==JOYSTICK_SPACE_UP)
	{
		IKBD_PressSTKey(57, false);               /* Release */
		JoystickSpaceBar = JOYSTICK_SPACE_NULL;   /* Complete */
	}

	/* If we're executing a custom IKBD program, call it to process the key/mouse/joystick event */
	if ( IKBD_ExeMode && pIKBD_CustomCodeHandler_Read )
		(*pIKBD_CustomCodeHandler_Read) ();
}


/*-----------------------------------------------------------------------*/
/**
 * When press/release key under host OS, execute this function.
 */
void IKBD_PressSTKey(uint8_t ScanCode, bool bPress)
{
	/* If IKBD is monitoring only joysticks, don't report key */
	if ( KeyboardProcessor.JoystickMode == AUTOMODE_JOYSTICK_MONITORING )
		return;

	/* Store the state of each ST scancode : 1=pressed 0=released */
	if ( bPress )		ScanCodeState[ ScanCode & 0x7f ] = 1;
	else			ScanCodeState[ ScanCode & 0x7f ] = 0;

	if (!bPress)
		ScanCode |= 0x80;				/* Set top bit if released key */

	if ( IKBD_OutputBuffer_CheckFreeCount ( 1 ) )
	{
		IKBD_Cmd_Return_Byte (ScanCode);		/* Add to the IKBD's output buffer */
	}

	/* If we're executing a custom IKBD program, call it to process the key event */
	if ( IKBD_ExeMode && pIKBD_CustomCodeHandler_Read )
		(*pIKBD_CustomCodeHandler_Read) ();
}


/*-----------------------------------------------------------------------*/
/**
 * Check if a key is pressed in the ScanCodeState array
 * Return the scancode >= 0 for the first key we find, else return -1
 * if no key is pressed
 */
static int IKBD_CheckPressedKey(void)
{
	unsigned int	i;

	for (i=0 ; i<sizeof(ScanCodeState) ; i++ )
		if ( ScanCodeState[ i ] )
			return i;

	return -1;
}


/*-----------------------------------------------------------------------*/
/**
 * This function is called regularly to automatically send keyboard, mouse
 * and joystick updates.
 */
void IKBD_InterruptHandler_AutoSend(void)
{
	/* Handle user events and other messages, (like quit message) */
	Main_EventHandler();

	/* Remove this interrupt from list and re-order.
	 * (needs to be done after UI event handling so
	 * that snapshots saved from UI and restored from
	 * command line don't miss the AUTOSEND interrupt)
	 */
	CycInt_AcknowledgeInterrupt();

	/* Did user try to quit? */
	if (bQuitProgram)
	{
		/* Assure that CPU core shuts down */
		M68000_SetSpecial(SPCFLAG_BRK);
		return;
	}

	/* Trigger this auto-update function again after a while */
	CycInt_AddRelativeInterrupt(Keyboard.AutoSendCycles, INT_CPU8_CYCLE, INTERRUPT_IKBD_AUTOSEND);

	/* We don't send keyboard data automatically within the first few
	 * VBLs to avoid that TOS gets confused during its boot time */
	if (nVBLs > 20)
	{
		/* Send automatic keyboard packets for mouse, joysticks etc... */
		IKBD_SendAutoKeyboardCommands();
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
			bBothMouseAndJoy = true;

			LOG_TRACE(TRACE_IKBD_ALL, "ikbd cancel commands 0x12 and 0x1a received during reset,"
				" enabling joystick and mouse reporting at the same time\n" );
		}
	}
}



/*-----------------------------------------------------------------------*/
/**
 * When a byte is received by the IKBD, it is added to a small 8 byte buffer.
 * - If the first byte is a valid command, we wait for additional bytes if needed
 *   and then we execute the command's handler.
 * - If the first byte is not a valid command or after a successful command, we
 *   empty the input buffer (extra bytes, if any, are lost)
 * - If the input buffer is full when a new byte is received, the new byte is lost.
 * - In case the first byte read is not a valid command then IKBD does nothing
 *   (it doesn't return any byte to indicate the command was not recognized)
 */
static void IKBD_RunKeyboardCommand(uint8_t aciabyte)
{
	int i=0;

	/* Write into our keyboard input buffer if it's not full yet */
	if ( Keyboard.nBytesInInputBuffer < SIZE_KEYBOARDINPUT_BUFFER )
		Keyboard.InputBuffer[Keyboard.nBytesInInputBuffer++] = aciabyte;

	/* Now check bytes to see if we have a valid/in-valid command string set */
	while (KeyboardCommands[i].Command!=0xff)
	{
		/* Found command? */
		if (KeyboardCommands[i].Command==Keyboard.InputBuffer[0])
		{
			/* If the command is complete (with its possible parameters) we can execute it */
			/* Else, we wait for the next bytes until the command is complete */
			if (KeyboardCommands[i].NumParameters==Keyboard.nBytesInInputBuffer)
			{
				/* Any new valid command will unpause the output (if command 0x13 was used) */
				Keyboard.PauseOutput = false;

				CALL_VAR(KeyboardCommands[i].pCallFunction);
				Keyboard.nBytesInInputBuffer = 0;	/* Clear input buffer after processing a command */
			}

			return;
		}

		i++;
	}

	/* Command not known, reset buffer(IKBD assumes a NOP) */
	Keyboard.nBytesInInputBuffer = 0;
}




/************************************************************************/
/* List of keyboard commands handled by the standard IKBD's ROM.	*/
/* Each IKBD's command is emulated to get the same result as if we were	*/
/* running a full HD6301 emulation.					*/
/************************************************************************/


/*-----------------------------------------------------------------------*/
/**
 * RESET
 *
 * 0x80
 * 0x01
 *
 * Performs self test and checks for stuck (closed) keys, if OK returns
 * IKBD_ROM_VERSION (0xF1). Otherwise returns break codes for keys (not emulated).
 */
static void IKBD_Cmd_Reset(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_Reset VBLs=%i framecyc=%i\n",
	          nVBLs, Cycles_GetCounter(CYCLES_COUNTER_VIDEO));

	/* Check that 0x01 was received after 0x80 */
	if (Keyboard.InputBuffer[1] == 0x01)
	{
		IKBD_Boot_ROM ( false );
	}
	/* else if not 0x80,0x01 just ignore */
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

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_MouseAction %d\n",
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

	/* Some games (like Barbarian by Psygnosis) enable both, mouse and
	 * joystick directly after a reset. This causes the IKBD to send both
	 * type of packets. To emulate this feature, we've got to remember
	 * that the mouse has been enabled during reset. */
	if (bDuringResetCriticalTime)
		bMouseEnabledDuringReset = true;

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_RelMouseMode\n");
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

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_AbsMouseMode %d,%d\n",
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

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_MouseCursorKeycodes %d,%d\n",
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

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_SetMouseThreshold %d,%d\n",
	          KeyboardProcessor.Mouse.XThreshold, KeyboardProcessor.Mouse.YThreshold);
}


/*-----------------------------------------------------------------------*/
/**
 * SET MOUSE SCALE
 *
 * 0x0C
 * X      ; horizontal mouse ticks per internal X
 * Y      ; vertical mouse ticks per internal Y
 */
static void IKBD_Cmd_SetMouseScale(void)
{
	KeyboardProcessor.Mouse.XScale = (unsigned int)Keyboard.InputBuffer[1];
	KeyboardProcessor.Mouse.YScale = (unsigned int)Keyboard.InputBuffer[2];

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_SetMouseScale %d,%d\n",
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
	uint8_t Buttons,PrevButtons;

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
	if ( IKBD_OutputBuffer_CheckFreeCount ( 6 ) )
	{
		IKBD_Cmd_Return_Byte_Delay (0xf7, 18000-ACIA_CYCLES);
		IKBD_Cmd_Return_Byte (Buttons);
		IKBD_Cmd_Return_Byte ((unsigned int)KeyboardProcessor.Abs.X>>8);
		IKBD_Cmd_Return_Byte ((unsigned int)KeyboardProcessor.Abs.X&0xff);
		IKBD_Cmd_Return_Byte ((unsigned int)KeyboardProcessor.Abs.Y>>8);
		IKBD_Cmd_Return_Byte ((unsigned int)KeyboardProcessor.Abs.Y&0xff);
	}

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReadAbsMousePos %d,%d 0x%X\n",
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

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_SetInternalMousePos %d,%d\n",
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

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_SetYAxisDown\n");
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

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_SetYAxisUp\n");
}


/*-----------------------------------------------------------------------*/
/**
 * RESUME
 *
 * Any command received by the IKBD will also resume the output if it was
 * paused by command 0x13, so this command is redundant.
 *
 * 0x11
 */
static void IKBD_Cmd_StartKeyboardTransfer(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_StartKeyboardTransfer\n");
	Keyboard.PauseOutput = false;
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
	bMouseDisabled = true;

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_TurnMouseOff\n");

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
	if (bDuringResetCriticalTime)
	{
		/* Required for the loader of 'Just Bugging' by ACF */
		LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_StopKeyboardTransfer ignored during ikbd reset\n");
		return;
	}

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_StopKeyboardTransfer\n");
	Keyboard.PauseOutput = true;
}


/*-----------------------------------------------------------------------*/
/**
 * SET JOYSTICK EVENT REPORTING
 *
 * 0x14
 */
static void IKBD_Cmd_ReturnJoystickAuto(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReturnJoystickAuto\n");

	KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK;
	KeyboardProcessor.MouseMode = AUTOMODE_OFF;

	/* If mouse was also enabled within time of a reset (0x08 command) it isn't disabled now!
	 * (used by the game Barbarian 1 by Psygnosis for example) */
	if ( bDuringResetCriticalTime && bMouseEnabledDuringReset )
	{
		KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
		bBothMouseAndJoy = true;
		LOG_TRACE(TRACE_IKBD_ALL, "ikbd commands 0x08 and 0x14 received during reset,"
			" enabling joystick and mouse reporting at the same time\n" );
	}
	/* If mouse was disabled during the reset (0x12 command) it is enabled again
	 * (used by the game Hammerfist for example) */
	else if ( bDuringResetCriticalTime && bMouseDisabled )
	{
		KeyboardProcessor.MouseMode = AUTOMODE_MOUSEREL;
		bBothMouseAndJoy = true;
		LOG_TRACE(TRACE_IKBD_ALL, "ikbd commands 0x12 and 0x14 received during reset,"
			" enabling joystick and mouse reporting at the same time\n" );
	}

	/* This command resets the internally previously stored joystick states */
	KeyboardProcessor.Joy.PrevJoyData[JOYID_JOYSTICK0] = KeyboardProcessor.Joy.PrevJoyData[JOYID_JOYSTICK1] = 0;

	/* This is a hack for the STE Utopos (=> v1.50) and Falcon Double Bubble
	 * 2000 games. They expect the joystick data to be sent within a certain
	 * amount of time after this command, without checking the ACIA control
	 * register first.
	 */
	IKBD_GetJoystickData();
	IKBD_SendAutoJoysticks();
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
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_StopJoystick\n");
}


/*-----------------------------------------------------------------------*/
/**
 * JOYSTICK INTERROGATE
 *
 * 0x16
 */
static void IKBD_Cmd_ReturnJoystick(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReturnJoystick\n");

	if ( IKBD_OutputBuffer_CheckFreeCount ( 3 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xFD , IKBD_Delay_Random ( 7500 , 10000 ) );
		IKBD_Cmd_Return_Byte (Joy_GetStickData(JOYID_JOYSTICK0));
		IKBD_Cmd_Return_Byte (Joy_GetStickData(JOYID_JOYSTICK1));
	}
}


/*-----------------------------------------------------------------------*/
/**
 * SET JOYSTICK MONITORING
 *
 * 0x17
 * rate      ; time between samples in hundredths of a second
 *   Returns: (in packets of two as long as in mode)
 *     %000000xy  where y is JOYSTICK1 Fire button
 *         and x is JOYSTICK0 Fire button
 *     %nnnnmmmm  where m is JOYSTICK1 state
 *         and n is JOYSTICK0 state
 *
 * TODO : we use a fixed 8 MHz clock to convert rate in 1/100th of sec into cycles.
 * This should be replaced by using MachineClocks.CPU_Freq.
 */
static void IKBD_Cmd_SetJoystickMonitoring(void)
{
	int	Rate;
	int	Cycles;

	Rate = (unsigned int)Keyboard.InputBuffer[1];

	KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK_MONITORING;
	KeyboardProcessor.MouseMode = AUTOMODE_OFF;

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_SetJoystickMonitoring %d\n" , Rate );

	if ( Rate == 0 )
		Rate = 1;

	Cycles = 8021247 * Rate / 100;
	CycInt_AddRelativeInterrupt ( Cycles, INT_CPU8_CYCLE, INTERRUPT_IKBD_AUTOSEND );

	Keyboard.AutoSendCycles = Cycles;
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
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_SetJoystickFireDuration (not implemented)\n");
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
 *         ; until horizontal cursor keystrokes are generated after RX
 *         ; has elapsed
 * VY        ; length (in tenths of seconds) of joystick closure
 *         ; until vertical cursor keystrokes are generated after RY
 *         ; has elapsed
 */
static void IKBD_Cmd_SetCursorForJoystick(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_SetCursorForJoystick (not implemented)\n");
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
	bJoystickDisabled = true;

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_DisableJoysticks\n");

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
 *
 * All bytes are stored in BCD format. If a byte is not in BCD, we ignore it
 * but we process the rest of the bytes.
 * Note that the IKBD doesn't check that month/day/hour/second/minute are in
 * their correct range, just that they're BCD encoded (so you can store 0x30 in hour
 * for example, see IKBD_UpdateClockOnVBL())
 */
static void IKBD_Cmd_SetClock(void)
{
	int	i;
	uint8_t	val;

	LOG_TRACE(TRACE_IKBD_CMDS,
		  "IKBD_Cmd_SetClock: %02x %02x %02x %02x %02x %02x\n",
		  Keyboard.InputBuffer[1], Keyboard.InputBuffer[2],
		  Keyboard.InputBuffer[3], Keyboard.InputBuffer[4],
		  Keyboard.InputBuffer[5], Keyboard.InputBuffer[6]);

	for ( i=1 ; i<=6 ; i++ )
	{
		val = Keyboard.InputBuffer[ i ];
		if ( IKBD_BCD_Check ( val ) )			/* Check if valid BCD, else ignore */
			pIKBD->Clock[ i-1 ] = val;		/* Store new value */
	}
}


/*-----------------------------------------------------------------------*/
/**
 * INTERROGATE TIME-OF-DAY CLOCK
 *
 * 0x1C
 *   Returns:
 *     0xFC  ; time-of-day event header
 *     YY    ; year (2 least significant digits)
 *     MM    ; month
 *     DD    ; day
 *     hh    ; hour
 *     mm    ; minute
 *     ss    ; second
 *
 * All bytes are stored/returned in BCD format.
 * Date/Time is updated in IKBD_UpdateClockOnVBL()
 */
static void IKBD_Cmd_ReadClock(void)
{
	int	i;

	LOG_TRACE(TRACE_IKBD_CMDS,
		"IKBD_Cmd_ReadClock: %02x %02x %02x %02x %02x %02x\n",
		pIKBD->Clock[ 0 ] ,pIKBD->Clock[ 1 ] , pIKBD->Clock[ 2 ] ,
		pIKBD->Clock[ 3 ] , pIKBD->Clock[ 4 ] , pIKBD->Clock[ 5 ] );

	/* Return packet header */
	if ( IKBD_OutputBuffer_CheckFreeCount ( 7 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xFC , IKBD_Delay_Random ( 7000 , 7500 ) );

		/* Return the 6 clock bytes */
		for ( i=0 ; i<6 ; i++ )
			IKBD_Cmd_Return_Byte ( pIKBD->Clock[ i ] );
	}
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
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_LoadMemory addr 0x%x count %d\n",
		  (Keyboard.InputBuffer[1] << 8) + Keyboard.InputBuffer[2], Keyboard.InputBuffer[3]);

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
 *
 * NOTE : This function requires to handle the IKBD's RAM, which is only
 * possible when emulating a real HD6301 CPU. For now, we only return
 * the correct header and 6 empty bytes.
 */
static void IKBD_Cmd_ReadMemory(void)
{
	int	i;

	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReadMemory (not implemented)\n");

	/* Return packet header */
	if ( IKBD_OutputBuffer_CheckFreeCount ( 8 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xF6 , IKBD_Delay_Random ( 7000 , 7500 ) );
		IKBD_Cmd_Return_Byte ( 0x20 );

		/* Return 6 empty bytes */
		for ( i=0 ; i<6 ; i++ )
			IKBD_Cmd_Return_Byte ( 0x00 );
	}
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
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_Execute addr 0x%x\n",
		(Keyboard.InputBuffer[1] << 8) + Keyboard.InputBuffer[2]);

	if ( pIKBD_CustomCodeHandler_Write )
	{
		LOG_TRACE(TRACE_IKBD_EXEC, "ikbd execute addr 0x%x using custom handler\n",
			  (Keyboard.InputBuffer[1] << 8) + Keyboard.InputBuffer[2]);

		IKBD_ExeMode = true;				/* turn 6301's custom mode ON */
	}
	else							/* unknown code uploaded to ikbd RAM */
	{
		LOG_TRACE(TRACE_IKBD_EXEC, "ikbd execute addr 0x%x ignored, no custom handler found\n",
			  (Keyboard.InputBuffer[1] << 8) + Keyboard.InputBuffer[2]);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * REPORT MOUSE BUTTON ACTION
 *
 * 0x87
 */
static void IKBD_Cmd_ReportMouseAction(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReportMouseAction\n");

	if ( IKBD_OutputBuffer_CheckFreeCount ( 8 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xF6 , IKBD_Delay_Random ( 7000 , 7500 ) );
		IKBD_Cmd_Return_Byte (7);
		IKBD_Cmd_Return_Byte (KeyboardProcessor.Mouse.Action);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * REPORT MOUSE MODE
 *
 * 0x88 or 0x89 or 0x8A
 */
static void IKBD_Cmd_ReportMouseMode(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReportMouseMode\n");

	if ( IKBD_OutputBuffer_CheckFreeCount ( 8 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xF6 , IKBD_Delay_Random ( 7000 , 7500 ) );
		switch (KeyboardProcessor.MouseMode)
		{
		case AUTOMODE_MOUSEREL:
			IKBD_Cmd_Return_Byte (8);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			break;
		case AUTOMODE_MOUSEABS:
			IKBD_Cmd_Return_Byte (9);
			IKBD_Cmd_Return_Byte (KeyboardProcessor.Abs.MaxX >> 8);
			IKBD_Cmd_Return_Byte (KeyboardProcessor.Abs.MaxX);
			IKBD_Cmd_Return_Byte (KeyboardProcessor.Abs.MaxY >> 8);
			IKBD_Cmd_Return_Byte (KeyboardProcessor.Abs.MaxY);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			break;
		case AUTOMODE_MOUSECURSOR:
			IKBD_Cmd_Return_Byte (10);
			IKBD_Cmd_Return_Byte (KeyboardProcessor.Mouse.KeyCodeDeltaX);
			IKBD_Cmd_Return_Byte (KeyboardProcessor.Mouse.KeyCodeDeltaY);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			break;
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * REPORT MOUSE THRESHOLD
 *
 * 0x8B
 */
static void IKBD_Cmd_ReportMouseThreshold(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReportMouseThreshold\n");

	if ( IKBD_OutputBuffer_CheckFreeCount ( 8 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xF6 , IKBD_Delay_Random ( 7000 , 7500 ) );
		IKBD_Cmd_Return_Byte (0x0B);
		IKBD_Cmd_Return_Byte (KeyboardProcessor.Mouse.XThreshold);
		IKBD_Cmd_Return_Byte (KeyboardProcessor.Mouse.YThreshold);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * REPORT MOUSE SCALE
 *
 * 0x8C
 */
static void IKBD_Cmd_ReportMouseScale(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReportMouseScale\n");

	if ( IKBD_OutputBuffer_CheckFreeCount ( 8 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xF6 , IKBD_Delay_Random ( 7000 , 7500 ) );
		IKBD_Cmd_Return_Byte (0x0C);
		IKBD_Cmd_Return_Byte (KeyboardProcessor.Mouse.XScale);
		IKBD_Cmd_Return_Byte (KeyboardProcessor.Mouse.YScale);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * REPORT MOUSE VERTICAL COORDINATES
 *
 * 0x8F and 0x90
 */
static void IKBD_Cmd_ReportMouseVertical(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReportMouseVertical\n");

	if ( IKBD_OutputBuffer_CheckFreeCount ( 8 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xF6 , IKBD_Delay_Random ( 7000 , 7500 ) );
		if (KeyboardProcessor.Mouse.YAxis == -1)
			IKBD_Cmd_Return_Byte (0x0F);
		else
			IKBD_Cmd_Return_Byte (0x10);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * REPORT MOUSE AVAILABILITY
 *
 * 0x92
 */
static void IKBD_Cmd_ReportMouseAvailability(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReportMouseAvailability\n");

	if ( IKBD_OutputBuffer_CheckFreeCount ( 8 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xF6 , IKBD_Delay_Random ( 7000 , 7500 ) );
		if (KeyboardProcessor.MouseMode == AUTOMODE_OFF)
			IKBD_Cmd_Return_Byte (0x12);
		else
			IKBD_Cmd_Return_Byte (0x00);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * REPORT JOYSTICK MODE
 *
 * 0x94 or 0x95 or 0x99
 */
static void IKBD_Cmd_ReportJoystickMode(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReportJoystickMode\n");

	if ( IKBD_OutputBuffer_CheckFreeCount ( 8 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xF6 , IKBD_Delay_Random ( 7000 , 7500 ) );
		switch (KeyboardProcessor.JoystickMode)
		{
		case AUTOMODE_JOYSTICK:
			IKBD_Cmd_Return_Byte (0x14);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			break;
		default:    /* TODO: Joystick keycodes mode not supported yet! */
			IKBD_Cmd_Return_Byte (0x15);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			IKBD_Cmd_Return_Byte (0);
			break;
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * REPORT JOYSTICK AVAILABILITY
 *
 * 0x9A
 */
static void IKBD_Cmd_ReportJoystickAvailability(void)
{
	LOG_TRACE(TRACE_IKBD_CMDS, "IKBD_Cmd_ReportJoystickAvailability\n");

	if ( IKBD_OutputBuffer_CheckFreeCount ( 8 ) )
	{
		IKBD_Cmd_Return_Byte_Delay ( 0xF6 , IKBD_Delay_Random ( 7000 , 7500 ) );
		if (KeyboardProcessor.JoystickMode == AUTOMODE_OFF)
			IKBD_Cmd_Return_Byte (0x1A);
		else
			IKBD_Cmd_Return_Byte (0x00);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
		IKBD_Cmd_Return_Byte (0);
	}
}




/************************************************************************/
/* End of the IKBD's commands emulation.				*/
/************************************************************************/




/*************************************************************************/
/**
 * Below part is for emulating custom 6301 program sent to the IKBD's RAM
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
 * If a match is found for this 2nd CRC, we will override default IKBD's behaviour
 * for reading/writing to $fffc02 with ExeMainHandler_Read / ExeMainHandler_Write
 * (once the Execute command 0x22 is received).
 *
 * When using custom program (ExeMode==true), we must ignore all keyboard/mouse/joystick
 * events sent to IKBD_Cmd_Return_Byte . Only our functions can add bytes
 * to the keyboard buffer.
 *
 * To exit 6301's execution mode, we can use the 68000 'reset' instruction.
 * Some 6301's programs also handle a write to $fffc02 as an exit signal.
 */


/*-----------------------------------------------------------------------*/
/**
 * Handle writes to $fffc02 when loading bytes in the IKBD's RAM.
 * We compute a CRC of the bytes that are sent until MemoryLoadNbBytesLeft
 * reaches 0.
 * When all bytes are loaded, we look for a matching CRC ; if found, we
 * use the ExeBootHandler defined for this CRC to process the next writes
 * that will occur in $fffc02.
 * LoadMemory is often used to load a small boot code into the 6301's RAM.
 * This small program will be executed later using the command 0x22.
 */

static void IKBD_LoadMemoryByte ( uint8_t aciabyte )
{
	unsigned int i;

	/* Write received bytes to a file for debug */
//	FILE *f = fopen ( "/tmp/ikbd_loadmemory.dump" , "ab" ) ; fprintf ( f , "%c" , aciabyte ) ; fclose ( f );

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
			LOG_TRACE(TRACE_IKBD_EXEC, "ikbd loadmemory %d bytes crc=0x%x matches <%s>\n",
				  MemoryLoadNbBytesTotal, MemoryLoadCrc, CustomCodeDefinitions[ i ].Name);

			crc32_reset ( &MemoryLoadCrc );
			MemoryExeNbBytes = 0;
			pIKBD_CustomCodeHandler_Read = NULL;
			pIKBD_CustomCodeHandler_Write = CustomCodeDefinitions[ i ].ExeBootHandler;
		}

		else							/* unknown code uploaded to IKBD's RAM */
		{
			LOG_TRACE(TRACE_IKBD_EXEC, "ikbd loadmemory %d bytes crc=0x%x : unknown code\n",
				  MemoryLoadNbBytesTotal, MemoryLoadCrc);

			pIKBD_CustomCodeHandler_Read = NULL;
			pIKBD_CustomCodeHandler_Write = NULL;
		}
	}
}



/*-----------------------------------------------------------------------*/
/**
 * Handle writes to $fffc02 when executing custom code in the IKBD's RAM.
 * This is used to send the small IKBD program that will handle
 * keyboard/mouse/joystick input.
 * We compute a CRC of the bytes that are sent until we found a match
 * with a known custom IKBD program.
 */

static void IKBD_CustomCodeHandler_CommonBoot ( uint8_t aciabyte )
{
	unsigned int i;

	/* Write received bytes to a file for debug */
//	FILE *f = fopen ( "/tmp/ikbd_custom_program.dump" , "ab" ) ; fprintf ( f , "%c" , aciabyte ) ; fclose ( f );

	crc32_add_byte ( &MemoryLoadCrc , aciabyte );
	MemoryExeNbBytes++;

	LOG_TRACE(TRACE_IKBD_EXEC, "ikbd custom exe common boot write 0x%02x count %d crc=0x%x\n",
	          aciabyte, MemoryExeNbBytes, MemoryLoadCrc);

	/* Search for a match amongst the known custom routines */
	for ( i = 0 ; i < sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ) ; i++ )
		if ( ( CustomCodeDefinitions[ i ].MainProgNbBytes == MemoryExeNbBytes )
			&& ( CustomCodeDefinitions[ i ].MainProgCrc == MemoryLoadCrc ) )
			break;

	if ( i < sizeof ( CustomCodeDefinitions ) / sizeof ( CustomCodeDefinitions[0] ) )	/* found */
	{
		LOG_TRACE(TRACE_IKBD_EXEC, "ikbd custom exe common boot, uploaded code matches <%s>\n",
			  CustomCodeDefinitions[i].Name);

		pIKBD_CustomCodeHandler_Read = CustomCodeDefinitions[ i ].ExeMainHandler_Read;
		pIKBD_CustomCodeHandler_Write = CustomCodeDefinitions[ i ].ExeMainHandler_Write;

		Keyboard.BufferHead = Keyboard.BufferTail = 0;	/* flush all queued bytes that would be read in $fffc02 */
		Keyboard.NbBytesInOutputBuffer = 0;
	}

	/* If not found, we keep on accumulating bytes until we find a matching crc */
}



/*----------------------------------------------------------------------*/
/* Froggies Over The Fence menu.					*/
/* Returns 'n' bytes with the mouse position, keyboard can be used too.	*/
/* Writing a <0 byte to $fffc02 will cause the 6301 to exit custom exe	*/
/* mode (jmp $f000).							*/
/* When writing byte 'n' >0 to $fffc02, the 6301 will return the content*/
/* of RAM $7f+n to $7f+1.						*/
/* $80/$81 contains deltaY/deltaX + left mouse button in bit 7, $82	*/
/* contains LMB in bit 7 and $83 contains a fixed value 0xfc.		*/
/* On each VBL, the demo will ask for 1 byte, then for 4 bytes ; only	*/
/* the last 2 bytes ($81/$80) will be used, $83/$82 are ignored.	*/
/* IKBD's $81 will be stored in $600 (CPU RAM), and $80 in $601.	*/
/*									*/
/* TODO : an extra delay of 7000 cycles is necessary to have $81 and $80*/
/* received after the overrun condition was cleared at the 68000 level.	*/
/* Does it mean some timings are wrong with acia/ikbd ?			*/
/*----------------------------------------------------------------------*/

static void IKBD_CustomCodeHandler_FroggiesMenu_Read ( void )
{
	/* Ignore read */
}

static void IKBD_CustomCodeHandler_FroggiesMenu_Write ( uint8_t aciabyte )
{
	uint8_t		res80 = 0;
	uint8_t		res81 = 0;
	uint8_t		res82 = 0;
	uint8_t		res83 = 0xfc;					/* fixed value, not used */

	/* When writing a <0 byte to $fffc02, Froggies ikbd's program will terminate itself */
	/* and leave Execution mode (jmp $f000) */
	if ( aciabyte & 0x80 )
	{
		IKBD_Boot_ROM ( false );
		return;
	}

	if ( KeyboardProcessor.Mouse.DeltaY < 0 )	res80 = 0x7a;	/* mouse up */
	if ( KeyboardProcessor.Mouse.DeltaY > 0 )	res80 = 0x06;	/* mouse down */
	if ( KeyboardProcessor.Mouse.DeltaX < 0 )	res81 = 0x7a;	/* mouse left */
	if ( KeyboardProcessor.Mouse.DeltaX > 0 )	res81 = 0x06;	/* mouse right */
	if ( Keyboard.bLButtonDown & BUTTON_MOUSE )	res82 |= 0x80;	/* left mouse button */

	if ( ScanCodeState[ 0x48 ] )			res80 |= 0x7a;	/* up */
	if ( ScanCodeState[ 0x50 ] )			res80 |= 0x06;	/* down */
	if ( ScanCodeState[ 0x4b ] )			res81 |= 0x7a;	/* left */
	if ( ScanCodeState[ 0x4d ] )			res81 |= 0x06;	/* right */
	if ( ScanCodeState[ 0x70 ] )			res82 |= 0x80;	/* keypad 0 */

	res80 |= res82;							/* bit 7 is left mouse button */
	res81 |= res82;

//	res80 = 0x10 ; res81 = 0x11 ; res82 = 0x12 ; res83 = 0x13 ;	/* force some discernible values to debug */
	
	if ( aciabyte == 1 )						/* Send 1 byte */
		IKBD_Send_Byte_Delay ( res80 , 0 );			/* $80 in IKBD's RAM */

	else if ( aciabyte == 4 )					/* Send 4 bytes */
	{
		IKBD_Send_Byte_Delay ( res83 , 7000 );			/* $83 in IKBD's RAM */
		IKBD_Send_Byte_Delay ( res82 , 0 );			/* $82 in IKBD's RAM */
		IKBD_Send_Byte_Delay ( res81 , 0 );			/* $81 in IKBD's RAM */
		IKBD_Send_Byte_Delay ( res80 , 0 );			/* $80 in IKBD's RAM */
	}
}



/*----------------------------------------------------------------------*/
/* Transbeauce II menu.							*/
/* Returns 1 byte with the joystick position, keyboard can be used too.	*/
/*----------------------------------------------------------------------*/

static void IKBD_CustomCodeHandler_Transbeauce2Menu_Read ( void )
{
	uint8_t		res = 0;

	/* keyboard emulation */
	if ( ScanCodeState[ 0x48 ] )	res |= 0x01;		/* up */
	if ( ScanCodeState[ 0x50 ] )	res |= 0x02;		/* down */
	if ( ScanCodeState[ 0x4b ] )	res |= 0x04;		/* left */
	if ( ScanCodeState[ 0x4d ] )	res |= 0x08;		/* right */
	if ( ScanCodeState[ 0x62 ] )	res |= 0x40;		/* help */
	if ( ScanCodeState[ 0x39 ] )	res |= 0x80;		/* space */

	/* joystick emulation (bit mapping is same as cursor above, with bit 7 = fire button */
	res |= ( Joy_GetStickData(JOYID_JOYSTICK1) & 0x8f ) ;			/* keep bits 0-3 and 7 */

	IKBD_Send_Byte_Delay ( res , 0 );
}

static void IKBD_CustomCodeHandler_Transbeauce2Menu_Write ( uint8_t aciabyte )
{
  /* Ignore write */
}



/*----------------------------------------------------------------------*/
/* Dragonnels demo menu.						*/
/* When any byte is written in $fffc02, returns one byte with the	*/
/* Y position of the mouse and the state of the left button.		*/
/*----------------------------------------------------------------------*/

static void IKBD_CustomCodeHandler_DragonnelsMenu_Read ( void )
{
	/* Ignore read */
}

static void IKBD_CustomCodeHandler_DragonnelsMenu_Write ( uint8_t aciabyte )
{
	uint8_t		res = 0;

	if ( KeyboardProcessor.Mouse.DeltaY < 0 )	res = 0xfc;	/* mouse up */
	if ( KeyboardProcessor.Mouse.DeltaY > 0 )	res = 0x04;	/* mouse down */

	if ( Keyboard.bLButtonDown & BUTTON_MOUSE )	res = 0x80;	/* left mouse button */

	IKBD_Send_Byte_Delay ( res , 0 );
}



/*----------------------------------------------------------------------*/
/* Chaos A.D. protection's decoder					*/
/* This custom program reads bytes, decode them and send back the result*/
/* to the 68000.							*/
/* The program first returns $fe to indicate it's ready to receive the	*/
/* encoded bytes.							*/
/* The program then receives the 8 bytes used to decode the data and	*/
/* store them in $f0 - $f7 (KeyBuffer is already initialized, so we	*/
/* ignore those 8 bytes).						*/
/* Then for any received byte a XOR is made with one of the byte in the	*/
/* 8 bytes buffer, by incrementing an index in this buffer.		*/
/* The decoded byte is written to addr $13 (TDR) to be received by ACIA	*/
/*----------------------------------------------------------------------*/

static void IKBD_CustomCodeHandler_ChaosAD_Read ( void )
{
	static bool	FirstCall = true;

	if ( FirstCall == true )
		IKBD_Send_Byte_Delay ( 0xfe , 0 );

	FirstCall = false;
}

static void IKBD_CustomCodeHandler_ChaosAD_Write ( uint8_t aciabyte )
{
	static int	IgnoreNb = 8;
	uint8_t		KeyBuffer[] = { 0xca , 0x0a , 0xbc , 0x00 , 0xde , 0xde , 0xfe , 0xca };
	static int	Index = 0;
	static int	Count = 0;

	/* We ignore the first 8 bytes we received (they're already in KeyBuffer) */
	if ( IgnoreNb > 0 )
	{
		IgnoreNb--;
		return;
	}

	if ( Count <= 6080 )						/* there're 6081 bytes to decode */
	{
		Count++;
		
		aciabyte ^= KeyBuffer[ Index ];
		Index++;
		Index &= 0x07;

		IKBD_Send_Byte_Delay ( aciabyte , 0 );
	}

	else
	{
		/* When all bytes were decoded if 0x08 is written to $fffc02 */
		/* the program will terminate itself and leave Execution mode */
		if ( aciabyte == 0x08 )
			IKBD_Boot_ROM ( false );
	}
}

/*----------------------------------------------------------------------*/
/* Audio Sculpture decryption support					*/
/* The main executable is decrypted with a key extracted from a   	*/
/* previously uploaded program in the 6301. When the magic value 0x42 	*/
/* is sent to fffc02 it will output the two bytes 0x4b and 0x13		*/
/* and exit the custom handler again					*/
/* [NP] The custom program has 2 parts :				*/
/*  - 1st part is used during the intro and wait for key 'space' in	*/
/*    color mode or any key in mono mode (but intro screen in mono	*/
/*    exits automatically without testing a key !)			*/
/*  - 2nd part wait to receive $42 from the ACIA, then send $4b and $13	*/
/*----------------------------------------------------------------------*/

static bool ASmagic = false;

static void IKBD_CustomCodeHandler_AudioSculpture_Color_Read ( void )
{
	IKBD_CustomCodeHandler_AudioSculpture_Read ( true );
}

static void IKBD_CustomCodeHandler_AudioSculpture_Mono_Read ( void )
{
	IKBD_CustomCodeHandler_AudioSculpture_Read ( false );
}

static void IKBD_CustomCodeHandler_AudioSculpture_Read ( bool ColorMode )
{
	uint8_t		res = 0;
	static int	ReadCount = 0;

	if ( ASmagic )
	{
		ReadCount++;
		if ( ReadCount == 2 )			/* We're done reading out the 2 bytes, exit the custom handler */
		{
			IKBD_Boot_ROM ( false );
			ASmagic = false;
			ReadCount = 0;
		}
	}

	else if ( ( ( ColorMode == false ) && ( IKBD_CheckPressedKey() >= 0 ) )		/* wait for any key in mono mode */
		|| ScanCodeState[ 0x39 ] )		/* wait for 'space' key in color mode */
	{
		res = 0x39;				/* send scancode for 'space' */
		IKBD_Send_Byte_Delay ( res , 0 );
	}
}

static void IKBD_CustomCodeHandler_AudioSculpture_Write ( uint8_t aciabyte )
{
	uint8_t		Magic = 0x42;
	uint8_t		Key[] = { 0x4b , 0x13 };

	if ( aciabyte == Magic )
	{
		ASmagic = true;
		IKBD_Send_Byte_Delay ( Key[0] , 0 );
		IKBD_Send_Byte_Delay ( Key[1] , 0 );
	}
}


void IKBD_Info(FILE *fp, uint32_t dummy)
{
	int i;
	fprintf(fp, "Transmit/Receive Control+Status: 0x%02x\n", pIKBD->TRCSR);
	fprintf(fp, "Rate + Mode Control:             0x%02x\n", pIKBD->RMCR);
	fprintf(fp, "Transmit:   Receive:\n");
	fprintf(fp, "- Data:  0x%02x  0x%02x\n", pIKBD->TDR, pIKBD->RDR);
	fprintf(fp, "- Shift: 0x%02x  0x%02x\n", pIKBD->TSR, pIKBD->RSR);
	fprintf(fp, "- State: %4d  %4d\n",
		pIKBD->SCI_TX_State, pIKBD->SCI_RX_State);
	fprintf(fp, "- #Bits: %4d  %4d\n",
		pIKBD->SCI_TX_Size, pIKBD->SCI_RX_Size);
	fprintf(fp, "- Delay: %4d\n", pIKBD->SCI_TX_Delay);
	fprintf(fp, "Clock:");
	for (i = 0; i < ARRAY_SIZE(pIKBD->Clock); i++)
		fprintf(fp, " %02x", pIKBD->Clock[i]);
	fprintf(fp, " (+%" PRId64 ")\n", pIKBD->Clock_micro);
}
