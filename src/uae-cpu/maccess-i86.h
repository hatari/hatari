 /* 
  * UAE - The Un*x Amiga Emulator - CPU core
  * 
  * Memory access functions for GCC on i86 computers.
  *
  * Copyright 1996 Bernd Schmidt
  *
  * Adaptation to Hatari by Thomas Huth
  *
  * This file is distributed under the GNU Public License, version 2 or at
  * your option any later version. Read the file gpl.txt for details.
  */

#ifndef UAE_MACCESS_H
#define UAE_MACCESS_H


static __inline__ uae_u32 do_get_mem_long (uae_u32 *a)
{
    uae_u32 retval;

    __asm__ ("bswap %0" : "=r" (retval) : "0" (*a) : "cc");
    return retval;
}

static __inline__ uae_u32 do_get_mem_word (uae_u16 *a)
{
    uae_u32 retval;

#ifdef X86_PPRO_OPT
    __asm__ ("movzwl %w1,%k0\n\tshll $16,%k0\n\tbswap %k0\n" : "=&r" (retval) : "m" (*a) : "cc");
#else
    __asm__ ("xorl %k0,%k0\n\tmovw %w1,%w0\n\trolw $8,%w0" : "=&r" (retval) : "m" (*a) : "cc");
#endif
    return retval;
}

#define do_get_mem_byte(a) ((uae_u32)*((uae_u8 *)a))

static __inline__ void do_put_mem_long (uae_u32 *a, uae_u32 v)
{
    __asm__ ("bswap %0" : "=r" (v) : "0" (v) : "cc");
    *a = v;
}

static __inline__ void do_put_mem_word (uae_u16 *a, uae_u32 v)
{
#ifdef X86_PPRO_OPT
    __asm__ ("bswap %0" : "=&r" (v) : "0" (v << 16) : "cc");
#else
    __asm__ ("rolw $8,%w0" : "=r" (v) : "0" (v) : "cc");
#endif
    *a = v;
}

#define do_put_mem_byte(a,v) (*(uae_u8 *)(a) = (v))

#if 0
static __inline__ uae_u32 call_mem_get_func(mem_get_func func, uae_cptr addr)
{
    uae_u32 result;
    __asm__("call %1"
	    : "=a" (result) : "r" (func), "a" (addr) : "cc", "edx", "ecx");
    return result;
}

static __inline__ void call_mem_put_func(mem_put_func func, uae_cptr addr, uae_u32 v)
{
    __asm__("call %2"
	    : : "a" (addr), "d" (v), "r" (func) : "cc", "eax", "edx", "ecx", "memory");
}
#else

#define call_mem_get_func(func,addr) ((*func)(addr))
#define call_mem_put_func(func,addr,v) ((*func)(addr,v))

#endif


#endif /* UAE_MACCESS_H */
