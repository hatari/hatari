/*
  Hatari - ymFormat.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

extern bool bRecordingYM;

extern bool YMFormat_BeginRecording(const char *pszYMFileName);
extern void YMFormat_EndRecording(void);
extern void YMFormat_UpdateRecording(void);
