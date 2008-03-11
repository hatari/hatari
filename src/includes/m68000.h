/*
  Hatari - m68000.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

/* 2007/11/10	[NP]	Add pairing for lsr / dbcc (and all variants	*/
/*			working on register, not on memory).		*/
/* 2008/01/07	[NP]	Use PairingArray to store all valid pairing	*/
/*			combinations (in m68000.c)			*/

#ifndef HATARI_M68000_H
#define HATARI_M68000_H

#include "cycles.h"     /* for nCyclesMainCounter */
#include "sysdeps.h"
#include "memory.h"
#include "newcpu.h"     /* for regs */
#include "int.h"
#include "trace.h"


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


/* interrupt type used in M68000_Exception() */
#define	M68000_INT_MFP		1		/* exception is an MFP interrupt */
#define	M68000_INT_VIDEO	2		/* exception is a video interrupt */

extern Uint32 BusErrorAddress;
extern Uint32 BusErrorPC;
extern BOOL bBusErrorReadWrite;
extern int nCpuFreqShift;
extern int nWaitStateCycles;

extern int	LastOpcodeFamily;
extern int	LastInstrCycles;
extern int	Pairing;
extern char	PairingArray[ MAX_OPCODE_FAMILY ][ MAX_OPCODE_FAMILY ];
extern const char *OpcodeName[];


/*-----------------------------------------------------------------------*/
/**
 * Add CPU cycles.
 * NOTE: All times are rounded up to nearest 4 cycles.
 */
static inline void M68000_AddCycles(int cycles)
{
	cycles = (cycles + 3) & ~3;
	cycles = cycles >> nCpuFreqShift;

	PendingInterruptCount -= INT_CONVERT_TO_INTERNAL(cycles, INT_CPU_CYCLE);
	nCyclesMainCounter += cycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Add CPU cycles, take cycles pairing into account.
 * NOTE: All times are rounded up to nearest 4 cycles.
 */
static inline void M68000_AddCyclesWithPairing(int cycles)
{
	Pairing = 0;
	/* Check if number of cycles for current instr and for */
	/* the previous one is of the form 4+2n */
	/* If so, a pairing could be possible depending on the opcode */
	if ( ( PairingArray[ LastOpcodeFamily ][ OpcodeFamily ] == 1 )
	    && ( ( cycles & 3 ) == 2 ) && ( ( LastInstrCycles & 3 ) == 2 ) )
	{
		Pairing = 1;
		HATARI_TRACE( HATARI_TRACE_CPU_PAIRING ,
		              "pairing detected pc=%x family %s/%s cycles %d/%d\n" ,
		              m68k_getpc(), OpcodeName[LastOpcodeFamily] ,
		              OpcodeName[OpcodeFamily], LastInstrCycles, cycles );
	}

	/* Store current instr (not rounded) to check next time */
	LastInstrCycles = cycles;
	LastOpcodeFamily = OpcodeFamily;

	/* If pairing is true, we need to substract 2 cycles for the	*/
	/* previous instr which was rounded to 4 cycles while it wasn't */
	/* needed (and we don't round the current one)			*/
	/* -> both instr will take 4 cycles less on the ST than if ran	*/
	/* separately.							*/
	if (Pairing == 1)
		cycles -= 2;
	else
		cycles = (cycles + 3) & ~3;	 /* no pairing, round current instr to 4 cycles */

	cycles = cycles >> nCpuFreqShift;

	PendingInterruptCount -= INT_CONVERT_TO_INTERNAL ( cycles , INT_CPU_CYCLE );

	nCyclesMainCounter += cycles;
}


extern void M68000_InitPairing(void);
extern void M68000_Reset(BOOL bCold);
extern void M68000_Start(void);
extern void M68000_CheckCpuLevel(void);
extern void M68000_MemorySnapShot_Capture(BOOL bSave);
extern void M68000_BusError(Uint32 addr, BOOL bReadWrite);
extern void M68000_Exception(Uint32 ExceptionVector , int InterruptType);
extern void M68000_WaitState(int nCycles);

#endif
