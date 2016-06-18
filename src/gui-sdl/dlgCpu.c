/*
  Hatari - dlgCpu.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This is the CPU settings dialog
*/
const char DlgCpu_fileid[] = "Hatari dlgCpu.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


#define DLGCPU_68000      4
#define DLGCPU_68010      5
#define DLGCPU_68020      6
#define DLGCPU_68030      7
#define DLGCPU_68040      8
#define DLGCPU_68060      9
#define DLGCPU_8MHZ       12
#define DLGCPU_16MHZ      13
#define DLGCPU_32MHZ      14
#define DLGCPU_FPU_NONE   17
#define DLGCPU_FPU_68881  18
#define DLGCPU_FPU_68882  19
#define DLGCPU_FPU_CPU_IN 20
#define DLGCPU_FPU_COMPAT 21
#define DLGCPU_PREFETCH   25
#define DLGCPU_CYC_EXACT  26
#define DLGCPU_MMU_EMUL   27
#define DLGCPU_24BITS     28


static SGOBJ cpudlg[] =
{
	{ SGBOX, 0, 0, 0,0, 46,23, NULL },
	{ SGTEXT, 0, 0, 17,1, 14,1, "CPU options" },

	{ SGBOX, 0, 0, 2,3, 13,9, NULL },
	{ SGTEXT, 0, 0, 3,3, 8,1, "CPU type" },
	{ SGRADIOBUT, 0, 0, 3, 5, 7,1, "680_00" },
	{ SGRADIOBUT, 0, 0, 3, 6, 7,1, "680_10" },
	{ SGRADIOBUT, 0, 0, 3, 7, 7,1, "680_20" },
#ifdef ENABLE_WINUAE_CPU
	{ SGRADIOBUT, 0, 0, 3, 8, 7,1, "680_30" },
	{ SGRADIOBUT, 0, 0, 3, 9, 7,1, "680_40" },
	{ SGRADIOBUT, 0, 0, 3,10, 7,1, "68060" },
#else
	{ SGRADIOBUT, 0, 0, 3, 8,11,1, "680_30+FPU" },
	{ SGRADIOBUT, 0, 0, 3, 9, 7,1, "680_40" },
	{ SGTEXT, 0, 0, 3,10, 7,1, "" },
#endif

	{ SGBOX, 0, 0, 16,3, 13,9, NULL },
	{ SGTEXT, 0, 0, 17,3, 15,1, "CPU clock" },
	{ SGRADIOBUT, 0, 0, 17,5, 8,1, " _8 Mhz" },
	{ SGRADIOBUT, 0, 0, 17,6, 8,1, "1_6 Mhz" },
	{ SGRADIOBUT, 0, 0, 17,7, 8,1, "32 _Mhz" },

	{ SGBOX, 0, 0, 30,3, 14,9, NULL },
	{ SGTEXT, 0, 0, 31,3, 11,1, "FPU" },
#ifdef ENABLE_WINUAE_CPU
	{ SGRADIOBUT, 0, 0, 31,5, 6,1, "N_one" },
	{ SGRADIOBUT, 0, 0, 31,6, 7,1, "68881" },
	{ SGRADIOBUT, 0, 0, 31,7, 7,1, "68882" },
	{ SGRADIOBUT, 0, 0, 31,8, 10,1, "_internal" },
	{ SGCHECKBOX, 0, 0, 31,10, 12,1, "_compatible" },
	{ SGTEXT,     0, 0, 33,11, 12,1, "but slower" },

#else
	{ SGTEXT, 0, 0, 31,5, 10,1, "config not" },
	{ SGTEXT, 0, 0, 31,6, 10,1, "available" },
	{ SGTEXT, 0, 0, 31,7, 10,1, "in old" },
	{ SGTEXT, 0, 0, 31,8, 10,1, "UAE mode" },
	{ SGTEXT, 0, 0, 31,10, 25,1, "" },
	{ SGTEXT, 0, 0, 33,11, 12,1, "" },
#endif

	{ SGBOX, 0, 0, 2,13, 42,7, NULL },
	{ SGTEXT, 0, 0, 3,13, 11,1, "CPU parameters" },
	{ SGCHECKBOX, 0, 0, 3,15, 23,1, "Prefetc_h mode, slower" },
#ifdef ENABLE_WINUAE_CPU
	{ SGCHECKBOX, 0, 0, 3,16, 21,1, "Cycle e_xact, slower" },
	{ SGCHECKBOX, 0, 0, 3,17, 15,1, "MM_U emulation" },
	{ SGCHECKBOX, 0, 0, 3,18, 20,1, "24 bits add_ressing" },
#else
	{ SGTEXT, 0, 0, 3,16, 1,1, "" },
	{ SGTEXT, 0, 0, 3,17, 1,1, "" },
	{ SGTEXT, 0, 0, 3,18, 1,1, "" },
#endif

	{ SGBUTTON, SG_DEFAULT, 0, 13,21, 19,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "CPU" dialog
 */
void DlgCpu_Main(void)
{
	int i;

	SDLGui_CenterDlg(cpudlg);

	/* CPU level: */
	for (i = DLGCPU_68000; i <= DLGCPU_68060; i++)
	{
		cpudlg[i].state &= ~SG_SELECTED;
	}
	cpudlg[DLGCPU_68000+ConfigureParams.System.nCpuLevel].state |= SG_SELECTED;

	/* CPU frequency: */
	for (i = DLGCPU_8MHZ; i <= DLGCPU_32MHZ; i++)
	{
		cpudlg[i].state &= ~SG_SELECTED;
	}
	if (ConfigureParams.System.nCpuFreq == 32)
		cpudlg[DLGCPU_32MHZ].state |= SG_SELECTED;
	else if (ConfigureParams.System.nCpuFreq == 16)
		cpudlg[DLGCPU_16MHZ].state |= SG_SELECTED;
	else
		cpudlg[DLGCPU_8MHZ].state |= SG_SELECTED;

	/* More compatible CPU, Prefetch mode */
	if (ConfigureParams.System.bCompatibleCpu)
		cpudlg[DLGCPU_PREFETCH].state |= SG_SELECTED;
	else
		cpudlg[DLGCPU_PREFETCH].state &= ~SG_SELECTED;

#ifdef ENABLE_WINUAE_CPU
	/* Address space 24 bits */
	if (ConfigureParams.System.bAddressSpace24)
		cpudlg[DLGCPU_24BITS].state |= SG_SELECTED;
	else
		cpudlg[DLGCPU_24BITS].state &= ~SG_SELECTED;
		
	/* Cycle exact CPU */
	if (ConfigureParams.System.bCycleExactCpu)
		cpudlg[DLGCPU_CYC_EXACT].state |= SG_SELECTED;
	else
		cpudlg[DLGCPU_CYC_EXACT].state &= ~SG_SELECTED;

	/* FPU emulation */
	for (i = DLGCPU_FPU_NONE; i <= DLGCPU_FPU_CPU_IN; i++)
	{
		cpudlg[i].state &= ~SG_SELECTED;
	}
	if (ConfigureParams.System.n_FPUType == FPU_NONE)
		cpudlg[DLGCPU_FPU_NONE].state |= SG_SELECTED;
	else if (ConfigureParams.System.n_FPUType == FPU_68881)
		cpudlg[DLGCPU_FPU_68881].state |= SG_SELECTED;
	else if (ConfigureParams.System.n_FPUType == FPU_68882)
		cpudlg[DLGCPU_FPU_68882].state |= SG_SELECTED;
	else
		cpudlg[DLGCPU_FPU_CPU_IN].state |= SG_SELECTED;

	/* More compatible FPU */
	if (ConfigureParams.System.bCompatibleFPU)
		cpudlg[DLGCPU_FPU_COMPAT].state |= SG_SELECTED;
	else
		cpudlg[DLGCPU_FPU_COMPAT].state &= ~SG_SELECTED;

	/* MMU Emulation */
	if (ConfigureParams.System.bMMU)
		cpudlg[DLGCPU_MMU_EMUL].state |= SG_SELECTED;
	else
		cpudlg[DLGCPU_MMU_EMUL].state &= ~SG_SELECTED;
#endif

	/* Show the dialog: */
	SDLGui_DoDialog(cpudlg, NULL, false);

	/* Read values from dialog: */

	for (i = DLGCPU_68000; i <= DLGCPU_68060; i++)
	{
		if (cpudlg[i].state&SG_SELECTED)
		{
			ConfigureParams.System.nCpuLevel = i-DLGCPU_68000;
			break;
		}
	}

	if (cpudlg[DLGCPU_32MHZ].state & SG_SELECTED)
		ConfigureParams.System.nCpuFreq = 32;
	else if (cpudlg[DLGCPU_16MHZ].state & SG_SELECTED)
		ConfigureParams.System.nCpuFreq = 16;
	else
		ConfigureParams.System.nCpuFreq = 8;

	ConfigureParams.System.bCompatibleCpu = (cpudlg[DLGCPU_PREFETCH].state & SG_SELECTED);

#ifdef ENABLE_WINUAE_CPU
	ConfigureParams.System.bCycleExactCpu = (cpudlg[DLGCPU_CYC_EXACT].state & SG_SELECTED);
	ConfigureParams.System.bMMU = (cpudlg[DLGCPU_MMU_EMUL].state & SG_SELECTED);
	ConfigureParams.System.bAddressSpace24 = (cpudlg[DLGCPU_24BITS].state & SG_SELECTED);

	/* FPU emulation */
	if (cpudlg[DLGCPU_FPU_NONE].state & SG_SELECTED)
		ConfigureParams.System.n_FPUType = FPU_NONE;
	else if (cpudlg[DLGCPU_FPU_68881].state & SG_SELECTED)
		ConfigureParams.System.n_FPUType = FPU_68881;
	else if (cpudlg[DLGCPU_FPU_68882].state & SG_SELECTED)
		ConfigureParams.System.n_FPUType = FPU_68882;
	else
		ConfigureParams.System.n_FPUType = FPU_CPU;

	ConfigureParams.System.bCompatibleFPU = (cpudlg[DLGCPU_FPU_COMPAT].state & SG_SELECTED);
#endif
}
