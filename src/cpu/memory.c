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
const char Memory_fileid[] = "Hatari memory.c";

#include <SDL.h>
#include "main.h"
#include "sysdeps.h"
#include "hatari-glue.h"
#include "maccess.h"
#include "memory.h"

#include "tos.h"
#include "ide.h"
#include "ioMem.h"
#include "reset.h"
#include "screen.h"
#include "stMemory.h"
#include "m68000.h"
#include "configuration.h"

#include "newcpu.h"


/* Set illegal_mem to 1 for debug output: */
#define illegal_mem 1

static int illegal_count;

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

/* Some prototypes: */
static int REGPARAM3 STmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 * REGPARAM3 STmem_xlate (uaecptr addr) REGPARAM;



#ifdef WINUAE_FOR_HATARI
#undef NATMEM_OFFSET			/* Don't use shm in Hatari */
#endif

#ifdef NATMEM_OFFSET
bool canbang;
int candirect = -1;
#endif

#ifdef JIT
/* Set by each memory handler that does not simply access real memory. */
int special_mem, special_mem_default;
/* do not use get_n_addr */
int jit_n_addr_unsafe;
#endif

#ifdef NATMEM_OFFSET
static bool isdirectjit (void)
{
	return currprefs.cachesize && !currprefs.comptrustbyte;
}

static bool canjit (void)
{
	if (currprefs.cpu_model < 68020 || currprefs.address_space_24)
		return false;
	return true;
}
static bool needmman (void)
{
	if (!currprefs.jit_direct_compatible_memory)
		return false;
#ifdef _WIN32
	return true;
#endif
	if (canjit ())
		return true;
	return false;
}

static void nocanbang (void)
{
	if (canbang) {
		write_log(_T("Switching JIT direct off!\n"));
	}
	canbang = 0;
}
#endif

uae_u8 ce_banktype[65536];
uae_u8 ce_cachable[65536];


/* The address space setting used during the last reset.  */
static bool last_address_space_24;

addrbank *mem_banks[MEMORY_BANKS];

/* This has two functions. It either holds a host address that, when added
to the 68k address, gives the host address corresponding to that 68k
address (in which case the value in this array is even), OR it holds the
same value as mem_banks, for those banks that have baseaddr==0. In that
case, bit 0 is set (the memory access routines will take care of it).  */

uae_u8 *baseaddr[MEMORY_BANKS];

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

int addr_valid (const TCHAR *txt, uaecptr addr, uae_u32 len)
{
	addrbank *ab = &get_mem_bank(addr);
	if (ab == 0 || !(ab->flags & (ABFLAG_RAM | ABFLAG_ROM)) || addr < 0x100 || len > 16777215 || !valid_address (addr, len)) {
		write_log (_T("corrupt %s pointer %x (%d) detected!\n"), txt, addr, len);
		return 0;
	}
	return 1;
}

static uae_u32 REGPARAM3 dummy_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 dummy_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 dummy_bget (uaecptr) REGPARAM;
static void REGPARAM3 dummy_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 dummy_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 dummy_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 dummy_check (uaecptr addr, uae_u32 size) REGPARAM;

#define	MAX_ILG 200
#define NONEXISTINGDATA 0
//#define NONEXISTINGDATA 0xffffffff


static void print_illegal_counted(const char *txt, uaecptr addr)
{
	if (!illegal_mem || illegal_count >= MAX_ILG)
		return;

	write_log("%s at %08lx\n", txt, (long)addr);
	if (++illegal_count == MAX_ILG)
		write_log("Suppressing further messages about illegal memory accesses.\n");
}

/* **** A dummy bank that only contains zeros **** */
/* TODO [NP] : in many cases, we should not return 0 but a value depending on the data */
/* last accessed on the bus */

static void dummylog(int rw, uaecptr addr, int size, uae_u32 val, int ins)
{
	if (illegal_count >= MAX_ILG && MAX_ILG > 0)
		return;
#ifndef WINUAE_FOR_HATARI
	/* ignore Zorro3 expansion space */
	if (addr >= 0xff000000 && addr <= 0xff000200)
		return;
	/* autoconfig and extended rom */
	if (addr >= 0xe00000 && addr <= 0xf7ffff)
		return;
	/* motherboard ram */
	if (addr >= 0x08000000 && addr <= 0x08000007)
		return;
	if (addr >= 0x07f00000 && addr <= 0x07f00007)
		return;
	if (addr >= 0x07f7fff0 && addr <= 0x07ffffff)
		return;
#endif
	if (MAX_ILG >= 0)
		illegal_count++;
	if (ins) {
		write_log (_T("WARNING: Illegal opcode %cget at %08x PC=%x\n"),
			size == sz_word ? 'w' : 'l', addr, M68K_GETPC);
	} else if (rw) {
		write_log (_T("Illegal %cput at %08x=%08x PC=%x\n"),
			size == sz_byte ? 'b' : size == sz_word ? 'w' : 'l', addr, val, M68K_GETPC);
	} else {
		write_log (_T("Illegal %cget at %08x PC=%x\n"),
			size == sz_byte ? 'b' : size == sz_word ? 'w' : 'l', addr, M68K_GETPC);
	}
}

void dummy_put (uaecptr addr, int size, uae_u32 val)
{
#ifndef WINUAE_FOR_HATARI
#if FLASHEMU
	if (addr >= 0xf00000 && addr < 0xf80000 && size < 2)
		flash_write(addr, val);
#endif

	if (gary_nonrange(addr) || (size > sz_byte && gary_nonrange(addr + (1 << size) - 1))) {
		if (gary_timeout)
			gary_wait(addr, size, true);
		if (gary_toenb && currprefs.mmu_model)
			hardware_exception2(addr, val, false, false, size);
	}

#else
	/* Hatari : do nothing in case of dummy_put */
#endif
}


static uae_u32 nonexistingdata(void)
{
#ifndef WINUAE_FOR_HATARI
	if (currprefs.cs_unmapped_space == 1)
		return 0x00000000;
	if (currprefs.cs_unmapped_space == 2)
		return 0xffffffff;
#endif
	return NONEXISTINGDATA;
}


uae_u32 dummy_get (uaecptr addr, int size, bool inst, uae_u32 defvalue)
{
	uae_u32 v = defvalue;

#ifndef WINUAE_FOR_HATARI
#if FLASHEMU
	if (addr >= 0xf00000 && addr < 0xf80000 && size < 2) {
		if (addr < 0xf60000)
			return flash_read(addr);
		return 8;
	}
#endif
#if ACA500x_DEVELOPMENT
	if (addr == 0xb03000) {
		return 0xffff;
	}
	if (addr == 0xb07000) {
		return 0x0000;
	}
	if (addr == 0xb2f800) {
		return 0xffff;
	}
	if (addr == 0xb3b800) {
		return 0x0000;
	}
	if (addr == 0xb3f800) {
		return currprefs.cpu_model > 68000 ? 0x0000 : 0xffff;
	}
	if (addr == 0xb0b000) {
		extern bool isideint(void);
		return isideint() ? 0xffff : 0x0000;
	}
#endif
	if (gary_nonrange(addr) || (size > 1 && gary_nonrange(addr + size - 1))) {
		if (gary_timeout)
			gary_wait (addr, size, false);
		if (gary_toenb)
			exception2 (addr, false, size, (regs.s ? 4 : 0) | (inst ? 0 : 1));
		return v;
	}

	if (currprefs.cpu_model >= 68040)
		return v;
	if (!currprefs.cpu_compatible)
		return v;
	if (currprefs.address_space_24)
		addr &= 0x00ffffff;
	if (addr >= 0x10000000)
		return v;
	if ((currprefs.cpu_model <= 68010) || (currprefs.cpu_model == 68020 && (currprefs.chipset_mask & CSMASK_AGA) && currprefs.address_space_24)) {
		if (size == sz_long) {
			v = regs.irc & 0xffff;
			if (addr & 1)
				v = (v << 8) | (v >> 8);
			v = (v << 16) | v;
		} else if (size == sz_word) {
			v = regs.irc & 0xffff;
			if (addr & 1)
				v = (v << 8) | (v >> 8);
		} else {
			v = regs.irc;
			v = (addr & 1) ? (v & 0xff) : ((v >> 8) & 0xff);
		}
	}
#if 0
	if (addr >= 0x10000000)
		write_log (_T("%08X %d = %08x\n"), addr, size, v);
#endif

#else
	/* Hatari : TODO returns 0 for now, but we should use last databus value */
	v = 0;
#endif
	return v;
}

static uae_u32 REGPARAM2 dummy_lget (uaecptr addr)
{
	if (illegal_mem)
		dummylog (0, addr, 4, 0, 0);
	return dummy_get(addr, sz_long, false, nonexistingdata());
}
uae_u32 REGPARAM2 dummy_lgeti (uaecptr addr)
{
	if (illegal_mem)
		dummylog(0, addr, sz_long, 0, 1);
	return dummy_get(addr, sz_long, true, nonexistingdata());
}

static uae_u32 REGPARAM2 dummy_wget (uaecptr addr)
{
	if (illegal_mem)
		dummylog(0, addr, sz_word, 0, 0);
	return dummy_get(addr, sz_word, false, nonexistingdata());
}
uae_u32 REGPARAM2 dummy_wgeti (uaecptr addr)
{
	if (illegal_mem)
		dummylog(0, addr, sz_word, 0, 1);
	return dummy_get(addr, sz_word, true, nonexistingdata());
}

static uae_u32 REGPARAM2 dummy_bget (uaecptr addr)
{
	if (illegal_mem)
		dummylog(0, addr, sz_byte, 0, 0);
	return dummy_get(addr, sz_byte, false, nonexistingdata());
}

static void REGPARAM2 dummy_lput (uaecptr addr, uae_u32 l)
{
	if (illegal_mem)
		dummylog(1, addr, sz_long, l, 0);
	dummy_put(addr, sz_long, l);
}
static void REGPARAM2 dummy_wput (uaecptr addr, uae_u32 w)
{
	if (illegal_mem)
		dummylog(1, addr, sz_word, w, 0);
	dummy_put(addr, sz_word, w);
}
static void REGPARAM2 dummy_bput (uaecptr addr, uae_u32 b)
{
	if (illegal_mem)
		dummylog(1, addr, sz_byte, b, 0);
	dummy_put(addr, sz_byte, b);
}

static int REGPARAM2 dummy_check (uaecptr addr, uae_u32 size)
{
	return 0;
}

static uae_u8 * REGPARAM3 dummy_xlate(uaecptr addr)
{
	write_log("Your Atari program just did something terribly stupid:"
	          " dummy_xlate($%x)\n", addr);
	/*Reset_Warm();*/
	return STmem_xlate(addr);  /* So we don't crash. */
}


#ifndef WINUAE_FOR_HATARI
static void REGPARAM2 none_put (uaecptr addr, uae_u32 v)
{
}
static uae_u32 REGPARAM2 ones_get (uaecptr addr)
{
	return 0xffffffff;
}

addrbank *get_sub_bank(uaecptr *paddr)
{
	int i;
	uaecptr addr = *paddr;
	addrbank *ab = &get_mem_bank(addr);
	struct addrbank_sub *sb = ab->sub_banks;
	if (!sb)
		return &dummy_bank;
	for (i = 0; sb[i].bank; i++) {
		int offset = addr & 65535;
		if (offset < sb[i + 1].offset) {
			uae_u32 mask = sb[i].mask;
			uae_u32 maskval = sb[i].maskval;
			if ((offset & mask) == maskval) {
				*paddr = addr - sb[i].suboffset;
				return sb[i].bank;
			}
		}
	}
	*paddr = addr - sb[i - 1].suboffset;
	return sb[i - 1].bank;
}
uae_u32 REGPARAM3 sub_bank_lget (uaecptr addr) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	return ab->lget(addr);
}
uae_u32 REGPARAM3 sub_bank_wget(uaecptr addr) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	return ab->wget(addr);
}
uae_u32 REGPARAM3 sub_bank_bget(uaecptr addr) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	return ab->bget(addr);
}
void REGPARAM3 sub_bank_lput(uaecptr addr, uae_u32 v) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	ab->lput(addr, v);
}
void REGPARAM3 sub_bank_wput(uaecptr addr, uae_u32 v) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	ab->wput(addr, v);
}
void REGPARAM3 sub_bank_bput(uaecptr addr, uae_u32 v) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	
/* last accessed on the bus */ab->bput(addr, v);
}
uae_u32 REGPARAM3 sub_bank_lgeti(uaecptr addr) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	return ab->lgeti(addr);
}
uae_u32 REGPARAM3 sub_bank_wgeti(uaecptr addr) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	return ab->wgeti(addr);
}
int REGPARAM3 sub_bank_check(uaecptr addr, uae_u32 size) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	return ab->check(addr, size);
}
uae_u8 *REGPARAM3 sub_bank_xlate(uaecptr addr) REGPARAM
{
	addrbank *ab = get_sub_bank(&addr);
	return ab->xlateaddr(addr);
}
#endif


/* **** This memory bank only generates bus errors **** */

static uae_u32 REGPARAM3 BusErrMem_lget(uaecptr addr)
{
	print_illegal_counted("Bus error lget", addr);

	M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, 0);
	return 0;
}

static uae_u32 REGPARAM3 BusErrMem_wget(uaecptr addr)
{
	print_illegal_counted("Bus error wget", addr);

	M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, 0);
	return 0;
}

static uae_u32 REGPARAM3 BusErrMem_bget(uaecptr addr)
{
	print_illegal_counted("Bus error bget", addr);

	M68000_BusError(addr, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
	return 0;
}

static void REGPARAM3 BusErrMem_lput(uaecptr addr, uae_u32 l)
{
	print_illegal_counted("Bus error lput", addr);

	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, l);
}

static void REGPARAM3 BusErrMem_wput(uaecptr addr, uae_u32 w)
{
	print_illegal_counted("Bus error wput", addr);

	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, w);
}

static void REGPARAM3 BusErrMem_bput(uaecptr addr, uae_u32 b)
{
	print_illegal_counted("Bus error bput", addr);

	M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, b);
}

static int REGPARAM3 BusErrMem_check(uaecptr addr, uae_u32 size)
{
	if (illegal_mem)
		write_log ("Bus error check at %08lx\n", (long)addr);

	return 0;
}

static uae_u8 * REGPARAM3 BusErrMem_xlate (uaecptr addr)
{
	write_log("Your Atari program just did something terribly stupid:"
	          " BusErrMem_xlate($%x)\n", addr);

	/*M68000_BusError(addr);*/
	return STmem_xlate(addr);  /* So we don't crash. */
}


/* **** ST RAM memory **** */

/*static uae_u8 *STmemory;*/
#define STmemory STRam

static uae_u32 REGPARAM3 STmem_lget(uaecptr addr)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	return do_get_mem_long(STmemory + addr);
}

static uae_u32 REGPARAM3 STmem_wget(uaecptr addr)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	return do_get_mem_word(STmemory + addr);
}

static uae_u32 REGPARAM3 STmem_bget(uaecptr addr)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	return STmemory[addr];
}

static void REGPARAM3 STmem_lput(uaecptr addr, uae_u32 l)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	do_put_mem_long(STmemory + addr, l);
}

static void REGPARAM3 STmem_wput(uaecptr addr, uae_u32 w)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	do_put_mem_word(STmemory + addr, w);
}

static void REGPARAM3 STmem_bput(uaecptr addr, uae_u32 b)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	STmemory[addr] = b;
}

static int REGPARAM3 STmem_check(uaecptr addr, uae_u32 size)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	return (addr + size) <= STmem_size;
}

static uae_u8 * REGPARAM3 STmem_xlate(uaecptr addr)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	return STmemory + addr;
}

/* Same functions for ST RAM but with MMU/MCU enabled to translate addresses */

static uae_u32 REGPARAM3 STmem_lget_MMU(uaecptr addr)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	addr = STMemory_MMU_Translate_Addr ( addr );
	return do_get_mem_long(STmemory + addr);
}

static uae_u32 REGPARAM3 STmem_wget_MMU(uaecptr addr)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	addr = STMemory_MMU_Translate_Addr ( addr );
	return do_get_mem_word(STmemory + addr);
}

static uae_u32 REGPARAM3 STmem_bget_MMU(uaecptr addr)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	addr = STMemory_MMU_Translate_Addr ( addr );
	return STmemory[addr];
}

static void REGPARAM3 STmem_lput_MMU(uaecptr addr, uae_u32 l)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	addr = STMemory_MMU_Translate_Addr ( addr );
	do_put_mem_long(STmemory + addr, l);
}

static void REGPARAM3 STmem_wput_MMU(uaecptr addr, uae_u32 w)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	addr = STMemory_MMU_Translate_Addr ( addr );
	do_put_mem_word(STmemory + addr, w);
}

static void REGPARAM3 STmem_bput_MMU(uaecptr addr, uae_u32 b)
{
	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;
	addr = STMemory_MMU_Translate_Addr ( addr );
	STmemory[addr] = b;
}


/*
 * **** ST RAM system memory ****
 * We need a separate mem bank for this region since the first 0x800 bytes on
 * the ST can only be accessed in supervisor mode. Note that the very first
 * 8 bytes of the ST memory are also a mirror of the TOS ROM, so they are write
 * protected!
 */
static uae_u32 REGPARAM3 SysMem_lget(uaecptr addr)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	/* Only CPU will trigger bus error if bit S=0, not the blitter or the debugger */
	if(addr < 0x800 && !is_super_access(true) && BusMode == BUS_MODE_CPU)
	{
		M68000_BusError(addr_in, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}

	return do_get_mem_long(STmemory + addr);
}

static uae_u32 REGPARAM3 SysMem_wget(uaecptr addr)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	/* Only CPU will trigger bus error if bit S=0, not the blitter or the debugger */
	if(addr < 0x800 && !is_super_access(true) && BusMode == BUS_MODE_CPU)
	{
		M68000_BusError(addr_in, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}

	return do_get_mem_word(STmemory + addr);
}

static uae_u32 REGPARAM3 SysMem_bget(uaecptr addr)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	/* Only CPU will trigger bus error if bit S=0, not the blitter or the debugger */
	if(addr < 0x800 && !is_super_access(true) && BusMode == BUS_MODE_CPU)
	{
		M68000_BusError(addr_in, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}

	return STmemory[addr];
}

static void REGPARAM3 SysMem_lput(uaecptr addr, uae_u32 l)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	if(addr < 0x8 || (addr < 0x800 && !is_super_access(false)))
	{
		M68000_BusError(addr_in, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, l);
		return;
	}

	do_put_mem_long(STmemory + addr, l);
}

static void REGPARAM3 SysMem_wput(uaecptr addr, uae_u32 w)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	/* Only CPU will trigger bus error if bit S=0, not the blitter */
	if (addr < 0x8 || (addr < 0x800 && !is_super_access(false)))
	{
		if (BusMode == BUS_MODE_CPU)
		{
			M68000_BusError(addr_in, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, w);
			return;
		}
		/* If blitter writes < 0x8 then it should be ignored, else the write should be made */
		else if ( ( BusMode == BUS_MODE_BLITTER ) && ( addr < 0x8 ) )
		{
			return;
		}
	}

	do_put_mem_word(STmemory + addr, w);
}

static void REGPARAM3 SysMem_bput(uaecptr addr, uae_u32 b)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	if (addr < 0x8 || (addr < 0x800 && !is_super_access(false)))
	{
		M68000_BusError(addr_in, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, b);
		return;
	}

	STmemory[addr] = b;
}

/* Same functions for ST RAM system but with MMU/MCU enabled to translate addresses */

static uae_u32 REGPARAM3 SysMem_lget_MMU(uaecptr addr)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	if (addr < 0x800 && !is_super_access(true))
	{
		M68000_BusError(addr_in, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}

	addr = STMemory_MMU_Translate_Addr ( addr );
	return do_get_mem_long(STmemory + addr);
}

static uae_u32 REGPARAM3 SysMem_wget_MMU(uaecptr addr)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	/* Only CPU will trigger bus error if bit S=0, not the blitter */
	if (addr < 0x800 && !is_super_access(true) && BusMode == BUS_MODE_CPU)
	{
		M68000_BusError(addr_in, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}

	addr = STMemory_MMU_Translate_Addr ( addr );
	return do_get_mem_word(STmemory + addr);
}

static uae_u32 REGPARAM3 SysMem_bget_MMU(uaecptr addr)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	if (addr < 0x800 && !is_super_access(true))
	{
		M68000_BusError(addr_in, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return 0;
	}

	addr = STMemory_MMU_Translate_Addr ( addr );
	return STmemory[addr];
}

static void REGPARAM3 SysMem_lput_MMU(uaecptr addr, uae_u32 l)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	if (addr < 0x8 || (addr < 0x800 && !is_super_access(false)))
	{
		M68000_BusError(addr_in, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, l);
		return;
	}

	addr = STMemory_MMU_Translate_Addr ( addr );
	do_put_mem_long(STmemory + addr, l);
}

static void REGPARAM3 SysMem_wput_MMU(uaecptr addr, uae_u32 w)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	/* Only CPU will trigger bus error if bit S=0, not the blitter */
	if (addr < 0x8 || (addr < 0x800 && !is_super_access(false)))
	{
		if ( BusMode == BUS_MODE_CPU )
		{
			M68000_BusError(addr_in, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, w);
			return;
		}
		/* If blitter writes < 0x8 then it should be ignored, else the write should be made */
		else if ( ( BusMode == BUS_MODE_BLITTER ) && ( addr < 0x8 ) )
		{
			return;
		}
	}

	addr = STMemory_MMU_Translate_Addr ( addr );
	do_put_mem_word(STmemory + addr, w);
}

static void REGPARAM3 SysMem_bput_MMU(uaecptr addr, uae_u32 b)
{
	uaecptr addr_in = addr;

	addr -= STmem_start & STmem_mask;
	addr &= STmem_mask;

	if (addr < 0x8 || (addr < 0x800 && !is_super_access(false)))
	{
		M68000_BusError(addr_in, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, b);
		return;
	}

	addr = STMemory_MMU_Translate_Addr ( addr );
	STmemory[addr] = b;
}


/*
 * **** Void memory ****
 * Between the ST-RAM end and the 4 MB barrier, there is a void memory space:
 * Reading depends on current activity on the bus and writing does nothing at all.
 *
 * [NP] : When there's no memory, it will return the latest data that was read on the bus.
 * In many cases, this will return the word that was just read in the 68000's
 * prefetch register to decode the next opcode (tested on a real STF)
 *
 * STF : this seems to always return the last data word on the bus read by the CPU (tested on real STF)
 * STE : result seems also related to what the shifter displays, it can be data read by the CPU or by
 *       the shifter ; for now do the same as STF [TODO improve this ? No program are known to depend on it]
 */

static uae_u32 REGPARAM3 VoidMem_lget(uaecptr addr)
{
    return ( regs.db<<16 ) | regs.db;
}

static uae_u32 REGPARAM3 VoidMem_wget(uaecptr addr)
{
    return regs.db;
}

static uae_u32 REGPARAM3 VoidMem_bget(uaecptr addr)
{
    return regs.db;
}

static void REGPARAM3 VoidMem_lput(uaecptr addr, uae_u32 l)
{
}

static void REGPARAM3 VoidMem_wput(uaecptr addr, uae_u32 w)
{
}

static void REGPARAM3 VoidMem_bput (uaecptr addr, uae_u32 b)
{
}

static int REGPARAM3 VoidMem_check(uaecptr addr, uae_u32 size)
{
    if (illegal_mem)
	write_log ("Void memory check at %08lx\n", (long)addr);

    return 0;
}

static uae_u8 * REGPARAM3 VoidMem_xlate (uaecptr addr)
{
    write_log("Your Atari program just did something terribly stupid:"
              " VoidMem_xlate($%x)\n", addr);

    return STmem_xlate(addr);  /* So we don't crash. */
}


/* **** TT fast memory **** */

uae_u8 *TTmemory;

static uae_u32 REGPARAM3 TTmem_lget(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return do_get_mem_long(TTmemory + addr);
}

static uae_u32 REGPARAM3 TTmem_wget(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return do_get_mem_word(TTmemory + addr);
}

static uae_u32 REGPARAM3 TTmem_bget(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return TTmemory[addr];
}

static void REGPARAM3 TTmem_lput(uaecptr addr, uae_u32 l)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    do_put_mem_long(TTmemory + addr, l);
}

static void REGPARAM3 TTmem_wput(uaecptr addr, uae_u32 w)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    do_put_mem_word(TTmemory + addr, w);
}

static void REGPARAM3 TTmem_bput(uaecptr addr, uae_u32 b)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    TTmemory[addr] = b;
}

static int REGPARAM3 TTmem_check(uaecptr addr, uae_u32 size)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return (addr + size) <= TTmem_size;
}

static uae_u8 * REGPARAM3 TTmem_xlate(uaecptr addr)
{
    addr -= TTmem_start & TTmem_mask;
    addr &= TTmem_mask;
    return TTmemory + addr;
}


/* **** ROM memory **** */

uae_u8 *ROMmemory;

static uae_u32 REGPARAM3 ROMmem_lget(uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return do_get_mem_long(ROMmemory + addr);
}

static uae_u32 REGPARAM3 ROMmem_wget(uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return do_get_mem_word(ROMmemory + addr);
}

static uae_u32 REGPARAM3 ROMmem_bget(uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return ROMmemory[addr];
}

static void REGPARAM3 ROMmem_lput(uaecptr addr, uae_u32 l)
{
    print_illegal_counted("Illegal ROMmem lput", addr);

    M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, l);
}

static void REGPARAM3 ROMmem_wput(uaecptr addr, uae_u32 w)
{
    print_illegal_counted("Illegal ROMmem wput", addr);

    M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, w);
}

static void REGPARAM3 ROMmem_bput(uaecptr addr, uae_u32 b)
{
    print_illegal_counted("Illegal ROMmem bput", addr);

    M68000_BusError(addr, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, b);
}

static int REGPARAM3 ROMmem_check(uaecptr addr, uae_u32 size)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return (addr + size) <= ROMmem_size;
}

static uae_u8 * REGPARAM3 ROMmem_xlate(uaecptr addr)
{
    addr -= ROMmem_start & ROMmem_mask;
    addr &= ROMmem_mask;
    return ROMmemory + addr;
}


/* IDE controller IO memory */
/* see also ide.c */

static uae_u8 *IdeMemory;

static int REGPARAM3 IdeMem_check(uaecptr addr, uae_u32 size)
{
    addr -= IdeMem_start;
    addr &= IdeMem_mask;
    return (addr + size) <= IdeMem_size;
}

static uae_u8 * REGPARAM3 IdeMem_xlate(uaecptr addr)
{
    addr -= IdeMem_start;
    addr &= IdeMem_mask;
    return IdeMemory + addr;
}


/* Hardware IO memory */
/* see also ioMem.c */

uae_u8 *IOmemory;

static int REGPARAM3 IOmem_check(uaecptr addr, uae_u32 size)
{
    addr -= IOmem_start;
    addr &= IOmem_mask;
    return (addr + size) <= IOmem_size;
}

static uae_u8 * REGPARAM3 IOmem_xlate(uaecptr addr)
{
    addr -= IOmem_start;
    addr &= IOmem_mask;
    return IOmemory + addr;
}



/* **** Address banks **** */

/*
 * NOTE : if ABFLAG_DIRECTACCESS is set for a bank we don't use
 * the functions get/put/xlate/check defined for this bank but
 * we use the more generic memory_get/memory_put/mgemory_get_real/memory_valid_address
 * functions which bypass the common memory handlers and are slightly faster.
 * This flag can only be set for memory region where content is "fixed" (ie RAM or ROM)
 */

addrbank dummy_bank =
{
    dummy_lget, dummy_wget, dummy_bget,
    dummy_lput, dummy_wput, dummy_bput,
    dummy_xlate, dummy_check, NULL, NULL, NULL,
    dummy_lget, dummy_wget, ABFLAG_NONE
};

static addrbank BusErrMem_bank =
{
    BusErrMem_lget, BusErrMem_wget, BusErrMem_bget,
    BusErrMem_lput, BusErrMem_wput, BusErrMem_bput,
    BusErrMem_xlate, BusErrMem_check, NULL, "bus_err_mem" , "BusError memory",
    BusErrMem_lget, BusErrMem_wget, ABFLAG_NONE
};

static addrbank STmem_bank =
{
    STmem_lget, STmem_wget, STmem_bget,
    STmem_lput, STmem_wput, STmem_bput,
    STmem_xlate, STmem_check, NULL, "st_mem" , "ST memory",
    STmem_lget, STmem_wget, ABFLAG_RAM | ABFLAG_DIRECTACCESS
};

static addrbank SysMem_bank =
{
    SysMem_lget, SysMem_wget, SysMem_bget,
    SysMem_lput, SysMem_wput, SysMem_bput,
    STmem_xlate, STmem_check, NULL, "sys_mem" , "Sys memory",
    SysMem_lget, SysMem_wget, ABFLAG_RAM
};

static addrbank STmem_bank_MMU =			/* similar to STmem_bank with MMU/MCU enabled */
{
    STmem_lget_MMU, STmem_wget_MMU, STmem_bget_MMU,
    STmem_lput_MMU, STmem_wput_MMU, STmem_bput_MMU,
    STmem_xlate, STmem_check, NULL, "st_mem" , "ST memory + MMU",
    STmem_lget_MMU, STmem_wget_MMU, ABFLAG_RAM
};

static addrbank SysMem_bank_MMU =			/* similar to STmem_bank with MMU/MCU enabled */
{
    SysMem_lget_MMU, SysMem_wget_MMU, SysMem_bget_MMU,
    SysMem_lput_MMU, SysMem_wput_MMU, SysMem_bput_MMU,
    STmem_xlate, STmem_check, NULL, "sys_mem" , "Sys memory + MMU",
    SysMem_lget_MMU, SysMem_wget_MMU, ABFLAG_RAM
};

static addrbank VoidMem_bank =
{
    VoidMem_lget, VoidMem_wget, VoidMem_bget,
    VoidMem_lput, VoidMem_wput, VoidMem_bput,
    VoidMem_xlate, VoidMem_check, NULL, "void_mem" , "Void memory",
    VoidMem_lget, VoidMem_wget, ABFLAG_NONE
};

static addrbank TTmem_bank =
{
    TTmem_lget, TTmem_wget, TTmem_bget,
    TTmem_lput, TTmem_wput, TTmem_bput,
    TTmem_xlate, TTmem_check, NULL, "tt_mem" , "TT memory",
    TTmem_lget, TTmem_wget, ABFLAG_RAM | ABFLAG_DIRECTACCESS	/* NP TODO : use ABFLAG_RAM_TT for non DMA RAM */
};

static addrbank ROMmem_bank =
{
    ROMmem_lget, ROMmem_wget, ROMmem_bget,
    ROMmem_lput, ROMmem_wput, ROMmem_bput,
    ROMmem_xlate, ROMmem_check, NULL, "rom_mem" , "ROM memory",
    ROMmem_lget, ROMmem_wget, ABFLAG_ROM | ABFLAG_DIRECTACCESS
};

static addrbank IdeMem_bank =
{
    Ide_Mem_lget, Ide_Mem_wget, Ide_Mem_bget,
    Ide_Mem_lput, Ide_Mem_wput, Ide_Mem_bput,
    IdeMem_xlate, IdeMem_check, NULL, "ide_mem" , "IDE memory",
    Ide_Mem_lget, Ide_Mem_wget, ABFLAG_IO
};

static addrbank IOmem_bank =
{
    IoMem_lget, IoMem_wget, IoMem_bget,
    IoMem_lput, IoMem_wput, IoMem_bput,
    IOmem_xlate, IOmem_check, NULL, "io_mem" , "IO memory",
    IoMem_lget, IoMem_wget, ABFLAG_IO
};


static void set_direct_memory(addrbank *ab)
{
	if (!(ab->flags & ABFLAG_DIRECTACCESS))
		return;
	ab->baseaddr_direct_r = ab->baseaddr;
	if (!(ab->flags & ABFLAG_ROM))
		ab->baseaddr_direct_w = ab->baseaddr;
}

#ifndef NATMEM_OFFSET
//extern uae_u8 *natmem_offset, *natmem_offset_end;

bool mapped_malloc (addrbank *ab)
{
	ab->startmask = ab->start;
	ab->startaccessmask = ab->start & ab->mask;
	ab->baseaddr = xcalloc (uae_u8, ab->reserved_size + 4);
	ab->allocated_size =  ab->baseaddr != NULL ? ab->reserved_size : 0;
	ab->baseaddr_direct_r = NULL;
	ab->baseaddr_direct_w = NULL;
	ab->flags &= ~ABFLAG_MAPPED;
	set_direct_memory(ab);
	return ab->baseaddr != NULL; 
}

void mapped_free (addrbank *ab)
{
	xfree(ab->baseaddr);
	ab->flags &= ~ABFLAG_MAPPED;
	ab->allocated_size = 0;
	ab->baseaddr = NULL; 
}

#else

#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/mman.h>

shmpiece *shm_start;

static void dumplist (void)
{
	shmpiece *x = shm_start;
	write_log (_T("Start Dump:\n"));
	while (x) {
		write_log (_T("this=%p,Native %p,id %d,prev=%p,next=%p,size=0x%08x\n"),
			x, x->native_address, x->id, x->prev, x->next, x->size);
		x = x->next;
	}
	write_log (_T("End Dump:\n"));
}

static shmpiece *find_shmpiece (uae_u8 *base, bool safe)
{
	shmpiece *x = shm_start;

	while (x && x->native_address != base)
		x = x->next;
	if (!x) {
#ifndef WINUAE_FOR_HATARI
		if (safe || bogomem_aliasing)
#else
		if (safe)
#endif
			return 0;
		write_log (_T("NATMEM: Failure to find mapping at %08lx, %p\n"), base - NATMEM_OFFSET, base);
		nocanbang ();
		return 0;
	}
	return x;
}

static void delete_shmmaps (uae_u32 start, uae_u32 size)
{
	if (!needmman ())
		return;

	while (size) {
		uae_u8 *base = mem_banks[bankindex (start)]->baseaddr;
		if (base) {
			shmpiece *x;
			//base = ((uae_u8*)NATMEM_OFFSET)+start;

			x = find_shmpiece (base, true);
			if (!x)
				return;

			if (x->size > size) {
				if (isdirectjit ())
					write_log (_T("NATMEM WARNING: size mismatch mapping at %08x (size %08x, delsize %08x)\n"),start,x->size,size);
				size = x->size;
			}

			uae_shmdt (x->native_address);
			size -= x->size;
			start += x->size;
			if (x->next)
				x->next->prev = x->prev;	/* remove this one from the list */
			if (x->prev)
				x->prev->next = x->next;
			else
				shm_start = x->next;
			xfree (x);
		} else {
			size -= 0x10000;
			start += 0x10000;
		}
	}
}

static void add_shmmaps (uae_u32 start, addrbank *what)
{
	shmpiece *x = shm_start;
	shmpiece *y;
	uae_u8 *base = what->baseaddr;

	if (!needmman ())
		return;

	if (!base)
		return;

	if (what->jit_read_flag && what->jit_write_flag)
		return;

	x = find_shmpiece (base, false);
	if (!x)
		return;

	y = xmalloc (shmpiece, 1);
	*y = *x;
	base = ((uae_u8 *) NATMEM_OFFSET) + start;
	y->native_address = (uae_u8*)uae_shmat (what, y->id, base, 0, NULL);
	if (y->native_address == (void *) -1) {
		write_log (_T("NATMEM: Failure to map existing at %08x (%p)\n"), start, base);
		dumplist ();
		nocanbang ();
		return;
	}
	y->next = shm_start;
	y->prev = NULL;
	if (y->next)
		y->next->prev = y;
	shm_start = y;
}

#define MAPPED_MALLOC_DEBUG 0

bool mapped_malloc (addrbank *ab)
{
	int id;
	void *answer;
	shmpiece *x;
	bool rtgmem = (ab->flags & ABFLAG_RTG) != 0;
	static int recurse;

	if (ab->allocated_size) {
		write_log(_T("mapped_malloc with memory bank '%s' already allocated!?\n"), ab->name);
	}
	ab->allocated_size = 0;
	ab->baseaddr_direct_r = NULL;
	ab->baseaddr_direct_w = NULL;
	ab->flags &= ~ABFLAG_MAPPED;

	if (ab->label && ab->label[0] == '*') {
		if (ab->start == 0 || ab->start == 0xffffffff) {
			write_log(_T("mapped_malloc(*) without start address!\n"));
			return false;
		}
	}

	struct uae_mman_data md = { 0 };
	uaecptr start = ab->start;
	if (uae_mman_info(ab, &md)) {
		start = md.start;
	}
	ab->startmask = start;
	ab->startaccessmask = start & ab->mask;
	if (!md.directsupport || (ab->flags & ABFLAG_ALLOCINDIRECT)) {
		if (!(ab->flags & ABFLAG_ALLOCINDIRECT)) {
			if (canbang) {
				write_log(_T("JIT direct switched off: %s\n"), ab->name);
			}
			nocanbang();
		}
		ab->flags &= ~ABFLAG_DIRECTMAP;
		if (ab->flags & ABFLAG_NOALLOC) {
			ab->allocated_size = ab->reserved_size;
#if MAPPED_MALLOC_DEBUG
			write_log(_T("mapped_malloc noalloc %s\n"), ab->name);
#endif
			return true;
		}
		ab->baseaddr = xcalloc (uae_u8, ab->reserved_size + 4);
		if (ab->baseaddr) {
			// fill end of ram with ILLEGAL to catch direct PC falling out of RAM.
			put_long_host(ab->baseaddr + ab->reserved_size, 0x4afc4afc);
			ab->allocated_size = ab->reserved_size;
		}
		set_direct_memory(ab);
#if MAPPED_MALLOC_DEBUG
		write_log(_T("mapped_malloc nodirect %s %p\n"), ab->name, ab->baseaddr);
#endif
		return ab->baseaddr != NULL;
	}

	id = uae_shmget (UAE_IPC_PRIVATE, ab, 0x1ff);
	if (id == -1) {
		nocanbang ();
		if (recurse)
			return NULL;
		recurse++;
		mapped_malloc (ab);
		recurse--;
		return ab->baseaddr != NULL;
	}
	if (!(ab->flags & ABFLAG_NOALLOC)) {
		answer = uae_shmat (iab, id, NULL, 0, &md);
		uae_shmctl (id, UAE_IPC_RMID, NULL);
	} else {
		write_log(_T("MMAN: mapped_malloc using existing baseaddr %p\n"), ab->baseaddr);
		answer = ab->baseaddr;
	}
	if (answer != (void *) -1) {
		x = xmalloc (shmpiece, 1);
		x->native_address = (uae_u8*)answer;
		x->id = id;
		x->size = ab->reserved_size;
		x->name = ab->label;
		x->next = shm_start;
		x->prev = NULL;
		if (x->next)
			x->next->prev = x;
		shm_start = x;
		ab->baseaddr = x->native_address;
		if (ab->baseaddr) {
			if (md.hasbarrier) {
				// fill end of ram with ILLEGAL to catch direct PC falling out of RAM.
				put_long_host(ab->baseaddr + ab->reserved_size, 0x4afc4afc);
				ab->barrier = true;
			}
			ab->allocated_size = ab->reserved_size;
		}
		ab->flags |= ABFLAG_DIRECTMAP;
		set_direct_memory(ab);
#if MAPPED_MALLOC_DEBUG
		write_log(_T("mapped_malloc direct %s %p\n"), ab->name, ab->baseaddr);
#endif
		return ab->baseaddr != NULL;
	}
	if (recurse)
		return NULL;
	nocanbang ();
	recurse++;
	mapped_malloc (ab);
	recurse--;
	set_direct_memory(ab);
#if MAPPED_MALLOC_DEBUG
	write_log(_T("mapped_malloc indirect %s %p\n"), ab->name, ab->baseaddr);
#endif
	return ab->baseaddr != NULL;}
}

#endif

static void init_mem_banks (void)
{
	// unsigned so i << 16 won't overflow to negative when i >= 32768
	for (unsigned int i = 0; i < MEMORY_BANKS; i++)
		put_mem_bank (i << 16, &dummy_bank, 0);
#ifdef NATMEM_OFFSET
	delete_shmmaps (0, 0xFFFF0000);
#endif
}


static void init_bank (addrbank *ab , uae_u32 size)
{
	ab->allocated_size = size;
	ab->startmask = ab->start;
	ab->startaccessmask = ab->start & ab->mask;
	ab->baseaddr_direct_r = NULL;
	ab->baseaddr_direct_w = NULL;
	set_direct_memory ( ab );
}


#ifdef WINUAE_FOR_HATARI
/*
 * Check if an address points to a memory region that causes bus error
 * Returns true if region gives bus error
 */
bool memory_region_bus_error ( uaecptr addr )
{
	return mem_banks[bankindex(addr)] == &BusErrMem_bank;
}

/*
 * Check if an address points to the IO memory region
 * Returns true if it's the case
 */
bool memory_region_iomem ( uaecptr addr )
{
	return mem_banks[bankindex(addr)] == &IOmem_bank;
}
#endif


/*
 * Initialize some extra parameters for the memory banks in CE mode
 * By default, we set all banks to CHIP16 and not cacheable
 *
 * Possible values for ce_banktype :
 *  CE_MEMBANK_CHIP16	shared between CPU and DMA, bus width = 16 bits
 *  CE_MEMBANK_CHIP32	shared between CPU and DMA, bus width = 32 bits (AGA chipset)
 *  CE_MEMBANK_FAST16	accessible only to the CPU,  bus width = 16 bits
 *  CE_MEMBANK_FAST32 	accessible only to the CPU,  bus width = 32 bits
 *  CE_MEMBANK_CIA	Amiga only, for CIA chips
 *
 * Possible values for ce_cachable :
 *  bit 0 : cacheable yes/no for data
 *  bit 1 : burst mode allowed when caching data yes/no
 *  bit 7 : cacheable yes/no for instructions
 *  bit 6 : burst mode allowed when caching instructions yes/no
 */
static void init_ce_banks (void)
{
	/* Default to CHIP16 */
	memset (ce_banktype, CE_MEMBANK_CHIP16, sizeof ce_banktype);

	/* Default to not cacheable */
	memset (ce_cachable, 0, sizeof ce_cachable);
}


/*
 * For CE mode, set banktype and cacheable for a memory region
 */
static void fill_ce_banks (int start, int size, int banktype, int cachable )
{
	int i;

	for ( i=start ; i<start+size ; i++ )
	{
		ce_banktype[ i ] = banktype;
		ce_cachable[ i ] = cachable;
	}
}


/*
 * Initialize the standard RAM memory banks
 *   - Unmodified STF/STE can have a max of 4 MB, but we can allow up to 14 MB
 *     if RAM detection code is bypassed in the ROM (see tos.c)
 *   - Falcon/TT can have up to 14 MB
 * Depending on the current config, we enable MMU/MCU to translate addresses on
 * each access (eg during boot when TOS checks RAM size)
 *
 * Most of the time, MMU bank size will be the same as RAM bank size and we use the "usual" non-MMU version
 * which considers logical address == physical address and gives faster result
 *
 * See stMemory.c for the address translation used by the MMU/MCU
 */
void memory_map_Standard_RAM ( uint32_t MMU_Bank0_Size , uint32_t MMU_Bank1_Size )
{
	Log_Printf(LOG_DEBUG, "memory_map_Standard_RAM total=%d ram0=%d ram1=%d mmu0=%d mmu1=%d\n", STmem_size, RAM_Bank0_Size, RAM_Bank1_Size, MMU_Bank0_Size, MMU_Bank1_Size);

	/* Between 0 and 4MB barrier, we default to void space */
	map_banks_ce(&VoidMem_bank, 0x00, 0x400000 >> 16, 0, CE_MEMBANK_CHIP16, CE_MEMBANK_NOT_CACHABLE);

	/* Space between 4MB barrier and TOS ROM causes a bus error */
	map_banks_ce(&BusErrMem_bank, 0x400000 >> 16, 0xA0, 0 , CE_MEMBANK_CHIP16, CE_MEMBANK_NOT_CACHABLE);

	/* Now map main ST RAM, overwriting the void and bus error regions if necessary */
	/*  - Map the ST system RAM from 0 to 0x10000 (required for supervisor check between 0x0 and 0x800) */
	/*  - Map rest of the ST RAM from 0x10000 to STmem_size */
	/* If possible/needed we enable MMU/MCU translation for maximum accuracy (when sizes of banks differ between MMU and RAM) */
	if ( ( Config_IsMachineST() || Config_IsMachineSTE() )
	  && !ConfigureParams.System.bFastBoot
	  && !ConfigureParams.Screen.bUseExtVdiResolutions
	  && STmem_size <= 0x400000
	  && ( ( MMU_Bank0_Size != RAM_Bank0_Size ) || ( ( RAM_Bank1_Size > 0 ) && ( MMU_Bank1_Size != RAM_Bank1_Size ) ) )
	  && !bRamTosImage
	  )
	{
		/* We map memory according to the logical MMU configuration and we will translate addresses on each memory access. */
		/* RAM bank 0 can never be empty, but RAM bank 1 can be empty. If there's no RAM bank 1, then we must */
		/* use 'Void' region directly, we don't map it to STmem_bank_MMU */
		Log_Printf(LOG_DEBUG, "memory_map_Standard_RAM - enable MMU %d %d %d\n",
		           STmem_size, MMU_Bank0_Size, MMU_Bank1_Size);

		/* Map RAM bank 0 to MMU bank 0 */
		map_banks_ce(&SysMem_bank_MMU, 0x00, 0x10000 >> 16, 0, CE_MEMBANK_CHIP16, CACHE_ENABLE_BOTH);
		map_banks_ce(&STmem_bank_MMU, 0x10000 >> 16, ( MMU_Bank0_Size - 0x10000 ) >> 16,
			0, CE_MEMBANK_CHIP16, CACHE_ENABLE_BOTH);

		/* If RAM bank 1 exists, we map it after MMU bank 0 ; else we keep void region */
		if ( RAM_Bank1_Size > 0 )
			map_banks_ce(&STmem_bank_MMU, MMU_Bank0_Size >> 16, MMU_Bank1_Size >> 16,
				0, CE_MEMBANK_CHIP16, CACHE_ENABLE_BOTH);

		/* [NP] There's a special case when bank0=128 and bank1=2048 : addresses between $40000 and $80000 */
		/* are returning 'void' region too, which creates a "hole" in the memory mapping */
		/* (this might be a bug / forgotten case by Atari in the STF non-IMP MMU as this memory */
		/* configuration was certainly never used in real machines) */
		if ( ( MMU_Bank0_Size == MEM_BANK_SIZE_128 ) && ( MMU_Bank1_Size == MEM_BANK_SIZE_2048 ) )
		{
			map_banks_ce(&VoidMem_bank, 0x40000 >> 16, 0x40000 >> 16, 0, CE_MEMBANK_CHIP16, CE_MEMBANK_NOT_CACHABLE);
		}
	}
	else
	{
		/* Don't enable MMU address translation */
		map_banks_ce(&SysMem_bank, 0x00, 0x10000 >> 16, 0, CE_MEMBANK_CHIP16, CACHE_ENABLE_BOTH);
		map_banks_ce(&STmem_bank, 0x10000 >> 16, ( STmem_size - 0x10000 ) >> 16, 0, CE_MEMBANK_CHIP16, CACHE_ENABLE_BOTH);
	}
}


/*
 * Initialize all the memory banks
 */
void memory_init(uae_u32 NewSTMemSize, uae_u32 NewTTMemSize, uae_u32 NewRomMemStart)
{
	int addr;

	last_address_space_24 = ConfigureParams.System.bAddressSpace24;

	/* Round to next multiple of 65536 bytes */
	STmem_size = (NewSTMemSize + 65535) & 0xFFFF0000;
	TTmem_size = (NewTTMemSize + 65535) & 0xFFFF0000;

	//fprintf ( stderr , "memory_init: STmem_size=$%x, TTmem_size=$%x, ROM-Start=$%x,\n", STmem_size, TTmem_size, NewRomMemStart);
	/*write_log("memory_init: STmem_size=$%x, TTmem_size=$%x, ROM-Start=$%x,\n",
	            STmem_size, TTmem_size, NewRomMemStart);*/

#if ENABLE_SMALL_MEM

	/* Allocate memory for normal ST RAM. Note that we always allocate
	 * either 4 MiB, 8 MiB or the full 16 MiB, since the functions that
	 * might access the memory directly (via "DMA" like the shifter, see
	 * the Video_CopyScreenLine*() functions) might also try to access
	 * the memory beyond the end of the RAM in case the base address has
	 * been wrongly set up by the Atari program (the accesses are only
	 * limited by the value returned from DMA_MaskAddressHigh()). To
	 * compensate for reads beyond the end, we also add a "runaway ramp"
	 * buffer with the size of the maximum ST screen, so that the function
	 * Video_CopyScreenLineColor() should never go out of bounds. */
	int alloc_size = NUM_VISIBLE_LINE_PIXELS * NUM_VISIBLE_LINES / 2;
	if (STmem_size > 0x800000)
		alloc_size += 0x1000000;
	else if (STmem_size > 0x400000)
		alloc_size += 0x800000;
	else
		alloc_size += 0x400000;

	STmemory = malloc(alloc_size);
	if (!STmemory)
	{
		write_log ("virtual memory exhausted (STmemory)!\n");
		SDL_Quit();
		exit(1);
	}
	memset(STmemory, 0, alloc_size);

	/* Set up memory for ROM areas, IDE and IO memory space (0xE00000 - 0xFFFFFF) */
	if (alloc_size >= 0x1000000)
	{
		ROMmemory = STmemory + ROMmem_start;
	}
	else
	{
		ROMmemory = malloc(2*1024*1024);
		if (!ROMmemory)
		{
			fprintf(stderr, "Out of memory (ROM/IO mem)!\n");
			SDL_Quit();
			exit(1);
		}
	}

	IdeMemory = ROMmemory + 0x100000;
	IOmemory  = ROMmemory + 0x1f0000;

#else

	/* STmemory points to the 16 MiB STRam array, we just have to set up
	* the remaining pointers here: */
	ROMmemory = STRam + ROMmem_start;
	IdeMemory = STRam + IdeMem_start;
	IOmemory = STRam + IOmem_start;

#endif

	init_mem_banks();
	init_ce_banks();

	/* Set the infos about memory pointers for each mem bank, used for direct memory access in stMemory.c */
	STmem_bank.baseaddr = STmemory;
	STmem_bank.mask = STmem_mask;
	STmem_bank.start = STmem_start;
	init_bank ( &STmem_bank , STmem_size );

	SysMem_bank.baseaddr = STmemory;
	SysMem_bank.mask = STmem_mask;
	SysMem_bank.start = STmem_start;
	init_bank ( &SysMem_bank , STmem_size );

	STmem_bank_MMU.baseaddr = STmemory;
	STmem_bank_MMU.mask = STmem_mask;
	STmem_bank_MMU.start = STmem_start;
	init_bank ( &STmem_bank_MMU , STmem_size );

	SysMem_bank_MMU.baseaddr = STmemory;
	SysMem_bank_MMU.mask = STmem_mask;
	SysMem_bank_MMU.start = STmem_start;
	init_bank ( &SysMem_bank_MMU , STmem_size );

	dummy_bank.baseaddr = NULL;				/* No real memory allocated for this region */
	init_bank ( &dummy_bank , 0 );
	VoidMem_bank.baseaddr = NULL;			/* No real memory allocated for this region */
	init_bank ( &VoidMem_bank , 0 );
	BusErrMem_bank.baseaddr = NULL;			/* No real memory allocated for this region */
	init_bank ( &BusErrMem_bank , 0 );


	/* Map the standard RAM (Max is 4 MB on unmodified STF/STE) */
	memory_map_Standard_RAM ( MMU_Bank0_Size , MMU_Bank1_Size );


	/* Handle extra RAM on TT and Falcon starting at 0x1000000 and up to 0x80000000 */
	/* This requires the CPU to use 32 bit addressing */
	TTmemory = NULL;
	if (!ConfigureParams.System.bAddressSpace24)
	{
		/* If there's no Fast-RAM, region 0x01000000 - 0x80000000 (2047 MB) must return bus errors */
		map_banks_ce ( &BusErrMem_bank, TTmem_start >> 16, ( TTmem_end - TTmem_start ) >> 16, 0, CE_MEMBANK_CHIP16, CE_MEMBANK_NOT_CACHABLE);

		if (TTmem_size > 0)
		{
			TTmemory = (uae_u8 *)malloc ( TTmem_size );

			if (TTmemory != NULL)
			{
				/* 32 bit RAM for CPU only + cache/burst allowed */
				map_banks_ce ( &TTmem_bank, TTmem_start >> 16, TTmem_size >> 16, 0, CE_MEMBANK_FAST32, CACHE_ENABLE_ALL);
				TTmem_mask = 0xffffffff;
				TTmem_bank.baseaddr = TTmemory;
				TTmem_bank.mask = TTmem_mask;
				TTmem_bank.start = TTmem_start;
				init_bank ( &TTmem_bank , TTmem_size );
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
	if(NewRomMemStart == 0xFC0000)
	{
		map_banks_ce(&ROMmem_bank, 0xFC0000 >> 16, 0x3, 0, CE_MEMBANK_FAST16, CACHE_ENABLE_BOTH);	/* [NP] tested on real STF, no bus wait from ROM */
		map_banks_ce(&BusErrMem_bank, 0xE00000 >> 16, 0x10, 0, CE_MEMBANK_CHIP16, CE_MEMBANK_NOT_CACHABLE);
	}
	else if(NewRomMemStart == 0xE00000)
	{
		map_banks_ce(&ROMmem_bank, 0xE00000 >> 16, 0x10, 0, CE_MEMBANK_FAST16, CACHE_ENABLE_BOTH);	/* [NP] tested on real STF, no bus wait from ROM */
		map_banks_ce(&BusErrMem_bank, 0xFC0000 >> 16, 0x3, 0, CE_MEMBANK_CHIP16, CE_MEMBANK_NOT_CACHABLE);
	}
	else
	{
		write_log("Illegal ROM memory start!\n");
	}

	/* Cartridge memory: */
	map_banks_ce(&ROMmem_bank, 0xFA0000 >> 16, 0x2, 0, CE_MEMBANK_FAST16, CACHE_ENABLE_BOTH);		/* [NP] tested on real STF, no bus wait from cartridge */
	ROMmem_bank.baseaddr = ROMmemory;
	ROMmem_bank.mask = ROMmem_mask;
	ROMmem_bank.start = ROMmem_start;
	init_bank ( &ROMmem_bank , ROMmem_size );

	/* IO memory: */
	map_banks_ce(&IOmem_bank, IOmem_start>>16, 0x1, 0, CE_MEMBANK_FAST16, CE_MEMBANK_NOT_CACHABLE);	/* [NP] tested on real STF, no bus wait for IO memory */
	IOmem_bank.baseaddr = IOmemory;									/* except for some shifter registers */
	IOmem_bank.mask = IOmem_mask;
	IOmem_bank.start = IOmem_start;
	init_bank ( &IOmem_bank , IOmem_size );

	/* IDE controller memory region at 0xF00000 (for Falcon or can be forced for other machines, else it's a bus error region) */
	if (Ide_IsAvailable())
	{
		map_banks_ce(&IdeMem_bank, IdeMem_start >> 16, 0x1, 0, CE_MEMBANK_CHIP16, CE_MEMBANK_NOT_CACHABLE);	/* IDE controller on the Falcon */
		IdeMem_bank.baseaddr = IdeMemory;
		IdeMem_bank.mask = IdeMem_mask;
		IdeMem_bank.start = IdeMem_start ;
		init_bank ( &IdeMem_bank , IdeMem_size );
	}
	else
	{
		map_banks_ce(&BusErrMem_bank, IdeMem_start >> 16, 0x1, 0, CE_MEMBANK_CHIP16, CE_MEMBANK_NOT_CACHABLE);
	}

	/* Illegal memory regions cause a bus error on the ST: */
	map_banks_ce(&BusErrMem_bank, 0xF10000 >> 16, 0x9, 0, CE_MEMBANK_CHIP16, CE_MEMBANK_NOT_CACHABLE);

	/* According to the "Atari TT030 Hardware Reference Manual", the
	 * lowest 16 MBs (i.e. the 24-bit address space) are always mirrored
	 * to 0xff000000, so we remap memory 00xxxxxx to FFxxxxxx here. If not,
	 * we'd get some crashes when booting TOS 3 and 4 (e.g. both TOS 3.06
	 * and TOS 4.04 touch 0xffff8606 before setting up the MMU tables) */
	if (!ConfigureParams.System.bAddressSpace24)
	{
		/* Copy all 256 banks 0x0000-0x00FF to banks 0xFF00-0xFFFF */
		for ( addr=0x0 ; addr<=0x00ffffff ; addr+=0x10000 )
		{
			//printf ( "put mem %x %x\n" , addr , addr|0xff000000 );
			put_mem_bank ( 0xff000000|addr , &get_mem_bank ( addr ) , 0 );

			/* Copy the CE parameters */
			ce_banktype[ (0xff000000|addr)>>16 ] = ce_banktype[ addr>>16 ];
			ce_cachable[ (0xff000000|addr)>>16 ] = ce_cachable[ addr>>16 ];
		}
	}

	illegal_count = 0;
}


/*
 * Uninitialize the memory banks.
 */
void memory_uninit (void)
{
	/* Here, we free allocated memory from memory_init */
	if (TTmemory) {
		free(TTmemory);
		TTmemory = NULL;
	}

#if ENABLE_SMALL_MEM

	if (STmemory) {
		free(STmemory);
		STmemory = NULL;
	}

	if (STmem_size <= 0x800000 && ROMmemory) {
		free(ROMmemory);
	}
	ROMmemory = NULL;

#endif  /* ENABLE_SMALL_MEM */
}


static void map_banks2 (addrbank *bank, int start, int size, int realsize, int quick)
{
#ifndef WINUAE_FOR_HATARI
	int bnr, old;
	unsigned long int hioffs = 0, endhioffs = 0x100;
	addrbank *orgbank = bank;
	uae_u32 realstart = start;
#else
	int bnr;
	unsigned long int hioffs = 0, endhioffs = 0x100;
	NOWARN_UNUSED(uae_u32 realstart) = start;
#endif

//printf ( "map %x %x 24=%d\n" , start<<16 , size<<16 , currprefs.address_space_24 );
#ifndef WINUAE_FOR_HATARI
	if (quick <= 0)
		old = debug_bankchange (-1);
#endif
	flush_icache(3); /* Sure don't want to keep any old mappings around! */
#ifdef NATMEM_OFFSET
	if (!quick)
		delete_shmmaps (start << 16, size << 16);
#endif

	if (!realsize)
		realsize = size << 16;

	if ((size << 16) < realsize) {
		write_log (_T("Broken mapping, size=%x, realsize=%x\nStart is %x\n"),
			size, realsize, start);
	}

#ifdef JIT
	if ((bank->jit_read_flag | bank->jit_write_flag) & S_N_ADDR) {
		jit_n_addr_unsafe = 1;
	}
#endif

#ifndef ADDRESS_SPACE_24BIT
	if (start >= 0x100) {
		int real_left = 0;
		for (bnr = start; bnr < start + size; bnr++) {
			if (!real_left) {
				realstart = bnr;
				real_left = realsize >> 16;
#ifdef NATMEM_OFFSET
				if (!quick)
					add_shmmaps (realstart << 16, bank);
#endif
			}
			put_mem_bank (bnr << 16, bank, realstart << 16);
			real_left--;
		}
#ifndef WINUAE_FOR_HATARI
		if (quick <= 0)
			debug_bankchange (old);
#endif
		return;
	}
#endif
	if (last_address_space_24)
		endhioffs = 0x10000;
#ifdef ADDRESS_SPACE_24BIT
	endhioffs = 0x100;
#endif
	for (hioffs = 0; hioffs < endhioffs; hioffs += 0x100) {
		int real_left = 0;
		for (bnr = start; bnr < start + size; bnr++) {
			if (!real_left) {
				realstart = bnr + hioffs;
				real_left = realsize >> 16;
#ifdef NATMEM_OFFSET
				if (!quick)
					add_shmmaps (realstart << 16, bank);
#endif
			}
			put_mem_bank ((bnr + hioffs) << 16, bank, realstart << 16);

	  		/* Copy the CE parameters for bank/start */
			ce_banktype[ (bnr + hioffs) ] = ce_banktype[ start ];
			ce_cachable[ (bnr + hioffs) ] = ce_cachable[ start ];
//printf ( "ce copy %x %x\n" , ce_banktype[ (bnr + hioffs) ] , ce_cachable[ (bnr + hioffs) ] );
			real_left--;
		}
	}
#ifndef WINUAE_FOR_HATARI
	if (quick <= 0)
		debug_bankchange (old);
	fill_ce_banks ();
#endif
}

void map_banks (addrbank *bank, int start, int size, int realsize)
{
	map_banks2 (bank, start, size, realsize, 0);
}
void map_banks_quick (addrbank *bank, int start, int size, int realsize)
{
	map_banks2 (bank, start, size, realsize, 1);
}
void map_banks_nojitdirect (addrbank *bank, int start, int size, int realsize)
{
	map_banks2 (bank, start, size, realsize, -1);
}

void map_banks_ce (addrbank *bank, int start, int size, int realsize , int banktype, int cachable )
{
	fill_ce_banks (start, size, banktype, cachable );
	map_banks2 (bank, start, size, realsize, 0);
}




void memory_hardreset (void)
{
}


/*
 * Called from newcpu.c / m68k_go() when cpu_hardreset==true
 * Clear RAM at STmemory and TTmemory
 */
void memory_clear (void)
{
// [NP] Don't clear memory for now, because it would erase memory after restoring a memory snapshot
// TODO : handle memstate save/restore from m68_go as in winuae ?
#if 0
	if ( STmemory )
		memset ( STmemory , 0 , STmem_size );
	if ( TTmemory )
		memset ( TTmemory , 0 , TTmem_size );
#endif
}


uae_u32 memory_get_longi(uaecptr addr)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_r) {
		return call_mem_get_func(ab->lgeti, addr);
	} else {
		uae_u8 *m;
		addr -= ab->startaccessmask;
		addr &= ab->mask;
		m = ab->baseaddr_direct_r + addr;
		return do_get_mem_long((uae_u32 *)m);
	}
}
uae_u32 memory_get_wordi(uaecptr addr)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_r) {
		return call_mem_get_func(ab->wgeti, addr);
	} else {
		uae_u8 *m;
		addr -= ab->startaccessmask;
		addr &= ab->mask;
		m = ab->baseaddr_direct_r + addr;
		return do_get_mem_word((uae_u16*)m);
	}
}
uae_u32 memory_get_long(uaecptr addr)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_r) {
		return call_mem_get_func(ab->lget, addr);
	} else {
		uae_u8 *m;
		addr -= ab->startaccessmask;
		addr &= ab->mask;
		m = ab->baseaddr_direct_r + addr;
		return do_get_mem_long((uae_u32*)m);
	}
}
uae_u32 memory_get_word(uaecptr addr)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_r) {
		return call_mem_get_func(ab->wget, addr);
	} else {
		uae_u8 *m;
		addr -= ab->startaccessmask;
		addr &= ab->mask;
		m = ab->baseaddr_direct_r + addr;
		return do_get_mem_word((uae_u16*)m);
	}
}
uae_u32 memory_get_byte(uaecptr addr)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_r) {
		return call_mem_get_func(ab->bget, addr);
	} else {
		uae_u8 *m;
		addr -= ab->startaccessmask;
		addr &= ab->mask;
		m = ab->baseaddr_direct_r + addr;
		return *m;
	}
}

void memory_put_long(uaecptr addr, uae_u32 v)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_w) {
		call_mem_put_func(ab->lput, addr, v);
	} else {
		uae_u8 *m;
		addr -= ab->startaccessmask;
		addr &= ab->mask;
		m = ab->baseaddr_direct_w + addr;
		do_put_mem_long((uae_u32*)m, v);
	}
}
void memory_put_word(uaecptr addr, uae_u32 v)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_w) {
		call_mem_put_func(ab->wput, addr, v);
	} else {
		uae_u8 *m;
		addr -= ab->startaccessmask;
		addr &= ab->mask;
		m = ab->baseaddr_direct_w + addr;
		do_put_mem_word((uae_u16*)m, v);
	}
}
void memory_put_byte(uaecptr addr, uae_u32 v)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_w) {
		call_mem_put_func(ab->bput, addr, v);
	} else {
		uae_u8 *m;
		addr -= ab->startaccessmask;
		addr &= ab->mask;
		m = ab->baseaddr_direct_w + addr;
		*m = (uae_u8)v;
	}
}

uae_u8 *memory_get_real_address(uaecptr addr)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_r) {
		return get_mem_bank(addr).xlateaddr(addr);
	} else {
		addr -= ab->startaccessmask;
		addr &= ab->mask;
		return ab->baseaddr_direct_r + addr;
	}
}

int memory_valid_address(uaecptr addr, uae_u32 size)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!ab->baseaddr_direct_r) {
		return get_mem_bank(addr).check(addr, size);
	}
	addr -= ab->startaccessmask;
	addr &= ab->mask;
	return addr + size <= ab->allocated_size;
}

void dma_put_word(uaecptr addr, uae_u16 v)
{
	addrbank* ab = &get_mem_bank(addr);
	if (ab->flags & ABFLAG_NODMA)
		return;
	put_word(addr, v);
}
void dma_put_byte(uaecptr addr, uae_u8 v)
{
	addrbank* ab = &get_mem_bank(addr);
	if (ab->flags & ABFLAG_NODMA)
		return;
	put_byte(addr, v);
}
uae_u16 dma_get_word(uaecptr addr)
{
	addrbank* ab = &get_mem_bank(addr);
	if (ab->flags & ABFLAG_NODMA)
		return 0xffff;
	return get_word(addr);
}
uae_u8 dma_get_byte(uaecptr addr)
{
	addrbank* ab = &get_mem_bank(addr);
	if (ab->flags & ABFLAG_NODMA)
		return 0xff;
	return get_byte(addr);
}
