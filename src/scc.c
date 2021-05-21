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

struct SCC {
	uint8_t regs[16];
	int charcount;
	int rd_handle, wr_handle;
	uint16_t oldTBE;
	uint16_t oldStatus;
	bool bFileHandleIsATTY;
};

static struct SCC scc[2];

static int active_reg;
static uint8_t RR3, RR3M;    // common to channel A & B

bool SCC_IsAvailable(CNF_PARAMS *cnf)
{
	return ConfigureParams.System.nMachineType == MACHINE_MEGA_STE
	       || ConfigureParams.System.nMachineType == MACHINE_TT
	       || ConfigureParams.System.nMachineType == MACHINE_FALCON;
}

void SCC_Init(void)
{
	SCC_Reset();

	scc[0].oldTBE = scc[1].oldTBE = 0;
	scc[0].oldStatus = scc[1].oldStatus = 0;

	scc[0].rd_handle = scc[0].wr_handle = -1;
	scc[1].rd_handle = scc[1].wr_handle = -1;

	if (!ConfigureParams.RS232.bEnableSccB || !SCC_IsAvailable(&ConfigureParams))
		return;

	if (ConfigureParams.RS232.sSccBInFileName[0] &&
	    strcmp(ConfigureParams.RS232.sSccBInFileName, ConfigureParams.RS232.sSccBOutFileName) == 0)
	{
#if HAVE_TERMIOS_H
		scc[1].rd_handle = open(ConfigureParams.RS232.sSccBInFileName, O_RDWR | O_NONBLOCK);
		if (scc[1].rd_handle >= 0)
		{
			if (isatty(scc[1].rd_handle))
			{
				scc[1].wr_handle = scc[1].rd_handle;
			}
			else
			{
				Log_Printf(LOG_ERROR, "SCC_Init: Setting SCC-B input and output "
				           "to the same file only works with tty devices.\n");
				close(scc[1].rd_handle);
				scc[1].rd_handle = -1;
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
			scc[1].rd_handle = open(ConfigureParams.RS232.sSccBInFileName, O_RDONLY | O_NONBLOCK);
			if (scc[1].rd_handle < 0)
			{
				Log_Printf(LOG_ERROR, "SCC_Init: Can not open input file '%s'\n",
					   ConfigureParams.RS232.sSccBInFileName);
			}
		}
		if (ConfigureParams.RS232.sSccBOutFileName[0])
		{
			scc[1].wr_handle = open(ConfigureParams.RS232.sSccBOutFileName,
						O_CREAT | O_WRONLY | O_NONBLOCK, S_IRUSR | S_IWUSR);
			if (scc[1].wr_handle < 0)
			{
				Log_Printf(LOG_ERROR, "SCC_Init: Can not open output file '%s'\n",
					   ConfigureParams.RS232.sSccBOutFileName);
			}
		}
	}
	if (scc[1].rd_handle == -1 && scc[1].wr_handle == -1)
	{
		ConfigureParams.RS232.bEnableSccB = false;
	}
}

void SCC_UnInit(void)
{
	if (scc[1].rd_handle >= 0)
	{
		if (scc[1].wr_handle == scc[1].rd_handle)
			scc[1].wr_handle = -1;
		close(scc[1].rd_handle);
		scc[1].rd_handle = -1;
	}
	if (scc[1].wr_handle >= 0)
	{
		close(scc[1].wr_handle);
		scc[1].wr_handle = -1;
	}
}

void SCC_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(&active_reg, sizeof(active_reg));
	MemorySnapShot_Store(&RR3, sizeof(RR3));
	MemorySnapShot_Store(&RR3M, sizeof(RR3M));
	for (int c = 0; c < 2; c++)
	{
		MemorySnapShot_Store(scc[c].regs, sizeof(scc[c].regs));
		MemorySnapShot_Store(&scc[c].charcount, sizeof(scc[c].charcount));
		MemorySnapShot_Store(&scc[c].oldTBE, sizeof(scc[c].oldTBE));
		MemorySnapShot_Store(&scc[c].oldStatus, sizeof(scc[c].oldStatus));
	}
}

static void SCC_channelAreset(void)
{
	LOG_TRACE(TRACE_SCC, "SCC: reset channel A\n");
	scc[0].regs[15] = 0xF8;
	scc[0].regs[14] = 0xA0;
	scc[0].regs[11] = 0x08;
	scc[0].regs[9] = 0;
	RR3 &= ~0x38;
	RR3M &= ~0x38;
	scc[0].regs[0] = 1 << TBE;  // RR0A
}

static void SCC_channelBreset(void)
{
	LOG_TRACE(TRACE_SCC, "SCC: reset channel B\n");
	scc[1].regs[15] = 0xF8;
	scc[1].regs[14] = 0xA0;
	scc[1].regs[11] = 0x08;
	scc[0].regs[9] = 0;         // single WR9
	RR3 &= ~7;
	RR3M &= ~7;
	scc[1].regs[0] = 1 << TBE;  // RR0B
}

void SCC_Reset(void)
{
	active_reg = 0;
	memset(scc[0].regs, 0, sizeof(scc[0].regs));
	memset(scc[1].regs, 0, sizeof(scc[1].regs));
	SCC_channelAreset();
	SCC_channelBreset();
	RR3 = 0;
	RR3M = 0;
	scc[0].charcount = scc[1].charcount = 0;
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

	if (scc[channel].rd_handle >= 0)
	{
		nb = read(scc[channel].rd_handle, &value, 1);
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

	LOG_TRACE(TRACE_SCC, "SCC: setData(%d, %d)\n", channel, value);
	if (scc[channel].wr_handle >= 0)
	{
		do
		{
			nb = write(scc[channel].wr_handle, &value, 1);
		} while (nb < 0 && (errno == EAGAIN || errno == EINTR));
	}
}

#if HAVE_TERMIOS_H
static void SCC_serial_setBaudAttr(int handle, speed_t new_speed)
{
	struct termios options;

	if (handle < 0)
		return;

	tcgetattr(handle, &options);

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

	LOG_TRACE(TRACE_SCC, "SCC: setBaud(%i, %i)\n", channel, value);

	switch (value)
	{
	 case 230400:	new_speed = B230400;	break;
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

	SCC_serial_setBaudAttr(scc[channel].rd_handle, new_speed);
	if (scc[channel].rd_handle != scc[channel].wr_handle)
		SCC_serial_setBaudAttr(scc[channel].wr_handle, new_speed);
#endif
}

static uint16_t SCC_getTBE(int chn)
{
	uint16_t value = 0;

#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCSERGETLSR) && defined(TIOCSER_TEMT)
	int status = 0;
	if (ioctl(scc[chn].wr_handle, TIOCSERGETLSR, &status) < 0)  // OK with ttyS0, not OK with ttyUSB0
	{
		// D(bug("SCC: Can't get LSR"));
		value |= (1<<TBE);   // only for serial USB
	}
	else if (status & TIOCSER_TEMT)
	{
		value = (1 << TBE);  // this is a real TBE for ttyS0
		if ((scc[chn].oldTBE & (1 << TBE)) == 0)
		{
			value |= 0x200;
		} // TBE rise=>TxIP (based on real TBE)
	}
#endif

	scc[chn].oldTBE = value;
	return value;
}

static uint16_t SCC_serial_getStatus(int chn)
{
	uint16_t value = 0;
	uint16_t diff;

#if defined(HAVE_SYS_IOCTL_H) && defined(FIONREAD)
	if (scc[chn].rd_handle >= 0)
	{
		int nbchar = 0;

		if (ioctl(scc[chn].rd_handle, FIONREAD, &nbchar) < 0)
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get input fifo count\n");
		}
		scc[chn].charcount = nbchar; // to optimize input (see UGLY in handleWrite)
		if (nbchar > 0)
			value = 0x0401;  // RxIC+RBF
	}
#endif
	if (scc[chn].wr_handle >= 0 && scc[chn].bFileHandleIsATTY)
	{
		value |= SCC_getTBE(chn); // TxIC
		value |= (1 << TBE);  // fake TBE to optimize output (for ttyS0)
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
		int status = 0;
		if (ioctl(scc[chn].wr_handle, TIOCMGET, &status) < 0)
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status\n");
		}
		if (status & TIOCM_CTS)
			value |= (1 << CTS);
#endif
	}

	if (scc[chn].wr_handle >= 0 && !scc[chn].bFileHandleIsATTY)
	{
		/* Output is a normal file, thus always set Clear-To-Send
		 * and Transmit-Buffer-Empty: */
		value |= (1 << CTS) | (1 << TBE);
	}
	else if (scc[chn].wr_handle < 0)
	{
		/* If not connected, signal transmit-buffer-empty anyway to
		 * avoid that the program blocks while polling this bit */
		value |= (1 << TBE);
	}

	diff = scc[chn].oldStatus ^ value;
	if (diff & (1 << CTS))
		value |= 0x100;  // ext status IC on CTS change

	LOG_TRACE(TRACE_SCC, "SCC: getStatus(%d) => 0x%04x\n", chn, value);

	scc[chn].oldStatus = value;
	return value;
}

static void SCC_serial_setRTS(int chn, bool value)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int status = 0;

	if (scc[chn].wr_handle >= 0 && scc[chn].bFileHandleIsATTY)
	{
		if (ioctl(scc[chn].wr_handle, TIOCMGET, &status) < 0)
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status for RTS\n");
		}
		if (value)
			status |= TIOCM_RTS;
		else
			status &= ~TIOCM_RTS;
		ioctl(scc[chn].wr_handle, TIOCMSET, &status);
	}
#endif
}

static void SCC_serial_setDTR(int chn, bool value)
{
#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int status = 0;

	if (scc[chn].wr_handle >= 0 && scc[chn].bFileHandleIsATTY)
	{
		if (ioctl(scc[chn].wr_handle, TIOCMGET, &status) < 0)
		{
			Log_Printf(LOG_DEBUG, "SCC: Can't get status for DTR\n");
		}
		if (value)
			status |= TIOCM_DTR;
		else
			status &= ~TIOCM_DTR;
		ioctl(scc[chn].wr_handle, TIOCMSET, &status);
	}
#endif
}

static uint8_t SCC_ReadControl(int chn)
{
	uint8_t value = 0;
	uint16_t temp;

	switch (active_reg)
	{
	 case 0:	// RR0
		temp = SCC_serial_getStatus(chn);
		scc[chn].regs[0] = temp & 0xFF;		// define CTS(5), TBE(2) and RBF=RCA(0)
		if (chn)
			RR3 = RR3M & (temp >> 8);	// define RxIP(2), TxIP(1) and ExtIP(0)
		else if (scc[0].regs[9] == 0x20)
			RR3 |= 0x8;
		value = scc[chn].regs[0];
		break;
	 case 2:	// not really useful (RR2 seems unaccessed...)
		value = scc[0].regs[2];
		if (chn == 0)	// vector base only for RR2A
			break;
		if ((scc[0].regs[9] & 1) == 0)	// no status bit added
			break;
		// status bit added to vector
		if (scc[0].regs[9] & 0x10) // modify high bits
		{
			if (RR3 == 0)
			{
				value |= 0x60;
				break;
			}
			if (RR3 & 32)
			{
				value |= 0x30;        // A RxIP
				break;
			}
			if (RR3 & 16)
			{
				value |= 0x10;        // A TxIP
				break;
			}
			if (RR3 & 8)
			{
				value |= 0x50;        // A Ext IP
				break;
			}
			if (RR3 & 4)
			{
				value |= 0x20;        // B RBF
				break;
			}
			if (RR3 & 2)
				break;                // B TBE
			if (RR3 & 1)
				value |= 0x40;        // B Ext Status
		}
		else // modify low bits
		{
			if (RR3 == 0)
			{
				value |= 6;           // no one
				break;
			}
			if (RR3 & 32)
			{
				value |= 0xC;         // A RxIP
				break;
			}
			if (RR3 & 16)
			{
				value |= 0x8;         // A TxIP
				break;
			}
			if (RR3 & 8)
			{
				value |= 0xA;         // A Ext IP
				break;
			}
			if (RR3 & 4)
			{
				value |= 4;           // B RBF
				break;
			}
			if (RR3 & 2)
				break;                // B TBE
			if (RR3 & 1)
				value |= 2;           // B Ext Status (CTS)
		}
		break;
	 case 3:
		value = chn ? 0 : RR3;     // access on A channel only
		break;
	 case 4: // RR0
		value = scc[chn].regs[0];
		break;
	 case 8: // DATA reg
		scc[chn].regs[8] = SCC_serial_getData(chn);
		value = scc[chn].regs[8];
		break;
	 case 9: // WR13
		value = scc[chn].regs[13];
		break;
	 case 11: // WR15
	 case 15: // EXT/STATUS IT Ctrl
		value = scc[chn].regs[15] &= 0xFA; // mask out D2 and D0
		break;
	 case 12: // BRG LSB
	 case 13: // BRG MSB
		value = scc[chn].regs[active_reg];
		break;

	 default: // RR5,RR6,RR7,RR10,RR14 not processed
		Log_Printf(LOG_DEBUG, "SCC: unprocessed read address=$%x\n", active_reg);
		value = 0;
		break;
	}

	return value;
}

static uint8_t SCC_handleRead(uint32_t addr)
{
	uint8_t value = 0;
	int channel;

	addr &= 0x6;
	channel = (addr >= 4) ? 1 : 0;  // 0 = channel A, 1 = channel B
	switch (addr)
	{
	 case 0: // channel A
	 case 4: // channel B
		value = SCC_ReadControl(channel);
		break;
	 case 2: // channel A
	 case 6: // channel B
		scc[channel].regs[8] = SCC_serial_getData(channel);
		value = scc[channel].regs[8];
		break;
	 default:
		Log_Printf(LOG_DEBUG, "SCC: illegal read address=$%x\n", addr);
		break;
	}

	active_reg = 0; // next access for RR0 or WR0

	return value;
}

static void SCC_WriteControl(int chn, uint8_t value)
{
	uint32_t BaudRate;
	int i;

	if (active_reg == 0)
	{

		if (value <= 15)
		{
			active_reg = value & 0x0f;
		}
		else
		{
			if ((value & 0x38) == 0x38) // Reset Highest IUS (last operation in IT service routine)
			{
				for (i = 0x20; i; i >>= 1)
				{
					if (RR3 & i)
						break;
				}
#define UGLY
#ifdef UGLY
				// tricky & ugly speed improvement for input
				if (i == 4) // RxIP
				{
					scc[chn].charcount--;
					if (scc[chn].charcount <= 0)
						RR3 &= ~4; // optimize input; don't reset RxIP when chars are buffered
				}
				else
				{
					RR3 &= ~i;
				}
#else
				RR3 &= ~i;
#endif
			}
			else if ((value & 0x38) == 0x28) // Reset Tx int pending
			{
				if (chn)
					RR3 &= ~2;       // channel B
				else
					RR3 &= ~0x10;    // channel A
			}
			else if ((value & 0x38) == 0x10) // Reset Ext/Status ints
			{
				if (chn)
					RR3 &= ~1;       // channel B
				else
					RR3 &= ~8;       // channel A
			}
			// Clear SCC flag if no pending IT or no properly
			// configured WR9. Must be done here to avoid
			// scc_do_Interrupt call without pending IT
			TriggerSCC((RR3 & RR3M) && ((0xB & scc[0].regs[9]) == 9));
		}
		return;
	}

	// active_reg > 0:
	scc[chn].regs[active_reg] = value;
	if (active_reg == 2)
	{
		scc[0].regs[active_reg] = value; // single WR2 on SCC
	}
	else if (active_reg == 8)
	{
		SCC_serial_setData(chn, value);
	}
	else if (active_reg == 1) // Tx/Rx interrupt enable
	{
		if (chn == 0)
		{
			// channel A
			if (value & 1)
				RR3M |= 8;
			else
				RR3 &= ~8; // no IP(RR3) if not enabled(RR3M)
			if (value & 2)
				RR3M |= 16;
			else
				RR3 &= ~16;
			if (value & 0x18)
				RR3M |= 32;
			else
				RR3 &= ~32;
		}
		else
		{
			// channel B
			if (value & 1)
				RR3M |= 1;
			else
				RR3 &= ~1;
			if (value & 2)
				RR3M |= 2;
			else
				RR3 &= ~2;
			if (value & 0x18)
				RR3M |= 4;
			else
				RR3 &= ~4;
			// set or clear SCC flag if necessary (see later)
		}
	}
	else if (active_reg == 5) // Transmit parameter and control
	{
		SCC_serial_setRTS(chn, value & 2);
		SCC_serial_setDTR(chn, value & 128);
		// Tx character format & Tx CRC would be selected also here (8 bits/char and no CRC assumed)
	}
	else if (active_reg == 9) // Master interrupt control (common for both channels)
	{
		scc[0].regs[9] = value; // single WR9 (accessible by both channels)
		if (value & 0x40)
		{
			SCC_channelBreset();
		}
		if (value & 0x80)
		{
			SCC_channelAreset();
		}
		//  set or clear SCC flag accordingly (see later)
	}
	else if (active_reg == 13) // set baud rate according to WR13 and WR12
	{
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
			switch (scc[chn].regs[12])
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
			switch (scc[chn].regs[12])
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
			if (scc[chn].regs[12] == 0xfe)
				BaudRate = 600; //HSMODEM
			break;
		 case 3:
			if (scc[chn].regs[12] == 0x45)
				BaudRate = 300; //normal
			break;
		 case 4:
			if (scc[chn].regs[12] == 0xe8)
				BaudRate = 200; //normal
			break;
		 case 5:
			if (scc[chn].regs[12] == 0xfe)
				BaudRate = 300; //HSMODEM
			break;
		 case 6:
			if (scc[chn].regs[12] == 0x8c)
				BaudRate = 150; //normal
			break;
		 case 7:
			if (scc[chn].regs[12] == 0x4d)
				BaudRate = 134; //normal
			break;
		 case 8:
			if (scc[chn].regs[12] == 0xee)
				BaudRate = 110; //normal
			break;
		 case 0xd:
			if (scc[chn].regs[12] == 0x1a)
				BaudRate = 75; //normal
			break;
		 case 0x13:
			if (scc[chn].regs[12] == 0xa8)
				BaudRate = 50; //normal
			break;
		 case 0xff: // HSMODEM dummy value->silently ignored
			break;
		 default:
			Log_Printf(LOG_DEBUG, "SCC: unexpected MSB constant for baud rate\n");
			break;
		}
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
	else if (active_reg == 15) // external status int control
	{
		if (value & 1)
		{
			Log_Printf(LOG_DEBUG, "SCC: WR7 prime not yet processed\n");
		}
	}

	// set or clear SCC flag accordingly. Yes it's ugly but avoids unnecessary useless calls
	if (active_reg == 1 || active_reg == 2 || active_reg == 9)
		TriggerSCC((RR3 & RR3M) && ((0xB & scc[0].regs[9]) == 9));

	active_reg = 0; // next access for RR0 or WR0
}

static void SCC_handleWrite(uint32_t addr, uint8_t value)
{
	int channel;

	addr &= 0x6;
	channel = (addr >= 4) ? 1 : 0;  // 0 = channel A, 1 = channel B
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

void SCC_IRQ(void)
{
	uint16_t temp;
	temp = SCC_serial_getStatus(0);
	if (scc[0].regs[9] == 0x20)
		temp |= 0x800; // fake ExtStatusChange for HSMODEM install
	scc[1].regs[0] = temp & 0xFF; // RR0B
	RR3 = RR3M & (temp >> 8);
	if (RR3 && (scc[0].regs[9] & 0xB) == 9)
		TriggerSCC(true);
}


// return : vector number, or zero if no interrupt
int SCC_doInterrupt(void)
{
	int vector;
	uint8_t i;
	for (i = 0x20 ; i ; i >>= 1) // highest priority first
	{
		if (RR3 & i & RR3M)
			break ;
	}
	vector = scc[0].regs[2]; // WR2 = base of vectored interrupts for SCC
	if ((scc[0].regs[9] & 3) == 0)
		return vector; // no status included in vector
	if ((scc[0].regs[9] & 0x32) != 0)  // shouldn't happen with TOS, (to be completed if needed)
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

void SCC_Info(FILE *fp, Uint32 dummy)
{
	unsigned int i, reg;
	const char *sep;

	fprintf(fp, "SCC common:\n");
	fprintf(fp, "- RR3:  %d\n", RR3);
	fprintf(fp, "- RR3M: %d\n", RR3M);
	fprintf(fp, "- Active register: %d\n", active_reg);

	for (i = 0; i < 2; i++)
	{
		fprintf(fp, "\nSCC %c:\n", 'A' + i);
		fprintf(fp, "- Registers:\n");
		for (reg = 0; reg < ARRAY_SIZE(scc[0].regs); reg++)
		{
			sep = "";
			if (unlikely(reg % 8 == 7))
				sep = "\n";
			fprintf(fp, "  %02x%s", scc[i].regs[reg], sep);
		}
		fprintf(fp, "- Char count: %d\n", scc[i].charcount);
		fprintf(fp, "- Old status: 0x%04x\n", scc[i].oldStatus);
		fprintf(fp, "- Old TBE:    0x%04x\n", scc[i].oldTBE);
		fprintf(fp, "- %s TTY\n", scc[i].bFileHandleIsATTY ? "A" : "Not a");
	}
}
