/*
  Hatari

  M68000 - CPU. This handles exception handling as well as a few OpCode's such as Line-F and Line-A.
  We also have a function to save off the last 'x' instruction ran which is very handy for debugging
  as we can see what code was run before a crash etc...
  (Other CPU functions can be found in 'decode.asm/.inc')
*/

#include "main.h"
#include "bios.h"
#include "cart.h"
#include "debug.h"
#include "decode.h"
#include "disass.h"
#include "fdc.h"
#include "gemdos.h"
#include "ikbd.h"
#include "int.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "misc.h"
#include "psg.h"
#include "screen.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"
#include "view.h"
#include "xbios.h"


/* Taken from Decode.asm - Thothy */
/* These should be replaced with UAE's CPU core equivalents one day */
unsigned short SR_Before;
unsigned long ExceptionVector;
short int PendingInterruptFlag;
void *PendingInterruptFunction;
short int PendingInterruptCount;
int SoundCycles;
BOOL bInSuperMode;
unsigned long EmuCCode;
unsigned long BusAddressLocation;


/* Use these if want to bring up any bus/address error, else uses 68000 vector handler */
#ifndef FINAL_VERSION
//  #define TRAP_BUSERROR_HISTORY                 // Output history on true Bus Error(bombs display)
//  #define TRAP_ADDRESSERROR_HISTORY             // Or Address Error
//  #define TRAP_ILLEGALINSTRUCTIONERROR_HISTORY  // Or Illegal Instruction
#endif

BOOL bDoTraceException;          /* Do TRACE? */


//-----------------------------------------------------------------------
/*
  Reset CPU 68000 variables
*/
void M68000_Reset(BOOL bCold)
{
  int i;

  // Clear registers, set PC, SR and stack pointers
  if (bCold) {
    for(i=0; i<(16+1); i++)
      Regs[i] = 0;
  }
  PC = TOSAddress;                            /* Start of TOS image, 0xfc0000 or 0xe00000 */
  SR = 0x2700;                                /* Starting status register */
  bDoTraceException = FALSE;                  /* No TRACE exceptions */
  bInSuperMode = TRUE;                        /* We begin in supervisor mode */
  Regs[REG_A7] = Regs[REG_A8] = 0x0000f000;   /* Stack default */
  PendingInterruptFlag = 0;                   /* Clear pending flag */

  // Read Supervisor Stack/PC for warm reset
  if (!bCold) {
    Regs[REG_A8] = STMemory_ReadLong(0x00000000);
    PC = STMemory_ReadLong(0x00000004);
  }

  // Hold display for extended VDI resolutions(under init our VDI)
  bHoldScreenDisplay = TRUE;
}

//-----------------------------------------------------------------------
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void M68000_MemorySnapShot_Capture(BOOL bSave)
{
  // Save/Restore details
  MemorySnapShot_Store(&bDoTraceException,sizeof(bDoTraceException));
}

//-----------------------------------------------------------------------
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
  This is for 'decode.asm' variables - as cannot use 'decode.c' as will overwrite assembler .obj file!
*/
void M68000_Decode_MemorySnapShot_Capture(BOOL bSave)
{
  int ID;

  // Save/Restore details
  MemorySnapShot_Store(Regs,sizeof(Regs));
  MemorySnapShot_Store(&STRamEnd,sizeof(STRamEnd));
  MemorySnapShot_Store(&STRamEnd_BusErr,sizeof(STRamEnd_BusErr));
  MemorySnapShot_Store(&PendingInterruptCount,sizeof(PendingInterruptCount));
  MemorySnapShot_Store(&PendingInterruptFlag,sizeof(PendingInterruptFlag));
  if (bSave) {
    // Convert function to ID
    ID = Int_HandlerFunctionToID(PendingInterruptFunction);
    MemorySnapShot_Store(&ID,sizeof(int));
  }
  else {
    // Convert ID to function
    MemorySnapShot_Store(&ID,sizeof(int));
    PendingInterruptFunction = Int_IDToHandlerFunction(ID);
  }
  MemorySnapShot_Store(&PC,sizeof(PC));
  MemorySnapShot_Store(&SR,sizeof(SR));
  MemorySnapShot_Store(&SR_Before,sizeof(SR_Before));
  MemorySnapShot_Store(&bInSuperMode,sizeof(bInSuperMode));
  /*MemorySnapShot_Store(&Reg_SuperSP,sizeof(Reg_SuperSP));*//*FIXME*/
  /*MemorySnapShot_Store(&Reg_UserSP,sizeof(Reg_UserSP));*/
  MemorySnapShot_Store(&EmuCCode,sizeof(EmuCCode));
  MemorySnapShot_Store(&ExceptionVector,sizeof(ExceptionVector));  
}

//-----------------------------------------------------------------------
/*
  ILLEGAL - Unknown 68000 OpCode
*/
void M68000_IllegalInstruction(void)
{
fprintf(stderr, "M68000_IllegalInstruction\n");
  ADD_CYCLES(34,4,3);
  ExceptionVector = EXCEPTION_ILLEGALINS;  /* Illegal vector */
  M68000_Exception();        /* Cause trap */
//  RET;
}

//-----------------------------------------------------------------------
/*
  BUSERROR - Access outside valid memory range
*/
void M68000_BusError(unsigned long addr)
{
 /* Reset PC's stack to normal(as may have done many calls) so return to
    correct level after exception.
    Enter here with 'ebp' as address we tried to access */
fprintf(stderr, "M68000_BusError at address $%lx\n", (long)addr);
 BusAddressLocation=addr;    /* Store for exception frame */
 ExceptionVector = EXCEPTION_BUSERROR;  /* Handler */
 M68000_Exception();      /* Cause trap */
/*
 asm("  mov  [BusAddressLocation],ebp\n"      // Store for exception frame
     "  mov  esp,[StackSave]\n"        // Restore stack
     "  mov  [ExceptionVector],EXCEPTION_BUSERROR\n");  // Handler
 SAVE_ASSEM_REGS();            // Save assembly registers
 asm("   call  M68000_Exception\n");        // Cause trap
 RESTORE_ASSEM_REGS();            // Restore assembly registers
 RET;                // Start decoding
*/
}

//-----------------------------------------------------------------------
/*
  ADDRESSERROR - Access incorrect memory boundary, eg byte offset for a word access
*/
void M68000_AddressError(unsigned long addr)
{
fprintf(stderr, "M68000_AddressError at address $%lx\n", (long)addr);
 BusAddressLocation=addr;    /* Store for exception frame */
 ExceptionVector=EXCEPTION_ADDRERROR;  /* Handler */
 M68000_Exception();      /* Cause trap */
/*
 // Reset PC's stack to normal(as may have done many calls) so return to correct level after exception
 __asm {
  // Enter here with 'ebp' as address we tried to access
  mov    [BusAddressLocation],ebp    // Store for exception frame
  mov    esp,[StackSave]        // Restore stack
  mov    [ExceptionVector],EXCEPTION_ADDRERROR  // Handler
  SAVE_ASSEM_REGS            // Save assembly registers
  call  M68000_Exception        // Cause trap
  RESTORE_ASSEM_REGS          // Restore assembly registers
  }
  RET                // Start decoding
*/  
}

//-----------------------------------------------------------------------
/*
  See if in user/super mode and if need to swap SP
*/
void M68000_CheckUserSuperToggle(void)
{
  unsigned long *TempReg;
  unsigned long TempSP;

  // Have we swapped mode?
  if ( (SR_Before&SR_SUPERMODE)!=(SR&SR_SUPERMODE) ) {
    // Yes, swap to ST's REG_A8!
    TempSP = Regs[REG_A7];
    Regs[REG_A7] = Regs[REG_A8];
    Regs[REG_A8] = TempSP;

    // Swap super flag
    bInSuperMode^=TRUE;
  }

  // Set/Clear trace mode
  if (SR&SR_TRACEMODE) {
    // Have we set the TRACE bit for the FIRST time? Don't let exception occur until NEXT instruction
    // NOTE Sometimes the TRACE bit can be set many times, so only skip exception on FIRST one
    if ((PendingInterruptFlag&PENDING_INTERRUPT_FLAG_TRACE)==0) {
      PendingInterruptFlag |= PENDING_INTERRUPT_FLAG_TRACE;
      bDoTraceException = FALSE;
    }
  }
  else
    PendingInterruptFlag &= CLEAR_PENDING_INTERRUPT_FLAG_TRACE;
}

//-----------------------------------------------------------------------
/*
  Called when TRACE bit is set. This causes 'exception' after each instruction, BUT
  it does not execute after the FIRST 'move SR,xxxx' to set the bit
*/
void M68000_TraceModeTriggered(void)
{
/* FIXME */
/*
  __asm {
    cmp    [bDoTraceException],FALSE    // First time around? Skip exception
    je    dont_need_expection

    mov    [ExceptionVector],EXCEPTION_TRACE  // Handler
    SAVE_ASSEM_REGS            // Save assembly registers
    call  M68000_Exception        // Cause trap
    RESTORE_ASSEM_REGS          // Restore assembly registers

dont_need_expection:;
    mov    [bDoTraceException],TRUE    // Do TRACE exception next time around
    ret
  }
*/
}

//-----------------------------------------------------------------------
/*
  Exception handler
*/
void M68000_Exception(void)
{
  unsigned long Vector,MFPBaseVector;
  BOOL bRet=FALSE;

  /* Was the CPU stopped, i.e. by a STOP instruction? */
  regs.stopped = 0;
  unset_special (SPCFLAG_STOP);   /* All is go,go,go! */

  /* At the moment, this functions ist just a wrapper to Exception() of the UAE CPU - Thothy */
  Exception(ExceptionVector/4, m68k_getpc());

  return;

#if 0
  // Was the CPU stopped, ie by a STOP instruction?
/* FIXME: */
/*
  if (CPUStopped) {
    PC += 4;    // Skip after the STOP instruction as CPU is now to resume!
    CPUStopped = FALSE;  // All is go,go,go!
  }
*/
  /* Find exception vector, keep 32-bit address as top byte is used by TOS */
  /* exception vectors as an ID! */
  Vector = STMemory_ReadLong(ExceptionVector);

  /* Check for intercept - The game 'Operation Wolf' re-directs traps, */
  /* so double check is a TOS trap and not software! (ie Vector is >0xE00000) */
  if ((Vector&0xffffff)>=0xE00000) {
    if (ExceptionVector==EXCEPTION_TRAP13)
      bRet = Bios();
    else if (ExceptionVector==EXCEPTION_TRAP14)
      bRet = XBios();
    else if (ExceptionVector==EXCEPTION_TRAP1)
      bRet = GemDOS();
    else if (ExceptionVector==EXCEPTION_TRAP2) {
      bRet = VDI();
      if (!bRet) {
        // Set 'PC' as address of 'VDI_OPCODE' illegal instruction
        // This will call VDI_OpCode after completion of Trap call!
        // Use to modify return structure from VDI
        if (bUseVDIRes) {
          VDI_OldPC = PC;
          PC = CART_VDI_OPCODE_ADDR;
        }
      }
    }

  // Do handly bit of debugging to trap bus/address errors, before goes to TOS handler
#ifdef TRAP_BUSERROR_HISTORY
    if (ExceptionVector==EXCEPTION_BUSERROR) {
      MessageBox(NULL,"TRAP BUS ERROR",PROG_NAME,MB_OK | MB_ICONSTOP);
      Debug_File("TRAP BUS ERROR\n");
      M68000_OutputHistory();
      exit(0);
    }
#endif
#ifdef TRAP_ADDRESSERROR_HISTORY
    if (ExceptionVector==EXCEPTION_ADDRERROR) {
      MessageBox(NULL,"TRAP ADDRESS ERROR",PROG_NAME,MB_OK | MB_ICONSTOP);
      Debug_File("TRAP ADDRESS ERROR\n");
      M68000_OutputHistory();
      exit(0);
    }
#endif
#ifdef TRAP_ILLEGALINSTRUCTIONERROR_HISTORY
    if (ExceptionVector==EXCEPTION_ILLEGALINS) {
      MessageBox(NULL,"TRAP ILLEGAL INSTRUCTION ERROR",PROG_NAME,MB_OK | MB_ICONSTOP);
      Debug_File("TRAP ILLEGAL INSTRUCTION ERROR\n");
      M68000_OutputHistory();
      exit(0);
    }
#endif
  }

  // Did we re-direct call? No, so let's call it!
  if (!bRet) {
    // Save PC and SR to supervisor stack
/* FIXME */
/*
    __asm {
      push  ebp

      mov    edx,[Reg_SuperSP]

      sub    DWORD PTR [edx],SIZE_LONG
      mov    ebp,[edx]        // Stack pointer
      and    ebp,0xffffff        // as 24-bit address in PC memory(note _C)
      mov    ecx,[PC]        // Save PC
      SWAP_ENDIAN_LONG_ECX
      mov    DWORD PTR STRam[ebp],ecx

      sub    DWORD PTR [edx],SIZE_WORD
      mov    ebp,[edx]        // Stack pointer
      and    ebp,0xffffff        // as 24-bit address in PC memory(note _C)
      mov    cx,WORD PTR [SR]
      and    ecx,SR_MASK        // Remove condition codes
      mov    eax,[EmuCCode]
      shr    eax,4          // Emulation codes in correct bits
      and    eax,SR_CCODE_MASK
      or    eax,ecx          // Or in current condition codes
      SWAP_ENDIAN_WORD_AX
      mov    WORD PTR STRam[ebp],ax

      pop    ebp
    }
*/
    // Exception frame, total 14 bytes (6 already put on stack, ie PC and SR)
    //       15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
    // SSP->|                                |RW|IN|Func.cod|         Higher
    // address
    //      |High word access address                       |
    //      |Low word access address                        |
    //      |Instruction register                           |
    //      |Status register                                |
    //      |High word program counter                      |
    //      |Low word program counter                       |         Lower address
    // 
    // RW: Write=0, Read=1
    // IN: Instruction=0, Not=1
    if ( (ExceptionVector==EXCEPTION_BUSERROR) || (ExceptionVector==EXCEPTION_ADDRERROR) ) {
/* FIXME */
/*
      __asm {
        push  ebp

        mov    ebp,[InsPC]      // Get PC of instruction last ran
        mov    cx,WORD PTR [ebp]    // 'Opcode' in 68000 endian
        mov    edx,[Reg_SuperSP]
        sub    DWORD PTR [edx],SIZE_WORD
        mov    ebp,[edx]      // Stack pointer
        and    ebp,0xffffff      // as 24-bit address in PC memory(note _C)
        mov    WORD PTR STRam[ebp],cx    // Store 'Instruction Register'

        sub    DWORD PTR [edx],SIZE_LONG
        mov    ebp,[edx]      // Stack pointer
        and    ebp,0xffffff      // as 24-bit address in PC memory(note _C)
        mov    ecx,[BusAddressLocation]  // Address which caused fault
        SWAP_ENDIAN_LONG_ECX
        mov    DWORD PTR STRam[ebp],ecx

        sub    DWORD PTR [edx],SIZE_WORD
        mov    ebp,[edx]      // Stack pointer
        and    ebp,0xffffff      // as 24-bit address in PC memory(note _C)
        xor    ecx,ecx
        mov    WORD PTR STRam[ebp],cx    // '0' for now

        pop    ebp
      }
*/
    }

    SR_Before = SR;
    SR &= SR_CLEAR_TRACEMODE;            // Clear trace mode (bit 15)
    SR |= SR_SUPERMODE;                // Set super mode (bit 13)
    M68000_CheckUserSuperToggle();          // Check if swapped mode

    // Do call to vector
//    char szString[256];
//    sprintf(szString,"0x%X (0x%X)",Vector,ExceptionVector);
//    debug << szString << endl;
    PC = Vector;
    // Set Status Register so interrupt can ONLY be stopped by another interrupt of higher priority!
    if (ExceptionVector==EXCEPTION_VBLANK)
      SR = (SR&SR_CLEAR_IPL)|0x0400;        // VBL, level 4
    else if (ExceptionVector==EXCEPTION_HBLANK)
      SR = (SR&SR_CLEAR_IPL)|0x0200;        // HBL, level 2    
    else {
      MFPBaseVector = (unsigned int)(MFP_VR&0xf0)<<2;
      if ( (ExceptionVector>=MFPBaseVector) && (ExceptionVector<=(MFPBaseVector+0x34)) )
        SR = (SR&SR_CLEAR_IPL)|0x0600;      // MFP, level 6
    }
  }
#endif
}

//-----------------------------------------------------------------------
/*
  Handle Line-A OpCode exception(Top 4-bit in opcode are 0xA)
*/
/*
void M68000_Line_A_OpCode_Execute(void)
{
  PC -= SIZE_WORD;    // PC needs to be at address of Line-A instruction
  ExceptionVector = EXCEPTION_LINE_A;
  M68000_Exception();
}

void M68000_Line_A_OpCode(void)
{
//FIXME   SAVE_ASSEM_REGS();        // Save assembly registers
  M68000_Line_A_OpCode_Execute();
//  RESTORE_ASSEM_REGS();        // Restore assembly registers
//  RET;
}
*/

//-----------------------------------------------------------------------
/*
  During init do 0xA000, followed by 0xA0FF - we use this to get pointer to Line-A structure details(to fix for extended VDI res)
*/
void M68000_Line_A_Trap(void)
{
/* FIXME */
/*

  PUSH_ALL
  __asm {
    mov    edx,DWORD PTR Regs[REG_D0*4]
    mov    [LineABase],edx
    mov    edx,DWORD PTR Regs[REG_A1*4]
    mov    [FontBase],edx
    call  VDI_LineA                // Modify Line-A structure
  }
  POP_ALL
  RET
*/
}

//-----------------------------------------------------------------------
/*
  Handle Line-F OpCode exception(Top 4-bit in opcode are 0xF)
*/
/*
void M68000_Line_F_OpCode_Execute(void)
{
  PC -= SIZE_WORD;    // PC needs to be at address of Line-F instruction
  ExceptionVector = EXCEPTION_LINE_F;
  M68000_Exception();
}

void M68000_Line_F_OpCode(void)
{
//FIXME   SAVE_ASSEM_REGS();    // Save assembly registers
  M68000_Line_F_OpCode_Execute();
//  RESTORE_ASSEM_REGS();      // Restore assembly registers
//  RET;
}
*/

//-----------------------------------------------------------------------
/*
  Use 'InsPC' to find how many cycles last instruction took to execute
*/
int M68000_FindLastInstructionCycles(void)
{
  unsigned short int OpCode;

//FIXME   OpCode = *(unsigned short int *)InsPC;      // Read 'opcode'
//FIXME   return( DecodeTable[(OpCode*SIZEOF_DECODE)+(DECODE_CYCLES/sizeof(long))] );
  return 4; /* Ouargs ... this is an uggly hack */
}

//-----------------------------------------------------------------------
/*
  Output CPU instruction history(last 'x' instructions) for debugging
*/
void M68000_OutputHistory(void)
{
#ifndef FINAL_VERSION
  unsigned long StartPC;
  int i;

  /* First, Return back into a Window */
  Screen_ReturnFromFullScreen();
  View_ToggleWindowsMouse(MOUSE_WINDOWS);

  Debug_File("HISTORY\n");

  for(i=0; i<(INSTRUCTION_HISTORY_SIZE-1); i++) {
    StartPC = DisPC = InstructionHistory[(InstructionHistoryIndex+i+1)&INSTRUCTION_HISTORY_MASK];
    Disass_DiassembleLine();            // Disassemble instruction

    Debug_File("%8.8X\t%s\n",StartPC,szOpString);
  }
#endif
}
