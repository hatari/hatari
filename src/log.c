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
 * of HatariTraceLevel. Multiple trace levels can be set at once, by setting
 * the corresponding bits in HatariTraceLevel
 */
const char Log_rcsid[] = "Hatari $Id: log.c,v 1.7 2008-04-03 20:30:32 eerot Exp $";

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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


	{ HATARI_TRACE_ALL		, "all" }
};


Uint32	HatariTraceLevel = HATARI_TRACE_NONE;


static FILE *hLogFile = NULL;


/*-----------------------------------------------------------------------*/
/**
 * Initialize the logging functions (open the log file etc.).
 */
void Log_Init(void)
{
	hLogFile = File_Open(ConfigureParams.Log.sLogFileName, "w");
}


/*-----------------------------------------------------------------------*/
/**
 * Un-Initialize - close error log file etc.
 */
void Log_UnInit(void)
{
	hLogFile = File_Close(hLogFile);
}


/*-----------------------------------------------------------------------*/
/**
 * Output string to log file
 */
void Log_Printf(LOGTYPE nType, const char *psFormat, ...)
{
	va_list argptr;

	if (hLogFile && (int)nType <= ConfigureParams.Log.nTextLogLevel)
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
	if (hLogFile && (int)nType <= ConfigureParams.Log.nTextLogLevel)
	{
		va_start(argptr, psFormat);
		vfprintf(hLogFile, psFormat, argptr);
		va_end(argptr);
		/* Add a new-line if necessary: */
		if (psFormat[strlen(psFormat)-1] != '\n')
			fputs("\n", hLogFile);
	}

	/* Show alert dialog box: */
	if (sdlscrn && (int)nType <= ConfigureParams.Log.nAlertDlgLogLevel)
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
 * Parse a list of comma separated strings.
 * If the string is prefixed with an optional '+',
 * corresponding trace level is turned on.
 * If the string is prefixed with a '-',
 * corresponding trace level is turned off.
 * Result is stored in HatariTraceLevel.
 */
int ParseTraceOptions (char *OptionsStr)
{
	char *OptionsCopy;
	char *cur, *sep;
	int i;
	int Mode;				/* 0=add, 1=del */
	int MaxOptions;

#ifndef HATARI_TRACE_ACTIVATED
	fprintf(stderr, "\nError: Trace option has not been activated during compile time.\n");
	exit(1);
#endif

	MaxOptions = sizeof(TraceOptions) / sizeof(TraceOptions[0]);
	
	/* special case for "help" : display the list of possible trace levels */
	if (strcmp (OptionsStr, "help") == 0)
	{
		fprintf(stderr, "\nList of available trace levels :\n");
		
		for (i = 0; i < MaxOptions; i++)
			fprintf(stderr, "  %s\n", TraceOptions[i].Name);
		
		fprintf(stderr, "Multiple trace levels can be separated by ','\n");
		fprintf(stderr, "Levels can be prefixed by '+' or '-' to be mixed.\n\n");
		return 0;
	}
	
	HatariTraceLevel = HATARI_TRACE_NONE;
	
	OptionsCopy = strdup(OptionsStr);
	if (!OptionsCopy)
	{
		fprintf(stderr, "strdup error in ParseTraceOptions\n");
		return 0;
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
				HatariTraceLevel |= TraceOptions[i].Level;
			else
				HatariTraceLevel &= (~TraceOptions[i].Level);
		}
		else
		{
			fprintf(stderr, "unknown trace option %s\n", cur);
			free(OptionsCopy);
			return 0;
		}
		
		cur = sep;
	}
	
	//fprintf(stderr, "trace parse <%x>\n", HatariTraceLevel);
	
	free (OptionsCopy);
	return 1;
}
