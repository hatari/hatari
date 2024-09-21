/*
  Hatari - rs232.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  RS-232 Communications

  This is similar to the printing functions, we open a direct file
  (e.g. /dev/ttyS0) and send bytes over it.
  Using such method mimics the ST exactly, and even allows us to connect
  to an actual ST!
*/
const char RS232_fileid[] = "Hatari rs232.c";

#include <errno.h>

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "ioMem.h"
#include "m68000.h"
#include "mfp.h"
#include "rs232.h"

/* AmigaOS has termios.h, but no tcsetattr() and friends - d'oh! */
#if !defined(HAVE_TCSETATTR)
#undef HAVE_TERMIOS_H
#endif

#if HAVE_TERMIOS_H
# include <termios.h>
# include <unistd.h>
#endif
#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#define RS232_DEBUG 0

#if RS232_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif





struct RS232_s {

	FILE	*ReadFile;		/* Handle to file for reading */
	int	Read_fd;		/* Corresponding file descriptor */
	bool	Read_fd_IsATTY;		/* true if fd is a tty */
	FILE	*WriteFile;		/* Same for writing */
	int	Write_fd;
	bool	Write_fd_IsATTY;

	bool	ByteReceived;
	uint8_t RxByte;
};


static struct RS232_s RS232_MFP;




#if HAVE_TERMIOS_H

#if !HAVE_CFMAKERAW
static inline void cfmakeraw(struct termios *termios_p)
{
	termios_p->c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
	termios_p->c_oflag &= ~OPOST;
	termios_p->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	termios_p->c_cflag &= ~(CSIZE|PARENB);
	termios_p->c_cflag |= CS8;
}
#endif


/*-----------------------------------------------------------------------*/
/**
 * Set serial line parameters to "raw" mode.
 */
static bool RS232_SetRawMode ( int fd , bool IsATTY )
{
	struct termios termmode;

	memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */

	if ( IsATTY )
	{
		if (tcgetattr(fd, &termmode) != 0)
			return false;

		/* Set "raw" mode: */
		termmode.c_cc[VMIN] = 1;
		termmode.c_cc[VTIME] = 0;
		cfmakeraw(&termmode);
		if (tcsetattr(fd, TCSADRAIN, &termmode) != 0)
			return false;
	}

	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Set hardware configuration of RS-232:
 * - Bits per character
 * - Parity
 * - Start/stop bits
 */
static bool RS232_SetBitsConfig ( int fd, bool IsATTY, int nCharSize, int nStopBits, bool bUseParity, bool bEvenParity )
{
	struct termios termmode;

	memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */

	if ( IsATTY )
	{
		if (tcgetattr(fd, &termmode) != 0)
		{
			Dprintf(("RS232_SetBitsConfig: tcgetattr failed.\n"));
			return false;
		}

		/* Set the character size: */
		termmode.c_cflag &= ~CSIZE;
		switch (nCharSize)
		{
		 case 8: termmode.c_cflag |= CS8; break;
		 case 7: termmode.c_cflag |= CS7; break;
		 case 6: termmode.c_cflag |= CS6; break;
		 case 5: termmode.c_cflag |= CS5; break;
		}

		/* Set stop bits: */
		if (nStopBits >= 2)
			termmode.c_oflag |= CSTOPB;
		else
			termmode.c_oflag &= ~CSTOPB;

		/* Parity bit: */
		if (bUseParity)
			termmode.c_cflag |= PARENB;
		else
			termmode.c_cflag &= ~PARENB;

		if (bEvenParity)
			termmode.c_cflag &= ~PARODD;
		else
			termmode.c_cflag |= PARODD;

		/* Now store the configuration: */
		if (tcsetattr(fd, TCSADRAIN, &termmode) != 0)
		{
			Dprintf(("RS232_SetBitsConfig: tcsetattr failed.\n"));
			return false;
		}
	}

	return true;
}

#endif /* HAVE_TERMIOS_H */


/*-----------------------------------------------------------------------*/
/**
 * Open file on COM port.
 */
static bool RS232_OpenCOMPort(void)
{
	bool ok = true;

	RS232_MFP.ByteReceived = false;

	if (!RS232_MFP.WriteFile && ConfigureParams.RS232.szOutFileName[0])
	{
		/* Create our serial file for output */
		RS232_MFP.WriteFile = File_Open(ConfigureParams.RS232.szOutFileName, "wb");
		if ( RS232_MFP.WriteFile )
		{
			setvbuf(RS232_MFP.WriteFile, NULL, _IONBF, 0);

			RS232_MFP.Write_fd = fileno ( RS232_MFP.WriteFile );
			if ( isatty ( RS232_MFP.Write_fd ) )
				RS232_MFP.Write_fd_IsATTY = true;
			else
				RS232_MFP.Write_fd_IsATTY = false;
#if HAVE_TERMIOS_H
			/* First set the output parameters to "raw" mode */
			if (!RS232_SetRawMode ( RS232_MFP.Write_fd , RS232_MFP.Write_fd_IsATTY ))
			{
				Log_Printf(LOG_WARN, "Can't set raw mode for %s\n",
					   ConfigureParams.RS232.szOutFileName);
			}
#endif
			Dprintf(("Successfully opened RS232 output file.\n"));
		}
		else
		{
			RS232_MFP.Write_fd = -1;
			Log_Printf(LOG_WARN, "RS232: Failed to open output file %s\n",
				   ConfigureParams.RS232.szOutFileName);
			ok = false;
		}
	}

	if (!RS232_MFP.ReadFile && ConfigureParams.RS232.szInFileName[0])
	{
		/* Create our serial file for output */
		RS232_MFP.ReadFile = File_Open(ConfigureParams.RS232.szInFileName, "rb");
		if ( RS232_MFP.ReadFile )
		{
			setvbuf(RS232_MFP.ReadFile, NULL, _IONBF, 0);

			RS232_MFP.Read_fd = fileno ( RS232_MFP.ReadFile );
			if ( isatty ( RS232_MFP.Read_fd ) )
				RS232_MFP.Read_fd_IsATTY = true;
			else
				RS232_MFP.Read_fd_IsATTY = false;
#if HAVE_TERMIOS_H
			/* Now set the input parameters to "raw" mode */
			if (!RS232_SetRawMode ( RS232_MFP.Read_fd, RS232_MFP.Read_fd_IsATTY ))
			{
				Log_Printf(LOG_WARN, "Can't set raw mode for %s\n",
					   ConfigureParams.RS232.szInFileName);
			}
#endif
			Dprintf(("Successfully opened RS232 input file.\n"));
		}
		else
		{
			RS232_MFP.Read_fd = -1;
			Log_Printf(LOG_WARN, "RS232: Failed to open input file %s\n",
				   ConfigureParams.RS232.szInFileName);
			ok = false;
		}
	}

	return ok;
}


/*-----------------------------------------------------------------------*/
/**
 * Close file on COM port
 */
static void RS232_CloseCOMPort(void)
{
	/* Write side needs to be closed first.  Otherwise Hatari
	 * instances at both ends of a "RS-232" FIFO file would freeze
	 * when Hatari exists or RS-232 configuration is changed
	 * (with this, only one of them freezes until other
	 * end of a FIFO also closes the "device" file(s)).
	 */
	if ( RS232_MFP.WriteFile )
	{
		File_Close ( RS232_MFP.WriteFile );
		RS232_MFP.WriteFile = NULL;
	}
	if ( RS232_MFP.ReadFile )
	{
		File_Close( RS232_MFP.ReadFile );
		RS232_MFP.ReadFile = NULL;
	}
	Dprintf(("Closed RS232 files.\n"));
}


/*-----------------------------------------------------------------------*/
void RS232_Update(void)
{
	if (!RS232_MFP.ByteReceived && RS232_MFP.ReadFile && File_InputAvailable(RS232_MFP.ReadFile))
	{
		int ch = fgetc(RS232_MFP.ReadFile);

		if (ch != EOF)
		{
			RS232_MFP.RxByte = ch;
			RS232_MFP.ByteReceived = true;
			MFP_InputOnChannel(pMFP_Main, MFP_INT_RCV_BUF_FULL, 0);
		}
		else
		{
			RS232_MFP.RxByte = 0xff;
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Initialize RS-232 (open and configure device handles if enabled).
 */
void RS232_Init(void)
{
	if (ConfigureParams.RS232.bEnableRS232)
	{
		if (!RS232_OpenCOMPort())
		{
			RS232_CloseCOMPort();
			Log_AlertDlg(LOG_ERROR, "RS232 input or output file open failed. RS232 support disabled.");
			ConfigureParams.RS232.bEnableRS232 = false;
			return;
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Close RS-232 connection and stop checking for incoming data.
 */
void RS232_UnInit(void)
{
	RS232_CloseCOMPort();
}


/*-----------------------------------------------------------------------*/
/**
 * Set hardware configuration of RS-232 according to the USART control register.
 *
 * ucr: USART Control Register
 *   Bit 0: unused
 *   Bit 1: 0-Odd Parity, 1-Even Parity
 *   Bit 2: 0-No Parity, 1-Parity
 *   Bits 3,4: Start/Stop bits
 *     0 0 : 0-Start, 0-Stop    Synchronous
 *     0 1 : 0-Start, 1-Stop    Asynchronous
 *     1 0 : 1-Start, 1.5-Stop  Asynchronous
 *     1 1 : 1-Start, 2-Stop    Asynchronous
 *   Bits 5,6: 'WordLength'
 *     0 0 : 8 Bits
 *     0 1 : 7 Bits
 *     1 0 : 6 Bits
 *     1 1 : 5 Bits
 *   Bit 7: Frequency from TC and RC
 */
static void RS232_HandleUCR(int16_t ucr)
{
#if HAVE_TERMIOS_H
	int nCharSize;                   /* Bits per character: 5, 6, 7 or 8 */
	int nStopBits;                   /* Stop bits: 0=0 bits, 1=1 bit, 2=1.5 bits, 3=2 bits */

	nCharSize = 8 - ((ucr >> 5) & 3);
	nStopBits = (ucr >> 3) & 3;

	Dprintf(("RS232_HandleUCR(%i) : character size=%i , stop bits=%i\n",
	         ucr, nCharSize, nStopBits));

	if ( RS232_MFP.WriteFile )
	{
		if ( !RS232_SetBitsConfig ( RS232_MFP.Write_fd, RS232_MFP.Write_fd_IsATTY, nCharSize, nStopBits, ucr&4, ucr&2 ) )
			Log_Printf(LOG_WARN, "RS232_HandleUCR: failed to set bits configuration for %s\n", ConfigureParams.RS232.szOutFileName);
	}

	if ( RS232_MFP.ReadFile )
	{
		if ( !RS232_SetBitsConfig ( RS232_MFP.Read_fd, RS232_MFP.Read_fd_IsATTY, nCharSize, nStopBits, ucr&4, ucr&2 ) )
			Log_Printf(LOG_WARN, "RS232_HandleUCR: failed to set bits configuration for %s\n", ConfigureParams.RS232.szInFileName);
	}
#endif /* HAVE_TERMIOS_H */
}


/*-----------------------------------------------------------------------*/
/**
 * Set baud rate configuration of RS-232.
 */
static bool RS232_SetBaudRate(int nBaud)
{
#if HAVE_TERMIOS_H
	int i;
	speed_t baudtype;
	struct termios termmode;
	static const int baudtable[][2] =
	{
		{ 50, B50 },
		{ 75, B75 },
		{ 110, B110 },
		{ 134, B134 },
		{ 150, B150 },
		{ 200, B200 },
		{ 300, B300 },
		{ 600, B600 },
		{ 1200, B1200 },
		{ 1800, B1800 },
		{ 2400, B2400 },
		{ 4800, B4800 },
		{ 9600, B9600 },
		{ 19200, B19200 },
		{ 38400, B38400 },
		{ 57600, B57600 },
		{ 115200, B115200 },
#ifdef B230400                 /* B230400 is not defined on all systems */
		{ 230400, B230400 },
#endif
		{ -1, -1 }
	};

	Dprintf(("RS232_SetBaudRate(%i)\n", nBaud));

	/* Convert baud number to baud termios constant: */
	baudtype = -1;
	for (i = 0; baudtable[i][0] != -1; i++)
	{
		if (baudtable[i][0] == nBaud)
		{
			baudtype = baudtable[i][1];
			break;
		}
	}

	if (baudtype == (speed_t)-1)
	{
		Dprintf(("RS232_SetBaudRate: Unsupported baud rate %i.\n", nBaud));
		return false;
	}

	/* Set output speed: */
	if ( RS232_MFP.WriteFile )
	{
		memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */
		if ( RS232_MFP.Write_fd_IsATTY )
		{
			if (tcgetattr ( RS232_MFP.Write_fd, &termmode ) != 0)
				return false;

			cfsetospeed(&termmode, baudtype);

			if (tcsetattr( RS232_MFP.Write_fd, TCSADRAIN, &termmode ) != 0)
				return false;
		}
	}

	/* Set input speed: */
	if ( RS232_MFP.ReadFile )
	{
		memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */
		if ( RS232_MFP.Read_fd_IsATTY )
		{
			if (tcgetattr ( RS232_MFP.Read_fd, &termmode ) != 0)
				return false;

			cfsetispeed(&termmode, baudtype);

			if (tcsetattr ( RS232_MFP.Read_fd, TCSADRAIN, &termmode ) != 0)
				return false;
		}
	}
#endif /* HAVE_TERMIOS_H */

	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Set baud rate configuration of RS-232 according to the Timer-D hardware
 * registers.
 */
void RS232_SetBaudRateFromTimerD(void)
{
	int nTimerD_CR, nTimerD_DR, nBaudRate;

	nTimerD_CR = IoMem[0xfffa1d] & 0x07;
	nTimerD_DR = IoMem[0xfffa25];

	if (!nTimerD_CR)
		return;

	if ( nTimerD_DR == 0 )
		nTimerD_DR = 256;		/* In MFP, a data register=0 is in fact 256 */

	/* Calculate baud rate: (MFP/Timer-D is supplied with 2.4576 MHz) */
	nBaudRate = 2457600 / nTimerD_DR / 2;

	/*if (IoMem[0xfffa29] & 0x80)*/  /* We only support the by-16 prescaler */
	nBaudRate /= 16;

	switch (nTimerD_CR)
	{
		case 1:  nBaudRate /= 4;  break;
		case 2:  nBaudRate /= 10;  break;
		case 3:  nBaudRate /= 16;  break;
		case 4:  nBaudRate /= 50;  break;
		case 5:  nBaudRate /= 64;  break;
		case 6:  nBaudRate /= 100;  break;
		case 7:  nBaudRate /= 200;  break;
	}

	/* Adjust some ugly baud rates from TOS to more reasonable values: */
	switch (nBaudRate)
	{
		case 80:  nBaudRate = 75;  break;
		case 109:  nBaudRate = 110;  break;
		case 120:  nBaudRate = 110;  break;
		case 1745:  nBaudRate = 1800;  break;
		case 1920:  nBaudRate = 1800;  break;
	}

	RS232_SetBaudRate(nBaudRate);
}


/*----------------------------------------------------------------------- */
/**
 * Get value of DCD and CTS input signals, as returned by the underlying OS
 */
void	RS232_Get_DCD_CTS ( uint8_t *pDCD , uint8_t *pCTS )
{
	/* Set default value for DCD and CTS in case ioctl() fails or RS232 emulation is not enabled */
	*pDCD = 1;
	*pCTS = 1;

	if (!ConfigureParams.RS232.bEnableRS232)
		return;

#if defined(HAVE_SYS_IOCTL_H) && defined(TIOCMGET)
	int	status = 0;
	if ( ( RS232_MFP.Read_fd >= 0 ) && RS232_MFP.Read_fd_IsATTY )
	{
		if ( ioctl ( RS232_MFP.Read_fd , TIOCMGET , &status ) < 0 )
		{
			Log_Printf(LOG_DEBUG, "RS232_Get_DCD_CTS: Can't get status for DCD/CTS errno=%d\n" , errno);
		}
		else
		{
			if ( status & TIOCM_CAR )
				*pDCD = 1;
			else
				*pDCD = 0;

			if ( status & TIOCM_CTS )
				*pCTS = 1;
			else
				*pCTS = 0;

		}
	}
//fprintf ( stderr , "RS232_Get_DCD_CTS dcd=%d cts=%d\n" , *pDCD, *pCTS );
#endif
}


/*----------------------------------------------------------------------- */
/**
 * Pass bytes from emulator to RS-232
 */
static bool RS232_TransferBytesTo(uint8_t *pBytes, int nBytes)
{
	/* Make sure there's a RS-232 connection if it's enabled */
	if (ConfigureParams.RS232.bEnableRS232)
		RS232_OpenCOMPort();

	/* Have we connected to the RS232 ? */
	if ( RS232_MFP.WriteFile )
	{
		/* Send bytes directly to the serial file */
		if (fwrite(pBytes, 1, nBytes, RS232_MFP.WriteFile))
		{
			Dprintf(("RS232: Sent %i bytes ($%x ...)\n", nBytes, *pBytes));
			MFP_InputOnChannel ( pMFP_Main , MFP_INT_TRN_BUF_EMPTY , 0 );

			return true;   /* OK */
		}
	}

	return false;  /* Failed */
}


/*-----------------------------------------------------------------------*/
/**
 * Read from the Synchronous Character Register.
 */
void RS232_SCR_ReadByte(void)
{
	M68000_WaitState(4);

	/* nothing */
}

/*-----------------------------------------------------------------------*/
/**
 * Write to the Synchronous Character Register.
 */
void RS232_SCR_WriteByte(void)
{
	M68000_WaitState(4);

	/*Dprintf(("RS232: Write to SCR: $%x\n", (int)IoMem[0xfffa27]));*/
}


/*-----------------------------------------------------------------------*/
/**
 * Read from the USART Control Register.
 */
void RS232_UCR_ReadByte(void)
{
	M68000_WaitState(4);

	Dprintf(("RS232: Read from UCR: $%x\n", (int)IoMem[0xfffa29]));
}

/*-----------------------------------------------------------------------*/
/**
 * Write to the USART Control Register.
 */
void RS232_UCR_WriteByte(void)
{
	M68000_WaitState(4);

	Dprintf(("RS232: Write to UCR: $%x\n", (int)IoMem[0xfffa29]));

	RS232_HandleUCR(IoMem[0xfffa29]);
}


/*-----------------------------------------------------------------------*/
/**
 * Read from the Receiver Status Register.
 */
void RS232_RSR_ReadByte(void)
{
	M68000_WaitState(4);

	if ( RS232_MFP.ByteReceived )
		IoMem[0xfffa2b] |= 0x80;        /* Buffer full */
	else
		IoMem[0xfffa2b] &= ~0x80;       /* Buffer not full */

	Dprintf(("RS232: Read from RSR: $%x\n", (int)IoMem[0xfffa2b]));
}

/*-----------------------------------------------------------------------*/
/**
 * Write to the Receiver Status Register.
 */
void RS232_RSR_WriteByte(void)
{
	M68000_WaitState(4);

	Dprintf(("RS232: Write to RSR: $%x\n", (int)IoMem[0xfffa2b]));
}


/*-----------------------------------------------------------------------*/
/**
 * Read from the Transmitter Status Register.
 * When RS232 emulation is not enabled, we still return 0x80 to allow
 * some games to work when they don't require send/receive on the RS232 port
 * (eg : 'Treasure Trap', 'The Deep' write some debug information to RS232)
 */
void RS232_TSR_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa2d] |= 0x80;        /* Buffer empty */

	Dprintf(("RS232: Read from TSR: $%x\n", (int)IoMem[0xfffa2d]));
}

/*-----------------------------------------------------------------------*/
/**
 * Write to the Transmitter Status Register.
 */
void RS232_TSR_WriteByte(void)
{
	M68000_WaitState(4);

	Dprintf(("RS232: Write to TSR: $%x\n", (int)IoMem[0xfffa2d]));
}


/*-----------------------------------------------------------------------*/
/**
 * Read from the USART Data Register.
 */
void RS232_UDR_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa2f] = RS232_MFP.RxByte;
	RS232_MFP.ByteReceived = false;
	Dprintf(("RS232: Read from UDR: $%x\n", (int)IoMem[0xfffa2f]));
}

/*-----------------------------------------------------------------------*/
/**
 * Write to the USART Data Register.
 */
void RS232_UDR_WriteByte(void)
{
	uint8_t OutByte;

	M68000_WaitState(4);

	OutByte = IoMem[0xfffa2f];
	RS232_TransferBytesTo(&OutByte, 1);
	Dprintf(("RS232: Write to UDR: $%x\n", (int)IoMem[0xfffa2f]));
}
