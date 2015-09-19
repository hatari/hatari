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


#define DLGSYS_ST          4
#define DLGSYS_STE         5
#define DLGSYS_TT          6
#define DLGSYS_FALCON      7
#define DLGSYS_68000      10
#define DLGSYS_68010      11
#define DLGSYS_68020      12
#define DLGSYS_68030      13
#define DLGSYS_68040      14
#define DLGSYS_68060      15
#define DLGSYS_8MHZ       18
#define DLGSYS_16MHZ      19
#define DLGSYS_32MHZ      20
#define DLGSYS_DSPOFF     23
#define DLGSYS_DSPDUMMY   24
#define DLGSYS_DSPON      25
#define DLGSYS_RTC        28
#define DLGSYS_BLITTER    29
#define DLGSYS_TIMERD     30
#define DLGSYS_FASTBOOT   31
#define DLGSYS_PREFETCH   32
#define DLGSYS_CYC_EXACT  33
#define DLGSYS_MMU_EMUL   34
#define DLGSYS_24BITS     35
#define DLGSYS_FPU_NONE   38
#define DLGSYS_FPU_68881  39
#define DLGSYS_FPU_68882  40
#define DLGSYS_FPU_CPU_IN 41
#define DLGSYS_FPU_COMPAT 42


static SGOBJ systemdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 60,25, NULL },
	{ SGTEXT, 0, 0, 23,1, 14,1, "System options" },

	{ SGBOX, 0, 0, 2,3, 14,8, NULL },
	{ SGTEXT, 0, 0, 3,3, 13,1, "Machine type" },
	{ SGRADIOBUT, 0, 0, 3,5, 4,1, "_ST" },
	{ SGRADIOBUT, 0, 0, 3,6, 5,1, "ST_E" },
	{ SGRADIOBUT, 0, 0, 3,7, 4,1, "_TT" },
	{ SGRADIOBUT, 0, 0, 3,8, 8,1, "_Falcon" },

	{ SGBOX, 0, 0, 17,3, 13,8, NULL },
	{ SGTEXT, 0, 0, 19,3, 8,1, "CPU type" },
	{ SGRADIOBUT, 0, 0, 19, 5, 7,1, "680_00" },
	{ SGRADIOBUT, 0, 0, 19, 6, 7,1, "680_10" },
	{ SGRADIOBUT, 0, 0, 19, 7, 7,1, "680_20" },
#ifdef ENABLE_WINUAE_CPU
	{ SGRADIOBUT, 0, 0, 19, 8, 7,1, "680_30" },
	{ SGRADIOBUT, 0, 0, 19, 9, 7,1, "680_40" },
	{ SGRADIOBUT, 0, 0, 19,10, 7,1, "68060" },
#else
	{ SGRADIOBUT, 0, 0, 19, 8,11,1, "680_30+FPU" },
	{ SGRADIOBUT, 0, 0, 19, 9, 7,1, "680_40" },
	{ SGTEXT, 0, 0, 19,10, 7,1, "" },
#endif

	{ SGBOX, 0, 0, 31,3, 12,8, NULL },
	{ SGTEXT, 0, 0, 32,3, 15,1, "CPU clock" },
	{ SGRADIOBUT, 0, 0, 32,5, 8,1, " _8 Mhz" },
	{ SGRADIOBUT, 0, 0, 32,6, 8,1, "1_6 Mhz" },
	{ SGRADIOBUT, 0, 0, 32,7, 8,1, "32 _Mhz" },

	{ SGBOX, 0, 0, 44,3, 14,8, NULL },
	{ SGTEXT, 0, 0, 45,3, 11,1, "Falcon DSP" },
	{ SGRADIOBUT, 0, 0, 45,5, 6,1, "_None" },
	{ SGRADIOBUT, 0, 0, 45,6, 7,1, "Dumm_y" },
	{ SGRADIOBUT, 0, 0, 45,7, 6,1, "Ful_l" },

#ifdef ENABLE_WINUAE_CPU
	{ SGBOX, 0, 0, 2,12, 41,10, NULL },
#else
	{ SGBOX, 0, 0, 2,12, 56,9, NULL },
#endif
	{ SGTEXT, 0, 0, 3,12, 11,1, "CPU & system parameters" },
	{ SGCHECKBOX, 0, 0, 3,14, 27,1, "_Real time clock emulation" },
	{ SGCHECKBOX, 0, 0, 3,15, 19,1, "_Blitter emulation" },
	{ SGCHECKBOX, 0, 0, 3,16, 15,1, "Patch Timer-_D" },
	{ SGCHECKBOX, 0, 0, 3,17, 39,1, "Boot faster by _patching TOS & sysvars" },
	{ SGCHECKBOX, 0, 0, 3,18, 23,1, "Prefetc_h mode, slower" },
#ifdef ENABLE_WINUAE_CPU
	{ SGCHECKBOX, 0, 0, 3,19, 22,1, "Cycle e_xact, slower" },
	{ SGCHECKBOX, 0, 0, 3,20, 15,1, "MM_U emulation" },
	{ SGCHECKBOX, 0, 0, 3,21, 21,1, "24 bits addressin_g" },
#else
	{ SGTEXT, 0, 0, 3,19, 1,1, "" },
	{ SGTEXT, 0, 0, 3,20, 1,1, "" },
	{ SGTEXT, 0, 0, 3,21, 1,1, "" },
#endif

#ifdef ENABLE_WINUAE_CPU
	{ SGBOX, 0, 0, 44,12, 14,10, NULL },
	{ SGTEXT, 0, 0, 45,12, 11,1, "FPU" },
	{ SGRADIOBUT, 0, 0, 45,14, 6,1, "N_one" },
	{ SGRADIOBUT, 0, 0, 45,15, 7,1, "68881" },
	{ SGRADIOBUT, 0, 0, 45,16, 7,1, "68882" },
	{ SGRADIOBUT, 0, 0, 45,17, 14,1, "_internal" },
	{ SGCHECKBOX, 0, 0, 45,19, 25,1, "_compatible" },
	{ SGTEXT,     0, 0, 47,20, 12,1, "but slower" },
#else
	{ SGTEXT, 0, 0, 44,12, 14,10, "" },
	{ SGTEXT, 0, 0, 45,12, 11,1, "" },
	{ SGTEXT, 0, 0, 45,14, 6,1, "" },
	{ SGTEXT, 0, 0, 45,15, 7,1, "" },
	{ SGTEXT, 0, 0, 45,16, 7,1, "" },
	{ SGTEXT, 0, 0, 45,17, 14,1, "" },
	{ SGTEXT, 0, 0, 45,19, 25,1, "" },
	{ SGTEXT, 0, 0, 47,20, 12,1, "" },
#endif
	{ SGBUTTON, SG_DEFAULT, 0, 21,23, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "System" dialog
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
	for (i = DLGSYS_8MHZ; i <= DLGSYS_32MHZ; i++)
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

	/* Boot faster by patching system variables */
	if (ConfigureParams.System.bFastBoot)
		systemdlg[DLGSYS_FASTBOOT].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_FASTBOOT].state &= ~SG_SELECTED;

#ifdef ENABLE_WINUAE_CPU
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
#endif

	/* Show the dialog: */
	SDLGui_DoDialog(systemdlg, NULL, false);

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
	ConfigureParams.System.bFastBoot = (systemdlg[DLGSYS_FASTBOOT].state & SG_SELECTED);

#ifdef ENABLE_WINUAE_CPU
	ConfigureParams.System.bCycleExactCpu = (systemdlg[DLGSYS_CYC_EXACT].state & SG_SELECTED);
	ConfigureParams.System.bMMU = (systemdlg[DLGSYS_MMU_EMUL].state & SG_SELECTED);
	ConfigureParams.System.bAddressSpace24 = (systemdlg[DLGSYS_24BITS].state & SG_SELECTED);

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
#endif
}
