/*
  Hatari - dlgScreen.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
const char DlgScreen_fileid[] = "Hatari dlgScreen.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "vdi.h"
#include "video.h"


#define DLGSCRN_FULLSCRN   3
#define DLGSCRN_OVERSCAN   4
#define DLGSCRN_ZOOMLOWRES 5
#define DLGSCRN_STATUSBAR  6
#define DLGSCRN_SKIP0      8
#define DLGSCRN_SKIP1      9
#define DLGSCRN_SKIP2      10
#define DLGSCRN_SKIP3      11
#define DLGSCRN_SKIP4      12
#define DLGSCRN_MONO       14
#define DLGSCRN_RGB        15
#define DLGSCRN_VGA        16
#define DLGSCRN_TV         17
#define DLGSCRN_USEVDIRES  19
#define DLGSCRN_WIDTHLESS  21
#define DLGSCRN_WIDTHTEXT  22
#define DLGSCRN_WIDTHMORE  23
#define DLGSCRN_HEIGHTLESS 25
#define DLGSCRN_HEIGHTTEXT 26
#define DLGSCRN_HEIGHTMORE 27
#define DLGSCRN_BPP1       29
#define DLGSCRN_BPP2       30
#define DLGSCRN_BPP4       31
#define DLGSCRN_ONCHANGE   34
#define DLGSCRN_CAPTURE    35
#define DLGSCRN_RECANIM    36
#define DLGSCRN_EXIT       37

#define ITEMS_IN_ARRAY(a) (sizeof(a)/sizeof(a[0]))

/* needs to match Frame skip values in screendlg[]! */
static const int skip_frames[] = { 0, 1, 2, 4, AUTO_FRAMESKIP_LIMIT };

/* Strings for VDI resolution width and height */
static char sVdiWidth[5];
static char sVdiHeight[5];

/* The screen dialog: */
static SGOBJ screendlg[] =
{
	{ SGBOX, 0, 0, 0,0, 50,25, NULL },

	{ SGBOX, 0, 0, 1,1, 48,7, NULL },
	{ SGTEXT, 0, 0, 18,1, 14,1, "Screen options" },
	{ SGCHECKBOX, 0, 0, 4,3, 12,1, "Fullscreen" },
	{ SGCHECKBOX, 0, 0, 4,4, 13,1, "Use borders" },
	{ SGCHECKBOX, 0, 0, 22,3, 18,1, "Zoom ST-low res." },
	{ SGCHECKBOX, 0, 0, 22,4, 13,1, "Statusbar" },
	{ SGTEXT, 0, 0, 4,6, 9,1, "Frame skip:" },
	{ SGRADIOBUT, 0, 0, 17,6, 5,1, "Off" },
	{ SGRADIOBUT, 0, 0, 24,6, 3,1, "1" },
	{ SGRADIOBUT, 0, 0, 29,6, 3,1, "2" },
	{ SGRADIOBUT, 0, 0, 35,6, 3,1, "4" },
	{ SGRADIOBUT, 0, 0, 40,6, 3,1, "Auto" },
	{ SGTEXT, 0, 0, 4,7, 8,1, "Monitor:" },
	{ SGRADIOBUT, 0, 0, 14,7, 6,1, "Mono" },
	{ SGRADIOBUT, 0, 0, 22,7, 5,1, "RGB" },
	{ SGRADIOBUT, 0, 0, 29,7, 5,1, "VGA" },
	{ SGRADIOBUT, 0, 0, 36,7, 4,1, "TV" },

	{ SGBOX, 0, 0, 1,9, 48,6, NULL },
	{ SGCHECKBOX, 0, 0, 4,10, 33,1, "Use extended GEM VDI resolution" },
	{ SGTEXT, 0, 0, 4,12, 11,1, "Resolution:" },
	{ SGBUTTON, 0, 0, 18,12, 1,1, "\x04" },     /* Arrow left */
	{ SGTEXT, 0, 0, 20,12, 4,1, sVdiWidth },
	{ SGBUTTON, 0, 0, 25,12, 1,1, "\x03" },     /* Arrow right */
	{ SGTEXT, 0, 0, 28,12, 1,1, "*" },
	{ SGBUTTON, 0, 0, 31,12, 1,1, "\x04" },     /* Arrow left */
	{ SGTEXT, 0, 0, 33,12, 4,1, sVdiHeight },
	{ SGBUTTON, 0, 0, 38,12, 1,1, "\x03" },     /* Arrow right */

	{ SGTEXT, 0, 0, 4,13, 12,1, "Color Depth:" },
	{ SGRADIOBUT, SG_EXIT, 0, 17,13, 7,1, "1 bpp" },
	{ SGRADIOBUT, SG_EXIT, 0, 26,13, 7,1, "2 bpp" },
	{ SGRADIOBUT, SG_EXIT, 0, 35,13, 7,1, "4 bpp" },

	{ SGBOX, 0, 0, 1,16, 48,6, NULL },
	{ SGTEXT, 0, 0, 18,16, 14,1, "Screen capture" },
	{ SGCHECKBOX, 0, 0, 4,18, 39,1, "Capture only when display changes" },
	{ SGBUTTON, 0, 0, 6,20, 16,1, "Capture screen" },
	{ SGBUTTON, 0, 0, 26,20, 18,1, NULL },

	{ SGBUTTON, SG_DEFAULT, 0, 15,23, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


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
static void DlgScreen_SetStepping(void)
{
	if (screendlg[DLGSCRN_BPP1].state & SG_SELECTED)
	{
		nVdiStepX = 128;
		nVdiStepY = 16;
	}
	else if (screendlg[DLGSCRN_BPP2].state & SG_SELECTED)
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
 * Show and process the screen dialog.
 */
void Dialog_ScreenDlg(void)
{
	int but, skip = 0;
	unsigned int i;

	SDLGui_CenterDlg(screendlg);

	/* Set up general screen options in the dialog from actual values: */

	if (ConfigureParams.Screen.bFullScreen)
		screendlg[DLGSCRN_FULLSCRN].state |= SG_SELECTED;
	else
		screendlg[DLGSCRN_FULLSCRN].state &= ~SG_SELECTED;

	if (ConfigureParams.Screen.bAllowOverscan)
		screendlg[DLGSCRN_OVERSCAN].state |= SG_SELECTED;
	else
		screendlg[DLGSCRN_OVERSCAN].state &= ~SG_SELECTED;

	if (ConfigureParams.Screen.bShowStatusbar)
		screendlg[DLGSCRN_STATUSBAR].state |= SG_SELECTED;
	else
		screendlg[DLGSCRN_STATUSBAR].state &= ~SG_SELECTED;

	if (ConfigureParams.Screen.bZoomLowRes)
		screendlg[DLGSCRN_ZOOMLOWRES].state |= SG_SELECTED;
	else
		screendlg[DLGSCRN_ZOOMLOWRES].state &= ~SG_SELECTED;

	for (i = 0; i < ITEMS_IN_ARRAY(skip_frames); i++)
	{
		if (ConfigureParams.Screen.nFrameSkips >= skip_frames[i])
			skip = i;
		screendlg[i+DLGSCRN_SKIP0].state &= ~SG_SELECTED;
	}
	screendlg[DLGSCRN_SKIP0+skip].state |= SG_SELECTED;

	for (i = DLGSCRN_MONO; i <= DLGSCRN_TV; i++)
		screendlg[i].state &= ~SG_SELECTED;
	screendlg[DLGSCRN_MONO+ConfigureParams.Screen.nMonitorType].state |= SG_SELECTED;

	/* Initialize VDI resolution options: */

	if (ConfigureParams.Screen.bUseExtVdiResolutions)
		screendlg[DLGSCRN_USEVDIRES].state |= SG_SELECTED;
	else
		screendlg[DLGSCRN_USEVDIRES].state &= ~SG_SELECTED;
	for (i=0; i<3; i++)
		screendlg[DLGSCRN_BPP1 + i].state &= ~SG_SELECTED;
	screendlg[DLGSCRN_BPP1 + ConfigureParams.Screen.nVdiColors - GEMCOLOR_2].state |= SG_SELECTED;
	sprintf(sVdiWidth, "%4i", ConfigureParams.Screen.nVdiWidth);
	sprintf(sVdiHeight, "%4i", ConfigureParams.Screen.nVdiHeight);
	DlgScreen_SetStepping();

	/* Initialize screen capture options: */

	if (ConfigureParams.Screen.bCaptureChange)
		screendlg[DLGSCRN_ONCHANGE].state |= SG_SELECTED;
	else
		screendlg[DLGSCRN_ONCHANGE].state &= ~SG_SELECTED;

	if (ScreenSnapShot_AreWeRecording())
		screendlg[DLGSCRN_RECANIM].txt = "Stop recording";
	else
		screendlg[DLGSCRN_RECANIM].txt = "Record animation";

	/* The screen dialog main loop */
	do
	{
		but = SDLGui_DoDialog(screendlg, NULL);
		switch (but)
		{
		 case DLGSCRN_WIDTHLESS:
			ConfigureParams.Screen.nVdiWidth = VDI_Limit(ConfigureParams.Screen.nVdiWidth - nVdiStepX,
			                                nVdiStepX, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			sprintf(sVdiWidth, "%4i", ConfigureParams.Screen.nVdiWidth);
			break;
		 case DLGSCRN_WIDTHMORE:
			ConfigureParams.Screen.nVdiWidth = VDI_Limit(ConfigureParams.Screen.nVdiWidth + nVdiStepX,
			                                nVdiStepX, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			sprintf(sVdiWidth, "%4i", ConfigureParams.Screen.nVdiWidth);
			break;

		 case DLGSCRN_HEIGHTLESS:
			ConfigureParams.Screen.nVdiHeight = VDI_Limit(ConfigureParams.Screen.nVdiHeight - nVdiStepY,
			                                 nVdiStepY, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiHeight, "%4i", ConfigureParams.Screen.nVdiHeight);
			break;
		 case DLGSCRN_HEIGHTMORE:
			ConfigureParams.Screen.nVdiHeight = VDI_Limit(ConfigureParams.Screen.nVdiHeight + nVdiStepY,
			                                 nVdiStepY, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiHeight, "%4i", ConfigureParams.Screen.nVdiHeight);
			break;

		 case DLGSCRN_BPP1:
		 case DLGSCRN_BPP2:
		 case DLGSCRN_BPP4:
			DlgScreen_SetStepping();
			/* Align resolution to actual conditions: */
			ConfigureParams.Screen.nVdiWidth = VDI_Limit(ConfigureParams.Screen.nVdiWidth, nVdiStepX,
			                                MIN_VDI_WIDTH, MAX_VDI_WIDTH);
			ConfigureParams.Screen.nVdiHeight = VDI_Limit(ConfigureParams.Screen.nVdiHeight, nVdiStepY,
			                                 MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
			sprintf(sVdiWidth, "%4i", ConfigureParams.Screen.nVdiWidth);
			sprintf(sVdiHeight, "%4i", ConfigureParams.Screen.nVdiHeight);
			break;

		 case DLGSCRN_CAPTURE:
			SDL_UpdateRect(sdlscrn, 0,0,0,0);
			ScreenSnapShot_SaveScreen();
			break;
		 case DLGSCRN_RECANIM:
			if (ScreenSnapShot_AreWeRecording())
			{
				screendlg[DLGSCRN_RECANIM].txt = "Record animation";
				ScreenSnapShot_EndRecording();
			}
			else
			{
				screendlg[DLGSCRN_RECANIM].txt = "Stop recording";
				ConfigureParams.Screen.bCaptureChange = (screendlg[DLGSCRN_ONCHANGE].state & SG_SELECTED);
				ScreenSnapShot_BeginRecording(ConfigureParams.Screen.bCaptureChange);
			}
			break;
		}
	}
	while (but != DLGSCRN_EXIT && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read new values from dialog: */

	ConfigureParams.Screen.bFullScreen = (screendlg[DLGSCRN_FULLSCRN].state & SG_SELECTED);
	ConfigureParams.Screen.bAllowOverscan = (screendlg[DLGSCRN_OVERSCAN].state & SG_SELECTED);

	if (screendlg[DLGSCRN_STATUSBAR].state & SG_SELECTED)
		ConfigureParams.Screen.bShowStatusbar = TRUE;
	else
		ConfigureParams.Screen.bShowStatusbar = FALSE;

	if (screendlg[DLGSCRN_ZOOMLOWRES].state & SG_SELECTED)
		ConfigureParams.Screen.bZoomLowRes = TRUE;
	else
		ConfigureParams.Screen.bZoomLowRes = FALSE;

	ConfigureParams.Screen.bUseExtVdiResolutions = (screendlg[DLGSCRN_USEVDIRES].state & SG_SELECTED);
	for (i = DLGSCRN_SKIP0; i <= DLGSCRN_SKIP4; i++)
	{
		if (screendlg[i].state & SG_SELECTED)
		{
			ConfigureParams.Screen.nFrameSkips = skip_frames[i-DLGSCRN_SKIP0];
			break;
		}
	}
	for (i = DLGSCRN_MONO; i <= DLGSCRN_TV; i++)
	{
		if (screendlg[i].state & SG_SELECTED)
		{
			ConfigureParams.Screen.nMonitorType = i - DLGSCRN_MONO;
			break;
		}
	}
	for (i=0; i<3; i++)
	{
		if (screendlg[DLGSCRN_BPP1 + i].state & SG_SELECTED)
			ConfigureParams.Screen.nVdiColors = GEMCOLOR_2 + i;
	}

	ConfigureParams.Screen.bCaptureChange = (screendlg[DLGSCRN_ONCHANGE].state & SG_SELECTED);
}
