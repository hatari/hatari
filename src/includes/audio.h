/*
  Hatari - audio.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_AUDIO_H
#define HATARI_AUDIO_H

/* Frequency index */
enum
{
  FREQ_11Khz,
  FREQ_22Khz,
  FREQ_44Khz
};

extern int SoundPlayBackFrequencies[];
extern BOOL bSoundWorking;
extern int OutputAudioFreqIndex;
extern int SoundBufferSize;
extern int CompleteSndBufIdx;

extern void Audio_Init(void);
extern void Audio_UnInit(void);
extern void Audio_Lock(void);
extern void Audio_Unlock(void);
extern void Audio_FreeSoundBuffer(void);
extern void Audio_SetOutputAudioFreq(int Frequency);
extern void Audio_EnableAudio(BOOL bEnable);

#endif  /* HATARI_AUDIO_H */
