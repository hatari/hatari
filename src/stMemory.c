/*
  Hatari - stMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  ST Memory access functions.
*/
const char STMemory_fileid[] = "Hatari stMemory.c : " __DATE__ " " __TIME__;

#include "stMemory.h"
#include "configuration.h"
#include "floppy.h"
#include "ioMem.h"
#include "gemdos.h"
#include "tos.h"
#include "log.h"
#include "vdi.h"
#include "memory.h"


/* STRam points to our ST Ram. Unless the user enabled SMALL_MEM where we have
 * to save memory, this includes all TOS ROM and IO hardware areas for ease
 * and emulation speed - so we create a 16 MiB array directly here.
 * But when the user turned on ENABLE_SMALL_MEM, this only points to a malloc'ed
 * buffer with the ST RAM; the ROM and IO memory will be handled separately. */
#if ENABLE_SMALL_MEM
Uint8 *STRam;
#else
Uint8 STRam[16*1024*1024];
#endif

Uint32 STRamEnd;            /* End of ST Ram, above this address is no-mans-land and ROM/IO memory */


/*-----------------------------------------------------------------------*/
/**
 * Clear section of ST's memory space.
 */
void STMemory_Clear(Uint32 StartAddress, Uint32 EndAddress)
{
	memset(&STRam[StartAddress], 0, EndAddress-StartAddress);
}


/*-----------------------------------------------------------------------*/
/**
 * Set default memory configuration, connected floppies, memory size and
 * clear the ST-RAM area.
 * As TOS checks hardware for memory size + connected devices on boot-up
 * we set these values ourselves and fill in the magic numbers so TOS
 * skips these tests.
 */
void STMemory_SetDefaultConfig(void)
{
	int i;
	int screensize;
	int memtop;
	Uint8 nMemControllerByte;
	static const int MemControllerTable[] =
	{
		0x01,   /* 512 KiB */
		0x05,   /* 1 MiB */
		0x02,   /* 2 MiB */
		0x06,   /* 2.5 MiB */
		0x0A    /* 4 MiB */
	};

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

	/* Set memory size, adjust for extra VDI screens if needed.
	 * Note: TOS seems to set phys_top-0x8000 as the screen base
	 * address - so we have to move phys_top down in VDI resolution
	 * mode, although there is more "physical" ST RAM available. */
	screensize = VDIWidth * VDIHeight / 8 * VDIPlanes;
        /* Use 32 kiB in normal screen mode or when the screen size is smaller than 32 kiB */
	if (!bUseVDIRes || screensize < 0x8000)
		screensize = 0x8000;
	/* mem top - upper end of user memory (right before the screen memory).
	 * Note: memtop / phystop must be dividable by 512, or TOS crashes */
	memtop = (STRamEnd - screensize) & 0xfffffe00;
	STMemory_WriteLong(0x436, memtop);
	/* phys top - This must be memtop + 0x8000 to make TOS happy */
	STMemory_WriteLong(0x42e, memtop+0x8000);

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
		nFalcSysCntrl &= FALCON_MONITOR_MASK;
		switch(ConfigureParams.Screen.nMonitorType) {
		case MONITOR_TYPE_TV:
			nFalcSysCntrl |= FALCON_MONITOR_TV;
			break;
		case MONITOR_TYPE_VGA:
			nFalcSysCntrl |= FALCON_MONITOR_VGA;
			break;
		case MONITOR_TYPE_RGB:
			nFalcSysCntrl |= FALCON_MONITOR_RGB;
			break;
		case MONITOR_TYPE_MONO:
			nFalcSysCntrl |= FALCON_MONITOR_MONO;
			break;
		}
		STMemory_WriteByte(0xff8006, nFalcSysCntrl);
	}

	/* Set TOS floppies */
	STMemory_WriteWord(0x446, nBootDrive);          /* Boot up on A(0) or C(2) */

	/* Create connected drives mask: */
	ConnectedDriveMask = STMemory_ReadLong(0x4c2);  // Get initial drive mask (see what TOS thinks)
	ConnectedDriveMask |= 0x03;                     // Always use A: and B:
	if (GEMDOS_EMU_ON)
	{
		for (i = 0; i < MAX_HARDDRIVES; i++)
		{
			if (emudrives[i] != NULL)     // Is this GEMDOS drive enabled?
				ConnectedDriveMask |= (1 << emudrives[i]->hd_letter);
		}
	}
	/* Set connected drives system variable.
	 * NOTE: some TOS images overwrite this value, see 'OpCode_SysInit', too */
	STMemory_WriteLong(0x4c2, ConnectedDriveMask);
}
