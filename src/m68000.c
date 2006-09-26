/*
  Hatari - m68000.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  These routines originally (in WinSTon) handled exceptions as well as some
  few OpCode's such as Line-F and Line-A. In Hatari it has mainly become a
  wrapper between the WinSTon sources and the UAE CPU code.
*/
const char M68000_rcsid[] = "Hatari $Id: m68000.c,v 1.37 2006-09-26 19:12:35 eerot Exp $";

#include "main.h"
#include "bios.h"
#include "gemdos.h"
#include "hatari-glue.h"
#include "int.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"
#include "xbios.h"


Uint32 BusErrorAddress;          /* Stores the offending address for bus-/address errors */
Uint32 BusErrorPC;               /* Value of the PC when bus error occurs */
BOOL bBusErrorReadWrite;         /* 0 for write error, 1 for read error */
int nCpuFreqShift;               /* Used to emulate higher CPU frequencies: 0=8MHz, 1=16MHz, 2=32Mhz */
int nWaitStateCycles;            /* Used to emulate the wait state cycles of certion IO registers */


/*-----------------------------------------------------------------------*/
/*
  Reset CPU 68000 variables
*/
void M68000_Reset(BOOL bCold)
{
  int i;

  /* Clear registers */
  if (bCold)
  {
    for(i=0; i<(16+1); i++)
      Regs[i] = 0;
  }

  /* Now directly reset the UAE CPU core: */
  m68k_reset();
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of CPU variables ('MemorySnapShot_Store' handles type)
*/
void M68000_MemorySnapShot_Capture(BOOL bSave)
{
  Uint32 savepc;

  /* Save/Restore details */
  MemorySnapShot_Store(Regs,sizeof(Regs));
  MemorySnapShot_Store(&STRamEnd,sizeof(STRamEnd));

  /* For the UAE CPU core: */
  MemorySnapShot_Store(&cpu_level, sizeof(cpu_level));          /* MODEL */
  MemorySnapShot_Store(&address_space_24, sizeof(address_space_24));
  MemorySnapShot_Store(&regs.regs[0], sizeof(regs.regs));       /* D0-D7 A0-A6 */
  if (bSave)
  {
    savepc = m68k_getpc();
    MemorySnapShot_Store(&savepc, sizeof(savepc));              /* PC */
  }
  else
  {
    MemorySnapShot_Store(&savepc, sizeof(savepc));              /* PC */
    regs.pc = savepc;
    regs.prefetch_pc = regs.pc + 128;
  }
  MemorySnapShot_Store(&regs.prefetch, sizeof(regs.prefetch));  /* prefetch */
  if (bSave)
  {
    MakeSR ();
    if (regs.s)
    {
      MemorySnapShot_Store(&regs.usp, sizeof(regs.usp));        /* USP */
      MemorySnapShot_Store(&regs.regs[15], sizeof(regs.regs[15]));  /* ISP */
    }
    else
    {
      MemorySnapShot_Store(&regs.regs[15], sizeof(regs.regs[15]));  /* USP */
      MemorySnapShot_Store(&regs.isp, sizeof(regs.isp));        /* ISP */
    }
    MemorySnapShot_Store(&regs.sr, sizeof(regs.sr));            /* SR/CCR */
  }
  else
  {
    MemorySnapShot_Store(&regs.usp, sizeof(regs.usp));
    MemorySnapShot_Store(&regs.isp, sizeof(regs.isp));
    MemorySnapShot_Store(&regs.sr, sizeof(regs.sr));
  }
  MemorySnapShot_Store(&regs.stopped, sizeof(regs.stopped));
  MemorySnapShot_Store(&regs.dfc, sizeof(regs.dfc));            /* DFC */
  MemorySnapShot_Store(&regs.sfc, sizeof(regs.sfc));            /* SFC */
  MemorySnapShot_Store(&regs.vbr, sizeof(regs.vbr));            /* VBR */
  MemorySnapShot_Store(&reg_caar, sizeof(reg_caar));            /* CAAR */
  MemorySnapShot_Store(&reg_cacr, sizeof(reg_cacr));            /* CACR */
  MemorySnapShot_Store(&regs.msp, sizeof(regs.msp));            /* MSP */

  if (!bSave)
  {
    m68k_setpc (regs.pc);
    /* MakeFromSR() must not swap stack pointer */
    regs.s = (regs.sr >> 13) & 1;
    MakeFromSR();
    /* set stack pointer */
    if (regs.s)
      m68k_areg(regs, 7) = regs.isp;
    else
      m68k_areg(regs, 7) = regs.usp;
  }

}


/*-----------------------------------------------------------------------*/
/*
  BUSERROR - Access outside valid memory range.
  Use bReadWrite = 0 for write errors and bReadWrite = 1 for read errors!
*/
void M68000_BusError(Uint32 addr, BOOL bReadWrite)
{
  /* FIXME: In prefetch mode, m68k_getpc() seems already to point to the next instruction */
  BusErrorPC = m68k_getpc();

  if(BusErrorPC < TosAddress || BusErrorPC > TosAddress + TosSize)
  {
    /* Print bus errors (except for TOS' hardware tests) */
    fprintf(stderr, "M68000_BusError at address $%lx\n", (long)addr);
  }

  BusErrorAddress = addr;         /* Store for exception frame */
  bBusErrorReadWrite = bReadWrite;
  set_special(SPCFLAG_BUSERROR);    /* The exception will be done in newcpu.c */
}


/*-----------------------------------------------------------------------*/
/*
  Exception handler
*/
void M68000_Exception(Uint32 ExceptionVector)
{
  int exceptionNr = ExceptionVector/4;

  if(exceptionNr>24 && exceptionNr<32)  /* 68k autovector interrupt? */
  {
    /* Handle autovector interrupts the UAE's way
     * (see intlev() and do_specialties() in UAE CPU core) */
    int intnr = exceptionNr - 24;
    pendingInterrupts |= (1 << intnr);
    set_special(SPCFLAG_INT);
  }
  else
  {
    /* Was the CPU stopped, i.e. by a STOP instruction? */
    if(regs.spcflags & SPCFLAG_STOP)
    {
      regs.stopped = 0;
      unset_special(SPCFLAG_STOP);      /* All is go,go,go! */
    }

    /* 68k exceptions are handled by Exception() of the UAE CPU core */
    Exception(exceptionNr, m68k_getpc());

    MakeSR();
    /* Set Status Register so interrupt can ONLY be stopped by another interrupt
     * of higher priority! */
#if 0  /* VBL and HBL are handled in the UAE CPU core (see above). */
    if (ExceptionVector==EXCEPTION_VBLANK)
      SR = (SR&SR_CLEAR_IPL)|0x0400;  /* VBL, level 4 */
    else if (ExceptionVector==EXCEPTION_HBLANK)
      SR = (SR&SR_CLEAR_IPL)|0x0200;  /* HBL, level 2 */
    else
#endif
    {
      Uint32 MFPBaseVector = (unsigned int)(MFP_VR&0xf0)<<2;
      if ( (ExceptionVector>=MFPBaseVector) && (ExceptionVector<=(MFPBaseVector+0x34)) )
        SR = (SR&SR_CLEAR_IPL)|0x0600; /* MFP, level 6 */
    }
    MakeFromSR();
  }
}


/*-----------------------------------------------------------------------*/
/*
  There seem to be wait states when a program accesses certain hardware
  registers on the ST. Use this function to simulate these wait states.
*/
void M68000_WaitState(int nCycles)
{
  set_special(SPCFLAG_EXTRA_CYCLES);

  nWaitStateCycles = nCycles;
}
