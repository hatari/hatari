/*
  Hatari

  ST Memory functions - takes care of endian swaps
*/

#include "main.h"
#include "decode.h"
#include "m68000.h"
#include "memAlloc.h"

//-----------------------------------------------------------------------
/*
  Clear section of ST's memory space
*/
void STMemory_Clear(unsigned long StartAddress, unsigned long EndAddress)
{
  Memory_Clear((void *)((unsigned long)STRam+StartAddress),EndAddress-StartAddress);
}

//-----------------------------------------------------------------------
/*
  Swap 16-bit integer to/from 68000/PC format
*/
/* Thanks to Stefan Berndtsson for the htons/l patch! - Thothy */
unsigned short int STMemory_Swap68000Int(unsigned short int var)
{
 return htons(var);
}

//-----------------------------------------------------------------------
/*
  Swap 32-bit integer to/from 68000/PC format
*/
unsigned long STMemory_Swap68000Long(unsigned long var)
{
 return htonl(var);
}

//-----------------------------------------------------------------------
/*
  Write 32-bit word into ST memory space, NOTE - value will be convert to 68000 endian
*/
void STMemory_WriteLong(unsigned long Address,unsigned long Var)
{
  unsigned long *pLongWord;

  Address &= 0xffffff;
  pLongWord = (unsigned long *)((unsigned long)STRam+Address);
  *pLongWord = STMemory_Swap68000Long(Var);
}

//-----------------------------------------------------------------------
/*
  Write 16-bit word into ST memory space, NOTE - value will be convert to 68000 endian
*/
void STMemory_WriteWord(unsigned long Address,unsigned short int Var)
{
  unsigned short int *pShortWord;

  Address &= 0xffffff;
  pShortWord = (unsigned short int *)((unsigned long)STRam+Address);
  *pShortWord = STMemory_Swap68000Int(Var);
}

//-----------------------------------------------------------------------
/*
  Write 8-bit byte into ST memory space
*/
void STMemory_WriteByte(unsigned long Address,unsigned char Var)
{
  unsigned char *pChar;

  Address &= 0xffffff;
  pChar = (unsigned char *)((unsigned long)STRam+Address);
  *pChar = Var;
}

//-----------------------------------------------------------------------
/*
  Read 32-bit word from ST memory space, NOTE - value will be converted to PC endian
*/
unsigned long STMemory_ReadLong(unsigned long Address)
{
  unsigned long *pLongWord;

  Address &= 0xffffff;
  pLongWord = (unsigned long *)((unsigned long)STRam+Address);
  return( STMemory_Swap68000Long(*pLongWord) );
}

//-----------------------------------------------------------------------
/*
  Read 16-bit word from ST memory space, NOTE - value will be converted to PC endian
*/
unsigned short int STMemory_ReadWord(unsigned long Address)
{
  unsigned short int *pShortWord;

  Address &= 0xffffff;
  pShortWord = (unsigned short int *)((unsigned long)STRam+Address);
  return( STMemory_Swap68000Int(*pShortWord) );
}

//-----------------------------------------------------------------------
/*
  Read 8-bit byte from ST memory space
*/
unsigned char STMemory_ReadByte(unsigned long Address)
{
  unsigned char *pChar;

  Address &= 0xffffff;
  pChar = (unsigned char *)((unsigned long)STRam+Address);
  return( *pChar );
}

//-----------------------------------------------------------------------
/*
  Write 32-bit word into PC memory space, NOTE - value will be convert to 68000 endian
*/
void STMemory_WriteLong_PCSpace(void *pAddress,unsigned long Var)
{
  unsigned long *pLongWord=(unsigned long *)pAddress;

  *pLongWord = STMemory_Swap68000Long(Var);
}

//-----------------------------------------------------------------------
/*
  Write 16-bit word into PC memory space, NOTE - value will be convert to 68000 endian
*/
void STMemory_WriteWord_PCSpace(void *pAddress,unsigned short int Var)
{
  unsigned short int *pShortWord=(unsigned short int *)pAddress;

  *pShortWord = STMemory_Swap68000Int(Var);
}

//-----------------------------------------------------------------------
/*
  Read 32-bit word from PC memory space, NOTE - value will be convert to 68000 endian
*/
unsigned long STMemory_ReadLong_PCSpace(void *pAddress)
{
  unsigned long *pLongWord=(unsigned long *)pAddress;

  return( STMemory_Swap68000Long(*pLongWord) );
}

//-----------------------------------------------------------------------
/*
  Read 16-bit word from PC memory space, NOTE - value will be convert to 68000 endian
*/
unsigned short int STMemory_ReadWord_PCSpace(void *pAddress)
{
  unsigned short int *pShortWord=(unsigned short int *)pAddress;

  return( STMemory_Swap68000Int(*pShortWord) );
}
