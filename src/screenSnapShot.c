/*
  Hatari - screenSnapShot.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Screen Snapshots.
*/
const char ScreenSnapShot_rcsid[] = "Hatari $Id: screenSnapShot.c,v 1.13 2007-12-19 11:54:41 thothy Exp $";

#include <SDL.h>
#include <dirent.h>
#include <string.h>

#include "main.h"
#include "log.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "video.h"


BOOL bRecordingAnimation = FALSE;           /* Recording animation? */
static int nScreenShots = 0;                /* Number of screen shots saved */
static BOOL bGrabWhenChange;
static int GrabFrameCounter, GrabFrameLatch;


/*-----------------------------------------------------------------------*/
/**
 * Scan working directory to get the screenshot number
 */
static void ScreenSnapShot_GetNum(void)
{
	char dummy[5];
	int i, num;
	DIR *workingdir = opendir(szWorkingDir);
	struct dirent *file;

	nScreenShots = 0;
	if (workingdir == NULL)  return;

	file = readdir(workingdir);
	while (file != NULL)
	{
		if ( strncmp("grab", file->d_name, 4) == 0 )
		{
			/* copy next 4 numbers */
			for (i = 0; i < 4; i++)
			{
				if (file->d_name[4+i] >= '0' && file->d_name[4+i] <= '9')
					dummy[i] = file->d_name[4+i];
				else
					break;
			}

			dummy[i] = '\0'; /* null terminate */
			num = atoi(dummy);
			if (num > nScreenShots)  nScreenShots = num;
		}
		/* next file.. */
		file = readdir(workingdir);
	}

	closedir(workingdir);
}


/*-----------------------------------------------------------------------*/
/**
 * Save screen shot out .BMP file with filename 'grab0000.bmp','grab0001.bmp'....
 */
void ScreenSnapShot_SaveScreen(void)
{
	char *szFileName = malloc(FILENAME_MAX);

	if (!szFileName)  return;

	ScreenSnapShot_GetNum();
	/* Create our filename */
	nScreenShots++;
	sprintf(szFileName,"%s/grab%4.4d.bmp",szWorkingDir,nScreenShots);
	if (SDL_SaveBMP(sdlscrn, szFileName))
		fprintf(stderr, "Screen dump failed!\n");
	else
		fprintf(stderr, "Screen dump saved to: %s\n", szFileName);

	free(szFileName);
}


/*-----------------------------------------------------------------------*/
/**
 * Are we recording an animation?
 */
BOOL ScreenSnapShot_AreWeRecording(void)
{
	return bRecordingAnimation;
}


/*-----------------------------------------------------------------------*/
/**
 * Start recording animation
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

	/* And inform user */
	Log_AlertDlg(LOG_INFO, "Screenshot recording started.");
}


/*-----------------------------------------------------------------------*/
/**
 * Stop recording animation
 */
void ScreenSnapShot_EndRecording()
{
	/* Were we recording? */
	if (bRecordingAnimation)
	{
		/* Stop animation */
		bRecordingAnimation = FALSE;

		/* And inform user */
		Log_AlertDlg(LOG_INFO, "Screenshot recording stopped.");
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Recording animation frame
 */
void ScreenSnapShot_RecordFrame(BOOL bFrameChanged)
{
	/* As we recording? */
	if (bRecordingAnimation)
	{
		/* Yes, but on a change basis or a timer? */
		if (bGrabWhenChange)
		{
			/* On change, so did change this frame? */
			if (bFrameChanged)
				ScreenSnapShot_SaveScreen();
		}
		else
		{
			/* On timer, check for latch and save */
			GrabFrameCounter++;
			if (GrabFrameCounter>=GrabFrameLatch)
			{
				ScreenSnapShot_SaveScreen();
				GrabFrameCounter = 0;
			}
		}
	}
}
