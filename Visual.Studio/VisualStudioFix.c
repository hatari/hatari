 /*
  * Hatari - Fix for compliation using Visual Studio 6
  *
  * This file is distributed under the GNU General Public License, version 2
  * or at your option any later version. Read the file gpl.txt for details.
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

#include	"log.h"

extern	FILE *TraceFile;

#if defined(_VCWIN_)
	#ifndef _INC_HATARI_TRACE
	#define _INC_HATARI_TRACE

	#if ENABLE_TRACING
		void LOG_TRACE(int level, const char* format, ...)
		{
			va_list	x;
			va_start(x,format);
			if ( HatariTraceFlags & level ) _vftprintf(TraceFile,format, x);
			va_end	(x);
		};
	#else		/* ENABLE_TRACING */
		void LOG_TRACE(int level, ...)
		{
		}

	#endif		/* ENABLE_TRACING */

	void LOG_TRACE_PRINT(char* strFirstString, ...)
	{
		va_list	x;
		va_start(x,strFirstString);
		_vftprintf(TraceFile,strFirstString, x);
		va_end	(x);
	};

	#endif

#endif


#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

// Windows Header Files:
#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

extern int SDL_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	return SDL_main(argc,argv);
}	

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow)
{
	return SDL_main(1,&lpCmdLine);
}

#ifdef __cplusplus
}
#endif
