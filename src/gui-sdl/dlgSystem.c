/*
  Hatari - dlgSystem.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
  
  This file contains 2 system panels :
    - 1 for the old uae CPU
    - 1 for the new WinUae cpu
    
  The selection is made during compilation with the ENABLE_WINUAE_CPU define

*/
const char DlgSystem_fileid[] = "Hatari dlgSystem.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


/* The old Uae cpu "System" dialog: */
#ifndef ENABLE_WINUAE_CPU

#define DLGSYS_68000    4
#define DLGSYS_68010    5
#define DLGSYS_68020    6
#define DLGSYS_68030    7
#define DLGSYS_68040    8
#define DLGSYS_ST       11
#define DLGSYS_STE      12
#define DLGSYS_TT       13
#define DLGSYS_FALCON   14
#define DLGSYS_8MHZ     17
#define DLGSYS_16MHZ    18
#define DLGSYS_32MHZ    19
#define DLGSYS_DSPOFF   21
#define DLGSYS_DSPDUMMY 22
#define DLGSYS_DSPON    23
#define DLGSYS_PREFETCH 24
#define DLGSYS_BLITTER  25
#define DLGSYS_RTC      26
#define DLGSYS_TIMERD   27
#define DLGSYS_FASTBOOT 28

static SGOBJ systemdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 36,25, NULL },
	{ SGTEXT, 0, 0, 11,1, 14,1, "System options" },

	{ SGBOX, 0, 0, 2,3, 16,9, NULL },
	{ SGTEXT, 0, 0, 3,4, 8,1, "CPU type:" },
	{ SGRADIOBUT, 0, 0, 4,6, 7,1, "68000" },
	{ SGRADIOBUT, 0, 0, 4,7, 7,1, "68010" },
	{ SGRADIOBUT, 0, 0, 4,8, 7,1, "68020" },
	{ SGRADIOBUT, 0, 0, 4,9, 13,1, "68EC030+FPU" },
	{ SGRADIOBUT, 0, 0, 4,10, 7,1, "68040" },

	{ SGBOX, 0, 0, 19,3, 15,9, NULL },
	{ SGTEXT, 0, 0, 20,4, 13,1, "Machine type:" },
	{ SGRADIOBUT, 0, 0, 23,6, 8,1, "ST" },
	{ SGRADIOBUT, 0, 0, 23,7, 8,1, "STE" },
	{ SGRADIOBUT, 0, 0, 23,8, 8,1, "TT *" },
	{ SGRADIOBUT, 0, 0, 23,9, 8,1, "Falcon *" },
	{ SGTEXT, 0, 0, 21,11, 12,1, "* incomplete" },

	{ SGTEXT, 0, 0, 2,13, 15,1, "CPU clock (MHz):" },
	{ SGRADIOBUT, 0, 0, 19,13, 3,1, "8" },
	{ SGRADIOBUT, 0, 0, 24,13, 4,1, "16" },
	{ SGRADIOBUT, 0, 0, 30,13, 4,1, "32" },

	{ SGTEXT, 0, 0, 2,15, 11,1, "Falcon DSP:" },
	{ SGRADIOBUT, 0, 0, 14,15, 5,1, "off" },
	{ SGRADIOBUT, 0, 0, 21,15, 7,1, "dummy" },
	{ SGRADIOBUT, 0, 0, 30,15, 4,1, "on" },

	{ SGCHECKBOX, 0, 0, 2,17, 32,1, "Slower but more compatible CPU" },
	{ SGCHECKBOX, 0, 0, 2,18, 20,1, "Blitter emulation" },
	{ SGCHECKBOX, 0, 0, 2,19, 27,1, "Real time clock emulation" },
	{ SGCHECKBOX, 0, 0, 2,20, 15,1, "Patch Timer-D" },
	{ SGCHECKBOX, 0, 0, 2,21, 27,1, "Patch TOS for faster boot" },

	{ SGBUTTON, SG_DEFAULT, 0, 8,23, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "System" dialog.
 */
void Dialog_SystemDlg(void)
{
	int i;
	MACHINETYPE	mti;

	SDLGui_CenterDlg(systemdlg);

	/* Set up dialog from actual values: */

	for (i = DLGSYS_68000; i <= DLGSYS_68040; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	systemdlg[DLGSYS_68000+ConfigureParams.System.nCpuLevel].state |= SG_SELECTED;

	for (i = DLGSYS_ST; i <= DLGSYS_FALCON; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	systemdlg[DLGSYS_ST + ConfigureParams.System.nMachineType].state |= SG_SELECTED;

	/* CPU frequency: */
	for (i = DLGSYS_8MHZ; i <= DLGSYS_16MHZ; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	if (ConfigureParams.System.nCpuFreq == 32)
		systemdlg[DLGSYS_32MHZ].state |= SG_SELECTED;
	else if (ConfigureParams.System.nCpuFreq == 16)
		systemdlg[DLGSYS_16MHZ].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_8MHZ].state |= SG_SELECTED;

	/* DSP mode: */
	for (i = DLGSYS_DSPOFF; i <= DLGSYS_DSPON; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	if (ConfigureParams.System.nDSPType == DSP_TYPE_NONE)
		systemdlg[DLGSYS_DSPOFF].state |= SG_SELECTED;
	else if (ConfigureParams.System.nDSPType == DSP_TYPE_DUMMY)
		systemdlg[DLGSYS_DSPDUMMY].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_DSPON].state |= SG_SELECTED;


	if (ConfigureParams.System.bCompatibleCpu)
		systemdlg[DLGSYS_PREFETCH].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_PREFETCH].state &= ~SG_SELECTED;

	if (ConfigureParams.System.bBlitter)
		systemdlg[DLGSYS_BLITTER].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_BLITTER].state &= ~SG_SELECTED;

	if (ConfigureParams.System.bRealTimeClock)
		systemdlg[DLGSYS_RTC].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_RTC].state &= ~SG_SELECTED;

	if (ConfigureParams.System.bPatchTimerD)
		systemdlg[DLGSYS_TIMERD].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_TIMERD].state &= ~SG_SELECTED;

	if (ConfigureParams.System.bFastBoot)
		systemdlg[DLGSYS_FASTBOOT].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_FASTBOOT].state &= ~SG_SELECTED;

	/* Show the dialog: */
	SDLGui_DoDialog(systemdlg, NULL);

	/* Read values from dialog: */

	for (i = DLGSYS_68000; i <= DLGSYS_68040; i++)
	{
		if (systemdlg[i].state&SG_SELECTED)
		{
			ConfigureParams.System.nCpuLevel = i-DLGSYS_68000;
			break;
		}
	}

	for (mti = MACHINE_ST; mti <= MACHINE_FALCON; mti++)
	{
		if (systemdlg[mti + DLGSYS_ST].state&SG_SELECTED)
		{
			ConfigureParams.System.nMachineType = mti;
			break;
		}
	}

	if (systemdlg[DLGSYS_32MHZ].state & SG_SELECTED)
		ConfigureParams.System.nCpuFreq = 32;
	else if (systemdlg[DLGSYS_16MHZ].state & SG_SELECTED)
		ConfigureParams.System.nCpuFreq = 16;
	else
		ConfigureParams.System.nCpuFreq = 8;

	if (systemdlg[DLGSYS_DSPOFF].state & SG_SELECTED)
		ConfigureParams.System.nDSPType = DSP_TYPE_NONE;
	else if (systemdlg[DLGSYS_DSPDUMMY].state & SG_SELECTED)
		ConfigureParams.System.nDSPType = DSP_TYPE_DUMMY;
	else
		ConfigureParams.System.nDSPType = DSP_TYPE_EMU;

	ConfigureParams.System.bCompatibleCpu = (systemdlg[DLGSYS_PREFETCH].state & SG_SELECTED);
	ConfigureParams.System.bBlitter = (systemdlg[DLGSYS_BLITTER].state & SG_SELECTED);
	ConfigureParams.System.bRealTimeClock = (systemdlg[DLGSYS_RTC].state & SG_SELECTED);
	ConfigureParams.System.bPatchTimerD = (systemdlg[DLGSYS_TIMERD].state & SG_SELECTED);
	ConfigureParams.System.bFastBoot = (systemdlg[DLGSYS_FASTBOOT].state & SG_SELECTED);
}

/* The new WinUae cpu "System" dialog: */
#else

#define DLGSYS_68000      4
#define DLGSYS_68010      5
#define DLGSYS_68020      6
#define DLGSYS_68030      7
#define DLGSYS_68040      8
#define DLGSYS_68060      9
#define DLGSYS_ST         12
#define DLGSYS_STE        13
#define DLGSYS_TT         14
#define DLGSYS_FALCON     15
#define DLGSYS_8MHZ       18
#define DLGSYS_16MHZ      19
#define DLGSYS_32MHZ      20
#define DLGSYS_DSPOFF     23
#define DLGSYS_DSPDUMMY   24
#define DLGSYS_DSPON      25
#define DLGSYS_24BITS     28
#define DLGSYS_PREFETCH   29
#define DLGSYS_CYC_EXACT  30
#define DLGSYS_RTC        31
#define DLGSYS_TIMERD     32
#define DLGSYS_BLITTER    33
#define DLGSYS_MMU_EMUL   34
#define DLGSYS_FPU_NONE   37
#define DLGSYS_FPU_68881  38
#define DLGSYS_FPU_68882  39
#define DLGSYS_FPU_CPU_IN 40
#define DLGSYS_FPU_COMPAT 41


static SGOBJ systemdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 60,25, NULL },
	{ SGTEXT, 0, 0, 23,1, 14,1, "System options" },

	{ SGBOX, 0, 0, 19,3, 11,9, NULL },
	{ SGTEXT, 0, 0, 20,3, 8,1, "CPU type" },
	{ SGRADIOBUT, 0, 0, 20,5, 7,1, "68000" },
	{ SGRADIOBUT, 0, 0, 20,6, 7,1, "68010" },
	{ SGRADIOBUT, 0, 0, 20,7, 7,1, "68020" },
	{ SGRADIOBUT, 0, 0, 20,8, 13,1, "68030" },
	{ SGRADIOBUT, 0, 0, 20,9, 13,1, "68040" },
	{ SGRADIOBUT, 0, 0, 20,10, 7,1, "68060" },

	{ SGBOX, 0, 0, 2,3, 15,9, NULL },
	{ SGTEXT, 0, 0, 3,3, 13,1, "Machine type" },
	{ SGRADIOBUT, 0, 0, 3,5, 8,1, "ST" },
	{ SGRADIOBUT, 0, 0, 3,6, 8,1, "STE" },
	{ SGRADIOBUT, 0, 0, 3,7, 8,1, "TT" },
	{ SGRADIOBUT, 0, 0, 3,8, 8,1, "Falcon" },

	{ SGBOX, 0, 0, 32,3, 12,9, NULL },
	{ SGTEXT, 0, 0, 33,3, 15,1, "CPU clock" },
	{ SGRADIOBUT, 0, 0, 33,5, 8,1, " 8 Mhz" },
	{ SGRADIOBUT, 0, 0, 33,6, 8,1, "16 Mhz" },
	{ SGRADIOBUT, 0, 0, 33,7, 8,1, "32 Mhz" },

	{ SGBOX, 0, 0, 46,3, 12,9, NULL },
	{ SGTEXT, 0, 0, 47,3, 11,1, "Falcon DSP" },
	{ SGRADIOBUT, 0, 0, 47,5, 6,1, "None" },
	{ SGRADIOBUT, 0, 0, 47,6, 7,1, "Dummy" },
	{ SGRADIOBUT, 0, 0, 47,7, 6,1, "Full" },

	{ SGBOX, 0, 0, 2,13, 28,9, NULL },
	{ SGTEXT, 0, 0, 3,13, 11,1, "CPU Pamameters" },
	{ SGCHECKBOX, 0, 0, 3,15, 15,1, "24 bits addressing" },
	{ SGCHECKBOX, 0, 0, 3,16, 32,1, "Prefetch mode, slower" },
	{ SGCHECKBOX, 0, 0, 3,17, 32,1, "Cycle exact,   slower" },
	{ SGCHECKBOX, 0, 0, 3,18, 27,1, "Real time clock emulation" },
	{ SGCHECKBOX, 0, 0, 3,19, 15,1, "Patch Timer-D" },
	{ SGCHECKBOX, 0, 0, 3,20, 20,1, "Blitter emulation" },
	{ SGCHECKBOX, 0, 0, 3,21, 15,1, "MMU emulation" },

	{ SGBOX, 0, 0, 32,13, 26,9, NULL },
	{ SGTEXT, 0, 0, 33,13, 11,1, "FPU" },
	{ SGRADIOBUT, 0, 0, 33,15, 6,1, "None" },
	{ SGRADIOBUT, 0, 0, 33,16, 7,1, "68881" },
	{ SGRADIOBUT, 0, 0, 33,17, 7,1, "68882" },
	{ SGRADIOBUT, 0, 0, 33,18, 14,1, "CPU internal" },
	{ SGCHECKBOX, 0, 0, 33,20, 25,1, "More compatible, slower" },

	{ SGBUTTON, SG_DEFAULT, 0, 21,23, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "System" dialog (specific to winUAE cpu).
 */
void Dialog_SystemDlg(void)
{
	int i;
	MACHINETYPE	mti;

	SDLGui_CenterDlg(systemdlg);

	/* Set up dialog from actual values: */

	for (i = DLGSYS_68000; i <= DLGSYS_68060; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	systemdlg[DLGSYS_68000+ConfigureParams.System.nCpuLevel].state |= SG_SELECTED;

	for (i = DLGSYS_ST; i <= DLGSYS_FALCON; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	systemdlg[DLGSYS_ST + ConfigureParams.System.nMachineType].state |= SG_SELECTED;

	/* CPU frequency: */
	for (i = DLGSYS_8MHZ; i <= DLGSYS_16MHZ; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	if (ConfigureParams.System.nCpuFreq == 32)
		systemdlg[DLGSYS_32MHZ].state |= SG_SELECTED;
	else if (ConfigureParams.System.nCpuFreq == 16)
		systemdlg[DLGSYS_16MHZ].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_8MHZ].state |= SG_SELECTED;

	/* DSP mode: */
	for (i = DLGSYS_DSPOFF; i <= DLGSYS_DSPON; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	if (ConfigureParams.System.nDSPType == DSP_TYPE_NONE)
		systemdlg[DLGSYS_DSPOFF].state |= SG_SELECTED;
	else if (ConfigureParams.System.nDSPType == DSP_TYPE_DUMMY)
		systemdlg[DLGSYS_DSPDUMMY].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_DSPON].state |= SG_SELECTED;

	/* More compatible CPU, Prefetch mode */
	if (ConfigureParams.System.bCompatibleCpu)
		systemdlg[DLGSYS_PREFETCH].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_PREFETCH].state &= ~SG_SELECTED;

	/* Emulate Blitter */
	if (ConfigureParams.System.bBlitter)
		systemdlg[DLGSYS_BLITTER].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_BLITTER].state &= ~SG_SELECTED;

	/* Real time clock CPU */
	if (ConfigureParams.System.bRealTimeClock)
		systemdlg[DLGSYS_RTC].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_RTC].state &= ~SG_SELECTED;

	/* Patch timer D */
	if (ConfigureParams.System.bPatchTimerD)
		systemdlg[DLGSYS_TIMERD].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_TIMERD].state &= ~SG_SELECTED;

	/* Address space 24 bits */
	if (ConfigureParams.System.bAddressSpace24)
		systemdlg[DLGSYS_24BITS].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_24BITS].state &= ~SG_SELECTED;
		
	/* Cycle exact CPU */
	if (ConfigureParams.System.bCycleExactCpu)
		systemdlg[DLGSYS_CYC_EXACT].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_CYC_EXACT].state &= ~SG_SELECTED;

	/* FPU emulation */
	for (i = DLGSYS_FPU_NONE; i <= DLGSYS_FPU_CPU_IN; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	if (ConfigureParams.System.n_FPUType == FPU_NONE)
		systemdlg[DLGSYS_FPU_NONE].state |= SG_SELECTED;
	else if (ConfigureParams.System.n_FPUType == FPU_68881)
		systemdlg[DLGSYS_FPU_68881].state |= SG_SELECTED;
	else if (ConfigureParams.System.n_FPUType == FPU_68882)
		systemdlg[DLGSYS_FPU_68882].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_FPU_CPU_IN].state |= SG_SELECTED;

	/* More compatible FPU */
	if (ConfigureParams.System.bCompatibleFPU)
		systemdlg[DLGSYS_FPU_COMPAT].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_FPU_COMPAT].state &= ~SG_SELECTED;

	/* MMU Emulation */
	if (ConfigureParams.System.bMMU)
		systemdlg[DLGSYS_MMU_EMUL].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_MMU_EMUL].state &= ~SG_SELECTED;

		
	/* Show the dialog: */
	SDLGui_DoDialog(systemdlg, NULL);

	/* Read values from dialog: */

	for (i = DLGSYS_68000; i <= DLGSYS_68060; i++)
	{
		if (systemdlg[i].state&SG_SELECTED)
		{
			ConfigureParams.System.nCpuLevel = i-DLGSYS_68000;
			break;
		}
	}

	for (mti = MACHINE_ST; mti <= MACHINE_FALCON; mti++)
	{
		if (systemdlg[mti + DLGSYS_ST].state&SG_SELECTED)
		{
			ConfigureParams.System.nMachineType = mti;
			break;
		}
	}

	if (systemdlg[DLGSYS_32MHZ].state & SG_SELECTED)
		ConfigureParams.System.nCpuFreq = 32;
	else if (systemdlg[DLGSYS_16MHZ].state & SG_SELECTED)
		ConfigureParams.System.nCpuFreq = 16;
	else
		ConfigureParams.System.nCpuFreq = 8;

	if (systemdlg[DLGSYS_DSPOFF].state & SG_SELECTED)
		ConfigureParams.System.nDSPType = DSP_TYPE_NONE;
	else if (systemdlg[DLGSYS_DSPDUMMY].state & SG_SELECTED)
		ConfigureParams.System.nDSPType = DSP_TYPE_DUMMY;
	else
		ConfigureParams.System.nDSPType = DSP_TYPE_EMU;

	ConfigureParams.System.bCompatibleCpu = (systemdlg[DLGSYS_PREFETCH].state & SG_SELECTED);
	ConfigureParams.System.bBlitter = (systemdlg[DLGSYS_BLITTER].state & SG_SELECTED);
	ConfigureParams.System.bRealTimeClock = (systemdlg[DLGSYS_RTC].state & SG_SELECTED);
	ConfigureParams.System.bPatchTimerD = (systemdlg[DLGSYS_TIMERD].state & SG_SELECTED);
	ConfigureParams.System.bAddressSpace24 = (systemdlg[DLGSYS_24BITS].state & SG_SELECTED);
	ConfigureParams.System.bCycleExactCpu = (systemdlg[DLGSYS_CYC_EXACT].state & SG_SELECTED);

	/* FPU emulation */
	if (systemdlg[DLGSYS_FPU_NONE].state & SG_SELECTED)
		ConfigureParams.System.n_FPUType = FPU_NONE;
	else if (systemdlg[DLGSYS_FPU_68881].state & SG_SELECTED)
		ConfigureParams.System.n_FPUType = FPU_68881;
	else if (systemdlg[DLGSYS_FPU_68882].state & SG_SELECTED)
		ConfigureParams.System.n_FPUType = FPU_68882;
	else
		ConfigureParams.System.n_FPUType = FPU_CPU;

	ConfigureParams.System.bCompatibleFPU = (systemdlg[DLGSYS_FPU_COMPAT].state & SG_SELECTED);
	ConfigureParams.System.bMMU = (systemdlg[DLGSYS_MMU_EMUL].state & SG_SELECTED);
}
#endif
