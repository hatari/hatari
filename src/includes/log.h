/*
  Hatari - log.h
  
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_LOG_H
#define HATARI_LOG_H

/* Logging is always enabled as it's information that can be useful
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
	LOG_DEBUG	/* information about internal Hatari working */
} LOGTYPE;

extern void Log_Init(void);
extern void Log_UnInit(void);
extern void Log_Printf(LOGTYPE nType, const char *psFormat, ...);
extern void Log_AlertDlg(LOGTYPE nType, const char *psFormat, ...);


#include <SDL_types.h>


/* Tracing outputs information about what happens in the emulated
 * system and is intended for Hatari developers.  Therefore tracing
 * can be compiled out.  Tracing is including if HATARI_TRACE_ACTIVATED
 * is defined.
 *
 * TODO: This should be a configure option and by default disabled
 */
#define HATARI_TRACE_ACTIVATED


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

#define	HATARI_TRACE_PSG_WRITE_REG	(1<<13)
#define	HATARI_TRACE_PSG_WRITE_DATA	(1<<14)

#define	HATARI_TRACE_CPU_PAIRING	(1<<15)
#define	HATARI_TRACE_CPU_DISASM		(1<<16)
#define	HATARI_TRACE_CPU_EXCEPTION	(1<<17)

#define	HATARI_TRACE_INT		(1<<18)

#define	HATARI_TRACE_FDC		(1<<19)

#define	HATARI_TRACE_IKBD		(1<<20)

#define	HATARI_TRACE_NONE		(0)
#define	HATARI_TRACE_ALL		(~0)

#define	HATARI_TRACE_VIDEO_ALL		( HATARI_TRACE_VIDEO_SYNC | HATARI_TRACE_VIDEO_RES | HATARI_TRACE_VIDEO_COLOR \
		| HATARI_TRACE_VIDEO_BORDER_V | HATARI_TRACE_VIDEO_BORDER_H | HATARI_TRACE_VIDEO_ADDR \
		| HATARI_TRACE_VIDEO_VBL | HATARI_TRACE_VIDEO_HBL | HATARI_TRACE_VIDEO_STE )

#define HATARI_TRACE_MFP_ALL		( HATARI_TRACE_MFP_EXCEPTION | HATARI_TRACE_MFP_START | HATARI_TRACE_MFP_READ | HATARI_TRACE_MFP_WRITE )

#define	HATARI_TRACE_PSG_ALL		( HATARI_TRACE_PSG_WRITE_REG | HATARI_TRACE_PSG_WRITE_DATA )

#define	HATARI_TRACE_CPU_ALL		( HATARI_TRACE_CPU_PAIRING | HATARI_TRACE_CPU_DISASM | HATARI_TRACE_CPU_EXCEPTION )




#ifndef HATARI_TRACE_ACTIVATED

#define HATARI_TRACE( level, args... )	{}
#define HATARI_TRACE_LEVEL( level )	(0)

#else

#define	HATARI_TRACE( level, args... ) \
	if ( HatariTraceFlags & level ) fprintf ( stderr , args )
#define HATARI_TRACE_LEVEL( level )	(HatariTraceFlags & level)

#endif


#define HATARI_TRACE_PRINT( args... )	fprintf ( stderr , args )



extern Uint32 HatariTraceFlags;


int ParseTraceOptions (char *OptionsStr);


#endif		/* HATARI_LOG_H */
