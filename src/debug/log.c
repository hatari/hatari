/*
 * Hatari - log.c
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * Logger functions.
 *
 * When Hatari runs, it can output information, debug, warning and error texts
 * to the error log file and/or displays them in alert dialog boxes.
 *
 * It can also dynamically output trace messages, based on the content
 * of LogTraceFlags. Multiple trace levels can be set at once, by setting
 * the corresponding bits in LogTraceFlags.
 */
const char Log_fileid[] = "Hatari log.c : " __DATE__ " " __TIME__;

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "log.h"
#include "screen.h"
#include "file.h"
#include "vdi.h"
#include "options.h"

int ExceptionDebugMask;

typedef struct {
	Uint64 flag;
	const char *name;
} flagname_t;

static flagname_t ExceptionFlags[] = {
	{ EXCEPT_NONE,      "none" },

	{ EXCEPT_BUS,       "bus" },
	{ EXCEPT_ADDRESS,   "address" },
	{ EXCEPT_ILLEGAL,   "illegal" },
	{ EXCEPT_ZERODIV,   "zerodiv" },
	{ EXCEPT_CHK,       "chk" },
	{ EXCEPT_TRAPV,     "trapv" },
	{ EXCEPT_PRIVILEGE, "privilege" },
	{ EXCEPT_TRACE,     "trace" },
	{ EXCEPT_NOHANDLER, "nohandler" },

	{ EXCEPT_DSP,       "dsp" },

	{ EXCEPT_AUTOSTART, "autostart" },

	{ EXCEPT_ALL,       "all" }
};

#if ENABLE_TRACING
static flagname_t TraceFlags[] = {
	{ TRACE_NONE		 , "none" },

	{ TRACE_VIDEO_SYNC	 , "video_sync" } ,
	{ TRACE_VIDEO_RES	 , "video_res" } ,
	{ TRACE_VIDEO_COLOR	 , "video_color" } ,
	{ TRACE_VIDEO_BORDER_V   , "video_border_v" } ,
	{ TRACE_VIDEO_BORDER_H   , "video_border_h" } ,
	{ TRACE_VIDEO_ADDR	 , "video_addr" } ,
	{ TRACE_VIDEO_HBL	 , "video_hbl" } ,
	{ TRACE_VIDEO_VBL	 , "video_vbl" } ,
	{ TRACE_VIDEO_STE	 , "video_ste" } ,
	{ TRACE_VIDEO_ALL	 , "video_all" } ,

	{ TRACE_MFP_EXCEPTION	 , "mfp_exception" } ,
	{ TRACE_MFP_START	 , "mfp_start" } ,
	{ TRACE_MFP_READ	 , "mfp_read" } ,
	{ TRACE_MFP_WRITE	 , "mfp_write" } ,
	{ TRACE_MFP_ALL 	 , "mfp_all" } ,

	{ TRACE_PSG_READ	 , "psg_read" } ,
	{ TRACE_PSG_WRITE	 , "psg_write" } ,
	{ TRACE_PSG_ALL 	 , "psg_all" } ,

	{ TRACE_CPU_PAIRING	 , "cpu_pairing" } ,
	{ TRACE_CPU_DISASM	 , "cpu_disasm" } ,
	{ TRACE_CPU_EXCEPTION	 , "cpu_exception" } ,
	{ TRACE_CPU_ALL 	 , "cpu_all" } ,

	{ TRACE_INT		 , "int" } ,

	{ TRACE_FDC		 , "fdc" } ,

	{ TRACE_ACIA		 , "acia" } ,

	{ TRACE_IKBD_CMDS	 , "ikbd_cmds" } ,
	{ TRACE_IKBD_ACIA	 , "ikbd_acia" } ,
	{ TRACE_IKBD_EXEC	 , "ikbd_exec" } ,
	{ TRACE_IKBD_ALL	 , "ikbd_all" } ,

	{ TRACE_BLITTER 	 , "blitter" } ,

	{ TRACE_OS_BIOS 	 , "bios" },
	{ TRACE_OS_XBIOS	 , "xbios" },
	{ TRACE_OS_GEMDOS	 , "gemdos" },
	{ TRACE_OS_VDI  	 , "vdi" },
	{ TRACE_OS_AES  	 , "aes" },
	{ TRACE_OS_ALL  	 , "os_all" } ,

	{ TRACE_IOMEM_RD	 , "io_read" } ,
	{ TRACE_IOMEM_WR	 , "io_write" } ,
	{ TRACE_IOMEM_ALL	 , "io_all" } ,

	{ TRACE_DMASND  	 , "dmasound" } ,

	{ TRACE_CROSSBAR  	 , "crossbar" } ,

	{ TRACE_VIDEL  	         , "videl" } ,

	{ TRACE_DSP_HOST_INTERFACE, "dsp_host_interface" },
	{ TRACE_DSP_HOST_COMMAND , "dsp_host_command" },
	{ TRACE_DSP_HOST_SSI	 , "dsp_host_ssi" },
	{ TRACE_DSP_INTERRUPT	 , "dsp_interrupt" },
	{ TRACE_DSP_DISASM	 , "dsp_disasm" },
	{ TRACE_DSP_DISASM_REG	 , "dsp_disasm_reg" },
	{ TRACE_DSP_DISASM_MEM	 , "dsp_disasm_mem" },
	{ TRACE_DSP_STATE	 , "dsp_state" },
	{ TRACE_DSP_ALL		 , "dsp_all" },

	{ TRACE_DSP_SYMBOLS	 , "dsp_symbols" },
	{ TRACE_CPU_SYMBOLS	 , "cpu_symbols" },

	{ TRACE_NVRAM		 , "nvram" } ,

	{ TRACE_SCSI_CMD	 , "scsi_cmd" } ,

	{ TRACE_NATFEATS	 , "natfeats" } ,

	{ TRACE_KEYMAP		 , "keymap" } ,

	{ TRACE_MIDI		 , "midi" } ,

	{ TRACE_IDE		 , "ide" } ,

	{ TRACE_OS_BASE		 , "os_base" } ,

	{ TRACE_SCSIDRV		 , "scsidrv" } ,

	{ TRACE_ALL		 , "all" }
};
#endif /* ENABLE_TRACING */


Uint64	LogTraceFlags = TRACE_NONE;
FILE *TraceFile = NULL;

static FILE *hLogFile = NULL;
static LOGTYPE TextLogLevel;
static LOGTYPE AlertDlgLogLevel;

/*-----------------------------------------------------------------------*/
/**
 * Set default files to stderr (used at the very start, before parsing options)
 */
void Log_Default(void)
{
	hLogFile = stderr;
	TraceFile = stderr;
	TextLogLevel = LOG_INFO;
}

/*-----------------------------------------------------------------------*/
/**
 * Initialize the logging and tracing functionality (open the log files etc.).
 * 
 * Return zero if that fails.
 */
int Log_Init(void)
{
	TextLogLevel = ConfigureParams.Log.nTextLogLevel;
	AlertDlgLogLevel = ConfigureParams.Log.nAlertDlgLogLevel;

	hLogFile = File_Open(ConfigureParams.Log.sLogFileName, "w");
	TraceFile = File_Open(ConfigureParams.Log.sTraceFileName, "w");
   
	return (hLogFile && TraceFile);
}

/**
 * Set Alert log level temporarily without config change.
 * 
 * Return old level for restoring the original level with this.
 */
int Log_SetAlertLevel(int level)
{
	int old = AlertDlgLogLevel;
	AlertDlgLogLevel = level;
	return old;
}


/*-----------------------------------------------------------------------*/
/**
 * Un-Initialize - close log files etc.
 */
void Log_UnInit(void)
{
	hLogFile = File_Close(hLogFile);
	TraceFile = File_Close(TraceFile);
}


/*-----------------------------------------------------------------------*/
/**
 * Output string to log file
 */
void Log_Printf(LOGTYPE nType, const char *psFormat, ...)
{
	va_list argptr;

	if (hLogFile && nType <= TextLogLevel)
	{
		va_start(argptr, psFormat);
		vfprintf(hLogFile, psFormat, argptr);
		va_end(argptr);
		/* Add a new-line if necessary: */
		if (psFormat[strlen(psFormat)-1] != '\n')
			fputs("\n", hLogFile);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Show logging alert dialog box and output string to log file
 */
void Log_AlertDlg(LOGTYPE nType, const char *psFormat, ...)
{
	va_list argptr;

	/* Output to log file: */
	if (hLogFile && nType <= TextLogLevel)
	{
		va_start(argptr, psFormat);
		vfprintf(hLogFile, psFormat, argptr);
		va_end(argptr);
		/* Add a new-line if necessary: */
		if (psFormat[strlen(psFormat)-1] != '\n')
			fputs("\n", hLogFile);
	}

	/* Show alert dialog box: */
	if (sdlscrn && nType <= AlertDlgLogLevel)
	{
		char *psTmpBuf;
		psTmpBuf = malloc(2048);
		if (!psTmpBuf)
		{
			perror("Log_AlertDlg");
			return;
		}
		va_start(argptr, psFormat);
		vsnprintf(psTmpBuf, 2048, psFormat, argptr);
		va_end(argptr);
		DlgAlert_Notice(psTmpBuf);
		free(psTmpBuf);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * parse what log level should be used and return it
 */
LOGTYPE Log_ParseOptions(const char *arg)
{
	const char *levels[] = {
		"fatal", "error", "warn", "info", "todo", "debug", NULL
	};
	LOGTYPE level = LOG_FATAL;
	const char **level_str;
	char *input, *str;

	input = strdup(arg);
	str = input;
	while (*str)
	{
		*str++ = tolower((unsigned char)*arg++);
	}
	for (level_str = levels; *level_str; level_str++, level++)
	{
		if (strcmp(input, *level_str) == 0)
		{
			free(input);
			return level;
		}
	}
	free(input);
	return level;
}


/*-----------------------------------------------------------------------*/
/**
 * Parse a list of comma separated strings.
 * If the string is prefixed with an optional '+',
 * corresponding mask flag is turned on.
 * If the string is prefixed with a '-',
 * corresponding mask flag is turned off.
 * Return error string (""=silent 'error') or NULL for success.
 */
static const char*
Log_ParseOptionFlags (const char *FlagsStr, flagname_t *Flags, int MaxFlags, Uint64 *Mask)
{
	char *FlagsCopy;
	char *cur, *sep;
	int i;
	int Mode;				/* 0=add, 1=del */
	
	/* special case for "help" : display the list of possible settings */
	if (strcmp (FlagsStr, "help") == 0)
	{
		fprintf(stderr, "\nList of available option flags :\n");
		
		for (i = 0; i < MaxFlags; i++)
			fprintf(stderr, "  %s\n", Flags[i].name);
		
		fprintf(stderr, "Multiple flags can be separated by ','.\n");
		fprintf(stderr, "They can be prefixed by '+' or '-' to be mixed.\n");
		fprintf(stderr, "Giving just 'none' flag disables all of them.\n\n");
		return "";
	}
	
	if (strcmp (FlagsStr, "none") == 0)
	{
		return NULL;
	}
	
	FlagsCopy = strdup(FlagsStr);
	if (!FlagsCopy)
	{
		return "strdup error in Log_OptionFlags";
	}
	
	cur = FlagsCopy;
	while (cur)
	{
		sep = strchr(cur, ',');
		if (sep)			/* end of next options */
			*sep++ = '\0';
		
		Mode = 0;				/* default is 'add' */
		if (*cur == '+')
		{ Mode = 0; cur++; }
		else if (*cur == '-')
		{ Mode = 1; cur++; }
		
		for (i = 0; i < MaxFlags; i++)
		{
			if (strcmp(cur, Flags[i].name) == 0)
				break;
		}
		
		if (i < MaxFlags)		/* option found */
		{
			if (Mode == 0)
				*Mask |= Flags[i].flag;
			else
				*Mask &= (~Flags[i].flag);
		}
		else
		{
			fprintf(stderr, "Unknown flag type '%s'\n", cur);
			free(FlagsCopy);
			return "Unknown flag type.";
		}
		
		cur = sep;
	}

	//fprintf(stderr, "flags parse <%x>\n", Mask);
	
	free (FlagsCopy);
	return NULL;
}

/**
 * Parse exception flags and store results in ExceptionDebugMask.
 * Return error string or NULL for success.
 * 
 * See Log_ParseOptionFlags() for details.
 */
const char* Log_SetExceptionDebugMask (const char *FlagsStr)
{
	const char *errstr;

	Uint64 mask = EXCEPT_NONE;
	errstr = Log_ParseOptionFlags(FlagsStr, ExceptionFlags, ARRAY_SIZE(ExceptionFlags), &mask);
	ConfigureParams.Log.nExceptionDebugMask = mask;
	return errstr;
}


#if ENABLE_TRACING

/**
 * Parse trace flags and store results in LogTraceFlags.
 * Return error string or NULL for success.
 * 
 * See Log_ParseOptionFlags() for details.
 */
const char* Log_SetTraceOptions (const char *FlagsStr)
{
	const char *errstr;

	LogTraceFlags = TRACE_NONE;
	errstr = Log_ParseOptionFlags(FlagsStr, TraceFlags, ARRAY_SIZE(TraceFlags), &LogTraceFlags);

	/* Enable Hatari flags needed for tracing selected items */
	if (LogTraceFlags & (TRACE_OS_AES|TRACE_OS_VDI))
		bVdiAesIntercept = true;

	if ((LogTraceFlags & TRACE_OS_BASE) && ConOutDevice == CONOUT_DEVICE_NONE)
		ConOutDevice = 2;

	return errstr;
}

/**
 * Readline match callback for trace type name completion.
 * STATE = 0 -> different text from previous one.
 * Return next match or NULL if no matches.
 */
char *Log_MatchTrace(const char *text, int state)
{
	static int i, len;
	const char *name;
	
	if (!state) {
		/* first match */
		len = strlen(text);
		i = 0;
	}
	/* next match */
	while (i < ARRAY_SIZE(TraceFlags)) {
		name = TraceFlags[i++].name;
		if (strncasecmp(name, text, len) == 0)
			return (strdup(name));
	}
	return NULL;
}

#else	/* !ENABLE_TRACING */

/** dummy */
const char* Log_SetTraceOptions (const char *FlagsStr)
{
	return "Hatari has been compiled without ENABLE_TRACING!";
}

/** dummy */
char *Log_MatchTrace(const char *text, int state)
{
	return NULL;
}

#endif	/* !ENABLE_TRACING */
