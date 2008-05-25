/*
  Hatari - screenSnapShot.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern bool bRecordingAnimation;

extern void ScreenSnapShot_SaveScreen(void);
extern bool ScreenSnapShot_AreWeRecording(void);
extern void ScreenSnapShot_BeginRecording(bool bCaptureChange, int nFramesPerSecond);
extern void ScreenSnapShot_EndRecording(void);
extern void ScreenSnapShot_RecordFrame(bool bFrameChanged);
