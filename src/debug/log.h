/*
  Hatari - log.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_LOG_H
#define HATARI_LOG_H

#include <stdbool.h>
#include <SDL_types.h>


/* Exception debugging
 * -------------------
 */

/* CPU exception flags
 * is catching needed also for: traps 0, 3-12, 15? (MonST catches them)
 */
#define	EXCEPT_BUS	 (1<<0)
#define	EXCEPT_ADDRESS 	 (1<<1)
#define	EXCEPT_ILLEGAL	 (1<<2)
#define	EXCEPT_ZERODIV	 (1<<3)
#define	EXCEPT_CHK	 (1<<4)
#define	EXCEPT_TRAPV	 (1<<5)
#define	EXCEPT_PRIVILEGE (1<<6)
#define	EXCEPT_TRACE     (1<<7)
#define	EXCEPT_NOHANDLER (1<<8)

/* DSP exception flags */
#define EXCEPT_DSP	 (1<<9)

/* whether to enable exception debugging on autostart */
#define EXCEPT_AUTOSTART (1<<10)

/* general flags */
#define	EXCEPT_NONE	 (0)
#define	EXCEPT_ALL	 (~EXCEPT_AUTOSTART)

/* defaults are same as with earlier -D option */
#define DEFAULT_EXCEPTIONS (EXCEPT_BUS|EXCEPT_ADDRESS|EXCEPT_DSP)

extern int ExceptionDebugMask;
extern const char* Log_SetExceptionDebugMask(const char *OptionsStr);


/* Logging
 * -------
 * Is always enabled as it's information that can be useful
 * to the Hatari users
 */
typedef enum
{
/* these present user with a dialog and log the issue */
	LOG_FATAL,	/* Hatari can't continue unless user resolves issue */
	LOG_ERROR,	/* something user did directly failed (e.g. save) */
/* these just log the issue */
	LOG_WARN,	/* something failed, but it's less serious */
	LOG_INFO,	/* user action success (e.g. TOS file load) */
	LOG_TODO,	/* functionality not yet being emulated */
	LOG_DEBUG,	/* information about internal Hatari working */
	LOG_NONE	/* invalid LOG level */
} LOGTYPE;

#define LOG_NAMES {"FATAL", "ERROR", "WARN ", "INFO ", "TODO ", "DEBUG"}


#ifndef __GNUC__
/* assuming attributes work only for GCC */
#define __attribute__(foo)
#endif

extern void Log_Default(void);
extern void Log_SetLevels(void);
extern int Log_Init(void);
extern int Log_SetAlertLevel(int level);
extern void Log_UnInit(void);
extern void Log_Printf(LOGTYPE nType, const char *psFormat, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void Log_AlertDlg(LOGTYPE nType, const char *psFormat, ...)
	__attribute__ ((format (printf, 2, 3)));
extern LOGTYPE Log_ParseOptions(const char *OptionStr);
extern const char* Log_SetTraceOptions(const char *OptionsStr);
extern char *Log_MatchTrace(const char *text, int state);

#ifndef __GNUC__
#undef __attribute__
#endif



/* Tracing
 * -------
 * Tracing outputs information about what happens in the emulated
 * system and slows down the emulation.  As it's intended mainly
 * just for the Hatari developers, tracing support is compiled in
 * by default.
 * 
 * Tracing can be enabled by defining ENABLE_TRACING
 * in the top level config.h
 */
#include "config.h"

/* Up to 64 levels when using Uint64 for HatariTraceFlags */
#define	TRACE_VIDEO_SYNC	 (1<<0)
#define	TRACE_VIDEO_RES 	 (1<<1)
#define	TRACE_VIDEO_COLOR	 (1<<2)
#define	TRACE_VIDEO_BORDER_V	 (1<<3)
#define	TRACE_VIDEO_BORDER_H	 (1<<4)
#define	TRACE_VIDEO_ADDR	 (1<<5)
#define	TRACE_VIDEO_VBL 	 (1<<6)
#define	TRACE_VIDEO_HBL 	 (1<<7)
#define	TRACE_VIDEO_STE 	 (1<<8)

#define	TRACE_MFP_EXCEPTION	 (1<<9)
#define	TRACE_MFP_START 	 (1<<10)
#define	TRACE_MFP_READ  	 (1<<11)
#define	TRACE_MFP_WRITE 	 (1<<12)

#define	TRACE_PSG_READ  	 (1<<13)
#define	TRACE_PSG_WRITE 	 (1<<14)

#define	TRACE_CPU_PAIRING	 (1<<15)
#define	TRACE_CPU_DISASM	 (1<<16)
#define	TRACE_CPU_EXCEPTION	 (1<<17)
#define	TRACE_CPU_REGS		 (1<<18)

#define	TRACE_INT		 (1<<19)

#define	TRACE_FDC		 (1<<20)

#define TRACE_ACIA		 (1<<21)

#define	TRACE_IKBD_CMDS 	 (1<<22)
#define	TRACE_IKBD_ACIA 	 (1<<23)
#define	TRACE_IKBD_EXEC 	 (1<<24)

#define TRACE_BLITTER		 (1<<25)

#define TRACE_OS_BIOS		 (1<<26)
#define TRACE_OS_XBIOS  	 (1<<27)
#define TRACE_OS_GEMDOS 	 (1<<28)
#define TRACE_OS_VDI		 (1<<29)
#define TRACE_OS_AES		 (1<<30)

#define TRACE_IOMEM_RD  	 (1ll<<31)
#define TRACE_IOMEM_WR  	 (1ll<<32)

#define TRACE_DMASND		 (1ll<<33)

#define TRACE_CROSSBAR		 (1ll<<34)
#define TRACE_VIDEL		 (1ll<<35)

#define TRACE_DSP_HOST_INTERFACE (1ll<<36)
#define TRACE_DSP_HOST_COMMAND	 (1ll<<37)
#define TRACE_DSP_HOST_SSI	 (1ll<<38)
#define TRACE_DSP_DISASM	 (1ll<<39)
#define TRACE_DSP_DISASM_REG	 (1ll<<40)
#define TRACE_DSP_DISASM_MEM	 (1ll<<41)
#define TRACE_DSP_STATE		 (1ll<<42)
#define TRACE_DSP_INTERRUPT	 (1ll<<43)

#define TRACE_DSP_SYMBOLS	 (1ll<<44)
#define TRACE_CPU_SYMBOLS	 (1ll<<45)

#define TRACE_NVRAM		 (1ll<<46)

#define TRACE_SCSI_CMD		 (1ll<<47)

#define TRACE_NATFEATS		 (1ll<<48)

#define TRACE_KEYMAP		 (1ll<<49)

#define TRACE_MIDI		 (1ll<<50)

#define TRACE_IDE		 (1ll<<51)

#define TRACE_OS_BASE		 (1ll<<52)

#define TRACE_SCSIDRV		 (1ll<<53)
    
#define TRACE_MEM		 (1ll<<54)

#define TRACE_VME		 (1ll<<55)

#define	TRACE_NONE		 (0)
#define	TRACE_ALL		 (~0)


#define	TRACE_VIDEO_ALL		( TRACE_VIDEO_SYNC | TRACE_VIDEO_RES | TRACE_VIDEO_COLOR \
		| TRACE_VIDEO_BORDER_V | TRACE_VIDEO_BORDER_H | TRACE_VIDEO_ADDR \
		| TRACE_VIDEO_VBL | TRACE_VIDEO_HBL | TRACE_VIDEO_STE )

#define TRACE_MFP_ALL		( TRACE_MFP_EXCEPTION | TRACE_MFP_START | TRACE_MFP_READ | TRACE_MFP_WRITE )

#define	TRACE_PSG_ALL		( TRACE_PSG_READ | TRACE_PSG_WRITE )

#define	TRACE_CPU_ALL		( TRACE_CPU_PAIRING | TRACE_CPU_DISASM | TRACE_CPU_EXCEPTION )

#define	TRACE_IKBD_ALL		( TRACE_IKBD_CMDS | TRACE_IKBD_ACIA | TRACE_IKBD_EXEC )

#define	TRACE_OS_ALL		( TRACE_OS_BASE | TRACE_OS_BIOS | TRACE_OS_XBIOS | TRACE_OS_GEMDOS | TRACE_OS_AES | TRACE_OS_VDI )

#define	TRACE_IOMEM_ALL		( TRACE_IOMEM_RD | TRACE_IOMEM_WR )

#define TRACE_DSP_ALL		( TRACE_DSP_HOST_INTERFACE | TRACE_DSP_HOST_COMMAND | TRACE_DSP_HOST_SSI | TRACE_DSP_DISASM \
		| TRACE_DSP_DISASM_REG | TRACE_DSP_DISASM_MEM | TRACE_DSP_STATE | TRACE_DSP_INTERRUPT )

extern FILE *TraceFile;
extern Uint64 LogTraceFlags;

#if ENABLE_TRACING

#define	LOG_TRACE(level, ...) \
	if (unlikely(LogTraceFlags & (level))) { fprintf(TraceFile, __VA_ARGS__); fflush(TraceFile); }

#define LOG_TRACE_LEVEL( level )	(unlikely(LogTraceFlags & (level)))

#else		/* ENABLE_TRACING */

#define LOG_TRACE(level, ...)	{}

#define LOG_TRACE_LEVEL( level )	(0)

#endif		/* ENABLE_TRACING */

/* Always defined in full to avoid compiler warnings about unused variables.
 * In code it's used in such a way that it will be optimized away when tracing
 * is disabled.
 */
#define LOG_TRACE_PRINT(...)	fprintf(TraceFile , __VA_ARGS__)


#endif		/* HATARI_LOG_H */
