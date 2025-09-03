/*
  Hatari - dlgRecord.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

 Hatari screenshot, video and audio recording dialog.
*/
const char DlgRecord_fileid[] = "Hatari dlgRecord.c";

#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "screen.h"
#include "statusbar.h"
#include "screenSnapShot.h"
#include "avi_record.h"
#include "sound.h"
#include "str.h"
#include "file.h"

/* video object indexes */
#define DLGRECORD_CAPTURE     3
#define DLGRECORD_FORMAT_PNG  4
#define DLGRECORD_FORMAT_BMP  5
#define DLGRECORD_FORMAT_NEO  6
#define DLGRECORD_FORMAT_XIMG 7
#define DLGRECORD_CAPTURE_DIR 8
#define DLGRECORD_RECVIDEO    10
#define DLGRECORD_CROP        11
/* audio object indexes */
#define DLGRECORD_RECBROWSE   14
#define DLGRECORD_AUDIONAME   15
#define DLGRECORD_RECAUDIO    16
/* exit */
#define DLGRECORD_EXIT_RECORD 17

/* path names shown in the dialog */
#define MAX_PATH_LEN 28
static char dlgShotDir[MAX_PATH_LEN+1];
static char dlgAudioName[MAX_PATH_LEN+1];

/* Recording dialog: */
static SGOBJ recorddlg[] =
{
	{ SGBOX, 0, 0, 0,0, 52,21, NULL },
	{ SGBOX,      0, 0,  1,1, 50,9, NULL },
	{ SGTEXT,     0, 0, 19,2, 17,1, "Screen recording" },
	{ SGBUTTON,   0, 0,  4,4, 14,1, "_Screenshot" },
	{ SGRADIOBUT, 0, 0, 21,4,  5,1, "_PNG" },
	{ SGRADIOBUT, 0, 0, 27,4,  5,1, "_BMP" },
	{ SGRADIOBUT, 0, 0, 33,4,  5,1, "_NEO" },
	{ SGRADIOBUT, 0, 0, 39,4,  5,1, "_XIMG" },
	{ SGBUTTON,   0, 0,  4,6, 14,1, "Directory:" },
	{ SGTEXT,     0, 0, 21,6, MAX_PATH_LEN,1, dlgShotDir },
	{ SGBUTTON,   0, 0,  4,8, 14,1, NULL }, /* text set later, see below */
	{ SGCHECKBOX, 0, 0, 21,8, 16,1, "_Crop statusbar" },

	{ SGBOX,      0,0,  1,11, 50,7, NULL },
	{ SGTEXT,     0,0, 19,12, 16,1, "Audio recording" },
	{ SGBUTTON,   0,0,  4,14, 14,1, "_File name:" },
	{ SGTEXT,     0,0, 21,14, MAX_PATH_LEN,1, dlgAudioName },
	{ SGBUTTON,   0,0, 16,16, 20,1, NULL }, /* text set later, see below */

	{ SGBUTTON, SG_DEFAULT, 0, 16,19, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};

/* for record buttons */
#define VIDEO_START "Recor_d AVI"
#define VIDEO_STOP  "Stop recor_d"
#define AUDIO_START "Rec_ord YM/WAV"
#define AUDIO_STOP  "Stop rec_ording"

#define DEFAULT_AUDIO_FILE "./hatari.wav"


/**
 * Set ScreenShotFormat depending on which button is selected
 */
static void DlgRecord_SetScreenShotFormat(void)
{
	if ( recorddlg[DLGRECORD_FORMAT_NEO].state & SG_SELECTED )
		ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_NEO;
	else if ( recorddlg[DLGRECORD_FORMAT_XIMG].state & SG_SELECTED )
		ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_XIMG;
#if HAVE_LIBPNG
	else if ( recorddlg[DLGRECORD_FORMAT_PNG].state & SG_SELECTED )
		ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_PNG;
#endif
	else
		ConfigureParams.Screen.ScreenShotFormat = SCREEN_SNAPSHOT_BMP;
}

/*-----------------------------------------------------------------------*/
/**
 * If screenshot dir path given, set it to screenshot dir config string,
 * and update dialog screenshot dir field accordingly.
 */
static void DlgRecord_UpdateScreenShotDir(void)
{
	if (ConfigureParams.Screen.szScreenShotDir[0])
	{
		File_MakeValidPathName(ConfigureParams.Screen.szScreenShotDir);
		File_CleanFileName(ConfigureParams.Screen.szScreenShotDir);
		File_ShrinkName(dlgShotDir, ConfigureParams.Screen.szScreenShotDir, sizeof(dlgShotDir)-1);
	}
	else
	{
		static const char base[] = "(default) ";
		const int len = sizeof(base) - 1;
		assert(len < sizeof(dlgShotDir));
		Str_Copy(dlgShotDir, base, sizeof(dlgShotDir));
		File_ShrinkName(dlgShotDir + len, Configuration_GetScreenShotDir(), sizeof(dlgShotDir)-len-1);
	}
}

/**
 * Show and process the recording dialog.
 */
void Dialog_RecordingDlg(void)
{
	char *selname;
	int but;

	SDLGui_CenterDlg(recorddlg);

	/* Initialize window capture options: */
	DlgRecord_UpdateScreenShotDir();

	recorddlg[DLGRECORD_FORMAT_PNG].state &= ~SG_SELECTED;
	recorddlg[DLGRECORD_FORMAT_BMP].state &= ~SG_SELECTED;
	recorddlg[DLGRECORD_FORMAT_NEO].state &= ~SG_SELECTED;
	recorddlg[DLGRECORD_FORMAT_XIMG].state &= ~SG_SELECTED;
	if (ConfigureParams.Screen.ScreenShotFormat == SCREEN_SNAPSHOT_NEO )
		recorddlg[DLGRECORD_FORMAT_NEO].state |= SG_SELECTED;
	else if (ConfigureParams.Screen.ScreenShotFormat == SCREEN_SNAPSHOT_XIMG )
		recorddlg[DLGRECORD_FORMAT_XIMG].state |= SG_SELECTED;
#if HAVE_LIBPNG
	else if (ConfigureParams.Screen.ScreenShotFormat == SCREEN_SNAPSHOT_PNG)
		recorddlg[DLGRECORD_FORMAT_PNG].state |= SG_SELECTED;
#endif
	else
		recorddlg[DLGRECORD_FORMAT_BMP].state |= SG_SELECTED;

	if (ConfigureParams.Screen.bCrop)
		recorddlg[DLGRECORD_CROP].state |= SG_SELECTED;
	else
		recorddlg[DLGRECORD_CROP].state &= ~SG_SELECTED;

	if (Avi_AreWeRecording())
		recorddlg[DLGRECORD_RECVIDEO].txt = VIDEO_STOP;
	else
		recorddlg[DLGRECORD_RECVIDEO].txt = VIDEO_START;

	/* Initialize audio capture options: */
	File_ShrinkName(dlgAudioName, ConfigureParams.Sound.szYMCaptureFileName, recorddlg[DLGRECORD_AUDIONAME].w);

	if (Sound_AreWeRecording())
		recorddlg[DLGRECORD_RECAUDIO].txt = AUDIO_STOP;
	else
		recorddlg[DLGRECORD_RECAUDIO].txt = AUDIO_START;

	/* Recording dialog main loop */
	do
	{
		but = SDLGui_DoDialog(recorddlg);
		switch (but)
		{
		 case DLGRECORD_CAPTURE_DIR:
			selname = SDLGui_FileSelect("Screenshot Directory", Configuration_GetScreenShotDir(), NULL, false);
			if (selname)
			{
				Str_Copy(ConfigureParams.Screen.szScreenShotDir, selname, sizeof(ConfigureParams.Screen.szScreenShotDir));
				free(selname);
			}
			DlgRecord_UpdateScreenShotDir();
			break;

		 case DLGRECORD_CAPTURE:
			DlgRecord_SetScreenShotFormat();	/* Take latest choice into account */
			Screen_UpdateRect(sdlscrn, 0,0,0,0);
			ConfigureParams.Screen.bCrop = (recorddlg[DLGRECORD_CROP].state & SG_SELECTED);
			ScreenSnapShot_SaveScreen();
			break;

		 case DLGRECORD_RECVIDEO:
			if (Avi_AreWeRecording())
			{
				/* AVI indexing can take a while for larger files */
				Screen_StatusbarMessage("Finishing AVI file...", 100);
				Avi_StopRecording();
				recorddlg[DLGRECORD_RECVIDEO].txt = VIDEO_START;
				Screen_StatusbarMessage("Emulation paused", 100);
			}
			else
			{
				ConfigureParams.Screen.bCrop = (recorddlg[DLGRECORD_CROP].state & SG_SELECTED);
				selname = SDLGui_FileSelect("Record to AVI file...", ConfigureParams.Video.AviRecordFile, NULL, true);
				if (!selname || File_DoesFileNameEndWithSlash(selname))
					break;
				if (!File_QueryOverwrite(selname))
					break;
				Str_Copy(ConfigureParams.Video.AviRecordFile, selname, sizeof(ConfigureParams.Video.AviRecordFile));
				Avi_StartRecording_WithConfig ();
				recorddlg[DLGRECORD_RECVIDEO].txt = VIDEO_STOP;
			}
			break;
		 case DLGRECORD_RECBROWSE:                    /* Choose a new record file */
			SDLGui_FileConfSelect("Capture file:", dlgAudioName,
			                      ConfigureParams.Sound.szYMCaptureFileName,
			                      sizeof(dlgAudioName)-1,
			                      true);
			break;
		 case  DLGRECORD_RECAUDIO:
			if (Sound_AreWeRecording())
			{
				recorddlg[DLGRECORD_RECAUDIO].txt = AUDIO_START;
				Sound_EndRecording();
			}
			else
			{
				/* make sure that we have a valid file name... */
				if (strlen(ConfigureParams.Sound.szYMCaptureFileName) < 4)
				{
					strcpy(ConfigureParams.Sound.szYMCaptureFileName, DEFAULT_AUDIO_FILE);
				}
				recorddlg[DLGRECORD_RECAUDIO].txt =  AUDIO_STOP;
				Sound_BeginRecording(ConfigureParams.Sound.szYMCaptureFileName);
			}
			break;
		}
	}
	while (but != DLGRECORD_EXIT_RECORD && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read new values from dialog: */
	DlgRecord_SetScreenShotFormat();

	ConfigureParams.Screen.bCrop = (recorddlg[DLGRECORD_CROP].state & SG_SELECTED);
}
