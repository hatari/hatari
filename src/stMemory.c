/*
  Hatari - stMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  ST Memory access functions - take care of endian swaps.
*/
static char rcsid[] = "Hatari $Id: stMemory.c,v 1.3 2003-03-17 13:19:18 thothy Exp $";

#include <SDL_endian.h>

#include "main.h"
#include "decode.h"
#include "m68000.h"
#include "memAlloc.h"
#include "uae-cpu/maccess.h"


/*-----------------------------------------------------------------------*/
/*
  Clear section of ST's memory space.
*/
void STMemory_Clear(unsigned long StartAddress, unsigned long EndAddress)
{
  Memory_Clear((void *)((unsigned long)STRam+StartAddress),EndAddress-StartAddress);
}


/*-----------------------------------------------------------------------*/
/*
  Swap 16-bit integer to/from 68000/PC format.
*/
unsigned short STMemory_Swap68000Int(unsigned short var)
{
  return SDL_SwapBE16(var);
}

/*-----------------------------------------------------------------------*/
/*
  Swap 32-bit integer to/from 68000/PC format.
*/
unsigned long STMemory_Swap68000Long(unsigned long var)
{
  return SDL_SwapBE32(var);
}


/*-----------------------------------------------------------------------*/
/*
  Write 32-bit word into ST memory space, NOTE - value will be convert to 68000 endian
*/
void STMemory_WriteLong(unsigned long Address, unsigned long Var)
{
  uae_u32 *pLongWord;

  Address &= 0xffffff;
  pLongWord = (uae_u32 *)((unsigned long)STRam+Address);
  do_put_mem_long(pLongWord, Var);
}

/*-----------------------------------------------------------------------*/
/*
  Write 16-bit word into ST memory space.
  NOTE - value will be convert to 68000 endian.
*/
void STMemory_WriteWord(unsigned long Address, unsigned short Var)
{
  uae_u16 *pShortWord;

  Address &= 0xffffff;
  pShortWord = (uae_u16 *)((unsigned long)STRam+Address);
  do_put_mem_word(pShortWord, Var);
}

/*-----------------------------------------------------------------------*/
/*
  Write 8-bit byte into ST memory space.
*/
void STMemory_WriteByte(unsigned long Address, unsigned char Var)
{
  unsigned char *pChar;

  Address &= 0xffffff;
  pChar = (unsigned char *)((unsigned long)STRam+Address);
  *pChar = Var;
}


/*-----------------------------------------------------------------------*/
/*
  Read 32-bit word from ST memory space.
  NOTE - value will be converted to PC endian.
*/
unsigned long STMemory_ReadLong(unsigned long Address)
{
  uae_u32 *pLongWord;

  Address &= 0xffffff;
  pLongWord = (uae_u32 *)((unsigned long)STRam+Address);
  return(do_get_mem_long(pLongWord));
}

/*-----------------------------------------------------------------------*/
/*
  Read 16-bit word from ST memory space.
  NOTE - value will be converted to PC endian.
*/
unsigned short STMemory_ReadWord(unsigned long Address)
{
  uae_u16 *pShortWord;

  Address &= 0xffffff;
  pShortWord = (uae_u16 *)((unsigned long)STRam+Address);
  return(do_get_mem_word(pShortWord));
}

/*-----------------------------------------------------------------------*/
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


/*-----------------------------------------------------------------------*/
/*
  Write 32-bit word into PC memory space.
  NOTE - value will be convert to 68000 endian.
*/
void STMemory_WriteLong_PCSpace(void *pAddress, unsigned long Var)
{
  do_put_mem_long(pAddress, Var);
}

/*-----------------------------------------------------------------------*/
/*
  Write 16-bit word into PC memory space.
  NOTE - value will be convert to 68000 endian.
*/
void STMemory_WriteWord_PCSpace(void *pAddress, unsigned short Var)
{
  do_put_mem_word(pAddress, Var);
}


/*-----------------------------------------------------------------------*/
/*
  Read 32-bit word from PC memory space.
  NOTE - value will be convert to PC endian.
*/
unsigned long STMemory_ReadLong_PCSpace(void *pAddress)
{
  return(do_get_mem_long(pAddress));
}

/*-----------------------------------------------------------------------*/
/*
  Read 16-bit word from PC memory space.
  NOTE - value will be convert to PC endian.
*/
unsigned short int STMemory_ReadWord_PCSpace(void *pAddress)
{
  return(do_get_mem_word(pAddress));
}
