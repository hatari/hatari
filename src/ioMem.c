/*
  Hatari - ioMem.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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
char IoMem_rcsid[] = "Hatari $Id: ioMem.c,v 1.9 2005-10-20 07:52:19 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"
#include "uae-cpu/sysdeps.h"


#define IOMEM_DEBUG 0

#if IOMEM_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif



static void (*pInterceptReadTable[0x8000])(void);     /* Table with read access handlers */
static void (*pInterceptWriteTable[0x8000])(void);    /* Table with write access handlers */

int nIoMemAccessSize;                                 /* Set to 1, 2 or 4 according to byte, word or long word access */
Uint32 IoAccessBaseAddress;                           /* Stores the base address of the IO mem access */
Uint32 IoAccessCurrentAddress;                        /* Current byte address while handling WORD and LONG accesses */
static int nBusErrorAccesses;                         /* Needed to count bus error accesses */


/*-----------------------------------------------------------------------*/
/*
  Fill a region with bus error handlers.
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
/*
  Create 'intercept' tables for hardware address access. Each 'intercept
  table is a list of 0x8000 pointers to a list of functions to call when
  that location in the ST's memory is accessed. 
*/
void IoMem_Init(void)
{
	Uint32 addr;
	int i;
	INTERCEPT_ACCESS_FUNC *pInterceptAccessFuncs = NULL;

	/* Set default IO access handler (-> bus error) */
	IoMem_SetBusErrorRegion(0xff8000, 0xffffff);

	switch (ConfigureParams.System.nMachineType)
	{
		case MACHINE_ST:  pInterceptAccessFuncs = IoMemTable_ST; break;
		case MACHINE_STE: pInterceptAccessFuncs = IoMemTable_STE; break;
		case MACHINE_TT: pInterceptAccessFuncs = IoMemTable_TT; break;
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

	/* Disable blitter? */
	if (!ConfigureParams.System.bBlitter && ConfigureParams.System.nMachineType != MACHINE_STE)
	{
		IoMem_SetBusErrorRegion(0xff8a00, 0xff8a3f);
	}

	/* Disable real time clock? */
	if (!ConfigureParams.System.bRealTimeClock)
	{
		for (addr = 0xfffc21; addr  <= 0xfffc3f; addr++)
		{
			pInterceptReadTable[addr - 0xff8000] = IoMem_VoidRead;     /* For 'read' */
			pInterceptWriteTable[addr - 0xff8000] = IoMem_VoidWrite;   /* and 'write' */
		}
	
	}
}


/*-----------------------------------------------------------------------*/
/*
  Uninitialize the IoMem code (currently unused).
*/
void IoMem_UnInit(void)
{
}


/*-----------------------------------------------------------------------*/
/*
  Check if need to change our address as maybe a mirror register.
  Currently we only have a PSG mirror area.
*/
static Uint32 IoMem_CheckMirrorAddresses(Uint32 addr)
{
	if (addr>=0xff8800 && addr<0xff8900)    /* Is a PSG mirror registers? */
		addr = 0xff8800 + (addr & 3);       /* Bring into 0xff8800-0xff8804 range */

	return addr;
}



/*-----------------------------------------------------------------------*/
/*
  Handle byte read access from IO memory.
*/
uae_u32 IoMem_bget(uaecptr addr)
{
	Dprintf(("IoMem_bget($%x)\n", addr));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0xff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		return -1;
	}

	IoAccessBaseAddress = addr;                   /* Store access location */
	nIoMemAccessSize = SIZE_BYTE;
	nBusErrorAccesses = 0;
	addr = IoMem_CheckMirrorAddresses(addr);

	IoAccessCurrentAddress = addr;
	pInterceptReadTable[addr-0xff8000]();         /* Call handler */

	/* Check if we read from a bus-error region */
	if (nBusErrorAccesses == 1)
	{
		M68000_BusError(addr, 1);
		return -1;
	}

	return IoMem[addr];
}


/*-----------------------------------------------------------------------*/
/*
  Handle word read access from IO memory.
*/
uae_u32 IoMem_wget(uaecptr addr)
{
	Uint32 idx;

	Dprintf(("IoMem_wget($%x)\n", addr));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0xff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
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
	addr = IoMem_CheckMirrorAddresses(addr);
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
		M68000_BusError(addr, 1);
		return -1;
	}

	return IoMem_ReadWord(addr);
}


/*-----------------------------------------------------------------------*/
/*
  Handle long-word read access from IO memory.
*/
uae_u32 IoMem_lget(uaecptr addr)
{
	Uint32 idx;

	Dprintf(("IoMem_lget($%x)\n", addr));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0xff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
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
	addr = IoMem_CheckMirrorAddresses(addr);
	idx = addr - 0xff8000;

	IoAccessCurrentAddress = addr;
	pInterceptReadTable[idx]();                   /* Call 1st handler */

	if (pInterceptReadTable[idx+1] != pInterceptReadTable[idx])
	{
		IoAccessCurrentAddress = addr + 1;
		pInterceptReadTable[idx+1]();             /* Call 2nd handler */
	}

	if (pInterceptReadTable[idx+2] != pInterceptReadTable[idx+1])
	{
		IoAccessCurrentAddress = addr + 2;
		pInterceptReadTable[idx+2]();             /* Call 3rd handler */
	}

	if (pInterceptReadTable[idx+3] != pInterceptReadTable[idx+2])
	{
		IoAccessCurrentAddress = addr + 3;
		pInterceptReadTable[idx+3]();             /* Call 4th handler */
	}

	/* Check if we completely read from a bus-error region */
	if (nBusErrorAccesses == 4)
	{
		M68000_BusError(addr, 1);
		return -1;
	}

	return IoMem_ReadLong(addr);
}


/*-----------------------------------------------------------------------*/
/*
  Handle byte write access to IO memory.
*/
void IoMem_bput(uaecptr addr, uae_u32 val)
{
	Dprintf(("IoMem_bput($%x, $%x)\n", addr, val));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0xff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
		return;
	}

	IoAccessBaseAddress = addr;                   /* Store for exception frame, just in case */
	nIoMemAccessSize = SIZE_BYTE;
	nBusErrorAccesses = 0;
	addr = IoMem_CheckMirrorAddresses(addr);

	IoMem[addr] = val;

	IoAccessCurrentAddress = addr;
	pInterceptWriteTable[addr-0xff8000]();        /* Call handler */

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == 1)
	{
		M68000_BusError(addr, 0);
	}
}


/*-----------------------------------------------------------------------*/
/*
  Handle word write access to IO memory.
*/
void IoMem_wput(uaecptr addr, uae_u32 val)
{
	Uint32 idx;

	Dprintf(("IoMem_wput($%x, $%x)\n", addr, val));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
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
	addr = IoMem_CheckMirrorAddresses(addr);

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
		M68000_BusError(addr, 0);
	}
}


/*-----------------------------------------------------------------------*/
/*
  Handle long-word write access to IO memory.
*/
void IoMem_lput(uaecptr addr, uae_u32 val)
{
	Uint32 idx;

	Dprintf(("IoMem_lput($%x, $%x)\n", addr, val));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0xff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
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
	addr = IoMem_CheckMirrorAddresses(addr);

	IoMem_WriteLong(addr, val);
	idx = addr - 0xff8000;

	IoAccessCurrentAddress = addr;
	pInterceptWriteTable[idx]();                  /* Call handler */

	if (pInterceptWriteTable[idx+1] != pInterceptWriteTable[idx])
	{
		IoAccessCurrentAddress = addr + 1;
		pInterceptWriteTable[idx+1]();            /* Call 2nd handler */
	}

	if (pInterceptWriteTable[idx+2] != pInterceptWriteTable[idx+1])
	{
		IoAccessCurrentAddress = addr + 2;
		pInterceptWriteTable[idx+2]();            /* Call 3rd handler */
	}

	if (pInterceptWriteTable[idx+3] != pInterceptWriteTable[idx+2])
	{
		IoAccessCurrentAddress = addr + 3;
		pInterceptWriteTable[idx+3]();            /* Call 4th handler */
	}

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == 4)
	{
		M68000_BusError(addr, 0);
	}
}


/*-------------------------------------------------------------------------*/
/*
  This handler will be called if a ST program tries to read from an address
  that causes a bus error on a real ST. However, we can't call M68000_BusError()
  directly: For example, a "move.b $ff8204,d0" triggers a bus error on a real ST,
  while a "move.w $ff8204,d0" works! So we have to count the accesses to bus error
  addresses and we only trigger a bus error later if the count matches the complete
  access size (e.g. nBusErrorAccesses==4 for a long word access).
*/
void IoMem_BusErrorEvenReadAccess(void)
{
	nBusErrorAccesses += 1;
	IoMem[IoAccessCurrentAddress] = 0xff;
}

/*
  We need two handler so that the IoMem_*get functions can distinguish
  consecutive addresses.
*/
void IoMem_BusErrorOddReadAccess(void)
{
	nBusErrorAccesses += 1;
	IoMem[IoAccessCurrentAddress] = 0xff;
}

/*-------------------------------------------------------------------------*/
/*
  Same as IoMem_BusErrorReadAccess() but for write access this time.
*/
void IoMem_BusErrorEvenWriteAccess(void)
{
	nBusErrorAccesses += 1;
}

/*
  We need two handler so that the IoMem_*put functions can distinguish
  consecutive addresses.
*/
void IoMem_BusErrorOddWriteAccess(void)
{
	nBusErrorAccesses += 1;
}


/*-------------------------------------------------------------------------*/
/*
  This is the read handler for the IO memory locations without an assigned
  IO register and which also do not generate a bus error. Reading from such
  a register will return the result 0xff.
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
/*
  This is the write handler for the IO memory locations without an assigned
  IO register and which also do not generate a bus error. We simply ignore
  a write access to these registers.
*/
void IoMem_VoidWrite(void)
{
	/* Nothing... */
}


/*-------------------------------------------------------------------------*/
/*
  A dummy function that does nothing at all - for memory regions that don't
  need a special handler for read access.
*/
void IoMem_ReadWithoutInterception(void)
{
	/* Nothing... */
}

/*-------------------------------------------------------------------------*/
/*
  A dummy function that does nothing at all - for memory regions that don't
  need a special handler for write access.
*/
void IoMem_WriteWithoutInterception(void)
{
	/* Nothing... */
}
