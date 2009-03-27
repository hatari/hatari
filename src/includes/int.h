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
  INTERRUPT_IKBD_MFP,
  INTERRUPT_IKBD_AUTOSEND,
  INTERRUPT_DMASOUND,
  INTERRUPT_DMASOUND_MICROWIRE,
  INTERRUPT_DSPXMIT,
  INTERRUPT_FDC,
  INTERRUPT_BLITTER,
  INTERRUPT_MIDI,

  MAX_INTERRUPTS
} interrupt_id;


#define	INT_CPU_CYCLE		1
#define	INT_MFP_CYCLE		2

#define	INT_CPU_TO_INTERNAL	9600
#define	INT_MFP_TO_INTERNAL	31333

/* Convert cpu or mfp cycles to internal cycles */
#define INT_CONVERT_TO_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc)*INT_CPU_TO_INTERNAL : (cyc)*INT_MFP_TO_INTERNAL )
/*
#define INT_CONVERT_TO_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? cyc*INT_CPU_TO_INTERNAL :\
	( ( (cyc*INT_MFP_TO_INTERNAL + INT_CPU_TO_INTERNAL*4 - 1) / (INT_CPU_TO_INTERNAL*4) ) * INT_CPU_TO_INTERNAL*4 ) )
*/

/* Convert internal cycles to real mfp or cpu cycles */
/* Rounding is important : for example 9500 internal is 0.98 cpu and should give 1 cpu cycle, not 0 */
/* so we do (9500+9600-1)/9600 to get the closest higher integer */
//#define INT_CONVERT_FROM_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc+INT_CPU_TO_INTERNAL-1)/INT_CPU_TO_INTERNAL : (cyc+INT_MFP_TO_INTERNAL-1)/INT_MFP_TO_INTERNAL )
#define INT_CONVERT_FROM_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc)/INT_CPU_TO_INTERNAL : ((cyc)+INT_MFP_TO_INTERNAL-1)/INT_MFP_TO_INTERNAL )



extern void (*PendingInterruptFunction)(void);
extern int PendingInterruptCount;

extern void Int_Reset(void);
extern void Int_MemorySnapShot_Capture(bool bSave);
extern void Int_AcknowledgeInterrupt(void);
extern void Int_AddAbsoluteInterrupt(int CycleTime, int CycleType, interrupt_id Handler);
extern void Int_AddRelativeInterrupt(int CycleTime, int CycleType, interrupt_id Handler);
extern void Int_AddRelativeInterruptNoOffset(int CycleTime, int CycleType, interrupt_id Handler);
extern void Int_AddRelativeInterruptWithOffset(int CycleTime, int CycleType, interrupt_id Handler, int CycleOffset);
extern void Int_RemovePendingInterrupt(interrupt_id Handler);
extern void Int_ResumeStoppedInterrupt(interrupt_id Handler);
extern bool Int_InterruptActive(interrupt_id Handler);
extern int Int_FindCyclesPassed(interrupt_id Handler, int CycleType);

#endif /* ifndef HATARI_INT_H */
