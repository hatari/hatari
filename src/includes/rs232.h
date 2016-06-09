/*
  Hatari - rs232.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_RS232_H
#define HATARI_RS232_H

extern void RS232_Init(void);
extern void RS232_UnInit(void);
extern void RS232_SetBaudRateFromTimerD(void);
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
