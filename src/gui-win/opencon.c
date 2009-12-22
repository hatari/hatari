/*
  Hatari - opencon.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  The SDL library redirects the stdio normally to the files stdout.txt and stderr.txt.
  But with this redirection, the debugger of Hatari does not work anymore.
  So we simply open a new console when the debug mode has been enabled, and we redirect
  the stdio again - this time to our new console.
*/

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <SDL_types.h>

#include "opencon.h"
#include "../debug/debugui.h"


void Win_OpenCon(void)
{
	if (!bExceptionDebugging)
		return;

	AllocConsole();
	freopen("CON", "w", stdout);
	freopen("CON", "r", stdin);
	freopen("CON", "wr", stderr);
}
