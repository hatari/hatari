/*
  Hatari - dlgSystem.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgSystem_rcsid[] = "Hatari $Id: dlgSystem.c,v 1.3 2005-02-10 00:11:43 thothy Exp $";

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
#define DLGSYS_PREFETCH 13
#define DLGSYS_BLITTER  14
#define DLGSYS_RTC      15
#define DLGSYS_TIMERD   16
#define DLGSYS_SLOWFDC  17


/* The "System" dialog: */
static SGOBJ systemdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 36,21, NULL },
	{ SGTEXT, 0, 0, 11,1, 14,1, "System options" },

	{ SGBOX, 0, 0, 2,3, 16,9, NULL },
	{ SGTEXT, 0, 0, 3,4, 8,1, "CPU type:" },
	{ SGRADIOBUT, 0, 0, 6,6, 7,1, "68000" },
	{ SGRADIOBUT, 0, 0, 6,7, 7,1, "68010" },
	{ SGRADIOBUT, 0, 0, 6,8, 7,1, "68020" },
	{ SGRADIOBUT, 0, 0, 6,9, 11,1, "68020+FPU" },
	{ SGRADIOBUT, 0, 0, 6,10, 7,1, "68040" },

	{ SGBOX, 0, 0, 19,3, 15,6, NULL },
	{ SGTEXT, 0, 0, 20,4, 13,1, "Machine type:" },
	{ SGRADIOBUT, 0, 0, 23,6, 7,1, "ST" },
	{ SGRADIOBUT, 0, 0, 23,7, 7,1, "STE" },

	{ SGCHECKBOX, 0, 0, 2,13, 32,1, "Slower but more compatible CPU" },
	{ SGCHECKBOX, 0, 0, 2,14, 20,1, "Blitter emulation" },
	{ SGCHECKBOX, 0, 0, 2,15, 27,1, "Real time clock emulation" },
	{ SGCHECKBOX, 0, 0, 2,16, 15,1, "Patch Timer-D" },
	{ SGCHECKBOX, 0, 0, 2,17, 25,1, "Slow down FDC emulation" },

	{ SGBUTTON, 0, 0, 8,19, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the "System" dialog.
*/
void Dialog_SystemDlg(void)
{
	int i;

	SDLGui_CenterDlg(systemdlg);

	/* Set up dialog from actual values: */

	for (i = DLGSYS_68000; i <= DLGSYS_68040; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	systemdlg[DLGSYS_68000+DialogParams.System.nCpuLevel].state |= SG_SELECTED;

	for (i = DLGSYS_ST; i <= DLGSYS_STE; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	systemdlg[DLGSYS_ST + DialogParams.System.nMachineType].state |= SG_SELECTED;

	if (DialogParams.System.bCompatibleCpu)
		systemdlg[DLGSYS_PREFETCH].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_PREFETCH].state &= ~SG_SELECTED;

	if (DialogParams.System.bBlitter)
		systemdlg[DLGSYS_BLITTER].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_BLITTER].state &= ~SG_SELECTED;

	if (DialogParams.System.bRealTimeClock)
		systemdlg[DLGSYS_RTC].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_RTC].state &= ~SG_SELECTED;

	if (DialogParams.System.bPatchTimerD)
		systemdlg[DLGSYS_TIMERD].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_TIMERD].state &= ~SG_SELECTED;

	if (DialogParams.System.bSlowFDC)
		systemdlg[DLGSYS_SLOWFDC].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_SLOWFDC].state &= ~SG_SELECTED;

	/* Show the dialog: */
	SDLGui_DoDialog(systemdlg);

	/* Read values from dialog: */

	for (i = DLGSYS_68000; i <= DLGSYS_68040; i++)
	{
		if (systemdlg[i].state&SG_SELECTED)
		{
			DialogParams.System.nCpuLevel = i-DLGSYS_68000;
			break;
		}
	}

	for (i = DLGSYS_ST; i <= DLGSYS_STE; i++)
	{
		if (systemdlg[i].state&SG_SELECTED)
		{
			DialogParams.System.nMachineType = i-DLGSYS_ST;
			break;
		}
	}

	DialogParams.System.bCompatibleCpu = (systemdlg[DLGSYS_PREFETCH].state & SG_SELECTED);
	DialogParams.System.bBlitter = (systemdlg[DLGSYS_BLITTER].state & SG_SELECTED);
	DialogParams.System.bRealTimeClock = (systemdlg[DLGSYS_RTC].state & SG_SELECTED);
	DialogParams.System.bPatchTimerD = (systemdlg[DLGSYS_TIMERD].state & SG_SELECTED);
	DialogParams.System.bSlowFDC = (systemdlg[DLGSYS_SLOWFDC].state & SG_SELECTED);
}
