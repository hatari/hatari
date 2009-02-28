 /*
  * Hatari - Fix for compliation using Visual Studio 6
  *
  * This file is distributed under the GNU Public License, version 2 or at
  * your option any later version. Read the file gpl.txt for details.
  */

#if defined(_VCWIN_)
#pragma comment(lib, ".\\SDL\\lib\\sdl.lib")			// sdl.lib	
#pragma comment(lib, ".\\zlib\\win32\\zlib1.lib")		// zlib1.lib 
#endif

#if defined(_VCWIN_)
	#include <tchar.h>
	#include <stdio.h>
	#include <stdarg.h>
#endif

extern	FILE *TraceFile;

#if defined(_VCWIN_)
	#ifndef _INC_HATARI_TRACE
	#define _INC_HATARI_TRACE

	#ifdef HATARI_TRACE_ACTIVATED
		void	HATARI_TRACE( int level, ...)
		{
			va_list	x;
			va_start(x,level);
			if ( HatariTraceFlags & level ) _vftprintf(TraceFile,level, x);
			va_end	(x);
		};
	#else		/* HATARI_TRACE_ACTIVATED */
		void	HATARI_TRACE( int level, ...)
		{
		}

	#endif		/* HATARI_TRACE_ACTIVATED */

	void HATARI_TRACE_PRINT(char*	strFirstString, ...)	
	{
		va_list	x;
		va_start(x,strFirstString);
		_vftprintf(TraceFile,strFirstString, x);
		va_end	(x);				
	};

	#endif

#endif
