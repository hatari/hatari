/*
  Hatari

  Error Log file

  When Hatari runs, it outputs text to the error log file to show if the system initialised
  correctly. Using this file output we can also list which video modes are present under
  DirectDraw and such like.
*/

#include <stdio.h>
#include <stdarg.h>

#include "main.h"
#include "debug.h"

/*#define USEERRLOG*/

static FILE *errlog;

//-----------------------------------------------------------------------
/*
  Create error log file
*/
void ErrLog_OpenFile(void)
{
#ifdef USEERRLOG
  char szString[MAX_FILENAME_LENGTH];

  sprintf(szString,"%s/errlog.txt",szWorkingDir);
  errlog = fopen(szString, "w");
#endif
}

//-----------------------------------------------------------------------
/*
  Close error log file
*/
void ErrLog_CloseFile(void)
{
#ifdef USEERRLOG
  fclose(errlog);
#endif
}

//-----------------------------------------------------------------------
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
