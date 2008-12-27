/*
  Hatari - log.h
  
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_LOG_H
#define HATARI_LOG_H

#include <stdbool.h>
#include <SDL_types.h>


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

#ifndef __GNUC__
/* assuming attributes work only for GCC */
#define __attribute__(foo)
#endif

extern int Log_Init(void);
extern void Log_UnInit(void);
extern void Log_Printf(LOGTYPE nType, const char *psFormat, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void Log_AlertDlg(LOGTYPE nType, const char *psFormat, ...)
	__attribute__ ((format (printf, 2, 3)));
extern LOGTYPE Log_ParseOptions(const char *OptionStr);
extern bool Log_SetTraceOptions(const char *OptionsStr);

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
 * Tracing can be enabled but defining HATARI_TRACE_ACTIVATED
 * in the top level config.h
 */
#include "config.h"

/* Up to 32 levels when using Uint32 for HatariTraceFlags */
#define	HATARI_TRACE_VIDEO_SYNC		(1<<0)
#define	HATARI_TRACE_VIDEO_RES		(1<<1)
#define	HATARI_TRACE_VIDEO_COLOR	(1<<2)
#define	HATARI_TRACE_VIDEO_BORDER_V	(1<<3)
#define	HATARI_TRACE_VIDEO_BORDER_H	(1<<4)
#define	HATARI_TRACE_VIDEO_ADDR		(1<<5)
#define	HATARI_TRACE_VIDEO_VBL		(1<<6)
#define	HATARI_TRACE_VIDEO_HBL		(1<<7)
#define	HATARI_TRACE_VIDEO_STE		(1<<8)

#define	HATARI_TRACE_MFP_EXCEPTION	(1<<9)
#define	HATARI_TRACE_MFP_START		(1<<10)
#define	HATARI_TRACE_MFP_READ		(1<<11)
#define	HATARI_TRACE_MFP_WRITE		(1<<12)

#define	HATARI_TRACE_PSG_READ		(1<<13)
#define	HATARI_TRACE_PSG_WRITE		(1<<14)

#define	HATARI_TRACE_CPU_PAIRING	(1<<15)
#define	HATARI_TRACE_CPU_DISASM		(1<<16)
#define	HATARI_TRACE_CPU_EXCEPTION	(1<<17)

#define	HATARI_TRACE_INT		(1<<18)

#define	HATARI_TRACE_FDC		(1<<19)

#define	HATARI_TRACE_IKBD_CMDS		(1<<20)
#define	HATARI_TRACE_IKBD_ACIA		(1<<21)
#define	HATARI_TRACE_IKBD_EXEC		(1<<22)

#define HATARI_TRACE_BLITTER		(1<<23)

#define HATARI_TRACE_OS_BIOS		(1<<24)
#define HATARI_TRACE_OS_XBIOS		(1<<25)
#define HATARI_TRACE_OS_GEMDOS		(1<<26)
#define HATARI_TRACE_OS_VDI		(1<<27)

#define HATARI_TRACE_IOMEM_RD		(1<<28)
#define HATARI_TRACE_IOMEM_WR		(1<<29)

#define	HATARI_TRACE_NONE		(0)
#define	HATARI_TRACE_ALL		(~0)


#define	HATARI_TRACE_VIDEO_ALL		( HATARI_TRACE_VIDEO_SYNC | HATARI_TRACE_VIDEO_RES | HATARI_TRACE_VIDEO_COLOR \
		| HATARI_TRACE_VIDEO_BORDER_V | HATARI_TRACE_VIDEO_BORDER_H | HATARI_TRACE_VIDEO_ADDR \
		| HATARI_TRACE_VIDEO_VBL | HATARI_TRACE_VIDEO_HBL | HATARI_TRACE_VIDEO_STE )

#define HATARI_TRACE_MFP_ALL		( HATARI_TRACE_MFP_EXCEPTION | HATARI_TRACE_MFP_START | HATARI_TRACE_MFP_READ | HATARI_TRACE_MFP_WRITE )

#define	HATARI_TRACE_PSG_ALL		( HATARI_TRACE_PSG_READ | HATARI_TRACE_PSG_WRITE )

#define	HATARI_TRACE_CPU_ALL		( HATARI_TRACE_CPU_PAIRING | HATARI_TRACE_CPU_DISASM | HATARI_TRACE_CPU_EXCEPTION )

#define	HATARI_TRACE_IKBD_ALL		( HATARI_TRACE_IKBD_CMDS | HATARI_TRACE_IKBD_ACIA | HATARI_TRACE_IKBD_EXEC | HATARI_TRACE_OS_VDI )

#define	HATARI_TRACE_OS_ALL		( HATARI_TRACE_OS_BIOS | HATARI_TRACE_OS_XBIOS | HATARI_TRACE_OS_GEMDOS | HATARI_TRACE_OS_VDI )


extern FILE *TraceFile;
extern Uint32 HatariTraceFlags;

#ifdef HATARI_TRACE_ACTIVATED

#define	HATARI_TRACE( level, args... ) \
	if ( HatariTraceFlags & level ) fprintf ( TraceFile , args )
#define HATARI_TRACE_LEVEL( level )	(HatariTraceFlags & level)

#else		/* HATARI_TRACE_ACTIVATED */

#define HATARI_TRACE( level, args... )	{}
#define HATARI_TRACE_LEVEL( level )	(0)

#endif		/* HATARI_TRACE_ACTIVATED */

/* Always defined in full to avoid compiler warnings about unused variables.
 * In code it's used in such a way that it will be optimized away when tracing
 * is disabled.
 */
#define HATARI_TRACE_PRINT( args... )	fprintf ( TraceFile , args )

#endif		/* HATARI_LOG_H */
