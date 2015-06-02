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
#include "m68000.h"

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

	if ( STMemory_CheckAreaType ( addr, len, ABFLAG_RAM ) )
	{
		memcpy(&STRam[addr], src, len);
		return true;
	}
	Log_Printf(LOG_WARN, "Invalid '%s' RAM range 0x%x+%i!\n", name, addr, len);

	for (end = addr + len; addr < end; addr++, src++)
	{
		if ( STMemory_CheckAreaType ( addr, 1, ABFLAG_RAM ) )
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
	int screensize, limit;
	int memtop, phystop;
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
	 * if VDI resolution is enabled or if more than 4 MB of ram are used
	 * or if TT RAM added in Falcon mode.
	 * (for highest compatibility, those tests should not be bypassed in
	 *  the common STF/STE cases as some programs like "Yolanda" rely on
	 *  the RAM content after those tests) */
	if ( ConfigureParams.System.bFastBoot
	  || bUseVDIRes
	  || ( ConfigureParams.Memory.nMemorySize > 4 && !bIsEmuTOS )
	  || ( ( ConfigureParams.System.nMachineType == MACHINE_FALCON ) && TTmemory ) )
	{
		/* Write magic values to sysvars to signal valid config */
		STMemory_WriteLong(0x420, 0x752019f3);    /* memvalid */
		STMemory_WriteLong(0x43a, 0x237698aa);    /* memval2 */
		STMemory_WriteLong(0x51a, 0x5555aaaa);    /* memval3 */

		/* If ST RAM detection is bypassed, we must also force TT RAM config if enabled */
		if ( TTmemory )
			STMemory_WriteLong ( 0x5a4 , 0x01000000 + TTmem_size );		/* ramtop */
		else
			STMemory_WriteLong ( 0x5a4 , 0 );		/* ramtop */
		STMemory_WriteLong ( 0x5a8 , 0x1357bd13 );		/* ramvalid */

		/* On Falcon, set bit6=1 at $ff8007 to simulate a warm start */
		/* (else memory detection is not skipped after a cold start/reset) */
		if ( ConfigureParams.System.nMachineType == MACHINE_FALCON )
			STMemory_WriteByte ( 0xff8007, IoMem_ReadByte(0xff8007) | 0x40 );

		/* On TT, set bit0=1 at $ff8e09 to simulate a warm start */
		/* (else memory detection is not skipped after a cold start/reset) */
		if ( ConfigureParams.System.nMachineType == MACHINE_TT )
			STMemory_WriteByte ( 0xff8e09, IoMem_ReadByte(0xff8e09) | 0x01 );
	}

	/* Set memory size, adjust for extra VDI screens if needed. */
	screensize = VDIWidth * VDIHeight / 8 * VDIPlanes;
	/* Use 32 kiB in normal screen mode or when the screen size is smaller than 32 kiB */
	if (!bUseVDIRes || screensize < 0x8000)
		screensize = 0x8000;
	/* mem top - upper end of user memory (right before the screen memory)
	 * memtop / phystop must be dividable by 512 or TOS crashes */
	memtop = (STRamEnd - screensize) & 0xfffffe00;
	/* phys top - 32k gap causes least issues with apps & TOS
	 * as that's the largest _common_ screen size. EmuTOS behavior
	 * depends on machine type.
	 *
	 * TODO: what to do about _native_ TT & Videl resolutions
	 * which size is >32k?  Should memtop be adapted also for
	 * those?
	 */
	switch (ConfigureParams.System.nMachineType)
	{
	case MACHINE_FALCON:
		/* TOS v4 doesn't work with VDI mode (yet), and
		 * EmuTOS works with correct gap, so use that */
		phystop = STRamEnd;
		break;
	case MACHINE_TT:
		/* For correct TOS v3 memory detection, phystop should be
		 * at the end of memory, not at memtop + 32k.
		 *
		 * However:
		 * - TOS v3 crashes/hangs if phystop-memtop gap is larger
		 *   than largest real HW screen size (150k)
		 * - NVDI hangs if gap is larger than 32k in any other than
		 *   monochrome mode
		 */
		if (VDIPlanes == 1)
			limit = 1280*960/8;
		else
			limit = 0x8000;
		if (screensize > limit)
		{
			phystop = memtop + limit;
			fprintf(stderr, "WARNING: too large VDI mode for TOS v3 memory detection to work correctly!\n");
		}
		else
			phystop = STRamEnd;
		break;
	default:
		phystop = memtop + 0x8000;
	}
	STMemory_WriteLong(0x436, memtop);
	STMemory_WriteLong(0x42e, phystop);
	if (bUseVDIRes)
		fprintf(stderr, "VDI mode memtop: 0x%x, phystop: 0x%x (screensize: %d kB, memtop->phystop: %d kB)\n",
			memtop, phystop, (screensize+511) / 1024, (phystop-memtop+511) / 1024);

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


/**
 * Check that the region of 'size' starting at 'addr' is entirely inside
 * a memory bank of the same memory type
 */
bool	STMemory_CheckAreaType ( Uint32 addr , int size , int mem_type )
{
	addrbank	*pBank;

	pBank = &get_mem_bank ( addr );

	if ( ( pBank->flags & mem_type ) == 0 )
	{
		fprintf(stderr, "pBank flags mismatch: 0x%x & 0x%x (RAM = 0x%x)\n", pBank->flags, mem_type, ABFLAG_RAM);
		return false;
	}

	return pBank->check ( addr , size );
}


/**
 * Convert an address in the ST memory space to a direct pointer
 * in the host memory.
 *
 * NOTE : Using this function to get a direct pointer to the memory should
 * only be used after doing a call to valid_address or STMemory_CheckAreaType
 * to ensure we don't try to access a non existing memory region.
 * Basically, this function should be used only for addr in RAM or in ROM
 */
void	*STMemory_STAddrToPointer ( Uint32 addr )
{
	Uint8	*p;

	if ( ConfigureParams.System.bAddressSpace24 == true )
		addr &= 0x00ffffff;			/* Only keep the 24 lowest bits */

	p = get_real_address ( addr );
	return (void *)p;
}



/**
 * Those functions are directly accessing the memory of the corresponding
 * bank, without calling its dedicated access handlers (they won't generate
 * bus errors or address errors or update IO values)
 * They are only used for internal work of the emulation, such as debugger,
 * log to print the content of memory, intercepting gemdos/bios calls, ...
 *
 * These functions are not used by the CPU emulation itself, see memory.c
 * for the functions that emulate real memory accesses.
 */

/**
 * Write long/word/byte into memory.
 * NOTE - value will be converted to 68000 endian
 */
void	STMemory_Write ( Uint32 addr , Uint32 val , int size )
{
	addrbank	*pBank;
	Uint8		*p;

//printf ( "mem direct write %x %x %d\n" , addr , val , size );
	pBank = &get_mem_bank ( addr );

	if ( pBank->baseaddr == NULL )
		return;					/* No real memory, do nothing */

	addr -= pBank->start & pBank->mask;
	addr &= pBank->mask;
	p = pBank->baseaddr + addr;

	/* We modify the memory, so we flush the data cache if needed */
	M68000_Flush_DCache ( addr , size );
	
	if ( size == 4 )
		do_put_mem_long ( p , val );
	else if ( size == 2 )
		do_put_mem_word ( p , (Uint16)val );
	else
		*p = (Uint8)val;
}

void	STMemory_WriteLong ( Uint32 addr , Uint32 val )
{
	STMemory_Write ( addr , val , 4 );
}

void	STMemory_WriteWord ( Uint32 addr , Uint16 val )
{
	STMemory_Write ( addr , (Uint32)val , 2 );
}

void	STMemory_WriteByte ( Uint32 addr , Uint8 val )
{
	STMemory_Write ( addr , (Uint32)val , 1 );
}


/**
 * Read long/word/byte from memory.
 * NOTE - value will be converted to 68000 endian
 */
Uint32	STMemory_Read ( Uint32 addr , int size )
{
	addrbank	*pBank;
	Uint8		*p;

//printf ( "mem direct read %x %d\n" , addr , size );
	pBank = &get_mem_bank ( addr );

	if ( pBank->baseaddr == NULL )
		return 0;				/* No real memory, return 0 */

	addr -= pBank->start & pBank->mask;
	addr &= pBank->mask;
	p = pBank->baseaddr + addr;
	
	if ( size == 4 )
		return do_get_mem_long ( p );
	else if ( size == 2 )
		return (Uint32)do_get_mem_word ( p );
	else
		return (Uint32)*p;
}

Uint32	STMemory_ReadLong ( Uint32 addr )
{
	return (Uint32) STMemory_Read ( addr , 4 );
}

Uint16	STMemory_ReadWord ( Uint32 addr )
{
	return (Uint16)STMemory_Read ( addr , 2 );
}

Uint8	STMemory_ReadByte ( Uint32 addr )
{
	return (Uint8)STMemory_Read ( addr , 1 );
}

