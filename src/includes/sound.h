/*
  Hatari - sound.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SOUND_H
#define HATARI_SOUND_H


/* Envelope shape table */
typedef struct
{
  int WaveStart[4], WaveDelta[4];
} ENVSHAPE;

#define MIXBUFFER_SIZE    8192          /* Size of circular buffer to store sample to (44Khz) */

#define SAMPLES_BUFFER_SIZE  1024
/* Number of generated samples per frame (eg. 44Khz=882) : */
#define SAMPLES_PER_FRAME  ((SoundPlayBackFrequencies[OutputAudioFreqIndex]+25)/50)
/* Frequency of generated samples: */
#define SAMPLES_FREQ   (SoundPlayBackFrequencies[OutputAudioFreqIndex])
#define YM_FREQ        (2000000/SAMPLES_FREQ)      /* YM Frequency 2Mhz */


extern BOOL bWriteEnvelopeFreq,bWriteChannelAAmp,bWriteChannelBAmp,bWriteChannelCAmp;
extern BOOL bEnvelopeFreqFlag;
extern char MixBuffer[MIXBUFFER_SIZE];
extern int SoundCycles;
extern int nGeneratedSamples;

extern void Sound_Init(void);
extern void Sound_Reset(void);
extern void Sound_ClearMixBuffer(void);
extern void Sound_MemorySnapShot_Capture(BOOL bSave);
extern void Sound_CreateLogTables(void);
extern void Sound_CreateEnvelopeShapes(void);
extern void Sound_CreateSoundMixClipTable(void);
extern void Sound_GenerateYMFrameSamples(void);
extern void Sound_UpdateHBL(void);
extern void Sound_Update(void);
extern void Sound_Update_VBL(void);
extern BOOL Sound_BeginRecording(char *pszCaptureFileName);
extern void Sound_EndRecording();
extern BOOL Sound_AreWeRecording(void);

#endif  /* HATARI_SOUND_H */
