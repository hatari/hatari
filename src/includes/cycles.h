/*
  Hatari - cycles.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CYCLES_H
#define HATARI_CYCLES_H

#include <stdbool.h>
#include <SDL_endian.h>

enum
{
	CYCLES_COUNTER_SOUND,
	CYCLES_COUNTER_VIDEO,
	CYCLES_COUNTER_CPU,

	CYCLES_COUNTER_MAX
};


extern int	nCyclesMainCounter;
extern Uint64	CyclesGlobalClockCounter;

extern int	CurrentInstrCycles;


extern void Cycles_MemorySnapShot_Capture(bool bSave);
extern void Cycles_SetCounter(int nId, int nValue);
extern int Cycles_GetCounter(int nId);
extern int Cycles_GetCounterOnReadAccess(int nId);
extern int Cycles_GetCounterOnWriteAccess(int nId);
extern Uint64 Cycles_GetClockCounterOnReadAccess(void);
extern Uint64 Cycles_GetClockCounterOnWriteAccess(void);

#endif  /* HATARI_CYCLES_H */
