/*
  Hatari - log.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Logger functions.

  When Hatari runs, it can output information, debug, warning and error texts
  to the error log file and/or displays them in alert dialog boxes.
*/
char Log_rcsid[] = "Hatari $Id: log.c,v 1.1 2005-04-05 14:41:28 thothy Exp $";

#include <stdio.h>
#include <stdarg.h>

#include "main.h"
#include "log.h"


static FILE *hLogFile;


/*-----------------------------------------------------------------------*/
/*
  Initialize the logging functions (open the log file etc.).
*/
void Log_Init(void)
{
	char szString[FILENAME_MAX];

	if (TRUE)
	{
		hLogFile = stderr;
	}
	else
	{
		sprintf(szString, "%s/errlog.txt", szWorkingDir);
		hLogFile = fopen(szString, "w");
	}
}


/*-----------------------------------------------------------------------*/
/*
  Un-Initialize - close error log file etc.
*/
void Log_UnInit(void)
{
	if (hLogFile != stdout && hLogFile != stderr)
	{
		fclose(hLogFile);
	}
	hLogFile = NULL;
}


/*-----------------------------------------------------------------------*/
/*
  Output string to log file
*/
void Log_Printf(LOGTYPE nType, const char *psFormat, ...)
{
  char szBuffer[1024];
  va_list argptr;

  va_start(argptr, psFormat);
  vsprintf(szBuffer, psFormat, argptr);
  va_end(argptr);

  fwrite(szBuffer, sizeof(char), strlen(szBuffer), hLogFile);
}
