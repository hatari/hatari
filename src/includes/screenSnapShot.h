/*
  Hatari
*/

extern BOOL bRecordingAnimation;

extern void ScreenSnapShot_CheckPrintKey(void);
extern void ScreenSnapShot_SaveScreen(void);
extern BOOL ScreenSnapShot_AreWeRecording(void);
extern void ScreenSnapShot_BeginRecording(/*HWND hWnd,*/BOOL bCaptureChange, int nFramesPerSecond);
extern void ScreenSnapShot_EndRecording(/*HWND hWnd*/);
extern void ScreenSnapShot_RecordFrame(BOOL bFrameChanged);
