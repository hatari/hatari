/*
  Hatari - rs232.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_RS232_H
#define HATARI_RS232_H


#define  MAX_RS232INPUT_BUFFER    2048  /* Must be ^2 */

extern BOOL bConnectedRS232;
extern unsigned char InputBuffer_RS232[MAX_RS232INPUT_BUFFER];
extern int InputBuffer_Head,InputBuffer_Tail;

extern void RS232_Init(void);
extern void RS232_UnInit(void);
extern BOOL RS232_OpenCOMPort(void);
extern void RS232_CloseCOMPort(void);
extern void RS232_HandleUCR(short int ucr);
extern BOOL RS232_SetBaudRate(int nBaud);
extern void RS232_SetBaudRateFromTimerD(void);
extern void RS232_SetFlowControl(int ctrl);
extern BOOL RS232_TransferBytesTo(unsigned char *pBytes, int nBytes);
extern BOOL RS232_ReadBytes(unsigned char *pBytes, int nBytes);
extern BOOL RS232_GetStatus(void);
extern int RS232_ThreadFunc(void *pData);
extern void RS232_SCR_ReadByte(void);
extern void RS232_SCR_WriteByte(void);
extern void RS232_UCR_ReadByte(void);
extern void RS232_UCR_WriteByte(void);
extern void RS232_RSR_ReadByte(void);
extern void RS232_RSR_WriteByte(void);
extern void RS232_TSR_ReadByte(void);
extern void RS232_TSR_WriteByte(void);
extern void RS232_UDR_ReadByte(void);
extern void RS232_UDR_WriteByte(void);


#endif  /* ifndef HATARI_RS232_H */
