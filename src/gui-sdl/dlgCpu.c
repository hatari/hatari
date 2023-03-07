/*
  Hatari - dlgCpu.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This is the CPU settings dialog
*/
const char DlgCpu_fileid[] = "Hatari dlgCpu.c";

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
#define DLGCPU_PREFETCH   23
#define DLGCPU_CYC_EXACT  24
#define DLGCPU_MMU_EMUL   25
#define DLGCPU_24BITS     26
#define DLGCPU_SOFTFLOAT  27

static SGOBJ cpudlg[] =
{
	{ SGBOX, 0, 0, 0,0, 44,24, NULL },
	{ SGTEXT, 0, 0, 17,1, 12,1, "CPU options" },

	{ SGBOX, 0, 0, 2,3, 12,8, NULL },
	{ SGTEXT, 0, 0, 3,3, 9,1, "CPU type:" },
	{ SGRADIOBUT, 0, 0, 3, 5, 7,1, "680_00" },
	{ SGRADIOBUT, 0, 0, 3, 6, 7,1, "680_10" },
	{ SGRADIOBUT, 0, 0, 3, 7, 7,1, "680_20" },
	{ SGRADIOBUT, 0, 0, 3, 8, 7,1, "680_30" },
	{ SGRADIOBUT, 0, 0, 3, 9, 7,1, "680_40" },
	{ SGRADIOBUT, 0, 0, 3,10, 7,1, "68060" },

	{ SGBOX, 0, 0, 16,3, 12,8, NULL },
	{ SGTEXT, 0, 0, 17,3, 10,1, "CPU clock:" },
	{ SGRADIOBUT, 0, 0, 17,5, 8,1, " _8 Mhz" },
	{ SGRADIOBUT, 0, 0, 17,6, 8,1, "1_6 Mhz" },
	{ SGRADIOBUT, 0, 0, 17,7, 8,1, "32 Mh_z" },

	{ SGBOX, 0, 0, 30,3, 12,8, NULL },
	{ SGTEXT, 0, 0, 31,3, 4,1, "FPU:" },
	{ SGRADIOBUT, 0, 0, 31,5, 6,1, "_None" },
	{ SGRADIOBUT, 0, 0, 31,6, 7,1, "68881" },
	{ SGRADIOBUT, 0, 0, 31,7, 7,1, "68882" },
	{ SGRADIOBUT, 0, 0, 31,8, 10,1, "_Internal" },

	{ SGBOX, 0, 0, 2,12, 40,9, NULL },
	{ SGTEXT, 0, 0, 9,12, 24,1, "CPU emulation parameters" },
	{ SGCHECKBOX, 0, 0, 3,14, 21,1, "_Prefetch emulation*" },
	{ SGCHECKBOX, 0, 0, 3,15, 35,1, "_Cycle-exact with cache emulation*" },
	{ SGCHECKBOX, 0, 0, 3,16, 16,1, "_MMU emulation*" },
	{ SGCHECKBOX, 0, 0, 3,17, 20,1, "24-bit _addressing" },
	{ SGCHECKBOX, 0, 0, 3,18, 26,1, "Accurate _FPU emulation*" },
	{ SGTEXT, 0, 0, 3,20, 20,1, "* Uses more host CPU" },

	{ SGBUTTON, SG_DEFAULT, 0, 13,22, 19,1, "Back to main menu" },
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

	/* MMU emulation */
	if (ConfigureParams.System.bMMU)
		cpudlg[DLGCPU_MMU_EMUL].state |= SG_SELECTED;
	else
		cpudlg[DLGCPU_MMU_EMUL].state &= ~SG_SELECTED;

	/* FPU emulation using softfloat */
	if (ConfigureParams.System.bSoftFloatFPU)
		cpudlg[DLGCPU_SOFTFLOAT].state |= SG_SELECTED;
	else
		cpudlg[DLGCPU_SOFTFLOAT].state &= ~SG_SELECTED;

	/* Show the dialog: */
	SDLGui_DoDialog(cpudlg);

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
		Configuration_ChangeCpuFreq ( 32 );
	else if (cpudlg[DLGCPU_16MHZ].state & SG_SELECTED)
		Configuration_ChangeCpuFreq ( 16 );
	else
		Configuration_ChangeCpuFreq ( 8 );

	ConfigureParams.System.bCompatibleCpu = (cpudlg[DLGCPU_PREFETCH].state & SG_SELECTED);

	ConfigureParams.System.bCycleExactCpu = (cpudlg[DLGCPU_CYC_EXACT].state & SG_SELECTED);
	ConfigureParams.System.bMMU = (cpudlg[DLGCPU_MMU_EMUL].state & SG_SELECTED);
	ConfigureParams.System.bAddressSpace24 = (cpudlg[DLGCPU_24BITS].state & SG_SELECTED);
	ConfigureParams.System.bSoftFloatFPU = (cpudlg[DLGCPU_SOFTFLOAT].state & SG_SELECTED);

	/* FPU emulation */
	if (cpudlg[DLGCPU_FPU_NONE].state & SG_SELECTED)
		ConfigureParams.System.n_FPUType = FPU_NONE;
	else if (cpudlg[DLGCPU_FPU_68881].state & SG_SELECTED)
		ConfigureParams.System.n_FPUType = FPU_68881;
	else if (cpudlg[DLGCPU_FPU_68882].state & SG_SELECTED)
		ConfigureParams.System.n_FPUType = FPU_68882;
	else
		ConfigureParams.System.n_FPUType = FPU_CPU;
}
