/*
  Hatari - cycInt.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CYCINT_H
#define HATARI_CYCINT_H

#define CYCINT_NEW

/* Interrupt handlers in system */
typedef enum
{
  INTERRUPT_NULL,			/* should always be the first of the list with value '0' */
  INTERRUPT_VIDEO_VBL,
  INTERRUPT_VIDEO_HBL,
  INTERRUPT_VIDEO_ENDLINE,
  INTERRUPT_MFP_MAIN_TIMERA,
  INTERRUPT_MFP_MAIN_TIMERB,
  INTERRUPT_MFP_MAIN_TIMERC,
  INTERRUPT_MFP_MAIN_TIMERD,
  INTERRUPT_MFP_TT_TIMERA,
  INTERRUPT_MFP_TT_TIMERB,
  INTERRUPT_MFP_TT_TIMERC,
  INTERRUPT_MFP_TT_TIMERD,
  INTERRUPT_ACIA_IKBD,
  INTERRUPT_IKBD_RESETTIMER,
  INTERRUPT_IKBD_AUTOSEND,
  INTERRUPT_DMASOUND_MICROWIRE, /* Used for both STE and Falcon Microwire emulation */
  INTERRUPT_CROSSBAR_25MHZ,
  INTERRUPT_CROSSBAR_32MHZ,
  INTERRUPT_FDC,
  INTERRUPT_BLITTER,
  INTERRUPT_MIDI,

  MAX_INTERRUPTS
} interrupt_id;


#define	INT_CPU_CYCLE		1
#define	INT_MFP_CYCLE		2
#define	INT_CPU8_CYCLE		3

#ifndef CYCINT_NEW

#define	INT_CPU_TO_INTERNAL	9600
#define	INT_MFP_TO_INTERNAL	31333

/* Convert cpu or mfp cycles to internal cycles */
#define INT_CONVERT_TO_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc)*INT_CPU_TO_INTERNAL : \
	       					  type == INT_MFP_CYCLE ? ( (cyc)*INT_MFP_TO_INTERNAL ) << nCpuFreqShift : \
						  ( (cyc)*INT_CPU_TO_INTERNAL ) << nCpuFreqShift )

//#define INT_CONVERT_TO_INTERNAL( cyc , type )	( ( type == INT_CPU_CYCLE ? (cyc)*INT_CPU_TO_INTERNAL : (cyc)*INT_MFP_TO_INTERNAL ) << nCpuFreqShift )
//#define INT_CONVERT_TO_INTERNAL_NO_FREQSHIFT( cyc , type )	( type == INT_CPU_CYCLE ? (cyc)*INT_CPU_TO_INTERNAL : (cyc)*INT_MFP_TO_INTERNAL )

/* Convert internal cycles to real mfp or cpu cycles */
/* Rounding is important : for example 9500 internal is 0.98 cpu and should give 1 cpu cycle, not 0 */
/* so we do (9500+9600-1)/9600 to get the closest higher integer */
//#define INT_CONVERT_FROM_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc+INT_CPU_TO_INTERNAL-1)/INT_CPU_TO_INTERNAL : (cyc+INT_MFP_TO_INTERNAL-1)/INT_MFP_TO_INTERNAL )
//#define INT_CONVERT_FROM_INTERNAL( cyc , type )	( ( type == INT_CPU_CYCLE ? (cyc)/INT_CPU_TO_INTERNAL : ((cyc)+INT_MFP_TO_INTERNAL-1)/INT_MFP_TO_INTERNAL ) >> nCpuFreqShift )

#define INT_CONVERT_FROM_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc)/INT_CPU_TO_INTERNAL : \
	       					  type == INT_MFP_CYCLE ? ( ((cyc)+INT_MFP_TO_INTERNAL-1)/INT_MFP_TO_INTERNAL ) >> nCpuFreqShift : \
						  ( (cyc)/INT_CPU_TO_INTERNAL ) >> nCpuFreqShift )

#else

/* Simulate extra bits of internal decimal precision, as MFP cycles don't convert to an integer number of CPU cycles */
#define	CYCINT_SHIFT		8

/* Convert CPU or MFP cycles to internal cycles */
#define INT_CONVERT_TO_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc) << CYCINT_SHIFT : \
						type == INT_MFP_CYCLE ? (int)( ( (Uint64)( (cyc) << CYCINT_SHIFT ) * MachineClocks.CPU_Freq ) / MachineClocks.MFP_Timer_Freq ) : \
						(cyc) << ( nCpuFreqShift + CYCINT_SHIFT ) )

/* Convert internal cycles to real CPU or MFP cycles */
#define INT_CONVERT_FROM_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc) >> CYCINT_SHIFT : \
						type == INT_MFP_CYCLE ? (int)( ( (Uint64)(cyc) * MachineClocks.MFP_Timer_Freq ) / MachineClocks.CPU_Freq ) >> CYCINT_SHIFT : \
						(cyc) >> ( nCpuFreqShift + CYCINT_SHIFT ) )


#endif


extern void (*PendingInterruptFunction)(void);
extern int PendingInterruptCount;
extern Uint64	CycInt_ActiveInt_Cycles;

extern void	CycInt_Reset(void);
extern void	CycInt_MemorySnapShot_Capture(bool bSave);

extern void	CycInt_AcknowledgeInterrupt(void);
extern void	CycInt_AddAbsoluteInterrupt(int CycleTime, int CycleType, interrupt_id Handler);
extern void	CycInt_AddRelativeInterrupt(int CycleTime, int CycleType, interrupt_id Handler);
extern void	CycInt_AddRelativeInterruptWithOffset(int CycleTime, int CycleType, interrupt_id Handler, int CycleOffset);
extern void	CycInt_ModifyInterrupt(int CycleTime, int CycleType, interrupt_id Handler);
extern void	CycInt_RemovePendingInterrupt(interrupt_id Handler);
extern int	CycInt_FindCyclesPassed(interrupt_id Handler, int CycleType, int AddCpuCycles);

extern bool	CycInt_InterruptActive(interrupt_id Handler);
extern int	CycInt_GetActiveInt(void);
extern void	CycInt_CallActiveHandler(void);

#ifndef CYCINT_NEW

static inline void CycInt_Process(void)
{
	while ( ( PendingInterruptCount <= 0 ) && ( PendingInterruptFunction ) )
		CALL_VAR(PendingInterruptFunction);
}
static inline void CycInt_Process_stop(int stop_cond)
{
	while ( ( PendingInterruptCount <= 0 ) && ( PendingInterruptFunction ) && ( stop_cond == 0 ) )
		CALL_VAR(PendingInterruptFunction);
}

#else

static inline void CycInt_Process(void)
{
	while ( CycInt_ActiveInt_Cycles <= ( CyclesGlobalClockCounter << CYCINT_SHIFT ) )
		CycInt_CallActiveHandler();
}
static inline void CycInt_Process_stop(int stop_cond)
{
	while ( ( CycInt_ActiveInt_Cycles <= ( CyclesGlobalClockCounter << CYCINT_SHIFT ) ) && ( stop_cond == 0 ) )
		CycInt_CallActiveHandler();
}

#endif


#endif /* ifndef HATARI_CYCINT_H */
