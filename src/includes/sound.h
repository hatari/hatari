/*
  Hatari - sound.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SOUND_H
#define HATARI_SOUND_H


#define MIXBUFFER_SIZE    8192          /* Size of circular buffer to store sample to (44Khz) */


extern bool bEnvelopeFreqFlag;
extern Sint8 MixBuffer[MIXBUFFER_SIZE];
extern int nGeneratedSamples;

extern Uint8 SoundRegs[ 14 ];		/* store YM regs 0 to 13 */

extern void Sound_Init(void);
extern void Sound_Reset(void);
extern void Sound_ResetBufferIndex(void);
extern void Sound_MemorySnapShot_Capture(bool bSave);
extern void Sound_Update(void);
extern void Sound_Update_VBL(void);
extern void Sound_WriteReg( int Reg , Uint8 Val );
extern bool Sound_BeginRecording(char *pszCaptureFileName);
extern void Sound_EndRecording(void);
extern bool Sound_AreWeRecording(void);

#endif  /* HATARI_SOUND_H */
