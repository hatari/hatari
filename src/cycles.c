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
char Cycles_rcsid[] = "Hatari $Id: cycles.c,v 1.2 2006-02-04 21:34:41 eerot Exp $";

#include "main.h"
#include "cycles.h"


int nCyclesMainCounter;                         /* Main cycles counter */

static int nCyclesCounter[CYCLES_COUNTER_MAX];  /* Array with all counters */


/*-----------------------------------------------------------------------*/
/*
  Update all cycles counters with the current value of nCyclesMainCounter.
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
/*
  Set a counter to a new value.
*/
void Cycles_SetCounter(int nId, int nValue)
{
	/* Update counters first (nCyclesMainCounter must be 0 afterwards) */
	Cycles_UpdateCounters();

	/* Now set the new value: */
	nCyclesCounter[nId] = nValue;
}


/*-----------------------------------------------------------------------*/
/*
  Read a counter.
*/
int Cycles_GetCounter(int nId)
{
	/* Update counters first so we read an up-to-date value */
	Cycles_UpdateCounters();

	return nCyclesCounter[nId];
}


/*-----------------------------------------------------------------------*/
/*
  Read a counter on CPU memory read access by taking care of the instruction
  type (add the needed amount of additional cycles).
*/
int Cycles_GetCounterOnReadAccess(int nId)
{
	int nAddCycles;

	/* Update counters first so we read an up-to-date value */
	Cycles_UpdateCounters();

	/* TODO: Find proper cycle offset depending on the type of the current instruction */
	nAddCycles = 0;

	return nCyclesCounter[nId] + nAddCycles;
}


/*-----------------------------------------------------------------------*/
/*
  Read a counter on CPU memory write access by taking care of the instruction
  type (add the needed amount of additional cycles).
*/
int Cycles_GetCounterOnWriteAccess(int nId)
{
	int nAddCycles;

	/* Update counters first so we read an up-to-date value */
	Cycles_UpdateCounters();

	/* TODO: Find proper cycle offset depending on the type of the current instruction */
	nAddCycles = 0;

	return nCyclesCounter[nId] + nAddCycles;
}
