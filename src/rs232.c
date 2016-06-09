/*
  Hatari - rs232.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  RS-232 Communications

  This is similar to the printing functions, we open a direct file
  (e.g. /dev/ttyS0) and send bytes over it.
  Using such method mimicks the ST exactly, and even allows us to connect
  to an actual ST! To wait for incoming data, we create a thread which copies
  the bytes into an input buffer. This method fits in with the internet code
  which also reads data into a buffer.
*/
const char RS232_fileid[] = "Hatari rs232.c : " __DATE__ " " __TIME__;

#include <config.h>

#if HAVE_TERMIOS_H
# include <termios.h>
# include <unistd.h>
#endif

#include <SDL.h>
#include <SDL_thread.h>
#include <errno.h>

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "m68000.h"
#include "mfp.h"
#include "rs232.h"


#define RS232_DEBUG 0

#if RS232_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


static FILE *hComIn = NULL;        /* Handle to file for reading */
static FILE *hComOut = NULL;       /* Handle to file for writing */

#define  MAX_RS232INPUT_BUFFER    2048  /* Must be ^2 */

static unsigned char InputBuffer_RS232[MAX_RS232INPUT_BUFFER];
static int InputBuffer_Head=0, InputBuffer_Tail=0;
static volatile bool bQuitThread = false;

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


#if defined(__AMIGAOS4__)

// dummy functions. REMOVE THEM LATER

int tcgetattr(int file_descriptor,struct termios *tios_p)
{
	return -1;
}

int tcsetattr(int file_descriptor,int action,struct termios *tios_p)
{
	return -1;
}

int cfsetospeed(struct termios *tios, speed_t ospeed)
{

	tios->c_ospeed = ospeed;

	return 0;
}


int cfsetispeed(struct termios *tios,speed_t ispeed)
{
	tios->c_ispeed = ispeed;

	return 0;
}

#endif  /* __AMIGAOS4__ */


/*-----------------------------------------------------------------------*/
/**
 * Set serial line parameters to "raw" mode.
 */
static bool RS232_SetRawMode(FILE *fhndl)
{
	struct termios termmode;
	int fd;

	memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */
	fd = fileno(fhndl);                         /* Get file descriptor */

	if (isatty(fd))
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
static bool RS232_SetBitsConfig(FILE *fhndl, int nCharSize, int nStopBits, bool bUseParity, bool bEvenParity)
{
	struct termios termmode;
	int fd;

	memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */
	fd = fileno(fhndl);

	if (isatty(fd))
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

	if (!hComOut && ConfigureParams.RS232.szOutFileName[0])
	{
		/* Create our COM file for output */
		hComOut = fopen(ConfigureParams.RS232.szOutFileName, "wb");
		if (hComOut)
		{
			setvbuf(hComOut, NULL, _IONBF, 0);
#if HAVE_TERMIOS_H
			/* First set the output parameters to "raw" mode */
			if (!RS232_SetRawMode(hComOut))
			{
				Log_Printf(LOG_WARN, "Can't set raw mode for %s\n",
					   ConfigureParams.RS232.szOutFileName);
			}
#endif
			Dprintf(("Successfully opened RS232 output file.\n"));
		}
		else
		{
			Log_Printf(LOG_WARN, "RS232: Failed to open output file %s\n",
				   ConfigureParams.RS232.szOutFileName);
			ok = false;
		}
	}

	if (!hComIn && ConfigureParams.RS232.szInFileName[0])
	{
		/* Create our COM file for input */
		hComIn = fopen(ConfigureParams.RS232.szInFileName, "rb");
		if (hComIn)
		{
			setvbuf(hComIn, NULL, _IONBF, 0);
#if HAVE_TERMIOS_H
			/* Now set the input parameters to "raw" mode */
			if (!RS232_SetRawMode(hComIn))
			{
				Log_Printf(LOG_WARN, "Can't set raw mode for %s\n",
					   ConfigureParams.RS232.szInFileName);
			}
#endif
			Dprintf(("Successfully opened RS232 input file.\n"));
		}
		else
		{
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
	if (hComIn)
	{
		/* Close */
		fclose(hComIn);
		hComIn = NULL;
	}
	if (hComOut)
	{
		fclose(hComOut);
		hComOut = NULL;
	}
	Dprintf(("Closed RS232 files.\n"));
}


/* thread stuff */
static SDL_sem* pSemFreeBuf;       /* Semaphore to sync free space in InputBuffer_RS232 */
static SDL_Thread *RS232Thread = NULL; /* Thread handle for reading incoming data */


/*-----------------------------------------------------------------------*/
/**
 * Add incoming bytes from other machine into our input buffer
 */
static void RS232_AddBytesToInputBuffer(unsigned char *pBytes, int nBytes)
{
	int i;

	/* Copy bytes into input buffer */
	for (i=0; i<nBytes; i++)
	{
		SDL_SemWait(pSemFreeBuf);    /* Wait for free space in buffer */
		InputBuffer_RS232[InputBuffer_Tail] = *pBytes++;
		InputBuffer_Tail = (InputBuffer_Tail+1) % MAX_RS232INPUT_BUFFER;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Thread to read incoming RS-232 data, and pass to emulator input buffer
 */
static int RS232_ThreadFunc(void *pData)
{
	int iInChar;
	unsigned char cInChar;

	/* Check for any RS-232 incoming data */
	while (!bQuitThread)
	{
		if (hComIn)
		{
			/* Read the bytes in, if we have any */
			iInChar = fgetc(hComIn);
			if (iInChar != EOF)
			{
				/* Copy into our internal queue */
				cInChar = iInChar;
				RS232_AddBytesToInputBuffer(&cInChar, 1);
				/* FIXME: Use semaphores to lock MFP variables? */
				MFP_InputOnChannel ( MFP_INT_RCV_BUF_FULL , 0 );
				Dprintf(("RS232: Read character $%x\n", iInChar));
				/* Sleep for a while */
				SDL_Delay(2);
			}
			else
			{
				/*Dprintf(("RS232: Reached end of input file!\n"));*/
				/* potential data race on hComIn modification */
				clearerr(hComIn);
				SDL_Delay(20);
			}
		}
		else
		{
			/* No RS-232 connection, sleep for 0.2s */
			SDL_Delay(200);
		}
	}

	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Initialize RS-232, start thread to wait for incoming data
 * (we will open a connection when first bytes are sent even
 *  if RS-232 isn't initialized for reading).
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
	if (hComIn)
	{
		/* Create semaphore */
		if (pSemFreeBuf == NULL)
			pSemFreeBuf = SDL_CreateSemaphore(MAX_RS232INPUT_BUFFER);
		if (pSemFreeBuf == NULL)
		{
			RS232_CloseCOMPort();
			Log_Printf(LOG_WARN, "RS232_Init: Can't create semaphore!\n");
			return;
		}

		/* Create thread to wait for incoming bytes over RS-232 */
		if (!RS232Thread)
		{
			bQuitThread = false;
#if WITH_SDL2
			RS232Thread = SDL_CreateThread(RS232_ThreadFunc, "rs232", NULL);
#else
			RS232Thread = SDL_CreateThread(RS232_ThreadFunc, NULL);
#endif
			Dprintf(("RS232 thread has been created.\n"));
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Close RS-232 connection and stop checking for incoming data.
 */
void RS232_UnInit(void)
{
	/* Close, kill thread and free resource */
	if (RS232Thread)
	{
		/* Instead of killing the thread directly, we should
		 * probably better inform it via IPC so that it can
		 * terminate gracefully... but then we would need to
		 * wait until it exits, otherwise there's a data race
		 * on accessing/modifying hComIn.
		 */
		Dprintf(("Killing RS232 thread...\n"));
		bQuitThread = true;
#if !WITH_SDL2
		SDL_KillThread(RS232Thread);
#endif
		RS232Thread = NULL;
	}
	RS232_CloseCOMPort();

	if (pSemFreeBuf)
	{
		SDL_DestroySemaphore(pSemFreeBuf);
		pSemFreeBuf = NULL;
	}
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
static void RS232_HandleUCR(Sint16 ucr)
{
#if HAVE_TERMIOS_H
	int nCharSize;                   /* Bits per character: 5, 6, 7 or 8 */
	int nStopBits;                   /* Stop bits: 0=0 bits, 1=1 bit, 2=1.5 bits, 3=2 bits */

	nCharSize = 8 - ((ucr >> 5) & 3);
	nStopBits = (ucr >> 3) & 3;

	Dprintf(("RS232_HandleUCR(%i) : character size=%i , stop bits=%i\n",
	         ucr, nCharSize, nStopBits));

	if (hComOut != NULL)
	{
		if (!RS232_SetBitsConfig(hComOut, nCharSize, nStopBits, ucr&4, ucr&2))
			Log_Printf(LOG_WARN, "RS232_HandleUCR: failed to set bits configuration for %s\n", ConfigureParams.RS232.szOutFileName);
	}

	if (hComIn != NULL)
	{
		if (!RS232_SetBitsConfig(hComIn, nCharSize, nStopBits, ucr&4, ucr&2))
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
	int fd;
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

	/* Set ouput speed: */
	if (hComOut != NULL)
	{
		memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */
		fd = fileno(hComOut);
		if (isatty(fd))
		{
			if (tcgetattr(fd, &termmode) != 0)
				return false;

			cfsetospeed(&termmode, baudtype);

			if (tcsetattr(fd, TCSADRAIN, &termmode) != 0)
				return false;
		}
	}

	/* Set input speed: */
	if (hComIn != NULL)
	{
		memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */
		fd = fileno(hComIn);
		if (isatty(fd))
		{
			if (tcgetattr(fd, &termmode) != 0)
				return false;

			cfsetispeed(&termmode, baudtype);

			if (tcsetattr(fd, TCSADRAIN, &termmode) != 0)
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
 * Pass bytes from emulator to RS-232
 */
static bool RS232_TransferBytesTo(Uint8 *pBytes, int nBytes)
{
	/* Make sure there's a RS-232 connection if it's enabled */
	if (ConfigureParams.RS232.bEnableRS232)
		RS232_OpenCOMPort();

	/* Have we connected to the RS232? */
	if (hComOut)
	{
		/* Send bytes directly to the COM file */
		if (fwrite(pBytes, 1, nBytes, hComOut))
		{
			Dprintf(("RS232: Sent %i bytes ($%x ...)\n", nBytes, *pBytes));
			MFP_InputOnChannel ( MFP_INT_TRN_BUF_EMPTY , 0 );

			return true;   /* OK */
		}
	}

	return false;  /* Failed */
}


/*-----------------------------------------------------------------------*/
/**
 * Read characters from our internal input buffer (bytes from other machine)
 */
static bool RS232_ReadBytes(Uint8 *pBytes, int nBytes)
{
	int i;

	/* Connected? */
	if (hComIn && InputBuffer_Head != InputBuffer_Tail)
	{
		/* Read bytes out of input buffer */
		for (i=0; i<nBytes; i++)
		{
			*pBytes++ = InputBuffer_RS232[InputBuffer_Head];
			InputBuffer_Head = (InputBuffer_Head+1) % MAX_RS232INPUT_BUFFER;
			SDL_SemPost(pSemFreeBuf);    /* Signal free space */
		}
		return true;
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Return true if bytes waiting!
 */
static bool RS232_GetStatus(void)
{
	/* Connected? */
	if (hComIn)
	{
		/* Do we have bytes in the input buffer? */
		if (InputBuffer_Head != InputBuffer_Tail)
			return true;
	}

	/* No, none */
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Read from the Syncronous Character Register.
 */
void RS232_SCR_ReadByte(void)
{
	M68000_WaitState(4);

	/* nothing */
}

/*-----------------------------------------------------------------------*/
/**
 * Write to the Syncronous Character Register.
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

	if (RS232_GetStatus())
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
 * (eg : 'Treasure Trap', 'The Deep' write some debug informations to RS232)
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
	Uint8 InByte = 0;

	M68000_WaitState(4);

	RS232_ReadBytes(&InByte, 1);
	IoMem[0xfffa2f] = InByte;
	Dprintf(("RS232: Read from UDR: $%x\n", (int)IoMem[0xfffa2f]));

	if (RS232_GetStatus())              /* More data waiting? */
	{
		/* Yes, generate another interrupt. */
		MFP_InputOnChannel ( MFP_INT_RCV_BUF_FULL , 0 );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Write to the USART Data Register.
 */
void RS232_UDR_WriteByte(void)
{
	Uint8 OutByte;

	M68000_WaitState(4);

	OutByte = IoMem[0xfffa2f];
	RS232_TransferBytesTo(&OutByte, 1);
	Dprintf(("RS232: Write to UDR: $%x\n", (int)IoMem[0xfffa2f]));
}
