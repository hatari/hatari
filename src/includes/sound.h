/*
  Hatari - sound.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SOUND_H
#define HATARI_SOUND_H

//#define OLD_SOUND

#ifdef OLD_SOUND

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

#else	/* OLD_SOUND */

#include <SDL_types.h>


/* StSound's internal data types */
#define YM_INTEGER_ONLY

#ifdef YM_INTEGER_ONLY
typedef         Sint64			yms64;
#else
typedef         float			ymfloat;
#endif

typedef		Sint8			yms8;
typedef		Sint16			yms16;
typedef		Sint32			yms32;

typedef		Uint8			ymu8;
typedef		Uint16			ymu16;
typedef		Uint32			ymu32;

typedef		yms16			ymsample;	/* StSound emulator renders mono 16bits signed PCM samples */


#define MIXBUFFER_SIZE    8192				/* Size of circular buffer to store sample to (44Khz) */

extern bool	UseLowPassFilter;
extern bool	bEnvelopeFreqFlag;
extern Sint8	MixBuffer[MIXBUFFER_SIZE];
extern int	nGeneratedSamples;

extern Uint8	SoundRegs[ 14 ];			/* store YM regs 0 to 13 */


extern void Sound_Init(void);
extern void Sound_Reset(void);
extern void Sound_ResetBufferIndex(void);
extern void Sound_MemorySnapShot_Capture(bool bSave);
extern void Sound_Update(void);
extern void Sound_Update_VBL(void);
extern void Sound_WriteReg( int reg , Uint8 data );
extern bool Sound_BeginRecording(char *pszCaptureFileName);
extern void Sound_EndRecording(void);
extern bool Sound_AreWeRecording(void);

#endif	/* OLD_SOUND */



#endif  /* HATARI_SOUND_H */
