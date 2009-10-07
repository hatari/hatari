/*
  Hatari - screenSnapShot.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREENSNAPSHOT_H
#define HATARI_SCREENSNAPSHOT_H

#include <stdio.h>
#include <SDL.h>


extern bool bRecordingAnimation;

extern int ScreenSnapShot_SavePNG_ToFile(SDL_Surface *surface, FILE *fp, int png_compression_level, int png_filter ,
		int CropLeft , int CropRight , int CropTop , int CropBottom );
extern void ScreenSnapShot_SaveScreen(void);
extern bool ScreenSnapShot_AreWeRecording(void);
extern void ScreenSnapShot_BeginRecording(bool bCaptureChange);
extern void ScreenSnapShot_EndRecording(void);
extern void ScreenSnapShot_RecordFrame(bool bFrameChanged);

#endif /* ifndef HATARI_SCREENSNAPSHOT_H */

