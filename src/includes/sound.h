/*
  Hatari - sound.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SOUND_H
#define HATARI_SOUND_H


#define MIXBUFFER_SIZE    8192          /* Size of circular buffer to store sample to (44Khz) */


extern BOOL bWriteEnvelopeFreq,bWriteChannelAAmp,bWriteChannelBAmp,bWriteChannelCAmp;
extern BOOL bEnvelopeFreqFlag;
extern char MixBuffer[MIXBUFFER_SIZE];
extern int SoundCycles;
extern int nGeneratedSamples;

extern void Sound_Init(void);
extern void Sound_Reset(void);
extern void Sound_ClearMixBuffer(void);
extern void Sound_MemorySnapShot_Capture(BOOL bSave);
extern void Sound_Update(void);
extern void Sound_Update_VBL(void);
extern void Sound_UpdateFromAudioCallBack(void);
extern BOOL Sound_BeginRecording(char *pszCaptureFileName);
extern void Sound_EndRecording();
extern BOOL Sound_AreWeRecording(void);

#endif  /* HATARI_SOUND_H */
