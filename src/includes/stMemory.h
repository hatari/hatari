/*
  Hatari - stMemory.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_STMEMORY_H
#define HATARI_STMEMORY_H

extern void STMemory_Clear(unsigned long StartAddress, unsigned long EndAddress);
extern unsigned short STMemory_Swap68000Int(unsigned short var);
extern unsigned long STMemory_Swap68000Long(unsigned long var);
extern void STMemory_WriteLong(unsigned long Address, unsigned long Var);
extern void STMemory_WriteWord(unsigned long Address, unsigned short Var);
extern void STMemory_WriteByte(unsigned long Address, unsigned char Var);
extern unsigned long STMemory_ReadLong(unsigned long Address);
extern unsigned short STMemory_ReadWord(unsigned long Address);
extern unsigned char STMemory_ReadByte(unsigned long Address);
extern void STMemory_WriteLong_PCSpace(void *pAddress, unsigned long Var);
extern void STMemory_WriteWord_PCSpace(void *pAddress, unsigned short Var);
extern unsigned long STMemory_ReadLong_PCSpace(void *pAddress);
extern unsigned short STMemory_ReadWord_PCSpace(void *pAddress);

#endif
