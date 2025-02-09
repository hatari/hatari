/*
  Hatari - clocks_timings.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CLOCKS_TIMINGS_H
#define HATARI_CLOCKS_TIMINGS_H




/* All the possible clock frequencies in Hz used in the supported machines. */
/* When a value is 0, the corresponding part is not available in this model */

typedef struct
{
  /* Common to all machines */
  uint32_t        MCLK_Freq;
  uint32_t        BUS_Freq;

  uint32_t        CPU_Freq;			/* 'normal' CPU Freq (eg 8 MHz for ST or 16 MHz for Falcon) */
  uint32_t        FPU_Freq;
  uint32_t        DMA_Freq;
  uint32_t        MFP_Freq;
  uint32_t        MFP_Timer_Freq;
  uint32_t        FDC_Freq;
  uint32_t        BLITTER_Freq;
  uint32_t        YM_Freq;
  uint32_t        ACIA_Freq;
  uint32_t        IKBD_Freq;

  /* STF specific */
  uint32_t        MMU_Freq;			/* STF only */
  uint32_t        GLUE_Freq;			/* STF only */
  uint32_t        SHIFTER_Freq;			/* STF/STE */

  /* STE specific */
  uint32_t        MCU_Freq;			/* replaces MMU+GLUE in STF */
  uint32_t	DMA_Audio_Freq;			/* also used for SND SHIFTER in TT */

  /* TT specific */
  uint32_t        TTVIDEO_Freq;

  /* Falcon specific */
  uint32_t	COMBEL_Freq;			/* includes the BLITTER */
  uint32_t        VIDEL_Freq;
  uint32_t        CODEC_Freq;
  uint32_t        DSP_Freq;

  /* Mega STE, TT, Falcon specific */
  uint32_t        SCC_Freq;

  /* Common to all machines, runtime variables */
  uint32_t        CPU_Freq_Emul;			/* Freq in Hz at which the CPU is emulated (taking nCpuFreqShift and CPU_Freq into account) */

} CLOCKS_STRUCT;

extern CLOCKS_STRUCT	MachineClocks;



typedef struct {
  uint64_t	Cycles;
  uint64_t	Remainder;
} CLOCKS_CYCLES_STRUCT;


extern bool	RoundVBLPerSec;


#define	CLOCKS_TIMINGS_SHIFT_VBL	24		/* The value returned by ClocksTimings_GetVBLPerSec is << 24 to increase precision */

/* Functions' prototypes */

void	ClocksTimings_InitMachine ( MACHINETYPE MachineType );
void	ClocksTimings_UpdateCpuFreqEmul ( MACHINETYPE MachineType , int nCpuFreqShift );
uint32_t	ClocksTimings_GetCyclesPerVBL ( MACHINETYPE MachineType , int ScreenRefreshRate );
uint32_t	ClocksTimings_GetVBLPerSec ( MACHINETYPE MachineType , int ScreenRefreshRate );
uint32_t	ClocksTimings_GetVBLDuration_micro ( MACHINETYPE MachineType , int ScreenRefreshRate );
int64_t	ClocksTimings_GetSamplesPerVBL ( MACHINETYPE MachineType , int ScreenRefreshRate , int AudioFreq );

void	ClocksTimings_ConvertCycles ( uint64_t CyclesIn , uint64_t ClockFreqIn , CLOCKS_CYCLES_STRUCT *CyclesStructOut , uint64_t ClockFreqOut );




#endif

