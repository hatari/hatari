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

extern uae_u8 *TTmemory;
extern uae_u32 TTmem_size;

extern Uint32 STRamEnd;


extern bool STMemory_SafeCopy(Uint32 addr, Uint8 *src, unsigned int len, const char *name);
extern void STMemory_MemorySnapShot_Capture(bool bSave);
extern void STMemory_SetDefaultConfig(void);
extern bool STMemory_CheckAreaType ( Uint32 addr , int size , int mem_type );
extern bool STMemory_CheckRegionBusError ( Uint32 addr );
extern void *STMemory_STAddrToPointer ( Uint32 addr );

extern void	STMemory_Write ( Uint32 addr , Uint32 val , int size );
extern void	STMemory_WriteLong ( Uint32 addr , Uint32 val );
extern void	STMemory_WriteWord ( Uint32 addr , Uint16 val );
extern void	STMemory_WriteByte ( Uint32 addr , Uint8 val );
extern Uint32	STMemory_Read ( Uint32 addr , int size );
extern Uint32	STMemory_ReadLong ( Uint32 addr );
extern Uint16	STMemory_ReadWord ( Uint32 addr );
extern Uint8	STMemory_ReadByte ( Uint32 addr );

#endif
