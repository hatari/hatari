/*
  Hatari
*/

extern BOOL bRecordingWav;

extern BOOL WAVFormat_OpenFile(/*HWND hWnd,*/ char *pszWavFileName);
extern void WAVFormat_CloseFile(/*HWND hWnd*/);
extern void WAVFormat_Update(char *pSamples,int Index);
