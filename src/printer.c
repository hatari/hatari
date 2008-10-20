/*
  Hatari - printer.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Printer communication. When bytes are sent from the ST they are sent to these
  functions via 'Printer_TransferByteTo()'. This will then open a file and
  direct the output to this. These bytes are buffered up (to improve speed) and
  this also allow us to detect when the stream goes into idle - at which point
  we close the file/printer.
*/
const char Printer_rcsid[] = "Hatari $Id: printer.c,v 1.25 2008-10-20 20:23:57 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "paths.h"
#include "printer.h"

/* #define PRINTER_DEBUG */

#define PRINTER_FILENAME "hatari.prn"

#define PRINTER_IDLE_CLOSE   (4*50)     /* After 4 seconds, close printer */

#define PRINTER_BUFFER_SIZE  2048       /* 2k buffer which when full will be written to printer/file */

static Uint8 PrinterBuffer[PRINTER_BUFFER_SIZE];   /* Buffer to store character before output */
static size_t nPrinterBufferChars;      /* # characters in above buffer */
static bool bConnectedPrinter;
static int nIdleCount;

static FILE *pPrinterHandle;


/* internal functions */
static void Printer_EmptyFile(void);
static void Printer_ResetInternalBuffer(void);
static bool Printer_EmptyInternalBuffer(void);
static void Printer_AddByteToInternalBuffer(Uint8 Byte);


/*-----------------------------------------------------------------------*/
/**
 * Initialise Printer
 */
void Printer_Init(void)
{
#ifdef PRINTER_DEBUG
	fprintf(stderr,"Printer_Init()\n");
#endif

	/* A valid file name for printing is already set up in configuration.c.
	 * But we check it again since the user might have entered an invalid
	 * file name in the hatari.cfg file... */
	if (strlen(ConfigureParams.Printer.szPrintToFileName) <= 1)
	{
		const char *psHomeDir;
		psHomeDir = Paths_GetHatariHome();

		/* construct filename for printing.... */
		if (strlen(psHomeDir)+1+strlen(PRINTER_FILENAME) < sizeof(ConfigureParams.Printer.szPrintToFileName))
		{
			sprintf(ConfigureParams.Printer.szPrintToFileName, "%s%c%s",
			        psHomeDir, PATHSEP, PRINTER_FILENAME);
		}
		else
		{
			strcpy(ConfigureParams.Printer.szPrintToFileName, PRINTER_FILENAME);
		}
	}

#ifdef PRINTER_DEBUG
	fprintf(stderr,"Filename for printing: %s \n", ConfigureParams.Printer.szPrintToFileName);
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Uninitialise Printer
 */
void Printer_UnInit(void)
{
	/* Close any open files */
	Printer_CloseAllConnections();

#ifdef PRINTER_DEBUG
	fprintf(stderr,"Printer_UnInit()\n");
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Close all open output file, empty buffers etc.
 */
void Printer_CloseAllConnections(void)
{
	/* Empty buffer */
	Printer_EmptyInternalBuffer();

	/* Close any open files */
	pPrinterHandle = File_Close(pPrinterHandle);

	/* Signal finished with printing */
	bConnectedPrinter = FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Reset Printer Buffer
 */
static void Printer_ResetInternalBuffer(void)
{
	nPrinterBufferChars = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Empty to file on disk.
 */
static void Printer_EmptyFile(void)
{
	/* Do have file open? */
	if (pPrinterHandle)
	{
		/* Write bytes out */
		if (fwrite((Uint8 *)PrinterBuffer,sizeof(Uint8),nPrinterBufferChars,pPrinterHandle) < nPrinterBufferChars)
		{
			/* we wrote less then all chars in the buffer --> ERROR */
			fprintf(stderr,"Printer_EmptyFile(): ERROR not all chars were written\n");
		}
		/* Reset */
		Printer_ResetInternalBuffer();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Empty Printer Buffer
 */
static bool Printer_EmptyInternalBuffer(void)
{
	/* Write bytes to file */
	if (nPrinterBufferChars > 0)
	{
		if (pPrinterHandle)
			Printer_EmptyFile();

		return TRUE;
	}
	/* Nothing to do */
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Add byte to our internal buffer, and when full write out - needed to speed
 */
static void Printer_AddByteToInternalBuffer(Uint8 Byte)
{
	/* Is buffer full? If so empty */
	if (nPrinterBufferChars == PRINTER_BUFFER_SIZE)
		Printer_EmptyInternalBuffer();
	/* Add character */
	PrinterBuffer[nPrinterBufferChars++] = Byte;
}


/*-----------------------------------------------------------------------*/
/**
 * Pass byte from emulator to printer.  Opens the printer file appending
 * if it isn't already open. Returns FALSE if connection to "printer"
 * failed
 */
bool Printer_TransferByteTo(Uint8 Byte)
{
	/* Do we want to output to a printer/file? */
	if (!ConfigureParams.Printer.bEnablePrinting)
		return FALSE;   /* Failed if printing disabled */

	/* Have we made a connection to our printer/file? */
	if (!bConnectedPrinter)
	{
		/* open printer file... */
		pPrinterHandle = File_Open(ConfigureParams.Printer.szPrintToFileName, "a+");
		bConnectedPrinter = (pPrinterHandle != NULL);

		/* Reset the printer */
		Printer_ResetInternalBuffer();
	}

	/* Is all OK? */
	if (bConnectedPrinter)
	{
		/* Add byte to our buffer. */
		Printer_AddByteToInternalBuffer(Byte);
		return TRUE;    /* OK */
	}
	else
		return FALSE;   /* Failed */
}


/*-----------------------------------------------------------------------*/
/**
 * Empty printer buffer, and if remains idle for set time close connection
 * (ie close file, stop printer)
 */
void Printer_CheckIdleStatus(void)
{
	/* Is anything waiting for printer? */
	if (Printer_EmptyInternalBuffer())
	{
		nIdleCount = 0;
	}
	else
	{
		nIdleCount++;
		/* Has printer been idle? */
		if (nIdleCount >= PRINTER_IDLE_CLOSE)
		{
			/* Close printer output */
			Printer_CloseAllConnections();
			nIdleCount = 0;
		}
	}
}
