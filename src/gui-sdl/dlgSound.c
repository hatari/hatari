/*
  Hatari - dlgSound.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
static char rcsid[] = "Hatari $Id: dlgSound.c,v 1.1 2003-08-04 19:37:31 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "sound.h"


#define DLGSOUND_ENABLE  3
#define DLGSOUND_LOW     5
#define DLGSOUND_MEDIUM  6
#define DLGSOUND_HIGH    7
#define DLGSOUND_YM      10
#define DLGSOUND_WAV     11
#define DLGSOUND_RECORD  12
#define DLGSOUND_EXIT    13


/* The sound dialog: */
SGOBJ sounddlg[] =
{
  { SGBOX, 0, 0, 0,0, 38,24, NULL },
  { SGBOX, 0, 0, 1,1, 36,11, NULL },
  { SGTEXT, 0, 0, 13,2, 13,1, "Sound options" },
  { SGCHECKBOX, 0, 0, 12,4, 14,1, "Enable sound" },
  { SGTEXT, 0, 0, 11,6, 14,1, "Playback quality:" },
  { SGRADIOBUT, 0, 0, 12,8, 15,1, "Low (11kHz)" },
  { SGRADIOBUT, 0, 0, 12,9, 19,1, "Medium (22kHz)" },
  { SGRADIOBUT, 0, 0, 12,10, 14,1, "High (44kHz)" },
  { SGBOX, 0, 0, 1,13, 36,7, NULL },
  { SGTEXT, 0, 0, 13,14, 14,1, "Capture YM/WAV" },
  { SGRADIOBUT, 0, SG_SELECTED, 7,16, 11,1, "hatari.ym" },
  { SGRADIOBUT, 0, 0, 20,16, 12,1, "hatari.wav" },
  { SGBUTTON, 0, 0, 12,18, 16,1, NULL },
  { SGBUTTON, 0, 0, 10,22, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the sound dialog.
*/
void Dialog_SoundDlg(void)
{
  int but;

  SDLGui_CenterDlg(sounddlg);

  /* Set up dialog from actual values: */

  if( DialogParams.Sound.bEnableSound )
    sounddlg[DLGSOUND_ENABLE].state |= SG_SELECTED;
  else
    sounddlg[DLGSOUND_ENABLE].state &= ~SG_SELECTED;

  sounddlg[DLGSOUND_LOW].state &= ~SG_SELECTED;
  sounddlg[DLGSOUND_MEDIUM].state &= ~SG_SELECTED;
  sounddlg[DLGSOUND_HIGH].state &= ~SG_SELECTED;
  if(DialogParams.Sound.nPlaybackQuality == PLAYBACK_LOW)
    sounddlg[DLGSOUND_LOW].state |= SG_SELECTED;
  else if(DialogParams.Sound.nPlaybackQuality == PLAYBACK_MEDIUM)
    sounddlg[DLGSOUND_MEDIUM].state |= SG_SELECTED;
  else
    sounddlg[DLGSOUND_HIGH].state |= SG_SELECTED;

  if( Sound_AreWeRecording() )
    sounddlg[DLGSOUND_RECORD].txt = "Stop recording";
  else
    sounddlg[DLGSOUND_RECORD].txt = "Record sound";

  /* The sound dialog main loop */
  do
  {
    but = SDLGui_DoDialog(sounddlg);
    if(but == DLGSOUND_RECORD)
    {
      if(Sound_AreWeRecording())
      {
        sounddlg[DLGSOUND_RECORD].txt = "Record sound";
        Sound_EndRecording();
      }
      else
      {
        sounddlg[DLGSOUND_RECORD].txt = "Stop recording";
        if(sounddlg[DLGSOUND_YM].state & SG_SELECTED)
        {
          strcpy(DialogParams.Sound.szYMCaptureFileName, "hatari.ym");
          Sound_BeginRecording("hatari.ym");
        }
        else
        {
          Sound_BeginRecording("hatari.wav");
        }
      }
    }
  }
  while( but!=DLGSOUND_EXIT && !bQuitProgram );

  /* Read values from dialog */
  DialogParams.Sound.bEnableSound = (sounddlg[DLGSOUND_ENABLE].state & SG_SELECTED);
  if( sounddlg[DLGSOUND_LOW].state & SG_SELECTED )
    DialogParams.Sound.nPlaybackQuality = PLAYBACK_LOW;
  else if( sounddlg[DLGSOUND_MEDIUM].state & SG_SELECTED )
    DialogParams.Sound.nPlaybackQuality = PLAYBACK_MEDIUM;
  else
    DialogParams.Sound.nPlaybackQuality = PLAYBACK_HIGH;

}
