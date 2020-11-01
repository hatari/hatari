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


#define	MEM_BANK_SIZE_128	( 128 * 1024 )		/* 00b */
#define	MEM_BANK_SIZE_512	( 512 * 1024 )		/* 01b */
#define	MEM_BANK_SIZE_2048	( 2048 * 1024 )		/* 10b */
#define	MEM_BANK_SIZE_8192	( 8192 * 1024 )		/* for TT */

extern Uint32	RAM_Bank0_Size;
extern Uint32	RAM_Bank1_Size;

extern Uint32	MMU_Bank0_Size;
extern Uint32	MMU_Bank1_Size;


extern void STMemory_Init ( int RAM_Size_Byte );
extern void STMemory_Reset ( bool bCold );

extern bool STMemory_SafeClear(Uint32 addr, unsigned int len);
extern bool STMemory_SafeCopy(Uint32 addr, Uint8 *src, unsigned int len, const char *name);
extern void STMemory_MemorySnapShot_Capture(bool bSave);
extern void STMemory_SetDefaultConfig(void);
extern int  STMemory_CorrectSTRamSize(void);
extern bool STMemory_CheckAreaType ( Uint32 addr , int size , int mem_type );
extern bool STMemory_CheckAddrBusError ( Uint32 addr );
extern void *STMemory_STAddrToPointer ( Uint32 addr );
extern char *STMemory_GetStringPointer(uint32_t addr);

extern void	STMemory_Write ( Uint32 addr , Uint32 val , int size );
extern void	STMemory_WriteLong ( Uint32 addr , Uint32 val );
extern void	STMemory_WriteWord ( Uint32 addr , Uint16 val );
extern void	STMemory_WriteByte ( Uint32 addr , Uint8 val );
extern Uint32	STMemory_Read ( Uint32 addr , int size );
extern Uint32	STMemory_ReadLong ( Uint32 addr );
extern Uint16	STMemory_ReadWord ( Uint32 addr );
extern Uint8	STMemory_ReadByte ( Uint32 addr );

extern Uint16	STMemory_DMA_ReadWord ( Uint32 addr );
extern void	STMemory_DMA_WriteWord ( Uint32 addr , Uint16 value );
extern Uint8	STMemory_DMA_ReadByte ( Uint32 addr );
extern void	STMemory_DMA_WriteByte ( Uint32 addr , Uint8 value );

extern void	STMemory_MMU_Config_ReadByte ( void );
extern void	STMemory_MMU_Config_WriteByte ( void );

extern int	STMemory_RAM_Validate_Size_KB ( int TotalMem );
extern bool	STMemory_RAM_SetBankSize ( int TotalMem , Uint32 *pBank0_Size , Uint32 *pBank1_Size , Uint8 *pMMU_Conf );
extern Uint32	STMemory_MMU_Translate_Addr ( Uint32 addr_logical );

#endif
