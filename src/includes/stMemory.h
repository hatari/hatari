/*
  Hatari - stMemory.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_STMEMORY_H
#define HATARI_STMEMORY_H

#include "main.h"
#include "sysdeps.h"
#include "maccess.h"
#include "memory.h"


#if ENABLE_SMALL_MEM
extern Uint8 *STRam;
extern uae_u8 *ROMmemory;
# define RomMem (ROMmemory-0xe00000)
#else
extern Uint8 STRam[16*1024*1024];
#define RomMem STRam
#endif  /* ENABLE_SMALL_MEM */

extern Uint32 STRamEnd;


/**
 * Write 32-bit word into ST memory space.
 * NOTE - value will be convert to 68000 endian
 */
static inline void STMemory_WriteLong(Uint32 Address, Uint32 Var)
{
	Address &= 0xffffff;
#if ENABLE_SMALL_MEM
	if (Address >= 0xe00000)
		do_put_mem_long(&ROMmemory[Address-0xe00000], Var);
	else
		do_put_mem_long(&STRam[Address], Var);
#else
	do_put_mem_long(&STRam[Address], Var);
#endif
}


/**
 * Write 16-bit word into ST memory space.
 * NOTE - value will be convert to 68000 endian.
 */
static inline void STMemory_WriteWord(Uint32 Address, Uint16 Var)
{
	Address &= 0xffffff;
#if ENABLE_SMALL_MEM
	if (Address >= 0xe00000)
		do_put_mem_word(&ROMmemory[Address-0xe00000], Var);
	else
		do_put_mem_word(&STRam[Address], Var);
#else
	do_put_mem_word(&STRam[Address], Var);
#endif
}


/**
 * Write 8-bit byte into ST memory space.
 */
static inline void STMemory_WriteByte(Uint32 Address, Uint8 Var)
{
	Address &= 0xffffff;
#if ENABLE_SMALL_MEM
	if (Address >= 0xe00000)
		ROMmemory[Address-0xe00000] = Var;
	else
		STRam[Address] = Var;
#else
	STRam[Address] = Var;
#endif
}


/**
 * Read 32-bit word from ST memory space.
 * NOTE - value will be converted to PC endian.
 */
static inline Uint32 STMemory_ReadLong(Uint32 Address)
{
	Address &= 0xffffff;
#if ENABLE_SMALL_MEM
	if (Address >= 0xe00000)
		return do_get_mem_long(&ROMmemory[Address-0xe00000]);
	else
		return do_get_mem_long(&STRam[Address]);
#else
	return do_get_mem_long(&STRam[Address]);
#endif
}


/**
 * Read 16-bit word from ST memory space.
 * NOTE - value will be converted to PC endian.
 */
static inline Uint16 STMemory_ReadWord(Uint32 Address)
{
	Address &= 0xffffff;
#if ENABLE_SMALL_MEM
	if (Address >= 0xe00000)
		return do_get_mem_word(&ROMmemory[Address-0xe00000]);
	else
		return do_get_mem_word(&STRam[Address]);
#else
	return do_get_mem_word(&STRam[Address]);
#endif
}


/**
 * Read 8-bit byte from ST memory space
 */
static inline Uint8 STMemory_ReadByte(Uint32 Address)
{
	Address &= 0xffffff;
#if ENABLE_SMALL_MEM
	if (Address >= 0xe00000)
		return ROMmemory[Address-0xe00000];
	else
		return STRam[Address];
#else
	return STRam[Address];
#endif
}


extern bool STMemory_SafeCopy(Uint32 addr, Uint8 *src, unsigned int len, const char *name);
extern void STMemory_MemorySnapShot_Capture(bool bSave);
extern void STMemory_SetDefaultConfig(void);
extern bool STMemory_CheckAreaType ( Uint32 addr , int size , int mem_type );
extern void *STMemory_STAddrToPointer ( Uint32 addr );

#endif
