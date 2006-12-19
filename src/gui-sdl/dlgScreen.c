/*
  Hatari - dlgScreen.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
const char DlgScreen_rcsid[] = "Hatari $Id: dlgScreen.c,v 1.9 2006-12-19 10:55:34 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "vdi.h"


#define DLGSCRN_FULLSCRN   3
#define DLGSCRN_FRAMESKIP  4
#define DLGSCRN_OVERSCAN   5
#define DLGSCRN_INTERLEAVE 6
#define DLGSCRN_8BPP       7
#define DLGSCRN_ZOOMLOWRES 8
#define DLGSCRN_MONO       10
#define DLGSCRN_RGB        11
#define DLGSCRN_VGA        12
#define DLGSCRN_TV         13
#define DLGSCRN_USEVDIRES  15
#define DLGSCRN_RES640     17
#define DLGSCRN_RES800     18
#define DLGSCRN_RES1024    19
#define DLGSCRN_BPP1       21
#define DLGSCRN_BPP2       22
#define DLGSCRN_BPP4       23
#define DLGSCRN_ONCHANGE   26
#define DLGSCRN_FPSPOPUP   28
#define DLGSCRN_CAPTURE    29
#define DLGSCRN_RECANIM    30
#define DLGSCRN_EXIT       31


/* The screen dialog: */
static SGOBJ screendlg[] =
{
  { SGBOX, 0, 0, 0,0, 50,25, NULL },

  { SGBOX, 0, 0, 1,1, 48,7, NULL },
  { SGTEXT, 0, 0, 18,1, 14,1, "Screen options" },
  { SGCHECKBOX, 0, 0, 4,3, 12,1, "Fullscreen" },
  { SGCHECKBOX, 0, 0, 4,4, 12,1, "Frame skips" },
  { SGCHECKBOX, 0, 0, 4,5, 13,1, "Use borders" },
  { SGCHECKBOX, 0, 0, 25,3, 18,1, "Interleaved mode" },
  { SGCHECKBOX, 0, 0, 25,4, 13,1, "Force 8 bpp" },
  { SGCHECKBOX, 0, 0, 25,5, 18,1, "Zoom ST-low res." },
  { SGTEXT, 0, 0, 4,7, 8,1, "Monitor:" },
  { SGRADIOBUT, 0, 0, 14,7, 6,1, "Mono" },
  { SGRADIOBUT, 0, 0, 22,7, 5,1, "RGB" },
  { SGRADIOBUT, 0, 0, 29,7, 5,1, "VGA" },
  { SGRADIOBUT, 0, 0, 36,7, 4,1, "TV" },

  { SGBOX, 0, 0, 1,9, 48,6, NULL },
  { SGCHECKBOX, 0, 0, 2,10, 33,1, "Use extended GEM VDI resolution" },
  { SGTEXT, 0, 0, 2,12, 11,1, "Resolution:" },
  { SGRADIOBUT, 0, 0, 15,12, 9,1, "640x480" },
  { SGRADIOBUT, 0, 0, 26,12, 9,1, "800x600" },
  { SGRADIOBUT, 0, 0, 37,12, 10,1, "1024x768" },
  { SGTEXT, 0, 0, 2,13, 12,1, "Color Depth:" },
  { SGRADIOBUT, 0, 0, 17,13, 7,1, "1 bpp" },
  { SGRADIOBUT, 0, 0, 26,13, 7,1, "2 bpp" },
  { SGRADIOBUT, 0, 0, 35,13, 7,1, "4 bpp" },

  { SGBOX, 0, 0, 1,16, 48,6, NULL },
  { SGTEXT, 0, 0, 18,16, 14,1, "Screen capture" },
  { SGCHECKBOX, 0, 0, 4,18, 39,1, "Capture only when display changes" },
  { SGTEXT, 0, 0, 31,19, 4,1, ""/*"FPS:"*/ },
  { SGTEXT/*SGPOPUP*/, 0, 0, 36,19, 3,1, ""/*"25"*/ },
  { SGBUTTON, 0, 0, 6,20, 16,1, "Capture screen" },
  { SGBUTTON, 0, 0, 26,20, 18,1, NULL },

  { SGBUTTON, 0, 0, 15,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the screen dialog.
*/
void Dialog_ScreenDlg(void)
{
  int but;
  int i;

  SDLGui_CenterDlg(screendlg);

  /* Set up general screen options in the dialog from actual values: */

  if (DialogParams.Screen.bFullScreen)
    screendlg[DLGSCRN_FULLSCRN].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_FULLSCRN].state &= ~SG_SELECTED;

  if (DialogParams.Screen.FrameSkips)
    screendlg[DLGSCRN_FRAMESKIP].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_FRAMESKIP].state &= ~SG_SELECTED;

  if (DialogParams.Screen.bAllowOverscan)
    screendlg[DLGSCRN_OVERSCAN].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_OVERSCAN].state &= ~SG_SELECTED;

  if (DialogParams.Screen.bInterleavedScreen)
    screendlg[DLGSCRN_INTERLEAVE].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_INTERLEAVE].state &= ~SG_SELECTED;

  if (DialogParams.Screen.bForce8Bpp)
    screendlg[DLGSCRN_8BPP].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_8BPP].state &= ~SG_SELECTED;

  if (DialogParams.Screen.bZoomLowRes)
    screendlg[DLGSCRN_ZOOMLOWRES].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_ZOOMLOWRES].state &= ~SG_SELECTED;

  for (i = DLGSCRN_MONO; i <= DLGSCRN_TV; i++)
    screendlg[i].state &= ~SG_SELECTED;
  screendlg[DLGSCRN_MONO+DialogParams.Screen.MonitorType].state |= SG_SELECTED;

  /* Initialize VDI resolution options: */

  if (DialogParams.Screen.bUseExtVdiResolutions)
    screendlg[DLGSCRN_USEVDIRES].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_USEVDIRES].state &= ~SG_SELECTED;

  for (i=0; i<3; i++)
  {
    screendlg[DLGSCRN_RES640 + i].state &= ~SG_SELECTED;
    screendlg[DLGSCRN_BPP1 + i].state &= ~SG_SELECTED;
  }
  screendlg[DLGSCRN_RES640 + DialogParams.Screen.nVdiResolution - GEMRES_640x480].state |= SG_SELECTED;
  screendlg[DLGSCRN_BPP1 + DialogParams.Screen.nVdiColors - GEMCOLOUR_2].state |= SG_SELECTED;

  /* Initialize screen capture options: */

  if( DialogParams.Screen.bCaptureChange )
    screendlg[DLGSCRN_ONCHANGE].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_ONCHANGE].state &= ~SG_SELECTED;

  if( ScreenSnapShot_AreWeRecording() )
    screendlg[DLGSCRN_RECANIM].txt = "Stop recording";
  else
    screendlg[DLGSCRN_RECANIM].txt = "Record animation";

  /* The screen dialog main loop */
  do
  {
    but = SDLGui_DoDialog(screendlg, NULL);
    switch( but )
    {
      case DLGSCRN_FPSPOPUP:
        fprintf(stderr,"Sorry, popup menus don't work yet\n");
        break;
      case DLGSCRN_CAPTURE:
        SDL_UpdateRect(sdlscrn, 0,0,0,0);
        ScreenSnapShot_SaveScreen();
        break;
      case DLGSCRN_RECANIM:
        if( ScreenSnapShot_AreWeRecording() )
        {
          screendlg[DLGSCRN_RECANIM].txt = "Record animation";
          ScreenSnapShot_EndRecording();
        }
        else
        {
          screendlg[DLGSCRN_RECANIM].txt = "Stop recording";
          DialogParams.Screen.bCaptureChange = (screendlg[DLGSCRN_ONCHANGE].state & SG_SELECTED);
          ScreenSnapShot_BeginRecording(DialogParams.Screen.bCaptureChange, 25);
        }
        break;
    }
  }
  while (but != DLGSCRN_EXIT && but != SDLGUI_QUIT
         && but != SDLGUI_ERROR && !bQuitProgram);

  /* Read new values from dialog: */

  DialogParams.Screen.bFullScreen = (screendlg[DLGSCRN_FULLSCRN].state & SG_SELECTED);
  DialogParams.Screen.bAllowOverscan = (screendlg[DLGSCRN_OVERSCAN].state & SG_SELECTED);
  DialogParams.Screen.bInterleavedScreen = (screendlg[DLGSCRN_INTERLEAVE].state & SG_SELECTED);

  if (screendlg[DLGSCRN_FRAMESKIP].state & SG_SELECTED)
    DialogParams.Screen.FrameSkips = 2;
  else
    DialogParams.Screen.FrameSkips = 1;

  if (screendlg[DLGSCRN_8BPP].state & SG_SELECTED)
    DialogParams.Screen.bForce8Bpp = TRUE;
  else
    DialogParams.Screen.bForce8Bpp = FALSE;

  if (screendlg[DLGSCRN_ZOOMLOWRES].state & SG_SELECTED)
    DialogParams.Screen.bZoomLowRes = TRUE;
  else
    DialogParams.Screen.bZoomLowRes = FALSE;

  DialogParams.Screen.bUseExtVdiResolutions = (screendlg[DLGSCRN_USEVDIRES].state & SG_SELECTED);
  for (i = DLGSCRN_MONO; i <= DLGSCRN_TV; i++)
  {
    if (screendlg[i].state & SG_SELECTED)
    {
      DialogParams.Screen.MonitorType = i - DLGSCRN_MONO;
      break;
    }
  }
  for (i=0; i<3; i++)
  {
    if(screendlg[DLGSCRN_RES640 + i].state & SG_SELECTED)
      DialogParams.Screen.nVdiResolution = GEMRES_640x480 + i;
    if(screendlg[DLGSCRN_BPP1 + i].state & SG_SELECTED)
      DialogParams.Screen.nVdiColors = GEMCOLOUR_2 + i;
  }

  DialogParams.Screen.bCaptureChange = (screendlg[DLGSCRN_ONCHANGE].state & SG_SELECTED);
}
