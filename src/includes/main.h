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

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

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

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (int)(sizeof(x)/sizeof(x[0]))
#endif

/* 68000 operand sizes */
#define SIZE_BYTE  1
#define SIZE_WORD  2
#define SIZE_LONG  4


extern bool bQuitProgram;

extern bool Main_PauseEmulation(bool visualize);
extern bool Main_UnPauseEmulation(void);
extern void Main_RequestQuit(int exitval);
extern void Main_SetQuitValue(int exitval);
extern uint32_t Main_SetRunVBLs(uint32_t vbls);
extern const char* Main_SetVBLSlowdown(int factor);
extern void Main_WaitOnVbl(void);
extern void Main_WarpMouse(int x, int y, bool restore);
extern bool Main_ShowCursor(bool show);
extern void Main_EventHandler(void);
extern void Main_SetTitle(const char *title);
extern void Main_ErrorExit(const char *msg1, const char *msg2, int errval);

#endif /* ifndef HATARI_MAIN_H */
