/*
  Hatari

  Screen Snapshot
*/

#include "main.h"
#include "misc.h"
#include "pcx.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "statusBar.h"
#include "video.h"
#include "vdi.h"

//#define ALLOW_SCREEN_GRABS  /* FIXME */

int nScreenShots=0;                  // Number of screen shots saved
BOOL bRecordingAnimation=FALSE;      // Recording animation?
BOOL bGrabWhenChange;
int GrabFrameCounter,GrabFrameLatch;

//-----------------------------------------------------------------------
/*
  Check if we have pressed PrintScreen
*/
void ScreenSnapShot_CheckPrintKey(void)
{
#ifdef ALLOW_SCREEN_GRABS
  // Did press Print Screen key?
  if (GetAsyncKeyState(VK_SNAPSHOT)&0x0001) {  // Print Key pressed(not held)
    // Save our screen
    ScreenSnapShot_SaveScreen();
  }
#endif  //ALLOW_SCREEN_GRABS
}

//-----------------------------------------------------------------------
/*
  Save screen shot out .PCX file with filename 'grab0000.pcx','grab0001.pcx'....
*/
void ScreenSnapShot_SaveScreen(void)
{
  char szFileName[MAX_FILENAME_LENGTH];

  // Only do when NOT in full screen and NOT VDI resolution
  if (!bInFullScreen && !bUseVDIRes) {
    // Create our filename
    sprintf(szFileName,"%s/grab%4.4d.pcx",szWorkingDir,nScreenShots);
    nScreenShots++;
    // And save as 1/24-bit PCX
    if (bUseHighRes)
      PCX_SaveScreenShot_Mono(szFileName);
    else
      PCX_SaveScreenShot(szFileName);
  }
}

//-----------------------------------------------------------------------
/*
  Are we recording an animation?
*/
BOOL ScreenSnapShot_AreWeRecording(void)
{
  return(bRecordingAnimation);
}

/*-----------------------------------------------------------------------*/
/*
  Start recording animation
*/
void ScreenSnapShot_BeginRecording(BOOL bCaptureChange, int nFramesPerSecond)
{
  /* Set in globals */
  bGrabWhenChange = bCaptureChange;
  /* Set animation timer rate */
  GrabFrameCounter = 0;
  GrabFrameLatch = (int)(50.0f/(float)nFramesPerSecond);
  /* Start animation */
  bRecordingAnimation = TRUE;
  /* Set status bar */
  StatusBar_SetIcon(STATUS_ICON_SCREEN,ICONSTATE_ON);
  /* And inform user */
  Main_Message("Screen-Shot recording started.",PROG_NAME /*,MB_OK|MB_ICONINFORMATION*/);
}

/*-----------------------------------------------------------------------*/
/*
  Stop recording animation
*/
void ScreenSnapShot_EndRecording()
{
  /* Were we recording? */
  if (bRecordingAnimation) {
    /* Stop animation */
    bRecordingAnimation = FALSE;
    /* Turn off icon */
    StatusBar_SetIcon(STATUS_ICON_SCREEN,ICONSTATE_OFF);
    /* And inform user */
    Main_Message("Screen-Shot recording stopped.",PROG_NAME /*,MB_OK|MB_ICONINFORMATION*/);
  }
}

//-----------------------------------------------------------------------
/*
  Recording animation frame
*/
void ScreenSnapShot_RecordFrame(BOOL bFrameChanged)
{
  // As we recording? And running in a Window
  if (bRecordingAnimation && !bInFullScreen) {
    // Yes, but on a change basis or a timer?
    if (bGrabWhenChange) {
      // On change, so did change this frame?
      if (bFrameChanged)
        ScreenSnapShot_SaveScreen();
    }
    else {
      // On timer, check for latch and save
      GrabFrameCounter++;
      if (GrabFrameCounter>=GrabFrameLatch) {
        ScreenSnapShot_SaveScreen();
        GrabFrameCounter = 0;
      }
    }
  }
}
