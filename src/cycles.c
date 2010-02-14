/*
  Hatari - cycles.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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


const char Cycles_fileid[] = "Hatari cycles.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "cycles.h"


int nCyclesMainCounter;				/* Main cycles counter */

static int nCyclesCounter[CYCLES_COUNTER_MAX];	/* Array with all counters */

int CurrentInstrCycles;



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void Cycles_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&nCyclesMainCounter, sizeof(nCyclesMainCounter));
	MemorySnapShot_Store(nCyclesCounter, sizeof(nCyclesCounter));
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
 * Read a counter on CPU memory read access by taking care of the instruction
 * type (add the needed amount of additional cycles).
 */
int Cycles_GetCounterOnReadAccess(int nId)
{
	int nAddCycles;

	/* Update counters first so we read an up-to-date value */
	Cycles_UpdateCounters();

	if ( BusMode == BUS_MODE_BLITTER )
	{
		nAddCycles = 4 + nWaitStateCycles;
	}
	else							/* BUS_MODE_CPU */
	{
		/* TODO: Find proper cycles count depending on the type of the current instruction */
		/* (e.g. movem is not correctly handled) */
		nAddCycles = CurrentInstrCycles + nWaitStateCycles;	/* read is effective at the end of the instr ? */
	}

	return nCyclesCounter[nId] + nAddCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Read a counter on CPU memory write access by taking care of the instruction
 * type (add the needed amount of additional cycles).
 */
int Cycles_GetCounterOnWriteAccess(int nId)
{
	int nAddCycles;

	/* Update counters first so we read an up-to-date value */
	Cycles_UpdateCounters();

	if ( BusMode == BUS_MODE_BLITTER )
	{
		nAddCycles = 4 + nWaitStateCycles;
	}
	else							/* BUS_MODE_CPU */
	{
		/* TODO: Find proper cycles count depending on the type of the current instruction */
		/* (e.g. movem is not correctly handled) */
		nAddCycles = CurrentInstrCycles + nWaitStateCycles;

		/* assume the behaviour of a 'move' (since this is the most */
		/* common instr used when requiring cycle precise writes) */
		if ( nAddCycles >= 8 )
			nAddCycles -= 4;			/* last 4 cycles are for prefetch */
	}

	return nCyclesCounter[nId] + nAddCycles;
}
