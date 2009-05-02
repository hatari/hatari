/*
  Hatari - dlgSound.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
const char DlgSound_fileid[] = "Hatari dlgSound.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "sound.h"


#define DLGSOUND_ENABLE     3
#define DLGSOUND_11KHZ      5
#define DLGSOUND_12KHZ      6
#define DLGSOUND_16KHZ      7
#define DLGSOUND_22KHZ      8
#define DLGSOUND_25KHZ      9
#define DLGSOUND_32KHZ      10
#define DLGSOUND_44KHZ      11
#define DLGSOUND_48KHZ      12
#define DLGSOUND_50KHZ      13
#define DLGSOUND_RECNAME    17
#define DLGSOUND_RECBROWSE  18
#define DLGSOUND_RECORD     19
#define DLGSOUND_EXIT       20


static char dlgRecordName[35];


/* The sound dialog: */
static SGOBJ sounddlg[] =
{
	{ SGBOX, 0, 0, 0,0, 38,24, NULL },
	{ SGBOX, 0, 0, 1,1, 36,11, NULL },
	{ SGTEXT, 0, 0, 13,2, 13,1, "Sound options" },
	{ SGCHECKBOX, 0, 0, 12,4, 14,1, "Enable sound" },

	{ SGTEXT, 0, 0, 11,6, 14,1, "Playback quality:" },
	{ SGRADIOBUT, 0, 0, 2,8, 10,1, "11025 Hz" },
	{ SGRADIOBUT, 0, 0, 2,9, 10,1, "12517 Hz" },
	{ SGRADIOBUT, 0, 0, 2,10, 10,1, "16000 Hz" },
	{ SGRADIOBUT, 0, 0, 14,8, 10,1, "22050 Hz" },
	{ SGRADIOBUT, 0, 0, 14,9, 10,1, "25033 Hz" },
	{ SGRADIOBUT, 0, 0, 14,10, 10,1, "32000 Hz" },
	{ SGRADIOBUT, 0, 0, 26,8, 10,1, "44100 Hz" },
	{ SGRADIOBUT, 0, 0, 26,9, 10,1, "48000 Hz" },
	{ SGRADIOBUT, 0, 0, 26,10, 10,1, "50066 Hz" },

	{ SGBOX, 0, 0, 1,13, 36,8, NULL },
	{ SGTEXT, 0, 0, 13,14, 14,1, "Capture YM/WAV" },
	{ SGTEXT, 0, 0, 2,16, 26,1, "File name (*.wav or *.ym):" },
	{ SGTEXT, 0, 0, 2,17, 34,1, dlgRecordName },
	{ SGBUTTON, 0, 0, 30,16, 6,1, "Browse" },
	{ SGBUTTON, 0, 0, 12,19, 16,1, NULL },
	{ SGBUTTON, SG_DEFAULT, 0, 10,22, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


static const int nSoundFreqs[] =
{
	11025,
	12517,
	16000,
	22050,
	25033,
	32000,
	44100,
	48000,
	50066
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the sound dialog.
*/
void Dialog_SoundDlg(void)
{
	int but, i;

	SDLGui_CenterDlg(sounddlg);

	/* Set up dialog from actual values: */

	if (ConfigureParams.Sound.bEnableSound)
		sounddlg[DLGSOUND_ENABLE].state |= SG_SELECTED;
	else
		sounddlg[DLGSOUND_ENABLE].state &= ~SG_SELECTED;

	for (i = DLGSOUND_11KHZ; i <= DLGSOUND_50KHZ; i++)
		sounddlg[i].state &= ~SG_SELECTED;

	for (i = 0; i < sizeof(nSoundFreqs)/sizeof(nSoundFreqs[0]); i++)
	{
		if (ConfigureParams.Sound.nPlaybackFreq > nSoundFreqs[i]-500
		    && ConfigureParams.Sound.nPlaybackFreq < nSoundFreqs[i]+500)
		{
			sounddlg[DLGSOUND_11KHZ + i].state |= SG_SELECTED;
			break;
		}
	}

	File_ShrinkName(dlgRecordName, ConfigureParams.Sound.szYMCaptureFileName, sounddlg[DLGSOUND_RECNAME].w);

	if ( Sound_AreWeRecording() )
		sounddlg[DLGSOUND_RECORD].txt = "Stop recording";
	else
		sounddlg[DLGSOUND_RECORD].txt = "Record sound";

	/* The sound dialog main loop */
	do
	{
		but = SDLGui_DoDialog(sounddlg, NULL);
		switch (but)
		{
		 case DLGSOUND_RECBROWSE:                    /* Choose a new record file */
			SDLGui_FileConfSelect(dlgRecordName,
			                      ConfigureParams.Sound.szYMCaptureFileName,
			                      sounddlg[DLGSOUND_RECNAME].w,
			                      TRUE);
			break;
		 case  DLGSOUND_RECORD:
			if (Sound_AreWeRecording())
			{
				sounddlg[DLGSOUND_RECORD].txt = "Record sound";
				Sound_EndRecording();
			}
			else
			{
				/* make sure that we have a valid file name... */
				if (strlen(ConfigureParams.Sound.szYMCaptureFileName) < 4)
				{
					strcpy(ConfigureParams.Sound.szYMCaptureFileName, "./hatari.wav");
				}
				sounddlg[DLGSOUND_RECORD].txt = "Stop recording";
				Sound_BeginRecording(ConfigureParams.Sound.szYMCaptureFileName);
			}
			break;
		}
	}
	while (but != DLGSOUND_EXIT && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram );

	/* Read values from dialog */
	ConfigureParams.Sound.bEnableSound = (sounddlg[DLGSOUND_ENABLE].state & SG_SELECTED);

	for (i = DLGSOUND_11KHZ; i <= DLGSOUND_50KHZ; i++)
	{
		if (sounddlg[i].state & SG_SELECTED)
		{
			ConfigureParams.Sound.nPlaybackFreq = nSoundFreqs[i-DLGSOUND_11KHZ];
			break;
		}
	}
}
