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
extern BOOL bDisableSound;
extern BOOL bSoundWorking;
extern int OutputAudioFreqIndex;

extern void Audio_Init(void);
extern void Audio_UnInit(void);
extern BOOL Audio_CreateSoundBuffer(void);
extern void Audio_FreeSoundBuffer(void);
extern void Audio_ReCreateSoundBuffer(void);
extern void Audio_SetOutputAudioFreq(int Frequency);
extern void Audio_ResetBuffer(void);
extern void Audio_StopBuffer(void);
extern void Audio_WriteSamplesIntoBuffer(char *pSamples,int Index,int Length,int RampSetting);
