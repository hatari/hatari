/*
  Hatari - stMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  ST Memory access functions - take care of endian swaps.
*/
char STMemory_rcsid[] = "Hatari $Id: stMemory.c,v 1.4 2004-04-14 22:36:58 thothy Exp $";

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
  memset((void *)((unsigned long)STRam+StartAddress), 0, EndAddress-StartAddress);
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
