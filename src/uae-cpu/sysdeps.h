 /*
  * UAE - The Un*x Amiga Emulator - CPU core
  *
  * Try to include the right system headers and get other system-specific
  * stuff right & other collected kludges.
  *
  * If you think about modifying this, think twice. Some systems rely on
  * the exact order of the #include statements. That's also the reason
  * why everything gets included unconditionally regardless of whether
  * it's actually needed by the .c file.
  *
  * Copyright 1996, 1997 Bernd Schmidt
  *
  * Adaptation to Hatari by Thomas Huth
  *
  * This file is distributed under the GNU General Public License, version 2
  * or at your option any later version. Read the file gpl.txt for details.
  */

#ifndef UAE_SYSDEPS_H
#define UAE_SYSDEPS_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>

#ifndef __STDC__
#error "Your compiler is not ANSI. Get a real one."
#endif

#include <stdarg.h>
#include <stdint.h>


#if EEXIST == ENOTEMPTY
#define BROKEN_OS_PROBABLY_AIX
#endif

#ifdef __NeXT__
#define S_IRUSR S_IREAD
#define S_IWUSR S_IWRITE
#define S_IXUSR S_IEXEC
#define S_ISDIR(val) (S_IFDIR & val)
struct utimbuf
{
    time_t actime;
    time_t modtime;
};
#endif


#if defined(WARPUP)
#include "devices/timer.h"
#include "osdep/posixemu.h"
#define REGPARAM
#define REGPARAM2
#define RETSIGTYPE
#define USE_ZFILE
#define strcasecmp stricmp
#define memcpy q_memcpy
#define memset q_memset
#define strdup my_strdup
#define random rand
#define creat(x,y) open("T:creat",O_CREAT|O_RDWR|O_TRUNC,777)
extern void* q_memset(void*,int,size_t);
extern void* q_memcpy(void*,const void*,size_t);
#endif


/* Acorn specific stuff */
#ifdef ACORN

#define S_IRUSR S_IREAD
#define S_IWUSR S_IWRITE
#define S_IXUSR S_IEXEC

#define strcasecmp stricmp

#endif


/* The variable-types used in the CPU core: */
typedef uint8_t uae_u8;
typedef int8_t uae_s8;

typedef uint16_t uae_u16;
typedef int16_t uae_s16;

typedef uint32_t uae_u32;
typedef int32_t uae_s32;

typedef uae_u32 uaecptr;

#undef uae_s64
#undef uae_u64

#if defined(INT64_MAX)
# define uae_u64 uint64_t
# define uae_s64 int64_t
# if defined(__GNUC__) || defined(__MWERKS__) || defined(__SUNPRO_C)
#  define VAL64(a) (a ## LL)
#  define UVAL64(a) (a ## ULL)
# else
#  define VAL64(a) (a ## L)
#  define UVAL64(a) (a ## UL)
# endif
#endif


/* We can only rely on GNU C getting enums right. Mickeysoft VSC++ is known
 * to have problems, and it's likely that other compilers choke too. */
#ifdef __GNUC__
#define ENUMDECL typedef enum
#define ENUMNAME(name) name
#else
#define ENUMDECL enum
#define ENUMNAME(name) ; typedef int name
#endif

/* When using GNU C, make abort more useful.  */
#ifdef __GNUC__
#define abort() \
  do { \
    fprintf(stderr, "Internal error; file %s, line %d\n", __FILE__, __LINE__); \
    (abort) (); \
} while (0)
#endif


#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef STATIC_INLINE
#define STATIC_INLINE static __inline__
#endif

/*
 * You can specify numbers from 0 to 5 here. It is possible that higher
 * numbers will make the CPU emulation slightly faster, but if the setting
 * is too high, you will run out of memory while compiling.
 * Best to leave this as it is.
 */
#define CPU_EMU_SIZE 0

#ifndef REGPARAM
# define REGPARAM
#endif
#ifndef REGPARAM2
# define REGPARAM2
#endif
#ifndef REGPARAM3
# define REGPARAM3
#endif

#endif /* ifndef UAE_SYSDEPS_H */
