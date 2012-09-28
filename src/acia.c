/*
  Hatari - acia.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  MC6850 ACIA emulation.
*/

const char ACIA_fileid[] = "Hatari acia.c : " __DATE__ " " __TIME__;

/*
  6850 ACIA (Asynchronous Communications Inferface Apdater)

  References :
   - MC6850 datasheet by Motorola (DS9493R4, 1985)
   - A6850 datasheet by Altera (A-DS-A6850-01, 1996) (nearly identical component)


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
      OVERRUN (a received character isn't properly read from the processor).
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
    The keyboard ACIA addresses are 0xfffc000 and 0xfffc02.
    The MIDI ACIA addresses are 0xfffc004 and 0xfffc06.
    Default parameters are :- 8-bit word, 1 stopbit, no parity, 7812.5 baud; 500KHz/64 (keyboard clock div)
    Default MIDI parameters are as above but :- 31250 baud; 500KHz/16 (MIDI clock div)

*/

/*-----------------------------------------------------------------------*/


#include "main.h"
#include "acia.h"
#include "log.h"
#include "memorySnapShot.h"



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
#define	ACIA_CR_RECEIVE_INTERRUPT_ENABLE( CR )	( ( CR >> 7 ) & 0x01 )	/* CR7 : Reveive interrupt enable */


typedef struct {
	/* MC6850 internal registers */
	Uint8		CR;					/* Control Register */
	Uint8		SR;					/* Status Register */
	Uint8		TDR;					/* Transmit Data Register */
	Uint8		RDR;					/* Receive Data Register */
	
	int		TX_State;
	Uint8		TDR_New;				/* 1 if a new byte was written in TDR and should be copied to TSR */
	Uint8		TSR;					/* Transmit Shift Register */
	Uint8		TX_Size;				/* How many data bits left to transmit in TSR (7/8 .. 0) */
	Uint8		TX_Parity;				/* Current parity bit value for transmit */
	Uint8		TX_StopBits;				/* How many stop bits left to transmit (1 or 2) */

	int		State_RX;
	Uint8		RSR;					/* Receive Shift Register */
	Uint8		RSR_Size;				/* How many bits left to receive in RSR (7/8 .. 0) */
	Uint8		RSR_Parity;				/* Current parity bit value for receive */
	Uint8		RSR_StopBits;				/* How many stop bits left to receive (1 or 2) */


	/* Other variables */
	char		*ACIA_Name;
} ACIA_STRUCT;



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




/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static void	ACIA_Prepare_TX ( ACIA_STRUCT *pACIA );
static void	ACIA_Clock_TX ( ACIA_STRUCT *pACIA );
static void	ACIA_Clock_RX ( ACIA_STRUCT *pACIA );





/*-----------------------------------------------------------------------*/
/**
 * Write to TDR. If the TX process is idle, we prepare a new transfer
 * immediatly, else the TDR value will be sent when current transfer is over.
 */
void	ACIA_Write_TDR ( ACIA_STRUCT *pACIA , Uint8 TDR )
{
	pACIA->TDR = TDR;
	pACIA->SR &= ~ACIA_SR_BIT_TDRE;					/* TDR is not empty */

	if ( pACIA->TX_State == ACIA_STATE_IDLE )			/* No transfer at the moment */
		ACIA_Prepare_TX ( pACIA );				/* Copy to TSR */
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
}




/*-----------------------------------------------------------------------*/
/**
 * Write a new bit on the TX line each time the TX clock expires.
 * This will send TDR over the serial line, using TSR, with additional
 * parity and start/stop bits.
 */
static void	ACIA_Clock_TX ( ACIA_STRUCT *pACIA )
{
	int	StateNext;
	Uint8	bit;

	
	StateNext = -1;
	switch ( pACIA->TX_State )
	{
	  case ACIA_STATE_IDLE :
		if ( pACIA->SR & ACIA_SR_BIT_TDRE == 0 )		/* If TDR is not empty when we reach idle state, this */
			ACIA_Prepare_TX ( pACIA );			/* means we already have a new byte to send immediatly */

		if ( pACIA->TX_Size == 0 )				/* TSR is empty */
			pACIA->Set_Line_TX ( 1 );			/* Send stop bits when idle */
		else							/* TSR has some new bits to transfer */
		{
			pACIA->Set_Line_TX ( 0 );			/* Send 1 start bit */
			StateNext = ACIA_STATE_DATA_BIT;
		}
		break;

	  case ACIA_STATE_DATA_BIT :
		bit = pACIA->TSR & 1;					/* New bit to send */
		pACIA->Set_Line_TX ( bit );
		pACIA->TX_Parity ^= bit;
		pACIA->TSR >> 1;
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
		if ( ACIA_Serial_Params[ ACIA_CR_WORD_SELECT ( pACIA->CR ) ].Parity != ACIA_PARITY_EVEN )
			pACIA->Set_Line_TX ( bit );
		else
			pACIA->Set_Line_TX ( ( ~bit ) & 1 );

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

	if ( StateNext >= 0 )
		pACIA->TX_State = StateNext;				/* Go to a new state */
}




/*-----------------------------------------------------------------------*/
/**
 * Interpret a new bit on the RX line each time the RX clock expires.
 * This will fill RDR with bits received from the serial line, using RSR.
 */
static void	ACIA_Clock_RX ( ACIA_STRUCT *pACIA )
{

}



