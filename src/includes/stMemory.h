/*
  Hatari - stMemory.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_STMEMORY_H
#define HATARI_STMEMORY_H

#include "sysdeps.h"
#include "maccess.h"
#include "main.h"


extern Uint8 STRam[16*1024*1024];
extern Uint32 STRamEnd;


/* Offset ST address to PC pointer: */
#define STRAM_ADDR(Var)  ((unsigned long)STRam+((Uint32)(Var) & 0x00ffffff))


/*-----------------------------------------------------------------------*/
/*
  Write 32-bit word into ST memory space.
  NOTE - value will be convert to 68000 endian
*/
static inline void STMemory_WriteLong(Uint32 Address, Uint32 Var)
{
	Address &= 0xffffff;
	do_put_mem_long((uae_u32 *)&STRam[Address], Var);
}

/*-----------------------------------------------------------------------*/
/*
  Write 16-bit word into ST memory space.
  NOTE - value will be convert to 68000 endian.
*/
static inline void STMemory_WriteWord(Uint32 Address, Uint16 Var)
{
	Address &= 0xffffff;
	do_put_mem_word((uae_u16 *)&STRam[Address], Var);
}

/*-----------------------------------------------------------------------*/
/*
  Write 8-bit byte into ST memory space.
*/
static inline void STMemory_WriteByte(Uint32 Address, Uint8 Var)
{
	Address &= 0xffffff;
	STRam[Address] = Var;
}


/*-----------------------------------------------------------------------*/
/*
  Read 32-bit word from ST memory space.
  NOTE - value will be converted to PC endian.
*/
static inline Uint32 STMemory_ReadLong(Uint32 Address)
{
	Address &= 0xffffff;
	return do_get_mem_long((uae_u32 *)&STRam[Address]);
}

/*-----------------------------------------------------------------------*/
/*
  Read 16-bit word from ST memory space.
  NOTE - value will be converted to PC endian.
*/
static inline Uint16 STMemory_ReadWord(Uint32 Address)
{
	Address &= 0xffffff;
	return do_get_mem_word((uae_u16 *)&STRam[Address]);
}

/*-----------------------------------------------------------------------*/
/*
  Read 8-bit byte from ST memory space
*/
static inline Uint8 STMemory_ReadByte(Uint32 Address)
{
	Address &= 0xffffff;
	return STRam[Address];
}


extern void STMemory_Clear(unsigned long StartAddress, unsigned long EndAddress);

#endif
