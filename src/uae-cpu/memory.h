 /*
  * UAE - The Un*x Amiga Emulator
  *
  * memory management
  *
  * Copyright 1995 Bernd Schmidt
  *
  * Adaptation to Hatari by Thomas Huth
  *
  * This file is distributed under the GNU General Public License, version 2
  * or at your option any later version. Read the file gpl.txt for details.
  */

#ifndef UAE_MEMORY_H
#define UAE_MEMORY_H

#include "maccess.h"

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))


/* Enabling this adds one additional native memory reference per 68k memory
 * access, but saves one shift (on the x86). Enabling this is probably
 * better for the cache. My favourite benchmark (PP2) doesn't show a
 * difference, so I leave this enabled. */
#if 1 || defined SAVE_MEMORY
#define SAVE_MEMORY_BANKS
#endif


typedef uae_u32 (*mem_get_func)(uaecptr) REGPARAM;
typedef void (*mem_put_func)(uaecptr, uae_u32) REGPARAM;
typedef uae_u8 *(*xlate_func)(uaecptr) REGPARAM;
typedef int (*check_func)(uaecptr, uae_u32) REGPARAM;

extern char *address_space, *good_address_map;


enum
{
	ABFLAG_UNK = 0, ABFLAG_RAM = 1, ABFLAG_ROM = 2, ABFLAG_ROMIN = 4, ABFLAG_IO = 8,
	ABFLAG_NONE = 16, ABFLAG_SAFE = 32, ABFLAG_INDIRECT = 64, ABFLAG_NOALLOC = 128,
	ABFLAG_RTG = 256, ABFLAG_THREADSAFE = 512, ABFLAG_DIRECTMAP = 1024
};
typedef struct {
    /* These ones should be self-explanatory... */
    mem_get_func lget, wget, bget;
    mem_put_func lput, wput, bput;
    /* Use xlateaddr to translate an Atari address to a uae_u8 * that can
     * be used to address memory without calling the wget/wput functions.
     * This doesn't work for all memory banks, so this function may call
     * abort(). */
    xlate_func xlateaddr;
    /* To prevent calls to abort(), use check before calling xlateaddr.
     * It checks not only that the memory bank can do xlateaddr, but also
     * that the pointer points to an area of at least the specified size.
     * This is used for example to translate bitplane pointers in custom.c */
    check_func check;
    /* For those banks that refer to real memory, we can save the whole trouble
    of going through function calls, and instead simply grab the memory
    ourselves. This holds the memory address where the start of memory is
    for this particular bank. */
    uae_u8 *baseaddr;
    int flags;
    uae_u32 mask;
    uae_u32 start;
} addrbank;


#define bankindex(addr) (((uaecptr)(addr)) >> 16)

#ifdef SAVE_MEMORY_BANKS
extern addrbank *mem_banks[65536];
#define get_mem_bank(addr) (*mem_banks[bankindex(addr)])
#define put_mem_bank(addr, b) (mem_banks[bankindex(addr)] = (b))
#else
extern addrbank mem_banks[65536];
#define get_mem_bank(addr) (mem_banks[bankindex(addr)])
#define put_mem_bank(addr, b) (mem_banks[bankindex(addr)] = *(b))
#endif

extern void memory_init(uae_u32 nNewSTMemSize, uae_u32 nNewTTMemSize, uae_u32 nNewRomMemStart);
extern void memory_uninit (void);
extern void map_banks(addrbank *bank, int first, int count);

#ifndef NO_INLINE_MEMORY_ACCESS

#define longget(addr) (call_mem_get_func(get_mem_bank(addr).lget, addr))
#define wordget(addr) (call_mem_get_func(get_mem_bank(addr).wget, addr))
#define byteget(addr) (call_mem_get_func(get_mem_bank(addr).bget, addr))
#define longput(addr,l) (call_mem_put_func(get_mem_bank(addr).lput, addr, l))
#define wordput(addr,w) (call_mem_put_func(get_mem_bank(addr).wput, addr, w))
#define byteput(addr,b) (call_mem_put_func(get_mem_bank(addr).bput, addr, b))

#else

extern uae_u32 alongget(uaecptr addr);
extern uae_u32 awordget(uaecptr addr);
extern uae_u32 longget(uaecptr addr);
extern uae_u32 wordget(uaecptr addr);
extern uae_u32 byteget(uaecptr addr);
extern void longput(uaecptr addr, uae_u32 l);
extern void wordput(uaecptr addr, uae_u32 w);
extern void byteput(uaecptr addr, uae_u32 b);

#endif


static inline uae_u32 get_long(uaecptr addr)
{
    return longget(addr);
}

static inline uae_u32 get_word(uaecptr addr)
{
    return wordget(addr);
}

static inline uae_u32 get_byte(uaecptr addr)
{
    return byteget(addr);
}

static inline void put_long(uaecptr addr, uae_u32 l)
{
    longput(addr, l);
}

static inline void put_word(uaecptr addr, uae_u32 w)
{
    wordput(addr, w);
}

static inline void put_byte(uaecptr addr, uae_u32 b)
{
    byteput(addr, b);
}

static inline uae_u8 *get_real_address(uaecptr addr)
{
    return get_mem_bank(addr).xlateaddr(addr);
}

static inline int valid_address(uaecptr addr, uae_u32 size)
{
    return get_mem_bank(addr).check(addr, size);
}


#endif /* UAE_MEMORY_H */
