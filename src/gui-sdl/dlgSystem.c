/*
  Hatari - dlgSystem.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Dialog for setting various system options
*/
const char DlgSystem_fileid[] = "Hatari dlgSystem.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


#define DLGSYS_ST          3
#define DLGSYS_MEGA_ST     4
#define DLGSYS_STE         5
#define DLGSYS_MEGA_STE    6
#define DLGSYS_TT          7
#define DLGSYS_FALCON      8
#define DLGSYS_DSPOFF     10
#define DLGSYS_DSPDUMMY   11
#define DLGSYS_DSPON      12
#define DLGSYS_WSRND      14
#define DLGSYS_WS1        15
#define DLGSYS_WS2        16
#define DLGSYS_WS3        17
#define DLGSYS_WS4        18
#define DLGSYS_BLITTER    19
#define DLGSYS_TIMERD     20
#define DLGSYS_FASTBOOT   21


static SGOBJ systemdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 56,17, NULL },
	{ SGTEXT, 0, 0, 21,1, 14,1, "System options" },

	{ SGTEXT, 0, 0, 2,3, 13,1, "Machine type:" },
	{ SGRADIOBUT, 0, 0, 17,3, 4,1, "_ST" },
	{ SGRADIOBUT, 0, 0, 17,4, 9,1, "Meg_a ST" },
	{ SGRADIOBUT, 0, 0, 28,3, 5,1, "ST_E" },
	{ SGRADIOBUT, 0, 0, 28,4, 10,1, "Me_ga STE" },
	{ SGRADIOBUT, 0, 0, 40,3, 4,1, "_TT" },
	{ SGRADIOBUT, 0, 0, 40,4, 8,1, "_Falcon" },

	{ SGTEXT, 0, 0, 2,6, 12,1, "Falcon DSP:" },
	{ SGRADIOBUT, 0, 0, 17,6, 6,1, "_None" },
	{ SGRADIOBUT, 0, 0, 28,6, 7,1, "Dumm_y" },
	{ SGRADIOBUT, 0, 0, 40,6, 6,1, "Ful_l" },

	{ SGTEXT, 0, 0, 2,8, 13,1, "Video timing:" },
	{ SGRADIOBUT, 0, 0, 17,8, 8,1, "_Random" },
	{ SGRADIOBUT, 0, 0, 27,8, 12,1, "Wakestate_1" },
	{ SGRADIOBUT, 0, 0, 27,9, 12,1, "Wakestate_2" },
	{ SGRADIOBUT, 0, 0, 41,8, 12,1, "Wakestate_3" },
	{ SGRADIOBUT, 0, 0, 41,9, 12,1, "Wakestate_4" },

	{ SGCHECKBOX, 0, 0, 3,11, 20,1, "_Blitter in ST mode" },
	{ SGCHECKBOX, 0, 0, 3,12, 15,1, "Patch Timer-_D" },
	{ SGCHECKBOX, 0, 0, 3,13, 39,1, "Boot faster by _patching TOS & sysvars" },

	{ SGBUTTON, SG_DEFAULT, 0, 19,15, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "System" dialog
 */
void DlgSystem_Main(void)
{
	int i;
	MACHINETYPE	mti;

	SDLGui_CenterDlg(systemdlg);

	/* Set up machine type */
	for (i = DLGSYS_ST; i <= DLGSYS_FALCON; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	systemdlg[DLGSYS_ST + ConfigureParams.System.nMachineType].state |= SG_SELECTED;


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

	/* Video timing */
	for (i = DLGSYS_WSRND; i <= DLGSYS_WS4; i++)
	{
		systemdlg[i].state &= ~SG_SELECTED;
	}
	systemdlg[DLGSYS_WSRND + ConfigureParams.System.VideoTimingMode].state |= SG_SELECTED;

	/* Emulate Blitter */
	if (ConfigureParams.System.bBlitter)
		systemdlg[DLGSYS_BLITTER].state |= SG_SELECTED;
	else
		systemdlg[DLGSYS_BLITTER].state &= ~SG_SELECTED;

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

	/* Show the dialog: */
	SDLGui_DoDialog(systemdlg, NULL, false);

	/* Read values from dialog: */

	for (mti = MACHINE_ST; mti <= MACHINE_FALCON; mti++)
	{
		if (systemdlg[mti + DLGSYS_ST].state&SG_SELECTED)
		{
			ConfigureParams.System.nMachineType = mti;
			break;
		}
	}

	if (systemdlg[DLGSYS_DSPOFF].state & SG_SELECTED)
		ConfigureParams.System.nDSPType = DSP_TYPE_NONE;
	else if (systemdlg[DLGSYS_DSPDUMMY].state & SG_SELECTED)
		ConfigureParams.System.nDSPType = DSP_TYPE_DUMMY;
	else
		ConfigureParams.System.nDSPType = DSP_TYPE_EMU;

	for (i = DLGSYS_WSRND; i <= DLGSYS_WS4; i++)
	{
		if (systemdlg[i].state & SG_SELECTED)
		{
			ConfigureParams.System.VideoTimingMode = i - DLGSYS_WSRND;
			break;
		}
	}

	ConfigureParams.System.bBlitter = (systemdlg[DLGSYS_BLITTER].state & SG_SELECTED);
	ConfigureParams.System.bPatchTimerD = (systemdlg[DLGSYS_TIMERD].state & SG_SELECTED);
	ConfigureParams.System.bFastBoot = (systemdlg[DLGSYS_FASTBOOT].state & SG_SELECTED);
}
