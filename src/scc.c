/*
 * scc.c - SCC 85C30 emulation code
 *
 * Adaptions to Hatari:
 *
 * Copyright 2023-2024 Nicolas Pomarède, major rewrite of most of the code
 *
 * Copyright 2018 Thomas Huth
 *
 * Original code taken from Aranym:
 *
 * Copyright (c) 2001-2004 Petr Stehlik of ARAnyM dev team
 *               2010 Jean Conter
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ARAnyM; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


/*
  The SCC is available in the Mega STE, the Falcon and the TT

  Depending on the machine, the SCC can have several clock sources, which allows
  to get closer to the requested baud rate by choosing the most appropriate base clock freq.

  Mega STE :
   SCC port A : 1 RS422 LAN port (MiniDIN, 8 pins) "Lan"
		or 1 internal 10 pins cable connected to RS232C serial port in VME slot (DP-9P, 9 pins) "Serial 2"
   SCC port B : 1 RS232C serial port (DP-9P, 9 pins) "Modem 2"
   - PCLK : connected to CLK8, 8021247 Hz for PAL
   - RTxCA and RTxCB : connected to PCLK4, dedicated OSC running at 3.672 MHz
   - TRxCA : connected to LCLK : SYNCI signal on pin 2 of the LAN connector or pin 6 of Serial port A
   - TRxCB : connected to BCLK, dedicated OSC running at 2.4576 MHz for the MFP's XTAL1

  TT :
   SCC port A : 1 RS422 LAN port (MiniDIN, 8 pins) "Lan"
		or 1 internal 10 pins cable connected to RS232C serial port in VME slot (DP-9P, 9 pins) "Serial 2"
   SCC port B : 1 RS232C serial port (DP-9P, 9 pins) "Modem 2"
   - PCLK : connected to CLK8, 8021247 Hz for PAL
   - RTxCA : connected to PCLK4, dedicated OSC running at 3.672 MHz
   - TRxCA : connected to LCLK : SYNCI signal on pin 2 of the LAN connector or pin 6 of Serial port A
   - RTxCB : connected to TCCLK on the TT-MFP (Timer C output)
   - TRxCB : connected to BCLK, dedicated OSC running at 2.4576 MHz for the 2 MFPs' XTAL1

  Falcon :
   SCC port A : 1 RS422 LAN port (MiniDIN, 8 pins) "Lan"
   SCC port B : 1 RS232C serial port B (DP-9P, 9 pins) "Modem"
   - PCLK : connected to CLK8, 8021247 Hz for PAL
   - RTxCA and RTxCB : connected to PCLK4, dedicated OSC running at 3.672 MHz
   - TRxCA : connected to SYNCA on the SCC
   - TRxCB : connected to BCLKA, dedicated OSC running at 2.4576 MHz for the MFP's XTAL1

  Note on other serial ports :
  MegaSTE and TT :
   - "Modem 1" is a RS232C serial port (DP-9P, 9 pins) connected to the main MFP
  TT :
   - "Serial 1" is a 3-wire serial port in VME slot (DP-9P, 9 pins) connected to the TT MFP
*/


#include "main.h"

#if HAVE_TERMIOS_H
# include <termios.h>
# include <unistd.h>
#endif
#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "configuration.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "scc.h"
#include "clocks_timings.h"
#include "m68000.h"
#include "cycles.h"
#include "cycInt.h"
#include "video.h"
#include "psg.h"

#ifndef O_NONBLOCK
# ifdef O_NDELAY
#  define O_NONBLOCK O_NDELAY
# else
#  define O_NONBLOCK 0
# endif
#endif


#define SCC_CLOCK_PCLK		MachineClocks.SCC_Freq		/* 8021247 Hz */
#define SCC_CLOCK_PCLK4		3672000				/* Dedicated OSC */
#define SCC_CLOCK_BCLK		MachineClocks.MFP_Timer_Freq	/* Connected to the MFP's XTAL clock 2.4576 MHz */

#define SCC_BAUDRATE_SOURCE_CLOCK_RTXC		0
#define SCC_BAUDRATE_SOURCE_CLOCK_TRXC		1
#define SCC_BAUDRATE_SOURCE_CLOCK_BRG		2
#define SCC_BAUDRATE_SOURCE_CLOCK_DPLL		3

#define SCC_BAUDRATE_SOURCE_CLOCK_PCLK		1
#define SCC_BAUDRATE_SOURCE_CLOCK_PCLK4		2
#define SCC_BAUDRATE_SOURCE_CLOCK_BCLK		3
#define SCC_BAUDRATE_SOURCE_CLOCK_TCCLK		4


/* CRC Reset codes 6-7 of WR0 */
#define SCC_WR0_COMMAND_CRC_NULL		0x00	/* Null command */
#define SCC_WR0_COMMAND_CRC_RESET_RX		0x01	/* Reset Receive CRC Checker */
#define SCC_WR0_COMMAND_CRC_RESET_TX		0x02	/* Reset Transmit CRC Generator */
#define SCC_WR0_COMMAND_CRC_RESET_TX_UNDERRUN	0x03	/* Reset Transmit Underrun/EOM Latch */

/* Commands for the bits 3-5 of WR0 */
#define SCC_WR0_COMMAND_NULL			0x00	/* Null Command */
#define SCC_WR0_COMMAND_POINT_HIGH		0x01	/* Point High */
#define SCC_WR0_COMMAND_RESET_EXT_STATUS_INT	0x02	/* Reset Ext/Status Int */
#define SCC_WR0_COMMAND_SEND_ABORT		0x03	/* Send Abort */
#define SCC_WR0_COMMAND_INT_NEXT_RX		0x04	/* Enable Interrupt on Next Rx Char */
#define SCC_WR0_COMMAND_RESET_TX_IP		0x05	/* Reset Tx Interrupt pending */
#define SCC_WR0_COMMAND_ERROR_RESET		0x06	/* Error Reset */
#define SCC_WR0_COMMAND_RESET_HIGHEST_IUS	0x07	/* Reset Highest IUS */


/* RX Int mode for the bits 3-4 of WR1 */
#define SCC_WR1_RX_MODE_INT_OFF			0x00
#define SCC_WR1_RX_MODE_INT_FIRST_CHAR_SPECIAL	0x01
#define SCC_WR1_RX_MODE_INT_ALL_CHAR_SPECIAL	0x02
#define SCC_WR1_RX_MODE_INT_SPECIAL		0x03

#define SCC_WR1_BIT_EXT_INT_ENABLE		0x01	/* Ext Int Enable */
#define SCC_WR1_BIT_TX_INT_ENABLE		0x02	/* Tx Int Enable */
#define SCC_WR1_BIT_PARITY_SPECIAL_COND		0x04	/* Parity is Special Condition */


#define SCC_WR3_BIT_RX_ENABLE			0x01	/* RX Enable */
#define SCC_WR5_BIT_RX_BITS_CHAR_BIT0		0x40	/* Bit 0 for SCC_WR5_RX_n_BITS */
#define SCC_WR5_BIT_RX_BITS_CHAR_BIT1		0x80	/* Bit 1 for SCC_WR5_RX_n_BITS */
/* RX Bits/char for the bits 6-7 */
#define SCC_WR3_RX_5_BITS			0x00	/* RX 5 bits/char or less */
#define SCC_WR3_RX_6_BITS			0x02	/* RX 6 bits/char or less */
#define SCC_WR3_RX_7_BITS			0x01	/* RX 7 bits/char or less */
#define SCC_WR3_RX_8_BITS			0x03	/* RX 8 bits/char or less */


#define SCC_WR4_BIT_PARITY_ENABLE		0x01	/* Parity Enable */
#define SCC_WR4_BIT_PARITY_IS_EVEN		0x02	/* Even Parity */
/* Parity mode for the bits 2-3 */
#define SCC_WR4_STOP_SYNC			0x00	/* Synchronous mode */
#define SCC_WR4_STOP_1_BIT			0x01	/* 1 Stop bit */
#define SCC_WR4_STOP_15_BIT			0x02	/* 1.5 Stop bit */
#define SCC_WR4_STOP_2_BIT			0x03	/* 2 Stop bits */


#define SCC_WR5_BIT_TX_CRC_ENABLE		0x01	/* TX CRC Enable */
#define SCC_WR5_BIT_RTS				0x02	/* RTS */
#define SCC_WR5_BIT_SDLC_CRC			0x04	/* SDLC/CRC-16 */
#define SCC_WR5_BIT_TX_ENABLE			0x08	/* Tx Enable */
#define SCC_WR5_BIT_SEND_BREAK			0x10	/* Send Break */
#define SCC_WR5_BIT_TX_BITS_CHAR_BIT0		0x20	/* Bit 0 for SCC_WR5_TX_n_BITS */
#define SCC_WR5_BIT_TX_BITS_CHAR_BIT1		0x40	/* Bit 1 for SCC_WR5_TX_n_BITS */
#define SCC_WR5_BIT_DTR				0x80	/* DTR */
/* TX Bits/char for the bits 5-6 */
#define SCC_WR5_TX_5_BITS			0x00	/* TX 5 bits/char or less */
#define SCC_WR5_TX_6_BITS			0x02	/* TX 6 bits/char or less */
#define SCC_WR5_TX_7_BITS			0x01	/* TX 7 bits/char or less */
#define SCC_WR5_TX_8_BITS			0x03	/* TX 8 bits/char or less */


#define SCC_WR9_BIT_VIS				0x01	/* Vector Includes Status */
#define SCC_WR9_BIT_NV				0x02	/* No Vector */
#define SCC_WR9_BIT_DISABLE_LOWER_CHAIN		0x04	/* Disable Lower Chain */
#define SCC_WR9_BIT_MIE				0x08	/* Master Interrupt Enable */
#define SCC_WR9_BIT_STATUS_HIGH_LOW		0x10	/* Status High / Low */
#define SCC_WR9_BIT_SOFT_INTACK			0x20	/* Software INTACK Enable */
/* Commands for the bits 7-8 of WR9 */
#define SCC_WR9_COMMAND_RESET_NULL		0x00	/* Null Command */
#define SCC_WR9_COMMAND_RESET_B			0x01	/* Channel B Reset */
#define SCC_WR9_COMMAND_RESET_A			0x02	/* Channel A Reset */
#define SCC_WR9_COMMAND_RESET_FORCE_HW		0x03	/* Force Hardware Reset */


#define SCC_WR15_BIT_WR7_PRIME			0x01	/* Point to Write Register WR7 Prime */
#define SCC_WR15_BIT_ZERO_COUNT_INT_ENABLE	0x02	/* Zero Count Interrupt Enable */
#define SCC_WR15_BIT_STATUS_FIFO_ENABLE		0x04	/* Status FIFO Enable */
#define SCC_WR15_BIT_DCD_INT_ENABLE		0x08	/* DCD Int Enable */
#define SCC_WR15_BIT_SYNC_HUNT_INT_ENABLE	0x10	/* SYNC/Hunt Int Enable */
#define SCC_WR15_BIT_CTS_INT_ENABLE		0x20	/* CTS Int Enable */
#define SCC_WR15_BIT_TX_UNDERRUN_EOM_INT_ENABLE	0x40	/* Transmit Underrun/EOM Int Enable */
#define SCC_WR15_BIT_BREAK_ABORT_INT_ENABLE	0x80	/* Break/Abort Int Enable */


#define SCC_RR0_BIT_RX_CHAR_AVAILABLE		0x01
#define SCC_RR0_BIT_ZERO_COUNT			0x02
#define SCC_RR0_BIT_TX_BUFFER_EMPTY		0x04
#define SCC_RR0_BIT_DCD				0x08
#define SCC_RR0_BIT_SYNC_HUNT			0x10
#define SCC_RR0_BIT_CTS				0x20
#define SCC_RR0_BIT_TX_UNDERRUN_EOM		0x40
#define SCC_RR0_BIT_BREAK_ABORT			0x80

#define SCC_RR1_BIT_ALL_SENT			0x01
#define SCC_RR1_BIT_RES_CODE_2			0x02
#define SCC_RR1_BIT_RES_CODE_1			0x04
#define SCC_RR1_BIT_RES_CODE_0			0x08
#define SCC_RR1_BIT_PARITY_ERROR		0x10
#define SCC_RR1_BIT_RX_OVERRUN_ERROR		0x20
#define SCC_RR1_BIT_CRC_FRAMING_ERROR		0x40
#define SCC_RR1_BIT_EOF_SDLC			0x80


#define SCC_RR3_BIT_EXT_STATUS_IP_B		0x01
#define SCC_RR3_BIT_TX_IP_B			0x02
#define SCC_RR3_BIT_RX_IP_B			0x04
#define SCC_RR3_BIT_EXT_STATUS_IP_A		0x08
#define SCC_RR3_BIT_TX_IP_A			0x10
#define SCC_RR3_BIT_RX_IP_A			0x20



struct SCC_Channel {
	/* NOTE : WR2 and WR9 are common to both channels, we store their content in channel A */
	/* RR2A stores the vector, RR2B stores the vector + status bits */
	/* RR3 is only in channel A, RR3B returns 0 */
	uint8_t	WR[16];			/* 0-15 are for WR0-WR15 */
	uint8_t	WR7p;			/* special case for WR7' */
	uint8_t	RR[16];			/* 0-15 are for RR0-RR15 */

	int	BaudRate_BRG;
	int	BaudRate_TX;		/* TODO : tx and rx baud rate can be different */
	int	BaudRate_RX;

	bool	RR0_IsLatched;
	uint8_t	RR0_No_Latch;		/* "real time" values of all bits, before being latched if necessary */

	bool	TX_Buffer_Written;	/* True if a write to data reg was made, needed for TBE int */

	uint8_t	TX_bits;		/* TX Bits/char (5 or less, 6, 7, 8) */
	uint8_t	RX_bits;		/* RX Bits/char (5 or less, 6, 7, 8) */
	uint8_t	Parity_bits;		/* 0 or 1 bit */
	float	Stop_bits;		/* Stop bit can be 0 bit (sync), 1 bit, 2 bits or 1.5 bit */
	uint8_t	TSR;			/* Transfer Shift Register */
	bool	TSR_Full;		/* True if data reg was copied to TSR, false when all bits of TSR have been sent */

	uint32_t IntSources;		/* Interrupt sources : 0=clear 1=set */

	/* Channel A can have serial and lan filehandles, channel B will have only serial filehandle */
	int	ReadHandle_Serial , WriteHandle_Serial;	/* For channels A and B on all machines */
	bool	FileHandle_Serial_IsATTY;
	int	ReadHandle_Lan , WriteHandle_Lan;	/* Only used on channel A for MegaSTE/TT */
	bool	FileHandle_Lan_IsATTY;

	/* Current values for filehandles */
	int	ReadHandle , WriteHandle;
	bool	FileHandle_IsATTY;
};

typedef struct {
	struct SCC_Channel Chn[2];	/* 0 is for channel A, 1 is for channel B */

	uint8_t		IRQ_Line;	/* 0=IRQ set   1=IRQ cleared */
	uint8_t		IUS;		/* Interrupt Under Service (same bits as RR3 bits 0-5) */
	int		Active_Reg;
} SCC_STRUCT;

static SCC_STRUCT SCC;



static int SCC_ClockMode[] = { 1 , 16 , 32 , 64 };	/* Clock multiplier from WR4 bits 6-7 */


static int SCC_Standard_Baudrate[] = {
	50 ,
	75,
	110,
	134,
	200,
	300,
	600,
	1200,
	1800,
	2400,
	4800,
	9600,
	19200,
	38400,
	57600,
	115200,
	230400
};


/* Possible sources of interrupt for each channel */
#define	SCC_INT_SOURCE_RX_CHAR_AVAILABLE	(1<<0)
#define	SCC_INT_SOURCE_RX_OVERRUN		(1<<1)
#define	SCC_INT_SOURCE_RX_FRAMING_ERROR		(1<<2)
#define	SCC_INT_SOURCE_RX_EOF_SDLC		(1<<3)
#define	SCC_INT_SOURCE_RX_PARITY_ERROR		(1<<4)
#define	SCC_INT_SOURCE_TX_BUFFER_EMPTY		(1<<5)
#define	SCC_INT_SOURCE_EXT_ZERO_COUNT		(1<<6)
#define	SCC_INT_SOURCE_EXT_DCD			(1<<7)
#define	SCC_INT_SOURCE_EXT_SYNC_HUNT		(1<<8)
#define	SCC_INT_SOURCE_EXT_CTS			(1<<9)
#define	SCC_INT_SOURCE_EXT_TX_UNDERRUN		(1<<10)
#define	SCC_INT_SOURCE_EXT_BREAK_ABORT		(1<<11)


/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static void	SCC_ResetChannel ( int Channel , bool HW_Reset );
static void	SCC_ResetFull ( bool HW_Reset );

static bool	SCC_Serial_Read_Byte ( int Channel , uint8_t *pValue );
static void	SCC_Serial_Write_Byte ( int Channel, uint8_t value );
#if HAVE_TERMIOS_H
static void	SCC_Serial_Set_BaudAttr ( int handle, speed_t new_speed );
#endif
static void	SCC_Serial_Set_BaudRate ( int Channel, int value );
static uint16_t	SCC_Serial_Get_CTS ( int Channel );
static uint16_t	SCC_Serial_Get_DCD ( int Channel );

static int	SCC_Get_Standard_BaudRate ( int BaudRate );
static int	SCC_Get_RTxC_Freq ( int chn );
static int	SCC_Get_TRxC_Freq ( int chn );
static int	SCC_Compute_BaudRate ( int chn , bool *pStartBRG , uint32_t *pBaudRate_BRG );
static void	SCC_Update_BaudRate ( int Channel );

static uint8_t	SCC_Get_Vector_Status ( void );

static void	SCC_Update_RR0 ( int Channel );
static void	SCC_Update_RR0_Clear ( int Channel , int bits );
static void	SCC_Update_RR0_Set ( int Channel , int bits );
static void	SCC_Update_RR0_Latch_Off ( int Channel );
static void	SCC_Update_RR2 ( void );
static void	SCC_Update_RR3_Bit ( bool Set , uint8_t Bit );
static void	SCC_Update_RR3 ( int Channel );

static void	SCC_Copy_TDR_TSR ( int Channel , uint8_t TDR );

static void	SCC_Process_TX ( int Channel );
static void	SCC_Process_RX ( int Channel );

static void	SCC_Start_InterruptHandler_BRG ( int Channel , int InternalCycleOffset );
static void	SCC_Stop_InterruptHandler_BRG ( int Channel );
static void	SCC_InterruptHandler_BRG ( int Channel );

static void	SCC_Start_InterruptHandler_TX_RX ( int Channel , bool is_tx , int InternalCycleOffset );
static void	SCC_Stop_InterruptHandler_TX_RX ( int Channel , bool is_tx );
static void	SCC_Restart_InterruptHandler_TX_RX ( int Channel , bool is_tx );

static void	SCC_InterruptHandler_TX_RX ( int Channel );
static void	SCC_InterruptHandler_RX ( int Channel );

static void     SCC_Set_Line_IRQ ( int bit );
static void	SCC_Update_IRQ ( void );
static void	SCC_IntSources_Change ( int Channel , uint32_t Source , bool Set );
static void	SCC_IntSources_Set ( int Channel , uint32_t Source );
static void	SCC_IntSources_Clear ( int Channel , uint32_t Source );
static void	SCC_IntSources_Clear_NoUpdate ( int Channel , uint32_t Source );

static int	SCC_Do_IACK ( bool Soft );
static void	SCC_Soft_IACK ( void );



/*
 * Return true if the current machine has a built-in SCC chip.
 * Else return false.
 */
bool SCC_IsAvailable(CNF_PARAMS *cnf)
{
	return ConfigureParams.System.nMachineType == MACHINE_MEGA_STE
	       || ConfigureParams.System.nMachineType == MACHINE_TT
	       || ConfigureParams.System.nMachineType == MACHINE_FALCON;
}



/*
 * Return true if the LAN port is connected to SCC Channel A
 * Return false if SCC Channel A is connected to the serial port
 * The LAN port is only available on MegaSTE and TT and can be enabled
 * using bit 7 in reg 14 of the YM2149 (0=Lan, 1=Serial)
 */
void	SCC_Check_Lan_IsEnabled ( void )
{
	if ( Config_IsMachineFalcon()			/* Falcon doesn't have a LAN port on SCC A */
	    || ( PSGRegisters[ PSG_REG_IO_PORTA ] & 0x80 ) )	/* Bit7 = 1 : Serial port */
	{
		SCC.Chn[0].ReadHandle = SCC.Chn[0].ReadHandle_Serial;
		SCC.Chn[0].WriteHandle = SCC.Chn[0].WriteHandle_Serial;
		SCC.Chn[0].FileHandle_IsATTY = SCC.Chn[0].FileHandle_Serial_IsATTY;
	}
	else							/* Bit7 = 0 : LAN port */
	{
		SCC.Chn[0].ReadHandle = SCC.Chn[0].ReadHandle_Lan;
		SCC.Chn[0].WriteHandle = SCC.Chn[0].WriteHandle_Lan;
		SCC.Chn[0].FileHandle_IsATTY = SCC.Chn[0].FileHandle_Lan_IsATTY;
	}
}



static void SCC_Init_Channel ( int Channel , bool *pConfEnableScc , char *InFileName , char *OutFileName ,
			       int *pReadHandle , int *pWriteHandle , bool *pIsATTY )
{
	*pReadHandle = *pWriteHandle = -1;
	*pIsATTY = false;

	if (!*pConfEnableScc || !SCC_IsAvailable(&ConfigureParams))
		return;

	if ( InFileName[0] && strcmp ( InFileName , OutFileName ) == 0 )
	{
#if HAVE_TERMIOS_H
		*pReadHandle = open ( InFileName , O_RDWR | O_NONBLOCK);
		if ( *pReadHandle >= 0 )
		{
			if ( isatty(*pReadHandle) )
			{
				*pWriteHandle = *pReadHandle;
				*pIsATTY = true;
			}
			else
			{
				Log_Printf(LOG_ERROR, "SCC_Init: Setting SCC %c input and output "
				           "to the same file only works with tty devices.\n" , Channel+'A');
				close ( *pReadHandle );
				 *pReadHandle = -1;
			}
		}
		else
		{
			Log_Printf(LOG_ERROR, "SCC_Init: Can not open device '%s'\n", InFileName);
		}
#else
		Log_Printf(LOG_ERROR, "SCC_Init: Setting SCC %c input and output "
		           "to the same file is not supported on this system.\n" , Channel+'A');
#endif
	}
	else
	{
		if ( InFileName[0] )
		{
			*pReadHandle = open ( InFileName , O_RDONLY | O_NONBLOCK);
			if ( *pReadHandle < 0)
			{
				Log_Printf(LOG_ERROR, "SCC_Init: Can not open input file '%s'\n", InFileName);
			}
		}
		if ( OutFileName[0] )
		{
			*pWriteHandle = open ( OutFileName , O_CREAT | O_WRONLY | O_NONBLOCK, S_IRUSR | S_IWUSR);
			if ( *pWriteHandle < 0 )
			{
				Log_Printf(LOG_ERROR, "SCC_Init: Can not open output file '%s'\n", OutFileName);
			}
		}
	}

	if ( *pReadHandle == -1 && *pWriteHandle == -1)
	{
		*pConfEnableScc = false;
	}
}


void SCC_Init ( void )
{
	SCC_Reset();

	/* Init filehandles for channel A Serial */
	SCC_Init_Channel ( 0 , &ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_SERIAL] ,
		ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_SERIAL] , ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_SERIAL] ,
		&SCC.Chn[0].ReadHandle_Serial , &SCC.Chn[0].WriteHandle_Serial , &SCC.Chn[0].FileHandle_Serial_IsATTY );

	/* Init filehandles for channel A LAN */
	SCC_Init_Channel ( 0 , &ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_A_LAN] ,
		ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_A_LAN] , ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_A_LAN] ,
		&SCC.Chn[0].ReadHandle_Lan , &SCC.Chn[0].WriteHandle_Lan , &SCC.Chn[0].FileHandle_Lan_IsATTY );

	/* Connect Channel A to Serial or LAN port depending on the machine */
	SCC_Check_Lan_IsEnabled();

	/* Init filehandles for channel B */
	SCC_Init_Channel ( 1 , &ConfigureParams.RS232.EnableScc[CNF_SCC_CHANNELS_B] ,
		ConfigureParams.RS232.SccInFileName[CNF_SCC_CHANNELS_B] , ConfigureParams.RS232.SccOutFileName[CNF_SCC_CHANNELS_B] ,
		&SCC.Chn[1].ReadHandle_Serial , &SCC.Chn[1].WriteHandle_Serial , &SCC.Chn[1].FileHandle_Serial_IsATTY );
	SCC.Chn[1].ReadHandle = SCC.Chn[1].ReadHandle_Serial;
	SCC.Chn[1].WriteHandle = SCC.Chn[1].WriteHandle_Serial;
	SCC.Chn[1].FileHandle_IsATTY = SCC.Chn[1].FileHandle_Serial_IsATTY;
}



void SCC_UnInit(void)
{
	int Channel;

	for ( Channel = 0; Channel < 2; Channel++ )
	{
		if ( SCC.Chn[Channel].ReadHandle_Serial >= 0 )
			close(SCC.Chn[Channel].ReadHandle_Serial);
		if ( ( SCC.Chn[Channel].WriteHandle_Serial != SCC.Chn[Channel].ReadHandle_Serial )
		  && ( SCC.Chn[Channel].WriteHandle_Serial >= 0 ) )
			close(SCC.Chn[Channel].WriteHandle_Serial);

		if ( SCC.Chn[Channel].ReadHandle_Lan >= 0 )
			close(SCC.Chn[Channel].ReadHandle_Lan);
		if ( ( SCC.Chn[Channel].WriteHandle_Lan != SCC.Chn[Channel].ReadHandle_Lan )
		  && ( SCC.Chn[Channel].WriteHandle_Lan >= 0 ) )
			close(SCC.Chn[Channel].WriteHandle_Lan);

		SCC.Chn[Channel].ReadHandle_Serial = SCC.Chn[Channel].WriteHandle_Serial = -1;
		SCC.Chn[Channel].ReadHandle_Lan = SCC.Chn[Channel].WriteHandle_Lan = -1;
		SCC.Chn[Channel].ReadHandle = SCC.Chn[Channel].WriteHandle = -1;
	}
}


void SCC_MemorySnapShot_Capture(bool bSave)
{
	int c;

	for (c = 0; c < 2; c++)
	{
		MemorySnapShot_Store(SCC.Chn[c].WR, sizeof(SCC.Chn[c].WR));
		MemorySnapShot_Store(&SCC.Chn[c].WR7p, sizeof(SCC.Chn[c].WR7p));
		MemorySnapShot_Store(SCC.Chn[c].RR, sizeof(SCC.Chn[c].RR));
		MemorySnapShot_Store(&SCC.Chn[c].BaudRate_BRG, sizeof(SCC.Chn[c].BaudRate_BRG));
		MemorySnapShot_Store(&SCC.Chn[c].BaudRate_TX, sizeof(SCC.Chn[c].BaudRate_TX));
		MemorySnapShot_Store(&SCC.Chn[c].BaudRate_RX, sizeof(SCC.Chn[c].BaudRate_RX));
		MemorySnapShot_Store(&SCC.Chn[c].RR0_IsLatched, sizeof(SCC.Chn[c].RR0_IsLatched));
		MemorySnapShot_Store(&SCC.Chn[c].RR0_No_Latch, sizeof(SCC.Chn[c].RR0_No_Latch));
		MemorySnapShot_Store(&SCC.Chn[c].TX_Buffer_Written, sizeof(SCC.Chn[c].TX_Buffer_Written));
		MemorySnapShot_Store(&SCC.Chn[c].TX_bits, sizeof(SCC.Chn[c].TX_bits));
		MemorySnapShot_Store(&SCC.Chn[c].RX_bits, sizeof(SCC.Chn[c].RX_bits));
		MemorySnapShot_Store(&SCC.Chn[c].Parity_bits, sizeof(SCC.Chn[c].Parity_bits));
		MemorySnapShot_Store(&SCC.Chn[c].Stop_bits, sizeof(SCC.Chn[c].Stop_bits));
		MemorySnapShot_Store(&SCC.Chn[c].TSR, sizeof(SCC.Chn[c].TSR));
		MemorySnapShot_Store(&SCC.Chn[c].TSR_Full, sizeof(SCC.Chn[c].TSR_Full));
		MemorySnapShot_Store(&SCC.Chn[c].IntSources, sizeof(SCC.Chn[c].IntSources));
	}

	MemorySnapShot_Store(&SCC.IRQ_Line, sizeof(SCC.IRQ_Line));
	MemorySnapShot_Store(&SCC.IUS, sizeof(SCC.IUS));
	MemorySnapShot_Store(&SCC.Active_Reg, sizeof(SCC.Active_Reg));
}



static void SCC_ResetChannel ( int Channel , bool HW_Reset )
{
	SCC.Chn[Channel].WR[0] = 0x00;
	SCC.Active_Reg = 0;
	SCC.Chn[Channel].WR[1] &= 0x24;			/* keep bits 2 and 5, clear others */
	SCC.Chn[Channel].WR[3] &= 0xfe;			/* keep bits 1 to 7, clear bit 0 */
	SCC.Chn[Channel].WR[4] |= 0x04;			/* set bit 2, keep others */
	SCC.Chn[Channel].WR[5] &= 0x61;			/* keep bits 0,5 and 6, clear others */
	SCC.Chn[Channel].WR[15] = 0xf8;
	SCC.Chn[Channel].WR7p = 0x20;			/* WR7' set bit5, clear others */

	if ( HW_Reset )
	{
		/* WR9 is common to channel A and B, we store it in channel A */
		SCC.Chn[0].WR[9] &= 0x03;		/* keep bits 0 and 1, clear others */
		SCC.Chn[0].WR[9] |= 0xC0;		/* set bits 7 and 8 */
		SCC.IUS = 0x00;				/* clearing MIE also clear IUS */

		SCC.Chn[Channel].WR[10] = 0x00;
		SCC.Chn[Channel].WR[11] = 0x08;
		SCC.Chn[Channel].WR[14] &= 0xC0;	/* keep bits 7 and 8, clear others */
		SCC.Chn[Channel].WR[14] |= 0x30;	/* set bits 4 and 5 */
	}
	else
	{
		/* WR9 is common to channel A and B, we store it in channel A */
		SCC.Chn[0].WR[9] &= 0xdf;		/* clear bit 6, keep others */

		SCC.Chn[Channel].WR[10] &= 0x60;	/* keep bits 5 and 6, clear others */
		SCC.Chn[Channel].WR[14] &= 0xC3;	/* keep bits 0,1,7 and 8, clear others */
		SCC.Chn[Channel].WR[14] |= 0x20;	/* set bit 5 */
	}


	SCC.Chn[Channel].RR[0] &= 0xb8;			/* keep bits 3,4 and 5, clear others */
	SCC.Chn[Channel].RR[0] |= 0x44;			/* set bits 2 and 6 */
	SCC.Chn[Channel].RR0_No_Latch = SCC.Chn[Channel].RR[0];
	SCC.Chn[Channel].RR0_IsLatched = false;		/* no pending int sources */
	SCC.Chn[Channel].RR[1] &= 0x01;			/* keep bits 0, clear others */
	SCC.Chn[Channel].RR[1] |= 0x06;			/* set bits 1 and 2 */
	SCC.Chn[Channel].RR[3] = 0x00;
	SCC.Chn[Channel].RR[10] &= 0x40;		/* keep bits 6, clear others */
	SCC.Chn[Channel].TX_Buffer_Written = false;	/* no write made to TB for now */
	SCC.Chn[Channel].TSR_Full = false;		/* TSR is empty */
}



/* On real hardware HW_Reset would be true when /RD and /WR are low at the same time (not supported in Mega STE / TT / Falcon)
 *  - For our emulation, we also do HW_Reset=true when resetting the emulated machine
 *  - When writing 0xC0 to WR9 a full reset will be done with HW_Reset=true
 */
static void SCC_ResetFull ( bool HW_Reset )
{
	uint8_t wr9_old;

	wr9_old = SCC.Chn[0].WR[9];		/* Save WR9 before reset */

	SCC_ResetChannel ( 0 , true );
	SCC_ResetChannel ( 1 , true );

	if ( !HW_Reset )			/* Reset through WR9, some bits are kept */
	{
		/* Restore bits 2,3,4 after software full reset */
		SCC.Chn[0].WR[9] &= ~0x1c;
		SCC.Chn[0].WR[9] |= ( wr9_old & 0x1c );
	}

	SCC.Chn[0].IntSources = 0;
	SCC.Chn[1].IntSources = 0;
	SCC_Set_Line_IRQ ( SCC_IRQ_OFF );	/* IRQ line goes high */
}


void SCC_Reset(void)
{
	memset(SCC.Chn[0].WR, 0, sizeof(SCC.Chn[0].WR));
	memset(SCC.Chn[0].RR, 0, sizeof(SCC.Chn[0].RR));
	memset(SCC.Chn[1].WR, 0, sizeof(SCC.Chn[1].WR));
	memset(SCC.Chn[1].RR, 0, sizeof(SCC.Chn[1].RR));

	SCC_ResetFull ( true );

}


static bool SCC_Serial_Read_Byte ( int Channel , uint8_t *pValue )
{
	int nb;

	if ( SCC.Chn[Channel].ReadHandle >= 0 )
	{
		nb = read ( SCC.Chn[Channel].ReadHandle , pValue , 1 );
		if (nb < 0)
		{
			if (errno == EAGAIN || errno == EINTR)	/* nothing yet, retry later */
				return false;
			Log_Printf(LOG_WARN, "scc serial read byte channel %c : read failed, errno=%d\n", 'A'+Channel, errno);
		}
		else if ( nb == 0 )
		{
			LOG_TRACE(TRACE_SCC, "scc serial read byte channel %c : no byte\n", 'A'+Channel);
		}
		else
		{
			LOG_TRACE(TRACE_SCC, "scc serial read byte channel %c rx=$%02x\n", 'A'+Channel, *pValue);
		}
		return ( nb > 0 );
	}

	return false;
}

static void SCC_Serial_Write_Byte ( int Channel, uint8_t value )
{
	int nb;

	LOG_TRACE(TRACE_SCC, "scc serial write byte channel=%c value=$%02x\n", 'A'+Channel, value);

	if ( SCC.Chn[Channel].WriteHandle >= 0 )
	{
		do
		{
			nb = write ( SCC.Chn[Channel].WriteHandle , &value , 1 );
		} while (nb < 0 && (errno == EAGAIN || errno == EINTR));
	}
}

#if HAVE_TERMIOS_H
static void SCC_Serial_Set_BaudAttr ( int handle, speed_t new_speed )
{
	struct termios options;

	if (handle < 0)
		return;

	memset(&options, 0, sizeof(options));
	if (tcgetattr(handle, &options) < 0)
	{
		LOG_TRACE(TRACE_SCC, "SCC: tcgetattr() failed\n");
		return;
	}

	cfsetispeed(&options, new_speed);
	cfsetospeed(&options, new_speed);

	options.c_cflag |= (CLOCAL | CREAD);
	options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // raw input
	options.c_iflag &= ~(ICRNL); // CR is not CR+LF

	tcsetattr(handle, TCSANOW, &options);
}
#endif


/* summary of baud rates:
    Rsconf   Falcon     Falcon(+HSMODEM)   Hatari    Hatari(+HSMODEM)
    0        19200         19200            19200       19200
    1         9600          9600             9600        9600
    2         4800          4800             4800        4800
    3         3600          3600            57600       57600
    4         2400          2400             2400        2400
    5         2000          2000            38400       38400
    6         1800          1800             1800        1800
    7         1200          1200             1200        1200
    8          600           600              600         600
    9          300           300              300         300
    10         200        230400              200      230400
    11         150        115200              150      115200
    12         134         57600              134       57600
    13         110         38400              110       38400
    14          75        153600               75          75
    15          50         76800               50          50
*/

static void SCC_Serial_Set_BaudRate ( int Channel, int value )
{
#if HAVE_TERMIOS_H
	speed_t new_speed = B0;

	LOG_TRACE(TRACE_SCC, "scc serial set baud channel=%c value=$%02x\n", 'A'+Channel, value);

	switch (value)
	{
#ifdef B230400					/* B230400 is not defined on all systems */
	 case 230400:	new_speed = B230400;	break;
#endif
	 case 115200:	new_speed = B115200;	break;
	 case 57600:	new_speed = B57600;	break;
	 case 38400:	new_speed = B38400;	break;
	 case 19200:	new_speed = B19200;	break;
	 case 9600:	new_speed = B9600;	break;
	 case 4800:	new_speed = B4800;	break;
	 case 2400:	new_speed = B2400;	break;
	 case 1800:	new_speed = B1800;	break;
	 case 1200:	new_speed = B1200;	break;
	 case 600:	new_speed = B600;	break;
	 case 300:	new_speed = B300;	break;
	 case 200:	new_speed = B200;	break;
	 case 150:	new_speed = B150;	break;
	 case 134:	new_speed = B134;	break;
	 case 110:	new_speed = B110;	break;
	 case 75:	new_speed = B75;	break;
	 case 50:	new_speed = B50;	break;
	 default:	Log_Printf(LOG_DEBUG, "SCC: unsupported baud rate %i\n", value);
		break;
	}

	if (new_speed == B0)
		return;

	SCC_Serial_Set_BaudAttr ( SCC.Chn[Channel].ReadHandle, new_speed );
	if ( SCC.Chn[Channel].ReadHandle != SCC.Chn[Channel].WriteHandle )
		SCC_Serial_Set_BaudAttr ( SCC.Chn[Channel].WriteHandle , new_speed );
#endif
}



static uint16_t SCC_Serial_Get_CTS ( int Channel )
{
	int	cts = 1;
	Log_Printf(LOG_DEBUG, "SCC: get status for CTS %d\n" , Channel);

#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int	status = 0;
	if ( SCC.Chn[Channel].WriteHandle >= 0 && SCC.Chn[Channel].FileHandle_IsATTY )
	{
		if ( ioctl ( SCC.Chn[Channel].WriteHandle , TIOCMGET , &status ) < 0 )
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status for CTS errno=%d\n", errno);
		}
		else
		{
			if ( status & TIOCM_CTS )
				cts = 1;
			else
				cts = 0;
		}
	}
#endif
	return cts;
}


static uint16_t SCC_Serial_Get_DCD ( int Channel )
{
	int	dcd = 1;
	Log_Printf(LOG_DEBUG, "SCC: get status for DCD %d\n" , Channel);

	#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int	status = 0;
	if ( SCC.Chn[Channel].WriteHandle >= 0 && SCC.Chn[Channel].FileHandle_IsATTY )
	{
		if ( ioctl ( SCC.Chn[Channel].WriteHandle , TIOCMGET , &status ) < 0 )
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status for DCD errno=%d\n" , errno);
		}
		else
		{
			if ( status & TIOCM_CAR )
				dcd = 1;
			else
				dcd = 0;
		}
	}
#endif
	return dcd;
}



/* Send BREAK. If value!=0, set break on. If value=0, set break off */
static void SCC_serial_Set_BRK(int chn, uint8_t value)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCSBRK)
	int cmd = 0;

	if ( SCC.Chn[chn].WriteHandle >= 0 && SCC.Chn[chn].FileHandle_IsATTY )
	{
		if ( value )
			cmd = TIOCSBRK;		/* set break */
		else
			cmd = TIOCCBRK;		/* clear break */

		if ( ioctl ( SCC.Chn[chn].WriteHandle , cmd) < 0)
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't set BRK=%s errno=%d\n" , value?"ON":"OFF" , errno );
		}
	}
#endif
}


static void SCC_serial_setRTS(int chn, uint8_t value)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int status = 0;

	if ( SCC.Chn[chn].WriteHandle >= 0 && SCC.Chn[chn].FileHandle_IsATTY)
	{
		if ( ioctl ( SCC.Chn[chn].WriteHandle , TIOCMGET , &status ) < 0 )
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status for RTS\n");
		}
		if (value)
			status |= TIOCM_RTS;
		else
			status &= ~TIOCM_RTS;
		ioctl ( SCC.Chn[chn].WriteHandle , TIOCMSET , &status );
	}
#endif
}

static void SCC_serial_setDTR(int chn, uint8_t value)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int status = 0;

	if ( SCC.Chn[chn].WriteHandle >= 0 && SCC.Chn[chn].FileHandle_IsATTY )
	{
		if ( ioctl ( SCC.Chn[chn].WriteHandle , TIOCMGET , &status ) < 0 )
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status for DTR\n");
		}
		if (value)
			status |= TIOCM_DTR;
		else
			status &= ~TIOCM_DTR;
		ioctl ( SCC.Chn[chn].WriteHandle , TIOCMSET , &status );
	}
#endif
}


/*
 * Depending on the selected clock mode the baud rate might not match
 * exactly the standard baud rates. For example with a 8 MHz clock and
 * time constant=24 with x16 multiplier, we get an effective baud rate
 * of 9641, instead of the standard 9600.
 * To handle this we use a 1% margin to check if the computed baud rate
 * match one of the standard baud rates. If so, we will use the standard
 * baud rate to configure the serial port.
 */

static int SCC_Get_Standard_BaudRate ( int BaudRate )
{
	float	margin , low , high;
	int	i;


	for ( i=0 ; i<(int)ARRAY_SIZE(SCC_Standard_Baudrate) ; i++ )
	{
		margin = SCC_Standard_Baudrate[ i ] * 0.01;	/* 1% */
		if ( margin < 4 )
			margin = 4;				/* increase margin for small bitrates < 600 */

		low = SCC_Standard_Baudrate[ i ] - margin;
		high = SCC_Standard_Baudrate[ i ] + margin;
//fprintf ( stderr , "check %d %d %f %f\n" , i , BaudRate , low , high );
		if ( ( low <= BaudRate ) && ( BaudRate <= high ) )
			return SCC_Standard_Baudrate[ i ];
	}

	return -1;
}


/*
 * Get the frequency in Hz for RTxCA and RTxCB depending on the machine type
 *  - RTxCA is connected to PCLK4 on all machines
 *  - RTxCB is also connected to PCLK4 on MegaSTE and Falcon
 *    On TT it's connected to TTCLK (Timer C output on the TT-MFP)
 */

static int SCC_Get_RTxC_Freq ( int chn )
{
	int ClockFreq;

	if ( Config_IsMachineMegaSTE() || Config_IsMachineFalcon() )
		ClockFreq = SCC_CLOCK_PCLK4;
	else						/* TT */
	{
		if ( chn == 0 )
			ClockFreq = SCC_CLOCK_PCLK4;
		else
		{
			/* TODO : clock is connected to timer C output on TT-MFP */
			ClockFreq = SCC_CLOCK_PCLK4;	/* TODO : use TCCLK */
		}
	}

	return ClockFreq;
}


/*
 * Get the frequency in Hz for TRxCA and TRxCB  depending on the machine type
 *  - TRxCB is connected to BCLK on all machines (2.4576 MHz on the MFP's XTAL)
 *  - TRxCA is connected to LCLK on MegaSTE and TT
 *    On Falcon it's connected to SYNCA on the SCC
 */

static int SCC_Get_TRxC_Freq ( int chn )
{
	int ClockFreq;

	if ( chn == 1 )
		ClockFreq = SCC_CLOCK_BCLK;
	else
	{
		if ( Config_IsMachineMegaSTE() || Config_IsMachineTT() )
			/* TODO : clock is connected to LCLK */
			ClockFreq = SCC_CLOCK_BCLK;	/* TODO : use LCLK */
		else
			/* TODO : clock is connected to SYNCA */
			ClockFreq = SCC_CLOCK_BCLK;	/* TODO : use SYNCA */
	}

	return ClockFreq;
}


/*
 * Return the generated baud rate depending on the value of WR4, WR11, WR12, WR13 and WR14
 *
 * The baud rate can use RtxC or TRxC clocks (which depend on the machine type) with
 * an additional clock multipier.
 * Or the baud rate can use the baud rate generator and its time constant.
 *
 * The SCC doc gives the formula to compute time constant from a baud rate in the BRG :
 *	TimeConstant = ( ClockFreq / ( 2 * BaudRate * ClockMult ) ) - 2
 *
 * when we know the time constant in the BRG, we can compute the baud rate for the BRG :
 *	BaudRate = ClockFreq / ( 2 * ( TimeConstant + 2 ) * ClockMult )
 */

static int SCC_Compute_BaudRate ( int chn , bool *pStartBRG , uint32_t *pBaudRate_BRG )
{
	int	TimeConstant;
	int	ClockFreq_BRG = 0;
	int	ClockFreq = 0;
	int	ClockMult;
	int	TransmitClock , ReceiveClock;
	int	BaudRate;
	const char	*ClockName;


	/* WR4 gives Clock Mode Multiplier */
	if ( ( SCC.Chn[chn].WR[4] & 0x0c ) == 0 )			/* bits 2-3 = 0, sync modes enabled, force x1 clock */
		ClockMult = 1;
	else
		ClockMult = SCC_ClockMode[ SCC.Chn[chn].WR[4] >> 6 ];	/* use bits 6-7 to get multiplier */


	/* WR12 and WR13 give Low/High values of the 16 bit time constant for the BRG  */
	TimeConstant = ( SCC.Chn[chn].WR[13]<<8 ) + SCC.Chn[chn].WR[12];


	/* WR14 gives the clock source for the baud rate generator + enable the BRG */
	/* NOTE : it's possible to start the BRG even if we use a different clock mode later */
	/* for the baud rate in WR11 */
	if ( ( SCC.Chn[chn].WR[14] & 1 ) == 0 )				/* BRG is disabled */
		*pStartBRG = false;
	else
	{
		*pStartBRG = true;

		if ( SCC.Chn[chn].WR[14] & 2 )				/* source is PCLK */
			ClockFreq_BRG = SCC_CLOCK_PCLK;
		else							/* source is RTxC */
			ClockFreq_BRG = SCC_Get_RTxC_Freq ( chn );

		*pBaudRate_BRG = round ( (float)ClockFreq_BRG / ( 2 * ClockMult * ( TimeConstant + 2 ) ) );

		if ( *pBaudRate_BRG == 0 )				/* if we rounded to O, we use 1 instead */
			*pBaudRate_BRG = 1;

		LOG_TRACE(TRACE_SCC, "scc compute baud rate start BRG clock_freq=%d chn=%d mult=%d tc=%d br=%d\n" , ClockFreq_BRG , chn , ClockMult , TimeConstant , *pBaudRate_BRG );
	}


	/* WR11 clock mode */
	/* In the case of our emulation we only support when "Receive Clock" mode is the same as "Transmit Clock" */
	TransmitClock = ( SCC.Chn[chn].WR[11] >> 3 ) & 3;
	ReceiveClock = ( SCC.Chn[chn].WR[11] >> 5 ) & 3;
	if ( TransmitClock != ReceiveClock )
	{
		LOG_TRACE(TRACE_SCC, "scc compute baud rate %c, unsupported clock mode in WR11, transmit=%d != receive=%d\n" , 'A'+chn , TransmitClock , ReceiveClock );
		return -1;
	}


	/* Compute the tx/rx baud rate depending on the clock mode in WR11 */
	if ( TransmitClock == SCC_BAUDRATE_SOURCE_CLOCK_BRG )		/* source is BRG */
	{
		if ( !*pStartBRG )
		{
			LOG_TRACE(TRACE_SCC, "scc compute baud rate %c, clock mode set to BRG but BRG not enabled\n" , 'A'+chn );
			return -1;
		}

		ClockName = "BRG";
		BaudRate = *pBaudRate_BRG;
	}
	else
	{
		if ( TransmitClock == SCC_BAUDRATE_SOURCE_CLOCK_RTXC )		/* source is RTxC */
		{
			ClockName = "RTxC";
			ClockFreq = SCC_Get_RTxC_Freq ( chn );
		}
		else if ( TransmitClock == SCC_BAUDRATE_SOURCE_CLOCK_TRXC )	/* source is TRxC */
		{
			ClockName = "TRxC";
			ClockFreq = SCC_Get_TRxC_Freq ( chn );
		}
		else								/* source is DPLL, not supported */
		{
			ClockName = "DPLL";
			LOG_TRACE(TRACE_SCC, "scc compute baud rate %c, unsupported clock mode dpll in WR11\n" , 'A'+chn );
			return -1;
		}

		if ( ClockFreq == 0 )						/* this can happen when using RTxC=TCCLK */
		{
			LOG_TRACE(TRACE_SCC, "scc compute baud rate clock_source=%s clock_freq=%d chn=%d, clock is stopped\n" , ClockName , ClockFreq , chn );
			return -1;
		}

		BaudRate = round ( (float)ClockFreq / ClockMult );
	}

	LOG_TRACE(TRACE_SCC, "scc compute baud rate clock_source=%s clock_freq=%d chn=%d clock_mode=%d mult=%d tc=%d br=%d\n" , ClockName , TransmitClock==SCC_BAUDRATE_SOURCE_CLOCK_BRG?ClockFreq_BRG:ClockFreq , chn , TransmitClock , ClockMult , TimeConstant , BaudRate );

	return BaudRate;
}


/*
 * This function groups all the actions when the corresponding WRx are modified
 * to change the baud rate on a channel :
 *  - compute new baud rate
 *  - start BRG timer if needed
 *  - check if baud rate is a standard one and configure host serial port
 */

static void SCC_Update_BaudRate ( int Channel )
{
	bool		StartBRG;
	uint32_t	BaudRate_BRG;
	int		BaudRate;
	int		BaudRate_Standard;
	bool		Serial_ON;


	BaudRate = SCC_Compute_BaudRate ( Channel , &StartBRG , &BaudRate_BRG );
	if ( StartBRG )
	{
		SCC.Chn[Channel].BaudRate_BRG = BaudRate_BRG;
		SCC_Start_InterruptHandler_BRG ( Channel , 0 );
	}
	else
	{
		SCC_Stop_InterruptHandler_BRG ( Channel );
	}

	SCC.Chn[Channel].BaudRate_TX = BaudRate;
	SCC.Chn[Channel].BaudRate_RX = BaudRate;
	if ( BaudRate == -1 )
	{
		Serial_ON = false;
		SCC_Stop_InterruptHandler_TX_RX ( Channel , true );		/* TX */
		SCC_Stop_InterruptHandler_TX_RX ( Channel , false );		/* RX */
	}
	else
	{
		BaudRate_Standard = SCC_Get_Standard_BaudRate ( BaudRate );
		if ( BaudRate_Standard > 0 )
			Serial_ON = true;
		else
			Serial_ON = false;

		/* If baudrate is the same for TX and RX we start only one timer TX for both */
		/* Else we start a timer for TX and a timer for RX */
		SCC_Start_InterruptHandler_TX_RX ( Channel , true , 0 );		/* start TX */
		if ( SCC.Chn[Channel].BaudRate_TX != SCC.Chn[Channel].BaudRate_RX )
			SCC_Start_InterruptHandler_TX_RX ( Channel , false , 0 );	/* start RX */
		else
			SCC_Stop_InterruptHandler_TX_RX ( Channel , false );		/* stop RX */
	}

	if ( Serial_ON )
	{
//fprintf(stderr , "update br serial_on %d->%d\n" , BaudRate , BaudRate_Standard );
		SCC_Serial_Set_BaudRate ( Channel , BaudRate_Standard );
	}
	else
	{
		/* TODO : stop serial ; use baudrate = B0 ? */
	}
}


/*
 * Return Status Information bits, as included in RR2B / Vector register
 * This status contains 3 bits to indicate the current highest IP
 *  - This status is always included in the vector when reading RR2B
 *  - During an INTACK this status will only be included in the vector if VIS bit is set in WR9
 * If no interrupt are pending, return "Ch B Special Receive Condition"
 *
 * Note that depending on Status High/Low bit in WR9 these 3 bits will be in a different order
 */

static uint8_t	SCC_Get_Vector_Status ( void )
{
	uint8_t status;
	uint8_t special_cond_mask;


	/* Check pending interrupts from highest to lowest ; if no IP, return Ch B Special Receive Condition */
	if ( SCC.Chn[0].RR[3] & SCC_RR3_BIT_RX_IP_A )
	{
		special_cond_mask = SCC_RR1_BIT_RX_OVERRUN_ERROR | SCC_RR1_BIT_CRC_FRAMING_ERROR | SCC_RR1_BIT_EOF_SDLC;
		if ( SCC.Chn[0].WR[1] & SCC_WR1_BIT_PARITY_SPECIAL_COND )
			special_cond_mask |= SCC_RR1_BIT_PARITY_ERROR;
		if ( SCC.Chn[0].RR[0] & special_cond_mask )
			status = 7;			/* Ch. A Special Receive Condition */
		else
			status = 6;			/* Ch. A Receive Char Available */
	}
	else if ( SCC.Chn[0].RR[3] & SCC_RR3_BIT_TX_IP_A )
		status = 4;
	else if ( SCC.Chn[0].RR[3] & SCC_RR3_BIT_EXT_STATUS_IP_A )
		status = 5;

	else if ( SCC.Chn[0].RR[3] & SCC_RR3_BIT_RX_IP_B )
	{
		special_cond_mask = SCC_RR1_BIT_RX_OVERRUN_ERROR | SCC_RR1_BIT_CRC_FRAMING_ERROR | SCC_RR1_BIT_EOF_SDLC;
		if ( SCC.Chn[1].WR[1] & SCC_WR1_BIT_PARITY_SPECIAL_COND )
			special_cond_mask |= SCC_RR1_BIT_PARITY_ERROR;
		if ( SCC.Chn[1].RR[0] & special_cond_mask )
			status = 3;			/* Ch. B Special Receive Condition */
		else
			status = 2;			/* Ch. B Receive Char Available */
	}
	else if ( SCC.Chn[0].RR[3] & SCC_RR3_BIT_TX_IP_B )
		status = 0;
	else if ( SCC.Chn[0].RR[3] & SCC_RR3_BIT_EXT_STATUS_IP_B )
		status = 1;

	else
		status = 3;				/* No IP : return Ch. B Special Receive Condition */

	return status;
}


/*
 * Update the content of RRO (TX/RX status and External status)
 * As a result of a pending interrupt, some bits in RR0 for external status can be latched.
 *  - Bit should not be updated if corresponding IE bit in WR15 is set (bit is latched)
 *  - If corresponding bit in WR15 is clear then RR0 will reflect the current status (bit is not latched)
 * Latch will be reset when writing command RESET_EXT_STATUS_INT in WR0
 */
static void	SCC_Update_RR0 ( int Channel )
{
	uint8_t		RR0_New;
	uint8_t		RR0_Old;
	bool		Update_CTS= false;
	bool		Update_DCD= false;
	bool		Set_RR3;


//fprintf ( stderr , "update rr0 %c in=$%02x wr15=$%02x pc=%x\n" , 'A'+Channel , SCC.Chn[Channel].RR[0] , SCC.Chn[Channel].WR[15] , M68000_GetPC() );

	if ( !SCC.Chn[ Channel ].RR0_IsLatched )
	{
		/* Use all "non latched" bits for RR0 */
		RR0_New = SCC.Chn[ Channel ].RR0_No_Latch;
		/* Update CTS and DCD with their current line value */
		Update_CTS = true;
		Update_DCD = true;
	}

	else
	{
		/* Bits 0 and 2 are not latched */
		RR0_New = SCC.Chn[ Channel ].RR0_No_Latch & ( SCC_RR0_BIT_RX_CHAR_AVAILABLE | SCC_RR0_BIT_TX_BUFFER_EMPTY );

		/* Bit 1 Zero count is special, it's never latched (although it can activate the latches) */
		RR0_New |= SCC.Chn[ Channel ].RR0_No_Latch & SCC_RR0_BIT_ZERO_COUNT;

		/* Bit 3 : DCD */
		if ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_DCD_INT_ENABLE )
			RR0_New |= SCC.Chn[ Channel ].RR[0] & SCC_RR0_BIT_DCD;
		else
			Update_DCD = true;

		/* Bit 4 : sync hunt */
		if ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_SYNC_HUNT_INT_ENABLE )
			RR0_New |= SCC.Chn[ Channel ].RR[0] & SCC_RR0_BIT_SYNC_HUNT;
		else
			RR0_New |= SCC.Chn[ Channel ].RR0_No_Latch & SCC_RR0_BIT_SYNC_HUNT;

		/* Bit 5 : CTS */
		if ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_CTS_INT_ENABLE )
			RR0_New |= SCC.Chn[ Channel ].RR[0] & SCC_RR0_BIT_CTS;
		else
			Update_CTS = true;

		/* Bit 6 : tx underrun */
		if ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_TX_UNDERRUN_EOM_INT_ENABLE )
			RR0_New |= SCC.Chn[ Channel ].RR[0] & SCC_RR0_BIT_TX_UNDERRUN_EOM;
		else
			RR0_New |= SCC.Chn[ Channel ].RR0_No_Latch & SCC_RR0_BIT_TX_UNDERRUN_EOM;

		/* Bit 7 : break/abort */
		if ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_BREAK_ABORT_INT_ENABLE )
			RR0_New |= SCC.Chn[ Channel ].RR[0] & SCC_RR0_BIT_BREAK_ABORT;
		else
			RR0_New |= SCC.Chn[ Channel ].RR0_No_Latch & SCC_RR0_BIT_BREAK_ABORT;
	}

	if ( Update_CTS )
	{
		RR0_New &= ~SCC_RR0_BIT_CTS;
		if ( SCC_Serial_Get_CTS ( Channel ) )
			RR0_New |= SCC_RR0_BIT_CTS;
	}

	if ( Update_DCD )
	{
		RR0_New &= ~SCC_RR0_BIT_DCD;
		if ( SCC_Serial_Get_DCD ( Channel ) )
			RR0_New |= SCC_RR0_BIT_DCD;
	}

	RR0_Old = SCC.Chn[ Channel ].RR[0];
	SCC.Chn[ Channel ].RR[0] = RR0_New;

	/* Changes in RR0 can set RR3 Ext IP bit */
	/* This can be either for any transition, or only 0 to 1 transition */
	/* IP bits can be reset using "reset command" in WR0 */
	if ( SCC.Chn[Channel].WR[1] & SCC_WR1_BIT_EXT_INT_ENABLE )
	{
		Set_RR3 = false;
		/* Bit 1 ZC : 0->1 */
		if ( ( ( ( RR0_Old & SCC_RR0_BIT_ZERO_COUNT ) == 0 ) && ( ( RR0_New & SCC_RR0_BIT_ZERO_COUNT ) != 0 ) )
			&& ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_ZERO_COUNT_INT_ENABLE ) )
			Set_RR3 = true;
		/* Bit 3 DCD : 0->1 or 1->0 */
		else if ( ( ( RR0_Old & SCC_RR0_BIT_DCD ) != ( RR0_New & SCC_RR0_BIT_DCD ) )
			&& ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_DCD_INT_ENABLE ) )
			Set_RR3 = true;
		/* Bit 4 SYNC : 0->1 or 1->0 (in asynchronous mode) */
		else if ( ( ( RR0_Old & SCC_RR0_BIT_SYNC_HUNT ) != ( RR0_New & SCC_RR0_BIT_SYNC_HUNT ) )
			&& ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_SYNC_HUNT_INT_ENABLE ) )
			Set_RR3 = true;
		/* Bit 5 CTS : 0->1 or 1->0 */
		else if ( ( ( RR0_Old & SCC_RR0_BIT_CTS ) != ( RR0_New & SCC_RR0_BIT_CTS ) )
			&& ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_CTS_INT_ENABLE ) )
			Set_RR3 = true;
		/* Bit 6 TX underrun : 0->1 */
		else if ( ( ( ( RR0_Old & SCC_RR0_BIT_TX_UNDERRUN_EOM ) == 0 ) && ( ( RR0_New & SCC_RR0_BIT_TX_UNDERRUN_EOM ) != 0 ) )
			&& ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_TX_UNDERRUN_EOM_INT_ENABLE ) )
			Set_RR3 = true;
		/* Bit 6 Break/Abord : 0->1 or 1->0 */
		else if ( ( ( RR0_Old & SCC_RR0_BIT_BREAK_ABORT ) != ( RR0_New & SCC_RR0_BIT_BREAK_ABORT ) )
			&& ( SCC.Chn[ Channel ].WR[15] & SCC_WR15_BIT_BREAK_ABORT_INT_ENABLE ) )
			Set_RR3 = true;

		if ( Set_RR3 )
		{
			SCC.Chn[ Channel ].RR0_IsLatched = true;	/* Latch bits in RR0 */

			if ( Channel )
				SCC_Update_RR3_Bit ( 1 , SCC_RR3_BIT_EXT_STATUS_IP_B );
			else
				SCC_Update_RR3_Bit ( 1 , SCC_RR3_BIT_EXT_STATUS_IP_A );
		}
	}

//fprintf ( stderr , "update rr0 %c out=$%02x wr15=$%02x pc=%x\n" , 'A'+Channel , SCC.Chn[Channel].RR[0] , SCC.Chn[Channel].WR[15] , M68000_GetPC() );
}

static void	SCC_Update_RR0_Clear ( int Channel , int bits )
{
	SCC.Chn[ Channel ].RR0_No_Latch &= ~bits;
}

static void	SCC_Update_RR0_Set ( int Channel , int bits )
{
	SCC.Chn[ Channel ].RR0_No_Latch |= bits;
}

static void	SCC_Update_RR0_Latch_Off ( int Channel )
{
	SCC.Chn[ Channel ].RR0_IsLatched = false;
	SCC_Update_RR0 ( Channel );
}



/*
 * Update the content of RR2A and RR2B.
 * This is used when reading RR2A/RR2B or to send the vector during IACK
 */
static void	SCC_Update_RR2 ( void )
{
	uint8_t Vector;
	uint8_t status;


	Vector = SCC.Chn[0].WR[2];
	/* RR2A is WR2 */
	SCC.Chn[0].RR[2] = Vector;

	/* RR2B is WR2 + status bits */
	/* RR2B always include status bit even if SCC_WR9_BIT_VIS is not set */
	status = SCC_Get_Vector_Status ();

	if ( SCC.Chn[0].WR[9] & SCC_WR9_BIT_STATUS_HIGH_LOW )	/* modify high bits */
	{
		status = ( ( status & 1 ) << 2 ) + ( status & 2 ) + ( ( status & 4 ) >> 2 );	/* bits 2,1,0 become bits 0,1,2 */
		Vector &= 0x8f;			/* clear bits 4,5,6 */
		Vector |= ( status << 4 );	/* insert status in bits 4,5,6 */
	}
	else
	{
		Vector &= 0xf1;			/* clear bits 1,2,3 */
		Vector |= ( status << 1 );	/* insert status in bits 1,2,3 */
	}

	SCC.Chn[1].RR[2] = Vector;
}


/*
 * Update the content of RR3A depending on the 3 interrupts sources (RX, TX, Ext)
 * and their corresponding IE bit
 */
static void	SCC_Update_RR3_Bit ( bool Set , uint8_t Bit )
{
	if ( Set )
		SCC.Chn[0].RR[3] |= Bit;
	else
		SCC.Chn[0].RR[3] &= ~Bit;
}

static void	SCC_Update_RR3 ( int Channel )
{
	uint8_t		Set;
	uint8_t		RX_Mode;
	bool		Int_On_RX;
	bool		Int_On_Special;

	/* RR3 depends on some RR0 bits, so update RR0 first */
	SCC_Update_RR0 ( Channel );

//printf ( stderr , "update rr3 %c in=$%02x rr0=$%02x wr15=$%02x ius=$%02x pc=%x\n" , 'A'+Channel , SCC.Chn[0].RR[3] , SCC.Chn[Channel].RR[0] , SCC.Chn[Channel].WR[15] , SCC.IUS , M68000_GetPC() );

	/*
	 * Update RR3 RX bits
	 */
	RX_Mode = ( SCC.Chn[ Channel ].WR[1] >> 3 ) & 0x03;
	Int_On_RX = false;
	Int_On_Special = false;
	if ( RX_Mode != SCC_WR1_RX_MODE_INT_OFF )
		Int_On_Special = true;
	if (   ( RX_Mode == SCC_WR1_RX_MODE_INT_FIRST_CHAR_SPECIAL )
	    || ( RX_Mode == SCC_WR1_RX_MODE_INT_ALL_CHAR_SPECIAL ) )
		Int_On_RX = true;

	if ( ( Int_On_RX && ( SCC.Chn[ Channel ].RR[0] & SCC_RR0_BIT_RX_CHAR_AVAILABLE ) )
	  || ( Int_On_Special && (
			   ( SCC.Chn[ Channel ].RR[1] & SCC_RR1_BIT_RX_OVERRUN_ERROR )
			|| ( SCC.Chn[ Channel ].RR[1] & SCC_RR1_BIT_CRC_FRAMING_ERROR )
			|| ( SCC.Chn[ Channel ].RR[1] & SCC_RR1_BIT_EOF_SDLC )
			|| (   ( SCC.Chn[ Channel ].RR[1] & SCC_RR1_BIT_PARITY_ERROR )
			    && ( SCC.Chn[ Channel ].WR[1] & SCC_WR1_BIT_PARITY_SPECIAL_COND ) )
			) ) )
		Set = 1;
	else
		Set = 0;

	if ( Channel )
		SCC_Update_RR3_Bit ( Set , SCC_RR3_BIT_RX_IP_B );
	else
		SCC_Update_RR3_Bit ( Set , SCC_RR3_BIT_RX_IP_A );


	/*
	 * Update RR3 TX bits (only if Tx buffer is empty but has previously been written to)
	 */
	if ( ( SCC.Chn[ Channel ].RR[0] & SCC_RR0_BIT_TX_BUFFER_EMPTY )
	  && ( SCC.Chn[ Channel ].WR[1] & SCC_WR1_BIT_TX_INT_ENABLE )
	  && ( SCC.Chn[ Channel ].TX_Buffer_Written )
	)
		Set = 1;
	else
		Set = 0;

	if ( Channel )
		SCC_Update_RR3_Bit ( Set , SCC_RR3_BIT_TX_IP_B );
	else
		SCC_Update_RR3_Bit ( Set , SCC_RR3_BIT_TX_IP_A );

//fprintf ( stderr , "update rr3 %c out=$%02x rr0=$%02x wr15=$%02x ius=$%02x pc=%x\n" , 'A'+Channel , SCC.Chn[0].RR[3] , SCC.Chn[Channel].RR[0] , SCC.Chn[Channel].WR[15] , SCC.IUS , M68000_GetPC() );
}


/*
 * Read from data register
 * This function is called either when reading from RR8 or when setting D//C signal to high
 * to read directly from the data register
 */
static uint8_t SCC_ReadDataReg(int chn)
{
	/* NOTE : when reading data reg we consider a 1 byte FIFO instead of the real 3 bytes FIFO */
	/* to simplify processing (see SCC_Process_RX) */
	/* So we clear SCC_RR0_BIT_RX_CHAR_AVAILABLE immediately after reading, but it should be cleared */
	/* when the 3 bytes FIFO is completely empty */
	SCC_Update_RR0_Clear ( chn , SCC_RR0_BIT_RX_CHAR_AVAILABLE );
	SCC_IntSources_Clear ( chn , SCC_INT_SOURCE_RX_CHAR_AVAILABLE );

	return SCC.Chn[chn].RR[8];
}


static uint8_t SCC_ReadControl(int chn)
{
	uint8_t value = 0;
	uint8_t active_reg;

	active_reg = SCC.Active_Reg;

	switch ( active_reg )
	{
	 case 0:	// RR0
	 case 4:	// also returns RR0
		SCC_Update_RR0 ( chn );

		value = SCC.Chn[chn].RR[0];
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d tx/rx buffer status value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 case 1:	// RR1
	 case 5:	// also returns RR1
		value = SCC.Chn[chn].RR[1];
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d special cond status value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 case 2:			/* Return interrupt vector and perform INTACK when in software mode */
		SCC_Update_RR2 ();

		if ( SCC.Chn[0].WR[9] & SCC_WR9_BIT_SOFT_INTACK )
		{
			SCC_Soft_IACK ();
		}

		value = SCC.Chn[chn].RR[2];		/* RR2A or RR2B with status bits */
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d int vector value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 case 3:
		value = chn ? 0 : SCC.Chn[0].RR[3];     /* access on A channel only, return 0 on channel B */
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d interrupt pending value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

//	RR4 : See RR0
//	RR5 : See RR1

//	RR6/RR7 : Low/High bytes of the frame byte count if WR15 bit 2 is set. Else, return RR2/RR3
	 case 6:
	 case 7:
		if ( SCC.Chn[0].WR[15] & SCC_WR15_BIT_STATUS_FIFO_ENABLE )
		{
			value = SCC.Chn[chn].RR[active_reg];	/* for SDLC, not used in Hatari */
			LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d status fifo lsb/msb value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		}
		else
		{
			value = SCC.Chn[0].RR[active_reg-4];	/* return RR2 or RR3 */
			LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d returns RR%d value=$%02x\n" , 'A'+chn , SCC.Active_Reg , active_reg-4 , value );
		}
		break;

	 case 8: // DATA reg
		value = SCC_ReadDataReg ( chn );
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d data reg value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		break;

//	RR9 : See RR13

	 case 10:	// Misc Status Bits
	 case 14:	// also returns RR10
		value = SCC.Chn[chn].RR[10];		/* for DPLL/SDLC, not used in Hatari */
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d dpll/sdlc status value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		break;

//	RR11 : See RR15

	 case 12: // BRG LSB
		value = SCC.Chn[chn].WR[active_reg];
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d baud rate time constant low value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 case 13: // BRG MSB
	 case 9:	// also returns RR13
		value = SCC.Chn[chn].WR[active_reg];
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d baud rate time constant high value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

//	RR14 : See RR10

	 case 15:	// EXT/STATUS IT Ctrl
	 case 11:	// also returns RR15
		value = SCC.Chn[chn].WR[15] &= 0xFA; // mask out D2 and D0
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d ext status IE value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 default:		/* should not happen */
		Log_Printf(LOG_DEBUG, "SCC read : unprocessed read address=$%x\n", active_reg);
		value = 0;
		break;
	}

	LOG_TRACE(TRACE_SCC, "scc read RR%d value=$%02x\n" , active_reg , value );
	return value;
}

static uint8_t SCC_handleRead(uint32_t addr)
{
	uint8_t		value;
	int		Channel;

	Channel = ( addr >> 2 ) & 1;			/* bit 2 : 0 = channel A, 1 = channel B */

	LOG_TRACE(TRACE_SCC, "scc read addr=%x channel=%c VBL=%d HBL=%d pc=%x\n" , addr , 'A'+Channel , nVBLs , nHBL , M68000_GetPC() );

	if ( addr & 2 )					/* bit 1 */
		value = SCC_ReadDataReg(Channel);
	else
		value = SCC_ReadControl(Channel);

	SCC.Active_Reg = 0;				/* Next access default to RR0 or WR0 */

	return value;
}


/*
 * Write to data register
 * This function is called either when writing to WR8 or when setting D//C signal to high
 * to write directly to the data register
 */
static void SCC_WriteDataReg(int chn, uint8_t value)
{
	SCC.Chn[chn].WR[8] = value;
//fprintf(stderr , "scc write_data_reg1 rr0_nolatch=%x rr0=%x written=%d rr1=%x\n" , SCC.Chn[chn].RR0_No_Latch , SCC.Chn[chn].RR[0] , SCC.Chn[ chn ].TX_Buffer_Written , SCC.Chn[chn].RR[1] );

	/* According to the SCC doc, transmit buffer will be copied to the */
	/* transmit shift register TSR after the last bit is shifted out, in ~3 PCLKs */
	/* This means that if transmitter is disabled we can consider TDR is copied */
	/* almost immediately to TSR when writing to WR8 */
	/* Else TDR will be copied during SCC_Process_TX when current transfer is completed */
	/* If this is the first write to WR8 (TX_Buffer_Written==false) we should also consider */
	/* that TDR is copied almost immediately to TSR */
	if ( ( ( SCC.Chn[chn].WR[5] & SCC_WR5_BIT_TX_ENABLE ) == 0 )
	    || ( SCC.Chn[chn].TSR_Full == false ) )
	{
		SCC_Copy_TDR_TSR ( chn , SCC.Chn[chn].WR[8] );
	}
	else
	{
		/* Clear TX Buffer Empty bit and reset TX Interrupt Pending for this channel */
		SCC_Update_RR0_Clear ( chn , SCC_RR0_BIT_TX_BUFFER_EMPTY );
		SCC_IntSources_Clear ( chn , SCC_INT_SOURCE_TX_BUFFER_EMPTY );
	}

	SCC.Chn[chn].TX_Buffer_Written = true;		/* Allow TBE int later if enabled */
//fprintf(stderr , "scc write_data_reg2 rr0_nolatch=%x rr0=%x written=%d rr1=%x\n" , SCC.Chn[chn].RR0_No_Latch , SCC.Chn[chn].RR[0] , SCC.Chn[ chn ].TX_Buffer_Written , SCC.Chn[chn].RR[1] );
}


static void SCC_WriteControl(int chn, uint8_t value)
{
	int i;
	uint8_t command;
	uint8_t bits;

	if ( SCC.Active_Reg == 0 )
	{
		// TODO for later [NP] : according to doc, it should be possible to set register nbr
		// and execute a command at the same time

		/* Bits 0-2 : register nbr */
		/* Bits 3-5 : command */
		/* Bits 6-7 : CRC reset codes */

		if (value <= 15)
		{
			SCC.Active_Reg = value & 0x0f;
			LOG_TRACE(TRACE_SCC, "scc set active reg=R%d\n" , SCC.Active_Reg );
		}
		else
		{
			command = ( value >> 3 ) & 7;

			if ( command == SCC_WR0_COMMAND_NULL )			{}	/* This will select registers 0-7 */
			else if ( command == SCC_WR0_COMMAND_POINT_HIGH )	{}	/* This will select registers 8-15 */

			else if ( command == SCC_WR0_COMMAND_RESET_EXT_STATUS_INT )
			{
				LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d value=$%02x command=reset ext/status int RR3=$%02x IUS=$%02x\n" ,
					'A'+chn , SCC.Active_Reg , value , SCC.Chn[0].RR[3] , SCC.IUS );
				/* Remove latches on RR0 and allow interrupt to happen again */
				if ( chn )
					SCC_Update_RR3_Bit ( 0 , SCC_RR3_BIT_EXT_STATUS_IP_B );
				else
					SCC_Update_RR3_Bit ( 0 , SCC_RR3_BIT_EXT_STATUS_IP_A );
				SCC_Update_RR0_Latch_Off ( chn );
				SCC_Update_RR3 ( chn );
				SCC_Update_IRQ ();
			}

			else if ( command == SCC_WR0_COMMAND_SEND_ABORT )	{}	/* Not emulated */

			else if ( command == SCC_WR0_COMMAND_INT_NEXT_RX )
			{
			}

			else if ( command == SCC_WR0_COMMAND_RESET_TX_IP )
			{
				LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d value=$%02x command=reset tx ip RR3=$%02x IUS=$%02x\n" ,
					'A'+chn , SCC.Active_Reg , value , SCC.Chn[0].RR[3] , SCC.IUS );
				SCC.Chn[chn].TX_Buffer_Written = false;
				if ( chn )
					SCC.Chn[0].RR[3] &= ~SCC_RR3_BIT_TX_IP_B;
				else
					SCC.Chn[0].RR[3] &= ~SCC_RR3_BIT_TX_IP_A;
				SCC_Update_IRQ ();
			}

			else if ( command == SCC_WR0_COMMAND_ERROR_RESET )
			{
				LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d value=$%02x command=reset error RR1=$%02x RR3=$%02x IUS=$%02x\n" ,
					'A'+chn , SCC.Active_Reg , value , SCC.Chn[chn].RR[1] , SCC.Chn[0].RR[3] , SCC.IUS );
				/* Reset error bits in RR1 */
				SCC.Chn[chn].RR[1] &= ~( SCC_RR1_BIT_PARITY_ERROR | SCC_RR1_BIT_RX_OVERRUN_ERROR | SCC_RR1_BIT_CRC_FRAMING_ERROR );
				SCC_IntSources_Clear ( chn , SCC_INT_SOURCE_RX_PARITY_ERROR | SCC_INT_SOURCE_RX_OVERRUN | SCC_INT_SOURCE_RX_FRAMING_ERROR );
			}

			else if ( command == SCC_WR0_COMMAND_RESET_HIGHEST_IUS )
			{
				LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d value=$%02x command=reset highest ius RR3=$%02x IUS=$%02x\n" ,
					'A'+chn , SCC.Active_Reg , value , SCC.Chn[0].RR[3] , SCC.IUS );
				for ( i=5 ; i>=0 ; i-- )
					if ( SCC.IUS & ( 1 << i ) )
					{
						SCC.IUS &= ~( 1 << i );
						break;
					}
				SCC_Update_IRQ ();
			}
		}
		return;
	}

	LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );

	/* Special case for WR7' (Active_Reg=7 and WR15 bit0=1) */
	if ( ( SCC.Active_Reg == 7 ) && ( SCC.Chn[chn].WR[15] & 1 ) )
		SCC.Chn[chn].WR7p = value;
	else
		SCC.Chn[chn].WR[SCC.Active_Reg] = value;


	if (SCC.Active_Reg == 1)		 // Tx/Rx interrupt enable
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set tx/rx int value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );

		/* Update RR3 depending on WR1 then update IRQ state */
		SCC_Update_RR3 ( chn );
		SCC_Update_IRQ ();
	}
	else if (SCC.Active_Reg == 2)		/* Interrupt Vector */
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set int vector value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		SCC.Chn[0].WR[2] = value;		/* WR2 is common to channels A and B, store it in channel A  */
	}
	else if (SCC.Active_Reg == 3)		/*  Receive parameter and control */
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set rx parameter and control value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );

		/* Bit 0 : RX Enable */
		// -> see SCC_Process_RX
		/* Bit 6-7 : RX Bits/char */
		bits = ( value >> 6 ) & 3;
		if ( bits == SCC_WR3_RX_5_BITS )		SCC.Chn[chn].RX_bits = 5;
		else if ( bits == SCC_WR3_RX_6_BITS )		SCC.Chn[chn].RX_bits = 6;
		else if ( bits == SCC_WR3_RX_7_BITS )		SCC.Chn[chn].RX_bits = 7;
		else if ( bits == SCC_WR3_RX_8_BITS )		SCC.Chn[chn].RX_bits = 8;
	}
	else if (SCC.Active_Reg == 4)		/* Tx/Rx misc parameters and modes */
	{
		uint8_t stop_bits;
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set tx/rx stop/parity value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );

		/* Bit 0 : Parity Enable */
		SCC.Chn[chn].Parity_bits = ( value & SCC_WR4_BIT_PARITY_ENABLE ? 1 : 0 );
		/* Bit 2-3 : Parity Mode */
		stop_bits = ( value >> 2 ) & 3;
		if ( stop_bits == SCC_WR4_STOP_SYNC )		SCC.Chn[chn].Stop_bits = 0;
		else if ( stop_bits == SCC_WR4_STOP_1_BIT )	SCC.Chn[chn].Stop_bits = 1;
		else if ( stop_bits == SCC_WR4_STOP_15_BIT )	SCC.Chn[chn].Stop_bits = 1.5;
		else if ( stop_bits == SCC_WR4_STOP_2_BIT )	SCC.Chn[chn].Stop_bits = 2;
		if ( stop_bits != SCC_WR4_STOP_SYNC )		/* asynchronous mode */
			SCC_Update_RR0_Set ( chn , SCC_RR0_BIT_TX_UNDERRUN_EOM );

		SCC_Update_BaudRate ( chn );
	}
	else if (SCC.Active_Reg == 5)		/* Transmit parameter and control */
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set tx parameter and control value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		/* Bit 2 : set RTS */
		SCC_serial_setRTS(chn, value & SCC_WR5_BIT_RTS);
		/* Bit 3 : TX Enable */
		// -> see SCC_Process_TX
		/* Bit 4 : Send Break */
		SCC_serial_Set_BRK(chn, value & SCC_WR5_BIT_SEND_BREAK);
		/* Bit 5-6 : TX Bits/char */
		bits = ( value >> 6 ) & 3;
		if ( bits == SCC_WR5_TX_5_BITS )		SCC.Chn[chn].TX_bits = 5;
		else if ( bits == SCC_WR5_TX_6_BITS )		SCC.Chn[chn].TX_bits = 6;
		else if ( bits == SCC_WR5_TX_7_BITS )		SCC.Chn[chn].TX_bits = 7;
		else if ( bits == SCC_WR5_TX_8_BITS )		SCC.Chn[chn].TX_bits = 8;
		/* Bit 7 : set DTR */
		SCC_serial_setDTR(chn, value & SCC_WR5_BIT_DTR);
	}
	else if (SCC.Active_Reg == 6) // Sync characters high or SDLC Address Field
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set sync hi/sdlc addr value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
	}
	else if (SCC.Active_Reg == 7) // Sync characters low or SDLC flag or WR7'
	{
		if ( SCC.Chn[chn].WR[15] & 1 )
		{
			LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set WR7' value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		}
		else
		{
			LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set sync low/sdlc flag value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		}
	}
	else if (SCC.Active_Reg == 8)
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set transmit buffer value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		SCC_WriteDataReg ( chn , value );
	}
	else if (SCC.Active_Reg == 9) 		/* Master interrupt control (common for both channels) */
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set master control value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		SCC.Chn[0].WR[9] = value;		/* WR9 is common to channels A and B, store it in channel A  */

		/* Bit 0 : VIS, Vector Includes Status */
		/* Bit 1 : NV, No Vector during INTACK */
		/* Bit 2 : Disable Lower Chain (not used in Hatari, there's only 1 SCC) */
		/* Bit 3 : Master Interrupt Enable */
		if ( ( value & SCC_WR9_BIT_MIE ) == 0 )
		{
			/* Clearing MIE reset IUS and IRQ */
			SCC.IUS = 0;
		}
		/* Bit 4 : Status High / Low */
		/* Bit 5 : Software INTACK Enable */
		/* Bits 6-7 : reset command */
		command = ( value >> 6 ) & 3;
		if ( command == SCC_WR9_COMMAND_RESET_FORCE_HW )
		{
			SCC_ResetFull ( false );		/* Force Hardware Reset */
		}
		else if ( command == SCC_WR9_COMMAND_RESET_A )
		{
			SCC_ResetChannel ( 0 , false );		/* Channel A */
		}
		else if ( command == SCC_WR9_COMMAND_RESET_B )
		{
			SCC_ResetChannel ( 1 , false );		/* Channel B */
		}

		SCC_Update_IRQ ();				/* Update IRQ depending on MIE bit */
	}
	else if (SCC.Active_Reg == 10) // Tx/Rx misc control bits
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set tx/rx control bits value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
	}
	else if (SCC.Active_Reg == 11) // Clock Mode Control
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set clock mode control value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		SCC_Update_BaudRate ( chn );
	}
	else if (SCC.Active_Reg == 12) // Lower byte of baud rate
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set baud rate time constant low value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		SCC_Update_BaudRate ( chn );
	}
	else if (SCC.Active_Reg == 13) // set baud rate according to WR13 and WR12
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set baud rate time constant high value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		SCC_Update_BaudRate ( chn );
	}
	else if (SCC.Active_Reg == 14) // Misc Control bits
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set misc control bits value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );
		SCC_Update_BaudRate ( chn );
	}
	else if (SCC.Active_Reg == 15) // external status int control
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set ext status int control value=$%02x\n" , 'A'+chn , SCC.Active_Reg , value );

		/* Bit 0 : Point to Write Register WR7 Prime */
		/* Bit 1 : Zero Count Interrupt Enable ; if 0 then zero count bit must be cleared in RR0 */
		if ( ( value & SCC_WR15_BIT_ZERO_COUNT_INT_ENABLE ) == 0 )
			SCC_Update_RR0_Clear ( chn , SCC_RR0_BIT_ZERO_COUNT );	/* Clear Zero Count bit */
		/* Bit 2 : Status FIFO Enable */
		/* Bit 3 : DCD Int Enable */
		/* Bit 4 : SYNC/Hunt Int Enable */
		/* Bit 5 : CTS Int Enable */
		/* Bit 6 : Transmit Underrun/EOM Int Enable */
		/* Bit 7 : Break/Abort Int Enable */

		/* Update RR3 depending on WR15 then update IRQ state */
		SCC_Update_RR3 ( chn );
		SCC_Update_IRQ ();
	}

	SCC.Active_Reg = 0;			/* next access for RR0 or WR0 */
}

static void SCC_handleWrite(uint32_t addr, uint8_t value)
{
	int 		Channel;

	Channel = ( addr >> 2 ) & 1;			/* bit 2 : 0 = channel A, 1 = channel B */

	LOG_TRACE(TRACE_SCC, "scc write addr=%x channel=%c value=$%02x VBL=%d HBL=%d pc=%x\n" , addr , 'A'+Channel , value , nVBLs , nHBL , M68000_GetPC() );

	if ( addr & 2 )					/* bit 1 */
		SCC_WriteDataReg ( Channel, value );
	else
		SCC_WriteControl ( Channel, value );
}



/*
 * Copy the content of the Transmit Data Register TDR to the Transmit Shift Register TSR
 * and set TX Buffer Empty (TBE) bit
 * According to the SCC doc, transmit buffer will be copied to the
 * transmit shift register TSR after the last bit is shifted out, in ~3 PCLKs
 * and 'all sent' bit will be cleared in RR1
 */

static void	SCC_Copy_TDR_TSR ( int Channel , uint8_t TDR )
{
	SCC.Chn[Channel].TSR = TDR;
	SCC.Chn[Channel].TSR_Full = true;

	/* Clear 'All Sent' bit in RR1 */
	SCC.Chn[Channel].RR[1] &= ~SCC_RR1_BIT_ALL_SENT;	/* new TSR to send */
	/* Set 'TX buffer empty' */
	SCC_Update_RR0_Set ( Channel , SCC_RR0_BIT_TX_BUFFER_EMPTY );
	SCC_IntSources_Set ( Channel , SCC_INT_SOURCE_TX_BUFFER_EMPTY );
}



/* Order of operations :
 *  - check there's no underrun
 *  - send current value of TSR to the emulator's underlying OS
 *  - set 'all sent' bit in RR1
 *  - load new value into TSR by copying TDR / WR8 if transmit buffer is not empty (+clear 'all sent' in RR1)
 * In case of underrun (tx buffer is empty and TSR is empty) then nothing is sent,
 * the transmitter will remain in its latest 'stop bit' state, until a new byte is written in WR8
 */

static void	SCC_Process_TX ( int Channel )
{
//fprintf(stderr , "scc process1 tx rr0_nolatch=%x rr0=%x latched=%d rr1=%x\n" , SCC.Chn[Channel].RR0_No_Latch , SCC.Chn[Channel].RR[0] , SCC.Chn[ Channel ].RR0_IsLatched , SCC.Chn[Channel].RR[1] );

	/* If no new byte was written to the tx buffer and TSR is empty then we have an underrun */
	/* Don't do anything in that case, TxD pin will remain in its latest 'stop bit' state */
	if ( ( SCC.Chn[Channel].RR[0] & SCC_RR0_BIT_TX_BUFFER_EMPTY )
	  && ( SCC.Chn[Channel].TSR_Full == false ) )
		return;

	if ( ( SCC.Chn[Channel].TSR_Full && ( SCC.Chn[Channel].WR[5] & SCC_WR5_BIT_TX_ENABLE ) ) )
	{
		/* Send byte to emulated serial device / file descriptor */
		SCC_Serial_Write_Byte ( Channel, SCC.Chn[Channel].TSR );

		/* All bits of TSR have been sent, TSR is empty */
		SCC.Chn[Channel].TSR_Full = false;
		SCC.Chn[Channel].RR[1] |= SCC_RR1_BIT_ALL_SENT;		/* Set 'All Sent' bit in RR1 */
	}

	/* Prepare TSR for the next call if TX buffer is not empty */
	if ( ( SCC.Chn[Channel].RR[0] & SCC_RR0_BIT_TX_BUFFER_EMPTY ) == 0 )
		SCC_Copy_TDR_TSR ( Channel , SCC.Chn[Channel].WR[8] );
//fprintf(stderr , "scc process2 tx rr0_nolatch=%x rr0=%x latched=%d rr1=%x\n" , SCC.Chn[Channel].RR0_No_Latch , SCC.Chn[Channel].RR[0] , SCC.Chn[ Channel ].RR0_IsLatched , SCC.Chn[Channel].RR[1] );
}



static void	SCC_Process_RX ( int Channel )
{
	uint8_t	rx_byte;

	if ( SCC.Chn[Channel].WR[3] & SCC_WR3_BIT_RX_ENABLE )
	{
		/* Receive byte from emulated serial device / file descriptor */
		if ( SCC_Serial_Read_Byte ( Channel , &rx_byte ) )
		{
			SCC.Chn[Channel].RR[8] = rx_byte;
			if ( SCC.Chn[Channel].RR[0] & SCC_RR0_BIT_RX_CHAR_AVAILABLE )
			{
				/* NOTE : The SCC has a 3 bytes deep FIFO, so we should have an overrun */
				/* when we receive a new byte and all 3 bytes were not read so far */
				/* In Hatari's case we simplify this condition by using a 1 byte FIFO, */
				/* so any new char received when latest char was not read will set */
				/* the overrun bit (this can be improved later if needed) */
				SCC.Chn[Channel].RR[1] |= SCC_RR1_BIT_RX_OVERRUN_ERROR;
				SCC_IntSources_Set ( Channel , SCC_INT_SOURCE_RX_OVERRUN );
			}
			else
			{
				SCC_Update_RR0_Set ( Channel , SCC_RR0_BIT_RX_CHAR_AVAILABLE );
				SCC_IntSources_Set ( Channel , SCC_INT_SOURCE_RX_CHAR_AVAILABLE );
			}
		}
	}
}


/*
 * Start the internal interrupt handler for SCC A or B when the baud rate generator is enabled
 */

static void	SCC_Start_InterruptHandler_BRG ( int Channel , int InternalCycleOffset )
{
	int	IntHandler;
	int	Cycles;

	if ( Channel == 0 )
		IntHandler = INTERRUPT_SCC_BRG_A;
	else
		IntHandler = INTERRUPT_SCC_BRG_B;

	Cycles = MachineClocks.CPU_Freq / SCC.Chn[Channel].BaudRate_BRG;	/* Convert baud rate in CPU cycles */

	LOG_TRACE ( TRACE_SCC, "scc start interrupt handler brg channel=%c baudrate=%d cpu_cycles=%d VBL=%d HBL=%d\n" ,
		'A'+Channel , SCC.Chn[Channel].BaudRate_BRG , Cycles , nVBLs , nHBL );

	CycInt_AddRelativeInterruptWithOffset ( Cycles, INT_CPU_CYCLE, IntHandler, InternalCycleOffset );
}


/*
 * Stop the internal interrupt handler for SCC A or B when the baud rate generator is disabled
 */

static void	SCC_Stop_InterruptHandler_BRG ( int Channel )
{
	int	IntHandler;

	if ( Channel == 0 )
		IntHandler = INTERRUPT_SCC_BRG_A;
	else
		IntHandler = INTERRUPT_SCC_BRG_B;

	CycInt_RemovePendingInterrupt ( IntHandler );
}


/*
 * Interrupt called each time the baud rate generator's counter reaches 0
 * We continuously restart the interrupt, taking into account PendingCyclesOver.
 */

static void	SCC_InterruptHandler_BRG ( int Channel )
{
	int	PendingCyclesOver;


	/* Number of internal cycles we went over for this timer ( <= 0 ) */
	/* Used to restart the next timer and keep a constant baud rate */
	PendingCyclesOver = -PendingInterruptCount;			/* >= 0 */

	LOG_TRACE ( TRACE_SCC, "scc interrupt handler brg channel=%c pending_cyc=%d VBL=%d HBL=%d\n" , 'A'+Channel , PendingCyclesOver , nVBLs , nHBL );

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	SCC_Start_InterruptHandler_BRG ( Channel , -PendingCyclesOver );	/* Compensate for a != 0 value of PendingCyclesOver */

	/* BRG counter reached 0, check if corresponding interrupt pending bit must be set in RR3 */
	/* NOTE : we should set bit ZERO_COUNT in RR0 here, but we don't support resetting it later */
	/* as this would require to emulate BRG on every count, which would slow down emulation too much */
	/* Instead, we set ZC bit, update irq, then clear ZC bit just after, which should give */
	/* a close enough result as real HW */
	SCC_Update_RR0_Set ( Channel , SCC_RR0_BIT_ZERO_COUNT );
	SCC_IntSources_Set ( Channel , SCC_INT_SOURCE_EXT_ZERO_COUNT );
	SCC_Update_RR0_Clear ( Channel , SCC_RR0_BIT_ZERO_COUNT );
	SCC_IntSources_Clear_NoUpdate ( Channel , SCC_INT_SOURCE_EXT_ZERO_COUNT );
}


void	SCC_InterruptHandler_BRG_A ( void )
{
	SCC_InterruptHandler_BRG ( 0 );
}


void	SCC_InterruptHandler_BRG_B ( void )
{
	SCC_InterruptHandler_BRG ( 1 );
}



/*
 * Start the TX or RX internal cycint timer
 * NOTE : instead of having a cycint interrupt on every bit, we trigger the cycint interrupt
 * only when 1 char has been sent/received, taking into account all start/parity/stop bits
 * Although not fully accurate this will give a good timing to emulate when RX buffer is full
 * or when TX buffer is empty (and set the corresponding status bits in RRx) and this will
 * lower the cpu usage on the host running the emulation (as baudrate can be rather high with the SCC)
 */

static void	SCC_Start_InterruptHandler_TX_RX ( int Channel , bool is_tx , int InternalCycleOffset )
{
	int	IntHandler;
	int	Cycles;
	int	BaudRate;
	float	Char_Total_Bits;	/* Total number of bits for 1 char, including start/stop/parity bits */

	if ( is_tx )
	{
		BaudRate = SCC.Chn[Channel].BaudRate_TX;
		Char_Total_Bits = SCC.Chn[Channel].TX_bits;
		if ( Channel == 0 )
			IntHandler = INTERRUPT_SCC_TX_RX_A;
		else
			IntHandler = INTERRUPT_SCC_TX_RX_B;
	}
	else
	{
		BaudRate = SCC.Chn[Channel].BaudRate_RX;
		Char_Total_Bits = SCC.Chn[Channel].RX_bits;
		if ( Channel == 0 )
			IntHandler = INTERRUPT_SCC_RX_A;
		else
			IntHandler = INTERRUPT_SCC_RX_B;
	}

	/* Number of cycles to send/receive 1 bit */
	Cycles = MachineClocks.CPU_Freq / BaudRate;		/* Convert baud rate in CPU cycles */

	/* Take start bit (=1), parity bit, stop bits and data bits into account */
	/* to get the total number of bits to send/receive 1 char */
	Char_Total_Bits += 1 + SCC.Chn[Channel].Parity_bits + SCC.Chn[Channel].Stop_bits;

	/* Get the total number of cycles to send/receive all the needed bits for 1 char */
	Cycles = Cycles * Char_Total_Bits;

	LOG_TRACE ( TRACE_SCC, "scc start interrupt handler %s channel=%c total_bits=%.1f baudrate=%d cpu_cycles=%d VBL=%d HBL=%d\n" ,
		is_tx?"tx":"rx" , 'A'+Channel , Char_Total_Bits , SCC.Chn[Channel].BaudRate_BRG , Cycles , nVBLs , nHBL );

	CycInt_AddRelativeInterruptWithOffset ( Cycles, INT_CPU_CYCLE, IntHandler, InternalCycleOffset );
}


/*
 * Stop the internal interrupt handler for SCC A or B
 */

static void	SCC_Stop_InterruptHandler_TX_RX ( int Channel , bool is_tx )
{
	int	IntHandler;

	if ( is_tx )
	{
		if ( Channel == 0 )
			IntHandler = INTERRUPT_SCC_TX_RX_A;
		else
			IntHandler = INTERRUPT_SCC_TX_RX_B;
	}
	else
	{
		if ( Channel == 0 )
			IntHandler = INTERRUPT_SCC_RX_A;
		else
			IntHandler = INTERRUPT_SCC_RX_B;
	}

	LOG_TRACE ( TRACE_SCC, "scc stop interrupt handler %s channel=%c VBL=%d HBL=%d\n" ,
		is_tx?"tx":"rx" , 'A'+Channel , nVBLs , nHBL );

	CycInt_RemovePendingInterrupt ( IntHandler );
}


static void	SCC_Restart_InterruptHandler_TX_RX ( int Channel , bool is_tx )
{
	int	PendingCyclesOver;


	/* Number of internal cycles we went over for this timer ( <= 0 ) */
	/* Used to restart the next timer and keep a constant baud rate */
	PendingCyclesOver = -PendingInterruptCount;			/* >= 0 */

	LOG_TRACE ( TRACE_SCC, "scc interrupt handler %s channel=%c pending_cyc=%d VBL=%d HBL=%d\n" , is_tx?"tx":"rx" , 'A'+Channel , PendingCyclesOver , nVBLs , nHBL );

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	SCC_Start_InterruptHandler_TX_RX ( Channel , is_tx , -PendingCyclesOver );	/* Compensate for a != 0 value of PendingCyclesOver */
}



static void	SCC_InterruptHandler_TX_RX ( int Channel )
{
	SCC_Restart_InterruptHandler_TX_RX ( Channel , true );

	SCC_Process_TX ( Channel );

	/* If TX and RX use the same baudrate, we process RX here too */
	/* (avoid using an additional cycint timer for RX) */
	if ( SCC.Chn[Channel].BaudRate_TX == SCC.Chn[Channel].BaudRate_RX )
		SCC_Process_RX ( Channel );
}

void	SCC_InterruptHandler_TX_RX_A ( void )
{
	SCC_InterruptHandler_TX_RX ( 0 );
}

void	SCC_InterruptHandler_TX_RX_B ( void )
{
	SCC_InterruptHandler_TX_RX ( 1 );
}



static void	SCC_InterruptHandler_RX ( int Channel )
{
	SCC_Restart_InterruptHandler_TX_RX ( Channel , false );

	SCC_Process_RX ( Channel );
}

void	SCC_InterruptHandler_RX_A ( void )
{
	SCC_InterruptHandler_RX ( 0 );
}

void	SCC_InterruptHandler_RX_B ( void )
{
	SCC_InterruptHandler_RX ( 1 );
}


/*-----------------------------------------------------------------------*/
/**
 * Set or reset the SCC's IRQ signal.
 * IRQ signal is inverted (0/low sets irq, 1/high clears irq)
 * On Falcon, SCC's INT pin is connected to COMBEL EINT5
 * On MegaSTE and TT, SCC's INT pin is connected to TTSCU XSCCIRQ/SIR5
 */
static void     SCC_Set_Line_IRQ ( int bit )
{
        LOG_TRACE ( TRACE_SCC, "scc set irq line val=%d VBL=%d HBL=%d\n" , bit , nVBLs , nHBL );

	SCC.IRQ_Line = bit;

        M68000_Update_intlev ();
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of the SCC's IRQ signal.
 * IRQ signal is inverted (0/low sets irq, 1/high clears irq)
 */
int	SCC_Get_Line_IRQ ( void )
{
	return SCC.IRQ_Line;
}



/*
 * Check if Master Interrupt is enabled and if any IP bits are set in RR3A (and not lower than IUS
 * Update main IRQ line accordingly
 */
static void	SCC_Update_IRQ ( void )
{
	int	IRQ_new;
	int	i;

//fprintf ( stderr , "scc update irq wr9=$%02x ius=$%02x rr3=$%02x irq_in=%d pc=%x\n" , SCC.Chn[0].WR[9] , SCC.IUS , SCC.Chn[0].RR[3] , SCC.IRQ_Line , M68000_GetPC() );
	if ( SCC.Chn[0].WR[9] & SCC_WR9_BIT_MIE )	/* Master Interrupt enabled */
	{
		/* Check if there's an IP bit set and not lower than IUS */
		IRQ_new = SCC_IRQ_OFF;
		for ( i=5 ; i>=0 ; i-- )		/* Test from higher to lower IP */
		{
			if ( SCC.IUS & ( 1 << i ) )	/* IUS bit set */
			{
				IRQ_new = SCC_IRQ_OFF;
				break;
			}
			else if ( SCC.Chn[0].RR[3] & ( 1 << i ) )	/* IP bit set and IUS bit not set */
			{
				IRQ_new = SCC_IRQ_ON;
				break;
			}
		}
	}
	else
		IRQ_new = SCC_IRQ_OFF;

//fprintf ( stderr , "scc update irq wr9=$%02x ius=$%02x rr3=$%02x irq_out=%d pc=%x\n" , SCC.Chn[0].WR[9] , SCC.IUS , SCC.Chn[0].RR[3] , IRQ_new , M68000_GetPC() );
	/* Update IRQ line if needed */
	if ( IRQ_new != SCC.IRQ_Line )
		SCC_Set_Line_IRQ ( IRQ_new );
}


/*
 * Set/Clear some interrupt sources for channel A or B
 * Update RR3A and set IRQ Low/High depending on the result
 */

static void	SCC_IntSources_Change ( int Channel , uint32_t Sources , bool Set )
{
//fprintf ( stderr,  "scc int source %d old=%x new=%x %d pc=%x\n" , Channel , SCC.Chn[ Channel ].IntSources , Sources , Set , M68000_GetPC() );
	if ( Set )
	{
		/* Don't do anything if all bits from Sources are already set */
		if ( ( SCC.Chn[ Channel ].IntSources & Sources ) == Sources )
			return;			/* No change */
	}
	else
	{
		/* Don't do anything if all bits from Sources are already cleared */
		if ( ( SCC.Chn[ Channel ].IntSources & Sources ) == 0 )
			return;			/* No change */
	}

	SCC_Update_RR3 ( Channel );

	if ( Set )
		SCC.Chn[ Channel ].IntSources |= Sources;
	else
		SCC.Chn[ Channel ].IntSources &= ~Sources;


	/* Set IRQ Low/high depending on RR3A */
	SCC_Update_IRQ ();
}


/*
 * Shortcut function to set an interrupt source
 */
static void	SCC_IntSources_Set ( int Channel , uint32_t Sources )
{
	SCC_IntSources_Change ( Channel , Sources , true );
}

/*
 * Shortcut function to clear an interrupt source
 */
static void	SCC_IntSources_Clear ( int Channel , uint32_t Sources )
{
	SCC_IntSources_Change ( Channel , Sources , false );
}

/*
 * Just clear bits in interrupts sources, don't update RR3 and IRQ
 */
static void	SCC_IntSources_Clear_NoUpdate ( int Channel , uint32_t Sources )
{
	SCC.Chn[ Channel ].IntSources &= ~Sources;
}


/*
 * Do the IACK sequence (common to the software IACK or the hardware IACK) :
 *  - clear IRQ
 *  - set IUS bit for the pending interrupt
 *  - return an interrupt vector (only used for hardware IACK)
 */
static int	SCC_Do_IACK ( bool Soft )
{
	int	Vector;
	int	i;

	SCC_Set_Line_IRQ ( SCC_IRQ_OFF );

	/* Set IUS bit corresponding to highest IP */
	for ( i=5 ; i>=0 ; i-- )		/* Test from higher to lower IP */
	{
		if ( SCC.Chn[0].RR[3] & ( 1 << i ) )
		{
			SCC.IUS |= ( 1 << i );
			break;
		}
	}

	SCC_Update_RR2 ();
	if ( SCC.Chn[0].WR[9] & SCC_WR9_BIT_VIS )
		Vector = SCC.Chn[1].RR[2];	/* RR2B including status bits */
	else
		Vector = SCC.Chn[0].RR[2];	/* RR2B without status bits */
	return Vector;
}


/*
 * Called when software IACK is enabled and reading from RR2
 */

static void	SCC_Soft_IACK ( void )
{
	SCC_Do_IACK ( true );
}


/*
 * Called by the CPU when processing interrupts (see iack_cycle() in newcpu.c)
 */
int	SCC_Process_IACK ( void )
{
	int	Vector;

	Vector = SCC_Do_IACK ( false );
	if ( SCC.Chn[0].WR[9] & SCC_WR9_BIT_NV )
		return -1;			/* IACK is disabled, no vector */

//fprintf ( stderr , "scc iack %d\n" , Vector );
	return Vector;
}



void SCC_IoMem_ReadByte(void)
{
	int i;

	for (i = 0; i < nIoMemAccessSize; i++)
	{
		uint32_t addr = IoAccessBaseAddress + i;
		if (addr & 1)
			IoMem[addr] = SCC_handleRead(addr);
		else
			IoMem[addr] = 0xff;
	}
}

void SCC_IoMem_WriteByte(void)
{
	int i;

	for (i = 0; i < nIoMemAccessSize; i++)
	{
		uint32_t addr = IoAccessBaseAddress + i;
		if (addr & 1)
			SCC_handleWrite(addr, IoMem[addr]);
	}
}

void SCC_Info(FILE *fp, uint32_t dummy)
{
	unsigned int i, reg;

	fprintf(fp, "SCC common:\n");
	fprintf(fp, "- IRQ_Line: %d (%s)\n", SCC.IRQ_Line , SCC.IRQ_Line==SCC_IRQ_ON ? "ON" : "OFF" );
	fprintf(fp, "- IUS: %02x\n", SCC.IUS);
	fprintf(fp, "- Active register: %d\n", SCC.Active_Reg);

	for (i = 0; i < 2; i++)
	{
		fprintf(fp, "\nSCC %c:\n", 'A' + i);
		fprintf(fp, "- Write Registers:\n");
		for (reg = 0; reg < ARRAY_SIZE(SCC.Chn[i].WR); reg++)
			fprintf(fp, "  %02x", SCC.Chn[i].WR[reg]);
		fprintf(fp, "  WR7'=%02x\n" , SCC.Chn[i].WR7p );

		fprintf(fp, "- Read Registers:\n");
		for (reg = 0; reg < ARRAY_SIZE(SCC.Chn[i].RR); reg++)
			fprintf(fp, "  %02x", SCC.Chn[i].RR[reg]);
		fprintf(fp, "\n");

		fprintf(fp, "- Device's file is %s TTY\n", SCC.Chn[i].FileHandle_IsATTY ? "a" : "not a");
	}
}
