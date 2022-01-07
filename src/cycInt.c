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

const char CycInt_fileid[] = "Hatari cycInt.c";

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
#include "cycles.h"
#include "cycInt.h"
#include "m68000.h"
#include "mfp.h"
#include "midi.h"
#include "memorySnapShot.h"
#include "sound.h"
#include "screen.h"
#include "video.h"
#include "acia.h"
#include "clocks_timings.h"


//#define	CYCINT_DEBUG


void (*PendingInterruptFunction)(void);		// TODO rename to CycInt_ActiveInt_Function
int PendingInterruptCount;

static int CycInt_DelayedCycles;

/* List of possible interrupt handlers to be stored in 'InterruptHandlers[]'
 * The list should be in the same order than the enum type 'interrupt_id' */
static void (* const pIntHandlerFunctions[MAX_INTERRUPTS])(void) =
{
	NULL,
	Video_InterruptHandler_VBL,
	Video_InterruptHandler_HBL,
	Video_InterruptHandler_EndLine,
	MFP_Main_InterruptHandler_TimerA,
	MFP_Main_InterruptHandler_TimerB,
	MFP_Main_InterruptHandler_TimerC,
	MFP_Main_InterruptHandler_TimerD,
	MFP_TT_InterruptHandler_TimerA,
	MFP_TT_InterruptHandler_TimerB,
	MFP_TT_InterruptHandler_TimerC,
	MFP_TT_InterruptHandler_TimerD,
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
	bool	Active;				/* Is interrupt active? */
#ifndef CYCINT_NEW
	Sint64	Cycles;
#else
	Uint64	Cycles;
#endif
	void	(*pFunction)(void);
	int	IntList_Prev;		/* Number of previous interrupt sorted by 'Cycles' value (or -1 if none) */
	int	IntList_Next;		/* Number of next interrupt sorted by 'Cycles' value (or -1 if none) */
					/* NOTE : type should be 'int' not 'interrupt_id' else compiler might internally */
					/* use 'unsigned int' which will fail when storing value '-1' */
} INTERRUPTHANDLER;

static INTERRUPTHANDLER InterruptHandlers[MAX_INTERRUPTS];
static interrupt_id	CycInt_ActiveInt = 0;
Uint64			CycInt_ActiveInt_Cycles;


#ifndef CYCINT_NEW
static void CycInt_SetNewInterrupt(void);
#else
static void CycInt_InsertInt ( interrupt_id IntId );
#endif


/*-----------------------------------------------------------------------*/
/**
 * Reset interrupts, handlers
 */
void CycInt_Reset(void)
{
	int i;

	/* Reset counts */
	PendingInterruptCount = 0;
	CycInt_DelayedCycles = 0;

	/* Reset interrupt table */
	for (i=0; i<MAX_INTERRUPTS; i++)
	{
#ifndef CYCINT_NEW
		InterruptHandlers[i].Active = false;
		InterruptHandlers[i].Cycles = INT_MAX;
		InterruptHandlers[i].pFunction = pIntHandlerFunctions[i];
		InterruptHandlers[i].IntList_Prev = 0;
		InterruptHandlers[i].IntList_Next = 0;
#else
		InterruptHandlers[i].Active = false;
		InterruptHandlers[i].Cycles = 0;
		InterruptHandlers[i].pFunction = pIntHandlerFunctions[i];
		InterruptHandlers[i].IntList_Prev = -1;
		InterruptHandlers[i].IntList_Next = -1;
#endif
	}

#ifdef CYCINT_NEW
	/* Interrupt 0 should always be active, but it will never trigger, */
	/* it will always be the last of the list */
	InterruptHandlers[ 0 ].Active = true;
	InterruptHandlers[ 0 ].Cycles = UINT64_MAX;

	CycInt_ActiveInt = 0;
	CycInt_ActiveInt_Cycles = InterruptHandlers[0].Cycles;
#endif


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
		MemorySnapShot_Store(&InterruptHandlers[i].Active, sizeof(InterruptHandlers[i].Active));
		MemorySnapShot_Store(&InterruptHandlers[i].Cycles, sizeof(InterruptHandlers[i].Cycles));
		MemorySnapShot_Store(&InterruptHandlers[i].IntList_Prev, sizeof(InterruptHandlers[i].IntList_Prev));
		MemorySnapShot_Store(&InterruptHandlers[i].IntList_Next, sizeof(InterruptHandlers[i].IntList_Next));
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
	MemorySnapShot_Store(&CycInt_DelayedCycles, sizeof(CycInt_DelayedCycles));
	MemorySnapShot_Store(&CycInt_ActiveInt, sizeof(CycInt_ActiveInt));
	MemorySnapShot_Store(&CycInt_ActiveInt_Cycles, sizeof(CycInt_ActiveInt_Cycles));
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
}


#ifndef CYCINT_NEW

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
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), CycInt_ActiveInt, PendingInterruptCount);

	/* Find next interrupt to go off */
	for (i = INTERRUPT_NULL+1; i < MAX_INTERRUPTS; i++)
	{
		/* Is interrupt pending? */
		if (InterruptHandlers[i].Active)
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
	CycInt_ActiveInt = LowestInterrupt;

	LOG_TRACE(TRACE_INT, "int set new out video_cyc=%d active_int=%d pending_count=%d\n",
	               Cycles_GetCounter(CYCLES_COUNTER_VIDEO), CycInt_ActiveInt, PendingInterruptCount );
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
	CycInt_DelayedCycles = PendingInterruptCount;
	/* Calculate how many cycles have passed, included time we went over */
	CycleSubtract = InterruptHandlers[CycInt_ActiveInt].Cycles - CycInt_DelayedCycles;

	/* Adjust table */
	for (i = 0; i < MAX_INTERRUPTS; i++)
	{
		if (InterruptHandlers[i].Active)
			InterruptHandlers[i].Cycles -= CycleSubtract;
	}

	LOG_TRACE(TRACE_INT, "int upd video_cyc=%d cycle_delayed=%d cycle_sub=%"PRId64"\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), CycInt_DelayedCycles, CycleSubtract);
}


/*-----------------------------------------------------------------------*/
/**
 * Adjust all interrupt timings as 'CycInt_ActiveInt' has occurred, and
 * remove from active list.
 */
void CycInt_AcknowledgeInterrupt(void)
{
	/* Update list cycle counts */
	CycInt_UpdateInterrupt();

	/* Disable interrupt entry which has just occurred */
	InterruptHandlers[CycInt_ActiveInt].Active = false;

	/* Set new */
	CycInt_SetNewInterrupt();

	LOG_TRACE(TRACE_INT, "int ack video_cyc=%d active_int=%d active_cyc=%d pending_count=%d\n",
	               Cycles_GetCounter(CYCLES_COUNTER_VIDEO), CycInt_ActiveInt, (int)InterruptHandlers[CycInt_ActiveInt].Cycles, PendingInterruptCount );
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
	if ( CycInt_ActiveInt > 0 )
		CycInt_UpdateInterrupt();

	InterruptHandlers[Handler].Active = true;
	InterruptHandlers[Handler].Cycles = INT_CONVERT_TO_INTERNAL((Sint64)CycleTime , CycleType) + CycInt_DelayedCycles;

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
	if ( CycInt_ActiveInt > 0 )
		CycInt_UpdateInterrupt();

	InterruptHandlers[Handler].Active = true;
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
	if ( CycInt_ActiveInt > 0 )
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
	InterruptHandlers[Handler].Active = false;

	/* Set new */
	CycInt_SetNewInterrupt();

	LOG_TRACE(TRACE_INT, "int remove pending video_cyc=%d handler=%d handler_cyc=%"PRId64" pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          InterruptHandlers[Handler].Cycles, PendingInterruptCount);
}



/*-----------------------------------------------------------------------*/
/**
 * Return cycles passed for an interrupt handler
 */
int CycInt_FindCyclesPassed(interrupt_id Handler, int CycleType, int AddCpuCycles)
{
	Sint64 CyclesPassed, CyclesFromLastInterrupt;

	CyclesFromLastInterrupt = InterruptHandlers[CycInt_ActiveInt].Cycles - PendingInterruptCount;
	CyclesPassed = InterruptHandlers[Handler].Cycles - CyclesFromLastInterrupt;

	LOG_TRACE(TRACE_INT, "int find passed cyc video_cyc=%d handler=%d last_cyc=%"PRId64" passed_cyc=%"PRId64"\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          CyclesFromLastInterrupt, CyclesPassed);

	return INT_CONVERT_FROM_INTERNAL ( CyclesPassed , CycleType ) ;
}


#else		// CYCINT_NEW


/*-----------------------------------------------------------------------*/
/**
 * When the interrupt handler for IntId becomes active, we insert IntId
 * in the linked list of active interrupts sorted by Cycles values
 */
static void CycInt_InsertInt ( interrupt_id IntId )
{
	int	n, prev;

#ifdef CYCINT_DEBUG
	fprintf ( stderr , "int before active=%02d active_cyc=%"PRIu64" new=%02d cyc=%"PRIu64" clock=%"PRIu64"\n" , CycInt_ActiveInt , CycInt_ActiveInt_Cycles , IntId , InterruptHandlers[ IntId ].Cycles , CyclesGlobalClockCounter );
	n = CycInt_ActiveInt;
	do
	{
		fprintf ( stderr , "  int %02d prev=%02d next=%02d cyc=%"PRIu64"\n" , n , InterruptHandlers[ n ].IntList_Prev , InterruptHandlers[ n ].IntList_Next , InterruptHandlers[ n ].Cycles );
		n = InterruptHandlers[ n ].IntList_Next;
	} while ( n >= 0 );
#endif

	/* Search for the position to insert IntId in the linked list ; we insert just before interrupt 'n'  */
	n = CycInt_ActiveInt;
	prev = InterruptHandlers[ n ].IntList_Prev;
	while ( ( n >= 0 ) && ( InterruptHandlers[ IntId ].Cycles > InterruptHandlers[ n ].Cycles ) )
	{
		n = InterruptHandlers[ n ].IntList_Next;
		prev = InterruptHandlers[ n ].IntList_Prev;
	}

	InterruptHandlers[ IntId ].IntList_Next = n;
	InterruptHandlers[ n ].IntList_Prev = IntId;

	if ( n == (int)CycInt_ActiveInt )		/* Add as the first entry from list */
	{
		/* Set the new ActiveInt */
		CycInt_ActiveInt = IntId;
		CycInt_ActiveInt_Cycles = InterruptHandlers[ CycInt_ActiveInt ].Cycles;
		/* New ActiveInt is first of the list */
		InterruptHandlers[ CycInt_ActiveInt ].IntList_Prev = -1;
	}
	else						/* Insert in middle of the list */
	{
		InterruptHandlers[ IntId ].IntList_Prev = prev;
		InterruptHandlers[ prev ].IntList_Next = IntId;
	}

#ifdef CYCINT_DEBUG
	fprintf ( stderr , "int after active=%02d active_cyc=%"PRIu64" new=%02d cyc=%"PRIu64" clock=%"PRIu64"\n" , CycInt_ActiveInt , CycInt_ActiveInt_Cycles , IntId , InterruptHandlers[ IntId ].Cycles , CyclesGlobalClockCounter );
	n = CycInt_ActiveInt;
	do
	{
		fprintf ( stderr , "  int %02d prev=%02d next=%02d cyc=%"PRIu64"\n" , n , InterruptHandlers[ n ].IntList_Prev , InterruptHandlers[ n ].IntList_Next , InterruptHandlers[ n ].Cycles );
		n = InterruptHandlers[ n ].IntList_Next;
	} while ( n >= 0 );
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * As 'CycInt_ActiveInt' has occurred, we remove it from active list
 * and set a new value for CycInt_ActiveInt
 */
void CycInt_AcknowledgeInterrupt(void)
{
	/* Disable interrupt's entry which has just occurred */
	InterruptHandlers[ CycInt_ActiveInt ].Active = false;

	/* Set the new ActiveInt as the next in list (it can be INTERRUPT_NULL (=0) ) */
	CycInt_ActiveInt = InterruptHandlers[ CycInt_ActiveInt ].IntList_Next;
	CycInt_ActiveInt_Cycles = InterruptHandlers[ CycInt_ActiveInt ].Cycles;
	/* New ActiveInt is first of the list */
	InterruptHandlers[ CycInt_ActiveInt ].IntList_Prev = -1;

	LOG_TRACE(TRACE_INT, "int ack video_cyc=%d active_int=%d clock=%"PRIu64" active_cyc=%"PRIu64" pending_count=%d\n",
			Cycles_GetCounter(CYCLES_COUNTER_VIDEO), CycInt_ActiveInt,
			CyclesGlobalClockCounter , InterruptHandlers[CycInt_ActiveInt].Cycles, PendingInterruptCount );
}


/*-----------------------------------------------------------------------*/
/**
 * Add interrupt from the time last one should have occurred.
 * We take into account CycInt_DelayedCycles (<=0) which can be =0 if the
 * interrupt could be processed at exactly InterruptHandlers[].Cycles or can be <0
 * if the interrupt was delayed by some cycles
 */
void CycInt_AddAbsoluteInterrupt(int CycleTime, int CycleType, interrupt_id Handler)
{
	/* Enable interrupt with new Cycles value */
	InterruptHandlers[ Handler ].Active = true;
	InterruptHandlers[ Handler ].Cycles = INT_CONVERT_TO_INTERNAL((Sint64)CycleTime , CycleType) + CycInt_DelayedCycles;
	InterruptHandlers[ Handler ].Cycles += INT_CONVERT_TO_INTERNAL(CyclesGlobalClockCounter,INT_CPU_CYCLE);

	CycInt_InsertInt ( Handler );

	LOG_TRACE(TRACE_INT, "int add abs video_cyc=%d handler=%d clock=%"PRIu64" handler_cyc=%"PRIu64" pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          CyclesGlobalClockCounter , InterruptHandlers[Handler].Cycles, PendingInterruptCount );
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
	/* Check interrupt is not already enabled ; if so, remove it first */
	if ( InterruptHandlers[ Handler ].Active == true )
		CycInt_RemovePendingInterrupt ( Handler );

	/* Enable interrupt with new Cycles value */
	InterruptHandlers[ Handler ].Active = true;
	InterruptHandlers[ Handler ].Cycles = INT_CONVERT_TO_INTERNAL((Sint64)CycleTime , CycleType) + CycleOffset;
	InterruptHandlers[ Handler ].Cycles += INT_CONVERT_TO_INTERNAL(CyclesGlobalClockCounter,INT_CPU_CYCLE);

	CycInt_InsertInt ( Handler );

	LOG_TRACE(TRACE_INT, "int add rel offset video_cyc=%d handler=%d clock=%"PRIu64" handler_cyc=%"PRIu64" offset_cyc=%d pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          CyclesGlobalClockCounter , InterruptHandlers[Handler].Cycles, CycleOffset, PendingInterruptCount);
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
	/* First, we remove the interrupt from the list */
	CycInt_RemovePendingInterrupt ( Handler );

	/* Enable interrupt with new Cycles value */
	InterruptHandlers[ Handler ].Active = true;
	InterruptHandlers[ Handler ].Cycles += INT_CONVERT_TO_INTERNAL((Sint64)CycleTime , CycleType);
	InterruptHandlers[ Handler ].Cycles += INT_CONVERT_TO_INTERNAL(CyclesGlobalClockCounter,INT_CPU_CYCLE);

	CycInt_InsertInt ( Handler );

	LOG_TRACE(TRACE_INT, "int modify video_cyc=%d handler=%d clock=%"PRIu64" handler_cyc=%"PRIu64" pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          CyclesGlobalClockCounter , InterruptHandlers[Handler].Cycles, PendingInterruptCount );
}


/*-----------------------------------------------------------------------*/
/**
 * Remove a pending interrupt from our table
 * If Handler== CycInt_ActiveInt, we also set a new value for CycInt_ActiveInt
 */
void CycInt_RemovePendingInterrupt(interrupt_id Handler)
{
	/* Check interrupt is not already disabled ; if so, don't do anything */
	if ( InterruptHandlers[ Handler ].Active == false )
	{
		LOG_TRACE(TRACE_INT, "int remove pending already disabled video_cyc=%d handler=%d clock=%"PRIu64" handler_cyc=%"PRIu64" pending_count=%d\n",
			Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
			CyclesGlobalClockCounter , InterruptHandlers[Handler].Cycles, PendingInterruptCount);
		return;
	}

	/* Disable interrupt's entry */
	InterruptHandlers[Handler].Active = false;

	if ( Handler == CycInt_ActiveInt )	/* Remove first entry from list */
	{
		/* Set the new ActiveInt as the next in list (it can be INTERRUPT_NULL) */
		CycInt_ActiveInt = InterruptHandlers[ CycInt_ActiveInt ].IntList_Next;
		CycInt_ActiveInt_Cycles = InterruptHandlers[ CycInt_ActiveInt ].Cycles;
		/* New ActiveInt is first of the list */
		InterruptHandlers[ CycInt_ActiveInt ].IntList_Prev = -1;
	}

	else					/* Remove an entry 'n' in middle of the list */
	{
		/* Update prev/next for the entries n-1 and n+1 */
		InterruptHandlers[ InterruptHandlers[Handler].IntList_Prev ].IntList_Next = InterruptHandlers[ Handler ].IntList_Next;
		InterruptHandlers[ InterruptHandlers[Handler].IntList_Next ].IntList_Prev = InterruptHandlers[ Handler ].IntList_Prev;
	}

	LOG_TRACE(TRACE_INT, "int remove pending video_cyc=%d handler=%d clock=%"PRIu64" handler_cyc=%"PRIu64" pending_count=%d\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          CyclesGlobalClockCounter , InterruptHandlers[Handler].Cycles, PendingInterruptCount);
#ifdef CYCINT_DEBUG
	fprintf ( stderr , "int remove after active=%02d active_cyc=%"PRIu64" clock=%"PRIu64"\n" , CycInt_ActiveInt , CycInt_ActiveInt_Cycles , CyclesGlobalClockCounter );
	int n = CycInt_ActiveInt;
	do
	{
		fprintf ( stderr , "  int %02d prev=%02d next=%02d cyc=%"PRIu64"\n" , n , InterruptHandlers[ n ].IntList_Prev , InterruptHandlers[ n ].IntList_Next , InterruptHandlers[ n ].Cycles );
		n = InterruptHandlers[ n ].IntList_Next;
	} while ( n >= 0 );
#endif
}



/*-----------------------------------------------------------------------*/
/**
 * Return cycles remaining for an interrupt handler
 * Remaining cycles are counted from current clock CyclesGlobalClockCounter,
 * with a possible extra CPU cycles delay in AddCpuCycles
 */
// TODO : this should be renamed to CycInt_FindCyclesRemaining instead
int CycInt_FindCyclesPassed(interrupt_id Handler, int CycleType, int AddCpuCycles)
{
	Sint64 Cycles;

	Cycles = InterruptHandlers[Handler].Cycles - INT_CONVERT_TO_INTERNAL( ( CyclesGlobalClockCounter+AddCpuCycles ) , INT_CPU_CYCLE );

	LOG_TRACE(TRACE_INT, "int find passed cyc video_cyc=%d handler=%d clock=%"PRIu64" int_cyc=%"PRIu64" remain_cyc=%"PRIu64"\n",
	          Cycles_GetCounter(CYCLES_COUNTER_VIDEO), Handler,
	          CyclesGlobalClockCounter , InterruptHandlers[Handler].Cycles, Cycles);

	return INT_CONVERT_FROM_INTERNAL ( Cycles , CycleType ) ;
}


#endif		// CYCINT_NEW


/*-----------------------------------------------------------------------*/
/**
 * Return true if interrupt is active in list
 */
bool	CycInt_InterruptActive(interrupt_id Handler)
{
	/* Is timer active? */
	if (InterruptHandlers[Handler].Active)
		return true;

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of the active interrupt (0 means no active int)
 */
int	CycInt_GetActiveInt(void)
{
	return CycInt_ActiveInt;
}


/*-----------------------------------------------------------------------*/
/**
 * Call the handler associated with the active interrupt (it should never be NULL)
 */
void	CycInt_CallActiveHandler(void)
{
#ifdef CYCINT_DEBUG
	fprintf ( stderr , "int remove after active=%02d active_cyc=%"PRIu64" clock=%"PRIu64"\n" , CycInt_ActiveInt , CycInt_ActiveInt_Cycles , CyclesGlobalClockCounter );
	int n = CycInt_ActiveInt;
	do
	{
		fprintf ( stderr , "  int %02d prev=%02d next=%02d cyc=%"PRIu64"\n" , n , InterruptHandlers[ n ].IntList_Prev , InterruptHandlers[ n ].IntList_Next , InterruptHandlers[ n ].Cycles );
		n = InterruptHandlers[ n ].IntList_Next;
	} while ( n >= 0 );
#endif
	/* For compatibility with old cycInt code, we compute a value of PendingInterruptCount */
	/* at the time the interrupt happens. PendingInterruptCount will be <= 0 */
	/* A value <0 indicates that the interrupt was delayed by some cycles */
	/* TODO : rename this variable later to sthg more explicit when old cycInt code is removed */
	/* (keep only CycInt_DelayedCycles for example) */
	PendingInterruptCount = CycInt_ActiveInt_Cycles - INT_CONVERT_TO_INTERNAL(CyclesGlobalClockCounter,INT_CPU_CYCLE);
	CycInt_DelayedCycles = PendingInterruptCount;
//fprintf ( stderr , "int call handler pending=%d\n" , PendingInterruptCount );

	CALL_VAR ( InterruptHandlers[CycInt_ActiveInt].pFunction );
}

