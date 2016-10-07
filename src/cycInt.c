/*
  Hatari - cycInt.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This code handles our table with callbacks for cycle accurate program
  interruption. We add any pending callback handler into a table so that we do
  not need to test for every possible interrupt event. We then scan
  the list if used entries in the table and copy the one with the least cycle
  count into the global 'PendingInterruptCount' variable. This is then
  decremented by the execution loop - rather than decrement each and every
  entry (as the others cannot occur before this one).
  We have two methods of adding interrupts; Absolute and Relative.
  Absolute will set values from the time of the previous interrupt (e.g., add
  HBL every 512 cycles), and Relative will add from the current cycle time.
  Note that interrupt may occur 'late'. I.e., if an interrupt is due in 4
  cycles time but the current instruction takes 20 cycles we will be 16 cycles
  late - this is handled in the adjust functions.

  In order to handle both CPU and MFP interrupt events, we don't convert MFP
  cycles to CPU cycles, because it requires some floating points approximations
  and accumulates some errors that could lead to bad results.
  Instead, CPU and MFP cycles are converted to 'internal' cycles with the
  following rule :
	- 1 CPU cycle gives  9600 internal cycles
	- 1 MFP cycle gives 31333 internal cycle

  All interrupt events are then handled in the 'internal' units and are
  converted back to cpu or mfp units when needed. This allows very good
  synchronisation between CPU and MFP, without the rounding errors of floating
  points math.

  Thanks to Arnaud Carre (Leonard / Oxygene) for sharing this method used in
  Saint (and also used in sc68).

  Conversions are based on these values :
	real MFP frequency is 2457600 Hz
	real CPU frequency is 8021247 Hz (PAL european STF), which we round to 8021248.

  Then :
	8021248 = ( 2^8 * 31333 )
	2457600 = ( 2^15 * 3 * 5^2 )

  So, the ratio 8021248 / 2457600 can be expressed as 31333 / 9600
*/

const char CycInt_fileid[] = "Hatari cycInt.c : " __DATE__ " " __TIME__;

#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "main.h"
#include "configuration.h"
#include "blitter.h"
#include "dmaSnd.h"
#include "crossbar.h"
#include "fdc.h"
#include "ikbd.h"
#include "cycInt.h"
#include "m68000.h"
#include "mfp.h"
#include "midi.h"
#include "memorySnapShot.h"
#include "sound.h"
#include "screen.h"
#include "video.h"
#include "acia.h"


void (*PendingInterruptFunction)(void);
int PendingInterruptCount;

static int nCyclesOver;

/* List of possible interrupt handlers to be store in 'PendingInterruptTable',
 * used for 'MemorySnapShot' */
static void (* const pIntHandlerFunctions[MAX_INTERRUPTS])(void) =
{
	NULL,
	Video_InterruptHandler_VBL,
	Video_InterruptHandler_HBL,
	Video_InterruptHandler_EndLine,
	MFP_InterruptHandler_TimerA,
	MFP_InterruptHandler_TimerB,
	MFP_InterruptHandler_TimerC,
	MFP_InterruptHandler_TimerD,
	ACIA_InterruptHandler_IKBD,
	IKBD_InterruptHandler_ResetTimer,
	IKBD_InterruptHandler_AutoSend,
	DmaSnd_InterruptHandler_Microwire, /* Used for both STE and Falcon Microwire emulation */
	Crossbar_InterruptHandler_25Mhz,
	Crossbar_InterruptHandler_32Mhz,
	FDC_InterruptHandler_Update,
	Blitter_InterruptHandler,
	Midi_InterruptHandler_Update,

};

/* Event timer structure - keeps next timer to occur in structure so don't need
 * to check all entries */
typedef struct
{
	bool bUsed;                   /* Is interrupt active? */
	Sint64 Cycles;
	void (*pFunction)(void);
} INTERRUPTHANDLER;

static INTERRUPTHANDLER InterruptHandlers[MAX_INTERRUPTS];
static int ActiveInterrupt=0;

static void CycInt_SetNewInterrupt(void);

/*-----------------------------------------------------------------------*/
/**
 * Reset interrupts, handlers
 */
void CycInt_Reset(void)
{
	int i;

	/* Reset counts */
	PendingInterruptCount = 0;
	ActiveInterrupt = 0;
	nCyclesOver = 0;

	/* Reset interrupt table */
	for (i=0; i<MAX_INTERRUPTS; i++)
	{
		InterruptHandlers[i].bUsed = false;
		InterruptHandlers[i].Cycles = INT_MAX;
		InterruptHandlers[i].pFunction = pIntHandlerFunctions[i];
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Convert interrupt handler function pointer to ID, used for saving
 */
static int CycInt_HandlerFunctionToID(void (*pHandlerFunction)(void))
{
	int i;

	/* Scan for function match */
	for (i=0; i<MAX_INTERRUPTS; i++)
	{
		if (pIntHandlerFunctions[i]==pHandlerFunction)
			return i;
	}

	/* Didn't find one! Oops */
	fprintf(stderr, "\nError: didn't find interrupt function matching 0x%p\n",
	        pHandlerFunction);
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Convert ID back into interrupt handler function, used for restoring
 * We return a function pointer :  void (*f)(void)
 * (we could use typedef for better readability)
 */
static void ( *CycInt_IDToHandlerFunction(int ID) )( void )
{
	/* Get function pointer */
	return pIntHandlerFunctions[ID];
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void CycInt_MemorySnapShot_Capture(bool bSave)
{
	int i,ID;

	/* Save/Restore details */
	for (i=0; i<MAX_INTERRUPTS; i++)
	{
		MemorySnapShot_Store(&InterruptHandlers[i].bUsed, sizeof(InterruptHandlers[i].bUsed));
		MemorySnapShot_Store(&InterruptHandlers[i].Cycles, sizeof(InterruptHandlers[i].Cycles));
		if (bSave)
		{
			/* Convert function to ID */
			ID = CycInt_HandlerFunctionToID(InterruptHandlers[i].pFunction);
			MemorySnapShot_Store(&ID, sizeof(int));
		}
		else
		{
			/* Convert ID to function */
			MemorySnapShot_Store(&ID, sizeof(int));
			InterruptHandlers[i].pFunction = CycInt_IDToHandlerFunction(ID);
		}
	}
	MemorySnapShot_Store(&nCyclesOver, sizeof(nCyclesOver));
	MemorySnapShot_Store(&PendingInterruptCount, sizeof(PendingInterruptCount));
	if (bSave)
	{
		/* Convert function to ID */
		ID = CycInt_HandlerFunctionToID(PendingInterruptFunction);
		MemorySnapShot_Store(&ID, sizeof(int));
	}
	else
	{
		/* Convert ID to function */
		MemorySnapShot_Store(&ID, sizeof(int));
		PendingInterruptFunction = CycInt_IDToHandlerFunction(ID);
	}


	if (!bSave)
		CycInt_SetNewInterrupt();	/* when restoring snapshot, compute current state after */
}


/*-----------------------------------------------------------------------*/
/**
 * Find next interrupt to occur, and store to global variables for decrement
 * in instruction decode loop.
 * Note: Although InterruptHandlers.Cycles and LowestCycleCount are 64 bit
 * variables to get all the cycle counters right (e.g. the DMA sound counter
 * can get very high), PendingInterruptCount is still a 32 bit variable for
 * performance reasons (it's decremented after each CPU instruction).
 * So we have to initialize LowestCycleCount with INT_MAX, not with INT64_MAX!
 * Since there is always a VBL or HBL counter pending which fits fine into the
 * 32 bit variable, we can be sure that we don't run into problems here.
 */
static void CycInt_SetNewInterrupt(void)
{
	Sint64 LowestCycleCount = INT_MAX;
	interrupt_id LowestInterrupt = INTERRUPT_NULL, i;

	LOG_TRACE(TRACE_INT, "int set new in video_cyc=%d active_int=%d pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), ActiveInterrupt, PendingInterruptCount);

	/* Find next interrupt to go off */
	for (i = INTERRUPT_NULL+1; i < MAX_INTERRUPTS; i++)
	{
		/* Is interrupt pending? */
		if (InterruptHandlers[i].bUsed)
		{
			if (InterruptHandlers[i].Cycles < LowestCycleCount)
			{
				LowestCycleCount = InterruptHandlers[i].Cycles;
				LowestInterrupt = i;
			}
		}
	}

	/* Set new counts, active interrupt */
	PendingInterruptCount = InterruptHandlers[LowestInterrupt].Cycles;
	PendingInterruptFunction = InterruptHandlers[LowestInterrupt].pFunction;
	ActiveInterrupt = LowestInterrupt;

	LOG_TRACE(TRACE_INT, "int set new out video_cyc=%d active_int=%d pending_count=%d\n",
	               Cycles_GetCounter(CYCLES_COUNTER_VIDEO), ActiveInterrupt, PendingInterruptCount );
}


/*-----------------------------------------------------------------------*/
/**
 * Adjust all interrupt timings, MUST call CycInt_SetNewInterrupt after this.
 */
static void CycInt_UpdateInterrupt(void)
{
	Sint64 CycleSubtract;
	int i;

	/* Find out how many cycles we went over (<=0) */
	nCyclesOver = PendingInterruptCount;
	/* Calculate how many cycles have passed, included time we went over */
	CycleSubtract = InterruptHandlers[ActiveInterrupt].Cycles - nCyclesOver;

	/* Adjust table */
	for (i = 0; i < MAX_INTERRUPTS; i++)
	{
		if (InterruptHandlers[i].bUsed)
			InterruptHandlers[i].Cycles -= CycleSubtract;
	}

	LOG_TRACE(TRACE_INT, "int upd video_cyc=%d cycle_over=%d cycle_sub=%"PRId64"\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), nCyclesOver, CycleSubtract);
}


/*-----------------------------------------------------------------------*/
/**
 * Adjust all interrupt timings as 'ActiveInterrupt' has occurred, and
 * remove from active list.
 */
void CycInt_AcknowledgeInterrupt(void)
{
	/* Update list cycle counts */
	CycInt_UpdateInterrupt();

	/* Disable interrupt entry which has just occurred */
	InterruptHandlers[ActiveInterrupt].bUsed = false;

	/* Set new */
	CycInt_SetNewInterrupt();

	LOG_TRACE(TRACE_INT, "int ack video_cyc=%d active_int=%d active_cyc=%d pending_count=%d\n",
	               Cycles_GetCounter(CYCLES_COUNTER_VIDEO), ActiveInterrupt, (int)InterruptHandlers[ActiveInterrupt].Cycles, PendingInterruptCount );
}


/*-----------------------------------------------------------------------*/
/**
 * Add interrupt from time last one occurred.
 */
void CycInt_AddAbsoluteInterrupt(int CycleTime, int CycleType, interrupt_id Handler)
{
	assert(CycleTime >= 0);

	/* Update list cycle counts with current PendingInterruptCount before adding a new int, */
	/* because CycInt_SetNewInterrupt can change the active int / PendingInterruptCount */
	if ( ActiveInterrupt > 0 )
		CycInt_UpdateInterrupt();

	InterruptHandlers[Handler].bUsed = true;
	InterruptHandlers[Handler].Cycles = INT_CONVERT_TO_INTERNAL((Sint64)CycleTime , CycleType) + nCyclesOver;

	/* Set new active int and compute a new value for PendingInterruptCount*/
	CycInt_SetNewInterrupt();

	LOG_TRACE(TRACE_INT, "int add abs video_cyc=%d handler=%d handler_cyc=%"PRId64" pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          InterruptHandlers[Handler].Cycles, PendingInterruptCount );
}


/*-----------------------------------------------------------------------*/
/**
 * Add interrupt to occur from now.
 */
void CycInt_AddRelativeInterrupt(int CycleTime, int CycleType, interrupt_id Handler)
{
	CycInt_AddRelativeInterruptWithOffset(CycleTime, CycleType, Handler, 0);
}


/*-----------------------------------------------------------------------*/
/**
 * Add interrupt to occur after CycleTime/CycleType + CycleOffset.
 * CycleOffset can be used to add another delay to the resulting
 * number of internal cycles (should be 0 most of the time, except in
 * the MFP emulation to start timers precisely based on the number of
 * cycles of the current instruction).
 * This allows to restart an MFP timer just after it expired.
 */
void CycInt_AddRelativeInterruptWithOffset(int CycleTime, int CycleType, interrupt_id Handler, int CycleOffset)
{
//fprintf ( stderr , "int add rel %d type %d handler %d offset %d\n" , CycleTime,CycleType,Handler,CycleOffset );
	assert(CycleTime >= 0);

	/* Update list cycle counts with current PendingInterruptCount before adding a new int, */
	/* because CycInt_SetNewInterrupt can change the active int / PendingInterruptCount */
	if ( ActiveInterrupt > 0 )
		CycInt_UpdateInterrupt();

	InterruptHandlers[Handler].bUsed = true;
	InterruptHandlers[Handler].Cycles = INT_CONVERT_TO_INTERNAL((Sint64)CycleTime , CycleType) + CycleOffset;

	/* Set new active int and compute a new value for PendingInterruptCount*/
	CycInt_SetNewInterrupt();

	LOG_TRACE(TRACE_INT, "int add rel offset video_cyc=%d handler=%d handler_cyc=%"PRId64" offset_cyc=%d pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          InterruptHandlers[Handler].Cycles, CycleOffset, PendingInterruptCount);
}


/*-----------------------------------------------------------------------*/
/**
 * Modify interrupt's Cycles to make it happen earlier or later.
 * This will not restart the interrupt, but add CycleTime cycles to the
 * current value of the counter.
 * CycleTime can be <0 or >0
 */
void CycInt_ModifyInterrupt(int CycleTime, int CycleType, interrupt_id Handler)
{
	/* Update list cycle counts with current PendingInterruptCount before adding a new int, */
	/* because CycInt_SetNewInterrupt can change the active int / PendingInterruptCount */
	if ( ActiveInterrupt > 0 )
		CycInt_UpdateInterrupt();

	InterruptHandlers[Handler].Cycles += INT_CONVERT_TO_INTERNAL((Sint64)CycleTime , CycleType);

	/* Set new active int and compute a new value for PendingInterruptCount*/
	CycInt_SetNewInterrupt();

	LOG_TRACE(TRACE_INT, "int modify video_cyc=%d handler=%d handler_cyc=%"PRId64" pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          InterruptHandlers[Handler].Cycles, PendingInterruptCount );
}


/*-----------------------------------------------------------------------*/
/**
 * Remove a pending interrupt from our table
 */
void CycInt_RemovePendingInterrupt(interrupt_id Handler)
{
	/* Update list cycle counts, including the handler we want to remove */
	/* to be able to resume it later (for MFP timers) */
	CycInt_UpdateInterrupt();

	/* Stop interrupt after CycInt_UpdateInterrupt, for CycInt_ResumeStoppedInterrupt */
	InterruptHandlers[Handler].bUsed = false;

	/* Set new */
	CycInt_SetNewInterrupt();

	LOG_TRACE(TRACE_INT, "int remove pending video_cyc=%d handler=%d handler_cyc=%"PRId64" pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          InterruptHandlers[Handler].Cycles, PendingInterruptCount);
}


/*-----------------------------------------------------------------------*/
/**
 * Resume a stopped interrupt from its current cycle count (for MFP timers)
 */
void CycInt_ResumeStoppedInterrupt(interrupt_id Handler)
{
	/* Restart interrupt */
	InterruptHandlers[Handler].bUsed = true;

	/* Update list cycle counts */
	CycInt_UpdateInterrupt();
	/* Set new */
	CycInt_SetNewInterrupt();

	LOG_TRACE(TRACE_INT, "int resume stopped video_cyc=%d handler=%d handler_cyc=%"PRId64" pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          InterruptHandlers[Handler].Cycles, PendingInterruptCount);
}


/*-----------------------------------------------------------------------*/
/**
 * Return true if interrupt is active in list
 */
bool CycInt_InterruptActive(interrupt_id Handler)
{
	/* Is timer active? */
	if (InterruptHandlers[Handler].bUsed)
		return true;

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of the active interrupt (0 means no active int)
 */
int CycInt_GetActiveInt(void)
{
	return ActiveInterrupt;
}


/*-----------------------------------------------------------------------*/
/**
 * Return cycles passed for an interrupt handler
 */
int CycInt_FindCyclesPassed(interrupt_id Handler, int CycleType)
{
	Sint64 CyclesPassed, CyclesFromLastInterrupt;

	CyclesFromLastInterrupt = InterruptHandlers[ActiveInterrupt].Cycles - PendingInterruptCount;
	CyclesPassed = InterruptHandlers[Handler].Cycles - CyclesFromLastInterrupt;

	LOG_TRACE(TRACE_INT, "int find passed cyc video_cyc=%d handler=%d last_cyc=%"PRId64" passed_cyc=%"PRId64"\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          CyclesFromLastInterrupt, CyclesPassed);

	return INT_CONVERT_FROM_INTERNAL ( CyclesPassed , CycleType ) ;
}
