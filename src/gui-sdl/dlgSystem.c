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


#define DLGSYS_ST          4
#define DLGSYS_MEGA_ST     5
#define DLGSYS_STE         6
#define DLGSYS_MEGA_STE    7
#define DLGSYS_TT          8
#define DLGSYS_FALCON      9
#define DLGSYS_WSRND      12
#define DLGSYS_WS1        13
#define DLGSYS_WS2        14
#define DLGSYS_WS3        15
#define DLGSYS_WS4        16
#define DLGSYS_DSPOFF     19
#define DLGSYS_DSPDUMMY   20
#define DLGSYS_DSPON      21
#define DLGSYS_BLITTER    22
#define DLGSYS_TIMERD     23
#define DLGSYS_FASTBOOT   24


static SGOBJ systemdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 50,18, NULL },
	{ SGTEXT, 0, 0, 18,1, 14,1, "System options" },

	{ SGBOX, 0, 0, 2,3, 15,8, NULL },
	{ SGTEXT, 0, 0, 3,3, 13,1, "Machine type:" },
	{ SGRADIOBUT, 0, 0, 3,5, 4,1, "_ST" },
	{ SGRADIOBUT, 0, 0, 3,6, 9,1, "Meg_a ST" },
	{ SGRADIOBUT, 0, 0, 3,7, 5,1, "ST_E" },
	{ SGRADIOBUT, 0, 0, 3,8, 10,1, "Me_ga STE" },
	{ SGRADIOBUT, 0, 0, 3,9, 4,1, "_TT" },
	{ SGRADIOBUT, 0, 0, 3,10, 8,1, "_Falcon" },

	{ SGBOX, 0, 0, 18,3, 15,8, NULL },
	{ SGTEXT, 0, 0, 19,3, 13,1, "Video timing:" },
	{ SGRADIOBUT, 0, 0, 19,5, 8,1, "_Random" },
	{ SGRADIOBUT, 0, 0, 19,6, 12,1, "Wakestate_1" },
	{ SGRADIOBUT, 0, 0, 19,7, 12,1, "Wakestate_2" },
	{ SGRADIOBUT, 0, 0, 19,8, 12,1, "Wakestate_3" },
	{ SGRADIOBUT, 0, 0, 19,9, 12,1, "Wakestate_4" },

	{ SGBOX, 0, 0, 34,3, 14,8, NULL },
	{ SGTEXT, 0, 0, 35,3, 12,1, "Falcon DSP:" },
	{ SGRADIOBUT, 0, 0, 35,5, 6,1, "_None" },
	{ SGRADIOBUT, 0, 0, 35,6, 7,1, "Dumm_y" },
	{ SGRADIOBUT, 0, 0, 35,7, 6,1, "Ful_l" },

	{ SGCHECKBOX, 0, 0, 3,12, 20,1, "_Blitter in ST mode" },
	{ SGCHECKBOX, 0, 0, 3,13, 15,1, "Patch Timer-_D" },
	{ SGCHECKBOX, 0, 0, 3,14, 39,1, "Boot faster by _patching TOS & sysvars" },

	{ SGBUTTON, SG_DEFAULT, 0, 16,16, 20,1, "Back to main menu" },
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
