/*
  Hatari - main.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MAIN_H
#define HATARI_MAIN_H


/* Name and version for window title: */
#define PROG_NAME "Hatari CVS (" __DATE__ ")"
// #define PROG_NAME "Hatari v0.96"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <SDL_types.h>


typedef signed char BOOL;

#ifndef FALSE
#define FALSE 0
#define TRUE (!0)
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

/* 68000 Register defines */
enum {
  REG_D0,    /* D0.. */
  REG_D1,
  REG_D2,
  REG_D3,
  REG_D4,
  REG_D5,
  REG_D6,
  REG_D7,    /* ..D7 */
  REG_A0,    /* A0.. */
  REG_A1,
  REG_A2,
  REG_A3,
  REG_A4,
  REG_A5,
  REG_A6,
  REG_A7,    /* ..A7 (also SP) */
};

/* 68000 Condition code's */
#define SR_AUX              0x0010
#define SR_NEG              0x0008
#define SR_ZERO             0x0004
#define SR_OVERFLOW         0x0002
#define SR_CARRY            0x0001

#define SR_CLEAR_AUX        0xffef
#define SR_CLEAR_NEG        0xfff7
#define SR_CLEAR_ZERO       0xfffb
#define SR_CLEAR_OVERFLOW   0xfffd
#define SR_CLEAR_CARRY      0xfffe

#define SR_CCODE_MASK       (SR_AUX|SR_NEG|SR_ZERO|SR_OVERFLOW|SR_CARRY)
#define SR_MASK             0xFFE0

#define SR_TRACEMODE        0x8000
#define SR_SUPERMODE        0x2000
#define SR_IPL              0x0700

#define SR_CLEAR_IPL        0xf8ff
#define SR_CLEAR_TRACEMODE  0x7fff
#define SR_CLEAR_SUPERMODE  0xdfff

/* Exception vectors */
#define  EXCEPTION_BUSERROR   0x00000008
#define  EXCEPTION_ADDRERROR  0x0000000c
#define  EXCEPTION_ILLEGALINS 0x00000010
#define  EXCEPTION_DIVZERO    0x00000014
#define  EXCEPTION_CHK        0x00000018
#define  EXCEPTION_TRAPV      0x0000001c
#define  EXCEPTION_TRACE      0x00000024
#define  EXCEPTION_LINE_A     0x00000028
#define  EXCEPTION_LINE_F     0x0000002c
#define  EXCEPTION_HBLANK     0x00000068
#define  EXCEPTION_VBLANK     0x00000070
#define  EXCEPTION_TRAP0      0x00000080
#define  EXCEPTION_TRAP1      0x00000084
#define  EXCEPTION_TRAP2      0x00000088
#define  EXCEPTION_TRAP13     0x000000B4
#define  EXCEPTION_TRAP14     0x000000B8


/* Size of 68000 instructions */
#define MAX_68000_INSTRUCTION_SIZE  10  /* Longest 68000 instruction is 10 bytes(6+4) */
#define MIN_68000_INSTRUCTION_SIZE  2   /* Smallest 68000 instruction is 2 bytes(ie NOP) */

/* Illegal Opcode used to help emulation. eg. free entries are 8 to 15 inc' */
#define  GEMDOS_OPCODE        8  /* Free op-code to intercept GemDOS trap */
#define  SYSINIT_OPCODE      10  /* Free op-code to initialize system (connected drives etc.) */
#define  VDI_OPCODE          12  /* Free op-code to call VDI handlers AFTER Trap#2 */


#define PRG_HEADER_SIZE    0x1c  /* Size of header at start of ST .prg files */


extern BOOL bQuitProgram;
extern BOOL bEnableDebug;
extern char szWorkingDir[FILENAME_MAX];


extern void Main_MemorySnapShot_Capture(BOOL bSave);
extern void Main_PauseEmulation(void);
extern void Main_UnPauseEmulation(void);
extern void Main_RequestQuit(void);
extern void Main_WaitOnVbl(void);
extern void Main_WarpMouse(int x, int y);
extern void Main_EventHandler(void);

#endif /* ifndef HATARI_MAIN_H */
