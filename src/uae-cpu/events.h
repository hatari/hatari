 /*
  * Hatari - events.h
  *
  * This file is distributed under the GNU Public License, version 2 or at
  * your option any later version. Read the file gpl.txt for details.
  *
  * These are best for low-frequency events. Having too many of them,
  * or using them for events that occur too frequently, can cause massive
  * slowdown.
  */

#ifndef UAE_EVENTS_H
#define UAE_EVENTS_H

#include "../includes/main.h"
#include "../includes/decode.h"
#include "../includes/mfp.h"


STATIC_INLINE void do_cycles(unsigned long cycles_to_add)
{
  cycles_to_add = (cycles_to_add+3)&0xfffffffc;
  SoundCycles += cycles_to_add;              /* Add in cycle time to get cycle-accurate sample playback */
  PendingInterruptCount -= (short)cycles_to_add;     /* Add cycle time including effective address time */
  if( PendingInterruptCount<=0 || PendingInterruptFlag)	/* Check for any interrupts or flag to service */
  {
    if( PendingInterruptFlag&PENDING_INTERRUPT_FLAG_MFP )
      MFP_CheckPendingInterrupts();
    if( PendingInterruptCount<=0 && PendingInterruptFunction )
      CALL_VAR(PendingInterruptFunction);
  }
}

#endif  /* UAE_EVENTS_H */
