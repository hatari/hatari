/*
  Hatari - ioMem.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_IOMEM_H
#define HATARI_IOMEM_H

#include "config.h"

#if ENABLE_SMALL_MEM
# include "sysdeps.h"
# include "maccess.h"
# include "main.h"
extern uae_u8 *IOmemory;
# define IoMem (IOmemory-0xff0000)
#else
# include "stMemory.h"
# define IoMem STRam
#endif  /* ENABLE_SMALL_MEM */


extern int nIoMemAccessSize;
extern uint32_t IoAccessFullAddress;
extern uint32_t IoAccessBaseAddress;
extern uint32_t IoAccessCurrentAddress;

extern int	IoAccessInstrCount;

enum FALCON_BUS_MODE {
	STE_BUS_COMPATIBLE,
	FALCON_ONLY_BUS
};

/**
 * Read 32-bit word from IO memory space without interception.
 * NOTE - value will be converted to PC endian.
 */
static inline uint32_t IoMem_ReadLong(uint32_t Address)
{
	Address &= 0x0ffffff;
	return do_get_mem_long(&IoMem[Address]);
}


/**
 * Read 16-bit word from IO memory space without interception.
 * NOTE - value will be converted to PC endian.
 */
static inline uint16_t IoMem_ReadWord(uint32_t Address)
{
	Address &= 0x0ffffff;
	return do_get_mem_word(&IoMem[Address]);
}


/**
 * Read 8-bit byte from IO memory space without interception.
 */
static inline uint8_t IoMem_ReadByte(uint32_t Address)
{
	Address &= 0x0ffffff;
 	return IoMem[Address];
}


/**
 * Write 32-bit word into IO memory space without interception.
 * NOTE - value will be convert to 68000 endian
 */
static inline void IoMem_WriteLong(uint32_t Address, uint32_t Var)
{
	Address &= 0x0ffffff;
	do_put_mem_long(&IoMem[Address], Var);
}


/**
 * Write 16-bit word into IO memory space without interception.
 * NOTE - value will be convert to 68000 endian.
 */
static inline void IoMem_WriteWord(uint32_t Address, uint16_t Var)
{
	Address &= 0xffffff;
	do_put_mem_word(&IoMem[Address], Var);
}


/**
 * Write 8-bit byte into IO memory space without interception.
 */
static inline void IoMem_WriteByte(uint32_t Address, uint8_t Var)
{
	Address &= 0x0ffffff;
	IoMem[Address] = Var;
}


extern void IoMem_Init(void);
extern void IoMem_UnInit(void);
extern void IoMem_Reset(void);

extern uint8_t IoMemTabMegaSTE_DIPSwitches_Read(void);

extern uint8_t IoMemTabFalcon_DIPSwitches_Read(void);
extern void IoMem_SetFalconBusMode(enum FALCON_BUS_MODE mode);
extern bool IoMem_IsFalconBusMode(void);

extern uae_u32 REGPARAM3 IoMem_bget(uaecptr addr);
extern uae_u32 REGPARAM3 IoMem_wget(uaecptr addr);
extern uae_u32 REGPARAM3 IoMem_lget(uaecptr addr);

extern void REGPARAM3 IoMem_bput(uaecptr addr, uae_u32 val);
extern void REGPARAM3 IoMem_wput(uaecptr addr, uae_u32 val);
extern void REGPARAM3 IoMem_lput(uaecptr addr, uae_u32 val);

extern bool IoMem_CheckBusError ( uint32_t addr );
extern void IoMem_BusErrorEvenReadAccess(void);
extern void IoMem_BusErrorOddReadAccess(void);
extern void IoMem_BusErrorEvenWriteAccess(void);
extern void IoMem_BusErrorOddWriteAccess(void);
extern void IoMem_VoidRead(void);
extern void IoMem_VoidRead_00(void);
extern void IoMem_VoidWrite(void);
extern void IoMem_WriteWithoutInterception(void);
extern void IoMem_ReadWithoutInterception(void);

extern void IoMem_MemorySnapShot_Capture(bool bSave);

#endif
