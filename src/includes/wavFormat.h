/*
  Hatari - wavFormat.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_WAVFORMAT_H
#define HATARI_WAVFORMAT_H

extern BOOL bRecordingWav;

extern BOOL WAVFormat_OpenFile(char *pszWavFileName);
extern void WAVFormat_CloseFile(void);
extern void WAVFormat_Update(char *pSamples, int Index, int Length);

#endif
