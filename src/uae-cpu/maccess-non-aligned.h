 /*
  * UAE - The Un*x Amiga Emulator - CPU core
  *
  * Generic memory access functions,
  *
  * Copyright 1996 Bernd Schmidt
  *
  * Adaptation to Hatari by Eero Tamminen
  * 
  * These work for devices which:
  * - have byte swap routines accelerated by SDL, AND
  * - can access non-aligned shorts and longs (which on
  *   Atari would produce Address error and e.g. on SPARC crash
  *   the emulator)
  * OR are big-endian (i.e. do not need byte swapping)
  *
  * This file is distributed under the GNU Public License, version 2 or at
  * your option any later version. Read the file gpl.txt for details.
  */

#ifndef UAE_MACCESS_H
#define UAE_MACCESS_H

#include <SDL_endian.h>


static __inline__ uae_u32 do_get_mem_long(uae_u32 *a)
{
	return SDL_SwapBE32(*a);
}

static __inline__ uae_u16 do_get_mem_word(uae_u16 *a)
{
	return SDL_SwapBE16(*a);
}

static __inline__ uae_u8 do_get_mem_byte(uae_u8 *a)
{
	return *a;
}

static __inline__ void do_put_mem_long(uae_u32 *a, uae_u32 v)
{
	*a = SDL_SwapBE32(v);
}

static __inline__ void do_put_mem_word(uae_u16 *a, uae_u16 v)
{
	*a = SDL_SwapBE16(v);
}

static __inline__ void do_put_mem_byte(uae_u8 *a, uae_u8 v)
{
	*a = v;
}

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))


#endif /* UAE_MACCESS_H */
