/*
  Hatari - dlgSystem.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
const char DlgSystem_fileid[] = "Hatari dlgSystem.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


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
#define DLGSYS_PREFETCH 20
#define DLGSYS_BLITTER  21
#define DLGSYS_RTC      22
#define DLGSYS_TIMERD   23
#define DLGSYS_SLOWFDC  24


/* The "System" dialog: */
static SGOBJ systemdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 36,23, NULL },
	{ SGTEXT, 0, 0, 11,1, 14,1, "System options" },

	{ SGBOX, 0, 0, 2,3, 16,9, NULL },
	{ SGTEXT, 0, 0, 3,4, 8,1, "CPU type:" },
	{ SGRADIOBUT, 0, 0, 6,6, 7,1, "68000" },
	{ SGRADIOBUT, 0, 0, 6,7, 7,1, "68010" },
	{ SGRADIOBUT, 0, 0, 6,8, 7,1, "68020" },
	{ SGRADIOBUT, 0, 0, 6,9, 11,1, "68020+FPU" },
	{ SGRADIOBUT, 0, 0, 6,10, 7,1, "68040" },

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

	{ SGCHECKBOX, 0, 0, 2,15, 32,1, "Slower but more compatible CPU" },
	{ SGCHECKBOX, 0, 0, 2,16, 20,1, "Blitter emulation" },
	{ SGCHECKBOX, 0, 0, 2,17, 27,1, "Real time clock emulation" },
	{ SGCHECKBOX, 0, 0, 2,18, 15,1, "Patch Timer-D" },
	{ SGCHECKBOX, 0, 0, 2,19, 25,1, "Slow down FDC emulation" },

	{ SGBUTTON, SG_DEFAULT, 0, 8,21, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the "System" dialog.
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

	if (ConfigureParams.System.bSlowFDC)
		systemdlg[DLGSYS_SLOWFDC].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_SLOWFDC].state &= ~SG_SELECTED;

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

	ConfigureParams.System.bCompatibleCpu = (systemdlg[DLGSYS_PREFETCH].state & SG_SELECTED);
	ConfigureParams.System.bBlitter = (systemdlg[DLGSYS_BLITTER].state & SG_SELECTED);
	ConfigureParams.System.bRealTimeClock = (systemdlg[DLGSYS_RTC].state & SG_SELECTED);
	ConfigureParams.System.bPatchTimerD = (systemdlg[DLGSYS_TIMERD].state & SG_SELECTED);
	ConfigureParams.System.bSlowFDC = (systemdlg[DLGSYS_SLOWFDC].state & SG_SELECTED);
}
