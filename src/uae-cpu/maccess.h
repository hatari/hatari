 /*
  * UAE - The Un*x Amiga Emulator - CPU core
  *
  * Generic memory access functions.
  *
  * Copyright 1996 Bernd Schmidt
  *
  * Adaptation to Hatari by Thomas Huth
  *
  * This file is distributed under the GNU Public License, version 2 or at
  * your option any later version. Read the file gpl.txt for details.
  */

/* Try to use accelerated memory access functions: */
#if defined(__GNUC__) && defined(__i386__) && !defined(__STRICT_ANSI__)
# include "maccess-i86.h"
#elif defined(powerpc) || defined(__mc68000__) || defined(__mc68020__)
# include "maccess-big.h"
#endif


#ifndef UAE_MACCESS_H
#define UAE_MACCESS_H


static __inline__ uae_u32 do_get_mem_long(uae_u32 *a)
{
    uae_u8 *b = (uae_u8 *)a;
    
    return (*b << 24) | (*(b+1) << 16) | (*(b+2) << 8) | (*(b+3));
}

static __inline__ uae_u16 do_get_mem_word(uae_u16 *a)
{
    uae_u8 *b = (uae_u8 *)a;
    
    return (*b << 8) | (*(b+1));
}

static __inline__ uae_u8 do_get_mem_byte(uae_u8 *a)
{
    return *a;
}

static __inline__ void do_put_mem_long(uae_u32 *a, uae_u32 v)
{
    uae_u8 *b = (uae_u8 *)a;
    
    *b = v >> 24;
    *(b+1) = v >> 16;    
    *(b+2) = v >> 8;
    *(b+3) = v;
}

static __inline__ void do_put_mem_word(uae_u16 *a, uae_u16 v)
{
    uae_u8 *b = (uae_u8 *)a;
    
    *b = v >> 8;
    *(b+1) = v;
}

static __inline__ void do_put_mem_byte(uae_u8 *a, uae_u8 v)
{
    *a = v;
}

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))


#endif /* UAE_MACCESS_H */
