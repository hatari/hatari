/*
  Hatari - audio.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_AUDIO_H
#define HATARI_AUDIO_H

#include <SDL_types.h>

/* Frequency index */
enum
{
  FREQ_11Khz,
  FREQ_22Khz,
  FREQ_44Khz
};

/* Ramp settings to fade sound in/out */
enum
{
  RAMP_HOLD,
  RAMP_UP,
  RAMP_DOWN
};
#define  RAMP_UP_VOLUME_LEVEL  0.20f    /* (1.0f/5.f) */
#define  RAMP_DOWN_VOLUME_LEVEL  0.20f  /* (1.0f/5.f) */


extern int SoundPlayBackFrequencies[];
extern BOOL bDisableSound;
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
extern void Audio_WriteSamplesIntoBuffer(Sint8 *pSamples,int Index, int Length, int RampSetting, Sint8 *pDestBuffer);

#endif  /* HATARI_AUDIO_H */
