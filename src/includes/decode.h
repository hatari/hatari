/*
  Hatari
*/

extern   short int PendingInterruptCount;
extern   int SoundCycles;


/*extern   unsigned long Regs[16+1];*/
/*extern   unsigned short int SR,SR_Before;*/
/*extern   unsigned long PC;*/
/*extern   unsigned long *Reg_SuperSP,*Reg_UserSP;*/

#ifndef UAESYSDEPS
#include "sysdeps.h"
#endif
#ifndef UAEMEMORY
#include "memory.h"
#endif
#ifndef UAENEWCPU
#include "newcpu.h"
#endif
#define Regs regs.regs       /* Ouah - uggly hack - FIXME! */
#define SR regs.sr
#define PC regs.pc
#define Reg_SuperSP regs.isp
#define Reg_UserSP regs.usp


/* Offset ST address to PC pointer: */
#define STRAM_ADDR(Var)  ( (unsigned long)STRam+((unsigned long)Var&0x00ffffff) )

/*-----------------------------------------------------------------------*/
/* Offsets from memory space program counter to immediate data(in wrong endian) */
#define IMM_BYTE  SIZE_BYTE              /* Byte offset */
#define IMM_WORD
#define IMM_LONG

/*-----------------------------------------------------------------------*/
/* Set clock times for each instruction, see '68000 timing' pages for details */
/* NOTE All times are rounded up to nearest 4 cycles */
#define  ROUND_CYCLES_TO4(var)  (((int)(var)+3)&0xfffffffc)

static inline void ADD_CYCLES(op,r,w)
{
 PendingInterruptCount-= (op+3)&0xfffffffc;
 SoundCycles += (op+3)&0xfffffffc;
}


//-----------------------------------------------------------------------
// Decode table structure offsets, see 'DecodeTable[]' MUST match 'decode.inc'
#define DECODE_CYCLES    0                // Cycles for this instruction, includes <ea> etc...
#define DECODE_FUNCTION    4              // Decode function to call
#define DECODE_EA      8                  // <ea> Decode function
#define DECODE_1      12                  // src/dest parameters
#define DECODE_2      16
#define DECODE_DISASS    20               // Disassembly function
#define SIZEOF_DECODE    8

//-----------------------------------------------------------------------
#define PENDING_INTERRUPT_FLAG_MFP      0x0001    // 'PendingInterruptFlag' masks, MUST match 'decode.inc'
#define PENDING_INTERRUPT_FLAG_TRACE    0x0002
#define  CLEAR_PENDING_INTERRUPT_FLAG_MFP  0xfffe
#define  CLEAR_PENDING_INTERRUPT_FLAG_TRACE  0xfffd

//-----------------------------------------------------------------------
#define INSTRUCTION_HISTORY_SIZE  8192            // MUST be ^2, and MUST match 'decode.inc'
#define INSTRUCTION_HISTORY_MASK  (INSTRUCTION_HISTORY_SIZE-1)

//-----------------------------------------------------------------------
#define  NUM_BREAKPOINTS    8                     // MUST match 'decode.inc'

//-----------------------------------------------------------------------
// 68000 instruction emulation functions

/*
extern   unsigned char PCCodeTable_NZVC[4096];
extern   unsigned char PCCodeTable_NZV_CX_SAME[4096];
extern   unsigned long ShiftCycleTable_ByteWord[64];
extern   unsigned long ShiftCycleTable_Long[64];
extern   int InsPC;
extern   BOOL bInSuperMode;
extern   unsigned long StackSave;
extern   unsigned long DecodeTable[65536*SIZEOF_DECODE];
extern   unsigned long InstructionHistory[INSTRUCTION_HISTORY_SIZE];
extern   int InstructionHistoryIndex;
extern   unsigned long BreakPointInstruction;
extern   unsigned long AddressBreakpoints[NUM_BREAKPOINTS];
extern   unsigned long BreakOnAddress;
extern   unsigned long CPUStopped;
*/
extern   unsigned long EmuCCode;
extern   unsigned char STRam[16*1024*1024];
extern   short int PendingInterruptFlag;
extern   void *PendingInterruptFunction;
extern   unsigned long STRamEnd;
extern   unsigned long STRamEnd_BusErr;
extern   unsigned long ExceptionVector;
extern   unsigned long BusAddressLocation;
