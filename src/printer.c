/*
  Hatari - printer.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Printer communication. When bytes are sent from the ST they are sent to these
  functions via 'Printer_TransferByteTo()'. This will then open a file and
  direct the output to this. These bytes are buffered up (to improve speed) and
  this also allow us to detect when the stream goes into idle - at which point
  we close the file/printer.
*/
const char Printer_fileid[] = "Hatari printer.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "paths.h"
#include "printer.h"
#include "log.h"

#define PRINTER_DEBUG 0
#if PRINTER_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif

/* After ~4 seconds (4*50 VBLs), flush & close printer */
#define PRINTER_IDLE_CLOSE   (4*50)

static int nIdleCount;
static int bUnflushed;

static FILE *pPrinterHandle;


/*-----------------------------------------------------------------------*/
/**
 * Initialise Printer
 */
void Printer_Init(void)
{
	char *separator;
	Dprintf((stderr, "Printer_Init()\n"));

	/* disabled from config/command line? */
	if (!ConfigureParams.Printer.szPrintToFileName[0])
		return;

	/* printer name without path? */
	separator = strrchr(ConfigureParams.Printer.szPrintToFileName, PATHSEP);
	if (!separator)
		return;
	
	*separator = '\0';
	if (!File_DirExists(ConfigureParams.Printer.szPrintToFileName)) {
		Log_AlertDlg(LOG_ERROR, "Printer output file directory inaccessible. Printing disabled.");
		ConfigureParams.Printer.bEnablePrinting = false;
	}
	*separator = PATHSEP;

	Dprintf((stderr, "Filename for printing: %s \n", ConfigureParams.Printer.szPrintToFileName));
}


/*-----------------------------------------------------------------------*/
/**
 * Uninitialise Printer
 */
void Printer_UnInit(void)
{
	Dprintf((stderr, "Printer_UnInit()\n"));

	/* Close any open files */
	pPrinterHandle = File_Close(pPrinterHandle);
	bUnflushed = false;
	nIdleCount = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Pass byte from emulator to printer.  Opens the printer file appending
 * if it isn't already open. Returns false if connection to "printer"
 * failed
 */
bool Printer_TransferByteTo(Uint8 Byte)
{
	/* Do we want to output to a printer/file? */
	if (!ConfigureParams.Printer.bEnablePrinting)
		return false;   /* Failed if printing disabled */

	/* Have we made a connection to our printer/file? */
	if (!pPrinterHandle)
	{
		/* open printer file... */
		pPrinterHandle = File_Open(ConfigureParams.Printer.szPrintToFileName, "a+b");
		if (!pPrinterHandle)
		{
			Log_AlertDlg(LOG_ERROR, "Printer output file open failed. Printing disabled.");
			ConfigureParams.Printer.bEnablePrinting = false;
			return false;
		}
	}
	if (fputc(Byte, pPrinterHandle) != Byte)
	{
		fprintf(stderr, "ERROR: Printer_TransferByteTo() writing failed!\n");
		return false;
	}
	bUnflushed = true;
	return true;
}


/*-----------------------------------------------------------------------*/
/**
 * Empty printer buffer, and if remains idle for set time close connection
 * (ie close file, stop printer)
 */
void Printer_CheckIdleStatus(void)
{
	/* Is anything waiting for printer? */
	if (bUnflushed)
	{
		fflush(pPrinterHandle);
		bUnflushed = false;
		nIdleCount = 0;
	}
	else
	{
		nIdleCount++;
		/* Has printer been idle? */
		if (nIdleCount >= PRINTER_IDLE_CLOSE)
		{
			/* Close printer output */
			Printer_UnInit();
		}
	}
}
