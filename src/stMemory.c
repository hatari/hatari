/*
  Hatari - stMemory.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  ST Memory access functions.
*/
const char STMemory_fileid[] = "Hatari stMemory.c : " __DATE__ " " __TIME__;

#include "stMemory.h"
#include "configuration.h"
#include "floppy.h"
#include "gemdos.h"
#include "ioMem.h"
#include "log.h"
#include "memory.h"
#include "memorySnapShot.h"
#include "tos.h"
#include "vdi.h"


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


/**
 * Clear section of ST's memory space.
 */
static void STMemory_Clear(Uint32 StartAddress, Uint32 EndAddress)
{
	memset(&STRam[StartAddress], 0, EndAddress-StartAddress);
}

/**
 * Copy given memory area safely to Atari RAM.
 * If the memory area isn't fully within RAM, only the valid parts are written.
 * Useful for all kinds of IO operations.
 * 
 * addr - destination Atari RAM address
 * src - source Hatari memory address
 * len - number of bytes to copy
 * name - name / description if this memory copy for error messages
 * 
 * Return true if whole copy was safe / valid.
 */
bool STMemory_SafeCopy(Uint32 addr, Uint8 *src, unsigned int len, const char *name)
{
	Uint32 end;

	if (STMemory_ValidArea(addr, len))
	{
		memcpy(&STRam[addr], src, len);
		return true;
	}
	Log_Printf(LOG_WARN, "Invalid '%s' RAM range 0x%x+%i!\n", name, addr, len);

	for (end = addr + len; addr < end; addr++, src++)
	{
		if (STMemory_ValidArea(addr, 1))
			STRam[addr] = *src;
	}
	return false;
}

/**
 * Save/Restore snapshot of RAM / ROM variables
 * ('MemorySnapShot_Store' handles type)
 */
void STMemory_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(&STRamEnd, sizeof(STRamEnd));

	/* Only save/restore area of memory machine is set to, eg 1Mb */
	MemorySnapShot_Store(STRam, STRamEnd);

	/* And Cart/TOS/Hardware area */
	MemorySnapShot_Store(&RomMem[0xE00000], 0x200000);
}


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
	Uint8 nFalcSysCntrl;

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

	/* Fill in magic numbers to bypass TOS' memory tests for faster boot or
	 * if VDI resolution is enabled or if more than 4 MB of ram are used.
	 * (for highest compatibility, those tests should not be bypassed in
	 *  the common STF/STE cases as some programs like "Yolanda" rely on
	 *  the RAM content after those tests) */
	if (ConfigureParams.System.bFastBoot || bUseVDIRes
	    || (ConfigureParams.Memory.nMemorySize > 4 && !bIsEmuTOS))
	{
		/* Write magic values to sysvars to signal valid config */
		STMemory_WriteLong(0x420, 0x752019f3);    /* memvalid */
		STMemory_WriteLong(0x43a, 0x237698aa);    /* memval2 */
		STMemory_WriteLong(0x51a, 0x5555aaaa);    /* memval3 */
	}

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

	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
	{
		/* Set the Falcon memory and monitor configuration register:

		         $ffff8006.b [R]  76543210  Monitor-memory
		                          ||||||||
		                          |||||||+- RAM Wait Status
		                          |||||||   0 =  1 Wait (default)
		                          |||||||   1 =  0 Wait
		                          ||||||+-- Video Bus size ???
		                          ||||||    0 = 16 Bit
		                          ||||||    1 = 32 Bit (default)
		                          ||||++--- ROM Wait Status
		                          ||||      00 = Reserved
		                          ||||      01 =  2 Wait (default)
		                          ||||      10 =  1 Wait
		                          ||||      11 =  0 Wait
		                          ||++----- Falcon Memory
		                          ||        00 =  1 MB
		                          ||        01 =  4 MB
		                          ||        10 = 14 MB
		                          ||        11 = no boot !
		                          ++------- Monitor-Typ
		                                    00 - Monochrome (SM124)
		                                    01 - Color (SC1224)
		                                    10 - VGA Color
		                                    11 - Television

		Bit 1 seems not to be well documented. It's used by TOS at bootup to compute the memory size.
		After some tests, I get the following RAM values (Bits 5, 4, 1 are involved) :

		00 =  512 Ko	20 = 8192 Ko
		02 = 1024 Ko	22 = 14366 Ko
		10 = 2048 Ko	30 = Illegal
		12 = 4096 Ko	32 = Illegal

		I use these values for Hatari's emulation.
		I also set the bit 3 and 2 at value 01 are mentioned in the register description.
		*/

		if (ConfigureParams.Memory.nMemorySize == 14)     /* 14 Meg */
			nFalcSysCntrl = 0x26;
		else if (ConfigureParams.Memory.nMemorySize == 8) /* 8 Meg */
			nFalcSysCntrl = 0x24;
		else if (ConfigureParams.Memory.nMemorySize == 4) /* 4 Meg */
			nFalcSysCntrl = 0x16;
		else if (ConfigureParams.Memory.nMemorySize == 2) /* 2 Meg */
			nFalcSysCntrl = 0x14;
		else if (ConfigureParams.Memory.nMemorySize == 1) /* 1 Meg */
			nFalcSysCntrl = 0x06;
		else
			nFalcSysCntrl = 0x04;                     /* 512 Ko */

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

	/* Create connected drives mask (only for harddrives, don't change floppy drive detected by TOS) */
	ConnectedDriveMask = STMemory_ReadLong(0x4c2);  // Get initial drive mask (see what TOS thinks)
	if (GEMDOS_EMU_ON)
	{
		for (i = 0; i < MAX_HARDDRIVES; i++)
		{
			if (emudrives[i] != NULL)     // Is this GEMDOS drive enabled?
				ConnectedDriveMask |= (1 << emudrives[i]->drive_number);
		}
	}
	/* Set connected drives system variable.
	 * NOTE: some TOS images overwrite this value, see 'OpCode_SysInit', too */
	STMemory_WriteLong(0x4c2, ConnectedDriveMask);
}
