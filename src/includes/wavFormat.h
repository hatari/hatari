/*
  Hatari - wavFormat.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_WAVFORMAT_H
#define HATARI_WAVFORMAT_H

extern bool bRecordingWav;

extern bool WAVFormat_OpenFile(char *pszWavFileName);
extern void WAVFormat_CloseFile(void);
extern void WAVFormat_Update(Sint16 pSamples[][2], int Index, int Length);

#endif
