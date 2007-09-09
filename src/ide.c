/*
  Hatari - ide.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This is where we intercept read/writes to/from the IDE controller hardware.
*/
const char Ide_rcsid[] = "Hatari $Id: ide.c,v 1.3 2007-09-09 20:49:58 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "ide.h"
#include "m68000.h"
#include "stMemory.h"
#include "sysdeps.h"


#define IDE_DEBUG 0

#if IDE_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


/*-----------------------------------------------------------------------*/
/**
 * Handle byte read access from IDE IO memory.
 */
uae_u32 Ide_Mem_bget(uaecptr addr)
{
	Dprintf(("IdeMem_bget($%x)\n", addr));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr >= 0xf00040 || !ConfigureParams.HardDisk.bUseIdeHardDiskImage)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		return -1;
	}

	return STRam[addr];
}


/*-----------------------------------------------------------------------*/
/**
 * Handle word read access from IDE IO memory.
 */
uae_u32 Ide_Mem_wget(uaecptr addr)
{
	Dprintf(("IdeMem_wget($%x)\n", addr));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr >= 0xf00040 || !ConfigureParams.HardDisk.bUseIdeHardDiskImage)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		//fprintf(stderr, "Illegal IDE IO memory access: IdeMem_wget($%x)\n", addr);
		return -1;
	}

	return STMemory_ReadWord(addr);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle long-word read access from IDE IO memory.
 */
uae_u32 Ide_Mem_lget(uaecptr addr)
{
	Dprintf(("IdeMem_lget($%x)\n", addr));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr >= 0xf00040 || !ConfigureParams.HardDisk.bUseIdeHardDiskImage)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		//fprintf(stderr, "Illegal IDE IO memory access: IdeMem_lget($%x)\n", addr);
		return -1;
	}

	return STMemory_ReadLong(addr);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle byte write access to IDE IO memory.
 */
void Ide_Mem_bput(uaecptr addr, uae_u32 val)
{
	Dprintf(("IdeMem_bput($%x, $%x)\n", addr, val));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr >= 0xf00040 || !ConfigureParams.HardDisk.bUseIdeHardDiskImage)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
		//fprintf(stderr, "Illegal IDE IO memory access: IdeMem_bput($%x)\n", addr);
		return;
	}

	STMemory_WriteByte(addr, val);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle word write access to IDE IO memory.
 */
void Ide_Mem_wput(uaecptr addr, uae_u32 val)
{
	Dprintf(("IdeMem_wput($%x, $%x)\n", addr, val));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr >= 0xf00040 || !ConfigureParams.HardDisk.bUseIdeHardDiskImage)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
		//fprintf(stderr, "Illegal IDE IO memory access: IdeMem_wput($%x)\n", addr);
		return;
	}

	STMemory_WriteWord(addr, val);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle long-word write access to IDE IO memory.
 */
void Ide_Mem_lput(uaecptr addr, uae_u32 val)
{
	Dprintf(("IdeMem_lput($%x, $%x)\n", addr, val));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr >= 0xf00040 || !ConfigureParams.HardDisk.bUseIdeHardDiskImage)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
		//fprintf(stderr, "Illegal IDE IO memory access: IdeMem_lput($%x)\n", addr);
		return;
	}

	STMemory_WriteLong(addr, val);
}
