 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Standard write_log that writes to the console
  *
  * Copyright 2001 Bernd Schmidt
  */
#include "sysconfig.h"
#include "sysdeps.h"

#ifndef WINUAE_FOR_HATARI
bool bCpuWriteLog = true;

void write_log (const char *fmt, ...)
{
	va_list ap;

	if (!bCpuWriteLog)
		return;

	va_start (ap, fmt);
	vfprintf (stderr, fmt, ap);
	va_end (ap);
}

#endif

void f_out (void *f, const TCHAR *format, ...)
{
	va_list parms;

	if (f == NULL)
		return;

	va_start (parms, format);
	vfprintf (f, format, parms);
	va_end (parms);
}
