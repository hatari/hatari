/*
  Hatari

  Printer communication. When bytes are sent to the ST they are sent on to these functions
  via 'Printer_TransferByteTo()'. This will then open a file or Windows printer and direct
  the output to this. These bytes are buffered up(to improve speed) and this also allow us
  to detect when the stream goes into idle - at which point we close the file/printer(Windows
  printing will only occur when we close).
  NOTE - Tab's are converted to spaces as the PC 'Tab' setting differs to that of the ST.

  Hack for simple printing by Matthias Arndt <marndt@asmsoftware.de>

  ChangeLog:

  9 Aug 2003:   Matthias Arndt <marndt@asmsoftware.de>
  	- initial rewrite
	- removed MS Windows specific parts
	- made printing to $HOME/hatari.prn possible

*/

#include "main.h"
#include "debug.h"
#include "dialog.h"
#include "file.h"
#include "printer.h"
#include "screen.h"

/* #define PRINTER_DEBUG */

#define PRINTERFILENAME "/hatari.prn"

#define PRINTER_TAB_SETTING  8          /* A 'Tab' on the ST is 8 spaces */
#define  PRINTER_IDLE_CLOSE  (4*50)     /* After 4 seconds, close printer */

#define PRINTER_BUFFER_SIZE  2048       /* 2k buffer which when full will be written to printer/file */

static unsigned char PrinterBuffer[PRINTER_BUFFER_SIZE];   /* Buffer to store character before output */
static int nPrinterBufferChars,nPrinterBufferCharsOnLine;  /* # characters in above buffer */
static BOOL bConnectedPrinter=FALSE;
static BOOL bPrinterDiscFile=FALSE;
static int nIdleCount;

static FILE *PrinterFileHandle;

char *fname;
/*-----------------------------------------------------------------------
  Initialise Printer
 -----------------------------------------------------------------------*/
void Printer_Init(void)
{
#ifdef PRINTER_DEBUG
	fprintf(stderr,"Printer_Init()\n");
#endif
	/* FIXME: enable and disable printing via the GUI */
	/* cheating: for testing we activate printing here by hand... */
	ConfigureParams.Printer.bEnablePrinting=TRUE;
	if(ConfigureParams.Printer.bEnablePrinting)
		fprintf(stderr,"Printer_Init(): printing activated...\n");

  /* construct filename for printing.... */
  fname=getenv("HOME");
  strcat(fname,PRINTER_FILENAME);

#ifdef PRINTER_DEBUG
  fprintf(stderr,"Filename for printing: %s \n",fname);
#endif


}


/*-----------------------------------------------------------------------*/
/*
  Uninitialise Printer
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
/*
  Close all open files, on disc or Windows printers
*/
void Printer_CloseAllConnections(void)
{
  /* Empty buffer */
  Printer_EmptyInternalBuffer();

  /* Close any open files */
  Printer_CloseDiscFile();

  /* Signal finished with printing */
  bConnectedPrinter = FALSE;
}


/*-----------------------------------------------------------------------
  Open file on disc, to which all printer output will be sent
  FIXME: filename needs to be made configurable
 -----------------------------------------------------------------------*/
BOOL Printer_OpenDiscFile(void)
{

  bPrinterDiscFile = TRUE;

  /* FIXME - allow setting of filename via GUI... */
  /* open printer file... */
  PrinterFileHandle=fopen(fname,"a+");
  if(!PrinterFileHandle)
  	bPrinterDiscFile=FALSE;


  return(bPrinterDiscFile);
}


/*-----------------------------------------------------------------------
  Close file on disc, if we have one open
 -----------------------------------------------------------------------*/
void Printer_CloseDiscFile(void)
{
  /* Do have file open? */
  if (bPrinterDiscFile) {
    /* Close */
	fclose(PrinterFileHandle);
    bPrinterDiscFile = FALSE;
  }
}


/*-----------------------------------------------------------------------
  Empty to disc file
 -----------------------------------------------------------------------*/
void Printer_EmptyDiscFile(void)
{
  /* Do have file open? */
  if (bPrinterDiscFile) {
    /* Write bytes out */
    if(fwrite((unsigned char *)PrinterBuffer,sizeof(unsigned char),nPrinterBufferChars,PrinterFileHandle)<nPrinterBufferChars)
	 {
		/* we worte less then chars in the buffer..error */
		fprintf(stderr,"Printer_EmptyDiscFile(): ERROR not all chars were written\n");
	 }
    /* Reset */
    Printer_ResetInternalBuffer();
  }
}


/*-----------------------------------------------------------------------
  Reset Printer Buffer
 -----------------------------------------------------------------------*/
void Printer_ResetInternalBuffer(void)
{
  nPrinterBufferChars = 0;
}

/*-----------------------------------------------------------------------
  Reset character line
 *----------------------------------------------------------------------*/
void Printer_ResetCharsOnLine(void)
{
  nPrinterBufferCharsOnLine = 0;
}


/*-----------------------------------------------------------------------
  Empty Printer Buffer
 -----------------------------------------------------------------------*/
BOOL Printer_EmptyInternalBuffer(void)
{
  /* Write bytes to file */
  if (nPrinterBufferChars>0) {
    if (bPrinterDiscFile)
      Printer_EmptyDiscFile();

    return(TRUE);
  }

  /* Nothing to do */
  return(FALSE);
}


/*-----------------------------------------------------------------------
  Return TRUE if byte is standard ASCII character which is OK to output
 -----------------------------------------------------------------------*/
BOOL Printer_ValidByte(unsigned char Byte)
{
  /* Return/New line? */
  if ( (Byte==0x0d) || (Byte==0x0a) )
    return(TRUE);
  /* Normal character? */
  if ( (Byte>=32) && (Byte<127) )
    return(TRUE);
  /* Tab */
  if (Byte=='\t')
    return(TRUE);
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Add byte to our internal buffer, and when full write out - needed to speed
*/
void Printer_AddByteToInternalBuffer(unsigned char Byte)
{
  /* Is buffer full? If so empty */
  if (nPrinterBufferChars==PRINTER_BUFFER_SIZE)
    Printer_EmptyInternalBuffer();
  /* Add character */
  PrinterBuffer[nPrinterBufferChars++] = Byte;
  /* Add count of character on line */
  if ( !((Byte==0xd) || (Byte==0xa)) )
    nPrinterBufferCharsOnLine++;
}


/*-----------------------------------------------------------------------
  Add 'Tab' to internal buffer
 -----------------------------------------------------------------------*/
void Printer_AddTabToInternalBuffer(void)
{
  int i,NumSpaces;

  /* Is buffer full? If so empty */
  if (nPrinterBufferChars>=(PRINTER_BUFFER_SIZE-PRINTER_TAB_SETTING))
    Printer_EmptyInternalBuffer();
  /* Add tab - convert to 'PRINTER_TAB_SETTING' space */
  NumSpaces = PRINTER_TAB_SETTING-(nPrinterBufferCharsOnLine%PRINTER_TAB_SETTING);
  for(i=0; i<NumSpaces; i++) {
    PrinterBuffer[nPrinterBufferChars++] = ' ';
    nPrinterBufferCharsOnLine++;
  }
}


/*-----------------------------------------------------------------------
  Pass byte from emulator to printer
 -----------------------------------------------------------------------*/
BOOL Printer_TransferByteTo(unsigned char Byte)
{
  /* Do we want to output to a printer/file? */
  if (!ConfigureParams.Printer.bEnablePrinting)
    return(FALSE);  /* Failed if printing disabled */

  /* Have we made a connection to our printer/file? */
  if (!bConnectedPrinter) {
    bConnectedPrinter = Printer_OpenDiscFile();

    /* Reset the printer */
    Printer_ResetInternalBuffer();
    Printer_ResetCharsOnLine();
  }

  /* Is all OK? */
  if (bConnectedPrinter) {
    /* Add byte to our buffer, if is useable character */
    if (Printer_ValidByte(Byte)) {
      if (Byte=='\t')
        Printer_AddTabToInternalBuffer();
      else
        Printer_AddByteToInternalBuffer(Byte);
      if (Byte==0xd)
        nPrinterBufferCharsOnLine = 0;
    }

    return(TRUE);  /* OK */
  }
  else

    return(FALSE);  /* Failed */
}


/*-----------------------------------------------------------------------
  Empty printer buffer, and if remains idle for set time close connection
  (ie close file, stop printer)
 -----------------------------------------------------------------------*/
void Printer_CheckIdleStatus(void)
{
  /* Is anything waiting for printer? */
  if (Printer_EmptyInternalBuffer()) {
    nIdleCount = 0;
  }
  else {
    nIdleCount++;
    /* Has printer been idle? */
    if (nIdleCount>=PRINTER_IDLE_CLOSE) {
      /* Close printer output */
      Printer_CloseAllConnections();
      nIdleCount = 0;
    }
  }
}
