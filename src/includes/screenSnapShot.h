/*
  Hatari
*/

extern BOOL bRecordingAnimation;

extern void ScreenSnapShot_SaveScreen(void);
extern BOOL ScreenSnapShot_AreWeRecording(void);
extern void ScreenSnapShot_BeginRecording(BOOL bCaptureChange, int nFramesPerSecond);
extern void ScreenSnapShot_EndRecording();
extern void ScreenSnapShot_RecordFrame(BOOL bFrameChanged);
