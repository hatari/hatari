/*
 * scc.c - SCC 85C30 emulation code
 *
 * Adaptions to Hatari:
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
   SCC port A : 1 RS422 LAN port (MiniDIN, 8 pins) and 1 RS232C serial port A (DB-9P, 9 pins)
   SCC port B : 1 RS232C serial port B (DP-9P, 9 pins)
   - PCLK : connected to CLK8, 8021247 Hz for PAL
   - RTxCA and RTxCB : connected to PCLK4, dedicated OSC running at 3.672 MHz
   - TRxCA : connected to LCLK : SYNCI signal on pin 2 of the LAN connector or pin 6 of Serial port A
   - TRxCB : connected to BCLK, dedicated OSC running at 2.4576 MHz for the MFP's XTAL1

  TT :
   SCC port A : 1 RS422 LAN port (MiniDIN, 8 pins) and 1 RS232C serial port A (DB-9P, 9 pins)
   SCC port B : 1 RS232C serial port B (DP-9P, 9 pins)
   - PCLK : connected to CLK8, 8021247 Hz for PAL
   - RTxCA : connected to PCLK4, dedicated OSC running at 3.672 MHz
   - TRxCA : connected to LCLK : SYNCI signal on pin 2 of the LAN connector or pin 6 of Serial port A
   - RTxCB : connected to TCCLK on the TT-MFP (Timer C output)
   - TRxCB : connected to BCLK, dedicated OSC running at 2.4576 MHz for the 2 MFPs' XTAL1

  Falcon :
   SCC port A : 1 RS422 LAN port (MiniDIN, 8 pins)
   SCC port B : 1 RS232C serial port B (DP-9P, 9 pins)
   - PCLK : connected to CLK8, 8021247 Hz for PAL
   - RTxCA and RTxCB : connected to PCLK4, dedicated OSC running at 3.672 MHz
   - TRxCA : connected to SYNCA on the SCC
   - TRxCB : connected to BCLKA, dedicated OSC running at 2.4576 MHz for the MFP's XTAL1


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
#include "cycles.h"
#include "cycInt.h"
#include "video.h"

#ifndef O_NONBLOCK
# ifdef O_NDELAY
#  define O_NONBLOCK O_NDELAY
# else
#  define O_NONBLOCK 0
# endif
#endif

#define RCA 0
#define TBE 2
#define CTS 5


#define SCC_CLOCK_PCLK		MachineClocks.SCC_Freq		/* 8021247 Hz */
#define SCC_CLOCK_PCLK4		3672000				/* Dedicated OSC */
#define SCC_CLOCK_BCLK		2457600				/* Connected to the MFP's XTAL clock */

#define SCC_BAUDRATE_SOURCE_CLOCK_RTXC		0
#define SCC_BAUDRATE_SOURCE_CLOCK_TRXC		1
#define SCC_BAUDRATE_SOURCE_CLOCK_BRG		2
#define SCC_BAUDRATE_SOURCE_CLOCK_DPLL		3

#define SCC_BAUDRATE_SOURCE_CLOCK_PCLK		1
#define SCC_BAUDRATE_SOURCE_CLOCK_PCLK4		2
#define SCC_BAUDRATE_SOURCE_CLOCK_BCLK		3
#define SCC_BAUDRATE_SOURCE_CLOCK_TCCLK		4


#define SCC_WR1_BIT_EXT_INT_ENABLE		0x01	/* Ext Int Enable */

#define SCC_WR9_BIT_VIS				0x01	/* Vector Includes Status */
#define SCC_WR9_BIT_NV				0x02	/* No Vector */
#define SCC_WR9_BIT_DISABLE_LOWER_CHAIN		0x04	/* Disable Lower Chain */
#define SCC_WR9_BIT_DISABLE_LOWER_CHAIN		0x04	/* Disable Lower Chain */
#define SCC_WR9_BIT_MIE				0x08	/* Master Interrupt Enable */
#define SCC_WR9_BIT_STATUS_HIGH_LOW		0x10	/* Master Interrupt Enable */
#define SCC_WR9_BIT_SOFT_INTACK			0x20	/* Software INTACK Enable */
/* Commands for the bits 7-8 of WR9 */
#define SCC_WR9_COMMAND_RESET_NULL		0x00	/* Null Command */
#define SCC_WR9_COMMAND_RESET_B			0x01	/* Channel B Reset */
#define SCC_WR9_COMMAND_RESET_A			0x02	/* Channel A Reset */
#define SCC_WR9_COMMAND_RESET_FORCE_HW		0x03	/* Force Hardware Reset */

#define SCC_WR15_BIT_ZERO_COUNT_IE		0x02	/* Zero Count Interrupt Enable */


/* CRC Reset codes 6-7 of RR0 */
#define SCC_RR0_COMMAND_CRC_NULL		0x00	/* Null command */
#define SCC_RR0_COMMAND_CRC_RESET_RX		0x01	/* Reset Receive CRC Checker */
#define SCC_RR0_COMMAND_CRC_RESET_TX		0x02	/* Reset Transmit CRC Generator */
#define SCC_RR0_COMMAND_CRC_RESET_TX_UNDERRUN	0x03	/* Reset Transmit Underrun/EOM Latch */

/* Commands for the bits 3-5 of RR0 */
#define SCC_RR0_COMMAND_NULL			0x00	/* Null Command */
#define SCC_RR0_COMMAND_POINT_HIGH		0x01	/* Point High */
#define SCC_RR0_COMMAND_RESET_EXT_STATUS_INT	0x02	/* Null Command */
#define SCC_RR0_COMMAND_SEND_ABORT		0x03	/* Send Abort */
#define SCC_RR0_COMMAND_INT_NEXT_RX		0x04	/* Enable Interrupt on Next Rx Char */
#define SCC_RR0_COMMAND_RESET_TX_IP		0x05	/* Reset Tx Interrupt pending */
#define SCC_RR0_COMMAND_ERROR_RESET		0x06	/* Error Reset */
#define SCC_RR0_COMMAND_RESET_HIGHEST_IUS	0x07	/* Reset Highest IUS */

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
	/* IUS is common to both channels, we store it in channel A */
	/* Also special case for WR7', we store it in reg 16 */
	uint8_t	WR[16+1];		/* 0-15 are for WR0-WR15, 16 is for WR7' */
	uint8_t	RR[16];			/* 0-15 are for RR0-RR15 */

	int	Active_Reg;
	int	BaudRate_BRG;

	int	charcount;
	int	rd_handle, wr_handle;
	uint16_t oldTBE;
	uint16_t oldStatus;
	bool	bFileHandleIsATTY;
};

typedef struct {
	struct SCC_Channel chn[2];	/* 0 is for channel A, 1 is for channel B */

	uint8_t		IRQ_Line;
	uint8_t		IUS;		/* Interrupt Under Service (same bits as RR3 bits 0-5) */
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
	2303400
};


/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static void	SCC_ResetChannel ( int Channel , bool HW_Reset );
static void	SCC_ResetFull ( bool HW_Reset );

static void	SCC_Start_InterruptHandler ( int channel , int InternalCycleOffset );
static void	SCC_Stop_InterruptHandler ( int channel );
static void	SCC_InterruptHandler ( int channel );
static void	SCC_Update_IRQ ( int channel );


bool SCC_IsAvailable(CNF_PARAMS *cnf)
{
	return ConfigureParams.System.nMachineType == MACHINE_MEGA_STE
	       || ConfigureParams.System.nMachineType == MACHINE_TT
	       || ConfigureParams.System.nMachineType == MACHINE_FALCON;
}

void SCC_Init(void)
{
	SCC_Reset();

	SCC.chn[0].oldTBE = SCC.chn[1].oldTBE = 0;
	SCC.chn[0].oldStatus = SCC.chn[1].oldStatus = 0;

	SCC.chn[0].rd_handle = SCC.chn[0].wr_handle = -1;
	SCC.chn[1].rd_handle = SCC.chn[1].wr_handle = -1;

	if (!ConfigureParams.RS232.bEnableSccB || !SCC_IsAvailable(&ConfigureParams))
		return;

	if (ConfigureParams.RS232.sSccBInFileName[0] &&
	    strcmp(ConfigureParams.RS232.sSccBInFileName, ConfigureParams.RS232.sSccBOutFileName) == 0)
	{
#if HAVE_TERMIOS_H
		SCC.chn[1].rd_handle = open(ConfigureParams.RS232.sSccBInFileName, O_RDWR | O_NONBLOCK);
		if (SCC.chn[1].rd_handle >= 0)
		{
			if (isatty(SCC.chn[1].rd_handle))
			{
				SCC.chn[1].wr_handle = SCC.chn[1].rd_handle;
			}
			else
			{
				Log_Printf(LOG_ERROR, "SCC_Init: Setting SCC-B input and output "
				           "to the same file only works with tty devices.\n");
				close(SCC.chn[1].rd_handle);
				SCC.chn[1].rd_handle = -1;
			}
		}
		else
		{
			Log_Printf(LOG_ERROR, "SCC_Init: Can not open device '%s'\n",
			           ConfigureParams.RS232.sSccBInFileName);
		}
#else
		Log_Printf(LOG_ERROR, "SCC_Init: Setting SCC-B input and output "
		           "to the same file is not supported on this system.\n");
#endif
	}
	else
	{
		if (ConfigureParams.RS232.sSccBInFileName[0])
		{
			SCC.chn[1].rd_handle = open(ConfigureParams.RS232.sSccBInFileName, O_RDONLY | O_NONBLOCK);
			if (SCC.chn[1].rd_handle < 0)
			{
				Log_Printf(LOG_ERROR, "SCC_Init: Can not open input file '%s'\n",
					   ConfigureParams.RS232.sSccBInFileName);
			}
		}
		if (ConfigureParams.RS232.sSccBOutFileName[0])
		{
			SCC.chn[1].wr_handle = open(ConfigureParams.RS232.sSccBOutFileName,
						O_CREAT | O_WRONLY | O_NONBLOCK, S_IRUSR | S_IWUSR);
			if (SCC.chn[1].wr_handle < 0)
			{
				Log_Printf(LOG_ERROR, "SCC_Init: Can not open output file '%s'\n",
					   ConfigureParams.RS232.sSccBOutFileName);
			}
		}
	}
	if (SCC.chn[1].rd_handle == -1 && SCC.chn[1].wr_handle == -1)
	{
		ConfigureParams.RS232.bEnableSccB = false;
	}
}

void SCC_UnInit(void)
{
	if (SCC.chn[1].rd_handle >= 0)
	{
		if (SCC.chn[1].wr_handle == SCC.chn[1].rd_handle)
			SCC.chn[1].wr_handle = -1;
		close(SCC.chn[1].rd_handle);
		SCC.chn[1].rd_handle = -1;
	}
	if (SCC.chn[1].wr_handle >= 0)
	{
		close(SCC.chn[1].wr_handle);
		SCC.chn[1].wr_handle = -1;
	}
}

void SCC_MemorySnapShot_Capture(bool bSave)
{
	for (int c = 0; c < 2; c++)
	{
		MemorySnapShot_Store(SCC.chn[c].WR, sizeof(SCC.chn[c].WR));
		MemorySnapShot_Store(SCC.chn[c].RR, sizeof(SCC.chn[c].RR));
		MemorySnapShot_Store(&SCC.chn[c].Active_Reg, sizeof(SCC.chn[c].Active_Reg));
		MemorySnapShot_Store(&SCC.chn[c].BaudRate_BRG, sizeof(SCC.chn[c].BaudRate_BRG));
		MemorySnapShot_Store(&SCC.chn[c].charcount, sizeof(SCC.chn[c].charcount));
		MemorySnapShot_Store(&SCC.chn[c].oldTBE, sizeof(SCC.chn[c].oldTBE));
		MemorySnapShot_Store(&SCC.chn[c].oldStatus, sizeof(SCC.chn[c].oldStatus));
	}

	MemorySnapShot_Store(&SCC.IRQ_Line, sizeof(SCC.IRQ_Line));
	MemorySnapShot_Store(&SCC.IUS, sizeof(SCC.IUS));
}



static void SCC_ResetChannel ( int Channel , bool HW_Reset )
{
	SCC.chn[Channel].WR[0] = 0x00;
	SCC.chn[Channel].Active_Reg = 0x00;
	SCC.chn[Channel].WR[1] &= 0x24;			/* keep bits 2 and 5, clear others */
	SCC.chn[Channel].WR[3] &= 0xfe;			/* keep bits 1 to 7, clear bit 0 */
	SCC.chn[Channel].WR[4] |= 0x04;			/* set bit 2, keep others */
	SCC.chn[Channel].WR[5] &= 0x61;			/* keep bits 0,5 and 6, clear others */
	SCC.chn[Channel].WR[15] = 0xf8;
	SCC.chn[Channel].WR[16] = 0x20;			/* WR7' set bit5, clear others */

	if ( HW_Reset )
	{
		/* WR9 is common to channel A and B, we store it in channel A */
		SCC.chn[0].WR[9] &= 0x03;		/* keep bits 0 and 1, clear others */
		SCC.chn[0].WR[9] |= 0xC0;		/* set bits 7 and 8 */
		SCC.IUS = 0x00;				/* clearing MIE also clear IUS */

		SCC.chn[Channel].WR[10] = 0x00;
		SCC.chn[Channel].WR[11] = 0x08;
		SCC.chn[Channel].WR[14] &= 0xC0;	/* keep bits 7 and 8, clear others */
		SCC.chn[Channel].WR[14] |= 0x30;	/* set bits 4 and 5 */
	}
	else
	{
		/* WR9 is common to channel A and B, we store it in channel A */
		SCC.chn[0].WR[9] &= 0xdf;		/* clear bit 6, keep others */

		SCC.chn[Channel].WR[10] &= 0x60;	/* keep bits 5 and 6, clear others */
		SCC.chn[Channel].WR[14] &= 0xC3;	/* keep bits 0,1,7 and 8, clear others */
		SCC.chn[Channel].WR[14] |= 0x20;	/* set bit 5 */
	}


	SCC.chn[Channel].RR[0] &= 0xb8;			/* keep bits 3,4 and 5, clear others */
	SCC.chn[Channel].RR[0] |= 0x44;			/* set bits 2 and 6 */
	SCC.chn[Channel].RR[1] &= 0x01;			/* keep bits 0, clear others */
	SCC.chn[Channel].RR[1] |= 0x06;			/* set bits 1 and 2 */
	SCC.chn[Channel].RR[3] = 0x00;
	SCC.chn[Channel].RR[10] &= 0x40;		/* keep bits 6, clear others */
}



/* On real hardware HW_Reset would be true when /RD and /WR are low at the same time (not supported in Mega STE / TT / Falcon)
 *  - For our emulation, we also do HW_Reset=true when resetting the emulated machine
 *  - When writing 0xC0 to WR9 a full reset will be done with HW_Reset=false
 */
static void SCC_ResetFull ( bool HW_Reset )
{
	uint8_t wr9_old;

	wr9_old = SCC.chn[0].WR[9];		/* Save WR9 before reset */

	SCC_ResetChannel ( 0 , true );
	SCC_ResetChannel ( 1 , true );

	if ( !HW_Reset )			/* Reset through WR9, some bits are kept */
	{
		/* Restore bits 2,3,4 after software full reset */
		SCC.chn[0].WR[9] = wr9_old & 0x1c;
	}

	// TODO : reset IRQ_Line
}


static void SCC_channelAreset(void)
{
	LOG_TRACE(TRACE_SCC, "SCC: reset channel A\n");
	SCC.chn[0].WR[15] = 0xF8;
	SCC.chn[0].WR[14] = 0xA0;
	SCC.chn[0].WR[11] = 0x08;
	SCC.chn[0].WR[9] = 0;

	SCC.chn[0].WR[0] = 1 << TBE;  // RR0A	// TODO NP
}

static void SCC_channelBreset(void)
{
	LOG_TRACE(TRACE_SCC, "SCC: reset channel B\n");
	SCC.chn[1].WR[15] = 0xF8;
	SCC.chn[1].WR[14] = 0xA0;
	SCC.chn[1].WR[11] = 0x08;
	SCC.chn[0].WR[9] = 0;         // single WR9

	SCC.chn[1].WR[0] = 1 << TBE;  // RR0B	// TODO NP
}

void SCC_Reset(void)
{
	memset(SCC.chn[0].WR, 0, sizeof(SCC.chn[0].WR));
	memset(SCC.chn[0].RR, 0, sizeof(SCC.chn[0].RR));
	memset(SCC.chn[1].WR, 0, sizeof(SCC.chn[1].WR));
	memset(SCC.chn[1].RR, 0, sizeof(SCC.chn[1].RR));

	SCC_ResetFull ( true );

	SCC.chn[0].charcount = SCC.chn[1].charcount = 0;
}

static void TriggerSCC(bool enable)
{
	if (enable)
	{
		Log_Printf(LOG_TODO, "TriggerSCC\n");
	}
}

static uint8_t SCC_serial_getData(int channel)
{
	uint8_t value = 0;
	int nb;

	if (SCC.chn[channel].rd_handle >= 0)
	{
		nb = read(SCC.chn[channel].rd_handle, &value, 1);
		if (nb < 0)
		{
			Log_Printf(LOG_WARN, "SCC: channel %d read failed\n", channel);
		}
	}
	LOG_TRACE(TRACE_SCC, "SCC: getData(%d) => %d\n", channel, value);
	return value;
}

static void SCC_serial_setData(int channel, uint8_t value)
{
	int nb;

	LOG_TRACE(TRACE_SCC, "scc serial set data channel=%c value=$%02x\n", 'A'+channel, value);

	if (SCC.chn[channel].wr_handle >= 0)
	{
		do
		{
			nb = write(SCC.chn[channel].wr_handle, &value, 1);
		} while (nb < 0 && (errno == EAGAIN || errno == EINTR));
	}
}

#if HAVE_TERMIOS_H
static void SCC_serial_setBaudAttr(int handle, speed_t new_speed)
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

static void SCC_serial_setBaud(int channel, int value)
{
#if HAVE_TERMIOS_H
	speed_t new_speed = B0;

	LOG_TRACE(TRACE_SCC, "scc serial set baud channel=%c value=$%02x\n", 'A'+channel, value);

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

	SCC_serial_setBaudAttr(SCC.chn[channel].rd_handle, new_speed);
	if (SCC.chn[channel].rd_handle != SCC.chn[channel].wr_handle)
		SCC_serial_setBaudAttr(SCC.chn[channel].wr_handle, new_speed);
#endif
}

static uint16_t SCC_getTBE(int chn)
{
	uint16_t value = 0;

#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCSERGETLSR) && defined(TIOCSER_TEMT)
	int status = 0;
	if (ioctl(SCC.chn[chn].wr_handle, TIOCSERGETLSR, &status) < 0)  // OK with ttyS0, not OK with ttyUSB0
	{
		// D(bug("SCC: Can't get LSR"));
		value |= (1<<TBE);   // only for serial USB
	}
	else if (status & TIOCSER_TEMT)
	{
		value = (1 << TBE);  // this is a real TBE for ttyS0
		if ((SCC.chn[chn].oldTBE & (1 << TBE)) == 0)
		{
			value |= 0x200;
		} // TBE rise=>TxIP (based on real TBE)
	}
#endif

	SCC.chn[chn].oldTBE = value;
	return value;
}

static uint16_t SCC_serial_getStatus(int chn)
{
	uint16_t value = 0;
	uint16_t diff;

#if defined(HAVE_SYS_IOCTL_H) && defined(FIONREAD)
	if (SCC.chn[chn].rd_handle >= 0)
	{
		int nbchar = 0;

		if (ioctl(SCC.chn[chn].rd_handle, FIONREAD, &nbchar) < 0)
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get input fifo count\n");
		}
		SCC.chn[chn].charcount = nbchar; // to optimize input (see UGLY in handleWrite)
		if (nbchar > 0)
			value = 0x0401;  // RxIC+RBF
	}
#endif
	if (SCC.chn[chn].wr_handle >= 0 && SCC.chn[chn].bFileHandleIsATTY)
	{
		value |= SCC_getTBE(chn); // TxIC
		value |= (1 << TBE);  // fake TBE to optimize output (for ttyS0)
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
		int status = 0;
		if (ioctl(SCC.chn[chn].wr_handle, TIOCMGET, &status) < 0)
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status\n");
		}
		if (status & TIOCM_CTS)
			value |= (1 << CTS);
#endif
	}

	if (SCC.chn[chn].wr_handle >= 0 && !SCC.chn[chn].bFileHandleIsATTY)
	{
		/* Output is a normal file, thus always set Clear-To-Send
		 * and Transmit-Buffer-Empty: */
		value |= (1 << CTS) | (1 << TBE);
	}
	else if (SCC.chn[chn].wr_handle < 0)
	{
		/* If not connected, signal transmit-buffer-empty anyway to
		 * avoid that the program blocks while polling this bit */
		value |= (1 << TBE);
	}

	diff = SCC.chn[chn].oldStatus ^ value;
	if (diff & (1 << CTS))
		value |= 0x100;  // ext status IC on CTS change

	LOG_TRACE(TRACE_SCC, "SCC: getStatus(%d) => 0x%04x\n", chn, value);

	SCC.chn[chn].oldStatus = value;
	return value;
}

static void SCC_serial_setRTS(int chn, bool value)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int status = 0;

	if (SCC.chn[chn].wr_handle >= 0 && SCC.chn[chn].bFileHandleIsATTY)
	{
		if (ioctl(SCC.chn[chn].wr_handle, TIOCMGET, &status) < 0)
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status for RTS\n");
		}
		if (value)
			status |= TIOCM_RTS;
		else
			status &= ~TIOCM_RTS;
		ioctl(SCC.chn[chn].wr_handle, TIOCMSET, &status);
	}
#endif
}

static void SCC_serial_setDTR(int chn, bool value)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int status = 0;

	if (SCC.chn[chn].wr_handle >= 0 && SCC.chn[chn].bFileHandleIsATTY)
	{
		if (ioctl(SCC.chn[chn].wr_handle, TIOCMGET, &status) < 0)
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status for DTR\n");
		}
		if (value)
			status |= TIOCM_DTR;
		else
			status &= ~TIOCM_DTR;
		ioctl(SCC.chn[chn].wr_handle, TIOCMSET, &status);
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


	for ( i=0 ; i<(int)sizeof(SCC_Standard_Baudrate) ; i++ )
	{
		margin = SCC_Standard_Baudrate[ i ] * 0.01;	/* 1% */
		if ( margin < 4 )
			margin = 4;				/* increase margin for small bitrates < 600 */

		low = SCC_Standard_Baudrate[ i ] - margin;
		high = SCC_Standard_Baudrate[ i ] + margin;
fprintf ( stderr , "check %d %d %f %f\n" , i , BaudRate , low , high );
		if ( ( low < BaudRate ) && ( BaudRate < high ) )
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
 * an additionnal clock multipier.
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
	if ( ( SCC.chn[chn].WR[4] & 0x0c ) == 0 )			/* bits 2-3 = 0, sync modes enabled, force x1 clock */
		ClockMult = 1;
	else
		ClockMult = SCC_ClockMode[ SCC.chn[chn].WR[4] >> 6 ];	/* use bits 6-7 to get multiplier */


	/* WR12 and WR13 give Low/High values of the 16 bit time constant for the BRG  */
	TimeConstant = ( SCC.chn[chn].WR[13]<<8 ) + SCC.chn[chn].WR[12];


	/* WR14 gives the clock source for the baud rate generator + enable the BRG */
	/* NOTE : it's possible to start the BRG even we use a different clock mode later */
	/* for the baud rate in WR11 */
	if ( ( SCC.chn[chn].WR[14] & 1 ) == 0 )				/* BRG is disabled */
		*pStartBRG = false;
	else
	{
		*pStartBRG = true;

		if ( SCC.chn[chn].WR[14] & 2 )				/* source is PCLK */
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
	TransmitClock = ( SCC.chn[chn].WR[11] >> 3 ) & 3;
	ReceiveClock = ( SCC.chn[chn].WR[11] >> 5 ) & 3;
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

static void SCC_Update_BaudRate ( int chn )
{
	bool		StartBRG;
	uint32_t	BaudRate_BRG;
	int		BaudRate;
	int		BaudRate_Standard;
	bool		Serial_ON;


	BaudRate = SCC_Compute_BaudRate ( chn , &StartBRG , &BaudRate_BRG );
	if ( StartBRG )
	{
		SCC.chn[chn].BaudRate_BRG = BaudRate_BRG;
		SCC_Start_InterruptHandler ( chn , 0 );
	}
	else
	{
		SCC_Stop_InterruptHandler ( chn );
	}

	if ( BaudRate == -1 )
	{
		Serial_ON = false;
	}
	else
	{
		BaudRate_Standard = SCC_Get_Standard_BaudRate ( BaudRate );
		if ( BaudRate_Standard > 0 )
			Serial_ON = true;
		else
			Serial_ON = false;
	}

	if ( Serial_ON )
	{
fprintf(stderr , "update br serial_on %d->%d\n" , BaudRate , BaudRate_Standard );
//		SCC_serial_setBaud ( chn , BaudRate_Standard );
	}
	else
	{
		/* TODO : stop serial */
	}

  }


static uint8_t SCC_ReadControl(int chn)
{
	uint8_t value = 0;
	uint16_t temp;
	int active_reg;

	active_reg = SCC.chn[chn].Active_Reg;

	switch ( active_reg )
	{
	 case 0:	// RR0
	 case 4:	// also returns RR0
		temp = SCC_serial_getStatus(chn);
		SCC.chn[chn].WR[0] = temp & 0xFF;		// define CTS(5), TBE(2) and RBF=RCA(0)
		if (chn)
			SCC.chn[0].RR[3] = SCC.IUS & (temp >> 8);	// define RxIP(2), TxIP(1) and ExtIP(0)
		else if (SCC.chn[0].WR[9] == 0x20)
			SCC.chn[0].RR[3] |= 0x8;
		value = SCC.chn[chn].WR[0];
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d tx/rx buffer status value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 case 1:	// RR1
	 case 5:	// also returns RR1
		// TODO
		break;

	 case 2:	// not really useful (RR2 seems unaccessed...)
		value = SCC.chn[0].WR[2];
		if (chn == 0)	// vector base only for RR2A
		{
			LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d int vector value=$%02x\n" , 'A'+chn , active_reg , value );
			break;
		}
		if ((SCC.chn[0].WR[9] & 1) == 0)	// no status bit added
			break;
		// status bit added to vector
		if (SCC.chn[0].WR[9] & 0x10) // modify high bits
		{
			if (SCC.chn[0].RR[3] == 0)
			{
				value |= 0x60;
				break;
			}
			if (SCC.chn[0].RR[3] & 32)
			{
				value |= 0x30;        // A RxIP
				break;
			}
			if (SCC.chn[0].RR[3] & 16)
			{
				value |= 0x10;        // A TxIP
				break;
			}
			if (SCC.chn[0].RR[3] & 8)
			{
				value |= 0x50;        // A Ext IP
				break;
			}
			if (SCC.chn[0].RR[3] & 4)
			{
				value |= 0x20;        // B RBF
				break;
			}
			if (SCC.chn[0].RR[3] & 2)
				break;                // B TBE
			if (SCC.chn[0].RR[3] & 1)
				value |= 0x40;        // B Ext Status
		}
		else // modify low bits
		{
			if (SCC.chn[0].RR[3] == 0)
			{
				value |= 6;           // no one
				break;
			}
			if (SCC.chn[0].RR[3] & 32)
			{
				value |= 0xC;         // A RxIP
				break;
			}
			if (SCC.chn[0].RR[3] & 16)
			{
				value |= 0x8;         // A TxIP
				break;
			}
			if (SCC.chn[0].RR[3] & 8)
			{
				value |= 0xA;         // A Ext IP
				break;
			}
			if (SCC.chn[0].RR[3] & 4)
			{
				value |= 4;           // B RBF
				break;
			}
			if (SCC.chn[0].RR[3] & 2)
				break;                // B TBE
			if (SCC.chn[0].RR[3] & 1)
				value |= 2;           // B Ext Status (CTS)
		}
		break;
	 case 3:
		value = chn ? 0 : SCC.chn[0].RR[3];     // access on A channel only
//value = SCC.chn[chn].RR[3];		// TEMP hack for st mint
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d interrupt pending value=$%02x\n" , 'A'+chn , active_reg , value );
		break;
	 case 8: // DATA reg
		SCC.chn[chn].WR[8] = SCC_serial_getData(chn);
		value = SCC.chn[chn].WR[8];
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d rx data value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 case 10:	// Misc Status Bits
	 case 14:	// also returns RR10
		break;

	 case 12: // BRG LSB
		value = SCC.chn[chn].WR[active_reg];
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d baud rate time constant low value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 case 13: // BRG MSB
	 case 9:	// also returns RR13
		value = SCC.chn[chn].WR[active_reg];
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d baud rate time constant high value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 case 15:	// EXT/STATUS IT Ctrl
	 case 11:	// also returns RR15
		value = SCC.chn[chn].WR[15] &= 0xFA; // mask out D2 and D0
		LOG_TRACE(TRACE_SCC, "scc read channel=%c RR%d ext status IE value=$%02x\n" , 'A'+chn , active_reg , value );
		break;

	 default: // RR5,RR6,RR7,RR10,RR14 not processed
		Log_Printf(LOG_DEBUG, "SCC: unprocessed read address=$%x\n", active_reg);
		value = 0;
		break;
	}

	LOG_TRACE(TRACE_SCC, "scc read RR%d value=$%02x\n" , active_reg , value );
	return value;
}

static uint8_t SCC_handleRead(uint32_t addr)
{
	uint8_t value = 0;
	int channel;

	addr &= 0x6;
	channel = (addr >= 4) ? 1 : 0;  // 0 = channel A, 1 = channel B

	LOG_TRACE(TRACE_SCC, "scc read addr=%d channel=%c\n" , addr , 'A'+channel );

	switch (addr)
	{
	 case 0: // channel A
	 case 4: // channel B
		value = SCC_ReadControl(channel);
		break;
	 case 2: // channel A
	 case 6: // channel B
		SCC.chn[channel].WR[8] = SCC_serial_getData(channel);
		value = SCC.chn[channel].WR[8];
		break;
	 default:
		Log_Printf(LOG_DEBUG, "SCC: illegal read address=$%x\n", addr);
		break;
	}

	SCC.chn[channel].Active_Reg = 0;		/* Next access for RR0 or WR0 */

	return value;
}

static void SCC_WriteControl(int chn, uint8_t value)
{
	uint32_t BaudRate;
	int i;
	int write_reg;

	if ( SCC.chn[chn].Active_Reg == 0 )
	{
		if (value <= 15)
		{
			SCC.chn[chn].Active_Reg = value & 0x0f;
			LOG_TRACE(TRACE_SCC, "scc set active reg=R%d\n" , SCC.chn[chn].Active_Reg );
		}
		else
		{
			if ((value & 0x38) == 0x38) // Reset Highest IUS (last operation in IT service routine)
			{
				for (i = 0x20; i; i >>= 1)
				{
					if (SCC.chn[0].RR[3] & i)
						break;
				}
#define UGLY
#ifdef UGLY
				// tricky & ugly speed improvement for input
				if (i == 4) // RxIP
				{
					SCC.chn[chn].charcount--;
					if (SCC.chn[chn].charcount <= 0)
						SCC.chn[0].RR[3] &= ~4; // optimize input; don't reset RxIP when chars are buffered
				}
				else
				{
					SCC.chn[0].RR[3] &= ~i;
				}
#else
				SCC.chn[0].RR[3] &= ~i;
#endif
			}
			else if ((value & 0x38) == 0x28) // Reset Tx int pending
			{
				if (chn)
					SCC.chn[0].RR[3] &= ~2;       // channel B
				else
					SCC.chn[0].RR[3] &= ~0x10;    // channel A
			}
			else if ((value & 0x38) == 0x10) // Reset Ext/Status ints
			{
				if (chn)
					SCC.chn[0].RR[3] &= ~1;       // channel B
				else
					SCC.chn[0].RR[3] &= ~8;       // channel A
			}
			// Clear SCC flag if no pending IT or no properly
			// configured WR9. Must be done here to avoid
			// scc_do_Interrupt call without pending IT
			TriggerSCC((SCC.chn[0].RR[3] & SCC.IUS) && ((0xB & SCC.chn[0].WR[9]) == 9));
		}
		return;
	}

	LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );

	/* write_reg can be different from Active_Reg when accessing WR7' */
	write_reg = SCC.chn[chn].Active_Reg;
	if ( SCC.chn[chn].WR[15] & 1 )
	{
		write_reg = 16;			/* WR[16] stores the content of WR7' */
	}
	SCC.chn[chn].WR[write_reg] = value;


	if (SCC.chn[chn].Active_Reg == 1) // Tx/Rx interrupt enable
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set tx/rx int value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		if (chn == 0)
		{
			// channel A
			if (value & 1)
				SCC.IUS |= 8;
			else
				SCC.chn[0].RR[3] &= ~8; // no IP(RR3) if not enabled(IUS)
			if (value & 2)
				SCC.IUS |= 16;
			else
				SCC.chn[0].RR[3] &= ~16;
			if (value & 0x18)
				SCC.IUS |= 32;
			else
				SCC.chn[0].RR[3] &= ~32;
		}
		else
		{
			// channel B
			if (value & 1)
				SCC.IUS |= 1;
			else
				SCC.chn[0].RR[3] &= ~1;
			if (value & 2)
				SCC.IUS |= 2;
			else
				SCC.chn[0].RR[3] &= ~2;
			if (value & 0x18)
				SCC.IUS |= 4;
			else
				SCC.chn[0].RR[3] &= ~4;
			// set or clear SCC flag if necessary (see later)
		}
	}
	else if (SCC.chn[chn].Active_Reg == 2)
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set int vector value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		SCC.chn[0].WR[2] = value;		/* WR2 is common to channels A and B, store it in channel A  */
	}
	else if (SCC.chn[chn].Active_Reg == 3) // Receive parameter and control
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set rx parameter and control value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
	}
	else if (SCC.chn[chn].Active_Reg == 4) // Tx/Rx misc parameters and modes
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set tx/rx stop/parity value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		SCC_Update_BaudRate ( chn );
	}
	else if (SCC.chn[chn].Active_Reg == 5) // Transmit parameter and control
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set tx parameter and control value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		SCC_serial_setRTS(chn, value & 2);
		SCC_serial_setDTR(chn, value & 128);
		// Tx character format & Tx CRC would be selected also here (8 bits/char and no CRC assumed)
	}
	else if (SCC.chn[chn].Active_Reg == 6) // Sync characters high or SDLC Address Field
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set sync hi/sdlc addr value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
	}
	else if (SCC.chn[chn].Active_Reg == 7) // Sync characters low or SDLC flag or WR7'
	{
		if ( SCC.chn[chn].WR[15] & 1 )
		{
			LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set WR7' value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		}
		else
		{
			LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set sync low/sdlc flag value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		}
	}
	else if (SCC.chn[chn].Active_Reg == 8)
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set transmit buffer value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		SCC_serial_setData(chn, value);
	}
	else if (SCC.chn[chn].Active_Reg == 9) 		/* Master interrupt control (common for both channels) */
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set master control value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		SCC.chn[0].WR[9] = value;		/* WR9 is common to channels A and B, store it in channel A  */

		/* Bit 0 : VIS, Vector Includes Status */
		/* Bit 1 : NV, No Vector during INTACK */
		/* Bit 2 : Disable Lower Chain (not use in Hatari, there's only 1 SCC */
		/* Bit 3 : Master Interrupt Enable */
		// TODO clear IUS if MIE=0
		/* Bit 4 : Status High / Low */
		/* Bit 5 : Software INTACK Enable */
		/* Bits 6-7 : reset command */
		if ( ( value >> 6 ) == SCC_WR9_COMMAND_RESET_FORCE_HW )
		{
			SCC_ResetFull ( false );		/* Force Hardware Reset */
		}
		else if ( ( value >> 6 ) == SCC_WR9_COMMAND_RESET_A )
		{
			SCC_ResetChannel ( 0 , false );		/* Channel A */
		}
		else if ( ( value >> 6 ) == SCC_WR9_COMMAND_RESET_B )
		{
			SCC_ResetChannel ( 1 , false );		/* Channel B */
		}

		//  set or clear SCC flag accordingly (see later)
	}
	else if (SCC.chn[chn].Active_Reg == 10) // Tx/Rx misc control bits
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set tx/rx control bits value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
	}
	else if (SCC.chn[chn].Active_Reg == 11) // Clock Mode Control
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set clock mode control value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		SCC_Update_BaudRate ( chn );
	}
	else if (SCC.chn[chn].Active_Reg == 12) // Lower byte of baud rate
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set baud rate time constant low value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		SCC_Update_BaudRate ( chn );
	}
	else if (SCC.chn[chn].Active_Reg == 13) // set baud rate according to WR13 and WR12
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set baud rate time constant high value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		// Normally we have to set the baud rate according
		// to clock source (WR11) and clock mode (WR4)
		// In fact, we choose the baud rate from the value stored in WR12 & WR13
		// Note: we assume that WR13 is always written last (after WR12)
		// we tried to be more or less compatible with HSMODEM (see below)
		// 75 and 50 bauds are preserved because 153600 and 76800 were not available
		// 3600 and 2000 were also unavailable and are remapped to 57600 and 38400 respectively
		BaudRate = 0;
		switch (value)
		{
		 case 0:
			switch (SCC.chn[chn].WR[12])
			{
			 case 0: // HSMODEM for 200 mapped to 230400
				BaudRate = 230400;
				break;
			 case 2: // HSMODEM for 150 mapped to 115200
				BaudRate = 115200;
				break;
			 case 6:    // HSMODEM for 134 mapped to 57600
			 case 0x7e: // HSMODEM for 3600 remapped to 57600
			 case 0x44: // normal for 3600 remapped to 57600
				BaudRate = 57600;
				break;
			 case 0xa:  // HSMODEM for 110 mapped to 38400
			 case 0xe4: // HSMODEM for 2000 remapped to 38400
			 case 0x7c: // normal for 2000 remapped to 38400
				BaudRate = 38400;
				break;
			 case 0x16: // HSMODEM for 19200
			 case 0xb:  // normal for 19200
				BaudRate = 19200;
				break;
			 case 0x2e: // HSMODEM for 9600
			 case 0x18: // normal for 9600
				BaudRate = 9600;
				break;
			 case 0x5e: // HSMODEM for 4800
			 case 0x32: // normal for 4800
				BaudRate = 4800;
				break;
			 case 0xbe: // HSMODEM for 2400
			 case 0x67: // normal
				BaudRate = 2400;
				break;
			 case 0xfe: // HSMODEM for 1800
			 case 0x8a: // normal for 1800
				BaudRate = 1800;
				break;
			 case 0xd0: // normal for 1200
				BaudRate = 1200;
				break;
			 case 1: // HSMODEM for 75 kept to 75
				BaudRate = 75;
				break;
			 case 4: // HSMODEM for 50 kept to 50
				BaudRate = 50;
				break;
			 default:
				Log_Printf(LOG_DEBUG, "SCC: unexpected LSB constant for baud rate\n");
				break;
			}
			break;
		 case 1:
			switch (SCC.chn[chn].WR[12])
			{
			 case 0xa1: // normal for 600
				BaudRate = 600;
				break;
			 case 0x7e: // HSMODEM for 1200
				BaudRate = 1200;
				break;
			}
			break;
		 case 2:
			if (SCC.chn[chn].WR[12] == 0xfe)
				BaudRate = 600; //HSMODEM
			break;
		 case 3:
			if (SCC.chn[chn].WR[12] == 0x45)
				BaudRate = 300; //normal
			break;
		 case 4:
			if (SCC.chn[chn].WR[12] == 0xe8)
				BaudRate = 200; //normal
			break;
		 case 5:
			if (SCC.chn[chn].WR[12] == 0xfe)
				BaudRate = 300; //HSMODEM
			break;
		 case 6:
			if (SCC.chn[chn].WR[12] == 0x8c)
				BaudRate = 150; //normal
			break;
		 case 7:
			if (SCC.chn[chn].WR[12] == 0x4d)
				BaudRate = 134; //normal
			break;
		 case 8:
			if (SCC.chn[chn].WR[12] == 0xee)
				BaudRate = 110; //normal
			break;
		 case 0xd:
			if (SCC.chn[chn].WR[12] == 0x1a)
				BaudRate = 75; //normal
			break;
		 case 0x13:
			if (SCC.chn[chn].WR[12] == 0xa8)
				BaudRate = 50; //normal
			break;
		 case 0xff: // HSMODEM dummy value->silently ignored
			break;
		 default:
			Log_Printf(LOG_DEBUG, "SCC: unexpected MSB constant for baud rate\n");
			break;
		}

		SCC_Update_BaudRate ( chn );

		if (BaudRate)  // set only if defined
			SCC_serial_setBaud(chn, BaudRate);

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
	}
	else if (SCC.chn[chn].Active_Reg == 14) // Misc Control bits
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set misc control bits value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
		SCC_Update_BaudRate ( chn );
	}
	else if (SCC.chn[chn].Active_Reg == 15) // external status int control
	{
		LOG_TRACE(TRACE_SCC, "scc write channel=%c WR%d set ext status int control value=$%02x\n" , 'A'+chn , SCC.chn[chn].Active_Reg , value );
	}

	// set or clear SCC flag accordingly. Yes it's ugly but avoids unnecessary useless calls
	if (SCC.chn[chn].Active_Reg == 1 || SCC.chn[chn].Active_Reg == 2 || SCC.chn[chn].Active_Reg == 9)
		TriggerSCC((SCC.chn[0].RR[3] & SCC.IUS) && ((0xB & SCC.chn[0].WR[9]) == 9));


	SCC.chn[chn].Active_Reg = 0;			/* next access for RR0 or WR0 */
}

static void SCC_handleWrite(uint32_t addr, uint8_t value)
{
	int channel;

	addr &= 0x6;
	channel = (addr >= 4) ? 1 : 0;  // 0 = channel A, 1 = channel B

	LOG_TRACE(TRACE_SCC, "scc write addr=%d channel=%c value=$%02x\n" , addr , 'A'+channel , value );

	switch (addr)
	{
	 case 0:
	 case 4:
		SCC_WriteControl(channel, value);
		break;
	 case 2: // channel A
	 case 6: // channel B
		SCC_serial_setData(channel, value);
		break;
	 default:
		Log_Printf(LOG_DEBUG, "SCC: illegal write address=$%x\n", addr);
		break;
	}
}




/*
 * Start the internal interrupt handler for SCC A or B when the baud rate generator is enabled
 */

static void	SCC_Start_InterruptHandler ( int channel , int InternalCycleOffset )
{
	int	IntHandler;
	int	Cycles;

	if ( channel == 0 )
		IntHandler = INTERRUPT_SCC_A;
	else
		IntHandler = INTERRUPT_SCC_B;

	Cycles = MachineClocks.CPU_Freq / SCC.chn[channel].BaudRate_BRG;	/* Convert baud rate in CPU cycles */

	LOG_TRACE ( TRACE_SCC, "scc start interrupt handler channel=%c baudrate=%d cpu_cycles=%d VBL=%d HBL=%d\n" ,
		'A'+channel , SCC.chn[channel].BaudRate_BRG , Cycles , nVBLs , nHBL );

	CycInt_AddRelativeInterruptWithOffset ( Cycles, INT_CPU_CYCLE, IntHandler, InternalCycleOffset );
}


/*
 * Stop the internal interrupt handler for SCC A or B when the baud rate generator is disabled
 */

static void	SCC_Stop_InterruptHandler ( int channel )
{
	int	IntHandler;

	if ( channel == 0 )
		IntHandler = INTERRUPT_SCC_A;
	else
		IntHandler = INTERRUPT_SCC_B;

	CycInt_RemovePendingInterrupt ( IntHandler );
}


/*
 * Interrupt called each time the baud rate generator's counter reaches 0
 * We continuously restart the interrupt, taking into account PendingCyclesOver.
 */

static void	SCC_InterruptHandler ( int channel )
{
	int	PendingCyclesOver;


	/* Number of internal cycles we went over for this timer ( <= 0 ) */
	/* Used to restart the next timer and keep a constant baud rate */
	PendingCyclesOver = -PendingInterruptCount;			/* >= 0 */

	LOG_TRACE ( TRACE_SCC, "scc interrupt handler channel=%c pending_cyc=%d VBL=%d HBL=%d\n" , 'A'+channel , PendingCyclesOver , nVBLs , nHBL );

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	SCC_Start_InterruptHandler ( channel , -PendingCyclesOver );	/* Compensate for a != 0 value of PendingCyclesOver */

	/* BRG counter reached 0, check if corresponding interrupt pending bit must be set in RR3 */
	if ( ( SCC.chn[channel].WR[1] &  SCC_WR1_BIT_EXT_INT_ENABLE )
		&& ( SCC.chn[channel].WR[15] &  SCC_WR15_BIT_ZERO_COUNT_IE ) )
	{
		/* IP bits are only set in RR3A */
		if ( channel == 0 )
			SCC.chn[0].RR[3] |= SCC_RR3_BIT_EXT_STATUS_IP_A;
		else
			SCC.chn[0].RR[3] |= SCC_RR3_BIT_EXT_STATUS_IP_B;

		SCC_Update_IRQ ( channel );
	}
}


void	SCC_InterruptHandler_A ( void )
{
	SCC_InterruptHandler ( 0 );
}


void	SCC_InterruptHandler_B ( void )
{
	SCC_InterruptHandler ( 1 );
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



static void	SCC_Update_IRQ ( int channel )
{
}


void SCC_IRQ(void)
{
	uint16_t temp;
	temp = SCC_serial_getStatus(0);
	if (SCC.chn[0].WR[9] == 0x20)
		temp |= 0x800; // fake ExtStatusChange for HSMODEM install
	SCC.chn[1].WR[0] = temp & 0xFF; // RR0B
	SCC.chn[0].RR[3] = SCC.IUS & (temp >> 8);
	if (SCC.chn[0].RR[3] && (SCC.chn[0].WR[9] & 0xB) == 9)
		TriggerSCC(true);
}


// return : vector number, or zero if no interrupt
int SCC_doInterrupt(void)
{
	int vector;
	uint8_t i;
	for (i = 0x20 ; i ; i >>= 1) // highest priority first
	{
		if (SCC.chn[0].RR[3] & i & SCC.IUS)
			break ;
	}
	vector = SCC.chn[0].WR[2]; // WR2 = base of vectored interrupts for SCC
	if ((SCC.chn[0].WR[9] & 3) == 0)
		return vector; // no status included in vector
	if ((SCC.chn[0].WR[9] & 0x32) != 0)  // shouldn't happen with TOS, (to be completed if needed)
	{
		Log_Printf(LOG_DEBUG, "SCC: unexpected WR9 contents\n");
		// no Soft IACK, Status Low control bit expected, no NV
		return 0;
	}
	switch (i)
	{
	 case 0: /* this shouldn't happen :-) */
		Log_Printf(LOG_WARN, "SCC: doInterrupt() called with no pending interrupt\n");
		vector = 0; // cancel
		break;
	 case 1:
		vector |= 2; // Ch B Ext/status change
		break;
	 case 2:
		break;// Ch B Transmit buffer Empty
	 case 4:
		vector |= 4; // Ch B Receive Char available
		break;
	 case 8:
		vector |= 0xA; // Ch A Ext/status change
		break;
	 case 16:
		vector |= 8; // Ch A Transmit Buffer Empty
		break;
	 case 32:
		vector |= 0xC; // Ch A Receive Char available
		break;
		// special receive condition not yet processed
	}
	LOG_TRACE(TRACE_SCC, "SCC: SCC_doInterrupt : vector %d\n", vector);
	return vector ;
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
	fprintf(fp, "- IUS: %02x\n", SCC.IUS);

	for (i = 0; i < 2; i++)
	{
		fprintf(fp, "\nSCC %c:\n", 'A' + i);
		fprintf(fp, "- Active register: %d\n", SCC.chn[i].Active_Reg);
		fprintf(fp, "- Write Registers:\n");
		for (reg = 0; reg < ARRAY_SIZE(SCC.chn[i].WR); reg++)
			fprintf(fp, "  %02x", SCC.chn[i].WR[reg]);
		fprintf(fp, "\n");

		fprintf(fp, "- Read Registers:\n");
		for (reg = 0; reg < ARRAY_SIZE(SCC.chn[i].RR); reg++)
			fprintf(fp, "  %02x", SCC.chn[i].RR[reg]);
		fprintf(fp, "\n");

		fprintf(fp, "- Char count: %d\n", SCC.chn[i].charcount);
		fprintf(fp, "- Old status: 0x%04x\n", SCC.chn[i].oldStatus);
		fprintf(fp, "- Old TBE:    0x%04x\n", SCC.chn[i].oldTBE);
		fprintf(fp, "- %s TTY\n", SCC.chn[i].bFileHandleIsATTY ? "A" : "Not a");
	}
}
