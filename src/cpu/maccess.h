 /*
  * UAE - The Un*x Amiga Emulator - CPU core
  *
  * Big endian memory access functions.
  *
  * Copyright 1996 Bernd Schmidt
  *
  * Adaptation to Hatari by Thomas Huth, Eero Tamminen
  *
  * This file is distributed under the GNU General Public License, version 2
  * or at your option any later version. Read the file gpl.txt for details.
  */

#ifndef UAE_MACCESS_H
#define UAE_MACCESS_H

#include "sysdeps.h"

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)

#define be_swap32(x) ((uint32_t)(x))
#define be_swap16(x) ((uint16_t)(x))

#else

#define be_swap32(x) bswap_32(x)
#define be_swap16(x) bswap_16(x)

#endif

/* Can the actual CPU access unaligned memory? */
#ifndef CPU_CAN_ACCESS_UNALIGNED
# if defined(__i386__) || defined(__x86_64__) || defined(__mc68020__) || \
     defined(powerpc) || defined(__ppc__) || defined(__ppc64__)
#  define CPU_CAN_ACCESS_UNALIGNED 1
# else
#  define CPU_CAN_ACCESS_UNALIGNED 0
# endif
#endif

/* If the CPU can access unaligned memory, use these accelerated functions: */
#if CPU_CAN_ACCESS_UNALIGNED

static inline uae_u32 do_get_mem_long(void *a)
{
	return be_swap32(*(uae_u32 *)a);
}

static inline uae_u16 do_get_mem_word(void *a)
{
	return be_swap16(*(uae_u16 *)a);
}


static inline void do_put_mem_long(void *a, uae_u32 v)
{
	*(uae_u32 *)a = be_swap32(v);
}

static inline void do_put_mem_word(void *a, uae_u16 v)
{
	*(uae_u16 *)a = be_swap16(v);
}


#else  /* Cpu can not access unaligned memory: */


static inline uae_u32 do_get_mem_long(void *a)
{
	uae_u8 *b = (uae_u8 *)a;

	return ((uae_u32)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

static inline uae_u16 do_get_mem_word(void *a)
{
	uae_u8 *b = (uae_u8 *)a;

	return (b[0] << 8) | b[1];
}

static inline void do_put_mem_long(void *a, uae_u32 v)
{
	uae_u8 *b = (uae_u8 *)a;

	b[0] = v >> 24;
	b[1] = v >> 16;
	b[2] = v >> 8;
	b[3] = v;
}

static inline void do_put_mem_word(void *a, uae_u16 v)
{
	uae_u8 *b = (uae_u8 *)a;

	b[0] = v >> 8;
	b[1] = v;
}


#endif  /* CPU_CAN_ACCESS_UNALIGNED */


/* These are same for all architectures: */

static inline uae_u8 do_get_mem_byte(uae_u8 *a)
{
	return *a;
}

static inline void do_put_mem_byte(uae_u8 *a, uae_u8 v)
{
	*a = v;
}


STATIC_INLINE uae_u32 do_byteswap_32(uae_u32 v)
{
	return bswap_32(v);
}

STATIC_INLINE uae_u16 do_byteswap_16(uae_u16 v)
{
	return bswap_16(v);
}

STATIC_INLINE uae_u32 do_get_mem_word_unswapped(uae_u16 *a)
{
	return *a;
}

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))


#endif /* UAE_MACCESS_H */
