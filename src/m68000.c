/*
  Hatari

  M68000 - CPU. This originally (in WinSTon) handled exceptions as well as some
  few OpCode's such as Line-F and Line-A. In Hatari it has mainly become a
  wrapper between the WinSTon sources and the UAE CPU code.
*/

#include "main.h"
#include "bios.h"
#include "cart.h"
#include "debug.h"
#include "decode.h"
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
#include "xbios.h"


unsigned long ExceptionVector;
short int PendingInterruptFlag;
void *PendingInterruptFunction;
short int PendingInterruptCount;
unsigned long BusAddressLocation;



/*-----------------------------------------------------------------------*/
/*
  Reset CPU 68000 variables
*/
void M68000_Reset(BOOL bCold)
{
  int i;

  /* Clear registers, set PC, SR and stack pointers */
  if (bCold)
  {
    for(i=0; i<(16+1); i++)
      Regs[i] = 0;
  }
  PC = TOSAddress;                            /* Start of TOS image, 0xfc0000 or 0xe00000 */
  SR = 0x2700;                                /* Starting status register */
  MakeFromSR();
  PendingInterruptFlag = 0;                   /* Clear pending flag */

  /* Now directly reset the UAE CPU core: */
  m68k_reset();
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void M68000_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  /*MemorySnapShot_Store(&bDoTraceException,sizeof(bDoTraceException));*/
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
  This is for 'decode.asm' variables - as cannot use 'decode.c' as will overwrite assembler .obj file!
*/
void M68000_Decode_MemorySnapShot_Capture(BOOL bSave)
{
  int ID;

  /* Save/Restore details */
  MemorySnapShot_Store(Regs,sizeof(Regs));
  MemorySnapShot_Store(&STRamEnd,sizeof(STRamEnd));
  MemorySnapShot_Store(&STRamEnd_BusErr,sizeof(STRamEnd_BusErr));
  MemorySnapShot_Store(&PendingInterruptCount,sizeof(PendingInterruptCount));
  MemorySnapShot_Store(&PendingInterruptFlag,sizeof(PendingInterruptFlag));
  if (bSave)
  {
    /* Convert function to ID */
    ID = Int_HandlerFunctionToID(PendingInterruptFunction);
    MemorySnapShot_Store(&ID,sizeof(int));
  }
  else
  {
    /* Convert ID to function */
    MemorySnapShot_Store(&ID,sizeof(int));
    PendingInterruptFunction = Int_IDToHandlerFunction(ID);
  }
  MemorySnapShot_Store(&PC,sizeof(PC));
  MemorySnapShot_Store(&SR,sizeof(SR));
  /*MemorySnapShot_Store(&bInSuperMode,sizeof(bInSuperMode));*/
  /*MemorySnapShot_Store(&Reg_SuperSP,sizeof(Reg_SuperSP));*//*FIXME*/
  /*MemorySnapShot_Store(&Reg_UserSP,sizeof(Reg_UserSP));*/
  /*MemorySnapShot_Store(&EmuCCode,sizeof(EmuCCode));*/
  MemorySnapShot_Store(&ExceptionVector,sizeof(ExceptionVector));  
}


/*-----------------------------------------------------------------------*/
/*
  BUSERROR - Access outside valid memory range
*/
void M68000_BusError(unsigned long addr)
{
  /* Reset PC's stack to normal(as may have done many calls) so return to
     correct level after exception.
     Enter here with 'ebp' as address we tried to access */
  fprintf(stderr, "M68000_BusError at address $%lx\n", (long)addr);
  BusAddressLocation=addr;               /* Store for exception frame */
  ExceptionVector = EXCEPTION_BUSERROR;  /* Handler */
  M68000_Exception();                    /* Cause trap */
}


/*-----------------------------------------------------------------------*/
/*
  ADDRESSERROR - Access incorrect memory boundary, eg byte offset for a word access
*/
void M68000_AddressError(unsigned long addr)
{
  fprintf(stderr, "M68000_AddressError at address $%lx\n", (long)addr);
  BusAddressLocation=addr;              /* Store for exception frame */
  ExceptionVector=EXCEPTION_ADDRERROR;  /* Handler */
  M68000_Exception();                   /* Cause trap */
}


/*-----------------------------------------------------------------------*/
/*
  Exception handler
*/
void M68000_Exception(void)
{
  /* Was the CPU stopped, i.e. by a STOP instruction? */
  regs.stopped = 0;
  unset_special (SPCFLAG_STOP);   /* All is go,go,go! */

  /* At the moment, this functions ist just a wrapper to Exception() of the UAE CPU - Thothy */
  Exception(ExceptionVector/4, m68k_getpc());
}

