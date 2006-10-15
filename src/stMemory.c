/*
  Hatari - stMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  ST Memory access functions.
*/
const char STMemory_rcsid[] = "Hatari $Id: stMemory.c,v 1.11 2006-10-15 21:21:54 thothy Exp $";

#include "stMemory.h"
#include "configuration.h"
#include "floppy.h"
#include "ioMem.h"
#include "tos.h"
#include "vdi.h"
#include "uae-cpu/memory.h"


Uint8 STRam[16*1024*1024];      /* This is our ST Ram, includes all TOS/hardware areas for ease */
Uint32 STRamEnd;                /* End of ST Ram, above this address is no-mans-land and hardware vectors */


/*-----------------------------------------------------------------------*/
/*
  Clear section of ST's memory space.
*/
void STMemory_Clear(Uint32 StartAddress, Uint32 EndAddress)
{
	memset(&STRam[StartAddress], 0, EndAddress-StartAddress);
}


/*-----------------------------------------------------------------------*/
/*
  Set default memory configuration, connected floppies, memory size and
  clear the ST-RAM area.
  As TOS checks hardware for memory size + connected devices on boot-up
  we set these values ourselves and fill in the magic numbers so TOS
  skips these tests.
*/
void STMemory_SetDefaultConfig(void)
{
	int i;
	Uint8 nMemControllerByte;
	static const int MemControllerTable[] =
	{
		0x01,   /* 512 KiB */
		0x05,   /* 1 MiB */
		0x02,   /* 2 MiB */
		0x06,   /* 2.5 MiB */
		0x0A    /* 4 MiB */
	};

	/* Calculate end of RAM */
	if (ConfigureParams.Memory.nMemorySize > 0 && ConfigureParams.Memory.nMemorySize <= 14)
		STRamEnd = ConfigureParams.Memory.nMemorySize * 0x100000;
	else
		STRamEnd = 0x80000;   /* 512 KiB */

	if (bRamTosImage)
	{
		/* Clear ST-RAM, excluding the RAM TOS image */
		STMemory_Clear(0x00000000, TosAddress);
		STMemory_Clear(TosAddress+TosSize, STRamEnd);
	}
	else
	{
		/* Clear whole ST-RAM */
		STMemory_Clear(0x00000000, STRamEnd);
	}

	/* Mirror ROM boot vectors */
	STMemory_WriteLong(0x00, STMemory_ReadLong(TosAddress));
	STMemory_WriteLong(0x04, STMemory_ReadLong(TosAddress+4));

	/* Fill in magic numbers, so TOS does not try to reference MMU */
	STMemory_WriteLong(0x420, 0x752019f3);            /* memvalid - configuration is valid */
	STMemory_WriteLong(0x43a, 0x237698aa);            /* another magic # */
	STMemory_WriteLong(0x51a, 0x5555aaaa);            /* and another */

	/* Set memory size, adjust for extra VDI screens if needed */
	if (bUseVDIRes)
	{
		/* This is enough for 1024x768x16colors (0x60000) */
		STMemory_WriteLong(0x436, STRamEnd-0x60000);  /* mem top - upper end of user memory (before 32k screen) */
		STMemory_WriteLong(0x42e, STRamEnd-0x58000);  /* phys top */
	}
	else
	{
		STMemory_WriteLong(0x436, STRamEnd-0x8000);   /* mem top - upper end of user memory (before 32k screen) */
		STMemory_WriteLong(0x42e, STRamEnd);          /* phys top */
	}

	/* Set memory controller byte according to different memory sizes */
	/* Setting per bank: %00=128k %01=512k %10=2Mb %11=reserved. - e.g. %1010 means 4Mb */
	if (ConfigureParams.Memory.nMemorySize <= 4)
		nMemControllerByte = MemControllerTable[ConfigureParams.Memory.nMemorySize];
	else
		nMemControllerByte = 0x0f;
	STMemory_WriteByte(0x424, nMemControllerByte);
	IoMem_WriteByte(0xff8001, nMemControllerByte);

	/* Set the Falcon memory (and monitor) configuration register: */
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
	{
		Uint8 nFalcSysCntrl;
		/* TODO: How does the 0xff8006 register work exactly on the Falcon?
		 * The following values are just guessed... */
		if (ConfigureParams.Memory.nMemorySize == 14)
			nFalcSysCntrl=  0x26;
		else if (ConfigureParams.Memory.nMemorySize >= 4)
			nFalcSysCntrl = 0x16;
		else if (ConfigureParams.Memory.nMemorySize >= 2)
			nFalcSysCntrl = 0x14;
		else if (ConfigureParams.Memory.nMemorySize == 1)
			nFalcSysCntrl = 0x06;
		else
			nFalcSysCntrl = 0x04;
		if (!ConfigureParams.Screen.bUseHighRes)
			nFalcSysCntrl |= 0x40;
		STMemory_WriteByte(0xff8006, nFalcSysCntrl);
	}

	/* Set TOS floppies */
	STMemory_WriteWord(0x446, nBootDrive);          /* Boot up on A(0) or C(2) */

	/* Create connected drives mask: */
	ConnectedDriveMask = 0;
	for (i = 0; i < nNumDrives; i++)
	{
		ConnectedDriveMask |= (1 << i);
	}
	/* Set connected drives system variable.
	 * NOTE: some TOS images overwrite this value, see 'OpCode_SysInit', too */
	STMemory_WriteLong(0x4c2, ConnectedDriveMask);

	/* Initialize the memory banks: */
	memory_uninit();
	memory_init(STRamEnd, 0, TosAddress);
}
