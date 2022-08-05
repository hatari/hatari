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
  Uint32        MCLK_Freq;
  Uint32        BUS_Freq;

  Uint32        CPU_Freq;			/* 'normal' CPU Freq (eg 8 MHz for ST or 16 MHz for Falcon) */
  Uint32        FPU_Freq;
  Uint32        DMA_Freq;
  Uint32        MFP_Freq;
  Uint32        MFP_Timer_Freq;
  Uint32        FDC_Freq;
  Uint32        BLITTER_Freq;
  Uint32        YM_Freq;
  Uint32        ACIA_Freq;
  Uint32        IKBD_Freq;

  /* STF specific */
  Uint32        MMU_Freq;			/* STF only */
  Uint32        GLUE_Freq;			/* STF only */
  Uint32        SHIFTER_Freq;			/* STF/STE */

  /* STE specific */
  Uint32        MCU_Freq;			/* replaces MMU+GLUE in STF */
  Uint32	DMA_Audio_Freq;			/* also used for SND SHIFTER in TT */

  /* TT specific */
  Uint32        TTVIDEO_Freq;

  /* Falcon specific */
  Uint32	COMBEL_Freq;			/* includes the BLITTER */
  Uint32        VIDEL_Freq;
  Uint32        CODEC_Freq;
  Uint32        DSP_Freq;

  /* Common to all machines, runtime variables */
  Uint32        CPU_Freq_Emul;			/* Freq in Hz at which the CPU is emulated (taking nCpuFreqShift and CPU_Freq into account) */

} CLOCKS_STRUCT;

extern CLOCKS_STRUCT	MachineClocks;



typedef struct {
  Uint64	Cycles;
  Uint64	Remainder;
} CLOCKS_CYCLES_STRUCT;


extern bool	RoundVBLPerSec;


#define	CLOCKS_TIMINGS_SHIFT_VBL	24		/* The value returned by ClocksTimings_GetVBLPerSec is << 24 to increase precision */

/* Functions' prototypes */

void	ClocksTimings_InitMachine ( MACHINETYPE MachineType );
void	ClocksTimings_UpdateCpuFreqEmul ( MACHINETYPE MachineType , int nCpuFreqShift );
Uint32	ClocksTimings_GetCyclesPerVBL ( MACHINETYPE MachineType , int ScreenRefreshRate );
Uint32	ClocksTimings_GetVBLPerSec ( MACHINETYPE MachineType , int ScreenRefreshRate );
Uint32	ClocksTimings_GetVBLDuration_micro ( MACHINETYPE MachineType , int ScreenRefreshRate );
Sint64	ClocksTimings_GetSamplesPerVBL ( MACHINETYPE MachineType , int ScreenRefreshRate , int AudioFreq );

void	ClocksTimings_ConvertCycles ( Uint32 CyclesIn , Uint64 ClockFreqIn , CLOCKS_CYCLES_STRUCT *CyclesStructOut , Uint64 ClockFreqOut );




#endif

