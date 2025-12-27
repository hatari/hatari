/*
  Hatari - dlgScreen.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

 Atari monitor and Hatari window settings.
*/
const char DlgScreen_fileid[] = "Hatari dlgScreen.c";

#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "options.h"
#include "screen.h"
#include "vdi.h"
#include "video.h"

/* how many pixels to increment VDI mode
 * width/height on each click
 */
#define VDI_SIZE_INC	     16

/* ------------ The Monitor dialog: -------------- */

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
#define DLGSCRN_BPP8         21
#define DLGSCRN_EXIT_MONITOR 22

/* Strings for VDI resolution width and height */
#define NUM_FIELD_LEN 4
static char sVdiWidth[NUM_FIELD_LEN+1];
static char sVdiHeight[NUM_FIELD_LEN+1];

static SGOBJ monitordlg[] =
{
	{ SGBOX, 0, 0, 0,0, 34,19, NULL },

	{ SGBOX,      0, 0,  1,1, 32,6, NULL },
	{ SGTEXT,     0, 0, 10,1, 14,1, "Atari monitor" },
	{ SGRADIOBUT, 0, 0,  4,3,  6,1, "_Mono" },
	{ SGRADIOBUT, 0, 0, 12,3,  5,1, "_RGB" },
	{ SGRADIOBUT, 0, 0, 19,3,  5,1, "_VGA" },
	{ SGRADIOBUT, 0, 0, 26,3,  4,1, "_TV" },
	{ SGCHECKBOX, 0, 0, 12,5, 14,1, "Show _borders" },

	{ SGBOX,      0, 0,  1,8, 32,8, NULL },
	{ SGCHECKBOX, 0, 0,  4,9, 25,1, "Use _extended VDI screen" },
	{ SGTEXT,     0, 0,  4,11, 5,1, "Size:" },
	{ SGBUTTON,   0, 0,  6,12, 1,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGEDITFIELD,SG_EXIT, 0,  8,12, NUM_FIELD_LEN,1, sVdiWidth },
	{ SGBUTTON,   0, 0, 13,12, 1,1, "\x03", SG_SHORTCUT_RIGHT },
	{ SGTEXT,     0, 0,  4,13, 1,1, "x" },
	{ SGBUTTON,   0, 0,  6,13, 1,1, "\x04", SG_SHORTCUT_UP },
	{ SGEDITFIELD,SG_EXIT, 0,  8,13, NUM_FIELD_LEN,1, sVdiHeight },
	{ SGBUTTON,   0, 0, 13,13, 1,1, "\x03", SG_SHORTCUT_DOWN },

	{ SGRADIOBUT, SG_EXIT, 0, 18,11, 11,1, "  _2 colors" },
	{ SGRADIOBUT, SG_EXIT, 0, 18,12, 11,1, "  _4 colors" },
	{ SGRADIOBUT, SG_EXIT, 0, 18,13, 11,1, " 1_6 colors" },
	{ SGRADIOBUT, SG_EXIT, 0, 18,14, 11,1, "2_56 colors" },

	{ SGBUTTON, SG_DEFAULT, 0, 7,17, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

/* ---------------- The window dialog: -------------- */

#define DLGSCRN_FULLSCRN    3
#define DLGSCRN_STATUSBAR   5
#define DLGSCRN_DRIVELED    6
#define DLGSCRN_NONE        7
#define DLGSCRN_SKIP0       9
#define DLGSCRN_SKIP1       10
#define DLGSCRN_SKIP2       11
#define DLGSCRN_SKIP3       12
#define DLGSCRN_SKIP4       13
#define DLGSCRN_KEEP_RES    14
#define DLGSCRN_MAX_WLESS   19
#define DLGSCRN_MAX_WTEXT   20
#define DLGSCRN_MAX_WMORE   21
#define DLGSCRN_MAX_HLESS   23
#define DLGSCRN_MAX_HTEXT   24
#define DLGSCRN_MAX_HMORE   25
#define DLGSCRN_GPUSCALE    28
#define DLGSCRN_RESIZABLE   29
#define DLGSCRN_VSYNC       30
#define DLGSCRN_EXIT_WINDOW 31

/* needs to match Frame skip values in windowdlg[]! */
static const int skip_frames[] = { 0, 1, 2, 4, AUTO_FRAMESKIP_LIMIT };

/* Strings for doubled resolution max width and height */
static char sMaxWidth[NUM_FIELD_LEN+1];
static char sMaxHeight[NUM_FIELD_LEN+1];

#define MAX_SIZE_STEP 8

/* The window dialog: */
static SGOBJ windowdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 52,18, NULL },
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
#if ENABLE_SDL3
	{ SGTEXT,     0, 0, 35,3, 14,1, "" },
	{ SGTEXT,     0, 0, 35,4, 10,1, "" },
	{ SGTEXT,     0, 0, 35,5, 13,1, "" },
#else
	{ SGCHECKBOX, 0, 0, 33,3, 14,1, "_Keep desktop" },
	{ SGTEXT,     0, 0, 35,4, 10,1, "resolution" },
	{ SGTEXT,     0, 0, 35,5, 13,1, "in fullscreen" },
#endif
	{ SGTEXT,     0, 0, 33,2,  1,1, "" },
	{ SGTEXT,     0, 0, 33,7, 15,1, "Max zoomed win:" },
	{ SGBUTTON,   0, 0, 35,8,  1,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGEDITFIELD,SG_EXIT, 0, 37,8, NUM_FIELD_LEN,1, sMaxWidth },
	{ SGBUTTON,   0, 0, 43,8,  1,1, "\x03", SG_SHORTCUT_RIGHT },
	{ SGTEXT,     0, 0, 33,9,  1,1, "x" },
	{ SGBUTTON,   0, 0, 35,9,  1,1, "\x04", SG_SHORTCUT_UP },
	{ SGEDITFIELD,SG_EXIT, 0, 37,9, NUM_FIELD_LEN,1, sMaxHeight },
	{ SGBUTTON,   0, 0, 43,9,  1,1, "\x03", SG_SHORTCUT_DOWN },

	{ SGBOX,      0, 0,  1,12, 50,3, NULL },
	{ SGTEXT,     0, 0,  4,13, 12,1, "SDL2:" },
	{ SGCHECKBOX, 0, 0, 12,13, 20,1, "GPU scal_ing" },
	{ SGCHECKBOX, 0, 0, 27,13, 20,1, "Resi_zable" },
	{ SGCHECKBOX, 0, 0, 40,13, 11,1, "_VSync" },

	{ SGBUTTON, SG_DEFAULT, 0, 17,16, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

/* --------------- Monitor dialog ---------------------- */
/**
 * To be called when changing VDI mode bit-depth.
 * Sets width & height stepping for VDI resolution changing,
 * and returns number of planes. See vdi.[ch] for details.
 */
static int DlgMonitor_SetVdiStepping(int *stepx, int *stepy)
{
	int planes;
	if (monitordlg[DLGSCRN_BPP1].state & SG_SELECTED)
		planes = 1;
	else if (monitordlg[DLGSCRN_BPP2].state & SG_SELECTED)
		planes = 2;
	else if (monitordlg[DLGSCRN_BPP4].state & SG_SELECTED)
		planes = 4;
	else
		planes = 8;
	*stepx = VDI_ALIGN_WIDTH;
	*stepy = VDI_ALIGN_HEIGHT;
	return planes;
}

/*-----------------------------------------------------------------------*/
/**
 * Show and process the monitor dialog.
 */
void Dialog_MonitorDlg(void)
{
	int but, vdiw, vdih, stepx, stepy, planes;
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
	for (i=0; i < 4; i++)
		monitordlg[DLGSCRN_BPP1 + i].state &= ~SG_SELECTED;
	monitordlg[DLGSCRN_BPP1 + ConfigureParams.Screen.nVdiColors - GEMCOLOR_2].state |= SG_SELECTED;

	vdiw = ConfigureParams.Screen.nVdiWidth;
	vdih = ConfigureParams.Screen.nVdiHeight;
	planes = DlgMonitor_SetVdiStepping(&stepx, &stepy);
	assert(VDI_SIZE_INC >= stepx && VDI_SIZE_INC >= stepy);
	sprintf(sVdiWidth, "%4i", vdiw);
	sprintf(sVdiHeight, "%4i", vdih);

	/* The monitor dialog main loop */
	do
	{
		bool update = true;
		but = SDLGui_DoDialog(monitordlg);
		switch (but)
		{
		 case DLGSCRN_VDI_WLESS:
			vdiw -= VDI_SIZE_INC;
			break;
		 case DLGSCRN_VDI_WTEXT:
			vdiw = atoi(sVdiWidth);
			break;
		 case DLGSCRN_VDI_WMORE:
			vdiw += VDI_SIZE_INC;
			break;

		 case DLGSCRN_VDI_HLESS:
			vdih -= VDI_SIZE_INC;
			break;
		 case DLGSCRN_VDI_HTEXT:
			vdih = atoi(sVdiHeight);
			break;
		 case DLGSCRN_VDI_HMORE:
			vdih += VDI_SIZE_INC;
			break;

		 case DLGSCRN_BPP1:
		 case DLGSCRN_BPP2:
		 case DLGSCRN_BPP4:
		 case DLGSCRN_BPP8:
			planes = DlgMonitor_SetVdiStepping(&stepx, &stepy);
			break;

		default:
			update = false;
		}
		if (update)
		{
			/* clamp & align */
			VDI_ByteLimit(&vdiw, &vdih, planes);
			vdiw = Opt_ValueAlignMinMax(vdiw, stepx, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			vdih = Opt_ValueAlignMinMax(vdih, stepy, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiWidth, "%4i", vdiw);
			sprintf(sVdiHeight, "%4i", vdih);
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
	for (i=0; i < 4; i++)
	{
		if (monitordlg[DLGSCRN_BPP1 + i].state & SG_SELECTED)
			ConfigureParams.Screen.nVdiColors = GEMCOLOR_2 + i;
	}
}

/*-------------------- Hatari window dialog ----------------------*/

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

	Screen_GetDesktopSize(&deskw, &deskh);
	maxw = ConfigureParams.Screen.nMaxWidth;
	maxh = ConfigureParams.Screen.nMaxHeight;
	sprintf(sMaxWidth, "%4i", maxw);
	sprintf(sMaxHeight, "%4i", maxh);

	/* SDL2 options */
	if (ConfigureParams.Screen.bResizable)
		windowdlg[DLGSCRN_RESIZABLE].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_RESIZABLE].state &= ~SG_SELECTED;
	if (ConfigureParams.Screen.bUseSdlRenderer)
		windowdlg[DLGSCRN_GPUSCALE].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_GPUSCALE].state &= ~SG_SELECTED;

	if (ConfigureParams.Screen.bUseVsync)
		windowdlg[DLGSCRN_VSYNC].state |= SG_SELECTED;
	else
		windowdlg[DLGSCRN_VSYNC].state &= ~SG_SELECTED;

	/* The window dialog main loop */
	do
	{
		bool update = true;
		but = SDLGui_DoDialog(windowdlg);
		switch (but)
		{
		 case DLGSCRN_MAX_WLESS:
			maxw -= MAX_SIZE_STEP;
			break;
		 case DLGSCRN_MAX_WTEXT:
			maxw = atoi(sMaxWidth);
			break;
		 case DLGSCRN_MAX_WMORE:
			maxw += MAX_SIZE_STEP;
			break;

		 case DLGSCRN_MAX_HLESS:
			maxh -= MAX_SIZE_STEP;
			break;
		 case DLGSCRN_MAX_HTEXT:
			maxh = atoi(sMaxHeight);
			break;
		 case DLGSCRN_MAX_HMORE:
			maxh += MAX_SIZE_STEP;
			break;

		default:
			update = false;
		}
		if (update)
		{
			/* clamp & align */
			maxw = Opt_ValueAlignMinMax(maxw, MAX_SIZE_STEP, MIN_VDI_WIDTH, deskw);
			maxh = Opt_ValueAlignMinMax(maxh, MAX_SIZE_STEP, MIN_VDI_HEIGHT, deskh);
			sprintf(sMaxHeight, "%4i", maxh);
			sprintf(sMaxWidth, "%4i", maxw);
		}
	}
	while (but != DLGSCRN_EXIT_WINDOW && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read new values from dialog: */

	ConfigureParams.Screen.bFullScreen = (windowdlg[DLGSCRN_FULLSCRN].state & SG_SELECTED);
	ConfigureParams.Screen.bKeepResolution = (windowdlg[DLGSCRN_KEEP_RES].state & SG_SELECTED);

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

	ConfigureParams.Screen.bResizable = (windowdlg[DLGSCRN_RESIZABLE].state & SG_SELECTED);
	ConfigureParams.Screen.bUseSdlRenderer = (windowdlg[DLGSCRN_GPUSCALE].state & SG_SELECTED);
	ConfigureParams.Screen.bUseVsync = (windowdlg[DLGSCRN_VSYNC].state & SG_SELECTED);
}
