/*
  Hatari - clocks_timings.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Clocks Timings for the hardware components in each supported machine type,
  as well as functions taking into account the exact length of a VBL to
  precisely emulate video/audio parts (number of VBL per sec, number of
  audio samples per VBL, ...)

  The video freq is not exactly 50 or 60 Hz because the number of cpu cycles
  per second is not a multiple of the number of cpu cycles per VBL.
  This can cause synchronisation errors between audio and video effects
  when both components use different clocks (eg in STE where audio DMA clock
  is not the same as the cpu clock).

  To get the best results, it's recommended to set RoundVBLPerSec=false.

  Note that if you do so, the number of VBL won't be exactly 50 or 60 per sec
  but 50.05 or 60.04 ; if this does not work with your display, set RoundVBLPerSec=true
  to get an integer number of VBL per sec (but this should not be needed).



ST :
  MCLK		= 32 MHz
  SHIFTER	IN = 32 MHz					OUT = 16 MHz
  MMU		IN = 16 MHz					OUT = 8 MHz, 4 MHz
  GLUE		IN = 8 MHz					OUT = 2 MHz, 500 kHz
  BUS		= 8 MHz

  CPU 68000	IN = 8 MHz
  DMA		IN = 8 MHz
  MFP 68901	IN = 4 MHz, 2.4576 MHz (external clock)
  FDC WD1772	IN = 8 MHz
  BLITTER	IN = 8 MHz
  YM2149	IN = 2 MHz
  ACIA MC6850	IN = 500 kHz
  IKBD HD6301	IN = 1 MHZ (local clock)


STE :
  MCLK		= 32 MHz
  EXT OSC	= 8 MHZ						OUT = 8 MHz (SCLK), 2 MHz (CLK2)
  GST SHIFTER	IN = 32 MHz, 8 MHz (external clock SCLK)	OUT = 16 MHz, 8 MHz (FCLK=SCLK)
  GST MCU	IN = 16 MHz					OUT = 8 MHz (CLK8), 4 MHz (CLK4), 500 kHz (KHZ500)
  BUS		= 8 MHz

  CPU 68000	IN = 8 MHz (CLK8)
  DMA		IN = 8 MHz (CLK8)
  DMA AUDIO	IN = 8 MHz (SCLK)
  MFP 68901	IN = 4 MHz (CLK4), 2.4576 MHz (external clock)
  FDC WD1772	IN = 8 MHz (SCLK)
  BLITTER	IN = 8 MHz (CLK8)
  YM2149	IN = 2 MHz (CLK2)
  ACIA MC6850	IN = 500 kHz (KHZ500)
  IKBD HD6301	IN = 1 MHZ (local clock)


MEGA STE :
  MCLK		= 32 MHz
  SCLK		= 8 MHz
  GST SHIFTER	IN = 32 MHz, 8 MHz (external clock SCLK)	OUT = 16 MHz (CLK16), 8 MHz (FCLK=SCLK)
  GST MCU	IN = 16 MHz (CLK16)				OUT = 8 MHz (CLK8), 4 MHz (CLK4), 500 kHz (KHZ500)
  BUS		= 8 MHz

  CPU 68000	IN = 16 MHz (CLK16)
  FPU 68881	IN = 16 MHz (CLK16)
  DMA		IN = 8 MHz (CLK8)
  DMA AUDIO	IN = 8 MHz (SCLK)
  MFP 68901	IN = 4 MHz (CLK4), 2.4576 MHz (external clock)
  FDC WD1772	IN = 8 MHz (SCLK)
  BLITTER	IN = 8 MHz (CLK8)
  YM2149	IN = 2 MHz (CLK2 = SCLK / 4)
  ACIA MC6850	IN = 500 kHz (KHZ500)
  IKBD HD6301	IN = 1 MHZ (local clock)
  SCC Z85C30	IN = 8 MHz (CLK8)


TT :
  MCLK		= 32 MHz (CLK32)
  TT VIDEO	IN = 32 MHz (CLK32)				OUT = 16 MHz (CLK16), 4 MHz (CLK4), 2 MHz (CLK2)
  GST MCU	IN = 16 MHz (CLK16A), 2 MHz (CLK2)		OUT = 8 MHz (CLK8), 8 MHz (FCCLK), 1 MHz (CLKE), 500 kHz (CLKX5)
  BUS		= 16 MHz

  CPU 68030	IN = 32 MHz (CLK32)
  FPU 68882	IN = 32 MHz (CLK32)
  DMA		IN = 8 MHz (CLK8)
  SND SHIFTER	IN = 16 MHz (CLK16F), 2 MHz (CLK2)		OUT = ? MHz (FCLK)
  MFP 68901	IN = 4 MHz (CLK4), 2.4576 MHz (external clock)	NOTE : TT has 2 MFPs 68901
  FDC WD1772	IN = 8 MHz (FCCLK)
  BLITTER	NOT AVAILABLE
  YM2149	IN = 2 MHz (CLK2)
  ACIA MC6850	IN = 500 kHz (CLKX5)
  IKBD HD6301	IN = 1 MHZ (local clock)
  SCC Z85C30	IN = 8 MHz (CLK8)


FALCON :
  MCLK		= 32 MHz (CLK32)
  VIDEL	IN 	= 32 MHz (VID32MHZ), 25 MHz (25K)
  COMBEL	IN = 32 MHz (CLK32)				OUT = 8 MHz (CLK8, not used), 4 MHz (CLK4), 500 kHz (KHZ500)
  BUS		= 16 MHz

  DSP 56001	IN = 32 MHz (DSP_32M)

  DMA		IN = 32 MHz (DSP_32M)				OUT = 8 MHz (CLK8), 16 MHz (FCCLK), 2 MHz (CLK2)

  CPU 68030	IN = 16 MHz (CPUCLKB)
  FPU 68882	IN = 16 MHz (CPUCLKA)
  CODEC		IN = 25 MHz (25K)
  MFP 68901	IN = 4 MHz (CLK4), 2.4576 MHz (external clock)
  FDC AJAX	IN = 16 MHz (FCCLK)
  BLITTER	IN = 16 MHz
  YM3439	IN = 2 MHz (CLK2)
  ACIA MC6850	IN = 500 kHz (KHZ500)
  IKBD HD6301	IN = 1 MHZ (local clock)
  SCC Z85C30	IN = 8 MHz (CLK8)

*/


const char ClocksTimings_fileid[] = "Hatari clocks_timings.c";

#include "main.h"
#include "configuration.h"
#include "log.h"
#include "clocks_timings.h"
#include "m68000.h"



/* The possible master frequencies used in the different machines */
/* depending on PAL/NTSC version. */

#define ATARI_STF_PAL_MCLK		32084988			/* CPU_Freq = 8.021247 MHz */
#define ATARI_STF_NTSC_MCLK		32042400			/* CPU_Freq = 8.010600 MHz */
#define ATARI_STF_CYCLES_PER_VBL_PAL	160256				/* 512 cycles * 313 lines */
#define ATARI_STF_CYCLES_PER_VBL_NTSC	133604				/* 508 cycles * 263 lines */
#define ATARI_STF_CYCLES_PER_VBL_HI	112224				/* 224 cycles * 501 lines */

#define ATARI_STE_PAL_MCLK		32084988			/* CPU_Freq = 8.021247 MHz */
#define ATARI_STE_NTSC_MCLK		32215905			/* CPU_Freq = 8.05397625 MHz */
#define ATARI_STE_EXT_OSC		8010613				/* OSC U303 */
#define ATARI_STE_CYCLES_PER_VBL_PAL	160256				/* 512 cycles * 313 lines */
#define ATARI_STE_CYCLES_PER_VBL_NTSC	133604				/* 508 cycles * 263 lines */
#define ATARI_STE_CYCLES_PER_VBL_HI	112224				/* 224 cycles * 501 lines */

#define ATARI_MEGA_STE_PAL_MCLK		32084988			/* CPU_Freq = 16.042494 MHz */
#define ATARI_MEGA_STE_NTSC_MCLK	32215905			/* CPU_Freq = 16.1079525 MHz */
#define ATARI_MEGA_STE_EXT_OSC		16021226			/* OSC U408 */

#define ATARI_TT_PAL_MCLK		32084988			/* CPU_Freq = 32.084988 MHz */
#define ATARI_TT_NTSC_MCLK		32215905			/* CPU_Freq = 32.215905 MHz */

#define ATARI_FALCON_PAL_MCLK		32084988			/* CPU_Freq = 16.042494 MHz */
#define ATARI_FALCON_NTSC_MCLK		32215905			/* CPU_Freq = 16.1079525 MHz */
#define ATARI_FALCON_25M_CLK		25175000

#define ATARI_MFP_XTAL			2457600				/* external clock for the MFP */
#define ATARI_IKBD_CLK			1000000				/* clock of the HD6301 ikbd cpu */



CLOCKS_STRUCT	MachineClocks;


bool	RoundVBLPerSec = false;						/* if false, don't round number of VBL to 50/60 Hz */
									/* but compute the exact value based on cpu/video clocks */




/*--------------------------------------------------------------------------*/
/**
 * Initialize all the clocks information related to a specific machine type.
 * We consider the machine is running with PAL clocks.
 */

void	ClocksTimings_InitMachine ( MACHINETYPE MachineType )
{
//fprintf ( stderr , "clock init mach=%d shift=%d\n" , MachineType , nCpuFreqShift );
	memset ( (void *)&MachineClocks , 0 , sizeof ( MachineClocks ) );

	if (MachineType == MACHINE_ST || MachineType == MACHINE_MEGA_ST)
	{
		int	CLK16, CLK8, CLK4, CLK2, CLK500;

		MachineClocks.MCLK_Freq		= ATARI_STF_PAL_MCLK;			/* 32.084988 MHz */

		MachineClocks.SHIFTER_Freq	= MachineClocks.MCLK_Freq;		/* 32 MHz */
		CLK16				= MachineClocks.SHIFTER_Freq / 2;

		MachineClocks.MMU_Freq		= CLK16;				/* 16 MHz */
		CLK8				= MachineClocks.MMU_Freq / 2;
		CLK4				= MachineClocks.MMU_Freq / 4;

		MachineClocks.GLUE_Freq		= CLK8;					/* 8 MHz */
		CLK2				= MachineClocks.GLUE_Freq / 4;
		CLK500				= MachineClocks.GLUE_Freq / 16;

		MachineClocks.BUS_Freq		= CLK8;					/* 8 MHz */

		MachineClocks.CPU_Freq		= CLK8;					/* 8 MHz */
		MachineClocks.DMA_Freq		= CLK8;					/* 8 MHz */
		MachineClocks.MFP_Freq		= CLK4;					/* 4 MHz */
		MachineClocks.MFP_Timer_Freq	= ATARI_MFP_XTAL;			/* 2.4576 MHz (XTAL)*/
		MachineClocks.FDC_Freq		= CLK8;					/* 8 MHz */
		MachineClocks.BLITTER_Freq	= CLK8;					/* 8 MHz */
		MachineClocks.YM_Freq		= CLK2;					/* 2 MHz */;
		MachineClocks.ACIA_Freq		= CLK500;				/* 500 kHz */
		MachineClocks.IKBD_Freq		= ATARI_IKBD_CLK;			/* 1 MHz */
	}

	else if ( MachineType == MACHINE_STE )
	{
		int	SCLK, CLK16, CLK8, CLK4, CLK2, KHZ500;
		//int	FCLK;								/* not used (audio filters) */

		MachineClocks.MCLK_Freq		= ATARI_STE_PAL_MCLK;			/* 32.084988 MHz */
		SCLK				= ATARI_STE_EXT_OSC;			/* 8.010613 MHz (SCLK) */
		CLK2				= SCLK / 4;

		MachineClocks.SHIFTER_Freq	= MachineClocks.MCLK_Freq;		/* 32 MHz */
		CLK16				= MachineClocks.SHIFTER_Freq / 2;
		//FCLK				= SCLK;

		MachineClocks.MCU_Freq		= CLK16;				/* 16 MHz */
		CLK8				= MachineClocks.MCU_Freq / 2;
		CLK4				= MachineClocks.MCU_Freq / 4;
		KHZ500				= MachineClocks.MCU_Freq / 32;

		MachineClocks.BUS_Freq		= CLK8;					/* 8 MHz (CLK8) */

		MachineClocks.CPU_Freq		= CLK8;					/* 8 MHz (CLK8) */
		MachineClocks.DMA_Freq		= CLK8;					/* 8 MHz (CLK8) */
		MachineClocks.DMA_Audio_Freq	= SCLK;					/* 8 MHz (SCLK) */
		MachineClocks.MFP_Freq		= CLK4;					/* 4 MHz (CLK4) */
		MachineClocks.MFP_Timer_Freq	= ATARI_MFP_XTAL;			/* 2.4576 MHz (XTAL)*/
		MachineClocks.FDC_Freq		= SCLK;					/* 8 MHz (SCLK) */
		MachineClocks.BLITTER_Freq	= CLK8;					/* 8 MHz (CLK8) */
		MachineClocks.YM_Freq		= CLK2;					/* 2 MHz (CLK2) */
		MachineClocks.ACIA_Freq		= KHZ500;				/* 500 kHz (KHZ500) */
		MachineClocks.IKBD_Freq		= ATARI_IKBD_CLK;			/* 1 MHz */
	}

	else if ( MachineType == MACHINE_MEGA_STE )
	{
		int	SCLK, CLK16, CLK8, CLK4, CLK2, KHZ500;
		//int	FCLK;								/* not used (audio filters) */

		MachineClocks.MCLK_Freq		= ATARI_MEGA_STE_PAL_MCLK;		/* 32.084988 MHz */
		SCLK				= ATARI_MEGA_STE_EXT_OSC / 2;		/* 16.021226 MHz / 2 = 8.010613 MHz */
		CLK2				= SCLK / 4;

		MachineClocks.SHIFTER_Freq	= MachineClocks.MCLK_Freq;		/* 32 MHz */
		CLK16				= MachineClocks.SHIFTER_Freq / 2;
		//FCLK				= SCLK;

		MachineClocks.MCU_Freq		= CLK16;				/* 16 MHz (CLK16) */
		CLK8				= MachineClocks.MCU_Freq / 2;
		CLK4				= MachineClocks.MCU_Freq / 4;
		KHZ500				= MachineClocks.MCU_Freq / 32;

		MachineClocks.BUS_Freq		= CLK8;					/* 8 MHz (CLK8) */

		/* Special case : the Mega STE has an internal 16 MHz CPU clock */
		/* but it can be set to 8 MHz for compatibility with STE using $FF8E21 */
		MachineClocks.CPU_Freq		= CLK16;				/* 16 MHz (CLK16) */
		MachineClocks.FPU_Freq		= CLK16;				/* 16 MHz (CLK16) */
		MachineClocks.DMA_Freq		= CLK8;					/* 8 MHz (CLK8) */
		MachineClocks.DMA_Audio_Freq	= SCLK;					/* 8 MHz (SCLK) */
		MachineClocks.MFP_Freq		= CLK4;					/* 4 MHz (CLK4) */
		MachineClocks.MFP_Timer_Freq	= ATARI_MFP_XTAL;			/* 2.4576 MHz (XTAL)*/
		MachineClocks.FDC_Freq		= SCLK;					/* 8 MHz (SCLK) */
		MachineClocks.BLITTER_Freq	= CLK8;					/* 8 MHz (CLK8) */
		MachineClocks.YM_Freq		= CLK2;					/* 2 MHz (CLK2) */
		MachineClocks.ACIA_Freq		= KHZ500;				/* 500 kHz (KHZ500) */
		MachineClocks.IKBD_Freq		= ATARI_IKBD_CLK;			/* 1 MHz */
		MachineClocks.SCC_Freq		= CLK8;					/* 8 MHz (CLK8) */
	}

	else if ( MachineType == MACHINE_TT )
	{
		int	CLK32, CLK16, CLK8, FCCLK, CLK4, CLK2, CLKX5;

		MachineClocks.MCLK_Freq		= ATARI_TT_PAL_MCLK;			/* 32.084988 MHz */
		CLK32				= MachineClocks.MCLK_Freq;

		MachineClocks.TTVIDEO_Freq	= MachineClocks.MCLK_Freq;		/* 32 MHz */
		CLK16				= MachineClocks.TTVIDEO_Freq / 2;
		CLK4				= MachineClocks.TTVIDEO_Freq / 8;
		CLK2				= MachineClocks.TTVIDEO_Freq / 16;

		MachineClocks.MCU_Freq		= CLK16;				/* 16 MHz (CLK16A) */
		CLK8				= MachineClocks.MCU_Freq / 2;
		FCCLK				= MachineClocks.MCU_Freq / 2;
		CLKX5				= MachineClocks.MCU_Freq / 32;

		MachineClocks.BUS_Freq		= CLK16;				/* 16 MHz (CLK16) */

		MachineClocks.CPU_Freq		= CLK32;				/* 32 MHz (CLK32) */
		MachineClocks.FPU_Freq		= CLK32;				/* 32 MHz (CLK32) */
		MachineClocks.DMA_Freq		= CLK8;					/* 8 MHz (CLK8) */
		MachineClocks.DMA_Audio_Freq	= CLK16;				/* 16 MHz (CLK16) SND SHIFTER */
		MachineClocks.MFP_Freq		= CLK4;					/* 4 MHz (CLK4) */
		MachineClocks.MFP_Timer_Freq	= ATARI_MFP_XTAL;			/* 2.4576 MHz (XTAL)*/
		MachineClocks.FDC_Freq		= FCCLK;				/* 8 MHz (FCCLK) */
		MachineClocks.BLITTER_Freq	= 0;					/* No blitter in TT */
		MachineClocks.YM_Freq		= CLK2;					/* 2 MHz (CLK2) */
		MachineClocks.ACIA_Freq		= CLKX5;				/* 500 kHz (CLKX5) */
		MachineClocks.IKBD_Freq		= ATARI_IKBD_CLK;			/* 1 MHz */
		MachineClocks.SCC_Freq		= CLK8;					/* 8 MHz (CLK8) */
	}

	else if ( MachineType == MACHINE_FALCON )
	{
		/* TODO : need more docs for Falcon's clocks */
		/* Note : Some clocks are made from 32 MHz MCLK, but others are made from */
		/* 32 MHz DSP's clock */
		/* CLK32 and VID32MHZ are coming from the same clock 32MHZ (32.084988 MHz for PAL) */
		int	CLK32, CLK25, CLK16, CLK8, FCCLK, CLK4, CLK2, KHZ500;

		MachineClocks.MCLK_Freq		= ATARI_FALCON_PAL_MCLK;		/* 32.084988 MHz */
		CLK32				= MachineClocks.MCLK_Freq;
		CLK25				= ATARI_FALCON_25M_CLK;
		CLK16				= CLK32 / 2;

		MachineClocks.DSP_Freq		= CLK32;				/* 32 MHz */
		CLK8				= MachineClocks.DSP_Freq / 4;
		CLK2				= MachineClocks.DSP_Freq / 16;
		FCCLK				= MachineClocks.DSP_Freq / 2;

		MachineClocks.VIDEL_Freq	= CLK32;				/* 32 MHz (VID32MHZ) */

		MachineClocks.COMBEL_Freq	= CLK32;				/* 32 MHz */
		CLK4				= MachineClocks.COMBEL_Freq / 8;
		KHZ500				= MachineClocks.COMBEL_Freq / 64;

		MachineClocks.BUS_Freq		= CLK16;				/* 16 MHz (CPUCLK16A) */
		MachineClocks.CPU_Freq		= CLK16;				/* 16 MHz (CPUCLK16B) */
		MachineClocks.FPU_Freq		= CLK16;				/* 16 MHz (CLK32) */
		MachineClocks.DMA_Freq		= CLK16;				/* 16 MHz (CLK16) ? */
		MachineClocks.CODEC_Freq	= CLK25;				/* 25 MHz (CLK25) */
		MachineClocks.MFP_Freq		= CLK4;					/* 4 MHz (CLK4) */
		MachineClocks.MFP_Timer_Freq	= ATARI_MFP_XTAL;			/* 2.4576 MHz (XTAL)*/
		MachineClocks.FDC_Freq		= FCCLK;				/* 16 MHz (FCCLK) ? */
		MachineClocks.BLITTER_Freq	= CLK16;				/* 16 MHz */
		MachineClocks.YM_Freq		= CLK2;					/* 2 MHz (CLK2) */
		MachineClocks.ACIA_Freq		= KHZ500;				/* 500 kHz (KHZ500) */
		MachineClocks.IKBD_Freq		= ATARI_IKBD_CLK;			/* 1 MHz */
		MachineClocks.SCC_Freq		= CLK8;					/* 8 MHz (CLK8) */
	}


	/* Update some other variables depending on the current nCpuFreqShift */
	ClocksTimings_UpdateCpuFreqEmul ( MachineType , nCpuFreqShift );
}




/*-----------------------------------------------------------------------------------------*/
/**
 * Update the number of emulated cycles per second for the current CPU settings, depending on the
 * base CPU freq in MachineClocks.CPU_Freq and nCpuFreqShift
 *
 * We use CPU_Freq as a base (instead of fixed values of 8, 16 or 32 MHz) to handle different CPU
 * clocks in case the machine is a PAL or NTSC model for example (in which cases CPU_Freq
 * values are slightly different)
 */
void	ClocksTimings_UpdateCpuFreqEmul ( MACHINETYPE MachineType , int nCpuFreqShift )
{
	uint32_t Cpu_Freq_Emul;

	Cpu_Freq_Emul = MachineClocks.CPU_Freq;

	/* Machines where the base CPU is 8 MHz */
	if (MachineType == MACHINE_ST || MachineType == MACHINE_MEGA_ST
	    || MachineType == MACHINE_STE)
	{
		Cpu_Freq_Emul <<= nCpuFreqShift;			/* 8, 16 or 32 MHz */
	}

	/* Machines where the base CPU is 16 MHz */
	else if (MachineType == MACHINE_MEGA_STE || MachineType == MACHINE_FALCON )
	{
		if ( nCpuFreqShift == 0 )	Cpu_Freq_Emul >>= 1;	/*  8 MHz */
		else if ( nCpuFreqShift == 2 )	Cpu_Freq_Emul <<= 1;	/* 32 MHz */
	}

	/* Machines where the base CPU is 32 MHz */
	else if (MachineType == MACHINE_TT )
	{
		if ( nCpuFreqShift == 0 )	Cpu_Freq_Emul >>= 2;	/*  8 MHz */
		else if ( nCpuFreqShift == 1 )	Cpu_Freq_Emul >>= 1;	/* 16 MHz */
	}

	MachineClocks.CPU_Freq_Emul = Cpu_Freq_Emul;
//fprintf ( stderr , "clock cpu freq mach=%d shift=%d base=%d -> %d\n" , MachineType , nCpuFreqShift , MachineClocks.CPU_Freq , MachineClocks.CPU_Freq_Emul );
}




/*-----------------------------------------------------------------------------------------*/
/**
 * Return the number of cycles per VBL, depending on the video settings and the simulated cpu freq.
 * This value is only precisely known for STF/STE running at 50, 60 or 71 Hz.
 * For the other machines, we return CPU_Freq_Emul / ScreenRefreshRate
 */
uint32_t	ClocksTimings_GetCyclesPerVBL ( MACHINETYPE MachineType , int ScreenRefreshRate )
{
	uint32_t	CyclesPerVBL;

	/* STF and STE have the same numbers of cycles per VBL (numbers are for an 8 MHz CPU) */
	if (MachineType == MACHINE_ST || MachineType == MACHINE_MEGA_ST
	    || MachineType == MACHINE_STE || MachineType == MACHINE_MEGA_STE)
	{
		if ( ScreenRefreshRate == 50 )
			CyclesPerVBL = ATARI_STF_CYCLES_PER_VBL_PAL;
		else if ( ScreenRefreshRate == 60 )
			CyclesPerVBL = ATARI_STF_CYCLES_PER_VBL_NTSC;
		else if ( ScreenRefreshRate == 71 )
			CyclesPerVBL = ATARI_STF_CYCLES_PER_VBL_HI;
		else
			CyclesPerVBL = MachineClocks.CPU_Freq / ScreenRefreshRate;	/* unknown refresh rate, should not happen */

		/* At this point CyclesPerVBL is the number of cycles per VBL for a 8 MHz CPU */
		/* We need to apply nCpuFreqShift to get the number of cycles at the currently simulated CPU speed (8, 16 or 32 MHz) */
		CyclesPerVBL <<= nCpuFreqShift;
	}

	else if (MachineType == MACHINE_FALCON)
	{
		/* Although our Videl emulation is not complete, it already supports 50, 60 or 71 Hz */
		/* for ScreenRefreshRate. We use this value to get the number of cycles per VBL */
		CyclesPerVBL = MachineClocks.CPU_Freq_Emul / ScreenRefreshRate;
	}

	/* For machines where cpu freq can be changed, the number of cycles per VBL is not constant */
	else										/* MACHINE_TT TODO : use ATARI_STF_CYCLES_PER_VBL_xxx too ?*/
		CyclesPerVBL = MachineClocks.CPU_Freq_Emul / ScreenRefreshRate;

//fprintf ( stderr , "clock cycles per vbl %d %d -> %d\n" , MachineType , ScreenRefreshRate , CyclesPerVBL );
	return CyclesPerVBL;
}




/*-----------------------------------------------------------------------------------------*/
/**
 * Return the number of VBL per second, depending on the video settings and the simulated cpu freq.
 * Since the cpu freq is not an exact multiple of the number of cycles per VBL, the real
 * value slightly differs from the usual 50/60 Hz.
 * Precise (not rounded) values are needed in STE mode to synchronize cpu and dma sound (as they both use
 * 2 different clocks).
 * example for STF/STE :
 *	PAL  STF/STE video PAL :	50.053 VBL/sec
 *	PAL  STF/STE video NTSC :	60.037 VBL/sec
 *	NTSC STF/STE video PAL :	49.986 VBL/sec
 *	NTSC STF/STE video NTSC :	59.958 VBL/sec
 *
 * The returned number of VBL per sec is << 24 (=CLOCKS_TIMINGS_SHIFT_VBL) to simulate floating point using uint32_t.
 */
uint32_t	ClocksTimings_GetVBLPerSec ( MACHINETYPE MachineType , int ScreenRefreshRate )
{
	uint32_t	VBLPerSec;							/* Upper 8 bits are for int part, 24 lower bits for float part */


	if ( RoundVBLPerSec == true )
	{
		VBLPerSec = ScreenRefreshRate << CLOCKS_TIMINGS_SHIFT_VBL;
	}

	else
	{
		VBLPerSec = ( (int64_t)MachineClocks.CPU_Freq_Emul << CLOCKS_TIMINGS_SHIFT_VBL ) / ClocksTimings_GetCyclesPerVBL ( MachineType , ScreenRefreshRate );
	}

//fprintf ( stderr , "clock vbl per sec %d %d %d -> %d\n" , MachineType , MachineClocks.CPU_Freq_Emul , ScreenRefreshRate , VBLPerSec );
	return VBLPerSec;
}




/*-----------------------------------------------------------------------------------------*/
/**
 * Return the length in microsec of a VBL (opposite function of ClocksTimings_GetVBLPerSec)
 * We use precise values only in STF/STE mode, else we use 1000000 / ScreenRefreshRate.
 * example for STF/STE :
 *	PAL  STF/STE video PAL :	19979 micro sec  (instead of 20000 for 50 Hz)
 *	PAL  STF/STE video NTSC :	16656 micro sec  (instead of 16667 for 60 Hz)
 */
uint32_t	ClocksTimings_GetVBLDuration_micro ( MACHINETYPE MachineType , int ScreenRefreshRate )
{
	uint32_t	VBLDuration_micro;

	if ( RoundVBLPerSec == true )
	{
		VBLDuration_micro = (uint32_t) (1000000.0 / ScreenRefreshRate + 0.5);
	}

	else
	{
		VBLDuration_micro = (uint32_t) (1000000.0 * ClocksTimings_GetCyclesPerVBL ( MachineType , ScreenRefreshRate ) / MachineClocks.CPU_Freq_Emul + 0.5);
	}

//fprintf ( stderr , "clock vbl duration %d %d %d -> %d\n" , MachineType , MachineClocks.CPU_Freq_Emul , ScreenRefreshRate , VBLDuration_micro );
	return VBLDuration_micro;
}




/*-----------------------------------------------------------------------------------------*/
/**
 * Return the number of samples needed to emulate the sound that was produced during one VBL.
 * This depends on the chosen audio output frequency, as well as the VBL's duration,
 *
 * We use precise values only in STF/STE mode, else we use AudioFreq/ScreenRefreshRate.
 *
 * The returned number of samples per VBL is << 28 to simulate maximum precision using
 * 64 bits integers (lower 28 bits are for the floating point part).
 * example for STF/STE with emulation's audio freq = 44100 :
 *	PAL  STF/STE video PAL :	881.07 samples per VBL  (instead of 882 for 50 Hz)
 *					44053.56 samples for 50 VBLs (instead of 44100 for 1 sec at 50 Hz)
 */
int64_t	ClocksTimings_GetSamplesPerVBL ( MACHINETYPE MachineType , int ScreenRefreshRate , int AudioFreq )
{
	int64_t	SamplesPerVBL;

	if ( RoundVBLPerSec == true )
	{
		SamplesPerVBL = ( ((int64_t)AudioFreq) << 28 ) / ScreenRefreshRate;
	}

	else
	{
		SamplesPerVBL = ( ((int64_t)AudioFreq * ClocksTimings_GetCyclesPerVBL ( MachineType , ScreenRefreshRate ) ) << 28 ) / MachineClocks.CPU_Freq_Emul;
	}

//fprintf ( stderr , "clock sample per vbl %d %d %d -> %ld\n" , MachineType , MachineClocks.CPU_Freq_Emul , AudioFreq , SamplesPerVBL );
	return SamplesPerVBL;
}




/*-----------------------------------------------------------------------------------------*/
/**
 * Convert a number of cycles "CyclesIn" for a clock running at "ClockFreqIn" into
 * an equivalent number of cycles for a clock running at "ClockFreqOut".
 * As clocks are rarely multiple of each other, this will give a remainder that must be kept
 * and used on next call.
 * This method uses only integer operations (integer division and modulo) and gives much more
 * precise results than using floating point, because there's no roundings that accumulate
 * after a while.
 */
void	ClocksTimings_ConvertCycles ( uint64_t CyclesIn , uint64_t ClockFreqIn , CLOCKS_CYCLES_STRUCT *CyclesStructOut , uint64_t ClockFreqOut )
{
	uint64_t	CyclesTotal;
	uint64_t	CyclesOut;
	uint64_t	CyclesOut_remainder;

	CyclesTotal = CyclesIn * ClockFreqOut;
	CyclesTotal += CyclesStructOut->Remainder;

	CyclesOut = CyclesTotal / ClockFreqIn;
	CyclesOut_remainder = CyclesTotal % ClockFreqIn;

	CyclesStructOut->Cycles = CyclesOut;
	CyclesStructOut->Remainder = CyclesOut_remainder;
}
