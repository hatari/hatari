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


extern uint8_t SoundRegs[14];		/* store YM regs 0 to 13 */
extern int	nGeneratedSamples;
extern bool	bEnvelopeFreqFlag;

#define AUDIOMIXBUFFER_SIZE    16384		/* Size of circular buffer to store samples (eg 44Khz), must be a power of 2 */
#define AUDIOMIXBUFFER_SIZE_MASK ( AUDIOMIXBUFFER_SIZE - 1 )	/* To limit index values inside AudioMixBuffer[] */
extern int16_t	AudioMixBuffer[AUDIOMIXBUFFER_SIZE][2];	/* Ring buffer to store mixed audio output (YM2149, DMA sound, ...) */
extern int	AudioMixBuffer_pos_write;	/* Current writing position into above buffer */
extern int	AudioMixBuffer_pos_read;	/* Current reading position into above buffer */

extern bool	Sound_BufferIndexNeedReset;

/* Internal data types */

typedef int64_t yms64;

typedef int8_t  yms8;
typedef int16_t yms16;
typedef int32_t yms32;

typedef uint8_t  ymu8;
typedef uint16_t ymu16;
typedef uint32_t ymu32;

typedef		yms16			ymsample;	/* Output samples are mono 16bits signed PCM */


#define YM_LINEAR_MIXING		1		/* Use ymout1c5bit[] to build ymout5[] */
#define YM_TABLE_MIXING			2		/* Use volumetable_original to build ymout5[] */
#define YM_MODEL_MIXING			3		/* Use circuit analysis model to build ymout5[] */

extern int	YmVolumeMixing;

#define		YM2149_LPF_FILTER_NONE			0
#define		YM2149_LPF_FILTER_LPF_STF		1
#define		YM2149_LPF_FILTER_PWM			2
#define		YM2149_LPF_FILTER_IIR			3
extern int	YM2149_LPF_Filter;

#define		YM2149_HPF_FILTER_NONE			0
#define		YM2149_HPF_FILTER_IIR			1
extern int	YM2149_HPF_Filter;

#define		YM2149_RESAMPLE_METHOD_NEAREST			0
#define		YM2149_RESAMPLE_METHOD_WEIGHTED_AVERAGE_2	1
#define		YM2149_RESAMPLE_METHOD_WEIGHTED_AVERAGE_N	2
extern int	YM2149_Resample_Method;


extern void Sound_Init(void);
extern void Sound_Reset(void);
extern void Sound_ResetBufferIndex(void);
extern void Sound_MemorySnapShot_Capture(bool bSave);
extern void Sound_Stats_Show (void);
extern void Sound_Update(uint64_t CPU_Clock);
extern void Sound_Update_VBL(void);
extern void Sound_WriteReg(int reg, uint8_t data);
extern bool Sound_BeginRecording(char *pszCaptureFileName);
extern void Sound_EndRecording(void);
extern bool Sound_AreWeRecording(void);
extern void Sound_SetYmVolumeMixing(void);
extern ymsample Subsonic_IIR_HPF_Left(ymsample x0);
extern ymsample Subsonic_IIR_HPF_Right(ymsample x0);


#endif  /* HATARI_SOUND_H */
