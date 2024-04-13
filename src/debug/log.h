/*
  Hatari - log.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_LOG_H
#define HATARI_LOG_H

#include <stdbool.h>
#include <stdint.h>

/* Exception debugging
 * -------------------
 */

/* CPU exception flags
 */
#define	EXCEPT_NOHANDLER (1<<0)
#define	EXCEPT_BUS	 (1<<1)
#define	EXCEPT_ADDRESS 	 (1<<2)
#define	EXCEPT_ILLEGAL	 (1<<3)
#define	EXCEPT_ZERODIV	 (1<<4)
#define	EXCEPT_CHK	 (1<<5)
#define	EXCEPT_TRAPV	 (1<<6)
#define	EXCEPT_PRIVILEGE (1<<7)
#define	EXCEPT_TRACE     (1<<8)
#define	EXCEPT_LINEA     (1<<9)
#define	EXCEPT_LINEF     (1<<10)

/* DSP exception flags */
#define EXCEPT_DSP	 (1<<30)

/* whether to enable exception debugging on autostart */
#define EXCEPT_AUTOSTART (1<<31)

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
extern void Log_ToggleMsgRepeat(void);
extern void Log_ResetMsgRepeat(void);
extern void Log_Trace(const char *format, ...)
	__attribute__ ((format (printf, 1, 2)));

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

/* Up to 64 levels when using uint64_t for HatariTraceFlags */
enum {
	TRACE_BIT_ACIA,

	TRACE_BIT_BLITTER,

	TRACE_BIT_CPU_DISASM,
	TRACE_BIT_CPU_EXCEPTION,
	TRACE_BIT_CPU_PAIRING,
	TRACE_BIT_CPU_REGS,
	TRACE_BIT_CPU_SYMBOLS,
	TRACE_BIT_CPU_VIDEO_CYCLES,

	TRACE_BIT_CROSSBAR,

	TRACE_BIT_DMASND,

	TRACE_BIT_DSP_DISASM,
	TRACE_BIT_DSP_DISASM_MEM,
	TRACE_BIT_DSP_DISASM_REG,
	TRACE_BIT_DSP_HOST_COMMAND,
	TRACE_BIT_DSP_HOST_INTERFACE,
	TRACE_BIT_DSP_HOST_SSI,
	TRACE_BIT_DSP_INTERRUPT,
	TRACE_BIT_DSP_STATE,
	TRACE_BIT_DSP_SYMBOLS,

	TRACE_BIT_FDC,

	TRACE_BIT_IDE,

	TRACE_BIT_IKBD_ACIA,
	TRACE_BIT_IKBD_CMDS,
	TRACE_BIT_IKBD_EXEC,

	TRACE_BIT_INT,

	TRACE_BIT_IOMEM_RD,
	TRACE_BIT_IOMEM_WR,

	TRACE_BIT_KEYMAP,

	TRACE_BIT_MEM,

	TRACE_BIT_MFP_EXCEPTION,
	TRACE_BIT_MFP_READ,
	TRACE_BIT_MFP_START,
	TRACE_BIT_MFP_WRITE,

	TRACE_BIT_MIDI,
	TRACE_BIT_MIDI_RAW,

	TRACE_BIT_NATFEATS,

	TRACE_BIT_NVRAM,

	TRACE_BIT_OS_AES,
	TRACE_BIT_OS_BASE,
	TRACE_BIT_OS_BIOS,
	TRACE_BIT_OS_GEMDOS,
	TRACE_BIT_OS_VDI,
	TRACE_BIT_OS_XBIOS,

	TRACE_BIT_PSG_READ,
	TRACE_BIT_PSG_WRITE,

	TRACE_BIT_SCC,

	TRACE_BIT_SCSI_CMD,

	TRACE_BIT_SCSIDRV,

	TRACE_BIT_VIDEL,

	TRACE_BIT_VIDEO_ADDR,
	TRACE_BIT_VIDEO_BORDER_H,
	TRACE_BIT_VIDEO_BORDER_V,
	TRACE_BIT_VIDEO_COLOR,
	TRACE_BIT_VIDEO_HBL,
	TRACE_BIT_VIDEO_RES,
	TRACE_BIT_VIDEO_STE,
	TRACE_BIT_VIDEO_SYNC,
	TRACE_BIT_VIDEO_VBL,

	TRACE_BIT_VME
};

#define TRACE_ACIA               (1ll<<TRACE_BIT_ACIA)

#define TRACE_BLITTER            (1ll<<TRACE_BIT_BLITTER)

#define TRACE_CPU_DISASM         (1ll<<TRACE_BIT_CPU_DISASM)
#define TRACE_CPU_EXCEPTION      (1ll<<TRACE_BIT_CPU_EXCEPTION)
#define TRACE_CPU_PAIRING        (1ll<<TRACE_BIT_CPU_PAIRING)
#define TRACE_CPU_REGS           (1ll<<TRACE_BIT_CPU_REGS)
#define TRACE_CPU_SYMBOLS        (1ll<<TRACE_BIT_CPU_SYMBOLS)
#define TRACE_CPU_VIDEO_CYCLES   (1ll<<TRACE_BIT_CPU_VIDEO_CYCLES)

#define TRACE_CROSSBAR           (1ll<<TRACE_BIT_CROSSBAR)

#define TRACE_DMASND             (1ll<<TRACE_BIT_DMASND)

#define TRACE_DSP_DISASM         (1ll<<TRACE_BIT_DSP_DISASM)
#define TRACE_DSP_DISASM_MEM     (1ll<<TRACE_BIT_DSP_DISASM_MEM)
#define TRACE_DSP_DISASM_REG     (1ll<<TRACE_BIT_DSP_DISASM_REG)
#define TRACE_DSP_HOST_COMMAND   (1ll<<TRACE_BIT_DSP_HOST_COMMAND)
#define TRACE_DSP_HOST_INTERFACE (1ll<<TRACE_BIT_DSP_HOST_INTERFACE)
#define TRACE_DSP_HOST_SSI       (1ll<<TRACE_BIT_DSP_HOST_SSI)
#define TRACE_DSP_INTERRUPT      (1ll<<TRACE_BIT_DSP_INTERRUPT)
#define TRACE_DSP_STATE          (1ll<<TRACE_BIT_DSP_STATE)
#define TRACE_DSP_SYMBOLS        (1ll<<TRACE_BIT_DSP_SYMBOLS)

#define TRACE_FDC                (1ll<<TRACE_BIT_FDC)

#define TRACE_IDE                (1ll<<TRACE_BIT_IDE)

#define TRACE_IKBD_ACIA          (1ll<<TRACE_BIT_IKBD_ACIA)
#define TRACE_IKBD_CMDS          (1ll<<TRACE_BIT_IKBD_CMDS)
#define TRACE_IKBD_EXEC          (1ll<<TRACE_BIT_IKBD_EXEC)

#define TRACE_INT                (1ll<<TRACE_BIT_INT)

#define TRACE_IOMEM_RD           (1ll<<TRACE_BIT_IOMEM_RD)
#define TRACE_IOMEM_WR           (1ll<<TRACE_BIT_IOMEM_WR)

#define TRACE_KEYMAP             (1ll<<TRACE_BIT_KEYMAP)

#define TRACE_MEM                (1ll<<TRACE_BIT_MEM)

#define TRACE_MFP_EXCEPTION      (1ll<<TRACE_BIT_MFP_EXCEPTION)
#define TRACE_MFP_READ           (1ll<<TRACE_BIT_MFP_READ)
#define TRACE_MFP_START          (1ll<<TRACE_BIT_MFP_START)
#define TRACE_MFP_WRITE          (1ll<<TRACE_BIT_MFP_WRITE)

#define TRACE_MIDI               (1ll<<TRACE_BIT_MIDI)
#define TRACE_MIDI_RAW           (1ll<<TRACE_BIT_MIDI_RAW)

#define TRACE_NATFEATS           (1ll<<TRACE_BIT_NATFEATS)

#define TRACE_NVRAM              (1ll<<TRACE_BIT_NVRAM)

#define TRACE_OS_AES             (1ll<<TRACE_BIT_OS_AES)
#define TRACE_OS_BASE            (1ll<<TRACE_BIT_OS_BASE)
#define TRACE_OS_BIOS            (1ll<<TRACE_BIT_OS_BIOS)
#define TRACE_OS_GEMDOS          (1ll<<TRACE_BIT_OS_GEMDOS)
#define TRACE_OS_VDI             (1ll<<TRACE_BIT_OS_VDI)
#define TRACE_OS_XBIOS           (1ll<<TRACE_BIT_OS_XBIOS)

#define TRACE_PSG_READ           (1ll<<TRACE_BIT_PSG_READ)
#define TRACE_PSG_WRITE          (1ll<<TRACE_BIT_PSG_WRITE)

#define TRACE_SCC                (1ll<<TRACE_BIT_SCC)

#define TRACE_SCSI_CMD           (1ll<<TRACE_BIT_SCSI_CMD)

#define TRACE_SCSIDRV            (1ll<<TRACE_BIT_SCSIDRV)

#define TRACE_VIDEL              (1ll<<TRACE_BIT_VIDEL)

#define TRACE_VIDEO_ADDR         (1ll<<TRACE_BIT_VIDEO_ADDR)
#define TRACE_VIDEO_BORDER_H     (1ll<<TRACE_BIT_VIDEO_BORDER_H)
#define TRACE_VIDEO_BORDER_V     (1ll<<TRACE_BIT_VIDEO_BORDER_V)
#define TRACE_VIDEO_COLOR        (1ll<<TRACE_BIT_VIDEO_COLOR)
#define TRACE_VIDEO_HBL          (1ll<<TRACE_BIT_VIDEO_HBL)
#define TRACE_VIDEO_RES          (1ll<<TRACE_BIT_VIDEO_RES)
#define TRACE_VIDEO_STE          (1ll<<TRACE_BIT_VIDEO_STE)
#define TRACE_VIDEO_SYNC         (1ll<<TRACE_BIT_VIDEO_SYNC)
#define TRACE_VIDEO_VBL          (1ll<<TRACE_BIT_VIDEO_VBL)

#define TRACE_VME                (1ll<<TRACE_BIT_VME)

#define	TRACE_NONE		 (0)
#define	TRACE_ALL		 (~0ll)


#define	TRACE_VIDEO_ALL		( TRACE_VIDEO_SYNC | TRACE_VIDEO_RES | TRACE_VIDEO_COLOR \
		| TRACE_VIDEO_BORDER_V | TRACE_VIDEO_BORDER_H | TRACE_VIDEO_ADDR \
		| TRACE_VIDEO_VBL | TRACE_VIDEO_HBL | TRACE_VIDEO_STE )

#define TRACE_MFP_ALL		( TRACE_MFP_EXCEPTION | TRACE_MFP_START | TRACE_MFP_READ | TRACE_MFP_WRITE )

#define	TRACE_PSG_ALL		( TRACE_PSG_READ | TRACE_PSG_WRITE )

#define	TRACE_CPU_ALL		( TRACE_CPU_PAIRING | TRACE_CPU_DISASM | TRACE_CPU_EXCEPTION | TRACE_CPU_VIDEO_CYCLES )

#define	TRACE_IKBD_ALL		( TRACE_IKBD_CMDS | TRACE_IKBD_ACIA | TRACE_IKBD_EXEC )

#define	TRACE_OS_ALL		( TRACE_OS_BASE | TRACE_OS_BIOS | TRACE_OS_XBIOS | TRACE_OS_GEMDOS | TRACE_OS_AES | TRACE_OS_VDI )

#define	TRACE_IOMEM_ALL		( TRACE_IOMEM_RD | TRACE_IOMEM_WR )

#define TRACE_DSP_ALL		( TRACE_DSP_HOST_INTERFACE | TRACE_DSP_HOST_COMMAND | TRACE_DSP_HOST_SSI | TRACE_DSP_DISASM \
		| TRACE_DSP_DISASM_REG | TRACE_DSP_DISASM_MEM | TRACE_DSP_STATE | TRACE_DSP_INTERRUPT )

extern FILE *TraceFile;
extern uint64_t LogTraceFlags;

#if ENABLE_TRACING

#define LOG_TRACE_LEVEL( level )	(unlikely(LogTraceFlags & (level)))

#define	LOG_TRACE(level, ...) \
	if (LOG_TRACE_LEVEL(level))	{ Log_Trace(__VA_ARGS__); }

#else		/* ENABLE_TRACING */

#define LOG_TRACE(level, ...)	{}

#define LOG_TRACE_LEVEL( level )	(0)

#endif		/* ENABLE_TRACING */

/* Always defined in full to avoid compiler warnings about unused variables.
 * In code it's used in such a way that it will be optimized away when tracing
 * is disabled.
 */
#define LOG_TRACE_PRINT(...)	Log_Trace(__VA_ARGS__)

/* Skip message repeat suppression on multi-line output.
 * LOG_TRACE_DIRECT_INIT() should called before doing them and
 * LOG_TRACE_DIRECT_FLUSH() can be called after them
 */
#define LOG_TRACE_DIRECT(...)	    fprintf(TraceFile, __VA_ARGS__)
#define	LOG_TRACE_DIRECT_LEVEL(level, ...) \
	if (LOG_TRACE_LEVEL(level)) { fprintf(TraceFile, __VA_ARGS__); }
#define LOG_TRACE_DIRECT_INIT()	    Log_ResetMsgRepeat()
#define LOG_TRACE_DIRECT_FLUSH()    fflush(TraceFile)

#endif		/* HATARI_LOG_H */
