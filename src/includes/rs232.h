/*
  Hatari
*/

/* Ctrl/Ucr defines for ST */
#define CTRL_XON_XOFF   0x0001
#define CTRL_RTS_CTS    0x0002

#define UCR_ODDPARITY   0x0000
#define UCR_EVENPARITY  0x0002
#define UCR_PARITY      0x0004
#define UCR_STARTSTOP   0x0018
#define UCR_0STOPBIT    0x0000
#define UCR_1STOPBIT    0x0008
#define UCR_15STOPBIT   0x0010
#define UCR_2STOPBIT    0x0018

#define  MAX_TEMP_RS232INPUT_BUFFER  1024

#define  MAX_RS232INPUT_BUFFER    2048  // Must be ^2
#define  MAX_RS232INPUT_BUFFER_MASK  (MAX_RS232INPUT_BUFFER-1)

//extern HANDLE hCom;
extern unsigned char TempRS232InputBuffer[MAX_TEMP_RS232INPUT_BUFFER];
extern unsigned char InputBuffer_RS232[MAX_RS232INPUT_BUFFER];
extern int InputBuffer_Head,InputBuffer_Tail;

extern void RS232_Init(void);
extern void RS232_UnInit(void);
extern BOOL RS232_OpenCOMPort(void);
extern void RS232_CloseCOMPort(void);
extern void RS232_SetConfig(int Baud,short int Ctrl,short int Ucr);
extern BOOL RS232_TransferBytesTo(unsigned char *pBytes, int nBytes);
extern BOOL RS232_ReadBytes(unsigned char *pBytes, int nBytes);
extern BOOL RS232_GetStatus(void);
//extern DWORD FAR PASCAL RS232_ThreadFunc(LPSTR lpData);
