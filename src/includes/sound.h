/*
  Hatari - sound.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
  
  Matthias Arndt 2008-08-15
    - cleanup to have definitions and declarations for both sound cores in one place
*/

#ifndef HATARI_SOUND_H
#define HATARI_SOUND_H


/* definitions common for all sound rendering engines */

#define MIXBUFFER_SIZE    16384			/* Size of circular buffer to store sample to (44Khz) */

extern Uint8	SoundRegs[ 14 ];		/* store YM regs 0 to 13 */
extern int	nGeneratedSamples;
extern bool	bEnvelopeFreqFlag;
extern Sint16	MixBuffer[MIXBUFFER_SIZE][2];
extern bool	Sound_BufferIndexNeedReset;

/* STSound sound renderer active */
#include <SDL_types.h>


/* Internal data types */

typedef         Sint64			yms64;

typedef		Sint8			yms8;
typedef		Sint16			yms16;
typedef		Sint32			yms32;

typedef		Uint8			ymu8;
typedef		Uint16			ymu16;
typedef		Uint32			ymu32;

typedef		yms16			ymsample;	/* Output samples are mono 16bits signed PCM */


#define YM_LINEAR_MIXING		1		/* Use ymout1c5bit[] to build ymout5[] */
#define YM_TABLE_MIXING			2		/* Use volumetable_original to build ymout5[] */
#define YM_MODEL_MIXING			3		/* Use circuit analysis model to build ymout5[] */

extern int	YmVolumeMixing;
extern bool	UseLowPassFilter;

extern void Sound_Init(void);
extern void Sound_Reset(void);
extern void Sound_ResetBufferIndex(void);
extern void Sound_MemorySnapShot_Capture(bool bSave);
extern void Sound_Update(bool FillFrame);
extern void Sound_Update_VBL(void);
extern void Sound_WriteReg( int reg , Uint8 data );
extern bool Sound_BeginRecording(char *pszCaptureFileName);
extern void Sound_EndRecording(void);
extern bool Sound_AreWeRecording(void);
extern void Sound_SetYmVolumeMixing(void);
extern ymsample Subsonic_IIR_HPF_Left(ymsample x0);
extern ymsample Subsonic_IIR_HPF_Right(ymsample x0);


#endif  /* HATARI_SOUND_H */
