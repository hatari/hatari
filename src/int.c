/*
  Hatari - int.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This code handles our interrupt table. So we do not need to test for every possible
  interrupt we add any pending interrupts into a table. We then scan the list if used
  entries in the table and copy the one with the least cycle count into the global
  'PendingInterruptCount' variable. This is then decremented by the execution loop -
  rather than decrement each and every entry (as the others cannot occur before this one)
  We have two methods of adding interrupts; Absolute and Relative. Absolute will set values
  from the time of the previous interrupt(eg, add HBL every 512 cycles), and Relative
  will add from the current cycle time.
  Note that interrupt may occur 'late'. Ie, if an interrupt is due in 4 cycles time but
  the current instruction takes 20 cycles we will be 16 cycles late - this is handled in
  the adjust functions.
*/
static char rcsid[] = "Hatari $Id: int.c,v 1.3 2003-07-29 12:01:55 thothy Exp $";

#include "main.h"
#include "debug.h"
#include "decode.h"
#include "ikbd.h"
#include "int.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "mfp.h"
#include "sound.h"
#include "video.h"


/* List of possible interrupt handlers to be store in 'PendingInterruptTable', used for 'MemorySnapShot' */
void *pIntHandlerFunctions[] =
{
  NULL,

#ifdef USE_DEBUGGER
  InterruptHandler_Debugger,
  InterruptHandler_SingleStep,
#endif  /* USE_DEBUGGER */
  Video_InterruptHandler_VBL,
  Video_InterruptHandler_HBL,
  Video_InterruptHandler_EndLine,
  MFP_InterruptHandler_TimerA,
  MFP_InterruptHandler_TimerB,
  MFP_InterruptHandler_TimerC,
  MFP_InterruptHandler_TimerD,
  IKBD_InterruptHandler_ResetTimer,
  IKBD_InterruptHandler_ACIA,
  NULL
};

INTERRUPTHANDLER InterruptHandlers[MAX_INTERRUPTS];
int nCyclesOver=0;
int nFrameCyclesOver=0;
int ActiveInterrupt=0;


/*-----------------------------------------------------------------------*/
/*
  Reset interrupts, handlers
*/
void Int_Reset(void)
{
  int i;

  /* Reset counts */
  PendingInterruptCount = 0;
  nCyclesOver = 0;

  /* Reset interrupt table */
  for(i=0; i<MAX_INTERRUPTS; i++) {
    InterruptHandlers[i].bUsed = FALSE;
    InterruptHandlers[i].pFunction = pIntHandlerFunctions[i];
  }
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void Int_MemorySnapShot_Capture(BOOL bSave)
{
  int i,ID;

  /* Save/Restore details */
  for(i=0; i<MAX_INTERRUPTS; i++) {
    MemorySnapShot_Store(&InterruptHandlers[i].bUsed,sizeof(InterruptHandlers[i].bUsed));
    MemorySnapShot_Store(&InterruptHandlers[i].Cycles,sizeof(InterruptHandlers[i].Cycles));
    if (bSave) {
      /* Convert function to ID */
      ID = Int_HandlerFunctionToID(InterruptHandlers[i].pFunction);
      MemorySnapShot_Store(&ID,sizeof(int));
    }
    else {
      /* Convert ID to function */
      MemorySnapShot_Store(&ID,sizeof(int));
      InterruptHandlers[i].pFunction = Int_IDToHandlerFunction(ID);
    }
  }
  MemorySnapShot_Store(&nCyclesOver,sizeof(nCyclesOver));
  MemorySnapShot_Store(&nFrameCyclesOver,sizeof(nFrameCyclesOver));
}


/*-----------------------------------------------------------------------*/
/*
  Convert interrupt handler function pointer to ID, used for saving
*/
int Int_HandlerFunctionToID(void *pHandlerFunction)
{
  int i;

  /* Is NULL, return ID 0 */
  if (pHandlerFunction==NULL)
    return(0);

  /* Scan for function match */
  for(i=1; i<MAX_INTERRUPTS; i++) {
    if (pIntHandlerFunctions[i]==pHandlerFunction)
      return(i);
  }
  
  /* Didn't find one! Oops */
  return(0);
}


/*-----------------------------------------------------------------------*/
/*
  Convert ID back into interrupt handler function, used for restoring
*/
void *Int_IDToHandlerFunction(int ID)
{
  /* Get function pointer */
  return( pIntHandlerFunctions[ID] );
}


/*-----------------------------------------------------------------------*/
/*
  Return number of clock cycles into retrace
*/
int Int_FindFrameCycles(void)
{
  return( nFrameCyclesOver + (InterruptHandlers[ActiveInterrupt].Cycles-PendingInterruptCount) );
}


/*-----------------------------------------------------------------------*/
/*
  Find next interrupt to occur, and store to global variables for decrement in instruction decode loop
*/
void Int_SetNewInterrupt(void)
{
  int LowestCycleCount=999999,LowestInterrupt=0;
  int i;

  /* Find next interrupt to go off */
  for(i=0; i<MAX_INTERRUPTS; i++) {
    /* Is interrupt pending? */
    if (InterruptHandlers[i].bUsed) {
      if (InterruptHandlers[i].Cycles<LowestCycleCount) {
        LowestCycleCount = InterruptHandlers[i].Cycles;
        LowestInterrupt = i;
      }
    }
  }

  /* Set new counts, active interrupt */
  PendingInterruptCount = (short int)InterruptHandlers[LowestInterrupt].Cycles;
  PendingInterruptFunction = InterruptHandlers[LowestInterrupt].pFunction;
  ActiveInterrupt = LowestInterrupt;

}


/*-----------------------------------------------------------------------*/
/*
  Adjust all interrupt timings, MUST call Int_SetNewInterrupt after this
*/
void Int_UpdateInterrupt(void)
{
  int CycleSubtract;
  int i;

  /* Find out how many cycles we went over (<=0) */
  nCyclesOver = PendingInterruptCount;
  /* Calculate how many cycles have passed, included time we went over */
  CycleSubtract = InterruptHandlers[ActiveInterrupt].Cycles-nCyclesOver;

  /* Adjust table */
  for(i=0; i<MAX_INTERRUPTS; i++) {
    if (InterruptHandlers[i].bUsed)
      InterruptHandlers[i].Cycles -= CycleSubtract;
  }
  nFrameCyclesOver += CycleSubtract;
}


/*-----------------------------------------------------------------------*/
/*
  Adjust all interrupt timings as 'ActiveInterrupt' has occured, and remove from active list
*/
void Int_AcknowledgeInterrupt(void)
{
  /* Update list cycle counts */
  Int_UpdateInterrupt();

  /* Disable interrupt entry which has just occured */
  InterruptHandlers[ActiveInterrupt].bUsed = FALSE;

  /* Set new */
  Int_SetNewInterrupt();
}


/*-----------------------------------------------------------------------*/
/*
  Add interrupt from time last one occurred
*/
void Int_AddAbsoluteInterrupt(int CycleTime, int Handler)
{
  InterruptHandlers[Handler].bUsed = TRUE;
  InterruptHandlers[Handler].Cycles = CycleTime + nCyclesOver;

  /* Set new */
  Int_SetNewInterrupt();
}


/*-----------------------------------------------------------------------*/
/*
  Add interrupt to occur from now
*/
void Int_AddRelativeInterrupt(int CycleTime, int Handler)
{
  InterruptHandlers[Handler].bUsed = TRUE;
  InterruptHandlers[Handler].Cycles = CycleTime;

  /* Set new */
  Int_SetNewInterrupt();
}


/*-----------------------------------------------------------------------*/
/*
  Remove a pending interrupt from our table
*/
void Int_RemovePendingInterrupt(int Handler)
{
  /* Stop interrupt */
  InterruptHandlers[Handler].bUsed = FALSE;

  /* Update list cycle counts */
  Int_UpdateInterrupt();
  /* Set new */
  Int_SetNewInterrupt();
}


/*-----------------------------------------------------------------------*/
/*
  Return TRUE if interrupt is active in list
*/
BOOL Int_InterruptActive(int Handler)
{
  /* Is timer active? */
  if (InterruptHandlers[Handler].bUsed)
    return(TRUE);

  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Return cycles passed for an interrupt handler
*/
int Int_FindCyclesPassed(int Handler)
{
  int CyclesPassed, CyclesFromLastInterrupt;

  CyclesFromLastInterrupt = (int)InterruptHandlers[ActiveInterrupt].Cycles-PendingInterruptCount;
  CyclesPassed = ((int)InterruptHandlers[Handler].Cycles-CyclesFromLastInterrupt);

  return(CyclesPassed);
}
