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
const char Log_fileid[] = "Hatari log.c";

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "main.h"
#include "configuration.h"
#include "console.h"
#include "dialog.h"
#include "log.h"
#include "screen.h"
#include "file.h"
#include "vdi.h"
#include "options.h"
#include "str.h"

int ExceptionDebugMask;

typedef struct {
	uint64_t flag;
	const char *name;
} flagname_t;

static flagname_t ExceptionFlags[] = {
	{ EXCEPT_NONE,      "none" },

	{ EXCEPT_NOHANDLER, "nohandler" },
	{ EXCEPT_BUS,       "bus" },
	{ EXCEPT_ADDRESS,   "address" },
	{ EXCEPT_ILLEGAL,   "illegal" },
	{ EXCEPT_ZERODIV,   "zerodiv" },
	{ EXCEPT_CHK,       "chk" },
	{ EXCEPT_TRAPV,     "trapv" },
	{ EXCEPT_PRIVILEGE, "privilege" },
	{ EXCEPT_TRACE,     "trace" },
	{ EXCEPT_LINEA,     "linea" },
	{ EXCEPT_LINEF,     "linef" },

	{ EXCEPT_DSP,       "dsp" },

	{ EXCEPT_AUTOSTART, "autostart" },

	{ EXCEPT_ALL,       "all" }
};

#if ENABLE_TRACING
static flagname_t TraceFlags[] = {
	{ TRACE_ALL		 , "all" },
	{ TRACE_NONE		 , "none" },

	{ TRACE_ACIA		 , "acia" },

	{ TRACE_OS_AES  	 , "aes" },

	{ TRACE_OS_BIOS 	 , "bios" },

	{ TRACE_BLITTER 	 , "blitter" },

	{ TRACE_CPU_ALL 	 , "cpu_all" },
	{ TRACE_CPU_DISASM	 , "cpu_disasm" },
	{ TRACE_CPU_EXCEPTION	 , "cpu_exception" },
	{ TRACE_CPU_PAIRING	 , "cpu_pairing" },
	{ TRACE_CPU_REGS	 , "cpu_regs" },
	{ TRACE_CPU_SYMBOLS	 , "cpu_symbols" },
	{ TRACE_CPU_VIDEO_CYCLES , "cpu_video_cycles" },

	{ TRACE_CROSSBAR  	 , "crossbar" },

	{ TRACE_DMASND  	 , "dmasound" },

	{ TRACE_DSP_ALL		 , "dsp_all" },
	{ TRACE_DSP_DISASM	 , "dsp_disasm" },
	{ TRACE_DSP_DISASM_REG	 , "dsp_disasm_reg" },
	{ TRACE_DSP_DISASM_MEM	 , "dsp_disasm_mem" },
	{ TRACE_DSP_HOST_COMMAND , "dsp_host_command" },
	{ TRACE_DSP_HOST_INTERFACE,"dsp_host_interface" },
	{ TRACE_DSP_HOST_SSI	 , "dsp_host_ssi" },
	{ TRACE_DSP_INTERRUPT	 , "dsp_interrupt" },
	{ TRACE_DSP_STATE	 , "dsp_state" },
	{ TRACE_DSP_SYMBOLS	 , "dsp_symbols" },

	{ TRACE_FDC		 , "fdc" },

	{ TRACE_OS_GEMDOS	 , "gemdos" },

	{ TRACE_IDE		 , "ide" },

	{ TRACE_IKBD_ALL	 , "ikbd_all" },
	{ TRACE_IKBD_ACIA	 , "ikbd_acia" },
	{ TRACE_IKBD_CMDS	 , "ikbd_cmds" },
	{ TRACE_IKBD_EXEC	 , "ikbd_exec" },

	{ TRACE_INT		 , "int" },

	{ TRACE_IOMEM_ALL	 , "io_all" },
	{ TRACE_IOMEM_RD	 , "io_read" },
	{ TRACE_IOMEM_WR	 , "io_write" },

	{ TRACE_KEYMAP		 , "keymap" },

	{ TRACE_MEM		 , "mem" },

	{ TRACE_MFP_ALL 	 , "mfp_all" },
	{ TRACE_MFP_EXCEPTION	 , "mfp_exception" },
	{ TRACE_MFP_READ	 , "mfp_read" },
	{ TRACE_MFP_START	 , "mfp_start" },
	{ TRACE_MFP_WRITE	 , "mfp_write" },

	{ TRACE_MIDI		 , "midi" },
	{ TRACE_MIDI_RAW	 , "midi_raw" },

	{ TRACE_NATFEATS	 , "natfeats" },

	{ TRACE_NVRAM		 , "nvram" },

	{ TRACE_OS_ALL  	 , "os_all" },
	{ TRACE_OS_BASE		 , "os_base" },

	{ TRACE_PSG_ALL 	 , "psg_all" },
	{ TRACE_PSG_READ	 , "psg_read" },
	{ TRACE_PSG_WRITE	 , "psg_write" },

	{ TRACE_SCC		 , "scc" },

	{ TRACE_SCSI_CMD	 , "scsi_cmd" },

	{ TRACE_SCSIDRV		 , "scsidrv" },

	{ TRACE_OS_VDI  	 , "vdi" },

	{ TRACE_VIDEL  	         , "videl" },

	{ TRACE_VIDEO_ALL	 , "video_all" },
	{ TRACE_VIDEO_ADDR	 , "video_addr" },
	{ TRACE_VIDEO_COLOR	 , "video_color" },
	{ TRACE_VIDEO_BORDER_H   , "video_border_h" },
	{ TRACE_VIDEO_BORDER_V   , "video_border_v" },
	{ TRACE_VIDEO_HBL	 , "video_hbl" },
	{ TRACE_VIDEO_RES	 , "video_res" },
	{ TRACE_VIDEO_STE	 , "video_ste" },
	{ TRACE_VIDEO_SYNC	 , "video_sync" },
	{ TRACE_VIDEO_VBL	 , "video_vbl" },

	{ TRACE_VME		 , "vme" },

	{ TRACE_OS_XBIOS	 , "xbios" },
};
#endif /* ENABLE_TRACING */


uint64_t LogTraceFlags = TRACE_NONE;
FILE *TraceFile = NULL;


/* SDL GUI Alerts can show 4*50 chars at max, and much longer
 * console messages are not very readable either, just slow
 */
#define MAX_MSG_LEN 256
#define REPEAT_LIMIT_INIT 8

/* FILE* for output stream, message line repeat suppression limit,
 * current repeat count, and previous line content for checking
 * repetition
 */
static struct {
	/* prev msg fp, in case same msg goes to multiple FILE*s */
	FILE *fp;
	int limit;
	int count;
	char prev[MAX_MSG_LEN];
} MsgState;

static FILE *hLogFile = NULL;

/* local settings, to be able change them temporarily */
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
	MsgState.limit = REPEAT_LIMIT_INIT;
}

/**
 * Set local log levels from configuration values
 */
void Log_SetLevels(void)
{
	TextLogLevel = ConfigureParams.Log.nTextLogLevel;
	AlertDlgLogLevel = ConfigureParams.Log.nAlertDlgLogLevel;
}

/*-----------------------------------------------------------------------*/
/**
 * Initialize the logging and tracing functionality (open the log files etc.).
 * 
 * Return zero if that fails.
 */
int Log_Init(void)
{
	Log_SetLevels();

	/* Flush pending msg & drop cached prev msg FILE pointer
	 * before default log & trace FILE pointers change
	 */
	Log_ResetMsgRepeat();

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
	/* Flush pending msg & drop cached prev msg FILE pointer
	 * before log & trace FILE pointers change
	 */
	Log_ResetMsgRepeat();

	hLogFile = File_Close(hLogFile);
	TraceFile = File_Close(TraceFile);
}

/*-----------------------------------------------------------------------
 * log/trace message repeat suppression handling
 */

static void printMsgRepeat(FILE *fp)
{
	/* strings already include trailing newline */
	fprintf(fp, "%d repeats of: %s", MsgState.count, MsgState.prev);
}

/**
 * If there is a pending message that has not been output yet,
 * output it and return true, otherwise false.
 */
static bool printPendingMsgRepeat(FILE *fp)
{
	if (likely(MsgState.count == 0))
		return false;
	if (MsgState.count > 1)
		printMsgRepeat(fp);
	else
		fputs(MsgState.prev, fp);
	return true;
}

/**
 * Output pending and given messages when appropriate,
 * and cache given fp & message if it's not a repeat.
 */
static void addMsgRepeat(FILE *fp, const char *line)
{
	/* repeated message? */
	if (fp == MsgState.fp &&
	    unlikely(strcmp(line, MsgState.prev) == 0))
	{
		MsgState.count++;
		/* limit crossed? -> print + increase repeat limit */
		if (unlikely(MsgState.count >= MsgState.limit))
		{
			printMsgRepeat(fp);
			MsgState.limit *= 2;
			MsgState.count = 0;
			fflush(fp);
		}
		return;
	}
	/* no repeat -> print previous message/repeat */
	printPendingMsgRepeat(MsgState.fp);

	/* store + print new message */
	Str_Copy(MsgState.prev, line, sizeof(MsgState.prev));
	MsgState.limit = REPEAT_LIMIT_INIT;
	MsgState.count = 0;
	MsgState.fp = fp;
	fputs(line, fp);
	fflush(fp);
}

/**
 * Output pending messages repeat info and reset repeat info.
 */
void Log_ResetMsgRepeat(void)
{
	if (!printPendingMsgRepeat(MsgState.fp))
	{
		MsgState.fp = NULL;
		return;
	}
	MsgState.prev[0] = '\0';
	if (MsgState.limit)
		MsgState.limit = REPEAT_LIMIT_INIT;
	MsgState.count = 0;
	MsgState.fp = NULL;
}

/**
 * Toggle whether message repeats are shown
 */
void Log_ToggleMsgRepeat(void)
{
	if (MsgState.limit)
	{
		fprintf(stderr, "Message repeats will be shown as-is\n");
		MsgState.limit = 0;
	}
	else
	{
		fprintf(stderr, "Message repeats will be suppressed\n");
		MsgState.limit = REPEAT_LIMIT_INIT;
	}
	Log_ResetMsgRepeat();
}

/*-----------------------------------------------------------------------*/
/**
 * Add log prefix to given string and return its lenght
 */
static int Log_AddPrefix(char *msg, int len, LOGTYPE idx)
{
	static const char* prefix[] = LOG_NAMES;

	assert(idx >= 0 && idx < ARRAY_SIZE(prefix));
	return snprintf(msg, len, "%s: ", prefix[idx]);
}

/**
 * Add a new-line if it's missing. 'msg' points to place
 * where it should be, and size is buffer size.
 */
static void addMissingNewline(char *msg, int size)
{
	assert(size > 2);
	if (size > 2 && msg[0] != '\n')
	{
		msg[1] = '\n';
		msg[2] = '\0';
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Output string to log file
 */
void Log_Printf(LOGTYPE nType, const char *psFormat, ...)
{
	if (!(hLogFile && nType <= TextLogLevel))
		return;

	char line[sizeof(MsgState.prev)];
	int count, len = sizeof(line);
	char *msg = line;

	count = Log_AddPrefix(line, len, nType);
	msg += count;
	len -= count;

	va_list argptr;
	va_start(argptr, psFormat);
	count = vsnprintf(msg, len, psFormat, argptr);
	va_end(argptr);
	msg += count;
	len -= count;

	addMissingNewline(msg-1, len+1);
	if (MsgState.limit)
		addMsgRepeat(hLogFile, line);
	else
		fputs(line, hLogFile);
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
		char line[sizeof(MsgState.prev)];
		int count, len = sizeof(line);
		char *msg = line;

		count = Log_AddPrefix(line, len, nType);
		msg += count;
		len -= count;

		va_start(argptr, psFormat);
		count = vsnprintf(msg, len, psFormat, argptr);
		va_end(argptr);
		msg += count;
		len -= count;

		addMissingNewline(msg-1, len+1);
		if (MsgState.limit)
			addMsgRepeat(hLogFile, line);
		else
			fputs(line, hLogFile);
	}

	/* Show alert dialog box: */
	if (sdlscrn && nType <= AlertDlgLogLevel)
	{
		char buf[MAX_MSG_LEN];
		va_start(argptr, psFormat);
		vsnprintf(buf, sizeof(buf), psFormat, argptr);
		va_end(argptr);
		DlgAlert_Notice(buf);
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
 * Parse a list of comma separated strings:
 * - Unless whole string is prefixed with '+'/'-', mask is initially zeroed
 * - Optionally prefixing flag name with '+' turns given mask flag on
 * - Prefixing flag name with '-' turns given mask flag off
 * Return error string (""=silent 'error') or NULL for success.
 */
static const char*
Log_ParseOptionFlags (const char *FlagsStr, flagname_t *Flags, int MaxFlags, uint64_t *Mask)
{
	char *FlagsCopy;
	char *cur, *sep;
	int i;
	int Mode;				/* 0=add, 1=del */
	enum {
		FLAG_ADD,
		FLAG_DEL
	};
	
	/* special case for "help" : display the list of possible settings */
	if (strcmp (FlagsStr, "help") == 0)
	{
		fprintf(stderr, "\nList of available option flags :\n");
		
		for (i = 0; i < MaxFlags; i++)
			fprintf(stderr, "  %s\n", Flags[i].name);
		
		fprintf(stderr,
			"Multiple flags can be separated by ','.\n"
			"Giving just 'none' flag disables all of them.\n\n"
			"Unless first flag starts with -/+ character, flags from\n"
			"previous trace command are zeroed.  Prefixing flag with\n"
			"'-' removes it from set, (optional) '+' adds it to set\n"
			"(which is useful at run-time in debugger).\n\n"
		       );
		return "";
	}
	
	if (strcmp (FlagsStr, "none") == 0)
	{
		*Mask = 0;
		return NULL;
	}
	
	FlagsCopy = strdup(FlagsStr);
	if (!FlagsCopy)
	{
		return "strdup error in Log_OptionFlags";
	}
	
	cur = FlagsCopy;
	/* starting anew, not modifiying old set? */
	if (*cur != '+' && *cur != '-')
		*Mask = 0;

	while (cur)
	{
		sep = strchr(cur, ',');
		if (sep)			/* end of next options */
			*sep++ = '\0';
		
		Mode = FLAG_ADD;
		if (*cur == '+')
			cur++;
		else if (*cur == '-')
		{
			Mode = FLAG_DEL;
			cur++;
		}
		for (i = 0; i < MaxFlags; i++)
		{
			if (strcmp(cur, Flags[i].name) == 0)
				break;
		}
		
		if (i < MaxFlags)		/* option found */
		{
			if (Mode == FLAG_ADD)
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

	uint64_t mask = ConfigureParams.Debugger.nExceptionDebugMask;
	errstr = Log_ParseOptionFlags(FlagsStr, ExceptionFlags, ARRAY_SIZE(ExceptionFlags), &mask);
	ConfigureParams.Debugger.nExceptionDebugMask = mask;
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

	errstr = Log_ParseOptionFlags(FlagsStr, TraceFlags, ARRAY_SIZE(TraceFlags), &LogTraceFlags);

	/* Enable Hatari flags needed for tracing selected items */
	if (LogTraceFlags & (TRACE_OS_AES|TRACE_OS_VDI))
		bVdiAesIntercept = true;

	if ((LogTraceFlags & TRACE_OS_BASE))
		Console_SetTrace(true);
	else if (!LogTraceFlags)
		Console_SetTrace(false);

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

/**
 * Do trace output with optional repeat suppression
 */
void Log_Trace(const char *format, ...)
{
	va_list argptr;
	char line[sizeof(MsgState.prev)];

	if (!TraceFile)
		return;

	va_start(argptr, format);
	if (MsgState.limit)
	{
		vsnprintf(line, sizeof(line), format, argptr);
		addMsgRepeat(TraceFile, line);
	}
	else
	{
		vfprintf(TraceFile, format, argptr);
		fflush(TraceFile);
	}
	va_end(argptr);
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

/** dummy */
void Log_Trace(const char *format, ...) {}

#endif	/* !ENABLE_TRACING */
