/*
  Hatari - cycInt.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CYCINT_H
#define HATARI_CYCINT_H


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
  INTERRUPT_SCC_BRG_A,
  INTERRUPT_SCC_TX_RX_A,
  INTERRUPT_SCC_RX_A,
  INTERRUPT_SCC_BRG_B,
  INTERRUPT_SCC_TX_RX_B,
  INTERRUPT_SCC_RX_B,

  MAX_INTERRUPTS
} interrupt_id;


#define	INT_CPU_CYCLE		1
#define	INT_MFP_CYCLE		2
#define	INT_CPU8_CYCLE		3

/* Simulate extra bits of internal decimal precision, as MFP cycles don't convert to an integer number of CPU cycles */
#define	CYCINT_SHIFT		8

/* Convert CPU or MFP cycles to internal cycles */
#define INT_CONVERT_TO_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc) << CYCINT_SHIFT : \
						type == INT_MFP_CYCLE ? (int)( ( (uint64_t)( (cyc) << CYCINT_SHIFT ) * MachineClocks.CPU_Freq_Emul ) / MachineClocks.MFP_Timer_Freq ) : \
						(cyc) << ( nCpuFreqShift + CYCINT_SHIFT ) )

/* Convert internal cycles to real CPU or MFP cycles */
#define INT_CONVERT_FROM_INTERNAL( cyc , type )	( type == INT_CPU_CYCLE ? (cyc) >> CYCINT_SHIFT : \
						type == INT_MFP_CYCLE ? (int)( ( (uint64_t)(cyc) * MachineClocks.MFP_Timer_Freq ) / MachineClocks.CPU_Freq_Emul ) >> CYCINT_SHIFT : \
						(cyc) >> ( nCpuFreqShift + CYCINT_SHIFT ) )


extern void (*PendingInterruptFunction)(void);
extern int PendingInterruptCount;
extern uint64_t	CycInt_ActiveInt_Cycles;

extern void	CycInt_Reset(void);
extern void	CycInt_MemorySnapShot_Capture(bool bSave);

extern void	CycInt_AcknowledgeInterrupt(void);
extern void	CycInt_AddAbsoluteInterrupt(int CycleTime, int CycleType, interrupt_id Handler);
extern void	CycInt_AddRelativeInterrupt(int CycleTime, int CycleType, interrupt_id Handler);
extern void	CycInt_AddRelativeInterruptWithOffset(int CycleTime, int CycleType, interrupt_id Handler, int CycleOffset);
extern void	CycInt_ModifyInterrupt(int CycleTime, int CycleType, interrupt_id Handler);
extern void	CycInt_RemovePendingInterrupt(interrupt_id Handler);
extern int	CycInt_FindCyclesRemaining(interrupt_id Handler, int CycleType);

extern bool	CycInt_InterruptActive(interrupt_id Handler);
extern int	CycInt_GetActiveInt(void);
extern void	CycInt_CallActiveHandler(uint64_t Clock);

static inline void CycInt_Process(void)
{
	while ( CycInt_ActiveInt_Cycles <= ( CyclesGlobalClockCounter << CYCINT_SHIFT ) )
		CycInt_CallActiveHandler( CyclesGlobalClockCounter );
}
static inline void CycInt_Process_stop(int stop_cond)
{
	while ( ( CycInt_ActiveInt_Cycles <= ( CyclesGlobalClockCounter << CYCINT_SHIFT ) ) && ( stop_cond == 0 ) )
		CycInt_CallActiveHandler( CyclesGlobalClockCounter );
}
/* Same as CycInt_Process but use a specific cycles clock value */
static inline void CycInt_Process_Clock(uint64_t Clock)
{
	while ( CycInt_ActiveInt_Cycles <= ( Clock << CYCINT_SHIFT ) )
		CycInt_CallActiveHandler( Clock );
}


/* TEMP : to update CYCLES_COUNTER_VIDEO during an opcode */
extern bool   CycInt_From_Opcode;
/* TEMP : to update CYCLES_COUNTER_VIDEO during an opcode */


#endif /* ifndef HATARI_CYCINT_H */
