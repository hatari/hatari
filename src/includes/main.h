/*
  Hatari
*/

#ifndef MAIN_H
#define MAIN_H

typedef int BOOL;

#define PROG_NAME      "Hatari v0.11" /* Name, version for window title */
#define PROG_VERSION        "v0.11"
#define VERSION_STRING      "0.11 "   /* Always 6 bytes(inc' NULL) */
#define VERSION_STRING_SIZE    6      /* Size of above(inc' NULL) */

//#define TOTALLY_FINAL_VERSION       /* Web release version... */

#define FINAL_VERSION                 /* Full-speed non-debug version for release */
//#define DEBUG_TO_FILE               /* Use debug.txt files */
#define FIND_PERFORMANCE

#ifndef FINAL_VERSION
  #define USE_DEBUGGER                /* Debugger version(non-release) */
  #define DEBUG_TO_FILE               /* Use debug.txt files */
#endif

#ifndef TOTALLY_FINAL_VERSION
  #define FORCE_WORKING_DIR           /* Set default directory when running in MsDev */
#endif

#ifdef TOTALLY_FINAL_VERSION
  #undef DEBUG_TO_FILE                /* Don't use debug files for final release */
  #undef FIND_PERFORMANCE
#endif

#define MAX_FILENAME_LENGTH  256
#define MAX_STRING_LENGTH  512

#define  DIRECTINPUT_VERSION  0x0500

#define MAX_PASSED_PARAMETERS  4      /* Number of passed parameters from command line */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <assert.h>


#ifndef FALSE
#define FALSE 0
#define TRUE (!0)
#endif

#define CALL_VAR(func)  { ((void(*)(void))func)(); }

// Binary conversion macro's
#define BIN2(a,b) ((a<<1)+(b))
#define BIN3(a,b,c) ((a<<2)+(b<<1)+(c))
#define BIN4(a,b,c,d) ((a<<3)+(b<<2)+(c<<1)+(d))
#define BIN5(a,b,c,d,e) ((a<<4)+(b<<3)+(c<<2)+(d<<1)+(e))
#define BIN6(a,b,c,d,e,f) ((a<<5)+(b<<4)+(c<<3)+(d<<2)+(e<<1)+(f))
#define BIN7(a,b,c,d,e,f,g) ((a<<6)+(b<<5)+(c<<4)+(d<<3)+(e<<2)+(f<<1)+(g))
#define BIN8(a,b,c,d,e,f,g,h) ((a<<7)+(b<<6)+(c<<5)+(d<<4)+(e<<3)+(f<<2)+(g<<1)+(h))
#define BIN10(a,b,c,d,e,f,g,h,i,j) ((a<<9)+(b<<8)+(c<<7)+(d<<6)+(e<<5)+(f<<4)+(g<<3)+(h<<2)+(i<<1)+(j))
#define BIN12(a,b,c,d,e,f,g,h,i,j,k,l) ((a<<11)+(b<<10)+(c<<9)+(d<<8)+(e<<7)+(f<<6)+(g<<5)+(h<<4)+(i<<3)+(j<<2)+(k<<1)+(l))
#define BIN14(a,b,c,d,e,f,g,h,i,j,k,l,m,n) ((a<<13)+(b<<12)+(c<<11)+(d<<10)+(e<<9)+(f<<8)+(g<<7)+(h<<6)+(i<<5)+(j<<4)+(k<<3)+(l<<2)+(m<<1)+(n))
#define BIN16(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p) ((a<<15)+(b<<14)+(c<<13)+(d<<12)+(e<<11)+(f<<10)+(g<<9)+(h<<8)+(i<<7)+(j<<6)+(k<<5)+(l<<4)+(m<<3)+(n<<2)+(o<<1)+(p))

// 68000 operand sizes
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
  REG_A7,    /* ..A7(also SP) */
  REG_A8=17  /* User/Super Stack Pointer */ /* FIXME: Nasty remap to regs.isp in decode.h */
};

/* PC Condition code's */
#define PC_CARRY    0x0001  // Bit 0
#define PC_AUX      0x0010  // Bit 4
#define PC_ZERO      0x0040  // Bit 6
#define PC_NEG      0x0080  // Bit 7
#define PC_OVERFLOW    0x0800  // Bit 11

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

/* Emuation condition codes, ordered so can do 'xor al,al' to set XNZVC -0000 */
#define  EMU_X    0x0100
#define  EMU_N    0x0080
#define  EMU_Z    0x0040
#define  EMU_V    0x0020
#define  EMU_C    0x0010

#define  EMU_CLEAR_X  0xfeff
#define  EMU_CLEAR_N  0xff7f
#define  EMU_CLEAR_Z  0xffbf
#define  EMU_CLEAR_V  0xffdf
#define  EMU_CLEAR_C  0xffef

/* Exception vectors */
#define  EXCEPTION_BUSERROR  0x00000008
#define  EXCEPTION_ADDRERROR  0x0000000c
#define EXCEPTION_ILLEGALINS  0x00000010
#define  EXCEPTION_DIVZERO  0x00000014
#define EXCEPTION_CHK    0x00000018
#define EXCEPTION_TRAPV    0x0000001c
#define EXCEPTION_TRACE    0x00000024
#define  EXCEPTION_LINE_A  0x00000028
#define  EXCEPTION_LINE_F  0x0000002c
#define  EXCEPTION_HBLANK  0x00000068
#define  EXCEPTION_VBLANK  0x00000070
#define EXCEPTION_TRAP0    0x00000080
#define EXCEPTION_TRAP1    0x00000084
#define EXCEPTION_TRAP2    0x00000088
#define EXCEPTION_TRAP13  0x000000B4
#define EXCEPTION_TRAP14  0x000000B8

/* Find IPL */
#define FIND_IPL    ((SR>>8)&0x7)

/* Size of 68000 instructions */
#define MAX_68000_INSTRUCTION_SIZE  10  /* Longest 68000 instruction is 10 bytes(6+4) */
#define MIN_68000_INSTRUCTION_SIZE  2   /* Smallest 68000 instruction is 2 bytes(ie NOP) */

/*
  All the following processor timings are based on a bog standard 8MHz 68000 as found in all standard ST's

  Clock cycles per line (50Hz)      : 512
  NOPs per scan line (50Hz)         : 128
  Scan lines per VBL (50Hz)         : 313(64 at top,200 screen,49 bottom)

  Clock cycles per line (60Hz)      : 508
  NOPs per scan line (60Hz)         : 127
  Scan lines per VBL (60Hz)         : 315

  Clock cycles per VBL              : 160256
  NOPs per VBL                      : 40064

  Pixels per clock cycle (low res)  : 1
  Pixels per clock cycle (med res)  : 2
  Pixels per clock cycle (high res) : 4
  Pixels per NOP (low res)          : 4
  Pixels per NOP (med res)          : 8
  Pixels per NOP (high res)         : 16
*/
#define SCREEN_START_HBL  64            /* This is usually the first line of the displayed screen */
#define SCREEN_HEIGHT_HBL  200          /* This is usually the height of the screen */
#define FIRST_VISIBLE_HBL  (SCREEN_START_HBL-OVERSCAN_TOP)    /* Normal screen starts 64 lines in, top border is 28 lines */
#define  NUM_VISIBLE_LINES  (OVERSCAN_TOP+SCREEN_HEIGHT_HBL+OVERSCAN_BOTTOM)  /* Number of visible screen lines including top/bottom borders */

/* Assumes 32 pixels left+right */
#define SCREENBYTES_LEFT    16          /* Bytes for left border in ST screen */
#define SCREENBYTES_MIDDLE    160       /* Middle(320 pixels) */
#define SCREENBYTES_RIGHT    16         /* right border */
#define SCREENBYTES_LINE    (SCREENBYTES_LEFT+SCREENBYTES_MIDDLE+SCREENBYTES_RIGHT)

/* Overscan values */
#define OVERSCAN_LEFT      (SCREENBYTES_LEFT*2)    /* Number of pixels in each border */
#define OVERSCAN_RIGHT      (SCREENBYTES_RIGHT*2)
#define OVERSCAN_TOP      29
#define OVERSCAN_BOTTOM      38
#define OVERSCAN_MIDDLE      320        /* Number of pixels across screen(low res) */

#define SCREEN_START_CYCLE    96        /* Cycle first normal pixel appears on */
#define SCANLINES_PER_FRAME    313      /* Number of scan lines per frame */
#define CYCLES_PER_LINE      512        /* Cycles per horiztonal line scan */
#define CYCLES_PER_FRAME    (SCANLINES_PER_FRAME*CYCLES_PER_LINE)  /* Cycles per VBL @ 50fps = 160256 */
#define CYCLES_VBL_IN      (SCREEN_START_HBL*CYCLES_PER_LINE)  /*((28+64)*CYCLES_PER_LINE) */
#define CYCLES_PER_SEC      (CYCLES_PER_FRAME*50)    /* Cycles per second */
#define CYCLES_ENDLINE      (64+320+88+40)      /* DE(Display Enable) */
#define CYCLES_HBL      (CYCLES_PER_LINE+96)    /* Cycles for first HBL - very inaccurate on ST */
#define CYCLES_DEBUGGER      3000       /* Check debugger every 'x' cycles */

/* Illegal Opcode used to help emulation. eg. free entries are 8 to 15 inc' */
#define  GEMDOS_OPCODE        8  /* Free op-code to intercept GemDOS trap */
#define  RUNOLDGEMDOS_OPCODE  9  /* Free op-code to set PC to old GemDOS vector(if doesn't need to intercept) */
#define  CONDRV_OPCODE       10  /* Free op-code to intercept set up connected drives */
#define  TIMERD_OPCODE       11  /* Free op-code to prevent Timer D starting in GemDOS */
#define  VDI_OPCODE          12  /* Free op-code to call VDI handlers AFTER Trap#2 */
#define  LINEA_OPCODE        13  /* Free op-code to call handlers AFTER Line-A */
/* Other Opcodes */
#define RTS_OPCODE  BIN16(0,1,0,0,1,1,1,0,0,1,1,1,0,1,0,1)
#define NOP_OPCODE  BIN16(0,1,0,0,1,1,1,0,0,1,1,1,0,0,0,1)
#define BRAW_OPCODE  BIN16(0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0)

/* Handy invalid address for flags(24-bit address can never be this) */
#define BRK_DISABLED      0xffffffff
#define BRK_SINGLE_INSTRUCTION    0xfffffffe
#define  BRK_STOP      0xfffffffd

#define PRG_HEADER_SIZE    0x1c  /* Size of header at start of ST .prg files */

/* Emulation states */
enum {
  EMULATION_INACTIVE,
  EMULATION_ACTIVE
};

extern BOOL bQuitProgram;
extern BOOL bEnableDebug;
extern BOOL bEmulationActive;
extern char szName[];
extern char szBootDiscImage[MAX_FILENAME_LENGTH];
extern char szWorkingDir[MAX_FILENAME_LENGTH];
extern char szCurrentDir[MAX_FILENAME_LENGTH];

extern void Main_MemorySnapShot_Capture(BOOL bSave);
extern void Main_SysError(char *Error,char *Title);
extern int Main_Message(char *lpText, char *lpCaption /*, unsigned int uType*/);
extern void Main_PauseEmulation(void);
extern void Main_UnPauseEmulation(void);
extern void Main_EventHandler();
extern void Main_WaitVBLEvent(void);
extern BOOL Main_AlreadyWaitingVBLEvent(void);
/*extern void Main_SoundTimerFunc(void);*/
extern void Main_SetSpeedThreadTimer(int nMinMaxSpeed);

#endif /* ifndef MAIN_H */
