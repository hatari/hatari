 /*
  * UAE - The Un*x Amiga Emulator - CPU core
  *
  * Memory management
  *
  * (c) 1995 Bernd Schmidt
  *
  * Adaptation to Hatari by Thomas Huth
  *
  * This file is distributed under the GNU Public License, version 2 or at
  * your option any later version. Read the file gpl.txt for details.
  */
char Memory_rcsid[] = "Hatari $Id: memory.c,v 1.13 2004-02-19 15:22:13 thothy Exp $";

#include "sysdeps.h"
#include "hatari-glue.h"
#include "maccess.h"
#include "memory.h"
#include "../includes/main.h"
#include "../includes/tos.h"
#include "../includes/intercept.h"
#include "../includes/reset.h"
#include "../includes/decode.h"
#include "../includes/m68000.h"
#include "newcpu.h"


uae_u32 STmem_size, TTmem_size = 0;
uae_u32 TTmem_mask;

#define STmem_start  0x00000000
#define ROMmem_start 0x00E00000
#define IOmem_start  0x00FF0000
#define TTmem_start  0x01000000

#define IOmem_size  65536
#define ROMmem_size (0x00FF0000 - 0x00E00000)  /* So we cover both possible ROM regions + cartridge */

#define STmem_mask  0x00ffffff
#define ROMmem_mask 0x00ffffff
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


/* A dummy bank that only contains zeros */

static uae_u32 dummy_lget (uaecptr) REGPARAM;
static uae_u32 dummy_wget (uaecptr) REGPARAM;
static uae_u32 dummy_bget (uaecptr) REGPARAM;
static void dummy_lput (uaecptr, uae_u32) REGPARAM;
static void dummy_wput (uaecptr, uae_u32) REGPARAM;
static void dummy_bput (uaecptr, uae_u32) REGPARAM;
static int dummy_check (uaecptr addr, uae_u32 size) REGPARAM;

uae_u32 REGPARAM2 dummy_lget (uaecptr addr)
{
    if (illegal_mem)
	write_log ("Illegal lget at %08lx\n", (long)addr);

    return 0;
}

uae_u32 REGPARAM2 dummy_wget (uaecptr addr)
{
    if (illegal_mem)
	write_log ("Illegal wget at %08lx\n", (long)addr);

    return 0;
}

uae_u32 REGPARAM2 dummy_bget (uaecptr addr)
{
    if (illegal_mem)
	write_log ("Illegal bget at %08lx\n", (long)addr);

    return 0;
}

void REGPARAM2 dummy_lput (uaecptr addr, uae_u32 l)
{
    if (illegal_mem)
	write_log ("Illegal lput at %08lx\n", (long)addr);
}

void REGPARAM2 dummy_wput (uaecptr addr, uae_u32 w)
{
    if (illegal_mem)
	write_log ("Illegal wput at %08lx\n", (long)addr);
}

void REGPARAM2 dummy_bput (uaecptr addr, uae_u32 b)
{
    if (illegal_mem)
	write_log ("Illegal bput at %08lx\n", (long)addr);
}

int REGPARAM2 dummy_check (uaecptr addr, uae_u32 size)
{
    if (illegal_mem)
	write_log ("Illegal check at %08lx\n", (long)addr);

    return 0;
}

uae_u8 REGPARAM2 *dummy_xlate (uaecptr addr)
{
    write_log("Your Atari program just did something terribly stupid:"
              " dummy_xlate($%x)\n", addr);
    /*Reset_Warm();*/
    return STmem_xlate(addr);  /* So we don't crash. */
}


/* **** This memory bank only generates bus errors **** */

uae_u32 REGPARAM2 BusErrMem_lget(uaecptr addr)
{
    if (illegal_mem)
	write_log ("Bus error lget at %08lx\n", (long)addr);

    M68000_BusError(addr, 1);
    return 0;
}

uae_u32 REGPARAM2 BusErrMem_wget(uaecptr addr)
{
    if (illegal_mem)
	write_log ("Bus error wget at %08lx\n", (long)addr);

    M68000_BusError(addr, 1);
    return 0;
}

uae_u32 REGPARAM2 BusErrMem_bget(uaecptr addr)
{
    if (illegal_mem)
	write_log ("Bus error bget at %08lx\n", (long)addr);

    M68000_BusError(addr, 1);
    return 0;
}

void REGPARAM2 BusErrMem_lput(uaecptr addr, uae_u32 l)
{
    if (illegal_mem)
	write_log ("Bus error lput at %08lx\n", (long)addr);

    M68000_BusError(addr, 0);
}

void REGPARAM2 BusErrMem_wput(uaecptr addr, uae_u32 w)
{
    if (illegal_mem)
	write_log ("Bus error wput at %08lx\n", (long)addr);

    M68000_BusError(addr, 0);
}

void REGPARAM2 BusErrMem_bput(uaecptr addr, uae_u32 b)
{
    if (illegal_mem)
	write_log ("Bus error bput at %08lx\n", (long)addr);

    M68000_BusError(addr, 0);
}

int REGPARAM2 BusErrMem_check(uaecptr addr, uae_u32 size)
{
    if (illegal_mem)
	write_log ("Bus error check at %08lx\n", (long)addr);

    return 0;
}

uae_u8 REGPARAM2 *BusErrMem_xlate (uaecptr addr)
{
    write_log("Your Atari program just did something terribly stupid:"
              " BusErrMem_xlate($%x)\n", addr);

    /*M68000_BusError(addr);*/
    return STmem_xlate(addr);  /* So we don't crash. */
}


/* **** ST RAM memory **** */

uae_u8 *STmemory;

uae_u32 REGPARAM2 STmem_lget (uaecptr addr)
{
    uae_u32 *m;

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    m = (uae_u32 *)(STmemory + addr);
    return do_get_mem_long (m);
}

uae_u32 REGPARAM2 STmem_wget (uaecptr addr)
{
    uae_u16 *m;

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    m = (uae_u16 *)(STmemory + addr);
    return do_get_mem_word (m);
}

uae_u32 REGPARAM2 STmem_bget (uaecptr addr)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    return STmemory[addr];
}

void REGPARAM2 STmem_lput (uaecptr addr, uae_u32 l)
{
    uae_u32 *m;

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    m = (uae_u32 *)(STmemory + addr);
    do_put_mem_long (m, l);
}

void REGPARAM2 STmem_wput (uaecptr addr, uae_u32 w)
{
    uae_u16 *m;

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    m = (uae_u16 *)(STmemory + addr);
    do_put_mem_word (m, w);
}

void REGPARAM2 STmem_bput (uaecptr addr, uae_u32 b)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    STmemory[addr] = b;
}

int REGPARAM2 STmem_check (uaecptr addr, uae_u32 size)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    return (addr + size) <= STmem_size;
}

uae_u8 REGPARAM2 *STmem_xlate (uaecptr addr)
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
uae_u32 REGPARAM2 SysMem_lget(uaecptr addr)
{
    uae_u32 *m;

    if(addr < 0x800 && !regs.s)
    {
      M68000_BusError(addr, 1);
      return 0;
    }

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    m = (uae_u32 *)(STmemory + addr);
    return do_get_mem_long (m);
}

uae_u32 REGPARAM2 SysMem_wget(uaecptr addr)
{
    uae_u16 *m;

    if(addr < 0x800 && !regs.s)
    {
      M68000_BusError(addr, 1);
      return 0;
    }

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    m = (uae_u16 *)(STmemory + addr);
    return do_get_mem_word (m);
}

uae_u32 REGPARAM2 SysMem_bget(uaecptr addr)
{
    if(addr < 0x800 && !regs.s)
    {
      M68000_BusError(addr, 1);
      return 0;
    }

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    return STmemory[addr];
}

void REGPARAM2 SysMem_lput(uaecptr addr, uae_u32 l)
{
    uae_u32 *m;

    if(addr < 0x8 || (addr < 0x800 && !regs.s))
    {
      M68000_BusError(addr, 0);
      return;
    }

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    m = (uae_u32 *)(STmemory + addr);
    do_put_mem_long (m, l);
}

void REGPARAM2 SysMem_wput(uaecptr addr, uae_u32 w)
{
    uae_u16 *m;

    if(addr < 0x8 || (addr < 0x800 && !regs.s))
    {
      M68000_BusError(addr, 0);
      return;
    }

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    m = (uae_u16 *)(STmemory + addr);
    do_put_mem_word (m, w);
}

void REGPARAM2 SysMem_bput(uaecptr addr, uae_u32 b)
{
    if(addr < 0x8 || (addr < 0x800 && !regs.s))
    {
      M68000_BusError(addr, 0);
      return;
    }

    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    STmemory[addr] = b;
}


/*
 * **** Void memory ****
 * Between the ST-RAM end and the 4 MB barrier, there is a void memory space:
 * Reading always returns the same value and writing does nothing at all.
 */

uae_u32 REGPARAM2 VoidMem_lget(uaecptr addr)
{
    return 0;
}

uae_u32 REGPARAM2 VoidMem_wget(uaecptr addr)
{
    return 0;
}

uae_u32 REGPARAM2 VoidMem_bget(uaecptr addr)
{
    return 0;
}

void REGPARAM2 VoidMem_lput(uaecptr addr, uae_u32 l)
{
}

void REGPARAM2 VoidMem_wput(uaecptr addr, uae_u32 w)
{
}

void REGPARAM2 VoidMem_bput (uaecptr addr, uae_u32 b)
{
}

int REGPARAM2 VoidMem_check(uaecptr addr, uae_u32 size)
{
    if (illegal_mem)
	write_log ("Void memory check at %08lx\n", (long)addr);

    return 0;
}

uae_u8 REGPARAM2 *VoidMem_xlate (uaecptr addr)
{
    write_log("Your Atari program just did something terribly stupid:"
              " VoidMem_xlate($%x)\n", addr);

    return STmem_xlate(addr);  /* So we don't crash. */
}


/* **** TT fast memory (not yet supported) **** */

static uae_u8 *TTmemory;

static uae_u32 TTmem_lget (uaecptr) REGPARAM;
static uae_u32 TTmem_wget (uaecptr) REGPARAM;
static uae_u32 TTmem_bget (uaecptr) REGPARAM;
static void TTmem_lput (uaecptr, uae_u32) REGPARAM;
static void TTmem_wput (uaecptr, uae_u32) REGPARAM;
static void TTmem_bput (uaecptr, uae_u32) REGPARAM;
static int TTmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *TTmem_xlate (uaecptr addr) REGPARAM;

uae_u32 REGPARAM2 TTmem_lget (uaecptr addr)
{
    uae_u32 *m;
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    m = (uae_u32 *)(TTmemory + addr);
    return do_get_mem_long (m);
}

uae_u32 REGPARAM2 TTmem_wget (uaecptr addr)
{
    uae_u16 *m;
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    m = (uae_u16 *)(TTmemory + addr);
    return do_get_mem_word (m);
}

uae_u32 REGPARAM2 TTmem_bget (uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return TTmemory[addr];
}

void REGPARAM2 TTmem_lput (uaecptr addr, uae_u32 l)
{
    uae_u32 *m;
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    m = (uae_u32 *)(TTmemory + addr);
    do_put_mem_long (m, l);
}

void REGPARAM2 TTmem_wput (uaecptr addr, uae_u32 w)
{
    uae_u16 *m;
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    m = (uae_u16 *)(TTmemory + addr);
    do_put_mem_word (m, w);
}

void REGPARAM2 TTmem_bput (uaecptr addr, uae_u32 b)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    TTmemory[addr] = b;
}

int REGPARAM2 TTmem_check (uaecptr addr, uae_u32 size)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return (addr + size) <= TTmem_size;
}

uae_u8 REGPARAM2 *TTmem_xlate(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return TTmemory + addr;
}


/* **** ROM memory **** */

uae_u8 *ROMmemory;

static uae_u32 ROMmem_lget (uaecptr) REGPARAM;
static uae_u32 ROMmem_wget (uaecptr) REGPARAM;
static uae_u32 ROMmem_bget (uaecptr) REGPARAM;
static void  ROMmem_lput (uaecptr, uae_u32) REGPARAM;
static void  ROMmem_wput (uaecptr, uae_u32) REGPARAM;
static void  ROMmem_bput (uaecptr, uae_u32) REGPARAM;
static int  ROMmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *ROMmem_xlate (uaecptr addr) REGPARAM;

uae_u32 REGPARAM2 ROMmem_lget (uaecptr addr)
{
    uae_u32 *m;
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    m = (uae_u32 *)(ROMmemory + addr);
    return do_get_mem_long (m);
}

uae_u32 REGPARAM2 ROMmem_wget (uaecptr addr)
{
    uae_u16 *m;
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    m = (uae_u16 *)(ROMmemory + addr);
    return do_get_mem_word (m);
}

uae_u32 REGPARAM2 ROMmem_bget (uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return ROMmemory[addr];
}

void REGPARAM2 ROMmem_lput (uaecptr addr, uae_u32 b)
{
    if (illegal_mem)
	write_log ("Illegal ROMmem lput at %08lx\n", (long)addr);

    M68000_BusError(addr, 0);
}

void REGPARAM2 ROMmem_wput (uaecptr addr, uae_u32 b)
{
    if (illegal_mem)
	write_log ("Illegal ROMmem wput at %08lx\n", (long)addr);

    M68000_BusError(addr, 0);
}

void REGPARAM2 ROMmem_bput (uaecptr addr, uae_u32 b)
{
    if (illegal_mem)
	write_log ("Illegal ROMmem bput at %08lx\n", (long)addr);

    M68000_BusError(addr, 0);
}

int REGPARAM2 ROMmem_check (uaecptr addr, uae_u32 size)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return (addr + size) <= ROMmem_size;
}

uae_u8 REGPARAM2 *ROMmem_xlate (uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return ROMmemory + addr;
}


/* Hardware IO memory */
/* see also intercept.c */

uae_u8 *IOmemory;

static int  IOmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *IOmem_xlate (uaecptr addr) REGPARAM;

int REGPARAM2 IOmem_check (uaecptr addr, uae_u32 size)
{
    addr -= IOmem_start;
    addr &= IOmem_mask;
    return (addr + size) <= IOmem_size;
}

uae_u8 REGPARAM2 *IOmem_xlate (uaecptr addr)
{
    addr -= IOmem_start;
    addr &= IOmem_mask;
    return IOmemory + addr;
}



/* **** Address banks **** */

addrbank dummy_bank = {
    dummy_lget, dummy_wget, dummy_bget,
    dummy_lput, dummy_wput, dummy_bput,
    dummy_xlate, dummy_check
};

addrbank BusErrMem_bank = {
    BusErrMem_lget, BusErrMem_wget, BusErrMem_bget,
    BusErrMem_lput, BusErrMem_wput, BusErrMem_bput,
    BusErrMem_xlate, BusErrMem_check
};

addrbank STmem_bank = {
    STmem_lget, STmem_wget, STmem_bget,
    STmem_lput, STmem_wput, STmem_bput,
    STmem_xlate, STmem_check
};

addrbank SysMem_bank = {
    SysMem_lget, SysMem_wget, SysMem_bget,
    SysMem_lput, SysMem_wput, SysMem_bput,
    STmem_xlate, STmem_check
};

addrbank VoidMem_bank = {
    VoidMem_lget, VoidMem_wget, VoidMem_bget,
    VoidMem_lput, VoidMem_wput, VoidMem_bput,
    VoidMem_xlate, VoidMem_check
};

addrbank TTmem_bank = {
    TTmem_lget, TTmem_wget, TTmem_bget,
    TTmem_lput, TTmem_wput, TTmem_bput,
    TTmem_xlate, TTmem_check
};

addrbank ROMmem_bank = {
    ROMmem_lget, ROMmem_wget, ROMmem_bget,
    ROMmem_lput, ROMmem_wput, ROMmem_bput,
    ROMmem_xlate, ROMmem_check
};

addrbank IOmem_bank = {
    Intercept_ReadLong, Intercept_ReadWord, Intercept_ReadByte,
    Intercept_WriteLong, Intercept_WriteWord, Intercept_WriteByte,
    IOmem_xlate, IOmem_check
};


char *address_space, *good_address_map;
int good_address_fd;


static void init_mem_banks (void)
{
    int i;
    for (i = 0; i < 65536; i++)
	put_mem_bank (i<<16, &dummy_bank);
}


/*
 * Initialize the memory banks
 */
void memory_init(uae_u32 f_STMemSize, uae_u32 f_TTMemSize, uae_u32 f_RomMemStart)
{
    STmem_size = (f_STMemSize + 65535) & 0xFFFF0000;
    TTmem_size = (f_TTMemSize + 65535) & 0xFFFF0000;

    /*write_log("memory_init: STmem_size=$%x, TTmem_size=$%x, ROM-Start=$%x,\n",
              STmem_size, TTmem_size, f_RomMemStart);*/

    STmemory = STRam;
    ROMmemory = STRam + ROMmem_start;
    IOmemory = STRam + IOmem_start;
/*
    while (! STmemory && STmem_size > 512*1024) {
	STmem_size >>= 1;
	STmemory = (uae_u8 *)malloc (STmem_size);
	if (STmemory)
	    fprintf (stderr, "Reducing STmem size to %dkb\n", STmem_size >> 10);
    }
    if (! STmemory) {
	write_log ("virtual memory exhausted (STmemory)!\n");
	abort ();
    }
*/

    init_mem_banks();

    /* Map the ST RAM: */
    map_banks(&SysMem_bank, 0x00, 1);
    map_banks(&VoidMem_bank, 0x08, 0x38);  /* Between STRamEnd and 4MB barrier, there is void space! */
    map_banks(&STmem_bank, 0x01, (STmem_size >> 16) - 1);

    /* TT memory isn't really supported yet */
    if (TTmem_size > 0)
	TTmemory = (uae_u8 *)malloc (TTmem_size);
    if (TTmemory != 0)
	map_banks (&TTmem_bank, TTmem_start >> 16, TTmem_size >> 16);
    else
	TTmem_size = 0;
    TTmem_mask = TTmem_size - 1;

    /* ROM memory: */
    /* Depending on which ROM version we are using, the other ROM region is illegal! */
    if(f_RomMemStart == 0xFC0000)
    {
        map_banks(&ROMmem_bank, 0xFC0000 >> 16, 0x3);
        map_banks(&BusErrMem_bank, 0xE00000 >> 16, 0x10);
    }
    else if(f_RomMemStart == 0xE00000)
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

    /* IO memory: */
    map_banks(&IOmem_bank, IOmem_start>>16, 0x1);

    /* Illegal memory regions cause a bus error on the ST: */
    map_banks(&BusErrMem_bank, 0x400000 >> 16, 0xA0); /* Space between 4MB barrier and TOS ROM */
    map_banks(&BusErrMem_bank, 0xF00000 >> 16, 0xA);  /* IDE controler on the Falcon */
}


/*
 * Uninitialize the memory banks.
 */
void memory_uninit (void)
{
    /* Here, we free allocated memory from memory_init */
    if(TTmem_size > 0)
    {
      free(TTmemory);
      TTmemory = NULL;
    }
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
    if (address_space_24)
	endhioffs = 0x10000;
    for (hioffs = 0; hioffs < endhioffs; hioffs += 0x100)
	for (bnr = start; bnr < start+size; bnr++)
	    put_mem_bank ((bnr + hioffs) << 16, bank);
}
