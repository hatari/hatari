/*
  Hatari - ioMem.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This is where we intercept read/writes to/from the hardware. The ST's memory
  is nicely split into four main parts - the bottom area of RAM is for user
  programs. This is followed by a large area which causes a Bus Error. After
  this is the ROM addresses for TOS and finally an area for hardware mapping.
  To gain speed any address in the user area can simply read/write, but anything
  above this range needs to be checked for validity and sent to the various
  handlers.
  A big problem for ST emulation is the use of the hardware registers. These
  often consist of an 'odd' byte in memory and is usually addressed as a single
  byte. A number of applications, however, write to the address using a word or
  even long word. So we have a list of handlers that take care of each address
  that has to be intercepted. Eg, a long write to a PSG register (which access
  two registers) will write the long into IO memory space and then call the two
  handlers which read off the bytes for each register.
  This means that any access to any hardware register in such a way will work
  correctly - it certainly fixes a lot of bugs and means writing just one
  routine for each hardware register we mean to intercept! Phew!
  You have also to take into consideration that some hardware registers are
  bigger than 1 byte (there are also word and longword registers) and that
  a lot of addresses in between can cause a bus error - so it's not so easy
  to cope with all type of handlers in a straight forward way.
  Also note the 'mirror' (or shadow) registers of the PSG - this is used by most
  games.
*/
const char IoMem_fileid[] = "Hatari ioMem.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "memorySnapShot.h"
#include "m68000.h"
#include "sysdeps.h"
#include "newcpu.h"


static void (*pInterceptReadTable[0x8000])(void);     /* Table with read access handlers */
static void (*pInterceptWriteTable[0x8000])(void);    /* Table with write access handlers */

int nIoMemAccessSize;                                 /* Set to 1, 2 or 4 according to byte, word or long word access */
Uint32 IoAccessBaseAddress;                           /* Stores the base address of the IO mem access */
Uint32 IoAccessCurrentAddress;                        /* Current byte address while handling WORD and LONG accesses */
static int nBusErrorAccesses;                         /* Needed to count bus error accesses */


/*
  Heuristics for better cycle accuracy when "cycle exact mode" is not used

  Some instructions can do several IO accesses that will be seen as several independent accesses,
  instead of one whole word or long word access as in the size of the instruction.
  For example :
    - movep.w and move.l will do 2 or 4 BYTE accesses (and not 1 WORD or LONG WORD access)
    - move.l will do 2 WORD accesses (and not 1 LONG WORD, because ST's bus is 16 bit)

  So, when a BYTE access is made, we need to know if it comes from an instruction where size=byte
  or if it comes from a word or long word instruction.

  In order to emulate correct read/write cycles when IO regs are accessed this way, we need to
  keep track of how many accesses were made by the same instruction.
  This will be used when CPU runs in "prefetch mode" and we try to approximate internal cycles
  (see cycles.c for heuristics using this).

  When CPU runs in "cycle exact mode", this is not used because the internal cycles will be computed
  precisely at the CPU emulation level.
*/
static Uint64	IoAccessInstrPrevClock;
int		IoAccessInstrCount;			/* Number of the accesses made in the current instruction (1..4) */
							/* 0 means no multiple accesses in the current instruction */


/* Falcon bus mode (Falcon STe compatible bus or Falcon only bus) */
static enum {
	STE_BUS_COMPATIBLE,
	FALCON_ONLY_BUS
} falconBusMode;

/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void IoMem_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&falconBusMode, sizeof(falconBusMode));
}

/*-----------------------------------------------------------------------*/
/**
 * Fill a region with bus error handlers.
 */
static void IoMem_SetBusErrorRegion(Uint32 startaddr, Uint32 endaddr)
{
	Uint32 a;

	for (a = startaddr; a <= endaddr; a++)
	{
		if (a & 1)
		{
			pInterceptReadTable[a - 0xff8000] = IoMem_BusErrorOddReadAccess;     /* For 'read' */
			pInterceptWriteTable[a - 0xff8000] = IoMem_BusErrorOddWriteAccess;   /* and 'write' */
		}
		else
		{
			pInterceptReadTable[a - 0xff8000] = IoMem_BusErrorEvenReadAccess;    /* For 'read' */
			pInterceptWriteTable[a - 0xff8000] = IoMem_BusErrorEvenWriteAccess;  /* and 'write' */
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Create 'intercept' tables for hardware address access. Each 'intercept
 * table is a list of 0x8000 pointers to a list of functions to call when
 * that location in the ST's memory is accessed. 
 */
void IoMem_Init(void)
{
	Uint32 addr;
	int i;
	const INTERCEPT_ACCESS_FUNC *pInterceptAccessFuncs = NULL;

	/* Set default IO access handler (-> bus error) */
	IoMem_SetBusErrorRegion(0xff8000, 0xffffff);


	/* Initialize STe bus specific registers for Falcon in FALCON STe compatible bus mode */
	if ((ConfigureParams.System.nMachineType == MACHINE_FALCON) && (falconBusMode == STE_BUS_COMPATIBLE)) {
		for (addr = 0xff8000; addr < 0xffd426; addr++)
		{
			if ( ((addr >= 0xff8002) && (addr < 0xff8006)) ||
				 ((addr >= 0xff8008) && (addr < 0xff800c)) ||
				 ((addr >= 0xff800e) && (addr < 0xff8060)) ||
				 ((addr >= 0xff8064) && (addr < 0xff8200)) ||
				 ((addr >= 0xff82c4) && (addr < 0xff8400)) ||
				  (addr == 0xff8560)                       ||
				  (addr == 0xff8564)                       ||
				 ((addr >= 0xff8804) && (addr < 0xff8900)) ||
				 ((addr >= 0xff8964) && (addr < 0xff8970)) ||
				 ((addr >= 0xff8c00) && (addr < 0xff8c80)) ||
				 ((addr >= 0xff8c88) && (addr < 0xff8d00)) ||
				 ((addr >= 0xff9000) && (addr < 0xff9200)) ||
				 ((addr >= 0xff9204) && (addr < 0xff9206)) ||
				 ((addr >= 0xff9207) && (addr < 0xff9210)) ||
				 ((addr >= 0xff9218) && (addr < 0xff9220)) ||
				 ((addr >= 0xff9224) && (addr < 0xff9800)) ||
				 ((addr >= 0xff9c00) && (addr < 0xffa000)) ||
				 ((addr >= 0xffa200) && (addr < 0xffa208)) ||
				  (addr == 0xffc020)                       ||
				  (addr == 0xffc021)                       ||
				  (addr == 0xffd020)                       ||
				  (addr == 0xffd021)                       ||
				  (addr == 0xffd420)                       ||
				  (addr == 0xffd421)                       ||
				  (addr == 0xffd425)
			  ) {
					pInterceptReadTable[addr - 0xff8000] = IoMem_VoidRead;      /* For 'read' */
					pInterceptWriteTable[addr - 0xff8000] = IoMem_VoidWrite;    /* and 'write' */
 			    }
		}
	}

	switch (ConfigureParams.System.nMachineType)
	{
		case MACHINE_ST:  pInterceptAccessFuncs = IoMemTable_ST; break;
		case MACHINE_STE: pInterceptAccessFuncs = IoMemTable_STE; break;
		case MACHINE_TT: pInterceptAccessFuncs = IoMemTable_TT; break;
		case MACHINE_FALCON: pInterceptAccessFuncs = IoMemTable_Falcon; break;
		default: abort(); /* bug */
	}

	/* Now set the correct handlers */
	for (addr=0xff8000; addr <= 0xffffff; addr++)
	{
		/* Does this hardware location/span appear in our list of possible intercepted functions? */
		for (i=0; pInterceptAccessFuncs[i].Address != 0; i++)
		{
			if (addr >= pInterceptAccessFuncs[i].Address
			    && addr < pInterceptAccessFuncs[i].Address+pInterceptAccessFuncs[i].SpanInBytes)
			{
				/* Security checks... */
				if (pInterceptReadTable[addr-0xff8000] != IoMem_BusErrorEvenReadAccess && pInterceptReadTable[addr-0xff8000] != IoMem_BusErrorOddReadAccess)
					fprintf(stderr, "IoMem_Init: Warning: $%x (R) already defined\n", addr);
				if (pInterceptWriteTable[addr-0xff8000] != IoMem_BusErrorEvenWriteAccess && pInterceptWriteTable[addr-0xff8000] != IoMem_BusErrorOddWriteAccess)
					fprintf(stderr, "IoMem_Init: Warning: $%x (W) already defined\n", addr);

				/* This location needs to be intercepted, so add entry to list */
				pInterceptReadTable[addr-0xff8000] = pInterceptAccessFuncs[i].ReadFunc;
				pInterceptWriteTable[addr-0xff8000] = pInterceptAccessFuncs[i].WriteFunc;
			}
		}
	}

	/* Set registers for Falcon DSP emulation */
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
	{
		switch (ConfigureParams.System.nDSPType)
		{
#if ENABLE_DSP_EMU
		case DSP_TYPE_EMU:
			IoMemTabFalcon_DSPemulation(pInterceptReadTable,
						    pInterceptWriteTable);
			break;
#endif
		case DSP_TYPE_DUMMY:
			IoMemTabFalcon_DSPdummy(pInterceptReadTable,
						pInterceptWriteTable);
			break;
		default:
			/* none */
			IoMemTabFalcon_DSPnone(pInterceptReadTable,
					       pInterceptWriteTable);
		}
	}

	/* Disable blitter? */
	if (!ConfigureParams.System.bBlitter && ConfigureParams.System.nMachineType == MACHINE_ST)
	{
		IoMem_SetBusErrorRegion(0xff8a00, 0xff8a3f);
	}

	/* Disable real time clock? */
	if (!ConfigureParams.System.bRealTimeClock)
	{
		for (addr = 0xfffc21; addr <= 0xfffc3f; addr++)
		{
			pInterceptReadTable[addr - 0xff8000] = IoMem_VoidRead;     /* For 'read' */
			pInterceptWriteTable[addr - 0xff8000] = IoMem_VoidWrite;   /* and 'write' */
		}
	}

	/* Initialize PSG shadow registers for ST, STe, TT machines */
	if (ConfigureParams.System.nMachineType != MACHINE_FALCON)
	{
		for (addr = 0xff8804; addr < 0xff8900; addr++)
		{
			pInterceptReadTable[addr - 0xff8000] = pInterceptReadTable[(addr & 0xfff803) - 0xff8000];
			pInterceptWriteTable[addr - 0xff8000] = pInterceptWriteTable[(addr & 0xfff803) - 0xff8000];
		}
	}
	else {
		/* Initialize PSG shadow registers for Falcon machine when in STe bus compatibility mode */
		if (falconBusMode == STE_BUS_COMPATIBLE) {
			for (addr = 0xff8804; addr < 0xff8900; addr++)
			{
				pInterceptReadTable[addr - 0xff8000] = IoMem_VoidRead;     /* For 'read' */
				pInterceptWriteTable[addr - 0xff8000] = IoMem_VoidWrite;   /* and 'write' */
			}

		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * This function is called to fix falconBusMode (0 = Falcon STe bus compatibility, other value = Falcon only bus compatibility)
 * This value comes from register $ff8007.b (Bit 5) and is called by ioMemTabFalcon
 */
void IoMem_Init_FalconInSTeBuscompatibilityMode(Uint8 value)
{
	if (value == 0)
		falconBusMode = STE_BUS_COMPATIBLE;
	else
		falconBusMode = FALCON_ONLY_BUS;

	IoMem_Init();
}

/*-----------------------------------------------------------------------*/
/**
 * Uninitialize the IoMem code (currently unused).
 */
void IoMem_UnInit(void)
{
}


/*-----------------------------------------------------------------------*/
/**
 * Handle byte read access from IO memory.
 */
uae_u32 IoMem_bget(uaecptr addr)
{
	Uint8 val;

	/* Check if access is made by a new instruction or by the same instruction doing multiple byte accesses */
	if ( IoAccessInstrPrevClock == CyclesGlobalClockCounter )
		IoAccessInstrCount++;			/* Same instruction, increase access count */
	else
	{
		IoAccessInstrPrevClock = CyclesGlobalClockCounter;
		if ( table68k[ M68000_CurrentOpcode ].size == 0 )
			IoAccessInstrCount = 0;		/* Instruction size is byte : no multiple accesses */
		else
			IoAccessInstrCount = 1;		/* 1st access */
	}

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0xff8000 || !regs.s)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
		return -1;
	}

	IoAccessBaseAddress = addr;                   /* Store access location */
	nIoMemAccessSize = SIZE_BYTE;
	nBusErrorAccesses = 0;

	IoAccessCurrentAddress = addr;
	pInterceptReadTable[addr-0xff8000]();         /* Call handler */

	/* Check if we read from a bus-error region */
	if (nBusErrorAccesses == 1)
	{
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
		return -1;
	}

	val = IoMem[addr];

	LOG_TRACE(TRACE_IOMEM_RD, "IO read.b $%06x = $%02x pc=%x\n", addr, val, M68000_GetPC());

	return val;
}


/*-----------------------------------------------------------------------*/
/**
 * Handle word read access from IO memory.
 */
uae_u32 IoMem_wget(uaecptr addr)
{
	Uint32 idx;
	Uint16 val;

	/* Check if access is made by a new instruction or by the same instruction doing multiple word accesses */
	if ( IoAccessInstrPrevClock == CyclesGlobalClockCounter )
		IoAccessInstrCount++;			/* Same instruction, increase access count */
	else
	{
		IoAccessInstrPrevClock = CyclesGlobalClockCounter;
		if ( ( table68k[ M68000_CurrentOpcode ].size == 1 )
		  && ( OpcodeFamily != i_MVMEL ) && ( OpcodeFamily != i_MVMLE ) )
			IoAccessInstrCount = 0;		/* Instruction size is word and not a movem : no multiple accesses */
		else
			IoAccessInstrCount = 1;		/* 1st access of a long or movem.w */
	}

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0xff8000 || !regs.s)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA);
		return -1;
	}
	if (addr > 0xfffffe)
	{
		fprintf(stderr, "Illegal IO memory access: IoMem_wget($%x)\n", addr);
		return -1;
	}

	IoAccessBaseAddress = addr;                   /* Store for exception frame */
	nIoMemAccessSize = SIZE_WORD;
	nBusErrorAccesses = 0;
	idx = addr - 0xff8000;

	IoAccessCurrentAddress = addr;
	pInterceptReadTable[idx]();                   /* Call 1st handler */

	if (pInterceptReadTable[idx+1] != pInterceptReadTable[idx])
	{
		IoAccessCurrentAddress = addr + 1;
		pInterceptReadTable[idx+1]();             /* Call 2nd handler */
	}

	/* Check if we completely read from a bus-error region */
	if (nBusErrorAccesses == 2)
	{
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA);
		return -1;
	}

	val = IoMem_ReadWord(addr);

	LOG_TRACE(TRACE_IOMEM_RD, "IO read.w $%06x = $%04x pc=%x\n", addr, val, M68000_GetPC());

	return val;
}


/*-----------------------------------------------------------------------*/
/**
 * Handle long-word read access from IO memory.
 */
uae_u32 IoMem_lget(uaecptr addr)
{
	Uint32 idx;
	Uint32 val;
	int n;

	/* Check if access is made by a new instruction or by the same instruction doing multiple long accesses */
	if ( IoAccessInstrPrevClock == CyclesGlobalClockCounter )
		IoAccessInstrCount++;			/* Same instruction, increase access count */
	else
	{
		IoAccessInstrPrevClock = CyclesGlobalClockCounter;
		if ( ( OpcodeFamily != i_MVMEL ) && ( OpcodeFamily != i_MVMLE ) )
			IoAccessInstrCount = 0;		/* Instruction is not a movem : no multiple accesses */
		else
			IoAccessInstrCount = 1;		/* 1st access of a movem.l */
	}

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0xff8000 || !regs.s)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA);
		return -1;
	}
	if (addr > 0xfffffc)
	{
		fprintf(stderr, "Illegal IO memory access: IoMem_lget($%x)\n", addr);
		return -1;
	}

	IoAccessBaseAddress = addr;                   /* Store for exception frame */
	nIoMemAccessSize = SIZE_LONG;
	nBusErrorAccesses = 0;
	idx = addr - 0xff8000;

	IoAccessCurrentAddress = addr;
	pInterceptReadTable[idx]();                   /* Call 1st handler */

	for (n = 1; n < nIoMemAccessSize; n++)
	{
		if (pInterceptReadTable[idx+n] != pInterceptReadTable[idx+n-1])
		{
			IoAccessCurrentAddress = addr + n;
			pInterceptReadTable[idx+n]();     /* Call n-th handler */
		}
	}

	/* Check if we completely read from a bus-error region */
	if (nBusErrorAccesses == 4)
	{
		M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA);
		return -1;
	}

	val = IoMem_ReadLong(addr);

	LOG_TRACE(TRACE_IOMEM_RD, "IO read.l $%06x = $%08x pc=%x\n", addr, val, M68000_GetPC());

	return val;
}


/*-----------------------------------------------------------------------*/
/**
 * Handle byte write access to IO memory.
 */
void IoMem_bput(uaecptr addr, uae_u32 val)
{
	/* Check if access is made by a new instruction or by the same instruction doing multiple byte accesses */
	if ( IoAccessInstrPrevClock == CyclesGlobalClockCounter )
		IoAccessInstrCount++;			/* Same instruction, increase access count */
	else
	{
		IoAccessInstrPrevClock = CyclesGlobalClockCounter;
		if ( table68k[ M68000_CurrentOpcode ].size == 0 )
			IoAccessInstrCount = 0;		/* Instruction size is byte : no multiple accesses */
		else
			IoAccessInstrCount = 1;		/* 1st access */
	}

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	LOG_TRACE(TRACE_IOMEM_WR, "IO write.b $%06x = $%02x pc=%x\n", addr, val&0xff, M68000_GetPC());

	if (addr < 0xff8000 || !regs.s)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
		return;
	}

	IoAccessBaseAddress = addr;                   /* Store for exception frame, just in case */
	nIoMemAccessSize = SIZE_BYTE;
	nBusErrorAccesses = 0;

	IoMem[addr] = val;

	IoAccessCurrentAddress = addr;
	pInterceptWriteTable[addr-0xff8000]();        /* Call handler */

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == 1)
	{
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle word write access to IO memory.
 */
void IoMem_wput(uaecptr addr, uae_u32 val)
{
	Uint32 idx;

	/* Check if access is made by a new instruction or by the same instruction doing multiple word accesses */
	if ( IoAccessInstrPrevClock == CyclesGlobalClockCounter )
		IoAccessInstrCount++;			/* Same instruction, increase access count */
	else
	{
		IoAccessInstrPrevClock = CyclesGlobalClockCounter;
		if ( ( table68k[ M68000_CurrentOpcode ].size == 1 )
		  && ( OpcodeFamily != i_MVMEL ) && ( OpcodeFamily != i_MVMLE ) )
			IoAccessInstrCount = 0;		/* Instruction size is word and not a movem : no multiple accesses */
		else
			IoAccessInstrCount = 1;		/* 1st access of a long or movem.w */
	}

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	LOG_TRACE(TRACE_IOMEM_WR, "IO write.w $%06x = $%04x pc=%x\n", addr, val&0xffff, M68000_GetPC());

	if (addr < 0x00ff8000 || !regs.s)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA);
		return;
	}
	if (addr > 0xfffffe)
	{
		fprintf(stderr, "Illegal IO memory access: IoMem_wput($%x)\n", addr);
		return;
	}

	IoAccessBaseAddress = addr;                   /* Store for exception frame, just in case */
	nIoMemAccessSize = SIZE_WORD;
	nBusErrorAccesses = 0;

	IoMem_WriteWord(addr, val);
	idx = addr - 0xff8000;

	IoAccessCurrentAddress = addr;
	pInterceptWriteTable[idx]();                  /* Call 1st handler */

	if (pInterceptWriteTable[idx+1] != pInterceptWriteTable[idx])
	{
		IoAccessCurrentAddress = addr + 1;
		pInterceptWriteTable[idx+1]();            /* Call 2nd handler */
	}

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == 2)
	{
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle long-word write access to IO memory.
 */
void IoMem_lput(uaecptr addr, uae_u32 val)
{
	Uint32 idx;
	int n;

	/* Check if access is made by a new instruction or by the same instruction doing multiple long accesses */
	if ( IoAccessInstrPrevClock == CyclesGlobalClockCounter )
		IoAccessInstrCount++;			/* Same instruction, increase access count */
	else
	{
		IoAccessInstrPrevClock = CyclesGlobalClockCounter;
		if ( ( OpcodeFamily != i_MVMEL ) && ( OpcodeFamily != i_MVMLE ) )
			IoAccessInstrCount = 0;		/* Instruction is not a movem : no multiple accesses */
		else
			IoAccessInstrCount = 1;		/* 1st access of a movem.l */
	}

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	LOG_TRACE(TRACE_IOMEM_WR, "IO write.l $%06x = $%08x pc=%x\n", addr, val, M68000_GetPC());

	if (addr < 0xff8000 || !regs.s)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA);
		return;
	}
	if (addr > 0xfffffc)
	{
		fprintf(stderr, "Illegal IO memory access: IoMem_lput($%x)\n", addr);
		return;
	}

	IoAccessBaseAddress = addr;                   /* Store for exception frame, just in case */
	nIoMemAccessSize = SIZE_LONG;
	nBusErrorAccesses = 0;

	IoMem_WriteLong(addr, val);
	idx = addr - 0xff8000;

	IoAccessCurrentAddress = addr;
	pInterceptWriteTable[idx]();                  /* Call first handler */

	for (n = 1; n < nIoMemAccessSize; n++)
	{
		if (pInterceptWriteTable[idx+n] != pInterceptWriteTable[idx+n-1])
		{
			IoAccessCurrentAddress = addr + n;
			pInterceptWriteTable[idx+n]();   /* Call n-th handler */
		}
	}

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == 4)
	{
		M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA);
	}
}


/*-------------------------------------------------------------------------*/
/**
 * This handler will be called if a ST program tries to read from an address
 * that causes a bus error on a real ST. However, we can't call M68000_BusError()
 * directly: For example, a "move.b $ff8204,d0" triggers a bus error on a real ST,
 * while a "move.w $ff8204,d0" works! So we have to count the accesses to bus error
 * addresses and we only trigger a bus error later if the count matches the complete
 * access size (e.g. nBusErrorAccesses==4 for a long word access).
 */
void IoMem_BusErrorEvenReadAccess(void)
{
	nBusErrorAccesses += 1;
	IoMem[IoAccessCurrentAddress] = 0xff;
}

/**
 * We need two handler so that the IoMem_*get functions can distinguish
 * consecutive addresses.
 */
void IoMem_BusErrorOddReadAccess(void)
{
	nBusErrorAccesses += 1;
	IoMem[IoAccessCurrentAddress] = 0xff;
}

/*-------------------------------------------------------------------------*/
/**
 * Same as IoMem_BusErrorReadAccess() but for write access this time.
 */
void IoMem_BusErrorEvenWriteAccess(void)
{
	nBusErrorAccesses += 1;
}

/**
 * We need two handler so that the IoMem_*put functions can distinguish
 * consecutive addresses.
 */
void IoMem_BusErrorOddWriteAccess(void)
{
	nBusErrorAccesses += 1;
}


/*-------------------------------------------------------------------------*/
/**
 * This is the read handler for the IO memory locations without an assigned
 * IO register and which also do not generate a bus error. Reading from such
 * a register will return the result 0xff.
 */
void IoMem_VoidRead(void)
{
	Uint32 a;

	/* handler is probably called only once, so we have to take care of the neighbour "void IO registers" */
	for (a = IoAccessBaseAddress; a < IoAccessBaseAddress + nIoMemAccessSize; a++)
	{
		if (pInterceptReadTable[a - 0xff8000] == IoMem_VoidRead)
		{
			IoMem[a] = 0xff;
		}
	}
}

/*-------------------------------------------------------------------------*/
/**
 * This is the same function as IoMem_VoidRead, but for IO registers that
 * return 0x00 instead of 0xff when read (this is the case for some video
 * registers on STE, Falcon, ...)
 */
void IoMem_VoidRead_00(void)
{
	Uint32 a;

	/* handler is probably called only once, so we have to take care of the neighbour "void IO registers" */
	for (a = IoAccessBaseAddress; a < IoAccessBaseAddress + nIoMemAccessSize; a++)
	{
		if (pInterceptReadTable[a - 0xff8000] == IoMem_VoidRead_00)
		{
			IoMem[a] = 0x00;
		}
	}
}

/*-------------------------------------------------------------------------*/
/**
 * This is the write handler for the IO memory locations without an assigned
 * IO register and which also do not generate a bus error. We simply ignore
 * a write access to these registers.
 */
void IoMem_VoidWrite(void)
{
	/* Nothing... */
}


/*-------------------------------------------------------------------------*/
/**
 * A dummy function that does nothing at all - for memory regions that don't
 * need a special handler for read access.
 */
void IoMem_ReadWithoutInterception(void)
{
	/* Nothing... */
}

/*-------------------------------------------------------------------------*/
/**
 * A dummy function that does nothing at all - for memory regions that don't
 * need a special handler for write access.
 */
void IoMem_WriteWithoutInterception(void)
{
	/* Nothing... */
}
