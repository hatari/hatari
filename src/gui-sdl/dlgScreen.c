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
#include "options.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "resolution.h"
#include "vdi.h"
#include "video.h"
#include "avi_record.h"
#include "statusbar.h"
#include "clocks_timings.h"


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
	{ SGRADIOBUT, 0, 0,  4,3,  6,1, "_Mono" },
	{ SGRADIOBUT, 0, 0, 12,3,  5,1, "_RGB" },
	{ SGRADIOBUT, 0, 0, 19,3,  5,1, "_VGA" },
	{ SGRADIOBUT, 0, 0, 26,3,  4,1, "_TV" },
	{ SGCHECKBOX, 0, 0, 12,5, 14,1, "Show _borders" },

	{ SGBOX,      0, 0,  1,8, 32,7, NULL },
	{ SGCHECKBOX, 0, 0,  4,9, 25,1, "Use _extended VDI screen" },
	{ SGTEXT,     0, 0,  4,11, 5,1, "Size:" },
	{ SGBUTTON,   0, 0,  6,12, 1,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGTEXT,     0, 0,  8,12, 4,1, sVdiWidth },
	{ SGBUTTON,   0, 0, 13,12, 1,1, "\x03", SG_SHORTCUT_RIGHT },
	{ SGTEXT,     0, 0,  4,13, 1,1, "x" },
	{ SGBUTTON,   0, 0,  6,13, 1,1, "\x04", SG_SHORTCUT_UP },
	{ SGTEXT,     0, 0,  8,13, 4,1, sVdiHeight },
	{ SGBUTTON,   0, 0, 13,13, 1,1, "\x03", SG_SHORTCUT_DOWN },

	{ SGRADIOBUT, SG_EXIT, 0, 18,11, 11,1, " _2 colors" },
	{ SGRADIOBUT, SG_EXIT, 0, 18,12, 11,1, " _4 colors" },
	{ SGRADIOBUT, SG_EXIT, 0, 18,13, 11,1, "1_6 colors" },

	{ SGBUTTON, SG_DEFAULT, 0, 7,16, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/* The window dialog: */
#define DLGSCRN_FULLSCRN    3
#define DLGSCRN_STATUSBAR   5
#define DLGSCRN_DRIVELED    6
#define DLGSCRN_NONE        7
#define DLGSCRN_SKIP0       9
#define DLGSCRN_SKIP1       10
#define DLGSCRN_SKIP2       11
#define DLGSCRN_SKIP3       12
#define DLGSCRN_SKIP4       13
#define DLGSCRN_KEEP_RES_ST 16
#define DLGSCRN_KEEP_RES    17
#define DLGSCRN_MAX_WLESS   19
#define DLGSCRN_MAX_WTEXT   20
#define DLGSCRN_MAX_WMORE   21
#define DLGSCRN_MAX_HLESS   23
#define DLGSCRN_MAX_HTEXT   24
#define DLGSCRN_MAX_HMORE   25
#define DLGSCRN_CROP        28
#define DLGSCRN_CAPTURE     29
#define DLGSCRN_RECANIM     30
#if WITH_SDL2
#define DLGSCRN_LINEARSCALE 33
#define DLGSCRN_VSYNC       34
#define DLGSCRN_EXIT_WINDOW 35
#else
#define DLGSCRN_EXIT_WINDOW 31
#endif

/* needs to match Frame skip values in windowdlg[]! */
static const int skip_frames[] = { 0, 1, 2, 4, AUTO_FRAMESKIP_LIMIT };

/* Strings for doubled resolution max width and height */
static char sMaxWidth[5];
static char sMaxHeight[5];

#define MAX_SIZE_STEP 8

/* The window dialog: */
static SGOBJ windowdlg[] =
{
#if WITH_SDL2
	{ SGBOX, 0, 0, 0,0, 52,25, NULL },
#else
	{ SGBOX, 0, 0, 0,0, 52,20, NULL },
#endif
	{ SGBOX,      0, 0,  1,1, 50,10, NULL },
	{ SGTEXT,     0, 0,  4,2, 20,1, "Hatari screen options" },
	{ SGCHECKBOX, 0, 0,  4,4, 12,1, "_Fullscreen" },
	{ SGTEXT,     0, 0,  4,6, 12,1, "Indicators:" },
	{ SGRADIOBUT, 0, 0,  6,7, 11,1, "Status_bar" },
	{ SGRADIOBUT, 0, 0,  6,8, 11,1, "Drive _led" },
	{ SGRADIOBUT, 0, 0,  6,9,  6,1, "_None" },
	{ SGTEXT,     0, 0, 19,4, 12,1, "Frame skip:" },
	{ SGRADIOBUT, 0, 0, 21,5,  5,1, "_Off" },
	{ SGRADIOBUT, 0, 0, 21,6,  3,1, "_1" },
	{ SGRADIOBUT, 0, 0, 21,7,  3,1, "_2" },
	{ SGRADIOBUT, 0, 0, 21,8,  3,1, "_4" },
	{ SGRADIOBUT, 0, 0, 21,9,  6,1, "_Auto" },
#if WITH_SDL2
	{ SGTEXT,     0, 0, 35,4, 10,1, "resolution" },
	{ SGTEXT,     0, 0, 35,5, 13,1, "in fullscreen" },
	{ SGTEXT,     0, 0, 33,2,  1,1, "" },
	{ SGCHECKBOX, 0, 0, 33,3, 14,1, "_Keep desktop" },
#else
	{ SGTEXT,     0, 0, 33,2, 14,1, "Keep desktop" },
	{ SGTEXT,     0, 0, 33,3, 14,1, "resolution:" },
	{ SGCHECKBOX, 0, 0, 35,4,  8,1, "ST/ST_e" },
	{ SGCHECKBOX, 0, 0, 35,5, 11,1, "_TT/Falcon" },
#endif
	{ SGTEXT,     0, 0, 33,7, 15,1, "Max zoomed win:" },
	{ SGBUTTON,   0, 0, 35,8,  1,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGTEXT,     0, 0, 37,8,  4,1, sMaxWidth },
	{ SGBUTTON,   0, 0, 43,8,  1,1, "\x03", SG_SHORTCUT_RIGHT },
	{ SGTEXT,     0, 0, 33,9,  1,1, "x" },
	{ SGBUTTON,   0, 0, 35,9,  1,1, "\x04", SG_SHORTCUT_UP },
	{ SGTEXT,     0, 0, 37,9,  4,1, sMaxHeight },
	{ SGBUTTON,   0, 0, 43,9,  1,1, "\x03", SG_SHORTCUT_DOWN },

	{ SGBOX,      0, 0,  1,12, 50,5, NULL },
	{ SGTEXT,     0, 0,  7,13, 16,1, "Screen capture" },
	{ SGCHECKBOX, 0, 0,  8,15, 16,1, "_Crop statusbar" },
	{ SGBUTTON,   0, 0, 29,13, 14,1, " _Screenshot " },
	{ SGBUTTON,   0, 0, 29,15, 14,1, NULL },      /* Record text set later */

#if WITH_SDL2
	{ SGBOX,      0, 0,  1,18, 50,4, NULL },
	{ SGTEXT,     0, 0, 20,18, 12,1, "SDL2 options" },
	{ SGCHECKBOX, 0, 0,  4,20, 20,1, "Use linear scal_ing" },
	{ SGCHECKBOX, 0, 0, 28,20, 11,1, "Use _VSync" },
	{ SGBUTTON, SG_DEFAULT, 0, 17,23, 20,1, "Back to main menu" },
#else
	{ SGBUTTON, SG_DEFAULT, 0, 17,18, 20,1, "Back to main menu" },
#endif
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

/* for record button */
#define RECORD_START "_Record AVI"
#define RECORD_STOP "Stop _record"


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
	int but, vdiw, vdih;
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

	vdiw = ConfigureParams.Screen.nVdiWidth;
	vdih = ConfigureParams.Screen.nVdiHeight;
	sprintf(sVdiWidth, "%4i", vdiw);
	sprintf(sVdiHeight, "%4i", vdih);
	DlgMonitor_SetVdiStepping();

	/* The monitor dialog main loop */
	do
	{
		but = SDLGui_DoDialog(monitordlg, NULL, false);
		switch (but)
		{
		 case DLGSCRN_VDI_WLESS:
			vdiw = Opt_ValueAlignMinMax(vdiw - nVdiStepX, nVdiStepX, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			sprintf(sVdiWidth, "%4i", vdiw);
			break;
		 case DLGSCRN_VDI_WMORE:
			vdiw = Opt_ValueAlignMinMax(vdiw + nVdiStepX, nVdiStepX, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			sprintf(sVdiWidth, "%4i", vdiw);
			break;

		 case DLGSCRN_VDI_HLESS:
			vdih = Opt_ValueAlignMinMax(vdih - nVdiStepY, nVdiStepY, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiHeight, "%4i", vdih);
			break;
		 case DLGSCRN_VDI_HMORE:
			vdih = Opt_ValueAlignMinMax(vdih + nVdiStepY, nVdiStepY, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiHeight, "%4i", vdih);
			break;

		 case DLGSCRN_BPP1:
		 case DLGSCRN_BPP2:
		 case DLGSCRN_BPP4:
			DlgMonitor_SetVdiStepping();
			/* Align resolution to actual conditions: */
			vdiw = Opt_ValueAlignMinMax(vdiw, nVdiStepX, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			vdih = Opt_ValueAlignMinMax(vdih, nVdiStepY, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiWidth, "%4i", vdiw);
			sprintf(sVdiHeight, "%4i", vdih);
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
	ConfigureParams.Screen.nVdiWidth = vdiw;
	ConfigureParams.Screen.nVdiHeight = vdih;

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
	int maxw, maxh, deskw, deskh, but, skip = 0;
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
#if !WITH_SDL2
	if (ConfigureParams.Screen.bKeepResolutionST)
		windowdlg[DLGSCRN_KEEP_RES_ST].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_KEEP_RES_ST].state &= ~SG_SELECTED;
#endif

	windowdlg[DLGSCRN_STATUSBAR].state &= ~SG_SELECTED;
	windowdlg[DLGSCRN_DRIVELED].state &= ~SG_SELECTED;
	windowdlg[DLGSCRN_NONE].state &= ~SG_SELECTED;
	if (ConfigureParams.Screen.bShowStatusbar)
		windowdlg[DLGSCRN_STATUSBAR].state |= SG_SELECTED;
	else if (ConfigureParams.Screen.bShowDriveLed)
		windowdlg[DLGSCRN_DRIVELED].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_NONE].state |= SG_SELECTED;

	for (i = 0; i < ARRAY_SIZE(skip_frames); i++)
	{
		if (ConfigureParams.Screen.nFrameSkips >= skip_frames[i])
			skip = i;
		windowdlg[i+DLGSCRN_SKIP0].state &= ~SG_SELECTED;
	}
	windowdlg[DLGSCRN_SKIP0+skip].state |= SG_SELECTED;

	Resolution_GetDesktopSize(&deskw, &deskh);
	maxw = ConfigureParams.Screen.nMaxWidth;
	maxh = ConfigureParams.Screen.nMaxHeight;
	sprintf(sMaxWidth, "%4i", maxw);
	sprintf(sMaxHeight, "%4i", maxh);

	/* Initialize window capture options: */

	if (ConfigureParams.Screen.bCrop)
		windowdlg[DLGSCRN_CROP].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_CROP].state &= ~SG_SELECTED;

	if (Avi_AreWeRecording())
		windowdlg[DLGSCRN_RECANIM].txt = RECORD_STOP;
	else
		windowdlg[DLGSCRN_RECANIM].txt = RECORD_START;

#if WITH_SDL2
	/* SDL2 options */
	if (ConfigureParams.Screen.nRenderScaleQuality)
		windowdlg[DLGSCRN_LINEARSCALE].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_LINEARSCALE].state &= ~SG_SELECTED;

	if (ConfigureParams.Screen.bUseVsync)
		windowdlg[DLGSCRN_VSYNC].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_VSYNC].state &= ~SG_SELECTED;
#endif

	/* The window dialog main loop */
	do
	{
		but = SDLGui_DoDialog(windowdlg, NULL, false);
		switch (but)
		{
		 case DLGSCRN_MAX_WLESS:
			maxw = Opt_ValueAlignMinMax(maxw - MAX_SIZE_STEP, MAX_SIZE_STEP, MIN_VDI_WIDTH, deskw);
			sprintf(sMaxWidth, "%4i", maxw);
			break;
		 case DLGSCRN_MAX_WMORE:
			maxw = Opt_ValueAlignMinMax(maxw + MAX_SIZE_STEP, MAX_SIZE_STEP, MIN_VDI_WIDTH, deskw);
			sprintf(sMaxWidth, "%4i", maxw);
			break;

		 case DLGSCRN_MAX_HLESS:
			maxh = Opt_ValueAlignMinMax(maxh - MAX_SIZE_STEP, MAX_SIZE_STEP, MIN_VDI_HEIGHT, deskh);
			sprintf(sMaxHeight, "%4i", maxh);
			break;
		 case DLGSCRN_MAX_HMORE:
			maxh = Opt_ValueAlignMinMax(maxh + MAX_SIZE_STEP, MAX_SIZE_STEP, MIN_VDI_HEIGHT, deskh);
			sprintf(sMaxHeight, "%4i", maxh);
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
				Statusbar_Update(sdlscrn, true);
				Avi_StopRecording();
				windowdlg[DLGSCRN_RECANIM].txt = RECORD_START;
				Statusbar_AddMessage("Emulation paused", 100);
				Statusbar_Update(sdlscrn, true);
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
				windowdlg[DLGSCRN_RECANIM].txt = RECORD_STOP;
			}
			break;
		}
	}
	while (but != DLGSCRN_EXIT_WINDOW && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read new values from dialog: */

	ConfigureParams.Screen.bFullScreen = (windowdlg[DLGSCRN_FULLSCRN].state & SG_SELECTED);
	ConfigureParams.Screen.bKeepResolution = (windowdlg[DLGSCRN_KEEP_RES].state & SG_SELECTED);
#if !WITH_SDL2
	ConfigureParams.Screen.bKeepResolutionST = (windowdlg[DLGSCRN_KEEP_RES_ST].state & SG_SELECTED);
#endif

	ConfigureParams.Screen.nMaxWidth = maxw;
	ConfigureParams.Screen.nMaxHeight = maxh;

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

#if WITH_SDL2
	ConfigureParams.Screen.nRenderScaleQuality = (windowdlg[DLGSCRN_LINEARSCALE].state & SG_SELECTED) ? 1 : 0;
	ConfigureParams.Screen.bUseVsync = (windowdlg[DLGSCRN_VSYNC].state & SG_SELECTED);
#endif
}
