/*
  Hatari - m68000.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_M68000_H
#define HATARI_M68000_H

#include "sound.h"	/* for SoundCycles */
#include "sysdeps.h"
#include "memory.h"
#include "newcpu.h"	/* for regs */

#define Regs regs.regs  /* Ugly hack to glue the WinSTon sources to the UAE CPU core. */
#define SR regs.sr      /* Don't forget to call MakeFromSR() and MakeSR() */


extern void *PendingInterruptFunction;
extern short int PendingInterruptCount;

extern Uint32 BusErrorAddress;
extern Uint32 BusErrorPC;
extern BOOL bBusErrorReadWrite;
extern int nCpuFreqShift;
extern int nWaitStateCycles;


/*-----------------------------------------------------------------------*/
/*
  Add CPU cycles.
  NOTE: All times are rounded up to nearest 4 cycles.
*/
static inline void M68000_AddCycles(int cycles)
{
	cycles = ((cycles + 3) & ~3) >> nCpuFreqShift;
	PendingInterruptCount -= cycles;
	SoundCycles += cycles;
}

extern void M68000_Reset(BOOL bCold);
extern void M68000_MemorySnapShot_Capture(BOOL bSave);
extern void M68000_BusError(Uint32 addr, BOOL bReadWrite);
extern void M68000_Exception(Uint32 ExceptionVector);
extern void M68000_WaitState(int nCycles);

#endif
