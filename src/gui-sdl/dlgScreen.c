/*
  Hatari - dlgScreen.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
static char rcsid[] = "Hatari $Id: dlgScreen.c,v 1.1 2003-08-04 19:37:31 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "screen.h"
#include "screenSnapShot.h"


#define DLGSCRN_FULLSCRN   3
#define DLGSCRN_INTERLACE  4
#define DLGSCRN_FRAMESKIP  5
#define DLGSCRN_OVERSCAN   6
#define DLGSCRN_8BPP       7
#define DLGSCRN_COLOR      9
#define DLGSCRN_MONO       10
#define DLGSCRN_LOW320     12
#define DLGSCRN_LOW640     13
/*#define DLGSCRN_LOW800   14*/  /* 800x600 resolution button is not used anymore */
#define DLGSCRN_ONCHANGE   16
#define DLGSCRN_FPSPOPUP   18
#define DLGSCRN_CAPTURE    19
#define DLGSCRN_RECANIM    20
#define DLGSCRN_EXIT       21


/* The screen dialog: */
static SGOBJ screendlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGBOX, 0, 0, 1,1, 38,13, NULL },
  { SGTEXT, 0, 0, 13,2, 14,1, "Screen options" },
  { SGCHECKBOX, 0, 0, 4,4, 12,1, "Fullscreen" },
  { SGCHECKBOX, 0, 0, 4,5, 23,1, "Interlaced mode" },
  { SGCHECKBOX, 0, 0, 4,6, 10,1, "Frame skip" },
  { SGCHECKBOX, 0, 0, 4,7, 13,1, "Use borders" },
  { SGCHECKBOX, 0, 0, 4,8, 13,1, "Force 8 bpp" },
  { SGTEXT, 0, 0, 4,10, 8,1, "Monitor:" },
  { SGRADIOBUT, 0, 0, 18,10, 7,1, "Color" },
  { SGRADIOBUT, 0, 0, 28,10, 6,1, "Mono" },
  { SGTEXT, 0, 0, 4,12, 12,1, "ST-Low mode:" },
  { SGRADIOBUT, 0, 0, 18,12, 8,1, "Normal" },
  { SGRADIOBUT, 0, 0, 28,12, 8,1, "Zoomed" },
/* { SGRADIOBUT, 0, 0, 27,12, 9,1, "800x600" },*/ /* 800x600 resolution button is not used anymore */
  { SGBOX, 0, 0, 1,15, 38,7, NULL },
  { SGTEXT, 0, 0, 13,16, 14,1, "Screen capture" },
  { SGCHECKBOX, 0, 0, 3,18, 27,1, "Only when display changes" },
  { SGTEXT, 0, 0, 31,18, 4,1, ""/*"FPS:"*/ },
  { SGTEXT/*SGPOPUP*/, 0, 0, 36,18, 3,1, ""/*"25"*/ },
  { SGBUTTON, 0, 0, 3,20, 16,1, "Capture screen" },
  { SGBUTTON, 0, 0, 20,20, 18,1, NULL },
  { SGBUTTON, 0, 0, 10,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the screen dialog.
*/
void Dialog_ScreenDlg(void)
{
  int but, i;

  SDLGui_CenterDlg(screendlg);

  /* Set up dialog from actual values: */

  if( DialogParams.Screen.bFullScreen )
    screendlg[DLGSCRN_FULLSCRN].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_FULLSCRN].state &= ~SG_SELECTED;

  if( DialogParams.Screen.bInterlacedScreen )
    screendlg[DLGSCRN_INTERLACE].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_INTERLACE].state &= ~SG_SELECTED;

  if( DialogParams.Screen.bFrameSkip )
    screendlg[DLGSCRN_FRAMESKIP].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_FRAMESKIP].state &= ~SG_SELECTED;

  if( DialogParams.Screen.bAllowOverscan )
    screendlg[DLGSCRN_OVERSCAN].state |= SG_SELECTED;
  else
    screendlg[DLGSCRN_OVERSCAN].state &= ~SG_SELECTED;


  if( DialogParams.Screen.bUseHighRes )
  {
    screendlg[DLGSCRN_COLOR].state &= ~SG_SELECTED;
    screendlg[DLGSCRN_MONO].state |= SG_SELECTED;
  }
  else
  {
    screendlg[DLGSCRN_COLOR].state |= SG_SELECTED;
    screendlg[DLGSCRN_MONO].state &= ~SG_SELECTED;
  }

  for(i=0; i<2; i++)
    screendlg[DLGSCRN_LOW320 + i].state &= ~SG_SELECTED;

  if(DialogParams.Screen.ChosenDisplayMode <= DISPLAYMODE_16COL_FULL)
  {
    screendlg[DLGSCRN_8BPP].state |= SG_SELECTED;
    screendlg[DLGSCRN_LOW320 + DialogParams.Screen.ChosenDisplayMode].state |= SG_SELECTED;
  }
  else
  {
    screendlg[DLGSCRN_8BPP].state &= ~SG_SELECTED;
    screendlg[DLGSCRN_LOW320 + DialogParams.Screen.ChosenDisplayMode
              - DISPLAYMODE_HICOL_LOWRES].state |= SG_SELECTED;
  }

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
    but = SDLGui_DoDialog(screendlg);
    switch( but )
    {
      case DLGSCRN_FPSPOPUP:
        fprintf(stderr,"Sorry, popup menus don't work yet\n");
        break;
      case DLGSCRN_CAPTURE:
        Screen_SetFullUpdate();
        Screen_Draw();
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
  while(but!=DLGSCRN_EXIT && !bQuitProgram);

  /* Read values from dialog */
  DialogParams.Screen.bFullScreen = (screendlg[DLGSCRN_FULLSCRN].state & SG_SELECTED);
  DialogParams.Screen.bInterlacedScreen = (screendlg[DLGSCRN_INTERLACE].state & SG_SELECTED);
  DialogParams.Screen.bFrameSkip = (screendlg[DLGSCRN_FRAMESKIP].state & SG_SELECTED);
  DialogParams.Screen.bAllowOverscan = (screendlg[DLGSCRN_OVERSCAN].state & SG_SELECTED);
  DialogParams.Screen.bUseHighRes = (screendlg[DLGSCRN_MONO].state & SG_SELECTED);
  DialogParams.Screen.bCaptureChange = (screendlg[DLGSCRN_ONCHANGE].state & SG_SELECTED);

  for(i=0; i<2; i++)
  {
    if(screendlg[DLGSCRN_LOW320 + i].state & SG_SELECTED)
    {
      DialogParams.Screen.ChosenDisplayMode = DISPLAYMODE_16COL_LOWRES + i
        + ((screendlg[DLGSCRN_8BPP].state&SG_SELECTED) ? 0 : DISPLAYMODE_HICOL_LOWRES);
      break;
    }
  }

}
