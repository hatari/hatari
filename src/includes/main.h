/*
  Hatari - main.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MAIN_H
#define HATARI_MAIN_H

typedef int BOOL;

#define PROG_NAME      "Hatari v0.61" /* Name, version for window title */
#define PROG_VERSION   "v0.61"

/*#define DEBUG_TO_FILE*/             /* Use debug.txt files */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <SDL_types.h>


#define MAX_STRING_LENGTH  512

#ifndef FALSE
#define FALSE 0
#define TRUE (!0)
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
#define SR_AUX      0x0010
#define SR_NEG      0x0008
#define SR_ZERO      0x0004
#define SR_OVERFLOW    0x0002
#define SR_CARRY    0x0001

#define SR_CLEAR_AUX    0xffef
#define SR_CLEAR_NEG    0xfff7
#define SR_CLEAR_ZERO    0xfffb
#define SR_CLEAR_OVERFLOW  0xfffd
#define SR_CLEAR_CARRY    0xfffe

#define SR_CCODE_MASK    (SR_AUX|SR_NEG|SR_ZERO|SR_OVERFLOW|SR_CARRY)
#define SR_MASK      0xFFE0

#define SR_TRACEMODE    0x8000
#define SR_SUPERMODE    0x2000
#define SR_IPL      0x0700

#define SR_CLEAR_IPL    0xf8ff
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

/* Find IPL - don't forget to call MakeSR() before you use it! */
#define FIND_IPL    ((SR>>8)&0x7)

/* Size of 68000 instructions */
#define MAX_68000_INSTRUCTION_SIZE  10  /* Longest 68000 instruction is 10 bytes(6+4) */
#define MIN_68000_INSTRUCTION_SIZE  2   /* Smallest 68000 instruction is 2 bytes(ie NOP) */

/*
  All the following processor timings are based on a bog standard 8MHz 68000 as found in all standard ST's

  Clock cycles per line (50Hz)      : 512
  NOPs per scan line (50Hz)         : 128
  Scan lines per VBL (50Hz)         : 313 (64 at top,200 screen,49 bottom)

  Clock cycles per line (60Hz)      : 508
  NOPs per scan line (60Hz)         : 127
  Scan lines per VBL (60Hz)         : 263

  Clock cycles per VBL (50Hz)       : 160256
  NOPs per VBL (50Hz)               : 40064

  Pixels per clock cycle (low res)  : 1
  Pixels per clock cycle (med res)  : 2
  Pixels per clock cycle (high res) : 4
  Pixels per NOP (low res)          : 4
  Pixels per NOP (med res)          : 8
  Pixels per NOP (high res)         : 16
*/
#define SCREEN_START_HBL   64           /* This is usually the first line of the displayed screen */
#define SCREEN_HEIGHT_HBL  200          /* This is usually the height of the screen */
#define FIRST_VISIBLE_HBL  (SCREEN_START_HBL-OVERSCAN_TOP)    /* Normal screen starts 64 lines in, top border is 28 lines */
#define NUM_VISIBLE_LINES  (OVERSCAN_TOP+SCREEN_HEIGHT_HBL+OVERSCAN_BOTTOM)  /* Number of visible screen lines including top/bottom borders */

/* Assumes 32 pixels left+right */
#define SCREENBYTES_LEFT    16          /* Bytes for left border in ST screen */
#define SCREENBYTES_MIDDLE  160         /* Middle(320 pixels) */
#define SCREENBYTES_RIGHT   16          /* right border */
#define SCREENBYTES_LINE    (SCREENBYTES_LEFT+SCREENBYTES_MIDDLE+SCREENBYTES_RIGHT)

/* Overscan values */
#define OVERSCAN_LEFT       (SCREENBYTES_LEFT*2)    /* Number of pixels in each border */
#define OVERSCAN_RIGHT      (SCREENBYTES_RIGHT*2)
#define OVERSCAN_TOP        29
#define OVERSCAN_BOTTOM     38
#define OVERSCAN_MIDDLE     320         /* Number of pixels across screen(low res) */

#define SCREEN_START_CYCLE  96          /* Cycle first normal pixel appears on */
#define SCANLINES_PER_FRAME 313         /* Number of scan lines per frame */
#define CYCLES_PER_LINE     512         /* Cycles per horiztonal line scan */
#define CYCLES_VBL_IN       (SCREEN_START_HBL*CYCLES_PER_LINE)     /* ((28+64)*CYCLES_PER_LINE) */
#define CYCLES_PER_FRAME    (SCANLINES_PER_FRAME*CYCLES_PER_LINE)  /* Cycles per VBL @ 50fps = 160256 */
#define CYCLES_PER_SEC      (CYCLES_PER_FRAME*50) /* Cycles per second */
#define CYCLES_ENDLINE      (64+320+88+40)        /* DE(Display Enable) */
#define CYCLES_HBL          (CYCLES_PER_LINE+96)  /* Cycles for first HBL - very inaccurate on ST */

/* Illegal Opcode used to help emulation. eg. free entries are 8 to 15 inc' */
#define  GEMDOS_OPCODE        8  /* Free op-code to intercept GemDOS trap */
#define  SYSINIT_OPCODE      10  /* Free op-code to initialize system (connected drives etc.) */
#define  VDI_OPCODE          12  /* Free op-code to call VDI handlers AFTER Trap#2 */


#define PRG_HEADER_SIZE    0x1c  /* Size of header at start of ST .prg files */


extern BOOL bQuitProgram;
extern BOOL bEnableDebug;
extern BOOL bEmulationActive;
extern char szBootDiscImage[FILENAME_MAX];
extern char szWorkingDir[FILENAME_MAX];


extern void Main_MemorySnapShot_Capture(BOOL bSave);
extern void Main_SysError(char *Error,char *Title);
extern int Main_Message(char *pText, char *pCaption);
extern void Main_PauseEmulation(void);
extern void Main_UnPauseEmulation(void);
extern void Main_WarpMouse(int x, int y);
extern void Main_EventHandler(void);

#endif /* ifndef HATARI_MAIN_H */
