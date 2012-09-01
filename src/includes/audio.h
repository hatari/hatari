/*
  Hatari - audio.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_AUDIO_H
#define HATARI_AUDIO_H

extern int nAudioFrequency;
extern bool bSoundWorking;
extern int SoundBufferSize;
extern int CompleteSndBufIdx;
extern int SdlAudioBufferSize;
extern int pulse_swallowing_count;


extern void Audio_Init(void);
extern void Audio_UnInit(void);
extern void Audio_Lock(void);
extern void Audio_Unlock(void);
extern void Audio_FreeSoundBuffer(void);
extern void Audio_SetOutputAudioFreq(int Frequency);
extern void Audio_EnableAudio(bool bEnable);

#endif  /* HATARI_AUDIO_H */
