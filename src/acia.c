/*
  Hatari - acia.c

  Copyright (C) 2012 by Nicolas Pomarède

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  MC6850 ACIA emulation.
*/

const char ACIA_fileid[] = "Hatari acia.c : " __DATE__ " " __TIME__;


/* 2012/09/28	[NP]	Start of the full rewrite of the MC6850 ACIA emulation, using the official	*/
/*			datasheets for maximum accuracy, as well as bit level serial transfers (start,	*/
/*			stop and parity bits).								*/
/* 2012/12/21	[NP]	Add accurate cycles delays when accessing an ACIA register, taking E Clock	*/
/*			into account.									*/
/* 2013/04/24	[NP]	Remove INTERRUPT_ACIA_MFP used to add a 4 cycle delay when IRQ is set, as this	*/
/*			delay is now correctly handled directly in the MFP since 2013/03/01.		*/




/*
  6850 ACIA (Asynchronous Communications Inferface Apdater)

  References :
   - MC6850 datasheet by Motorola (DS9493R4, 1985)
   - A6850 datasheet by Altera (A-DS-A6850-01, 1996) (nearly identical component)

  Others references :
   - MAME's 6850acia.c for RTS, CTS and DCD behaviour


  Pins :
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

  Registers :
    0xfffc00 Keyboard ACIA Control (write)/Status(read)
    0xfffc02 Keyboard ACIA Data
    0xfffc04 MIDI ACIA Control (write)/Status(read)
    0xfffc06 MIDI ACIA Data

  Control Register (0xfffc00 write) :
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
      OVERRUN (a received character isn't properly read from the processor).
        0 Interrupt disabled
        1 Interrupt enabled

  Status Register (0xfffc00 read) :
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

  ST ACIA :
    CTS,DCD and RTS are not connected
    The keyboard ACIA addresses are 0xfffc000 and 0xfffc02.
    The MIDI ACIA addresses are 0xfffc004 and 0xfffc06.
    Default keyboard parameters are : 8-bit word, 1 stopbit, no parity, 7812.5 baud; 500KHz/64 (keyboard clock div)
    Default MIDI parameters are as above but : 31250 baud; 500KHz/16 (MIDI clock div)


  CPU cycles in the ST :
    When accessing an ACIA register, an additional delay will be added to the usual number of
    cycles for this CPU instruction. This delay is made of 2 parts (for a 68000 at 8 MHz) :
	- a fixed delay of 6 cycles.
	- a variable delay of 0 to 8 cycles to synchronise with the E Clock.

    Examples for some common instructions measured on a real 520 STF
    (with a0=$fffffc00 and 'n' the delay for E Clock) :
	move.b  (a0),d2 : 14 cycles = 8 + 6 + n
	move.w  (a0),d2 : 14 cycles = 8 + 6 + n
	move.l  (a0),d2 : 24 cycles = 12 + 6 + 6 + n
	movep.w (a0),d2 : 28 cycles = 16 + 6 + 6 + n
	movep.l (a0),d2 : 48 cycles = 24 + 6 + 6 + 6 + 6 + n
    (on ST, those values might be rounded to the next multiple of 4 cycles)

    When the ACIA's IRQ signal goes low, the resulting bit in the MFP is visible to the CPU only 4 cycles later.
    From the hardware point of view, the ACIA's irq signal is immediately propagated to the MFP,
    but the MFP will then add a 4 cycle delay before generating a 68000 interrupt.

*/

/*-----------------------------------------------------------------------*/


#include "main.h"
#include "log.h"
#include "memorySnapShot.h"
#include "configuration.h"
#include "acia.h"
#include "m68000.h"
#include "cycInt.h"
#include "ioMem.h"
#include "clocks_timings.h"
#include "mfp.h"
#include "screen.h"
#include "video.h"


#define	ACIA_SR_BIT_RDRF			0x01		/* Receive Data Register Full */
#define	ACIA_SR_BIT_TDRE			0x02		/* Transmit Data Register Empty */
#define	ACIA_SR_BIT_DCD				0x04		/* Data Carrier Detect */
#define	ACIA_SR_BIT_CTS				0x08		/* Clear To Send */
#define	ACIA_SR_BIT_FE				0x10		/* Framing Error */
#define	ACIA_SR_BIT_OVRN			0x20		/* Receiver Overrun */
#define	ACIA_SR_BIT_PE				0x40		/* Parity Error */
#define	ACIA_SR_BIT_IRQ				0x80		/* IRQ */

#define	ACIA_CR_COUNTER_DIVIDE( CR )		( CR & 0x03 )		/* CR1 + CR0 : 0x03 causes a master reset */
#define	ACIA_CR_WORD_SELECT( CR )		( ( CR >> 2 ) & 0x07 )	/* CR4 + CR3 + CR2 : size, parity, stop bits */
#define	ACIA_CR_TRANSMITTER_CONTROL( CR )	( ( CR >> 5 ) & 0x03 )	/* CR6 + CR5 : RTS + IRQ on send */
#define	ACIA_CR_RECEIVE_INTERRUPT_ENABLE( CR )	( ( CR >> 7 ) & 0x01 )	/* CR7 : Receive interrupt enable */


static const int ACIA_Counter_Divide[3] = { 1 , 16 , 64 };	/* Used to divide txclock/rxclock to get the correct baud rate */


/* Data size, parity and stop bits used for the transfer depending on CR_WORD_SELECT */
enum
{
	ACIA_PARITY_NONE ,
	ACIA_PARITY_EVEN ,
	ACIA_PARITY_ODD
};


static struct {
	int	DataBits;						/* 7 or 8 */
	int	Parity;							/* EVEN or ODD or NONE */
	int	StopBits;						/* 1 or 2 */
} ACIA_Serial_Params [ 8 ] = {
  { 7 , ACIA_PARITY_EVEN , 2 },
  { 7 , ACIA_PARITY_ODD ,  2 },
  { 7 , ACIA_PARITY_EVEN , 1 },
  { 7 , ACIA_PARITY_ODD ,  1 },
  { 8 , ACIA_PARITY_NONE , 2 },
  { 8 , ACIA_PARITY_NONE , 1 },
  { 8 , ACIA_PARITY_EVEN , 1 },
  { 8 , ACIA_PARITY_ODD ,  1 }
};



/* Possible states when handling TX/RX interrupts */
enum
{
	ACIA_STATE_IDLE = 0,
	ACIA_STATE_DATA_BIT,
	ACIA_STATE_PARITY_BIT,
	ACIA_STATE_STOP_BIT
};


ACIA_STRUCT		ACIA_Array[ ACIA_MAX_NB ];
ACIA_STRUCT		*pACIA_IKBD;
ACIA_STRUCT		*pACIA_MIDI;




/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static void		ACIA_Init_Pointers ( ACIA_STRUCT *pAllACIA );

static void		ACIA_Set_Line_IRQ_MFP ( int bit );
static Uint8 		ACIA_Get_Line_CTS_Dummy ( void );
static Uint8 		ACIA_Get_Line_DCD_Dummy ( void );
static void		ACIA_Set_Line_RTS_Dummy ( int bit );

static void		ACIA_Set_Timers_IKBD ( void *pACIA );
static void		ACIA_Start_InterruptHandler_IKBD ( ACIA_STRUCT *pACIA , int InternalCycleOffset );

static Uint8		ACIA_MasterReset ( ACIA_STRUCT *pACIA , Uint8 CR );

static void		ACIA_UpdateIRQ ( ACIA_STRUCT *pACIA );

static Uint8		ACIA_Read_SR ( ACIA_STRUCT *pACIA );
static void		ACIA_Write_CR ( ACIA_STRUCT *pACIA , Uint8 CR );
static Uint8		ACIA_Read_RDR ( ACIA_STRUCT *pACIA );
static void		ACIA_Write_TDR ( ACIA_STRUCT *pACIA , Uint8 TDR );

static void		ACIA_Prepare_TX ( ACIA_STRUCT *pACIA );
static void		ACIA_Prepare_RX ( ACIA_STRUCT *pACIA );
static void		ACIA_Clock_TX ( ACIA_STRUCT *pACIA );
static void		ACIA_Clock_RX ( ACIA_STRUCT *pACIA );





/*-----------------------------------------------------------------------*/
/**
 * Init the 2 ACIAs in an Atari ST.
 * Both ACIAs have a 500 MHZ TX/RX clock.
 * This is called only once, when the emulator starts.
 * NOTE : when testing EmuTos on real hardware, it seems the tx/rx is working
 * after a cold reset (ST switched on), even if Clock_Divider was not initialized yet.
 * The default behaviour is not described in the ACIA's ref doc, but bits
 * seem to be transmitted (maybe with errors ?). So we default
 * to 9600 bauds to avoid a lock if a program uses tx/rx after a reset.
 */
void	ACIA_Init ( ACIA_STRUCT *pAllACIA , Uint32 TX_Clock , Uint32 RX_Clock )
{
	int	i;


	LOG_TRACE ( TRACE_ACIA, "acia init tx_clock=%d rx_clock=%d\n" , TX_Clock , RX_Clock );

	for ( i=0 ; i<ACIA_MAX_NB ; i++ )
	{
		memset ( (void *)&(pAllACIA[ i ]) , 0 , sizeof ( ACIA_STRUCT) );

		pAllACIA[ i ].TX_Clock = TX_Clock;
		pAllACIA[ i ].RX_Clock = RX_Clock;
		pAllACIA[ i ].Clock_Divider = 0;		/* Divider not initialized yet */
		pAllACIA[ i ].FirstMasterReset = 1;
	}

	/* Set the default common callback functions + other pointers */
	ACIA_Init_Pointers ( pAllACIA );
}



/*-----------------------------------------------------------------------*/
/**
 * Init some functions/memory pointers for each ACIA.
 * This is called at Init and when restoring a memory snapshot.
 */
static void	ACIA_Init_Pointers ( ACIA_STRUCT *pAllACIA )
{
	int	i;


	for ( i=0 ; i<ACIA_MAX_NB ; i++ )
	{
		/* Set the default common callback functions */
		pAllACIA[ i ].Set_Line_IRQ = ACIA_Set_Line_IRQ_MFP;
		pAllACIA[ i ].Get_Line_CTS = ACIA_Get_Line_CTS_Dummy;
		pAllACIA[ i ].Get_Line_DCD = ACIA_Get_Line_DCD_Dummy;
		pAllACIA[ i ].Set_Line_RTS = ACIA_Set_Line_RTS_Dummy;
	}

	strcpy ( pAllACIA[ 0 ].ACIA_Name , "ikbd" );
	strcpy ( pAllACIA[ 1 ].ACIA_Name , "midi" );

	pACIA_IKBD = &(pAllACIA[ 0 ]);
	pACIA_MIDI = &(pAllACIA[ 1 ]);

	pACIA_IKBD->Set_Timers = ACIA_Set_Timers_IKBD;
//	pACIA_MIDI->Set_Timers = ACIA_Set_Timers_MIDI;			/* Not used for now */
}




/*-----------------------------------------------------------------------*/
/**
 * There's no real hardware reset on the ACIA, but as the Reset_ST()
 * functions turns off all internal interrupts, we must restart the ACIA's
 * interrupt after a reset.
 */
void	ACIA_Reset ( ACIA_STRUCT *pAllACIA )
{
	int	i;


	LOG_TRACE ( TRACE_ACIA, "acia reset\n" );

	for ( i=0 ; i<ACIA_MAX_NB ; i++ )
	{
		if ( pAllACIA[ i ].Clock_Divider > 0 )				/* Divider already initialized */
			pAllACIA[ i ].Set_Timers ( &(pAllACIA[ i ]) );		/* Restart the timer */
	}
}



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void	ACIA_MemorySnapShot_Capture ( bool bSave )
{
	MemorySnapShot_Store(&ACIA_Array, sizeof(ACIA_Array));

	if ( !bSave )						/* If restoring */
		ACIA_Init_Pointers ( ACIA_Array );		/* Restore pointers */
}




/*-----------------------------------------------------------------------*/
/**
 * Set or reset the ACIA's IRQ signal.
 * IRQ signal is inverted (0/low sets irq, 1/high resets irq)
 * In the ST, the 2 ACIA's IRQ pins are connected to the same MFP input,
 * so they share the same IRQ bit in GPIP4.
 */
static void	ACIA_Set_Line_IRQ_MFP ( int bit )
{
	LOG_TRACE ( TRACE_ACIA, "acia set irq line val=%d VBL=%d HBL=%d\n" , bit , nVBLs , nHBL );

	if ( bit == 0 )
	{
		/* There's a small delay on a real ST between the point in time
		* the irq bit is set and the MFP interrupt is triggered - for example
		* the "V8 music system" demo depends on this behaviour.
		* This 4 cycle delay is handled in mfp.c */
		MFP_GPIP_Set_Line_Input ( MFP_GPIP_LINE_ACIA , MFP_GPIP_STATE_LOW );
	}
	else
	{
		MFP_GPIP_Set_Line_Input ( MFP_GPIP_LINE_ACIA , MFP_GPIP_STATE_HIGH );
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Read the Clear To Send (CTS) pin
 * When CTS is high, TDRE should always be set to 0
 * Note : this is not connected on an ST, so we always return 0.
 */
static Uint8 	ACIA_Get_Line_CTS_Dummy ( void )
{
	Uint8		bit;

	bit = 0;
	LOG_TRACE ( TRACE_ACIA, "acia get cts=%d VBL=%d HBL=%d\n" , bit , nVBLs , nHBL );
	return bit;
}

/*-----------------------------------------------------------------------*/
/**
 * Read the Data Carrier Detect (DCD) pin
 * Note : this is not connected on an ST, so we always return 0.
 */
static Uint8 	ACIA_Get_Line_DCD_Dummy ( void )
{
	Uint8		bit;

	bit = 0;
	LOG_TRACE ( TRACE_ACIA, "acia get dcd=%d VBL=%d HBL=%d\n" , bit , nVBLs , nHBL );
	return bit;
}

/*-----------------------------------------------------------------------*/
/**
 * Set the Request To Send (RTS) pin.
 * Note : this is not connected on an ST, so we ignore it.
 */
static void	ACIA_Set_Line_RTS_Dummy ( int bit )
{
	LOG_TRACE ( TRACE_ACIA, "acia set rts val=%d VBL=%d HBL=%d\n" , bit , nVBLs , nHBL );
}




/*-----------------------------------------------------------------------*/
/**
 * Set the required timers to handle RX / TX, depending on the CR_DIVIDE
 * value.
 * When CR is changed with a new CR_DIVIDE value, we restart the timers.
 */
static void	ACIA_Set_Timers_IKBD ( void *pACIA )
{
	ACIA_Start_InterruptHandler_IKBD ( (ACIA_STRUCT *)pACIA , 0 );
}




/*-----------------------------------------------------------------------*/
/**
 * Set a timer to handle the RX / TX bits at the expected baud rate.
 * NOTE : on ST, TX_Clock and RX_Clock are the same, so the timer's freq will be
 * TX_Clock / Divider and we only need one timer interrupt to handle both RX and TX.
 * This freq should be converted to CPU_CYCLE : 1 ACIA cycle = 16 CPU cycles
 * (with cpu running at 8 MHz)
 * InternalCycleOffset allows to compensate for a != 0 value in PendingInterruptCount
 * to keep a constant baud rate.
 * TODO : we use a fixed 8 MHz clock and nCpuFreqShift to convert cycles for our
 * internal timers in cycInt.c. This should be replaced some days by using
 * MachineClocks.CPU_Freq and not using nCpuFreqShift anymore.
 */
static void	ACIA_Start_InterruptHandler_IKBD ( ACIA_STRUCT *pACIA , int InternalCycleOffset )
{
	int		Cycles;


//	Cycles = MachineClocks.CPU_Freq / pACIA->TX_Clock;		/* Convert ACIA cycles in CPU cycles */
	Cycles = 8021247 / pACIA->TX_Clock;				/* Convert ACIA cycles in CPU cycles, for a 8 MHz STF reference */
	Cycles *= pACIA->Clock_Divider;
	Cycles <<= nCpuFreqShift;					/* Compensate for x2 or x4 cpu speed */

	LOG_TRACE ( TRACE_ACIA, "acia %s start timer divider=%d cpu_cycles=%d VBL=%d HBL=%d\n" , pACIA->ACIA_Name ,
		pACIA->Clock_Divider , Cycles , nVBLs , nHBL );

	CycInt_AddRelativeInterruptWithOffset ( Cycles, INT_CPU_CYCLE, INTERRUPT_ACIA_IKBD , InternalCycleOffset );
}




/*-----------------------------------------------------------------------*/
/**
 * Interrupt called each time a new bit must be sent / received with the IKBD.
 * This interrupt will be called at freq ( 500 MHz / ACIA_CR_COUNTER_DIVIDE )
 * On ST, RX_Clock = TX_Clock = 500 MHz.
 * We continuously restart the interrupt, taking into account PendingCyclesOver.
 */
void	ACIA_InterruptHandler_IKBD ( void )
{
	int	PendingCyclesOver;


	/* Number of internal cycles we went over for this timer ( <= 0 ) */
	/* Used to restart the next timer and keep a constant baud rate */
	PendingCyclesOver = -PendingInterruptCount;			/* >= 0 */

	LOG_TRACE ( TRACE_ACIA, "acia ikbd interrupt handler pending_cyc=%d VBL=%d HBL=%d\n" , PendingCyclesOver , nVBLs , nHBL );

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	ACIA_Clock_TX ( pACIA_IKBD );
	ACIA_Clock_RX ( pACIA_IKBD );

	ACIA_Start_InterruptHandler_IKBD ( pACIA_IKBD , -PendingCyclesOver );	/* Compensate for a != 0 value of PendingCyclesOver */
}




/*-----------------------------------------------------------------------*/
/**
 * Interrupt called each time a new bit must be sent / received with the MIDI.
 * This interrupt will be called at freq ( 500 MHz / ACIA_CR_COUNTER_DIVIDE )
 * On ST, RX_Clock = TX_Clock = 500 MHz.
 */
void	ACIA_InterruptHandler_MIDI ( void )
{
	ACIA_Clock_TX ( pACIA_MIDI );
	ACIA_Clock_RX ( pACIA_MIDI );
}




/*-----------------------------------------------------------------------*/
/**
 * - For each access to an ACIA register, a 6 cycles delay is added to the
 *   normal 68000 timing for the current CPU instruction. If the instruction
 *   accesses several registers at once, the delays are cumulated.
 * - An additional delay will also be added to ensure the 68000 clock and
 *   the E clock are synchronised ; this delay can add between 0 and 8 cycles
 *   to reach the next multiple of 10 cycles. This delay is added only once
 *   per CPU instruction.
 * These delays are measured for an 8 MHz 68000 CPU.
 */
void	ACIA_AddWaitCycles ( void )
{
	int	cycles;

	/* Add a default of 6 cycles for each access */
	cycles = 6;

	/* Wait for E clock only if this is the first ACIA access for this instruction */
	/* (NOTE : in UAE, movep behaves like several bytes access with different IoAccessBaseAddress, */
	/* so only the first movep's access should wait for E Clock) */
	if ( ( ( IoAccessInstrCount == 0 ) && ( IoAccessBaseAddress == IoAccessCurrentAddress ) )
	  || ( IoAccessInstrCount == 1 ) )				/* First access of a movep */
		cycles += M68000_WaitEClock ();
	
	M68000_WaitState ( cycles );
}



/*-----------------------------------------------------------------------*/
/**
 * Return SR for the IKBD's ACIA (0xfffc00)
 */
void	ACIA_IKBD_Read_SR ( void )
{
	ACIA_AddWaitCycles ();						/* Additional cycles when accessing the ACIA */

	IoMem[0xfffc00] = ACIA_Read_SR ( pACIA_IKBD );

	if (LOG_TRACE_LEVEL(TRACE_ACIA))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("acia %s read fffc00 sr=0x%02x video_cyc=%d %d@%d pc=%x instr_cycle %d\n", pACIA_IKBD->ACIA_Name ,
		                IoMem[0xfffc00], FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Return RDR for the IKBD's ACIA (0xfffc02) : receive a byte from the IKBD
 */
void	ACIA_IKBD_Read_RDR ( void )
{
	ACIA_AddWaitCycles ();						/* Additional cycles when accessing the ACIA */

	IoMem[0xfffc02] = ACIA_Read_RDR ( pACIA_IKBD );

	if (LOG_TRACE_LEVEL(TRACE_IKBD_ACIA))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("acia %s read fffc02 rdr=0x%02x video_cyc=%d %d@%d pc=%x instr_cycle %d\n", pACIA_IKBD->ACIA_Name ,
				IoMem[0xfffc02], FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Write to CR in the IKBD's ACIA (0xfffc00)
 */
void	ACIA_IKBD_Write_CR ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	ACIA_AddWaitCycles ();						/* Additional cycles when accessing the ACIA */

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
	LOG_TRACE(TRACE_IKBD_ACIA, "acia %s write fffc00 cr=0x%02x video_cyc=%d %d@%d pc=%x instr_cycle %d\n", pACIA_IKBD->ACIA_Name ,
				IoMem[0xfffc00], FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);

	ACIA_Write_CR ( pACIA_IKBD , IoMem[0xfffc00] );
}




/*-----------------------------------------------------------------------*/
/**
 * Write to TDR in the IKBD's ACIA (0xfffc02) : send a byte to the IKBD
 */
void	ACIA_IKBD_Write_TDR ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	ACIA_AddWaitCycles ();						/* Additional cycles when accessing the ACIA */

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
	LOG_TRACE(TRACE_IKBD_ACIA, "acia %s write fffc02 tdr=0x%02x video_cyc=%d %d@%d pc=%x instr_cycle %d\n", pACIA_IKBD->ACIA_Name ,
				IoMem[0xfffc02], FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);

	ACIA_Write_TDR ( pACIA_IKBD , IoMem[0xfffc02] );
}




/*----------------------------------------------------------------------*/
/* The part below is the real core of the 6850's emulation.		*/
/*									*/
/* This core is not correlated to any specific machine. All the specific*/
/* code between the 6850 and the rest of Hatari is called through some	*/
/* callback functions (see above).					*/
/*----------------------------------------------------------------------*/



/*-----------------------------------------------------------------------*/
/**
 * Reset an ACIA.
 * There's no RESET pin on the MC6850, so the only way to reset the ACIA
 * is to set bit 0 an 1 to 0x03 in the CR to force a master reset.
 * This will clear SR (except CTS and DCD) and halt/initialize both the
 * receiver and transmitter.
 * This also returns the new state of the RTS bit, that must be updated
 * in ACIA_Write_CR.
 */
static Uint8	ACIA_MasterReset ( ACIA_STRUCT *pACIA , Uint8 CR )
{
	Uint8		dcd_bit;
	Uint8		cts_bit;
	Uint8		rts_bit;


	LOG_TRACE ( TRACE_ACIA, "acia %s master reset VBL=%d HBL=%d\n" , pACIA->ACIA_Name , nVBLs , nHBL );
	
	dcd_bit = pACIA->Get_Line_DCD ();
	cts_bit = pACIA->Get_Line_CTS ();

	pACIA->SR = ACIA_SR_BIT_TDRE | ( dcd_bit << 2 ) | ( cts_bit << 3 );

	pACIA->TX_State = ACIA_STATE_IDLE;
	pACIA->TSR = 0;
	pACIA->TX_Size = 0;
	pACIA->TX_SendBrk = 0;

	pACIA->RX_State = ACIA_STATE_IDLE;
	pACIA->RSR = 0;
	pACIA->RX_Size = 0;
	pACIA->RX_Overrun = 0;

	/* On Master Reset, IRQ line is high */
	/* If it's the 1st reset, RTS should be high, else RTS depends on CR bit 5 and 6 */
	pACIA->Set_Line_IRQ ( 1 );					/* IRQ line goes high */
	if ( pACIA->FirstMasterReset == 1 )
	{
		pACIA->FirstMasterReset = 0;
		rts_bit = 1;						/* RTS line goes high */
	}
	else
		rts_bit = ( ACIA_CR_TRANSMITTER_CONTROL ( CR ) == 0x02 ) ? 1 : 0;

	return rts_bit;
}




/*-----------------------------------------------------------------------*/
/**
 * Check if the IRQ must be changed in SR.
 * When there's a change, we must change the IRQ line too.
 */
static void	ACIA_UpdateIRQ ( ACIA_STRUCT *pACIA )
{
	Uint8		irq_bit_new;

	irq_bit_new = 0;

	if ( ACIA_CR_RECEIVE_INTERRUPT_ENABLE ( pACIA->CR ) 		/* Check for RX causes of interrupt */
	  && ( ( pACIA->SR & ( ACIA_SR_BIT_RDRF | ACIA_SR_BIT_DCD ) )
	    || ( pACIA->RX_Overrun ) ) )
	  irq_bit_new = ACIA_SR_BIT_IRQ;
//fprintf(stderr , "acia irq %x %x %x %d\n" , pACIA->CR , pACIA->SR , pACIA->RX_Overrun , irq_bit_new);

	if ( pACIA->TX_EnableInt					/* Check for TX causes of interrupt */
	  && ( pACIA->SR & ACIA_SR_BIT_TDRE )
	  && ( ( pACIA->SR & ACIA_SR_BIT_CTS ) == 0 ) )
	  irq_bit_new = ACIA_SR_BIT_IRQ;
	
	/* Update SR and IRQ line if a change happened */
	if ( ( pACIA->SR & ACIA_SR_BIT_IRQ ) != irq_bit_new )
	{
		LOG_TRACE ( TRACE_ACIA, "acia %s update irq irq_new=%d VBL=%d HBL=%d\n" , pACIA->ACIA_Name , irq_bit_new?1:0 , nVBLs , nHBL );

		if ( irq_bit_new )
		{
			pACIA->SR |= ACIA_SR_BIT_IRQ;			/* Set IRQ bit */
			pACIA->Set_Line_IRQ ( 0 );			/* IRQ line goes low */
		}
		else
		{
			pACIA->SR &= ~ACIA_SR_BIT_IRQ;			/* Clear IRQ bit */
			pACIA->Set_Line_IRQ ( 1 );			/* IRQ line goes high */
		}
	}
}



/*-----------------------------------------------------------------------*/
/**
 * Read SR.
 * Also update CTS ; when CTS is high, TDRE should always be masked to 0.
 */
static Uint8	ACIA_Read_SR ( ACIA_STRUCT *pACIA )
{
	Uint8	SR;


	if ( pACIA->Get_Line_CTS() == 1 )
		pACIA->SR |= ACIA_SR_BIT_CTS;
	else
		pACIA->SR &= ~ACIA_SR_BIT_CTS;

	SR = pACIA->SR;
	pACIA->SR_Read = 1;						/* Used in ACIA_Read_RDR to clear Overrun and DCD IRQ */

	if ( SR & ACIA_SR_BIT_CTS )
		SR &= ~ACIA_SR_BIT_TDRE;				/* Inhibit TDRE when CTS is set */

	LOG_TRACE ( TRACE_ACIA, "acia %s read sr data=0x%02x VBL=%d HBL=%d\n" , pACIA->ACIA_Name , SR , nVBLs , nHBL );

	return SR;
}




/*-----------------------------------------------------------------------*/
/**
 * Write to CR.
 */
static void	ACIA_Write_CR ( ACIA_STRUCT *pACIA , Uint8 CR )
{
	int	Divide;
	int	Force_rts_bit;
	Uint8	rts_bit=0;

	LOG_TRACE ( TRACE_ACIA, "acia %s write cr data=0x%02x VBL=%d HBL=%d\n" , pACIA->ACIA_Name , CR , nVBLs , nHBL );

	/* Bit 0 and 1 : Counter Divide */
	Divide = ACIA_CR_COUNTER_DIVIDE ( CR );
	if ( Divide == 0x03 )
	{
		Force_rts_bit = ACIA_MasterReset ( pACIA , CR );	/* Special behaviour for RTS after a master reset */
	}
	else
	{
		if ( ACIA_CR_COUNTER_DIVIDE ( CR ) != ACIA_CR_COUNTER_DIVIDE ( pACIA->CR ) )
		{
			pACIA->Clock_Divider = ACIA_Counter_Divide[ Divide ];
			pACIA->Set_Timers ( pACIA );			/* Set a timer at the baud rate computed from Clock_Divider */
		}
		Force_rts_bit = -1;					/* Don't force RTS bit, use bit 5/6 in CR */
	}

	/* Bits 2, 3 and 4 : word select */
	/* Don't do anything here, see ACIA_Prepare_TX and ACIA_Prepare_RX */

	/* Bits 5 and 6 : transmitter control */
	pACIA->TX_EnableInt = 0;
	pACIA->TX_SendBrk = 0;
	switch ( ACIA_CR_TRANSMITTER_CONTROL ( CR ) )
	{
	  case 0x00 :
		rts_bit = 0;
		break;
	  case 0x01 :
		rts_bit = 0;
		pACIA->TX_EnableInt = 1;
		break;
	  case 0x02 :
		rts_bit = 1;
		break;
	  case 0x03 :
		rts_bit = 0;
		pACIA->TX_SendBrk = 1;					/* We will send break bit until CR is changed */
		break;
	}

	if ( Force_rts_bit >= 0 )
		rts_bit = Force_rts_bit;				/* Use the value from ACIA_MasterReset */
	pACIA->Set_Line_RTS ( rts_bit );

	/* Bits 7 : receive interrupt enable, see ACIA_UpdateIRQ */

	pACIA->CR = CR;

	ACIA_UpdateIRQ ( pACIA );
}




/*-----------------------------------------------------------------------*/
/**
 * Read RDR. This will clear RDRF and PE.
 * - OVRN / DCD bits are cleared if SR was read before reading RDR.
 * - OVRN bit is set only when reading RDR, not when the actual overrun happened
 *   during ACIA_Clock_RX.
 * - IRQ bit should be updated depending on the new values of BIT_RDRF,
 *   BIT_DCD and BIT_OVRN.
 */
static Uint8	ACIA_Read_RDR ( ACIA_STRUCT *pACIA )
{
	pACIA->SR &= ~( ACIA_SR_BIT_RDRF | ACIA_SR_BIT_PE );

	/* If we read RDR after reading SR, we clear OVRN / DCD bits */
	if ( pACIA->SR_Read == 1 )
	{
		pACIA->SR_Read = 0;
		pACIA->SR &= ~( ACIA_SR_BIT_DCD | ACIA_SR_BIT_OVRN );
		if ( pACIA->Get_Line_DCD () == 1 )
			pACIA->SR |= ACIA_SR_BIT_DCD;
	}

	if ( pACIA->RX_Overrun )
	{  
		pACIA->SR |= ACIA_SR_BIT_OVRN;
		pACIA->RX_Overrun = 0;
	}

	ACIA_UpdateIRQ ( pACIA );

	LOG_TRACE ( TRACE_ACIA, "acia %s read rdr data=0x%02x new sr=0x%02x overrun=%s VBL=%d HBL=%d\n" , pACIA->ACIA_Name , pACIA->RDR ,
		pACIA->SR , ( pACIA->SR & ACIA_SR_BIT_OVRN ) ? "yes" : "no" , nVBLs , nHBL );

	return pACIA->RDR;
}




/*-----------------------------------------------------------------------*/
/**
 * Write to TDR.
 * If the TX process is idle, we should not prepare a new transfer
 * immediately, to ensure that BIT_TDRE remains clear until the next bit
 * is sent (BIT_TDRE will be set again in ACIA_Clock_TX).
 */
static void	ACIA_Write_TDR ( ACIA_STRUCT *pACIA , Uint8 TDR )
{
	LOG_TRACE ( TRACE_ACIA, "acia %s write tdr data=0x%02x overwrite=%s tx_state=%d VBL=%d HBL=%d\n" , pACIA->ACIA_Name , TDR ,
		( pACIA->SR & ACIA_SR_BIT_TDRE ) ? "no" : "yes" , pACIA->TX_State , nVBLs , nHBL );

	pACIA->TDR = TDR;
	pACIA->SR &= ~ACIA_SR_BIT_TDRE;					/* TDR is not empty anymore */

	ACIA_UpdateIRQ ( pACIA );
}




/*-----------------------------------------------------------------------*/
/**
 * Prepare a new transfer. Copy TDR to TSR and initialize parity, data size
 * and stop bits.
 * Transfer will then start at the next call of ACIA_Clock_TX
 */
static void	ACIA_Prepare_TX ( ACIA_STRUCT *pACIA )
{
	pACIA->TSR = pACIA->TDR;
	pACIA->TX_Parity = 0;
	pACIA->TX_Size = ACIA_Serial_Params[ ACIA_CR_WORD_SELECT ( pACIA->CR ) ].DataBits;
	pACIA->TX_StopBits = ACIA_Serial_Params[ ACIA_CR_WORD_SELECT ( pACIA->CR ) ].StopBits;

	pACIA->SR |= ACIA_SR_BIT_TDRE;					/* TDR was copied to TSR. TDR is now empty */

	LOG_TRACE ( TRACE_ACIA, "acia %s prepare tx tsr=0x%02x size=%d stop=%d VBL=%d HBL=%d\n" , pACIA->ACIA_Name , pACIA->TSR ,
		pACIA->TX_Size , pACIA->TX_StopBits , nVBLs , nHBL );
}




/*-----------------------------------------------------------------------*/
/**
 * Prepare a new reception. Initialize parity, data size and stop bits.
 */
static void	ACIA_Prepare_RX ( ACIA_STRUCT *pACIA )
{
	pACIA->RSR = 0;
	pACIA->RX_Parity = 0;
	pACIA->RX_Size = ACIA_Serial_Params[ ACIA_CR_WORD_SELECT ( pACIA->CR ) ].DataBits;
	pACIA->RX_StopBits = ACIA_Serial_Params[ ACIA_CR_WORD_SELECT ( pACIA->CR ) ].StopBits;

	LOG_TRACE ( TRACE_ACIA, "acia %s prepare rx size=%d stop=%d VBL=%d HBL=%d\n" , pACIA->ACIA_Name ,
		pACIA->RX_Size , pACIA->RX_StopBits , nVBLs , nHBL );
}




/*-----------------------------------------------------------------------*/
/**
 * Write a new bit on the TX line each time the TX clock expires.
 * This will send TDR over the serial line, using TSR, with additional
 * parity and start/stop bits.
 * We send bit 0 of TSR, then TSR is shifted to the right.
 */
static void	ACIA_Clock_TX ( ACIA_STRUCT *pACIA )
{
	int	StateNext;
	Uint8	tx_bit;


	LOG_TRACE ( TRACE_ACIA, "acia %s clock_tx tx_state=%d VBL=%d HBL=%d\n" , pACIA->ACIA_Name , pACIA->TX_State , nVBLs , nHBL );

	StateNext = -1;
	switch ( pACIA->TX_State )
	{
	  case ACIA_STATE_IDLE :
		if ( pACIA->TX_SendBrk )
		{
			pACIA->Set_Line_TX ( 0 );			/* Send 1 break bit */
			break;
		}

		/* If TDR is not empty when we are in idle state, */
		/* this means we have a new byte to send */
		if ( ( pACIA->SR & ACIA_SR_BIT_TDRE ) == 0 )
			ACIA_Prepare_TX ( pACIA );

		if ( pACIA->TX_Size == 0 )				/* TSR is empty */
			pACIA->Set_Line_TX ( 1 );			/* Send stop bits when idle */

		else							/* TSR has some new bits to transfer */
		{
			pACIA->Set_Line_TX ( 0 );			/* Send 1 start bit */
			StateNext = ACIA_STATE_DATA_BIT;
		}
		break;

	  case ACIA_STATE_DATA_BIT :
		tx_bit = pACIA->TSR & 1;				/* New bit to send */
		pACIA->Set_Line_TX ( tx_bit );
		pACIA->TX_Parity ^= tx_bit;
		pACIA->TSR >>= 1;
		pACIA->TX_Size--;

		if ( pACIA->TX_Size == 0 )
		{
			if ( ACIA_Serial_Params[ ACIA_CR_WORD_SELECT ( pACIA->CR ) ].Parity != ACIA_PARITY_NONE )
				StateNext = ACIA_STATE_PARITY_BIT;
			else
				StateNext = ACIA_STATE_STOP_BIT;	/* No parity */
		}
		break;

	  case ACIA_STATE_PARITY_BIT :
		if ( ACIA_Serial_Params[ ACIA_CR_WORD_SELECT ( pACIA->CR ) ].Parity == ACIA_PARITY_EVEN )
			pACIA->Set_Line_TX ( pACIA->TX_Parity );
		else
			pACIA->Set_Line_TX ( ( ~pACIA->TX_Parity ) & 1 );

		StateNext = ACIA_STATE_STOP_BIT;
		break;

	  case ACIA_STATE_STOP_BIT :
		pACIA->Set_Line_TX ( 1 );				/* Send 1 stop bit */
		pACIA->TX_StopBits--;

		if ( pACIA->TX_StopBits == 0 )				/* All stop bits were sent : transfer is complete */
		{
			StateNext = ACIA_STATE_IDLE;			/* Go to idle state to see if a new TDR need to be sent */
		}
		break;
	}

	ACIA_UpdateIRQ ( pACIA );

	if ( StateNext >= 0 )
		pACIA->TX_State = StateNext;				/* Go to a new state */
}




/*-----------------------------------------------------------------------*/
/**
 * Handle a new bit on the RX line each time the RX clock expires.
 * This will fill RDR with bits received from the serial line, using RSR.
 * Incoming bits are stored in bit 7 of RSR, then RSR is shifted to the right.
 */
static void	ACIA_Clock_RX ( ACIA_STRUCT *pACIA )
{
	int	StateNext;
	Uint8	rx_bit;


	rx_bit = pACIA->Get_Line_RX();

	LOG_TRACE ( TRACE_ACIA, "acia %s clock_rx rx_state=%d bit=%d VBL=%d HBL=%d\n" , pACIA->ACIA_Name , pACIA->RX_State , rx_bit , nVBLs , nHBL );

	StateNext = -1;
	switch ( pACIA->RX_State )
	{
	  case ACIA_STATE_IDLE :
		if ( rx_bit == 0 )					/* Receive 1 start bit */
		{
			ACIA_Prepare_RX ( pACIA );
			StateNext = ACIA_STATE_DATA_BIT;
		}
		break;							/* If no start bit, we stay in idle state */

	  case ACIA_STATE_DATA_BIT :
		if ( rx_bit )
			pACIA->RSR |= 0x80;
		pACIA->RX_Parity ^= rx_bit;
		pACIA->RX_Size--;

		if ( pACIA->RX_Size > 0 )				/* All bits were not received yet */
		{
			pACIA->RSR >>= 1;
		}
		else
		{
// [NP] : MC6850 doc is not very clear "the overrun condition begins at the midpoint of the last bit
// of the second character received [...]". Is it the last bit of the data word, or the stop bit ?
// It makes more sense to check for overrun after the stop bit, when RSR should be copied to RDR,
// because RDR could be read between the last data bit and the stop bit, so RX_Overrun and
// ACIA_SR_BIT_OVRN would need to be cancelled.
// 			if ( pACIA->SR & ACIA_SR_BIT_RDRF )
// 			{
// 				LOG_TRACE ( TRACE_ACIA, "acia %s clock_rx overrun rsr=0x%02x VBL=%d HBL=%d\n" ,
// 					pACIA->ACIA_Name , pACIA->RSR , nVBLs , nHBL );
// 				pACIA->RX_Overrun = 1;			/* Bit in SR will be set when reading RDR */
// 			}
			if ( ACIA_Serial_Params[ ACIA_CR_WORD_SELECT ( pACIA->CR ) ].Parity != ACIA_PARITY_NONE )
				StateNext = ACIA_STATE_PARITY_BIT;
			else
				StateNext = ACIA_STATE_STOP_BIT;	/* No parity */
		}
		break;

	  case ACIA_STATE_PARITY_BIT :
		if ( ( ACIA_Serial_Params[ ACIA_CR_WORD_SELECT ( pACIA->CR ) ].Parity == ACIA_PARITY_EVEN )
		    && ( pACIA->RX_Parity != rx_bit ) )
			pACIA->SR |= ACIA_SR_BIT_PE;

		else if ( pACIA->RX_Parity == rx_bit )			/* Odd parity */
			pACIA->SR |= ACIA_SR_BIT_PE;

		if ( pACIA->SR & ACIA_SR_BIT_PE )
			LOG_TRACE ( TRACE_ACIA, "acia %s clock_rx parity error VBL=%d HBL=%d\n" , pACIA->ACIA_Name , nVBLs , nHBL );

		StateNext = ACIA_STATE_STOP_BIT;
		break;

	  case ACIA_STATE_STOP_BIT :
		if ( rx_bit == 1 )					/* Wait for 1 or 2 "1" stop bits */
		{
			pACIA->RX_StopBits--;
			if ( pACIA->RX_StopBits == 0 )			/* All stop bits were received : reception is complete */
			{
				pACIA->SR &= ~ACIA_SR_BIT_FE;
				
				if ( ( pACIA->SR & ACIA_SR_BIT_RDRF ) == 0 )
				{
					pACIA->RDR = pACIA->RSR;
					pACIA->SR |= ACIA_SR_BIT_RDRF;
					LOG_TRACE ( TRACE_ACIA, "acia %s clock_rx received rdr=0x%02x VBL=%d HBL=%d\n" ,
						pACIA->ACIA_Name , pACIA->RDR , nVBLs , nHBL );
				}
				else
				{
					LOG_TRACE ( TRACE_ACIA, "acia %s clock_rx overrun rsr=0x%02x unread rdr=0x%02x VBL=%d HBL=%d\n" ,
						pACIA->ACIA_Name , pACIA->RSR , pACIA->RDR , nVBLs , nHBL );
					pACIA->RX_Overrun = 1;		/* Bit in SR will be set when reading RDR */
				}
				StateNext = ACIA_STATE_IDLE;		/* Go to idle state and wait for start bit */
			}
		}
		else							/* Not a valid stop bit */
		{
			LOG_TRACE ( TRACE_ACIA, "acia %s clock_rx framing error VBL=%d HBL=%d\n" , pACIA->ACIA_Name , nVBLs , nHBL );

			/* According to the A6850 doc, RSR is copied to RDR in case of a framing error */
			/* (Should be the same for the MC6850 ?) */
			pACIA->SR |= ACIA_SR_BIT_FE;
			pACIA->RDR = pACIA->RSR;
			StateNext = ACIA_STATE_IDLE;			/* Go to idle state and wait for start bit */
		}
		break;
	}

	ACIA_UpdateIRQ ( pACIA );

	if ( StateNext >= 0 )
		pACIA->RX_State = StateNext;				/* Go to a new state */
}



