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
char RS232_rcsid[] = "Hatari $Id: rs232.c,v 1.7 2004-04-23 15:33:59 thothy Exp $";

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


BOOL bConnectedRS232 = FALSE;       /* Connection to RS232? */
static FILE *hComIn = NULL;         /* Handle to file for reading */
static FILE *hComOut = NULL;        /* Handle to file for writing */
SDL_Thread *RS232Thread = NULL;     /* Thread handle for reading incoming data */
unsigned char InputBuffer_RS232[MAX_RS232INPUT_BUFFER];
int InputBuffer_Head=0, InputBuffer_Tail=0;


/*-----------------------------------------------------------------------*/
/*
  Initialize RS-232, start thread to wait for incoming data
  (we will open a connection when first bytes are sent).
*/
void RS232_Init(void)
{
	if (ConfigureParams.RS232.bEnableRS232)
	{
		/* Create thread to wait for incoming bytes over RS-232 */
		RS232Thread = SDL_CreateThread(RS232_ThreadFunc, NULL);
		Dprintf(("RS232 thread has been created.\n"));
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
		SDL_KillThread(RS232Thread);
		RS232Thread = NULL;
	}
	RS232_CloseCOMPort();
}


/*-----------------------------------------------------------------------*/
/*
  Open file on COM port.
*/
BOOL RS232_OpenCOMPort(void)
{
	bConnectedRS232 = FALSE;

	/* Create our COM file for output */
	hComOut = fopen(ConfigureParams.RS232.szDeviceFileName, "wb"); 
	if (hComOut == NULL)
	{
		Dprintf(("RS232: Failed to open output file %s\n",
		         ConfigureParams.RS232.szDeviceFileName));
		return FALSE;
	}
	setvbuf(hComOut, NULL, _IONBF, 0);

	/* Create our COM file for input */
	hComIn = fopen(ConfigureParams.RS232.szDeviceFileName, "rb"); 
	if (hComIn == NULL)
	{
		Dprintf(("RS232: Failed to open input file %s\n",
		         ConfigureParams.RS232.szDeviceFileName));
		fclose(hComOut); hComOut = NULL;
		return FALSE;
	}
	setvbuf(hComIn, NULL, _IONBF, 0);

	/* Set defaults */
	RS232_SetConfig(9600,0,UCR_1STOPBIT|UCR_PARITY|UCR_ODDPARITY);

	/* Set all OK */
	bConnectedRS232 = TRUE;

	Dprintf(("Opened RS232 file: %s\n", ConfigureParams.RS232.szDeviceFileName));

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
  Set hardware configuration of RS-232

  Ctrl:Communications parameters, (default 0)No handshake
    Bit 0: XOn/XOff
    Bit 1: RTS/CTS

  Ucr: USART Control Register
    Bit 1:0-Odd Parity, 1-Even Parity
    Bit 2:0-No Parity, 1-Parity
    Bits 3,4: Start/Stop bits
      0 0 0-Start, 0-Stop    Synchronous
      0 1 0-Start, 1-Stop    Asynchronous
      1 0 1-Start, 1.5-Stop  Asynchronous
      1 1 1-Start, 2-Stop    Asynchronous
    Bits 5,6: 'WordLength'
      0 0 ,8 Bits
      0 1 ,7 Bits
      1 0 ,6 Bits
      1 1 ,5 Bits
    Bit 7: Frequency from TC and RC
*/
void RS232_SetConfig(int Baud,short int Ctrl,short int Ucr)
{
	Dprintf(("RS232_SetConfig(%i,%i,%i)\n",Baud,(int)Ctrl,(int)Ucr));
/* FIXME */
/*  
  DCB dcb;                           // Control block

  // Get current config
  memset(&dcb,0x0,sizeof(DCB));
  GetCommState(hCom, &dcb);
  // Set defaults
  BuildCommDCB("baud=9600 parity=N data=8 stop=1",&dcb);
  
  // Need XOn/XOff?
  if (Ctrl&CTRL_XON_XOFF) {
    dcb.fOutX = TRUE;
    dcb.fInX = TRUE;
  }
  // And RTS/CTS?
  if (Ctrl&CTRL_RTS_CTS)
    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;

  // Type of parity(if enabled)
  if (Ucr&UCR_EVENPARITY)
    dcb.Parity = EVENPARITY;
  else
    dcb.Parity = ODDPARITY;
  // Need parity?
  if (Ucr&UCR_PARITY)
    dcb.fParity = TRUE;
  // Number of stop bits
  switch(Ucr&UCR_STARTSTOP) {
    case UCR_0STOPBIT:        // PC doesn't appear to have no stop bits? Eh?
    case UCR_1STOPBIT:
      dcb.StopBits = ONESTOPBIT;
      break;
    case UCR_15STOPBIT:
      dcb.StopBits = ONE5STOPBITS;
      break;
    case UCR_2STOPBIT:
      dcb.StopBits = TWOSTOPBITS;
      break;
  }

  // And set
  SetCommState(hCom, &dcb);
*/
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
			/*fflush(hComOut);*/
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
	if (bConnectedRS232)
	{
		/* Read bytes out of input buffer */
		for (i=0; i<nBytes; i++)
		{
			*pBytes++ = InputBuffer_RS232[InputBuffer_Head];
			InputBuffer_Head = (InputBuffer_Head+1) % MAX_RS232INPUT_BUFFER;
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
