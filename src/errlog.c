/*
  Hatari - errlog.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Error Log file.

  When Hatari runs, it outputs text to the error log file to show if the system
  initialised correctly and such like.
*/
char ErrLog_rcsid[] = "Hatari $Id: errlog.c,v 1.4 2003-12-25 14:19:38 thothy Exp $";

#include <stdio.h>
#include <stdarg.h>

#include "main.h"
#include "debug.h"


#undef USEERRLOG


#ifdef USEERRLOG
static FILE *errlog;
#endif


/*-----------------------------------------------------------------------*/
/*
  Create error log file
*/
void ErrLog_OpenFile(void)
{
#ifdef USEERRLOG
  char szString[FILENAME_MAX];

  sprintf(szString,"%s/errlog.txt",szWorkingDir);
  errlog = fopen(szString, "w");
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Close error log file
*/
void ErrLog_CloseFile(void)
{
#ifdef USEERRLOG
  fclose(errlog);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Output string to error log file
*/
void ErrLog_File(char *format, ...)
{
#ifdef USEERRLOG
  char szBuffer[1024];
  va_list argptr;

  va_start(argptr, format);
  vsprintf(szBuffer, format, argptr);
  va_end(argptr);

  fwrite(szBuffer, sizeof(char), strlen(szBuffer), errlog);
#endif
}
