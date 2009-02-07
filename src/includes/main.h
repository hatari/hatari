/*
  Hatari - main.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MAIN_H
#define HATARI_MAIN_H


/* Name and version for window title: */
#define PROG_NAME "Hatari devel (" __DATE__ ")"
//#define PROG_NAME "Hatari v1.2.0"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <SDL_types.h>
#include <stdbool.h>

#ifndef FALSE
#define FALSE false
#define TRUE  true
#endif

#ifdef WIN32
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

#define CALL_VAR(func)  { ((void(*)(void))func)(); }


/* 68000 operand sizes */
#define SIZE_BYTE  1
#define SIZE_WORD  2
#define SIZE_LONG  4


extern bool bQuitProgram;
extern bool bEnableDebug;

extern void Main_MemorySnapShot_Capture(bool bSave);
extern bool Main_PauseEmulation(bool visualize);
extern bool Main_UnPauseEmulation(void);
extern void Main_RequestQuit(void);
extern void Main_WaitOnVbl(void);
extern void Main_WarpMouse(int x, int y);
extern void Main_EventHandler(void);

#endif /* ifndef HATARI_MAIN_H */
