/*
  Hatari - ymFormat.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern BOOL bRecordingYM;

extern BOOL YMFormat_BeginRecording(char *pszYMFileName);
extern void YMFormat_EndRecording(void);
extern void YMFormat_FreeRecording(void);
extern void YMFormat_UpdateRecording(void);
extern BOOL YMFormat_ConvertToStreams(void);
