/*
  Hatari - acia.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_ACIA_H
#define HATARI_ACIA_H


typedef struct ACIA {
	/* MC6850 internal registers */
	uint8_t		CR;					/* Control Register */
	uint8_t		SR;					/* Status Register */
	uint8_t		TDR;					/* Transmit Data Register */
	uint8_t		RDR;					/* Receive Data Register */

	uint32_t	TX_Clock;				/* 500 MHz on ST */
	uint32_t	RX_Clock;				/* 500 MHz on ST */
	int		Clock_Divider;				/* 1, 16 or 64 */

	uint8_t		FirstMasterReset;			/* Set to 1 on first use, always 0 after 1st Master Reset */
	uint8_t		SR_Read;				/* Set to 1 when SR is read and reset to 0 when RDR is read */

	int		TX_State;
	uint8_t		TSR;					/* Transmit Shift Register */
	uint8_t		TX_Size;				/* How many data bits left to transmit in TSR (7/8 .. 0) */
	uint8_t		TX_Parity;				/* Current parity bit value for transmit */
	uint8_t		TX_StopBits;				/* How many stop bits left to transmit (1 or 2) */
	uint8_t		TX_EnableInt;				/* When TDRE goes from 0 to 1 :  0=disable interrupt, 1=enable interrupt */
	uint8_t		TX_SendBrk;				/* Send a break bit in idle state */

	int		RX_State;
	uint8_t		RSR;					/* Receive Shift Register */
	uint8_t		RX_Size;				/* How many bits left to receive in RSR (7/8 .. 0) */
	uint8_t		RX_Parity;				/* Current parity bit value for receive */
	uint8_t		RX_StopBits;				/* How many stop bits left to receive (1 or 2) */
	uint8_t		RX_Overrun;				/* Set to 1 if previous RDR was not read when RSR is full */ 

	uint8_t		IRQ_Line;				/* 0=IRQ   1=no IRQ */

	/* Callback functions */
	uint8_t		(*Get_Line_RX) ( void );		/* Input  : RX */
	void		(*Set_Line_TX) ( int val );		/* Output : TX */
	void		(*Set_Line_IRQ) ( struct ACIA *pACIA , int val ); /* Output : IRQ */
	uint8_t		(*Get_Line_IRQ) ( struct ACIA *pACIA );	/* Output : IRQ */

	void		(*Set_Timers) ( struct ACIA *pACIA );	/* Start timers to handle RX and TX bits at specified baud rate */
	
	uint8_t		(*Get_Line_CTS) ( void );		/* Input  : Clear To Send (not connected in ST) */
	uint8_t		(*Get_Line_DCD) ( void );		/* Input  : Data Carrier Detect (not connected in ST) */
	void		(*Set_Line_RTS) ( int val );		/* Output : Request To Send (not connected in ST) */

	/* Other variables */
	char		ACIA_Name[ 10 ];			/* IKBD or MIDI */

} ACIA_STRUCT;


#define		ACIA_MAX_NB		2			/* 2 ACIAs in the ST */

extern ACIA_STRUCT		ACIA_Array[ ACIA_MAX_NB ];
extern ACIA_STRUCT		*pACIA_IKBD;
extern ACIA_STRUCT		*pACIA_MIDI;



void	ACIA_Init ( ACIA_STRUCT *pAllACIA , uint32_t TX_Clock , uint32_t RX_Clock );
void	ACIA_Reset ( ACIA_STRUCT *pAllACIA );
void	ACIA_MemorySnapShot_Capture ( bool bSave );

void	ACIA_InterruptHandler_IKBD ( void );
void	ACIA_InterruptHandler_MIDI ( void );

void	ACIA_AddWaitCycles ( void );

void	ACIA_IKBD_Read_SR ( void );
void	ACIA_IKBD_Read_RDR ( void );
void	ACIA_IKBD_Write_CR ( void );
void	ACIA_IKBD_Write_TDR ( void );

void	ACIA_Info(FILE *fp, uint32_t dummy);


#endif /* ifndef HATARI_ACIA_H */
