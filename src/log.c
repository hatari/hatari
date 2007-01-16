/*
  Hatari - log.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Logger functions.

  When Hatari runs, it can output information, debug, warning and error texts
  to the error log file and/or displays them in alert dialog boxes.
*/
const char Log_rcsid[] = "Hatari $Id: log.c,v 1.4 2007-01-16 18:42:59 thothy Exp $";

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "log.h"
#include "screen.h"


static FILE *hLogFile = NULL;


/*-----------------------------------------------------------------------*/
/**
 * Initialize the logging functions (open the log file etc.).
 */
void Log_Init(void)
{
	/* First check for "stdout" and "stderr" which are special */
	if (!strcmp(ConfigureParams.Log.sLogFileName, "stdout"))
	{
		hLogFile = stdout;
	}
	else if (!strcmp(ConfigureParams.Log.sLogFileName, "stderr"))
	{
		hLogFile = stderr;
	}
	else
	{
		/* Open a normal log file */
		hLogFile = fopen(ConfigureParams.Log.sLogFileName, "w");
		if (!hLogFile)
			fprintf(stderr, "Can't open log file %s: %s\n",
			        ConfigureParams.Log.sLogFileName, strerror(errno));
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Un-Initialize - close error log file etc.
 */
void Log_UnInit(void)
{
	if (hLogFile && hLogFile != stdout && hLogFile != stderr)
	{
		fclose(hLogFile);
	}
	hLogFile = NULL;
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
