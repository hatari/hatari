/*
  Hatari - main.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MAIN_H
#define HATARI_MAIN_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <SDL_types.h>
#include <stdbool.h>

#if defined(_MSC_VER)
#include "vs-fix.h"
#endif

#if __GNUC__ >= 3
# define likely(x)      __builtin_expect (!!(x), 1)
# define unlikely(x)    __builtin_expect (!!(x), 0)
#else
# define likely(x)      (x)
# define unlikely(x)    (x)
#endif

/* avoid warnings with variables used only in asserts */
#ifdef NDEBUG
# define ASSERT_VARIABLE(x) (void)(x)
#else
# define ASSERT_VARIABLE(x) assert(x)
#endif

#ifdef WIN32
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

#define CALL_VAR(func)  { ((void(*)(void))func)(); }

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (int)(sizeof(x)/sizeof(x[0]))
#endif

/* 68000 operand sizes */
#define SIZE_BYTE  1
#define SIZE_WORD  2
#define SIZE_LONG  4

/* The 8 MHz CPU frequency */
#define CPU_FREQ   8012800

extern bool bQuitProgram;

extern bool Main_PauseEmulation(bool visualize);
extern bool Main_UnPauseEmulation(void);
extern void Main_RequestQuit(int exitval);
extern void Main_SetRunVBLs(Uint32 vbls);
extern bool Main_SetVBLSlowdown(int factor);
extern void Main_WaitOnVbl(void);
extern void Main_WarpMouse(int x, int y, bool restore);
extern void Main_EventHandler(void);
extern void Main_SetTitle(const char *title);

#endif /* ifndef HATARI_MAIN_H */
