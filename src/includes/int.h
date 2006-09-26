/*
  Hatari - int.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_INT_H
#define HATARI_INT_H

/* Interrupt handlers in system */
typedef enum
{
  INTERRUPT_NULL,
  INTERRUPT_VIDEO_VBL,
  INTERRUPT_VIDEO_HBL,
  INTERRUPT_VIDEO_ENDLINE,
  INTERRUPT_MFP_TIMERA,
  INTERRUPT_MFP_TIMERB,
  INTERRUPT_MFP_TIMERC,
  INTERRUPT_MFP_TIMERD,
  INTERRUPT_IKBD_RESETTIMER,
  INTERRUPT_IKBD_ACIA,
  INTERRUPT_DMASOUND,

  MAX_INTERRUPTS
} interrupt_id;

extern void (*PendingInterruptFunction)(void);
extern short int PendingInterruptCount;

extern void Int_Reset(void);
extern void Int_MemorySnapShot_Capture(BOOL bSave);
extern void Int_AcknowledgeInterrupt(void);
extern void Int_AddAbsoluteInterrupt(int CycleTime, interrupt_id Handler);
extern void Int_AddRelativeInterrupt(int CycleTime, interrupt_id Handler);
extern void Int_AddRelativeInterruptNoOffset(int CycleTime, interrupt_id Handler);
extern void Int_RemovePendingInterrupt(interrupt_id Handler);
extern BOOL Int_InterruptActive(interrupt_id Handler);
extern int Int_FindCyclesPassed(interrupt_id Handler);

#endif /* ifndef HATARI_INT_H */
