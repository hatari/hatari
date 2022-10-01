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
extern uint8_t *STRam;
extern uae_u8 *ROMmemory;
# define RomMem (ROMmemory-0xe00000)
#else
extern uint8_t STRam[16*1024*1024];
#define RomMem STRam
#endif  /* ENABLE_SMALL_MEM */

extern uae_u8 *TTmemory;
extern uae_u32 TTmem_size;

extern uint32_t STRamEnd;


#define	MEM_BANK_SIZE_128	( 128 * 1024 )		/* 00b */
#define	MEM_BANK_SIZE_512	( 512 * 1024 )		/* 01b */
#define	MEM_BANK_SIZE_2048	( 2048 * 1024 )		/* 10b */
#define	MEM_BANK_SIZE_8192	( 8192 * 1024 )		/* for TT */

extern uint32_t	RAM_Bank0_Size;
extern uint32_t	RAM_Bank1_Size;

extern uint32_t	MMU_Bank0_Size;
extern uint32_t	MMU_Bank1_Size;


extern void STMemory_Init ( int RAM_Size_Byte );
extern void STMemory_Reset ( bool bCold );

extern bool STMemory_SafeClear(uint32_t addr, unsigned int len);
extern bool STMemory_SafeCopy(uint32_t addr, uint8_t *src, unsigned int len, const char *name);
extern void STMemory_MemorySnapShot_Capture(bool bSave);
extern void STMemory_SetDefaultConfig(void);
extern int  STMemory_CorrectSTRamSize(void);
extern bool STMemory_CheckAreaType ( uint32_t addr , int size , int mem_type );
extern bool STMemory_CheckAddrBusError ( uint32_t addr );
extern void *STMemory_STAddrToPointer ( uint32_t addr );
extern char *STMemory_GetStringPointer(uint32_t addr);

extern void	STMemory_Write ( uint32_t addr , uint32_t val , int size );
extern void	STMemory_WriteLong ( uint32_t addr , uint32_t val );
extern void	STMemory_WriteWord ( uint32_t addr , uint16_t val );
extern void	STMemory_WriteByte ( uint32_t addr , uint8_t val );
extern uint32_t	STMemory_Read ( uint32_t addr , int size );
extern uint32_t	STMemory_ReadLong ( uint32_t addr );
extern uint16_t	STMemory_ReadWord ( uint32_t addr );
extern uint8_t	STMemory_ReadByte ( uint32_t addr );

extern uint16_t	STMemory_DMA_ReadWord ( uint32_t addr );
extern void	STMemory_DMA_WriteWord ( uint32_t addr , uint16_t value );
extern uint8_t	STMemory_DMA_ReadByte ( uint32_t addr );
extern void	STMemory_DMA_WriteByte ( uint32_t addr , uint8_t value );

extern void	STMemory_MMU_Config_ReadByte ( void );
extern void	STMemory_MMU_Config_WriteByte ( void );

extern int	STMemory_RAM_Validate_Size_KB ( int TotalMem );
extern bool	STMemory_RAM_SetBankSize ( int TotalMem , uint32_t *pBank0_Size , uint32_t *pBank1_Size , uint8_t *pMMU_Conf );
extern uint32_t	STMemory_MMU_Translate_Addr ( uint32_t addr_logical );

#endif
