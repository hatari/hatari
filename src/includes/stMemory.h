/*
  Hatari - stMemory.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_STMEMORY_H
#define HATARI_STMEMORY_H

#include "sysdeps.h"
#include "maccess.h"


/*-----------------------------------------------------------------------*/
/*
  Write 32-bit word into ST memory space.
  NOTE - value will be convert to 68000 endian
*/
static inline void STMemory_WriteLong(Uint32 Address, Uint32 Var)
{
  Address &= 0xffffff;
  do_put_mem_long((uae_u32 *)((Uint32)STRam+Address), Var);
}

/*-----------------------------------------------------------------------*/
/*
  Write 16-bit word into ST memory space.
  NOTE - value will be convert to 68000 endian.
*/
static inline void STMemory_WriteWord(Uint32 Address, Uint16 Var)
{
  Address &= 0xffffff;
  do_put_mem_word((uae_u16 *)((Uint32)STRam+Address), Var);
}

/*-----------------------------------------------------------------------*/
/*
  Write 8-bit byte into ST memory space.
*/
static inline void STMemory_WriteByte(Uint32 Address, Uint8 Var)
{
  Address &= 0xffffff;
  *(Uint8 *)((Uint32)STRam+Address) = Var;
}


/*-----------------------------------------------------------------------*/
/*
  Read 32-bit word from ST memory space.
  NOTE - value will be converted to PC endian.
*/
static inline Uint32 STMemory_ReadLong(Uint32 Address)
{
  Address &= 0xffffff;
  return do_get_mem_long((uae_u32 *)((Uint32)STRam+Address));
}

/*-----------------------------------------------------------------------*/
/*
  Read 16-bit word from ST memory space.
  NOTE - value will be converted to PC endian.
*/
static inline Uint16 STMemory_ReadWord(Uint32 Address)
{
  Address &= 0xffffff;
  return do_get_mem_word((uae_u16 *)((Uint32)STRam+Address));
}

/*-----------------------------------------------------------------------*/
/*
  Read 8-bit byte from ST memory space
*/
static inline Uint8 STMemory_ReadByte(Uint32 Address)
{
  Address &= 0xffffff;
  return *(Uint8 *)((Uint32)STRam+Address);
}


extern void STMemory_Clear(unsigned long StartAddress, unsigned long EndAddress);
extern void STMemory_WriteLong_PCSpace(void *pAddress, unsigned long Var);
extern void STMemory_WriteWord_PCSpace(void *pAddress, unsigned short Var);
extern unsigned long STMemory_ReadLong_PCSpace(void *pAddress);
extern unsigned short STMemory_ReadWord_PCSpace(void *pAddress);

#endif
