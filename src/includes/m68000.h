/*
  Hatari - m68000.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_M68000_H
#define HATARI_M68000_H

#include "cycles.h"     /* for nCyclesMainCounter */
#include "sysdeps.h"
#include "memory.h"
#include "newcpu.h"     /* for regs */
#include "int.h"


/* Ugly hacks to adapt the main code to the different CPU cores: */

#define Regs regs.regs

#if defined(UAE_NEWCPU_H)

# define M68000_GetPC()     m68k_getpc()
# define M68000_SetPC(val)  m68k_setpc(val)

static inline Uint16 M68000_GetSR(void)
{
	MakeSR();
	return regs.sr;
}
static inline void M68000_SetSR(Uint16 v)
{
	regs.sr = v;
	MakeFromSR();
}

# define M68000_SetSpecial(flags)   set_special(flags)
# define M68000_UnsetSpecial(flags) unset_special(flags)

#else  /* following code is for WinUAE CPU: */

# define M68000_GetPC()     m68k_getpc(&regs)
# define M68000_SetPC(val)  m68k_setpc(&regs,val)

static inline Uint16 M68000_GetSR(void)
{
	MakeSR(&regs);
	return regs.sr;
}
static inline void M68000_SetSR(Uint16 v)
{
	regs.sr = v;
	MakeFromSR(&regs);
}

# define M68000_SetSpecial(flags)   set_special(&regs,flags)
# define M68000_UnsetSpecial(flags) unset_special(&regs,flags)

#endif /* defined(UAE_NEWCPU_H) */


#define FIND_IPL    ((regs.intmask)&0x7)


/* bus error mode */
#define BUS_ERROR_WRITE 0
#define BUS_ERROR_READ 1

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
	nCyclesMainCounter += cycles;
}

extern void M68000_Reset(BOOL bCold);
extern void M68000_CheckCpuLevel(void);
extern void M68000_MemorySnapShot_Capture(BOOL bSave);
extern void M68000_BusError(Uint32 addr, BOOL bReadWrite);
extern void M68000_Exception(Uint32 ExceptionVector);
extern void M68000_WaitState(int nCycles);

#endif
