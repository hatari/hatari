/*
  Hatari - rs232.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  RS-232 Communications

  This is similar to the printing functions, we open a direct file
  (e.g. /dev/ttyS0) and send bytes over it.
  Using such method mimicks the ST exactly, and even allows us to connect
  to an actual ST! To wait for incoming data, we create a thread which copies
  the bytes into an input buffer. This method fits in with the internet code
  which also reads data into a buffer.
*/
char RS232_rcsid[] = "Hatari $Id: rs232.c,v 1.15 2004-08-03 21:18:37 thothy Exp $";

#ifndef HAVE_TERMIOS_H
#define HAVE_TERMIOS_H 1
#endif

#if HAVE_TERMIOS_H
# include <termios.h>
# include <unistd.h>
#endif

#include <SDL.h>
#include <SDL_thread.h>
#include <errno.h>

#include "main.h"
#include "configuration.h"
#include "debug.h"
#include "mfp.h"
#include "stMemory.h"
#include "rs232.h"


#define RS232_DEBUG 0

#if RS232_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


#ifndef HAVE_CFMAKERAW
# if defined(__BEOS__) || (defined(__sun) && defined(__SVR4))
#  define HAVE_CFMAKERAW 0
# else
#  define HAVE_CFMAKERAW 1
# endif
#endif


BOOL bConnectedRS232 = FALSE;       /* Connection to RS232? */
static FILE *hComIn = NULL;         /* Handle to file for reading */
static FILE *hComOut = NULL;        /* Handle to file for writing */
SDL_Thread *RS232Thread = NULL;     /* Thread handle for reading incoming data */
unsigned char InputBuffer_RS232[MAX_RS232INPUT_BUFFER];
int InputBuffer_Head=0, InputBuffer_Tail=0;
SDL_sem* pSemFreeBuf;               /* Semaphore to sync free space in InputBuffer_RS232 */


#if HAVE_TERMIOS_H && !HAVE_CFMAKERAW
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
/*
  Initialize RS-232, start thread to wait for incoming data
  (we will open a connection when first bytes are sent).
*/
void RS232_Init(void)
{
	if (ConfigureParams.RS232.bEnableRS232)
	{
		/* Create semaphore */
		if (pSemFreeBuf == NULL)
			pSemFreeBuf = SDL_CreateSemaphore(MAX_RS232INPUT_BUFFER);
		if (pSemFreeBuf == NULL)
		{
			fprintf(stderr, "RS232_Init: Can't create semaphore!\n");
			return;
		}

		if (!bConnectedRS232)
			RS232_OpenCOMPort();

		/* Create thread to wait for incoming bytes over RS-232 */
		if (!RS232Thread)
		{
			RS232Thread = SDL_CreateThread(RS232_ThreadFunc, NULL);
			Dprintf(("RS232 thread has been created.\n"));
		}
	}
}


/*-----------------------------------------------------------------------*/
/*
  Close RS-232 connection and stop checking for incoming data.
*/
void RS232_UnInit(void)
{
	/* Close, kill thread and free resource */
	if (RS232Thread)
	{
		/* Instead of killing the thread directly, we should probably better
		   inform it via IPC so that it can terminate gracefully... */
		Dprintf(("Killing RS232 thread...\n"));
		SDL_KillThread(RS232Thread);
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
/*
  Set serial line parameters to "raw" mode.
*/
#if HAVE_TERMIOS_H
static BOOL RS232_SetRawMode(FILE *fhndl)
{
	struct termios termmode;
	int fd;

	memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */
	fd = fileno(fhndl);                         /* Get file descriptor */

	if (isatty(fd))
	{
		if (tcgetattr(fd, &termmode) != 0)
			return FALSE;

		/* Set "raw" mode: */
		termmode.c_cc[VMIN] = 1;
		termmode.c_cc[VTIME] = 0;
		cfmakeraw(&termmode);
		if (tcsetattr(fd, TCSADRAIN, &termmode) != 0)
			return FALSE;
	}

	return TRUE;
}
#endif /* HAVE_TERMIOS_H */


/*-----------------------------------------------------------------------*/
/*
  Open file on COM port.
*/
BOOL RS232_OpenCOMPort(void)
{
	bConnectedRS232 = FALSE;

	/* Create our COM file for output */
	hComOut = fopen(ConfigureParams.RS232.szOutFileName, "wb"); 
	if (hComOut == NULL)
	{
		Dprintf(("RS232: Failed to open output file %s\n",
		         ConfigureParams.RS232.szOutFileName));
		return FALSE;
	}
	setvbuf(hComOut, NULL, _IONBF, 0);

	/* Create our COM file for input */
	hComIn = fopen(ConfigureParams.RS232.szInFileName, "rb"); 
	if (hComIn == NULL)
	{
		Dprintf(("RS232: Failed to open input file %s\n",
		         ConfigureParams.RS232.szInFileName));
		fclose(hComOut); hComOut = NULL;
		return FALSE;
	}
	setvbuf(hComIn, NULL, _IONBF, 0);

#if HAVE_TERMIOS_H
	/* First set the output parameters to "raw" mode */
	if (!RS232_SetRawMode(hComOut))
	{
		fprintf(stderr, "Can't set raw mode for %s\n",
		        ConfigureParams.RS232.szOutFileName);
	}

	/* Now set the input parameters to "raw" mode */
	if (!RS232_SetRawMode(hComIn))
	{
		fprintf(stderr, "Can't set raw mode for %s\n",
		        ConfigureParams.RS232.szInFileName);
	}
#endif

	/* Set all OK */
	bConnectedRS232 = TRUE;

	Dprintf(("Successfully opened RS232 files.\n"));

	return TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Close file on COM port
*/
void RS232_CloseCOMPort(void)
{
	/* Do have file open? */
	if (bConnectedRS232)
	{
		bConnectedRS232 = FALSE;

		/* Close */
		fclose(hComIn);
		hComIn = NULL;

		fclose(hComOut);
		hComOut = NULL;

		Dprintf(("Closed RS232 files.\n"));
	}
}


/*-----------------------------------------------------------------------*/
/*
  Set hardware configuration of RS-232:
  - Bits per character
  - Parity
  - Start/stop bits
*/
#if HAVE_TERMIOS_H
static BOOL RS232_SetBitsConfig(int fd, int nCharSize, int nStopBits, BOOL bUseParity, BOOL bEvenParity)
{
	struct termios termmode;

	memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */

	if (isatty(fd))
	{
		if (tcgetattr(fd, &termmode) != 0)
		{
			Dprintf(("RS232_SetBitsConfig: tcgetattr failed.\n"));
			return FALSE;
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
			return FALSE;
		}
	}

	return TRUE;
}
#endif /* HAVE_TERMIOS_H */


/*-----------------------------------------------------------------------*/
/*
  Set hardware configuration of RS-232 according to the USART control register.

  ucr: USART Control Register
    Bit 0: unused
    Bit 1: 0-Odd Parity, 1-Even Parity
    Bit 2: 0-No Parity, 1-Parity
    Bits 3,4: Start/Stop bits
      0 0 : 0-Start, 0-Stop    Synchronous
      0 1 : 0-Start, 1-Stop    Asynchronous
      1 0 : 1-Start, 1.5-Stop  Asynchronous
      1 1 : 1-Start, 2-Stop    Asynchronous
    Bits 5,6: 'WordLength'
      0 0 : 8 Bits
      0 1 : 7 Bits
      1 0 : 6 Bits
      1 1 : 5 Bits
    Bit 7: Frequency from TC and RC
*/
void RS232_HandleUCR(short int ucr)
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
		if (!RS232_SetBitsConfig(fileno(hComOut), nCharSize, nStopBits, ucr&4, ucr&2))
			fprintf(stderr, "Failed to set bits configuration for hComOut!\n");
	}

	if (hComIn != NULL)
	{
		if (!RS232_SetBitsConfig(fileno(hComIn), nCharSize, nStopBits, ucr&4, ucr&2))
			fprintf(stderr, "Failed to set bits configuration for hComIn!\n");
	}
#endif /* HAVE_TERMIOS_H */
}


/*-----------------------------------------------------------------------*/
/*
  Set baud rate configuration of RS-232.
*/
BOOL RS232_SetBaudRate(int nBaud)
{
#if HAVE_TERMIOS_H
	int i;
	int fd;
	speed_t baudtype;
	struct termios termmode;
	static int baudtable[][2] =
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
		{ 230400, B230400 },
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
		return FALSE;
	}

	/* Set ouput speed: */
	if (hComOut != NULL)
	{
		memset (&termmode, 0, sizeof(termmode));    /* Init with zeroes */
		fd = fileno(hComOut);
		if (isatty(fd))
		{
			if (tcgetattr(fd, &termmode) != 0)
				return FALSE;

			cfsetospeed(&termmode, baudtype);

			if (tcsetattr(fd, TCSADRAIN, &termmode) != 0)
				return FALSE;
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
				return FALSE;

			cfsetispeed(&termmode, baudtype);

			if (tcsetattr(fd, TCSADRAIN, &termmode) != 0)
				return FALSE;
		}
	}
#endif /* HAVE_TERMIOS_H */

	return TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Set baud rate configuration of RS-232 according to the Timer-D hardware
  registers.
*/
void RS232_SetBaudRateFromTimerD(void)
{
	int nTimerD_CR, nTimerD_DR, nBaudRate;

	nTimerD_CR = STRam[0xfffa1d] & 0x07;
	nTimerD_DR = STRam[0xfffa25];

	if (!nTimerD_CR)
		return;

	/* Calculate baud rate: (MFP/Timer-D is supplied with 2.4576 MHz) */
	nBaudRate = 2457600 / nTimerD_DR / 2;
	if (STRam[0xfffa29] & 0x80)
	{
		nBaudRate /= 16;
	}
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


/*-----------------------------------------------------------------------*/
/*
  Set flow control configuration of RS-232.
*/
void RS232_SetFlowControl(int ctrl)
{
	Dprintf(("RS232_SetFlowControl(%i)\n", ctrl));

	/* Not yet written */
}


/*----------------------------------------------------------------------- */
/*
  Pass bytes from emulator to RS-232
*/
BOOL RS232_TransferBytesTo(unsigned char *pBytes, int nBytes)
{
	/* Do need to open a connection to RS232? */
	if (!bConnectedRS232)
	{
		/* Do have RS-232 enabled? */
		if (ConfigureParams.RS232.bEnableRS232)
			bConnectedRS232 = RS232_OpenCOMPort();
	}

	/* Have we connected to the RS232? */
	if (bConnectedRS232)
	{
		/* Send bytes directly to the COM file */
		if (fwrite(pBytes, 1, nBytes, hComOut))
		{
			Dprintf(("RS232: Sent %i bytes ($%x ...)\n", nBytes, *pBytes));
			MFP_InputOnChannel(MFP_TRNBUFEMPTY_BIT, MFP_IERA, &MFP_IPRA);

			return(TRUE);   /* OK */
		}
	}

	return(FALSE);  /* Failed */
}


/*-----------------------------------------------------------------------*/
/*
  Read characters from our internal input buffer (bytes from other machine)
*/
BOOL RS232_ReadBytes(unsigned char *pBytes, int nBytes)
{
	int i;

	/* Connected? */
	if (bConnectedRS232 && InputBuffer_Head != InputBuffer_Tail)
	{
		/* Read bytes out of input buffer */
		for (i=0; i<nBytes; i++)
		{
			*pBytes++ = InputBuffer_RS232[InputBuffer_Head];
			InputBuffer_Head = (InputBuffer_Head+1) % MAX_RS232INPUT_BUFFER;
			SDL_SemPost(pSemFreeBuf);    /* Signal free space */
		}
		return(TRUE);
	}

	return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Return TRUE if bytes waiting!
*/
BOOL RS232_GetStatus(void)
{
	/* Connected? */
	if (bConnectedRS232)
	{
		/* Do we have bytes in the input buffer? */
		if (InputBuffer_Head != InputBuffer_Tail)
			return(TRUE);
	}

	/* No, none */
	return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Add incoming bytes from other machine into our input buffer
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
/*
  Thread to read incoming RS-232 data, and pass to emulator input buffer
*/
int RS232_ThreadFunc(void *pData)
{
	int iInChar;
	char cInChar;

	/* Check for any RS-232 incoming data */
	while (TRUE)
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
				MFP_InputOnChannel(MFP_RCVBUFFULL_BIT, MFP_IERA, &MFP_IPRA);
				Dprintf(("RS232: Read character $%x\n", iInChar));
			}
			else
			{
				/*Dprintf(("RS232: Reached end of input file!\n"));*/
				clearerr(hComIn);
				SDL_Delay(20);
			}

			/* Sleep for a while */
			SDL_Delay(2);
		}
		else
		{
			/* No RS-232 connection, sleep for 20ms */
			SDL_Delay(20);
		}
	}

	return(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Read from the Syncronous Character Register.
*/
void RS232_SCR_ReadByte(void)
{
}

/*-----------------------------------------------------------------------*/
/*
  Write to the Syncronous Character Register.
*/
void RS232_SCR_WriteByte(void)
{
	/*Dprintf(("RS232: Write to SCR: $%x\n", (int)STRam[0xfffa27]));*/
}


/*-----------------------------------------------------------------------*/
/*
  Read from the USART Control Register.
*/
void RS232_UCR_ReadByte(void)
{
	Dprintf(("RS232: Read from UCR: $%x\n", (int)STRam[0xfffa29]));
}

/*-----------------------------------------------------------------------*/
/*
  Write to the USART Control Register.
*/
void RS232_UCR_WriteByte(void)
{
	Dprintf(("RS232: Write to UCR: $%x\n", (int)STRam[0xfffa29]));

	if (bConnectedRS232)
		RS232_HandleUCR(STRam[0xfffa29]);
}


/*-----------------------------------------------------------------------*/
/*
  Read from the Receiver Status Register.
*/
void RS232_RSR_ReadByte(void)
{
	if (RS232_GetStatus())
		STRam[0xfffa2b] |= 0x80;        /* Buffer full */
	else
		STRam[0xfffa2b] &= ~0x80;       /* Buffer not full */

	Dprintf(("RS232: Read from RSR: $%x\n", (int)STRam[0xfffa2b]));
}

/*-----------------------------------------------------------------------*/
/*
  Write to the Receiver Status Register.
*/
void RS232_RSR_WriteByte(void)
{
	Dprintf(("RS232: Write to RSR: $%x\n", (int)STRam[0xfffa2b]));
}


/*-----------------------------------------------------------------------*/
/*
  Read from the Transmitter Status Register.
*/
void RS232_TSR_ReadByte(void)
{
	if (ConfigureParams.RS232.bEnableRS232)
		STRam[0xfffa2d] |= 0x80;        /* Buffer empty */
	else
		STRam[0xfffa2d] &= ~0x80;       /* Buffer not empty */

	Dprintf(("RS232: Read from TSR: $%x\n", (int)STRam[0xfffa2d]));
}

/*-----------------------------------------------------------------------*/
/*
  Write to the Transmitter Status Register.
*/
void RS232_TSR_WriteByte(void)
{
	Dprintf(("RS232: Write to TSR: $%x\n", (int)STRam[0xfffa2d]));
}


/*-----------------------------------------------------------------------*/
/*
  Read from the USART Data Register.
*/
void RS232_UDR_ReadByte(void)
{
	unsigned char InByte;

	RS232_ReadBytes(&InByte, 1);
	STRam[0xfffa2f] = InByte;
	Dprintf(("RS232: Read from UDR: $%x\n", (int)STRam[0xfffa2f]));

	if (RS232_GetStatus())              /* More data waiting? */
	{
		/* Yes, generate another interrupt. */
		MFP_InputOnChannel(MFP_RCVBUFFULL_BIT, MFP_IERA, &MFP_IPRA);
	}
}

/*-----------------------------------------------------------------------*/
/*
  Write to the USART Data Register.
*/
void RS232_UDR_WriteByte(void)
{
	unsigned char OutByte;

	OutByte = STRam[0xfffa2f];
	RS232_TransferBytesTo(&OutByte, 1);
	Dprintf(("RS232: Write to UDR: $%x\n", (int)STRam[0xfffa2f]));
}
