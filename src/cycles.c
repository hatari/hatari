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
/* 2008/04/14	[NP]	Take nWaitStateCycles into account when computing the value of	*/
/*			Cycles_GetCounterOnReadAccess and Cycles_GetCounterOnWriteAccess*/
/* 2008/12/21	[NP]	Use BusMode to adjust Cycles_GetCounterOnReadAccess and		*/
/*			Cycles_GetCounterOnWriteAccess depending on who is owning the	*/
/*			bus (cpu, blitter).						*/
/* 2011/03/26	[NP]	In Cycles_GetCounterOnReadAccess, add a special case for opcode	*/
/*			$11f8 'move.b xxx.w,xxx.w' (fix MOVE.B $ffff8209.w,$26.w in	*/
/*			'Bird Mad Girl Show' demo's loader/protection)			*/
/* 2012/08/19	[NP]	Add a global counter CyclesGlobalClockCounter to count cycles	*/
/*			since the last reset.						*/


const char Cycles_fileid[] = "Hatari cycles.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "cycles.h"


int	nCyclesMainCounter;			/* Main cycles counter since previous Cycles_UpdateCounters() */

static int nCyclesCounter[CYCLES_COUNTER_MAX];	/* Array with all counters */

Uint64	CyclesGlobalClockCounter = 0;		/* Global clock counter since starting Hatari (it's never reset afterwards) */

int	CurrentInstrCycles;
int	MovepByteNbr = 0;			/* Number of the byte currently transferred in a movep (1..2 or 1..4) */
						/* 0 means current instruction is not a movep */


static void	Cycles_UpdateCounters(void);
static int	Cycles_GetInternalCycleOnReadAccess(void);
static int	Cycles_GetInternalCycleOnWriteAccess(void);



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
static int Cycles_GetInternalCycleOnReadAccess(void)
{
	int AddCycles;
	int Opcode;

	if ( BusMode == BUS_MODE_BLITTER )
	{
		AddCycles = 4 + nWaitStateCycles;
	}
	else							/* BUS_MODE_CPU */
	{
		/* TODO: Find proper cycles count depending on the opcode/family of the current instruction */
		/* (e.g. movem is not correctly handled) */
		Opcode = M68000_CurrentOpcode;
		//fprintf ( stderr , "opcode=%x\n" , Opcode );

		/* Assume we use 'move src,dst' : access cycle depends on dst mode */
		if ( Opcode == 0x11f8 )				/* move.b xxx.w,xxx.w (eg MOVE.B $ffff8209.w,$26.w in Bird Mad Girl Show) */
			AddCycles = CurrentInstrCycles + nWaitStateCycles - 8;		/* read is effective before the 8 write cycles for dst */
		else if ( OpcodeFamily == i_MVPRM )					/* eg movep.l d0,$ffc3(a1) in E605 (STE) */
			AddCycles = 12 + MovepByteNbr * 4;				/* [NP] FIXME, it works with E605 but gives 20-32 cycles instead of 16-28 */
											/* something must be wrong in video.c */
			/* FIXME : this should be : AddCycles = 4 + MovepByteNbr * 4, but this breaks e605 in video.c */
		else
			AddCycles = CurrentInstrCycles + nWaitStateCycles;		/* assume dest is reg : read is effective at the end of the instr */
	}

	return AddCycles;
}



/*-----------------------------------------------------------------------*/
/**
 * Compute the cycles where a write actually happens inside a specific
 * instruction type. We use some common cases, this should be handled more
 * accurately in the cpu emulation for each opcode.
 */
static int Cycles_GetInternalCycleOnWriteAccess(void)
{
	int AddCycles;

	if ( BusMode == BUS_MODE_BLITTER )
	{
		AddCycles = 4 + nWaitStateCycles;
	}
	else							/* BUS_MODE_CPU */
	{
		/* TODO: Find proper cycles count depending on the type of the current instruction */
		/* (e.g. movem is not correctly handled) */
		AddCycles = CurrentInstrCycles + nWaitStateCycles;

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

		else
		{
			/* assume the behaviour of a 'move' (since this is the most */
			/* common instr used when requiring cycle precise writes) */
			if ( AddCycles >= 8 )
				AddCycles -= 4;			/* last 4 cycles are for prefetch */
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



