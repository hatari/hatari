/*
  Hatari

  Printer communication. When bytes are sent to the ST they are sent on to these functions
  via 'Printer_TransferByteTo()'. This will then open a file or Windows printer and direct
  the output to this. These bytes are buffered up(to improve speed) and this also allow us
  to detect when the stream goes into idle - at which point we close the file/printer(Windows
  printing will only occur when we close).
  NOTE - Tab's are converted to spaces as the PC 'Tab' setting differs to that of the ST.
  NOTE - As the ST differs so greatly from modern Windows printing we are limited in the way
  we can output data - Printer no longer respond to communication over the LPT port(DOS can
  not use modern printers). As such, we can only print out basic text based documents - the
  output to file option allows text to be loaded into a Windows word-processor and formatted
  correctly using that.
*/

#include "main.h"
#include "debug.h"
#include "dialog.h"
#include "file.h"
#include "printer.h"
#include "screen.h"
#include "statusBar.h"
#include "view.h"

#define PRINTER_TAB_SETTING  8          /* A 'Tab' on the ST is 8 spaces */
#define  PRINTER_IDLE_CLOSE  (4*50)     /* After 4 seconds, close printer */

#define PRINTER_BUFFER_SIZE  2048       /* 2k buffer which when full will be written to printer/file */

//static HFILE PrinterFile;
//static OFSTRUCT PrinterFileInfo;
static unsigned char PrinterBuffer[PRINTER_BUFFER_SIZE];   /* Buffer to store character before output */
static int nPrinterBufferChars,nPrinterBufferCharsOnLine;  /* # characters in above buffer */
static BOOL bConnectedPrinter=FALSE;
static BOOL bPrinterDiscFile=FALSE,bPrinterWindows=FALSE;
//static PRINTDLG pd;                                      // Printer Dlg
static BOOL bStartedPage;                                  /* Have set up new page? */
static BOOL bAlreadyOpenedPrintingFile=FALSE;              /* TRUE if already opened file, so can add to end */
static int PrinterX,PrinterY;                              /* X,Y to print to on page */
static int nPrinterWidthPels,nPrinterHeightPels;           /* Width/Height of page */
static int nIdleCount=0;                                   /* Frames printer has been idle */


/*-----------------------------------------------------------------------*/
/*
  Initialise Printer
*/
void Printer_Init(void)
{
}


/*-----------------------------------------------------------------------*/
/*
  Uninitialise Printer
*/
void Printer_UnInit(void)
{
  /* Close any open files */
  Printer_CloseAllConnections();
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
  /* And printers */
  Printer_CloseWindowsPrinter();

  /* Signal finished with printing */
  bConnectedPrinter = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Open file on disc, to which all printer output will be sent
*/
BOOL Printer_OpenDiscFile(void)
{
/* FIXME */
/*
  // Close existing
  Printer_CloseAllConnections();

  // Open file
  bPrinterDiscFile = TRUE;
  // Do we have a filename for output?
  if (strlen(ConfigureParams.Printer.szPrintToFileName)<=0) {
    // First, Return back into a Window
    Screen_ReturnFromFullScreen();
    View_ToggleWindowsMouse(MOUSE_WINDOWS);
    // Ask for filename
    if (!File_OpenSelectDlg(hWnd,ConfigureParams.Printer.szPrintToFileName,FILEFILTER_ALLFILES,FALSE,TRUE))
      bPrinterDiscFile = FALSE;
    // Revert back to ST mouse mode
    View_ToggleWindowsMouse(MOUSE_ST);
  }

  // OK to print?
  if (bPrinterDiscFile) {
    // Open file to print to - if previously opened, just re-open to add to end
    if (!bAlreadyOpenedPrintingFile)
      PrinterFile = OpenFile(ConfigureParams.Printer.szPrintToFileName,&PrinterFileInfo,OF_CREATE | OF_WRITE);
    else {
      // Re-open and position at end of file
      PrinterFile = OpenFile(ConfigureParams.Printer.szPrintToFileName,&PrinterFileInfo,OF_WRITE);
      _llseek(PrinterFile,0,FILE_END);
    }

    if (PrinterFile==HFILE_ERROR)
      bPrinterDiscFile = FALSE;
    else
      bAlreadyOpenedPrintingFile = TRUE;
  }
*/
  return(bPrinterDiscFile);
}


/*-----------------------------------------------------------------------*/
/*
  Close file on disc, if we have one open
*/
void Printer_CloseDiscFile(void)
{
  /* Do have file open? */
  if (bPrinterDiscFile) {
    /* Close */
//FIXME    _lclose(PrinterFile);

    bPrinterDiscFile = FALSE;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Empty to disc file
*/
void Printer_EmptyDiscFile(void)
{
  /* Do have file open? */
  if (bPrinterDiscFile) {
    /* Write bytes out */
//FIXME    _hwrite(PrinterFile,(char *)PrinterBuffer,nPrinterBufferChars);
    /* Reset */
    Printer_ResetInternalBuffer();
  }
}


/*-----------------------------------------------------------------------*/
/*
  Start new printer page
*/
void Printer_StartNewPage(void)
{
  /* Reset coords on printer(start printing 'top-left') */
  PrinterX = PrinterY = 0;
  /* Set new page */
//FIXME  StartPage(pd.hDC); 
  /* Set flag */
  bStartedPage = TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Send character to Windows printer, add to XY position
*/
void Printer_PrintCharacter(char Char)
{
/*FIXME*/
#if 0
  SIZE CharSize;

  /* Have started page? If not, begin new one */
  if (!bStartedPage)
    Printer_StartNewPage();

  /* Find width/height of character */
  GetTextExtentPoint32(pd.hDC, &Char,1, &CharSize);

  /* Handle returns to new lines... */
  if (Char==0xa) {
    PrinterY += CharSize.cy;
    return;
  }
  if (Char==0xd) {
    PrinterX = 0;
    return;
  }
 
  /* Will fit on page? */
  if ((PrinterX+CharSize.cx)>=nPrinterWidthPels) {
    PrinterY += CharSize.cy;            /* Off right of page, start new line */
    PrinterX = 0;
  }
  if ((PrinterY+CharSize.cy)>=nPrinterHeightPels) {
    if (bStartedPage)
      EndPage(pd.hDC);                  /* Off bottom of page, start new one */
    Printer_StartNewPage();
  }

  /* Print character */
  TextOut(pd.hDC, PrinterX,PrinterY, &Char, 1);
  PrinterX += CharSize.cx;
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Open Windows printer - Always return TRUE as sucess so we can 'print' without constantly
  bringing up the Printer dialog if they happened to press 'Cancel'.
*/
BOOL Printer_OpenWindowsPrinter(void)
{
/* FIXME */
/*
  DOCINFO di;
  int nError;

  // Close existing
  Printer_CloseAllConnections();

  // First, Return back into a Window
  Screen_ReturnFromFullScreen();
  View_ToggleWindowsMouse(MOUSE_WINDOWS);

  // Initialize the PRINTDLG members. 
  pd.lStructSize = sizeof(PRINTDLG); 
  pd.hDevMode = (HANDLE) NULL; 
  pd.hDevNames = (HANDLE) NULL; 
  pd.Flags = PD_RETURNDC | PD_ALLPAGES | PD_NOSELECTION | PD_DISABLEPRINTTOFILE | PD_HIDEPRINTTOFILE; 
  pd.hwndOwner = hWnd; 
  pd.hDC = (HDC) NULL; 
  pd.nFromPage = 1; 
  pd.nToPage = 1; 
  pd.nMinPage = 0; 
  pd.nMaxPage = 0; 
  pd.nCopies = 1; 
  pd.hInstance = (HINSTANCE)NULL; 
  pd.lCustData = 0L; 
  pd.lpfnPrintHook = (LPPRINTHOOKPROC) NULL; 
  pd.lpfnSetupHook = (LPSETUPHOOKPROC) NULL; 
  pd.lpPrintTemplateName = (LPSTR) NULL; 
  pd.lpSetupTemplateName = (LPSTR)  NULL; 
  pd.hPrintTemplate = (HANDLE) NULL; 
  pd.hSetupTemplate = (HANDLE) NULL; 
 
  // Display the PRINT dialog box. 
  if (PrintDlg(&pd)!=0) { 
    // Initialize the members of a DOCINFO structure. 
    di.cbSize = sizeof(DOCINFO); 
    di.lpszDocName = "Hatari Print Output"; 
    di.lpszOutput = (LPTSTR) NULL; 
    di.lpszDatatype = (LPTSTR) NULL; 
    di.fwType = 0; 
 
    // Begin a print job by calling the StartDoc function. 
    nError = StartDoc(pd.hDC, &di); 
    if (nError == SP_ERROR) {
      // Report error
      Main_SysError("Unable to start Printing operation.",PROG_NAME);
      // Delete the printer DC.
      DeleteDC(pd.hDC);
      // Fail printing
      bPrinterWindows = FALSE;

      return(TRUE);
    }
    
    // Get Printer Information
    nPrinterWidthPels = GetDeviceCaps(pd.hDC, HORZRES);
    nPrinterHeightPels = GetDeviceCaps(pd.hDC, VERTRES);
    // Ready for first page
    bStartedPage = FALSE;

    // All OK
    bPrinterWindows = TRUE;
  }
  else
    bPrinterWindows = FALSE;

  // Revert back to ST mouse mode
  View_ToggleWindowsMouse(MOUSE_ST);
*/
  return(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Close Windows printer
*/
void Printer_CloseWindowsPrinter(void)
{
/* FIXME */
/*
  // Do have file open?
  if (bPrinterWindows) {
    // End page
    if (bStartedPage)
      EndPage(pd.hDC);
    // Inform the driver that document has ended.
    EndDoc(pd.hDC);
    // Delete the printer DC.
    DeleteDC(pd.hDC);

    bPrinterWindows = FALSE;
  }
*/
}


/*-----------------------------------------------------------------------*/
/*
  Empty Windows printer
*/
void Printer_EmptyWindowsPrinter(void)
{
  int i;

  /* Do have file open? */
  if (bPrinterWindows) {
    /* Write bytes out */
    for(i=0; i<nPrinterBufferChars; i++)
      Printer_PrintCharacter(PrinterBuffer[i]);
    /* Reset */
    Printer_ResetInternalBuffer();
  }
}


/*-----------------------------------------------------------------------*/
/*
  Reset Printer Buffer
*/
void Printer_ResetInternalBuffer(void)
{
  nPrinterBufferChars = 0;
}


/*-----------------------------------------------------------------------*/
/*
  Reset character line
*/
void Printer_ResetCharsOnLine(void)
{
  nPrinterBufferCharsOnLine = 0;
}


/*-----------------------------------------------------------------------*/
/*
  Empty Printer Buffer
*/
BOOL Printer_EmptyInternalBuffer(void)
{
  /* Write bytes to file */
  if (nPrinterBufferChars>0) {
    if (bPrinterDiscFile)
      Printer_EmptyDiscFile();
    else if (bPrinterWindows)
      Printer_EmptyWindowsPrinter();

    return(TRUE);
  }

  /* Nothing to do */
  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Return TRUE if byte is standard ASCII character which is OK to output
*/
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


/*-----------------------------------------------------------------------*/
/*
  Add 'Tab' to internal buffer
*/
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


/*-----------------------------------------------------------------------*/
/*
  Pass byte from emulator to printer
*/
BOOL Printer_TransferByteTo(unsigned char Byte)
{
/* FIXME */
/*
  // Do we want to output to a printer/file?
  if (!ConfigureParams.Printer.bEnablePrinting)
    return(FALSE);  //Failed

  // Have we made a connection to our printer/file?
  if (!bConnectedPrinter) {
    // Open file, to Windows printer
    if (ConfigureParams.Printer.bPrintToFile)
      bConnectedPrinter = Printer_OpenDiscFile();
    else
      bConnectedPrinter = Printer_OpenWindowsPrinter();
    // Reset
    Printer_ResetInternalBuffer();
    Printer_ResetCharsOnLine();
  }

  // Is all OK?
  if (bConnectedPrinter) {
    // Add byte to our buffer, if is useable character
    if (Printer_ValidByte(Byte)) {
      if (Byte=='\t')
        Printer_AddTabToInternalBuffer();
      else
        Printer_AddByteToInternalBuffer(Byte);
      if (Byte==0xd)
        nPrinterBufferCharsOnLine = 0;
    }
    // Show icon on status bar
    StatusBar_SetIcon(STATUS_ICON_PRINTER,ICONSTATE_UPDATE);

    return(TRUE);  //OK
  }
  else
    return(FALSE);  //Failed
*/
}


/*-----------------------------------------------------------------------*/
/*
  Empty printer buffer, and if remains idle for set time close connection(ie close file, stop printer)
*/
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
