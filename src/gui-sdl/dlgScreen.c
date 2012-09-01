/*
  Hatari - dlgScreen.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

 Atari monitor and Hatari window settings.
*/
const char DlgScreen_fileid[] = "Hatari dlgScreen.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "resolution.h"
#include "vdi.h"
#include "video.h"
#include "avi_record.h"
#include "statusbar.h"
#include "clocks_timings.h"

#define ITEMS_IN_ARRAY(a) (sizeof(a)/sizeof(a[0]))


/* The Monitor dialog: */
#define DLGSCRN_MONO         3
#define DLGSCRN_RGB          4
#define DLGSCRN_VGA          5
#define DLGSCRN_TV           6
#define DLGSCRN_OVERSCAN     7
#define DLGSCRN_USEVDIRES    9
#define DLGSCRN_VDI_WLESS    11
#define DLGSCRN_VDI_WTEXT    12
#define DLGSCRN_VDI_WMORE    13
#define DLGSCRN_VDI_HLESS    15
#define DLGSCRN_VDI_HTEXT    16
#define DLGSCRN_VDI_HMORE    17
#define DLGSCRN_BPP1         18
#define DLGSCRN_BPP2         19
#define DLGSCRN_BPP4         20
#define DLGSCRN_EXIT_MONITOR 21

/* Strings for VDI resolution width and height */
static char sVdiWidth[5];
static char sVdiHeight[5];

static SGOBJ monitordlg[] =
{
	{ SGBOX, 0, 0, 0,0, 34,18, NULL },

	{ SGBOX,      0, 0,  1,1, 32,6, NULL },
	{ SGTEXT,     0, 0, 10,1, 14,1, "Atari monitor" },
	{ SGRADIOBUT, 0, 0,  4,3,  6,1, "Mono" },
	{ SGRADIOBUT, 0, 0, 12,3,  6,1, "RGB" },
	{ SGRADIOBUT, 0, 0, 19,3,  6,1, "VGA" },
	{ SGRADIOBUT, 0, 0, 26,3,  6,1, "TV" },
	{ SGCHECKBOX, 0, 0, 12,5, 22,1, "Show borders" },

	{ SGBOX,      0, 0,  1,8, 32,7, NULL },
	{ SGCHECKBOX, 0, 0,  4,9, 33,1, "Use extended VDI screen" },
	{ SGTEXT,     0, 0,  4,11, 5,1, "Size:" },
	{ SGBUTTON,   0, 0,  6,12, 1,1, "\x04" },     /* Arrow left */
	{ SGTEXT,     0, 0,  8,12, 4,1, sVdiWidth },
	{ SGBUTTON,   0, 0, 13,12, 1,1, "\x03" },     /* Arrow right */
	{ SGTEXT,     0, 0,  4,13, 1,1, "x" },
	{ SGBUTTON,   0, 0,  6,13, 1,1, "\x04" },     /* Arrow left */
	{ SGTEXT,     0, 0,  8,13, 4,1, sVdiHeight },
	{ SGBUTTON,   0, 0, 13,13, 1,1, "\x03" },     /* Arrow right */

	{ SGRADIOBUT, SG_EXIT, 0, 18,11, 11,1, " 2 colors" },
	{ SGRADIOBUT, SG_EXIT, 0, 18,12, 11,1, " 4 colors" },
	{ SGRADIOBUT, SG_EXIT, 0, 18,13, 11,1, "16 colors" },

	{ SGBUTTON, SG_DEFAULT, 0, 7,16, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/* The window dialog: */
#define DLGSCRN_FULLSCRN    3
#define DLGSCRN_STATUSBAR   5
#define DLGSCRN_DRIVELED     6
#define DLGSCRN_NONE        7
#define DLGSCRN_SKIP0       9
#define DLGSCRN_SKIP1       10
#define DLGSCRN_SKIP2       11
#define DLGSCRN_SKIP3       12
#define DLGSCRN_SKIP4       13
#define DLGSCRN_KEEP_RES    15
#define DLGSCRN_MAX_WLESS   18
#define DLGSCRN_MAX_WTEXT   19
#define DLGSCRN_MAX_WMORE   20
#define DLGSCRN_MAX_HLESS   22
#define DLGSCRN_MAX_HTEXT   23
#define DLGSCRN_MAX_HMORE   24
#define DLGSCRN_CROP        27
#define DLGSCRN_CAPTURE     28
#define DLGSCRN_RECANIM     29
#define DLGSCRN_EXIT_WINDOW 30

/* needs to match Frame skip values in windowdlg[]! */
static const int skip_frames[] = { 0, 1, 2, 4, AUTO_FRAMESKIP_LIMIT };

/* Strings for doubled resolution max width and height */
static char sMaxWidth[5];
static char sMaxHeight[5];

#define MAX_SIZE_STEP 8

/* The window dialog: */
static SGOBJ windowdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 52,20, NULL },

	{ SGBOX,      0, 0,  1,1, 50,10, NULL },
	{ SGTEXT,     0, 0,  4,2, 20,1, "Hatari screen options" },
	{ SGCHECKBOX, 0, 0,  4,4, 12,1, "Fullscreen" },
	{ SGTEXT,     0, 0,  4,6, 12,1, "Indicators:" },
	{ SGRADIOBUT, 0, 0,  6,7, 11,1, "Statusbar" },
	{ SGRADIOBUT, 0, 0,  6,8, 11,1, "Drive led" },
	{ SGRADIOBUT, 0, 0,  6,9, 11,1, "None" },
	{ SGTEXT,     0, 0, 19,4, 12,1, "Frame skip:" },
	{ SGRADIOBUT, 0, 0, 21,5,  7,1, "Off" },
	{ SGRADIOBUT, 0, 0, 21,6,  7,1, "1" },
	{ SGRADIOBUT, 0, 0, 21,7,  7,1, "2" },
	{ SGRADIOBUT, 0, 0, 21,8,  7,1, "4" },
	{ SGRADIOBUT, 0, 0, 21,9,  7,1, "Auto" },
	{ SGTEXT,     0, 0, 33,2, 15,1, "Falcon/TT only:" },
	{ SGCHECKBOX, 0, 0, 33,4, 14,2, "Keep desktop" },
	{ SGTEXT,     0, 0, 35,5, 12,1, "resolution" },
	{ SGTEXT,     0, 0, 33,7, 15,1, "Max zoomed win:" },
	{ SGBUTTON,   0, 0, 35,8,  1,1, "\x04" },     /* Arrow left */
	{ SGTEXT,     0, 0, 37,8,  4,1, sMaxWidth },
	{ SGBUTTON,   0, 0, 43,8,  1,1, "\x03" },     /* Arrow right */
	{ SGTEXT,     0, 0, 33,9,  1,1, "x" },
	{ SGBUTTON,   0, 0, 35,9,  1,1, "\x04" },     /* Arrow left */
	{ SGTEXT,     0, 0, 37,9,  4,1, sMaxHeight },
	{ SGBUTTON,   0, 0, 43,9,  1,1, "\x03" },     /* Arrow right */

	{ SGBOX,      0, 0,  1,12, 50,5, NULL },
	{ SGTEXT,     0, 0,  7,13, 16,1, "Screen capture" },
	{ SGCHECKBOX, 0, 0,  8,15, 16,1, "Crop statusbar" },
	{ SGBUTTON,   0, 0, 29,13, 14,1, " Screenshot " },
	{ SGBUTTON,   0, 0, 29,15, 14,1, " Record AVI " },

	{ SGBUTTON, SG_DEFAULT, 0, 17,18, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/* ---------------------------------------------------------------- */

static int nVdiStepX, nVdiStepY;   /* VDI resolution changing steps */

/**
 * Set width and height stepping for VDI resolution changing.
 * Depending on the color depth we can only change the VDI resolution
 * in certain steps:
 * - The screen width must be dividable by 16 bytes (i.e. 128 pixels in
 *   monochrome, 32 pixels in 16 color mode), or the text mode scrolling
 *   function of TOS will fail.
 * - The screen height must be a multiple of the character cell height
 *   (i.e. 16 pixels in monochrome, 8 pixels in color mode).
 */
static void DlgMonitor_SetVdiStepping(void)
{
	if (monitordlg[DLGSCRN_BPP1].state & SG_SELECTED)
	{
		nVdiStepX = 128;
		nVdiStepY = 16;
	}
	else if (monitordlg[DLGSCRN_BPP2].state & SG_SELECTED)
	{
		nVdiStepX = 64;
		nVdiStepY = 8;
	}
	else
	{
		nVdiStepX = 32;
		nVdiStepY = 8;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Show and process the monitor dialog.
 */
void Dialog_MonitorDlg(void)
{
	int but;
	unsigned int i;
	MONITORTYPE	mti;

	SDLGui_CenterDlg(monitordlg);

	/* Set up general monitor options in the dialog from actual values: */

	if (ConfigureParams.Screen.bAllowOverscan)
		monitordlg[DLGSCRN_OVERSCAN].state |= SG_SELECTED;
	else
		monitordlg[DLGSCRN_OVERSCAN].state &= ~SG_SELECTED;

	for (i = DLGSCRN_MONO; i <= DLGSCRN_TV; i++)
		monitordlg[i].state &= ~SG_SELECTED;
	monitordlg[DLGSCRN_MONO+ConfigureParams.Screen.nMonitorType].state |= SG_SELECTED;

	/* Initialize VDI resolution options: */

	if (ConfigureParams.Screen.bUseExtVdiResolutions)
		monitordlg[DLGSCRN_USEVDIRES].state |= SG_SELECTED;
	else
		monitordlg[DLGSCRN_USEVDIRES].state &= ~SG_SELECTED;
	for (i=0; i<3; i++)
		monitordlg[DLGSCRN_BPP1 + i].state &= ~SG_SELECTED;
	monitordlg[DLGSCRN_BPP1 + ConfigureParams.Screen.nVdiColors - GEMCOLOR_2].state |= SG_SELECTED;
	sprintf(sVdiWidth, "%4i", ConfigureParams.Screen.nVdiWidth);
	sprintf(sVdiHeight, "%4i", ConfigureParams.Screen.nVdiHeight);
	DlgMonitor_SetVdiStepping();

	/* The monitor dialog main loop */
	do
	{
		but = SDLGui_DoDialog(monitordlg, NULL);
		switch (but)
		{
		 case DLGSCRN_VDI_WLESS:
			ConfigureParams.Screen.nVdiWidth = VDI_Limit(ConfigureParams.Screen.nVdiWidth - nVdiStepX,
			                                nVdiStepX, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			sprintf(sVdiWidth, "%4i", ConfigureParams.Screen.nVdiWidth);
			break;
		 case DLGSCRN_VDI_WMORE:
			ConfigureParams.Screen.nVdiWidth = VDI_Limit(ConfigureParams.Screen.nVdiWidth + nVdiStepX,
			                                nVdiStepX, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			sprintf(sVdiWidth, "%4i", ConfigureParams.Screen.nVdiWidth);
			break;

		 case DLGSCRN_VDI_HLESS:
			ConfigureParams.Screen.nVdiHeight = VDI_Limit(ConfigureParams.Screen.nVdiHeight - nVdiStepY,
			                                 nVdiStepY, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiHeight, "%4i", ConfigureParams.Screen.nVdiHeight);
			break;
		 case DLGSCRN_VDI_HMORE:
			ConfigureParams.Screen.nVdiHeight = VDI_Limit(ConfigureParams.Screen.nVdiHeight + nVdiStepY,
			                                 nVdiStepY, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiHeight, "%4i", ConfigureParams.Screen.nVdiHeight);
			break;

		 case DLGSCRN_BPP1:
		 case DLGSCRN_BPP2:
		 case DLGSCRN_BPP4:
			DlgMonitor_SetVdiStepping();
			/* Align resolution to actual conditions: */
			ConfigureParams.Screen.nVdiWidth = VDI_Limit(ConfigureParams.Screen.nVdiWidth, nVdiStepX,
			                                MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			ConfigureParams.Screen.nVdiHeight = VDI_Limit(ConfigureParams.Screen.nVdiHeight, nVdiStepY,
			                                 MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiWidth, "%4i", ConfigureParams.Screen.nVdiWidth);
			sprintf(sVdiHeight, "%4i", ConfigureParams.Screen.nVdiHeight);
			break;
		}
	}
	while (but != DLGSCRN_EXIT_MONITOR && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read new values from dialog: */

	ConfigureParams.Screen.bAllowOverscan = (monitordlg[DLGSCRN_OVERSCAN].state & SG_SELECTED);

	for (mti = MONITOR_TYPE_MONO; mti <= MONITOR_TYPE_TV; mti++)
	{
		if (monitordlg[mti + DLGSCRN_MONO].state & SG_SELECTED)
		{
			ConfigureParams.Screen.nMonitorType = mti;
			break;
		}
	}

	ConfigureParams.Screen.bUseExtVdiResolutions = (monitordlg[DLGSCRN_USEVDIRES].state & SG_SELECTED);
	for (i=0; i<3; i++)
	{
		if (monitordlg[DLGSCRN_BPP1 + i].state & SG_SELECTED)
			ConfigureParams.Screen.nVdiColors = GEMCOLOR_2 + i;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Show and process the window dialog.
 */
void Dialog_WindowDlg(void)
{
	int deskw, deskh, but, skip = 0;
	unsigned int i;

	SDLGui_CenterDlg(windowdlg);

	/* Set up general window options in the dialog from actual values: */

	if (ConfigureParams.Screen.bFullScreen)
		windowdlg[DLGSCRN_FULLSCRN].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_FULLSCRN].state &= ~SG_SELECTED;

	if (ConfigureParams.Screen.bKeepResolution)
		windowdlg[DLGSCRN_KEEP_RES].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_KEEP_RES].state &= ~SG_SELECTED;

		windowdlg[DLGSCRN_STATUSBAR].state |= SG_SELECTED;

	windowdlg[DLGSCRN_STATUSBAR].state &= ~SG_SELECTED;
	windowdlg[DLGSCRN_DRIVELED].state &= ~SG_SELECTED;
	windowdlg[DLGSCRN_NONE].state &= ~SG_SELECTED;
	if (ConfigureParams.Screen.bShowStatusbar)
		windowdlg[DLGSCRN_STATUSBAR].state |= SG_SELECTED;
	else if (ConfigureParams.Screen.bShowDriveLed)
		windowdlg[DLGSCRN_DRIVELED].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_NONE].state |= SG_SELECTED;

	for (i = 0; i < ITEMS_IN_ARRAY(skip_frames); i++)
	{
		if (ConfigureParams.Screen.nFrameSkips >= skip_frames[i])
			skip = i;
		windowdlg[i+DLGSCRN_SKIP0].state &= ~SG_SELECTED;
	}
	windowdlg[DLGSCRN_SKIP0+skip].state |= SG_SELECTED;

	Resolution_GetDesktopSize(&deskw, &deskh);
	sprintf(sMaxWidth, "%4i", ConfigureParams.Screen.nMaxWidth);
	sprintf(sMaxHeight, "%4i", ConfigureParams.Screen.nMaxHeight);

	/* Initialize window capture options: */

	if (ConfigureParams.Screen.bCrop)
		windowdlg[DLGSCRN_CROP].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_CROP].state &= ~SG_SELECTED;

	if (Avi_AreWeRecording())
		windowdlg[DLGSCRN_RECANIM].txt = "Stop record";
	else
		windowdlg[DLGSCRN_RECANIM].txt = "Record AVI";

	/* The window dialog main loop */
	do
	{
		but = SDLGui_DoDialog(windowdlg, NULL);
		switch (but)
		{
		 case DLGSCRN_MAX_WLESS:
			ConfigureParams.Screen.nMaxWidth = VDI_Limit(ConfigureParams.Screen.nMaxWidth - MAX_SIZE_STEP,
			                                MAX_SIZE_STEP, MIN_VDI_WIDTH, deskw);
			sprintf(sMaxWidth, "%4i", ConfigureParams.Screen.nMaxWidth);
			break;
		 case DLGSCRN_MAX_WMORE:
			ConfigureParams.Screen.nMaxWidth = VDI_Limit(ConfigureParams.Screen.nMaxWidth + MAX_SIZE_STEP,
			                                MAX_SIZE_STEP, MIN_VDI_WIDTH, deskw);
			sprintf(sMaxWidth, "%4i", ConfigureParams.Screen.nMaxWidth);
			break;

		 case DLGSCRN_MAX_HLESS:
			ConfigureParams.Screen.nMaxHeight = VDI_Limit(ConfigureParams.Screen.nMaxHeight - MAX_SIZE_STEP,
			                                 MAX_SIZE_STEP, MIN_VDI_HEIGHT, deskh);
			sprintf(sMaxHeight, "%4i", ConfigureParams.Screen.nMaxHeight);
			break;
		 case DLGSCRN_MAX_HMORE:
			ConfigureParams.Screen.nMaxHeight = VDI_Limit(ConfigureParams.Screen.nMaxHeight + MAX_SIZE_STEP,
			                                 MAX_SIZE_STEP, MIN_VDI_HEIGHT, deskh);
			sprintf(sMaxHeight, "%4i", ConfigureParams.Screen.nMaxHeight);
			break;

		 case DLGSCRN_CAPTURE:
			SDL_UpdateRect(sdlscrn, 0,0,0,0);
			ConfigureParams.Screen.bCrop = (windowdlg[DLGSCRN_CROP].state & SG_SELECTED);
			ScreenSnapShot_SaveScreen();
			break;

		case DLGSCRN_RECANIM:
			if (Avi_AreWeRecording())
			{
				/* AVI indexing can take a while for larger files */
				Statusbar_AddMessage("Finishing AVI file...", 100);
				Statusbar_Update(sdlscrn);
				Avi_StopRecording();
				windowdlg[DLGSCRN_RECANIM].txt = "Record AVI";
				Statusbar_AddMessage("Emulation paused", 100);
				Statusbar_Update(sdlscrn);
			}
			else
			{
				ConfigureParams.Screen.bCrop = (windowdlg[DLGSCRN_CROP].state & SG_SELECTED);
				Avi_StartRecording ( ConfigureParams.Video.AviRecordFile , ConfigureParams.Screen.bCrop ,
					ConfigureParams.Video.AviRecordFps == 0 ?
						ClocksTimings_GetVBLPerSec ( ConfigureParams.System.nMachineType , nScreenRefreshRate ) :
						(Uint32)ConfigureParams.Video.AviRecordFps << CLOCKS_TIMINGS_SHIFT_VBL ,
					1 << CLOCKS_TIMINGS_SHIFT_VBL ,
					ConfigureParams.Video.AviRecordVcodec );
				windowdlg[DLGSCRN_RECANIM].txt = "Stop record";
			}
			break;
		}
	}
	while (but != DLGSCRN_EXIT_WINDOW && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read new values from dialog: */

	ConfigureParams.Screen.bFullScreen = (windowdlg[DLGSCRN_FULLSCRN].state & SG_SELECTED);
	ConfigureParams.Screen.bKeepResolution = (windowdlg[DLGSCRN_KEEP_RES].state & SG_SELECTED);

	ConfigureParams.Screen.bShowStatusbar = false;
	ConfigureParams.Screen.bShowDriveLed = false;
	if (windowdlg[DLGSCRN_STATUSBAR].state & SG_SELECTED)
		ConfigureParams.Screen.bShowStatusbar = true;
	else if (windowdlg[DLGSCRN_DRIVELED].state & SG_SELECTED)
		ConfigureParams.Screen.bShowDriveLed = true;

	for (i = DLGSCRN_SKIP0; i <= DLGSCRN_SKIP4; i++)
	{
		if (windowdlg[i].state & SG_SELECTED)
		{
			ConfigureParams.Screen.nFrameSkips = skip_frames[i-DLGSCRN_SKIP0];
			break;
		}
	}

	ConfigureParams.Screen.bCrop = (windowdlg[DLGSCRN_CROP].state & SG_SELECTED);
}
