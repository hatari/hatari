/*
  Hatari - ioMem.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_IOMEM_H
#define HATARI_IOMEM_H

#if 0
#include "sysdeps.h"
#include "maccess.h"
#include "main.h"
#else
/* TODO: IoMem will later become independent from STRam... */
#include "stMemory.h"
#define IoMem STRam
#endif


extern BOOL bEnableBlitter;


/*-----------------------------------------------------------------------*/
/*
  Read 32-bit word from IO memory space without interception.
  NOTE - value will be converted to PC endian.
*/
static inline Uint32 IoMem_ReadLong(Uint32 Address)
{
	Address &= 0x0ffffff;
	return do_get_mem_long((uae_u32 *)&IoMem[Address]);
}

/*-----------------------------------------------------------------------*/
/*
  Read 16-bit word from IO memory space without interception.
  NOTE - value will be converted to PC endian.
*/
static inline Uint16 IoMem_ReadWord(Uint32 Address)
{
	Address &= 0x0ffffff;
	return do_get_mem_word((uae_u16 *)&IoMem[Address]);
}

/*-----------------------------------------------------------------------*/
/*
  Read 8-bit byte from IO memory space without interception.
*/
static inline Uint8 IoMem_ReadByte(Uint32 Address)
{
	Address &= 0x0ffffff;
 	return IoMem[Address];
}


/*-----------------------------------------------------------------------*/
/*
  Write 32-bit word into IO memory space without interception.
  NOTE - value will be convert to 68000 endian
*/
static inline void IoMem_WriteLong(Uint32 Address, Uint32 Var)
{
	Address &= 0x0ffffff;
	do_put_mem_long((uae_u32 *)&IoMem[Address], Var);
}

/*-----------------------------------------------------------------------*/
/*
  Write 16-bit word into IO memory space without interception.
  NOTE - value will be convert to 68000 endian.
*/
static inline void IoMem_WriteWord(Uint32 Address, Uint16 Var)
{
	Address &= 0xffffff;
	do_put_mem_word((uae_u16 *)&IoMem[Address], Var);
}

/*-----------------------------------------------------------------------*/
/*
  Write 8-bit byte into IO memory space without interception.
*/
static inline void IoMem_WriteByte(Uint32 Address, Uint8 Var)
{
	Address &= 0x0ffffff;
	IoMem[Address] = Var;
}


extern void IoMem_Init(void);
extern void IoMem_UnInit(void);

extern uae_u32 IoMem_bget(uaecptr addr);
extern uae_u32 IoMem_wget(uaecptr addr);
extern uae_u32 IoMem_lget(uaecptr addr);

extern void IoMem_bput(uaecptr addr, uae_u32 val);
extern void IoMem_wput(uaecptr addr, uae_u32 val);
extern void IoMem_lput(uaecptr addr, uae_u32 val);

/* deprecated: */
extern void Intercept_EnableBlitter(BOOL enableFlag);
extern void Intercept_ModifyTablesForBusErrors(void);

#endif
