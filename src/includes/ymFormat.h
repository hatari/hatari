/*
  Hatari
*/

extern BOOL bRecordingYM;

extern BOOL YMFormat_BeginRecording(/*HWND hWnd,*/ char *pszYMFileName);
extern void YMFormat_EndRecording(/*HWND hWnd*/);
extern void YMFormat_FreeRecording(void);
extern void YMFormat_UpdateRecording(void);
extern BOOL YMFormat_ConvertToStreams(void);
