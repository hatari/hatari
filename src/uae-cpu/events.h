 /*
  * Events
  * These are best for low-frequency events. Having too many of them,
  * or using them for events that occur too frequently, can cause massive
  * slowdown.
  */

#include "../includes/decode.h"
#include "../includes/main.h"

STATIC_INLINE void do_cycles(unsigned long cycles_to_add)
{
 SoundCycles += cycles_to_add;			/* Add in cycle time to get cycle-accurate sample playback */
 PendingInterruptCount -= (short)cycles_to_add;	/* Add cycle time including effective address time */
 if( PendingInterruptCount<=0 || PendingInterruptFlag)	/* Check for any interrupts or flag to service */
  {
   if( PendingInterruptFlag&PENDING_INTERRUPT_FLAG_TRACE )
     M68000_TraceModeTriggered();
//fprintf(stderr,"do_cycles: PendingInterruptFlag=%i, PendingInterruptCount=%i, PendingInterruptFunction=%lx\n",
//           (int)PendingInterruptFlag, (int)PendingInterruptCount, (long)PendingInterruptFunction);
   if( 1/*PendingInterruptFlag&PENDING_INTERRUPT_FLAG_MFP*/ )
    if( PendingInterruptCount<=0 && PendingInterruptFunction )
      { CALL_VAR(PendingInterruptFunction); }
   MFP_CheckPendingInterrupts();
  }
}
