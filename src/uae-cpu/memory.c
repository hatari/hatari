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
static char rcsid[] = "Hatari $Id: memory.c,v 1.4 2003-03-12 17:25:57 thothy Exp $";

#include "sysdeps.h"
#include "hatari-glue.h"
#include "maccess.h"
#include "memory.h"
#include "../includes/main.h"
#include "../includes/tos.h"
#include "../includes/intercept.h"
#include "../includes/reset.h"

#ifdef USE_MAPPED_MEMORY
#include <sys/mman.h>
#endif


extern   unsigned char STRam[16*1024*1024];  /* See hatari.c */


/* Set by each memory handler that does not simply access real memory.  */
int special_mem;

uae_u32 allocated_STmem;
uae_u32 allocated_TTmem;

uae_u32 STmem_mask, ROMmem_mask, IOmem_mask, TTmem_mask;


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
    special_mem |= S_READ;
    if (illegal_mem)
	write_log ("Illegal lget at %08lx\n", (long)addr);

    return 0;
}

uae_u32 REGPARAM2 dummy_wget (uaecptr addr)
{
    special_mem |= S_READ;
    if (illegal_mem)
	write_log ("Illegal wget at %08lx\n", (long)addr);

    return 0;
}

uae_u32 REGPARAM2 dummy_bget (uaecptr addr)
{
    special_mem |= S_READ;
    if (illegal_mem)
	write_log ("Illegal bget at %08lx\n", (long)addr);

    return 0;
}

void REGPARAM2 dummy_lput (uaecptr addr, uae_u32 l)
{
    special_mem |= S_WRITE;
    if (illegal_mem)
	write_log ("Illegal lput at %08lx\n", (long)addr);
}
void REGPARAM2 dummy_wput (uaecptr addr, uae_u32 w)
{
    special_mem |= S_WRITE;
    if (illegal_mem)
	write_log ("Illegal wput at %08lx\n", (long)addr);
}
void REGPARAM2 dummy_bput (uaecptr addr, uae_u32 b)
{
    special_mem |= S_WRITE;
    if (illegal_mem)
	write_log ("Illegal bput at %08lx\n", (long)addr);
}

int REGPARAM2 dummy_check (uaecptr addr, uae_u32 size)
{
    if (illegal_mem)
	write_log ("Illegal check at %08lx\n", (long)addr);

    return 0;
}


/* ST RAM memory */

uae_u8 *STmemory;

static int STmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *STmem_xlate (uaecptr addr) REGPARAM;

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
    return (addr + size) <= allocated_STmem;
}

uae_u8 REGPARAM2 *STmem_xlate (uaecptr addr)
{
    addr -= STmem_start & STmem_mask;
    addr &= STmem_mask;
    return STmemory + addr;
}


/* TT fast memory */

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
    return (addr + size) <= allocated_TTmem;
}

uae_u8 REGPARAM2 *TTmem_xlate(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return TTmemory + addr;
}

/* ROM memory */

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
}

void REGPARAM2 ROMmem_wput (uaecptr addr, uae_u32 b)
{
    if (illegal_mem)
	write_log ("Illegal ROMmem wput at %08lx\n", (long)addr);
}

void REGPARAM2 ROMmem_bput (uaecptr addr, uae_u32 b)
{
    if (illegal_mem)
	write_log ("Illegal ROMmem lput at %08lx\n", (long)addr);
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
    addr -= IOmem_start & IOmem_mask;
    addr &= IOmem_mask;
    return (addr + size) <= IOmem_size;
}

uae_u8 REGPARAM2 *IOmem_xlate (uaecptr addr)
{
    addr -= IOmem_start & IOmem_mask;
    addr &= IOmem_mask;
    return IOmemory + addr;
}



/* Default memory access functions */

int REGPARAM2 default_check (uaecptr a, uae_u32 b)
{
    return 0;
}

uae_u8 REGPARAM2 *default_xlate (uaecptr a)
{
    write_log ("Your Atari program just did something terribly stupid\n");
    Reset_Warm();
    return ROMmem_xlate (get_long (0xFC0000)); /* So we don't crash. */
}


/* Address banks */

addrbank dummy_bank = {
    dummy_lget, dummy_wget, dummy_bget,
    dummy_lput, dummy_wput, dummy_bput,
    default_xlate, dummy_check
};

addrbank STmem_bank = {
    STmem_lget, STmem_wget, STmem_bget,
    STmem_lput, STmem_wput, STmem_bput,
    STmem_xlate, STmem_check
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

#define MAKE_USER_PROGRAMS_BEHAVE 1
void memory_init (void)
{
/*
    char buffer[4096];
    char *nam;
    int i, fd;
*/

    allocated_STmem = STmem_size;
    allocated_TTmem = TTmem_size;

#ifdef USE_MAPPED_MEMORY
#error So nicht
    fd = open ("/dev/zero", O_RDWR);
    good_address_map = mmap (NULL, 1 << 24, PROT_READ, MAP_PRIVATE, fd, 0);
    /* Don't believe USER_PROGRAMS_BEHAVE. Otherwise, we'd segfault as soon
     * as a decrunch routine tries to do color register hacks. */
    address_space = mmap (NULL, 1 << 24, PROT_READ | (USER_PROGRAMS_BEHAVE || MAKE_USER_PROGRAMS_BEHAVE? PROT_WRITE : 0), MAP_PRIVATE, fd, 0);
    if ((int)address_space < 0 || (int)good_address_map < 0) {
	write_log ("Your system does not have enough virtual memory - increase swap.\n");
	abort ();
    }
#ifdef MAKE_USER_PROGRAMS_BEHAVE
    memset (address_space + 0xDFF180, 0xFF, 32*2);
#else
    /* Likewise. This is mostly for mouse button checks. */
    if (USER_PROGRAMS_BEHAVE)
	memset (address_space + 0xA00000, 0xFF, 0xF00000 - 0xA00000);
#endif
    STmemory = mmap (address_space, 0x200000, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0);
    ROMmemory = mmap (address_space + ROMmem_start, ROMmem_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED, fd, 0);

    close(fd);

    good_address_fd = open (nam = tmpnam (NULL), O_CREAT|O_RDWR, 0600);
    memset (buffer, 1, sizeof(buffer));
    write (good_address_fd, buffer, sizeof buffer);
    unlink (nam);

    for (i = 0; i < allocated_STmem; i += 4096)
	mmap (good_address_map + i, 4096, PROT_READ, MAP_FIXED | MAP_PRIVATE,
	      good_address_fd, 0);
    for (i = 0; i < ROMmem_size; i += 4096)
	mmap (good_address_map + i + 0x1000000 - ROMmem_size, 4096, PROT_READ,
	      MAP_FIXED | MAP_PRIVATE, good_address_fd, 0);
#else
    ROMmemory = STRam+ROMmem_start;  /*(uae_u8 *)malloc (ROMmem_size)*/;
    STmemory = STRam; /*(uae_u8 *)malloc (allocated_STmem);*/
/*
    while (! STmemory && allocated_STmem > 512*1024) {
	allocated_STmem >>= 1;
	STmemory = (uae_u8 *)malloc (allocated_STmem);
	if (STmemory)
	    fprintf (stderr, "Reducing STmem size to %dkb\n", allocated_STmem >> 10);
    }
    if (! STmemory) {
	write_log ("virtual memory exhausted (STmemory)!\n");
	abort ();
    }
*/
#endif

    init_mem_banks ();

    /* Map the STmem into all of the lower 16MB */
    map_banks (&STmem_bank, 0x00, 256);

    if (allocated_TTmem > 0)
	TTmemory = (uae_u8 *)malloc (allocated_TTmem);
    if (TTmemory != 0)
	map_banks (&TTmem_bank, TTmem_start >> 16, allocated_TTmem >> 16);
    else
	allocated_TTmem = 0;

    map_banks(&ROMmem_bank, ROMmem_start >> 16, ROMmem_size/65536);
/*    map_banks(&ROMmem_bank, 0xFFFC, 4);*/

    map_banks(&IOmem_bank, IOmem_start>>16, 1);

    STmem_mask = 0x00ffffff;
    ROMmem_mask = 0x00ffffff;
    TTmem_mask = allocated_TTmem - 1;
    IOmem_mask = IOmem_size - 1;
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
