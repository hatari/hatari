/*
  Hatari - sysconfig.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains needed auto generated includes and defines needed by WinUae CPU core.
  The aim is to have minimum changes in WinUae CPU core for next updates
*/

#ifndef HATARI_SYSCONFIG_H
#define HATARI_SYSCONFIG_H

#define SUPPORT_THREADS
#define MAX_DPATH 1000

//#define X86_MSVC_ASSEMBLY
#define X86_MSVC_ASSEMBLY_MEMACCESS
#define OPTIMIZED_FLAGS
//#define __i386__

#ifndef UAE_MINI

//#define DEBUGGER
#define FILESYS /* filesys emulation */
#define UAE_FILESYS_THREADS
//#define AUTOCONFIG /* autoconfig support, fast ram, harddrives etc.. */
//#define JIT /* JIT compiler support */
#define NATMEM_OFFSET natmem_offset
#define USE_NORMAL_CALLING_CONVENTION 0
#define USE_X86_FPUCW 1
#define WINDDK /* Windows DDK available, keyboard leds and harddrive support */
#define CATWEASEL /* Catweasel MK2/3 support */
#define AHI /* AHI sound emulation */
#define ENFORCER /* UAE Enforcer */
#define ECS_DENISE /* ECS DENISE new features */
#define AGA /* AGA chipset emulation (ECS_DENISE must be enabled) */
#define CD32 /* CD32 emulation */
#define CDTV /* CDTV emulation */
#define D3D /* D3D display filter support */
//#define OPENGL /* OpenGL display filter support */
#define PARALLEL_PORT /* parallel port emulation */
#define PARALLEL_DIRECT /* direct parallel port emulation */
#define SERIAL_PORT /* serial port emulation */
#define SERIAL_ENET /* serial port UDP transport */
#define SCSIEMU /* uaescsi.device emulation */
#define UAESERIAL /* uaeserial.device emulation */
#define FPUEMU /* FPU emulation */
#define FPU_UAE
//#define WITH_SOFTFLOAT
#define MMUEMU /* Aranym 68040 MMU */
#define FULLMMU /* Aranym 68040 MMU */
#define CPUEMU_0 /* generic 680x0 emulation with direct memory access */
#define CPUEMU_11 /* 68000/68010 prefetch emulation */
#define CPUEMU_13 /* 68000/68010 cycle-exact cpu&blitter */
#define CPUEMU_20 /* 68020 prefetch */
#define CPUEMU_21 /* 68020 "cycle-exact" + blitter */
#define CPUEMU_22 /* 68030 prefetch */
#define CPUEMU_23 /* 68030 "cycle-exact" + blitter */
#define CPUEMU_24 /* 68060 "cycle-exact" + blitter */
#define CPUEMU_25 /* 68040 "cycle-exact" + blitter */
#define CPUEMU_31 /* Aranym 68040 MMU */
#define CPUEMU_32 /* Previous 68030 MMU */
#define CPUEMU_33 /* 68060 MMU */
#define CPUEMU_40 /* generic 680x0 with indirect memory access */
//#define ACTION_REPLAY /* Action Replay 1/2/3 support */
#define PICASSO96 /* Picasso96 display card emulation */
#define UAEGFX_INTERNAL /* built-in libs:picasso96/uaegfx.card */
#define BSDSOCKET /* bsdsocket.library emulation */
#define CAPS /* CAPS-image support */
#define FDI2RAW /* FDI 1.0 and 2.x image support */
#define AVIOUTPUT /* Avioutput support */
#define PROWIZARD /* Pro-Wizard module ripper */
#define ARCADIA /* Arcadia arcade system */
#define ARCHIVEACCESS /* ArchiveAccess decompression library */
#define LOGITECHLCD /* Logitech G15 LCD */
#define SAVESTATE /* State file support */
#define A2091 /* A590/A2091 SCSI */
#define A2065 /* A2065 Ethernet card */
#define NCR /* A4000T/A4091, 53C710/53C770 SCSI */
#define NCR9X /* 53C9X SCSI */
#define SANA2 /* SANA2 network driver */
#define AMAX /* A-Max ROM adapater emulation */
#define RETROPLATFORM /* Cloanto RetroPlayer support */

#else

/* #define SINGLEFILE */

#define CUSTOM_SIMPLE /* simplified custom chipset emulation */
#define CPUEMU_0
#define CPUEMU_68000_ONLY /* drop 68010+ commands from CPUEMU_0 */
#define ADDRESS_SPACE_24BIT
#ifndef UAE_NOGUI
#define D3D
#define OPENGL
#endif
#define CAPS
#define CPUEMU_13
#define CPUEMU_11


#endif

#define WITH_SCSI_IOCTL
#define WITH_SCSI_SPTI

#define UAE_RAND_MAX RAND_MAX

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#ifdef WIN64
#undef X86_MSVC_ASSEMBLY_MEMACCESS
#undef X86_MSVC_ASSEMBLY
#undef JIT
#define X64_MSVC_ASSEMBLY
#define CPU_64_BIT
#define SIZEOF_VOID_P 8
#else
#define SIZEOF_VOID_P 4
#endif

#if !defined(AHI)
#undef ENFORCER
#endif

/* Define if utime(file, NULL) sets file's timestamp to the present.  */
#define HAVE_UTIME_NULL 1

/* Define as __inline if that's what the C compiler calls it.  */
/* #undef inline */
#define __inline__ __inline
#define __volatile__ volatile

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define if you can safely include both <sys/time.h> and <time.h>.  */
#ifdef __GNUC__
#define TIME_WITH_SYS_TIME 1
#endif

#ifdef _WIN32_WCE
#define NO_TIME_H 1
#endif

/* Define if the X Window System is missing or not being used.  */
#define X_DISPLAY_MISSING 1

/* The number of bytes in a __int64.  */
#define SIZEOF___INT64 8

/* The number of bytes in a char.  */
#define SIZEOF_CHAR 1

/* The number of bytes in a int.  */
#define SIZEOF_INT 4

/* The number of bytes in a long.  */
#define SIZEOF_LONG 4

/* The number of bytes in a long long.  */
#define SIZEOF_LONG_LONG 8

/* The number of bytes in a short.  */
#define SIZEOF_SHORT 2

#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8

#define HAVE_ISNAN
#define HAVE_ISINF

/* Define to 1 if `S_un' is a member of `struct in_addr'. */
#define HAVE_STRUCT_IN_ADDR_S_UN 1

#endif
