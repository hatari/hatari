/*
 * Hatari - log.c
 *
 * This file is distributed under the GNU Public License, version 2 or at
 * your option any later version. Read the file gpl.txt for details.
 *
 * Logger functions.
 *
 * When Hatari runs, it can output information, debug, warning and error texts
 * to the error log file and/or displays them in alert dialog boxes.
 *
 * It can also dynamically output trace messages, based on the content
 * of HatariTraceFlags. Multiple trace levels can be set at once, by setting
 * the corresponding bits in HatariTraceFlags
 */
const char Log_rcsid[] = "Hatari $Id: log.c,v 1.12 2008-04-06 12:39:46 eerot Exp $";

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


struct {
	Uint32 Level;
	const char *Name;
}
TraceOptions[] = {
	{ HATARI_TRACE_VIDEO_SYNC	, "video_sync" } ,
	{ HATARI_TRACE_VIDEO_RES	, "video_res" } ,
	{ HATARI_TRACE_VIDEO_COLOR	, "video_color" } ,
	{ HATARI_TRACE_VIDEO_BORDER_V	, "video_border_v" } ,
	{ HATARI_TRACE_VIDEO_BORDER_H	, "video_border_h" } ,
	{ HATARI_TRACE_VIDEO_ADDR	, "video_addr" } ,
	{ HATARI_TRACE_VIDEO_HBL	, "video_hbl" } ,
	{ HATARI_TRACE_VIDEO_VBL	, "video_vbl" } ,
	{ HATARI_TRACE_VIDEO_STE	, "video_ste" } ,
	{ HATARI_TRACE_VIDEO_ALL	, "video_all" } ,

	{ HATARI_TRACE_MFP_EXCEPTION	, "mfp_exception" } ,
	{ HATARI_TRACE_MFP_START	, "mfp_start" } ,
	{ HATARI_TRACE_MFP_READ		, "mfp_read" } ,
	{ HATARI_TRACE_MFP_WRITE	, "mfp_write" } ,
	{ HATARI_TRACE_MFP_ALL		, "mfp_all" } ,

	{ HATARI_TRACE_PSG_WRITE_REG	, "psg_write_reg" } ,
	{ HATARI_TRACE_PSG_WRITE_DATA	, "psg_write_data" } ,
	{ HATARI_TRACE_PSG_ALL		, "psg_all" } ,

	{ HATARI_TRACE_CPU_PAIRING	, "cpu_pairing" } ,
	{ HATARI_TRACE_CPU_DISASM	, "cpu_disasm" } ,
	{ HATARI_TRACE_CPU_EXCEPTION	, "cpu_exception" } ,
	{ HATARI_TRACE_CPU_ALL		, "cpu_all" } ,

	{ HATARI_TRACE_INT		, "int" } ,

	{ HATARI_TRACE_FDC		, "fdc" } ,

	{ HATARI_TRACE_IKBD		, "ikbd" } ,

	{ HATARI_TRACE_OS_BIOS		, "bios" },
	{ HATARI_TRACE_OS_XBIOS		, "xbios" },
	{ HATARI_TRACE_OS_GEMDOS	, "gemdos" },
	{ HATARI_TRACE_OS_VDI		, "vdi" },
	{ HATARI_TRACE_OS_ALL		, "os_all" } ,

	{ HATARI_TRACE_ALL		, "all" }
};


Uint32	HatariTraceFlags = HATARI_TRACE_NONE;
FILE *TraceFile = NULL;

static FILE *hLogFile = NULL;
static LOGTYPE TextLogLevel;
static LOGTYPE AlertDlgLogLevel;

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

	if (hLogFile && (int)nType <= TextLogLevel)
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
		"fail", "error", "warn", "info", "todo", "debug", NULL
	};
	const char **level_str;
	char *input, *str;
	LOGTYPE level;

	input = strdup(arg);
	str = input;
	while (*str)
	{
		*str++ = tolower(*arg++);
	}
	for (level = 0, level_str = levels; *level_str; level_str++, level++)
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
 * corresponding trace flag is turned on.
 * If the string is prefixed with a '-',
 * corresponding trace flag is turned off.
 * Result is stored in HatariTraceFlags.
 */
BOOL Log_SetTraceOptions (const char *OptionsStr)
{
	char *OptionsCopy;
	char *cur, *sep;
	int i;
	int Mode;				/* 0=add, 1=del */
	int MaxOptions;

	MaxOptions = sizeof(TraceOptions) / sizeof(TraceOptions[0]);
	
	/* special case for "help" : display the list of possible trace levels */
	if (strcmp (OptionsStr, "help") == 0)
	{
		fprintf(stderr, "\nList of available trace levels :\n");
		
		for (i = 0; i < MaxOptions; i++)
			fprintf(stderr, "  %s\n", TraceOptions[i].Name);
		
		fprintf(stderr, "Multiple trace levels can be separated by ','\n");
		fprintf(stderr, "Levels can be prefixed by '+' or '-' to be mixed.\n\n");
		return FALSE;
	}

#ifndef HATARI_TRACE_ACTIVATED
	fprintf(stderr, "\nError: Trace option has not been activated during compile time.\n");
	exit(1);
#endif
	
	HatariTraceFlags = HATARI_TRACE_NONE;
	
	OptionsCopy = strdup(OptionsStr);
	if (!OptionsCopy)
	{
		fprintf(stderr, "strdup error in ParseTraceOptions\n");
		return FALSE;
	}
	
	cur = OptionsCopy;
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
		
		for (i = 0; i < MaxOptions; i++)
		{
			if (strcmp(cur, TraceOptions[i].Name) == 0)
				break;
		}
		
		if (i < MaxOptions)		/* option found */
		{
			if (Mode == 0)
				HatariTraceFlags |= TraceOptions[i].Level;
			else
				HatariTraceFlags &= (~TraceOptions[i].Level);
		}
		else
		{
			fprintf(stderr, "unknown trace option %s\n", cur);
			free(OptionsCopy);
			return FALSE;
		}
		
		cur = sep;
	}
	
	//fprintf(stderr, "trace parse <%x>\n", HatariTraceFlags);
	
	free (OptionsCopy);
	return TRUE;
}
