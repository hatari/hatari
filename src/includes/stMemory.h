/*
  Hatari
*/

extern void STMemory_Clear(unsigned long StartAddress, unsigned long EndAddress);
extern unsigned short int STMemory_Swap68000Int(unsigned short int var);
extern unsigned long STMemory_Swap68000Long(unsigned long var);
extern void STMemory_WriteLong(unsigned long Address,unsigned long Var);
extern void STMemory_WriteWord(unsigned long Address,unsigned short int Var);
extern void STMemory_WriteByte(unsigned long Address,unsigned char Var);
extern unsigned long STMemory_ReadLong(unsigned long Address);
extern unsigned short int STMemory_ReadWord(unsigned long Address);
extern unsigned char STMemory_ReadByte(unsigned long Address);
extern void STMemory_WriteLong_PCSpace(void *pAddress,unsigned long Var);
extern void STMemory_WriteWord_PCSpace(void *pAddress,unsigned short int Var);
extern unsigned long STMemory_ReadLong_PCSpace(void *pAddress);
extern unsigned short int STMemory_ReadWord_PCSpace(void *pAddress);
