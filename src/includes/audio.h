/*
  Hatari
*/

// Frequency index
enum {
  FREQ_11Khz,
  FREQ_22Khz,
  FREQ_44Khz
};

// Odd/Even frame count
enum {
  FRAME_ODD,
  FRAME_EVEN
};

// Ramp settings to fade sound in/out
enum {
  RAMP_HOLD,
  RAMP_UP,
  RAMP_DOWN
};
#define  RAMP_UP_VOLUME_LEVEL  0.20f    /* (1.0f/5.f) */
#define  RAMP_DOWN_VOLUME_LEVEL  0.20f  /* (1.0f/5.f) */


extern int SoundPlayBackFrequencies[];
extern int SoundPlayBackFreqFrameLengths[][2];
extern BOOL bDisableDirectSound;
extern BOOL bDirectSoundWorking;
extern int OutputAudioFreqIndex;

extern void DAudio_Init(void);
extern void DAudio_UnInit(void);
extern BOOL DAudio_CreateSoundBuffer(void);
extern void DAudio_FreeSoundBuffer(void);
extern void DAudio_ReCreateDirectSoundBuffer(void);
extern void DAudio_SetOutputAudioFreq(int Frequency);
extern void DAudio_ResetBuffer(void);
extern void DAudio_StopBuffer(void);
extern void DAudio_WriteSamplesIntoBuffer(char *pSamples,int Index,int Length,int RampSetting);
