/*
  Hatari - acia.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_ACIA_H
#define HATARI_ACIA_H


typedef struct {
	/* MC6850 internal registers */
	Uint8		CR;					/* Control Register */
	Uint8		SR;					/* Status Register */
	Uint8		TDR;					/* Transmit Data Register */
	Uint8		RDR;					/* Receive Data Register */

	Uint32		TX_Clock;				/* 500 MHz on ST */
	Uint32		RX_Clock;				/* 500 MHz on ST */
	int		Clock_Divider;				/* 1, 16 or 64 */

	Uint8		FirstMasterReset;			/* Set to 1 on first use, always 0 after 1st Master Reset */
	Uint8		SR_Read;				/* Set to 1 when SR is read and reset to 0 when RDR is read */

	int		TX_State;
	Uint8		TSR;					/* Transmit Shift Register */
	Uint8		TX_Size;				/* How many data bits left to transmit in TSR (7/8 .. 0) */
	Uint8		TX_Parity;				/* Current parity bit value for transmit */
	Uint8		TX_StopBits;				/* How many stop bits left to transmit (1 or 2) */
	Uint8		TX_EnableInt;				/* When TDRE goes from 0 to 1 :  0=disable interrupt, 1=enable interrupt */
	Uint8		TX_SendBrk;				/* Send a break bit in idle state */

	int		RX_State;
	Uint8		RSR;					/* Receive Shift Register */
	Uint8		RX_Size;				/* How many bits left to receive in RSR (7/8 .. 0) */
	Uint8		RX_Parity;				/* Current parity bit value for receive */
	Uint8		RX_StopBits;				/* How many stop bits left to receive (1 or 2) */
	Uint8		RX_Overrun;				/* Set to 1 if previous RDR was not read when RSR is full */ 

	/* Callback functions */
	Uint8		(*Get_Line_RX) ( void );		/* Input  : RX */
	void		(*Set_Line_TX) ( int val );		/* Output : TX */
	void		(*Set_Line_IRQ) ( int val );		/* Output : IRQ */

	void		(*Set_Timers) ( void *pACIA );		/* Start timers to handle RX and TX bits at specified baud rate */
	
	Uint8		(*Get_Line_CTS) ( void );		/* Input  : Clear To Send (not connected in ST) */
	Uint8		(*Get_Line_DCD) ( void );		/* Input  : Data Carrier Detect (not connected in ST) */
	void		(*Set_Line_RTS) ( int val );		/* Output : Request To Send (not connected in ST) */

	/* Other variables */
	char		ACIA_Name[ 10 ];			/* IKBD or MIDI */

} ACIA_STRUCT;


#define		ACIA_MAX_NB		2			/* 2 ACIAs in the ST */

extern ACIA_STRUCT		ACIA_Array[ ACIA_MAX_NB ];
extern ACIA_STRUCT		*pACIA_IKBD;
extern ACIA_STRUCT		*pACIA_MIDI;



void	ACIA_Init ( ACIA_STRUCT *pAllACIA , Uint32 TX_Clock , Uint32 RX_Clock );
void	ACIA_Reset ( ACIA_STRUCT *pAllACIA );
void	ACIA_MemorySnapShot_Capture ( bool bSave );

void	ACIA_InterruptHandler_IKBD ( void );
void	ACIA_InterruptHandler_MIDI ( void );

void	ACIA_AddWaitCycles ( void );

void	ACIA_IKBD_Read_SR ( void );
void	ACIA_IKBD_Read_RDR ( void );
void	ACIA_IKBD_Write_CR ( void );
void	ACIA_IKBD_Write_TDR ( void );



#endif /* ifndef HATARI_ACIA_H */
