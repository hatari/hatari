/*
  Hatari - cycles.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Here we take care of cycle counters. For performance reasons we don't increase
  all counters after each 68k instruction, but only one main counter.
  When we need to read one of the normal counters (currently only for video
  and sound cycles), we simply update these counters with the main counter
  before returning the current counter value.
*/


/* 2007/03/xx	[NP]	Use 'CurrentInstrCycles' to get a good approximation for	*/
/*			Cycles_GetCounterOnReadAccess and Cycles_GetCounterOnWriteAccess*/
/*			(this should work correctly with 'move' instruction).		*/
/* 2008/04/14	[NP]	Take WaitStateCycles into account when computing the value of	*/
/*			Cycles_GetCounterOnReadAccess and Cycles_GetCounterOnWriteAccess*/
/* 2008/12/21	[NP]	Use BusMode to adjust Cycles_GetCounterOnReadAccess and		*/
/*			Cycles_GetCounterOnWriteAccess depending on who is owning the	*/
/*			bus (cpu, blitter).						*/
/* 2011/03/26	[NP]	In Cycles_GetCounterOnReadAccess, add a special case for opcode	*/
/*			$11f8 'move.b xxx.w,xxx.w' (fix MOVE.B $ffff8209.w,$26.w in	*/
/*			'Bird Mad Girl Show' demo's loader/protection)			*/
/* 2012/08/19	[NP]	Add a global counter CyclesGlobalClockCounter to count cycles	*/
/*			since the last reset.						*/
/* 2015/10/04	[NP]	In Cycles_GetInternalCycleOnReadAccess / WriteAccess, use the	*/
/*			sub-cycles provided by WinUAE cpu core when using cycle exact	*/
/*			mode (instead of using heuristics for the most common opcodes).	*/


const char Cycles_fileid[] = "Hatari cycles.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "cycles.h"
#include "ioMem.h"
#include "hatari-glue.h"


int	nCyclesMainCounter;			/* Main cycles counter since previous Cycles_UpdateCounters() */

static int nCyclesCounter[CYCLES_COUNTER_MAX];	/* Array with all counters */

Uint64	CyclesGlobalClockCounter = 0;		/* Global clock counter since starting Hatari (it's never reset afterwards) */

int	CurrentInstrCycles;


static void	Cycles_UpdateCounters(void);



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void Cycles_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&nCyclesMainCounter, sizeof(nCyclesMainCounter));
	MemorySnapShot_Store(nCyclesCounter, sizeof(nCyclesCounter));
	MemorySnapShot_Store(&CyclesGlobalClockCounter, sizeof(CyclesGlobalClockCounter));
	MemorySnapShot_Store(&CurrentInstrCycles, sizeof(CurrentInstrCycles));
}


/*-----------------------------------------------------------------------*/
/**
 * Update all cycles counters with the current value of nCyclesMainCounter.
 */
static void Cycles_UpdateCounters(void)
{
	int i;

	for (i = 0; i < CYCLES_COUNTER_MAX; i++)
	{
		nCyclesCounter[i] += nCyclesMainCounter;
	}

	nCyclesMainCounter = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Set a counter to a new value.
 */
void Cycles_SetCounter(int nId, int nValue)
{
	/* Update counters first (nCyclesMainCounter must be 0 afterwards) */
	Cycles_UpdateCounters();

	/* Now set the new value: */
	nCyclesCounter[nId] = nValue;
}


/*-----------------------------------------------------------------------*/
/**
 * Read a counter.
 */
int Cycles_GetCounter(int nId)
{
	/* Update counters first so we read an up-to-date value */
	Cycles_UpdateCounters();

	return nCyclesCounter[nId];
}


/*-----------------------------------------------------------------------*/
/**
 * Compute the cycles where a read actually happens inside a specific
 * instruction type. We use some common cases, this should be handled more
 * accurately in the cpu emulation for each opcode.
 */
int Cycles_GetInternalCycleOnReadAccess(void)
{
	int AddCycles;
	int Opcode;

	if ( BusMode == BUS_MODE_BLITTER )
	{
		AddCycles = 4 + WaitStateCycles;
	}
#ifdef WINUAE_FOR_HATARI
	/* When using WinUAE CPU in CE mode, 'currcycle' will be the number of cycles */
	/* inside the current opcode just before accessing memory. */
	/* As memory accesses take 4 cycles, we just need to add 4 cycles to get */
	/* the number of cycles when the read will be completed. */
	/* (see mem_access_delay_XXX_read() in cpu_prefetch.h and wait_cpu_cycle_read() in custom.c) */
	else if ( currprefs.cpu_cycle_exact )
	{
		AddCycles = currcycle*2/CYCLE_UNIT + 4;
	}
#endif
	else							/* BUS_MODE_CPU */
	{
		/* TODO: Find proper cycles count depending on the opcode/family of the current instruction */
		/* (e.g. movem is not correctly handled) */
		Opcode = M68000_CurrentOpcode;

		/* Assume we use 'move src,dst' : access cycle depends on dst mode */
		if ( Opcode == 0x11f8 )							/* move.b xxx.w,xxx.w (eg MOVE.B $ffff8209.w,$26.w in Bird Mad Girl Show) */
			AddCycles = 8 + WaitStateCycles;				/* read is effective after 8 cycles */

		else if ( OpcodeFamily == i_MVPRM )					/* movep.l d0,$ffc3(a1) in E605 (STE) or movep.l d1,$fffb(a2) in RGBeast (STE) */
			AddCycles = 4 + IoAccessInstrCount * 4 + WaitStateCycles;	/* [NP] FIXME, it works with RGBeast, but not with E605 */
											/* something must be wrong in video.c */

		else									/* assume the behaviour of a 'move' to Dn */
			AddCycles = CurrentInstrCycles - 4 + WaitStateCycles;		/* read is effective 4 cycles before the end of the instr */
	}

	return AddCycles;
}



/*-----------------------------------------------------------------------*/
/**
 * Compute the cycles where a write actually happens inside a specific
 * instruction type. We use some common cases, this should be handled more
 * accurately in the cpu emulation for each opcode.
 */
int Cycles_GetInternalCycleOnWriteAccess(void)
{
	int AddCycles;

	if ( BusMode == BUS_MODE_BLITTER )
	{
		AddCycles = 4 + WaitStateCycles;
	}
#ifdef WINUAE_FOR_HATARI
	/* When using WinUAE CPU in CE mode, 'currcycle' will be the number of cycles */
	/* inside the current opcode just before accessing memory. */
	/* As memory accesses take 4 cycles, we just need to add 4 cycles to get */
	/* the number of cycles when the write will be completed. */
	/* (see mem_access_delay_XXX_write() in cpu_prefetch.h and wait_cpu_cycle_write() in custom.c) */
	else if ( currprefs.cpu_cycle_exact )
	{
		AddCycles = currcycle*2/CYCLE_UNIT + 4;
	}
#endif
	else							/* BUS_MODE_CPU */
	{
		/* TODO: Find proper cycles count depending on the type of the current instruction */
		/* (e.g. movem is not correctly handled) */
		AddCycles = CurrentInstrCycles + WaitStateCycles;

		if ( ( OpcodeFamily == i_CLR ) || ( OpcodeFamily == i_NEG ) || ( OpcodeFamily == i_NEGX ) || ( OpcodeFamily == i_NOT ) )
			;						/* Do nothing, the write is done during the last 4 cycles */
									/* (e.g i_CLR for bottom border removal in No Scroll / Delirious Demo 4) */

		else if ( ( OpcodeFamily == i_ADD ) || ( OpcodeFamily == i_SUB ) )
			;						/* Do nothing, the write is done during the last 4 cycles */
									/* (eg 'add d1,(a0)' in rasters.prg by TOS Crew */

		else if ( ( OpcodeFamily == i_AND ) || ( OpcodeFamily == i_OR ) || ( OpcodeFamily == i_EOR ) )
			;						/* Do nothing, the write is done during the last 4 cycles */

		else if ( ( OpcodeFamily == i_BCHG ) || ( OpcodeFamily == i_BCLR ) || ( OpcodeFamily == i_BSET ) )
			;						/* Do nothing, the write is done during the last 4 cycles */

		else if ( OpcodeFamily == i_MVPRM )			/* movep.l d0,$ffc3(a1) in E605 (STE) or movep.l d1,$fffb(a2) in RGBeast (STE) */
			AddCycles = 4 + IoAccessInstrCount * 4 + WaitStateCycles;	/* [NP] FIXME, it works with RGBeast, but not with E605 */

		else if ( OpcodeFamily == i_MVMLE )
		{
			/* In the case of movem, CurrentInstrCycles is dynamic (depends on the number */
			/* of registers to transfer). The 4*n for .W or 8*n for .L is not counted in CurrentInstrCycles */
			/* The last 4 cycles of a movem are for prefetch, so number of cycles is : */
			/* x + 4*n + 4 (movem.w) or x + 8*n + 4 (movem.l)  with x + 4 = CurrentInstrCycles */

			if (nIoMemAccessSize == SIZE_LONG)		/* long access from a movem.l */
			{
				//AddCycles += -4 + IoAccessInstrCount * 8 - 4;
				AddCycles -= 0;				/* NOTE [NP] : this is used by old uae cpu core but does not happen */
									/* on real HW because IO regs can't be accessesed with a long */
									/* FIXME : fix old uae cpu to remove long accesses to memory for 68000 ? */
									/* We keep it this way fo now ... */
			}
			else						/* word access with movem.w or movem.l doing 2 words accesses per long */
			{
				AddCycles += -4 + IoAccessInstrCount * 4;
			}
		}

		else
		{
			/* Default case : write first, then prefetch (mostly for 'move' since this is the most */
			/* common instr used when requiring cycle precise writes) */
			if (nIoMemAccessSize == SIZE_LONG)	/* long access */
				AddCycles -= 8;
			else					/* word/byte access */
			{
				if ( IoAccessInstrCount == 0 )	/* instruction does only 1 access */
					AddCycles -= 4;
				else				/* instruction does multiple accesses (eg: move.l gives 2 word accesses) */
					AddCycles += -12 + IoAccessInstrCount * 4;	/* gives -8 or -4 */
			}
		}
	}

	return AddCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Read a counter on CPU memory read access by taking care of the instruction
 * type (add the needed amount of additional cycles).
 */
int Cycles_GetCounterOnReadAccess(int nId)
{
	int AddCycles;

	AddCycles = Cycles_GetInternalCycleOnReadAccess();

	return Cycles_GetCounter(nId) + AddCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Read a counter on CPU memory write access by taking care of the instruction
 * type (add the needed amount of additional cycles).
 */
int Cycles_GetCounterOnWriteAccess(int nId)
{
	int AddCycles;

	AddCycles = Cycles_GetInternalCycleOnWriteAccess();

	return Cycles_GetCounter(nId) + AddCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Read the main clock counter on CPU memory read access by taking care of the instruction
 * type (add the needed amount of additional cycles).
 */
Uint64 Cycles_GetClockCounterOnReadAccess(void)
{
	int AddCycles;

	AddCycles = Cycles_GetInternalCycleOnReadAccess();

	return CyclesGlobalClockCounter + AddCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Read the main clock counter on CPU memory write access by taking care of the instruction
 * type (add the needed amount of additional cycles).
 */
Uint64 Cycles_GetClockCounterOnWriteAccess(void)
{
	int AddCycles;

	AddCycles = Cycles_GetInternalCycleOnWriteAccess();

	return CyclesGlobalClockCounter + AddCycles;
}



