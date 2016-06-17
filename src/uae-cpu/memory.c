 /*
  * UAE - The Un*x Amiga Emulator - CPU core
  *
  * Memory management
  *
  * (c) 1995 Bernd Schmidt
  *
  * Adaptation to Hatari by Thomas Huth
  *
  * This file is distributed under the GNU General Public License, version 2
  * or at your option any later version. Read the file gpl.txt for details.
  */
const char Memory_fileid[] = "Hatari memory.c : " __DATE__ " " __TIME__;

#include <SDL.h>
#include "config.h"
#include "sysdeps.h"
#include "hatari-glue.h"
#include "maccess.h"
#include "memory.h"

#include "main.h"
#include "tos.h"
#include "ide.h"
#include "ioMem.h"
#include "reset.h"
#include "stMemory.h"
#include "m68000.h"
#include "configuration.h"

#include "newcpu.h"


/* Set illegal_mem to 1 for debug output: */
#define illegal_mem 1

static int illegal_count = 50;

static uae_u32 STmem_size;
uae_u32 TTmem_size = 0;
static uae_u32 TTmem_mask;

#define STmem_start  0x00000000
#define ROMmem_start 0x00E00000
#define IdeMem_start 0x00F00000
#define IOmem_start  0x00FF0000
#define TTmem_start  0x01000000			/* TOS 3 and TOS 4 always expect extra RAM at this address */
#define TTmem_end    0x80000000			/* Max value for end of TT ram, which gives 2047 MB */

#define IdeMem_size  65536
#define IOmem_size  65536
#define ROMmem_size (0x00FF0000 - 0x00E00000)  /* So we cover both possible ROM regions + cartridge */

#define STmem_mask  0x00ffffff
#define ROMmem_mask 0x00ffffff
#define IdeMem_mask  (IdeMem_size - 1)
#define IOmem_mask  (IOmem_size - 1)


#ifdef SAVE_MEMORY_BANKS
addrbank *mem_banks[65536];
#else
addrbank mem_banks[65536];
#endif

#ifdef NO_INLINE_MEMORY_ACCESS
__inline__ uae_u32 longget (uaecptr addr)
{
    return call_mem_get_func (get_mem_bank (addr).lget, addr);
}
__inline__ uae_u32 wordget (uaecptr addr)
{
    return call_mem_get_func (get_mem_bank (addr).wget, addr);
}
__inline__ uae_u32 byteget (uaecptr addr)
{
    return call_mem_get_func (get_mem_bank (addr).bget, addr);
}
__inline__ void longput (uaecptr addr, uae_u32 l)
{
    call_mem_put_func (get_mem_bank (addr).lput, addr, l);
}
__inline__ void wordput (uaecptr addr, uae_u32 w)
{
    call_mem_put_func (get_mem_bank (addr).wput, addr, w);
}
__inline__ void byteput (uaecptr addr, uae_u32 b)
{
    call_mem_put_func (get_mem_bank (addr).bput, addr, b);
}
#endif


/* Some prototypes: */
static int STmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *STmem_xlate (uaecptr addr) REGPARAM;


static void print_illegal_counted(const char *txt, uaecptr addr)
{
    if (!illegal_mem || illegal_count <= 0)
        return;

    write_log("%s at %08lx\n", txt, (long)addr);
    if (--illegal_count == 0)
        write_log("Suppressing further messages about illegal memory accesses.\n");
}


/* A dummy bank that only contains zeros */

static uae_u32 dummy_lget(uaecptr addr)
{
    if (illegal_mem)
	write_log ("Illegal lget at %08lx\n", (long)addr);

    return 0;
}

static uae_u32 dummy_wget(uaecptr addr)
{
    if (illegal_mem)
	write_log ("Illegal wget at %08lx\n", (long)addr);

    return 0;
}

static uae_u32 dummy_bget(uaecptr addr)
{
    if (illegal_mem)
	write_log ("Illegal bget at %08lx\n", (long)addr);

    return 0;
}

static void dummy_lput(uaecptr addr, uae_u32 l)
{
    if (illegal_mem)
	write_log ("Illegal lput at %08lx\n", (long)addr);
}

static void dummy_wput(uaecptr addr, uae_u32 w)
{
    if (illegal_mem)
	write_log ("Illegal wput at %08lx\n", (long)addr);
}

static void dummy_bput(uaecptr addr, uae_u32 b)
{
    if (illegal_mem)
	write_log ("Illegal bput at %08lx\n", (long)addr);
}

static int dummy_check(uaecptr addr, uae_u32 size)
{
    return 0;
}

static uae_u8 *dummy_xlate(uaecptr addr)
{
    write_log("Your Atari program just did something terribly stupid:"
              " dummy_xlate($%x)\n", addr);
    /*Reset_Warm();*/
    return STmem_xlate(addr);  /* So we don't crash. */
}


/* **** This memory bank only generates bus errors **** */

static uae_u32 BusErrMem_lget(uaecptr addr)
{
    print_illegal_counted("Bus error lget", addr);

    M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA);
    return 0;
}

static uae_u32 BusErrMem_wget(uaecptr addr)
{
    print_illegal_counted("Bus error wget", addr);

    M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA);
    return 0;
}

static uae_u32 BusErrMem_bget(uaecptr addr)
{
    print_illegal_counted("Bus error bget", addr);

    M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
    return 0;
}

static void BusErrMem_lput(uaecptr addr, uae_u32 l)
{
    print_illegal_counted("Bus error lput", addr);

    M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA);
}

static void BusErrMem_wput(uaecptr addr, uae_u32 w)
{
    print_illegal_counted("Bus error wput", addr);

    M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA);
}

static void BusErrMem_bput(uaecptr addr, uae_u32 b)
{
    print_illegal_counted("Bus error bput", addr);

    M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
}

static int BusErrMem_check(uaecptr addr, uae_u32 size)
{
    return 0;
}

static uae_u8 *BusErrMem_xlate (uaecptr addr)
{
    write_log("Your Atari program just did something terribly stupid:"
              " BusErrMem_xlate($%x)\n", addr);

    /*M68000_BusError(addr);*/
    return STmem_xlate(addr);  /* So we don't crash. */
}


/* **** ST RAM memory **** */

/*static uae_u8 *STmemory;*/
#define STmemory STRam

static uae_u32 STmem_lget(uaecptr addr)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    return do_get_mem_long(STmemory + addr);
}

static uae_u32 STmem_wget(uaecptr addr)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    return do_get_mem_word(STmemory + addr);
}

static uae_u32 STmem_bget(uaecptr addr)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    return STmemory[addr];
}

static void STmem_lput(uaecptr addr, uae_u32 l)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    do_put_mem_long(STmemory + addr, l);
}

static void STmem_wput(uaecptr addr, uae_u32 w)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    do_put_mem_word(STmemory + addr, w);
}

static void STmem_bput(uaecptr addr, uae_u32 b)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    STmemory[addr] = b;
}

static int STmem_check(uaecptr addr, uae_u32 size)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    return (addr + size) <= STmem_size;
}

static uae_u8 *STmem_xlate(uaecptr addr)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    return STmemory + addr;
}


/*
 * **** ST RAM system memory ****
 * We need a separate mem bank for this region since the first 0x800 bytes on
 * the ST can only be accessed in supervisor mode. Note that the very first
 * 8 bytes of the ST memory are also a mirror of the TOS ROM, so they are write
 * protected!
 */
static uae_u32 SysMem_lget(uaecptr addr)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;

    if(addr < 0x800 && !regs.s)
    {
      M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA);
      return 0;
    }

    return do_get_mem_long(STmemory + addr);
}

static uae_u32 SysMem_wget(uaecptr addr)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;

    /* Only CPU will trigger bus error if bit S=0, not the blitter */
    if(addr < 0x800 && !regs.s && BusMode == BUS_MODE_CPU )
    {
      M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA);
      return 0;
    }

    return do_get_mem_word(STmemory + addr);
}

static uae_u32 SysMem_bget(uaecptr addr)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;

    if(addr < 0x800 && !regs.s)
    {
      M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
      return 0;
    }

    return STmemory[addr];
}

static void SysMem_lput(uaecptr addr, uae_u32 l)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;

    if(addr < 0x8 || (addr < 0x800 && !regs.s))
    {
      M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA);
      return;
    }

    do_put_mem_long(STmemory + addr, l);
}

static void SysMem_wput(uaecptr addr, uae_u32 w)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;

    /* Only CPU will trigger bus error if bit S=0, not the blitter */
    if(addr < 0x8 || (addr < 0x800 && !regs.s))
    {
      if ( BusMode == BUS_MODE_CPU )
      {
	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA);
	return;
      }
      /* If blitter writes < 0x8 then it should be ignored, else the write should be made */
      else if ( ( BusMode == BUS_MODE_BLITTER ) && ( addr < 0x8 ) )
	return;
    }

    do_put_mem_word(STmemory + addr, w);
}

static void SysMem_bput(uaecptr addr, uae_u32 b)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;

    if(addr < 0x8 || (addr < 0x800 && !regs.s))
    {
      M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
      return;
    }

    STmemory[addr] = b;
}


/*
 * **** Void memory ****
 * Between the ST-RAM end and the 4 MB barrier, there is a void memory space:
 * Reading always returns the same value and writing does nothing at all.
 * [NP] : this is not correct, reading does not always return 0, when there's
 * no memory, it will return the latest data that was read on the bus.
 * In many cases, this will return the word that was just read in the 68000's
 * prefetch register to decode the next opcode (tested on a real STF)
 */

static uae_u32 VoidMem_lget(uaecptr addr)
{
    return 0;
}

static uae_u32 VoidMem_wget(uaecptr addr)
{
    return 0;
}

static uae_u32 VoidMem_bget(uaecptr addr)
{
    return 0;
}

static void VoidMem_lput(uaecptr addr, uae_u32 l)
{
}

static void VoidMem_wput(uaecptr addr, uae_u32 w)
{
}

static void VoidMem_bput (uaecptr addr, uae_u32 b)
{
}

static int VoidMem_check(uaecptr addr, uae_u32 size)
{
    return 0;
}

static uae_u8 *VoidMem_xlate (uaecptr addr)
{
    write_log("Your Atari program just did something terribly stupid:"
              " VoidMem_xlate($%x)\n", addr);

    return STmem_xlate(addr);  /* So we don't crash. */
}


/* **** TT fast memory **** */

uae_u8 *TTmemory;

static uae_u32 TTmem_lget(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return do_get_mem_long(TTmemory + addr);
}

static uae_u32 TTmem_wget(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return do_get_mem_word(TTmemory + addr);
}

static uae_u32 TTmem_bget(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return TTmemory[addr];
}

static void TTmem_lput(uaecptr addr, uae_u32 l)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    do_put_mem_long(TTmemory + addr, l);
}

static void TTmem_wput(uaecptr addr, uae_u32 w)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    do_put_mem_word(TTmemory + addr, w);
}

static void TTmem_bput(uaecptr addr, uae_u32 b)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    TTmemory[addr] = b;
}

static int TTmem_check(uaecptr addr, uae_u32 size)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return (addr + size) <= TTmem_size;
}

static uae_u8 *TTmem_xlate(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return TTmemory + addr;
}


/* **** ROM memory **** */

uae_u8 *ROMmemory;

static uae_u32 ROMmem_lget(uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return do_get_mem_long(ROMmemory + addr);
}

static uae_u32 ROMmem_wget(uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return do_get_mem_word(ROMmemory + addr);
}

static uae_u32 ROMmem_bget(uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return ROMmemory[addr];
}

static void ROMmem_lput(uaecptr addr, uae_u32 b)
{
    print_illegal_counted("Illegal ROMmem lput", (long)addr);

    M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA);
}

static void ROMmem_wput(uaecptr addr, uae_u32 b)
{
    print_illegal_counted("Illegal ROMmem wput", (long)addr);

    M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA);
}

static void ROMmem_bput(uaecptr addr, uae_u32 b)
{
    print_illegal_counted("Illegal ROMmem bput", (long)addr);

    M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA);
}

static int ROMmem_check(uaecptr addr, uae_u32 size)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return (addr + size) <= ROMmem_size;
}

static uae_u8 *ROMmem_xlate(uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return ROMmemory + addr;
}


/* IDE controller IO memory */
/* see also ide.c */

static uae_u8 *IdeMemory;

static int IdeMem_check(uaecptr addr, uae_u32 size)
{
    addr -= IdeMem_start;
    addr &= IdeMem_mask;
    return (addr + size) <= IdeMem_size;
}

static uae_u8 *IdeMem_xlate(uaecptr addr)
{
    addr -= IdeMem_start;
    addr &= IdeMem_mask;
    return IdeMemory + addr;
}


/* Hardware IO memory */
/* see also ioMem.c */

uae_u8 *IOmemory;

static int IOmem_check(uaecptr addr, uae_u32 size)
{
    addr -= IOmem_start;
    addr &= IOmem_mask;
    return (addr + size) <= IOmem_size;
}

static uae_u8 *IOmem_xlate(uaecptr addr)
{
    addr -= IOmem_start;
    addr &= IOmem_mask;
    return IOmemory + addr;
}



/* **** Address banks **** */

static addrbank dummy_bank =
{
    dummy_lget, dummy_wget, dummy_bget,
    dummy_lput, dummy_wput, dummy_bput,
    dummy_xlate, dummy_check, NULL, ABFLAG_NONE
};

static addrbank BusErrMem_bank =
{
    BusErrMem_lget, BusErrMem_wget, BusErrMem_bget,
    BusErrMem_lput, BusErrMem_wput, BusErrMem_bput,
    BusErrMem_xlate, BusErrMem_check, NULL, ABFLAG_NONE
};

static addrbank STmem_bank =
{
    STmem_lget, STmem_wget, STmem_bget,
    STmem_lput, STmem_wput, STmem_bput,
    STmem_xlate, STmem_check, NULL, ABFLAG_RAM
};

static addrbank SysMem_bank =
{
    SysMem_lget, SysMem_wget, SysMem_bget,
    SysMem_lput, SysMem_wput, SysMem_bput,
    STmem_xlate, STmem_check, NULL, ABFLAG_RAM
};

static addrbank VoidMem_bank =
{
    VoidMem_lget, VoidMem_wget, VoidMem_bget,
    VoidMem_lput, VoidMem_wput, VoidMem_bput,
    VoidMem_xlate, VoidMem_check , NULL, ABFLAG_NONE
};

static addrbank TTmem_bank =
{
    TTmem_lget, TTmem_wget, TTmem_bget,
    TTmem_lput, TTmem_wput, TTmem_bput,
    TTmem_xlate, TTmem_check, NULL, ABFLAG_RAM
};

static addrbank ROMmem_bank =
{
    ROMmem_lget, ROMmem_wget, ROMmem_bget,
    ROMmem_lput, ROMmem_wput, ROMmem_bput,
    ROMmem_xlate, ROMmem_check, NULL, ABFLAG_ROM
};

static addrbank IdeMem_bank =
{
    Ide_Mem_lget, Ide_Mem_wget, Ide_Mem_bget,
    Ide_Mem_lput, Ide_Mem_wput, Ide_Mem_bput,
    IdeMem_xlate, IdeMem_check, NULL, ABFLAG_IO
};

static addrbank IOmem_bank =
{
    IoMem_lget, IoMem_wget, IoMem_bget,
    IoMem_lput, IoMem_wput, IoMem_bput,
    IOmem_xlate, IOmem_check, NULL, ABFLAG_IO
};



static void init_mem_banks (void)
{
    int i;
    for (i = 0; i < 65536; i++)
	put_mem_bank (i<<16, &dummy_bank);
}


/*
 * Check if an address points to a memory region that causes bus error
 * Returns true if region gives bus error
 */
bool memory_region_bus_error ( uaecptr addr )
{
	return mem_banks[bankindex(addr)] == &BusErrMem_bank;
}


/*
 * Initialize the memory banks
 */
void memory_init(uae_u32 nNewSTMemSize, uae_u32 nNewTTMemSize, uae_u32 nNewRomMemStart)
{
    STmem_size = (nNewSTMemSize + 65535) & 0xFFFF0000;
    TTmem_size = (nNewTTMemSize + 65535) & 0xFFFF0000;

    /*write_log("memory_init: STmem_size=$%x, TTmem_size=$%x, ROM-Start=$%x,\n",
              STmem_size, TTmem_size, nNewRomMemStart);*/

#if ENABLE_SMALL_MEM

    /* Allocate memory for ROM areas, IDE and IO memory space (0xE00000 - 0xFFFFFF) */
    ROMmemory = malloc(2*1024*1024);
    if (!ROMmemory) {
	fprintf(stderr, "Out of memory (ROM/IO mem)!\n");
	SDL_Quit();
	exit(1);
    }
    IdeMemory = ROMmemory + 0x100000;
    IOmemory  = ROMmemory + 0x1f0000;

    /* Allocate memory for normal ST RAM */
    STmemory = malloc(STmem_size);
    while (!STmemory && STmem_size > 512*1024) {
	STmem_size >>= 1;
	STmemory = (uae_u8 *)malloc (STmem_size);
	if (STmemory)
	    write_log ("Reducing STmem size to %dkb\n", STmem_size >> 10);
    }
    if (!STmemory) {
	write_log ("virtual memory exhausted (STmemory)!\n");
	SDL_Quit();
	exit(1);
    }

#else

    /* STmemory points to the 16 MiB STRam array, we just have to set up
     * the remaining pointers here: */
    ROMmemory = STRam + ROMmem_start;
    IdeMemory = STRam + IdeMem_start;
    IOmemory = STRam + IOmem_start;

#endif

    init_mem_banks();

    /* Set the infos about memory pointers for each mem bank, used for direct memory access in stMemory.c */
    STmem_bank.baseaddr = STmemory;
    STmem_bank.mask = STmem_mask;
    STmem_bank.start = STmem_start;

    SysMem_bank.baseaddr = STmemory;
    SysMem_bank.mask = STmem_mask;
    SysMem_bank.start = STmem_start;

    dummy_bank.baseaddr = NULL;				/* No real memory allocated for this region */
    VoidMem_bank.baseaddr = NULL;			/* No real memory allocated for this region */
    BusErrMem_bank.baseaddr = NULL;			/* No real memory allocated for this region */


    /* Map the ST system RAM: */
    map_banks(&SysMem_bank, 0x00, 1);
    /* Between STRamEnd and 4MB barrier, there is void space: */
    map_banks(&VoidMem_bank, 0x08, 0x38);
    /* Space between 4MB barrier and TOS ROM causes a bus error: */
    map_banks(&BusErrMem_bank, 0x400000 >> 16, 0xA0);
    /* Now map main ST RAM, overwriting the void and bus error regions if necessary: */
    map_banks(&STmem_bank, 0x01, (STmem_size >> 16) - 1);


    /* Handle extra RAM on TT and Falcon starting at 0x1000000 and up to 0x80000000 */
    /* This requires the CPU to use 32 bit addressing */
    /* NOTE : code backported from cpu/memory.c, but as bAddressSpace24 is always true with old */
    /* UAE's core, TT RAM is not supported at the moment */
    TTmemory = NULL;
    if ( ConfigureParams.System.bAddressSpace24 == false )
    {
	/* If there's no extra RAM on a TT, region 0x01000000 - 0x80000000 (2047 MB) must return bus errors */
	if ( ConfigureParams.System.nMachineType == MACHINE_TT )
	    map_banks ( &BusErrMem_bank, TTmem_start >> 16, ( TTmem_end - TTmem_start ) >> 16 );

	if ( TTmem_size > 0 )
	{
	    TTmemory = (uae_u8 *)malloc ( TTmem_size );

	    if ( TTmemory != NULL )
	    {
		map_banks ( &TTmem_bank, TTmem_start >> 16, TTmem_size >> 16 );
		TTmem_mask = 0xffffffff;
		TTmem_bank.baseaddr = TTmemory;
		TTmem_bank.mask = TTmem_mask;
		TTmem_bank.start = TTmem_start;
	    }
	    else
	    {
		write_log ("can't allocate %d MB for TT RAM\n" , TTmem_size / ( 1024*1024 ) );
		TTmem_size = 0;
	    }
	}
    }


    /* ROM memory: */
    /* Depending on which ROM version we are using, the other ROM region is illegal! */
    if(nNewRomMemStart == 0xFC0000)
    {
        map_banks(&ROMmem_bank, 0xFC0000 >> 16, 0x3);
        map_banks(&BusErrMem_bank, 0xE00000 >> 16, 0x10);
    }
    else if(nNewRomMemStart == 0xE00000)
    {
        map_banks(&ROMmem_bank, 0xE00000 >> 16, 0x10);
        map_banks(&BusErrMem_bank, 0xFC0000 >> 16, 0x3);
    }
    else
    {
        write_log("Illegal ROM memory start!\n");
    }

    /* Cartridge memory: */
    map_banks(&ROMmem_bank, 0xFA0000 >> 16, 0x2);
    ROMmem_bank.baseaddr = ROMmemory;
    ROMmem_bank.mask = ROMmem_mask;
    ROMmem_bank.start = ROMmem_start;

    /* IO memory: */
    map_banks(&IOmem_bank, IOmem_start>>16, 0x1);
    IOmem_bank.baseaddr = IOmemory;
    IOmem_bank.mask = IOmem_mask;
    IOmem_bank.start = IOmem_start;

    /* IDE controller memory region: */
    map_banks(&IdeMem_bank, IdeMem_start >> 16, 0x1);  /* IDE controller on the Falcon */
    IdeMem_bank.baseaddr = IdeMemory;
    IdeMem_bank.mask = IdeMem_mask;
    IdeMem_bank.start = IdeMem_start ;

    /* Illegal memory regions cause a bus error on the ST: */
    map_banks(&BusErrMem_bank, 0xF10000 >> 16, 0x9);

    illegal_count = 50;
}


/*
 * Uninitialize the memory banks.
 */
void memory_uninit (void)
{
    /* Here, we free allocated memory from memory_init */
    if (TTmem_size > 0) {
	free(TTmemory);
	TTmemory = NULL;
    }

#if ENABLE_SMALL_MEM

    if (STmemory) {
	free(STmemory);
	STmemory = NULL;
    }

    if (ROMmemory) {
	free(ROMmemory);
	ROMmemory = NULL;
    }

#endif  /* ENABLE_SMALL_MEM */
}


void map_banks (addrbank *bank, int start, int size)
{
    int bnr;
    unsigned long int hioffs = 0, endhioffs = 0x100;

    if (start >= 0x100) {
	for (bnr = start; bnr < start + size; bnr++)
	    put_mem_bank (bnr << 16, bank);
	return;
    }
    /* Some ROMs apparently require a 24 bit address space... */
    if (currprefs.address_space_24)
	endhioffs = 0x10000;
    for (hioffs = 0; hioffs < endhioffs; hioffs += 0x100)
	for (bnr = start; bnr < start+size; bnr++)
	    put_mem_bank ((bnr + hioffs) << 16, bank);
}
