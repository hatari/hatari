/*
  Hatari

  RS-232 Communications

  This is similar to the Printing functions, we open a direct file to the COM port and send bytes over it.
  Using such method mimicks the ST exactly, and even allows us to connect to an actual ST! To wait for
  incoming data, we create a thread which copies the bytes into an input buffer. This method fits in with
  the internet code which also reads data into a buffer.
*/

/* FIXME: The functions here do not yet work with Hatari */


#include "main.h"
#include "debug.h"
#include "dialog.h"
#include "rs232.h"


BOOL bConnectedRS232=FALSE;          // Connection to RS232?
//HANDLE hCom=NULL;                  // Handle to file
//HANDLE RS232Thread=NULL;           // Thread handle for reading incoming data
long RS232ThreadID;
//DCB dcb;                           // Control block
unsigned char TempRS232InputBuffer[MAX_TEMP_RS232INPUT_BUFFER];
unsigned char InputBuffer_RS232[MAX_RS232INPUT_BUFFER];
int InputBuffer_Head=0,InputBuffer_Tail=0;

//-----------------------------------------------------------------------
/*
  Initialize RS-232, start thread to wait for incoming data (we will open a connection when first bytes are sent)
*/
void RS232_Init(void)
{
  // Create thread to wait for incoming bytes over RS-232
//FIXME   RS232Thread = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)RS232_ThreadFunc,0,NULL,&RS232ThreadID);
}

//-----------------------------------------------------------------------
/*
  Close RS-232 connection and stop checking for incoming data
*/
void RS232_UnInit(void)
{
  // Close, kill thread and free resource
/* FIXME */
/*
  if (RS232Thread) {
    TerminateThread(RS232Thread,FALSE);
    CloseHandle(RS232Thread);
  }
  RS232_CloseCOMPort();
*/
}

//-----------------------------------------------------------------------
/*
  Open file on COM port
*/
BOOL RS232_OpenCOMPort(void)
{
/* FIXME */
/*
  char szString[32];

  // Generate correct filename for COM port
  sprintf(szString,"COM%d",ConfigureParams.RS232.nCOMPort+1);  // 1,2...

  // Create our COM file for input/output
  bConnectedRS232 = FALSE;
  hCom = CreateFile(szString,GENERIC_READ | GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL ); 
  if (hCom!=INVALID_HANDLE_VALUE) {
    // Get any early notifications, for thread
    SetCommMask(hCom,EV_RXCHAR);
    // Create input/output buffers
    SetupComm(hCom,4096,4096);
    // Purge buffers
    PurgeComm(hCom,PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

    // Set defaults
    RS232_SetConfig(9600,0,UCR_1STOPBIT|UCR_PARITY|UCR_ODDPARITY);

    // Set all OK
    bConnectedRS232 = TRUE;
  }
*/
  return(bConnectedRS232);
}

//-----------------------------------------------------------------------
/*
  Close file on COM port
*/
void RS232_CloseCOMPort(void)
{
/* FIXME */
/*
  // Do have file open?
  if (bConnectedRS232) {
    // Close
    CloseHandle(hCom);
    hCom=NULL;

    bConnectedRS232 = FALSE;
  }
*/
}

//-----------------------------------------------------------------------
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
/* FIXME */
/*  
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

//-----------------------------------------------------------------------
/*
  Pass bytes from emulator to RS-232
*/
BOOL RS232_TransferBytesTo(unsigned char *pBytes, int nBytes)
{
/* FIXME */
/*
  DWORD BytesWritten;

  // Do need to open a connection to RS232?
  if (!bConnectedRS232) {
    // Do have RS-232 enabled?
    if (ConfigureParams.RS232.bEnableRS232)
      bConnectedRS232 = RS232_OpenCOMPort();
  }

  // Have we connected to the RS232?
  if (bConnectedRS232) {
    // Send bytes directly to COM port
    if (WriteFile(hCom,pBytes,nBytes,&BytesWritten,NULL)) {
      FlushFileBuffers(hCom);
    }

    // Show icon on status bar
    StatusBar_SetIcon(STATUS_ICON_RS232,ICONSTATE_UPDATE);

    return(TRUE);  //OK
  }
  else
*/    return(FALSE);  //Failed

}

//-----------------------------------------------------------------------
/*
  Read characters from our internal input buffer(bytes from other machine)
*/
BOOL RS232_ReadBytes(unsigned char *pBytes, int nBytes)
{
/* FIXME */
/*
  int i;

  // Connected?
  if (bConnectedRS232) {
    // Read bytes out of input buffer
    for(i=0; i<nBytes; i++) {
      *pBytes++ = InputBuffer_RS232[InputBuffer_Head];
      InputBuffer_Head = (InputBuffer_Head+1)&MAX_RS232INPUT_BUFFER_MASK;
    }
    return(TRUE);
  }
*/
  return(FALSE);

}

//-----------------------------------------------------------------------
/*
  Return TRUE if bytes waiting!
*/
BOOL RS232_GetStatus(void)
{
/* FIXME */
/*
  // Connected?
  if (bConnectedRS232) {
    // Do we have bytes in the input buffer?
    if (InputBuffer_Head!=InputBuffer_Tail)
      return(TRUE);
  }
*/
  // No, none
  return(FALSE);
}

//-----------------------------------------------------------------------
/*
  Add incoming bytes from other machine into our input buffer
*/
void RS232_AddBytesToInputBuffer(unsigned char *pBytes, int nBytes)
{
  int i;

  // Copy bytes into input buffer
  for(i=0; i<nBytes; i++) {
    InputBuffer_RS232[InputBuffer_Tail] = *pBytes++;
    InputBuffer_Tail = (InputBuffer_Tail+1)&MAX_RS232INPUT_BUFFER_MASK;
  }
}

//-----------------------------------------------------------------------
/*
  Thread to read incoming RS-232 data, and pass to emulator input buffer
*/
/* FIXME */
/*
DWORD FAR PASCAL RS232_ThreadFunc(LPSTR lpData)
{
  COMSTAT ComStat;
  DWORD dwErrorFlags;
  DWORD dwEvtMask;
  DWORD dwLength;

  SetCommMask(hCom,EV_RXCHAR);

  // Check for any RS-232 incoming data
  while(TRUE) {
    if (hCom) {
      // Halt here until we find some data coming through the RS-232
      dwEvtMask = 0;
      WaitCommEvent(hCom,&dwEvtMask,NULL);

      // Chars awaiting? Read them in
      if ((dwEvtMask&EV_RXCHAR)==EV_RXCHAR) {
        // Only try to read number of bytes in queue - don't read more than our buffer allows
        ClearCommError(hCom, &dwErrorFlags, &ComStat );
        dwLength = min(MAX_TEMP_RS232INPUT_BUFFER, ComStat.cbInQue);
        // Read the bytes in, if we have any
        if (dwLength!=0) {
          // Read into temporary buffer
          ReadFile(hCom,TempRS232InputBuffer,dwLength,&dwLength,NULL);
          // And copy into our internal queue
          RS232_AddBytesToInputBuffer(TempRS232InputBuffer,dwLength);
        }
      }

      // Sleep or a while
      Sleep(2);
    }
    else {
      // No RS-232 connection, sleep for 20ms
      Sleep(20);
    }
  }

  return(TRUE);
}
*/
