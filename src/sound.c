/*
  Hatari - sound.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This is where we emulate the YM2149. To obtain cycle-accurate timing we store
  the current cycle time and this is incremented during each instruction.
  When a write occurs in the PSG registers we take the difference in time and
  generate this many samples using the previous register data.
  Now we begin again from this point. To make sure we always have 1/50th of
  samples we update the buffer generation every 1/50th second, just in case no
  write took place on the PSG.
  NOTE: If the emulator runs slower than 50fps it cannot update the buffers,
  but the sound thread still needs some data to play to prevent a 'pop'. The
  ONLY feasible solution is to play the same buffer again. I have tried all
  kinds of methods to play the sound 'slower', but this produces un-even timing
  in the sound and it simply doesn't work. If the emulator cannot keep the
  speed, users will have to turn off the sound - that's it.

  The new version of the sound core uses/used some code/ideas from the following GPL projects :
    - tone and noise steps computations are from StSound 1.2 by Arnaud Carré (Leonard/Oxygene)
      (not used since Hatari 1.1.0)
    - 5 bits volume table and 16*16*16 combinations of all volume are from Sc68 by Benjamin Gerard
    - 4 bits to 5 bits volume interpolation from 16*16*16 to 32*32*32 from YM blep synthesis by Antti Lankila

  Special case for per==0 : as measured on a real STF, when tone/noise/env's per==0, we get
  the same sound output as when per==1.


  NOTE [NP] : As of June 2017, Hatari uses a completely new/rewritten cycle exact YM2149 emulation.

  Instead of updating YM2149's state on every host call (for example at 44.1 kHz), YM2149's state is now updated
  at 250 kHz (which is the base frequency used by real YM2149 to handle its various counters) and
  then downsampled to the host frequency.
  This is required to perfectly emulate transitions when the periods for tone/noise/envelope
  are changed and whether a new phase should be started or the current phase should be extended.

  Using a custom program on a real STF, it's possible to change YM2149 registers at very precise
  cycles during various sound phases and to record the STF sound output as a wav file on a modern PC.
  The wav file can then be studied (using Audacity for example) to precisely see how and when
  a change in a register affects the sound output.

  Based on these measures I made, the following behaviours of the YM2149 were confirmed :

    - unlike what it written in Yamaha documentation, each period counter doesn't count
      from 'period' down to 0, but count up from 0 until the counter reaches 'period'.
      The counter is incremented at 250 kHz, with the following pseudo code :

         tone_counter = tone_counter+1
         if ( tone_counter >= tone_period )
           tone_counter = 0
           invert tone output

      This means that when a new value is written in tone period A for example, the current phase
      will be either extended (if new_period_A > tone_counter_A) or a new phase will be
      started immediately (if new_period_A < tone_counter_A).

    - noise counter is incremented twice slower than tone counter ; for example with period=31
      a tone phase will remain up or down for ~0.0001 sec, but a noise phase will remain up or down
      for ~0.0002 sec. This is equivalent to incrementing noise counter at 125 kHz instead
      of 250 kHz like tone counter.

    - it is known that when writing to the env wave register (reg 13) the current envelope
      is restarted from the start (this is often used in so called sync-buzzer effects).
      But the measures on the resulting wav file of the YM2149 also show that the current
      envelope phase is restarted at the same time that envelope wave register is written to.

    - The various counters for tone/noise/env all give the same result when period=1 and when period=0.
      For noise and envelope, this can be seen when sampling the sound output as above
      For tone, this was also measured using a logic analyser (to sample at much higher rate than 250 kHz)

    - 2 voices playing tones at the same frequency and at the same volume can cancel each other partially
      or completely, giving no sound output at all.
      This happens when the phase for one voice is "up" while the phase for the other voice
      is "down", then on the next phase inversion, 1st voice will be "down" and 2nd voice will be "up".
      In such cases, the sum of these 2 tone waveforms will give a constant output signal, producing no sound at all.
      If both voices are not fully in opposite phase, the resulting sound will vary depending
      on how much phases are in common and how much they "cancel" each other

*/

/* 2008/05/05	[NP]	Fix case where period is 0 for noise, sound or envelope.	*/
/*			In that case, a real ST sounds as if period was in fact 1.	*/
/*			(fix buggy sound replay in ESwat that set volume<0 and trigger	*/
/*			a badly initialised envelope with envper=0).			*/
/* 2008/07/27	[NP]	Better separation between accesses to the YM hardware registers	*/
/*			and the sound rendering routines. Use Sound_WriteReg() to pass	*/
/*			all writes to the sound rendering functions. This allows to	*/
/*			have sound.c independent of psg.c (to ease replacement of	*/
/*			sound.c	by another rendering method).				*/
/* 2008/08/02	[NP]	Initial convert of Ym2149Ex.cpp from C++ to C.			*/
/*			Remove unused part of the code (StSound specific).		*/
/* 2008/08/09	[NP]	Complete integration of StSound routines into sound.c		*/
/*			Set EnvPer=3 if EnvPer<3 (ESwat buggy replay).			*/
/* 2008/08/13	[NP]	StSound was generating samples in the range 0-32767, instead	*/
/*			of really signed samples between -32768 and 32767, which could	*/
/*			give incorrect results in many case.				*/
/* 2008/09/06	[NP]	Use sc68 volumes table for a more accurate mixing of the voices	*/
/*			All volumes are converted to 5 bits and the table contains	*/
/*			32*32*32 values. Samples are signed and centered to get the	*/
/*			biggest amplitude possible.					*/
/*			Faster mixing routines for tone+volume+envelope (don't use	*/
/*			StSound's version anymore, it gave problem with some GCC).	*/
/* 2008/09/17	[NP]	Add ym_normalise_5bit_table to normalise the 32*32*32 table and	*/
/*			to optionally center 16 bit signed sample.			*/
/*			Possibility to mix volumes using a table measured on ST or a	*/
/*			linear mean of the 3 channels' volume.				*/
/*			Default mixing set to YM_LINEAR_MIXING.				*/
/* 2008/10/14	[NP]	Full support for 5 bits volumes : envelopes are generated with	*/
/*			32 volumes per pattern as on a real YM-2149. Fixed volumes	*/
/*			on 4 bits are converted to their 5 bits equivalent. This should	*/
/*			give the maximum accuracy possible when computing volumes.	*/
/*			New version of Ym2149_EnvStepCompute to handle 5 bits volumes.	*/
/*			Function YM2149_EnvBuild to compute the 96 volumes that define	*/
/*			a single envelope (32 initial volumes, then 64 repeated values).*/
/* 2008/10/26	[NP]	Correctly save/restore all necessary variables in		*/
/*			Sound_MemorySnapShot_Capture.					*/
/* 2008/11/23	[NP]	Clean source, remove old sound core.				*/
/* 2011/11/03	[DS]	Stereo DC filtering which accounts for DMA sound.               */
/* 2017/06/xx	[NP]	New cycle exact emulation method, all counters are incremented	*/
/*			using a simulated freq of 250 kHz. Some undocumented cases	*/
/*			where also measured on real STF to improve accuracy.		*/
/* 2019/09/09	[NP]	Add YM2149_Next_Resample_Weighted_Average_N for better		*/
/*			downsampling of the internal 250 kHz sound buffer.		*/
/* 2021/07/23	[NP]	Default to 250 kHz cycle accurate emulation and remove older	*/
/*			rendering and associated functions/variables.			*/


const char Sound_fileid[] = "Hatari sound.c";

#include "main.h"
#include "audio.h"
#include "cycles.h"
#include "m68000.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "crossbar.h"
#include "file.h"
#include "cycInt.h"
#include "log.h"
#include "memorySnapShot.h"
#include "psg.h"
#include "sound.h"
#include "screen.h"
#include "video.h"
#include "wavFormat.h"
#include "ymFormat.h"
#include "avi_record.h"
#include "clocks_timings.h"



/*--------------------------------------------------------------*/
/* Definition of the possible envelopes shapes (using 5 bits)	*/
/*--------------------------------------------------------------*/

#define	ENV_GODOWN	0		/* 31 ->  0 */
#define	ENV_GOUP	1		/*  0 -> 31 */
#define	ENV_DOWN	2		/*  0 ->  0 */
#define	ENV_UP		3		/* 31 -> 31 */

/* To generate an envelope, we first use block 0, then we repeat blocks 1 and 2 */
static const int YmEnvDef[ 16 ][ 3 ] = {
	{ ENV_GODOWN,	ENV_DOWN, ENV_DOWN } ,		/* 0 \___ */
	{ ENV_GODOWN,	ENV_DOWN, ENV_DOWN } ,		/* 1 \___ */
	{ ENV_GODOWN,	ENV_DOWN, ENV_DOWN } ,		/* 2 \___ */
	{ ENV_GODOWN,	ENV_DOWN, ENV_DOWN } ,		/* 3 \___ */
	{ ENV_GOUP,	ENV_DOWN, ENV_DOWN } ,		/* 4 /___ */
	{ ENV_GOUP,	ENV_DOWN, ENV_DOWN } ,		/* 5 /___ */
	{ ENV_GOUP,	ENV_DOWN, ENV_DOWN } ,		/* 6 /___ */
	{ ENV_GOUP,	ENV_DOWN, ENV_DOWN } ,		/* 7 /___ */
	{ ENV_GODOWN,	ENV_GODOWN, ENV_GODOWN } ,	/* 8 \\\\ */
	{ ENV_GODOWN,	ENV_DOWN, ENV_DOWN } ,		/* 9 \___ */
	{ ENV_GODOWN,	ENV_GOUP, ENV_GODOWN } ,	/* A \/\/ */
	{ ENV_GODOWN,	ENV_UP, ENV_UP } ,		/* B \--- */
	{ ENV_GOUP,	ENV_GOUP, ENV_GOUP } ,		/* C //// */
	{ ENV_GOUP,	ENV_UP, ENV_UP } ,		/* D /--- */
	{ ENV_GOUP,	ENV_GODOWN, ENV_GOUP } ,	/* E /\/\ */
	{ ENV_GOUP,	ENV_DOWN, ENV_DOWN } ,		/* F /___ */
	};


/* Buffer to store the 16 envelopes built from YmEnvDef */
static ymu16	YmEnvWaves[ 16 ][ 32 * 3 ];		/* 16 envelopes with 3 blocks of 32 volumes */



/*--------------------------------------------------------------*/
/* Definition of the volumes tables (using 5 bits) and of the	*/
/* mixing parameters for the 3 voices.				*/
/*--------------------------------------------------------------*/

/* Table of unsigned 5 bit D/A output level for 1 channel as measured on a real ST (expanded from 4 bits to 5 bits) */
/* Vol 0 should be 310 when measuread as a voltage, but we set it to 0 in order to have a volume=0 matching */
/* the 0 level of a 16 bits unsigned sample (no sound output) */
static const ymu16 ymout1c5bit[ 32 ] =
{
  0 /*310*/,  369,  438,  521,  619,  735,  874, 1039,
 1234, 1467, 1744, 2072, 2463, 2927, 3479, 4135,
 4914, 5841, 6942, 8250, 9806,11654,13851,16462,
19565,23253,27636,32845,39037,46395,55141,65535
};

/* Convert a constant 4 bits volume to the internal 5 bits value : */
/* volume5=volume4*2+1, except for volumes 0 and 1 which remain 0 and 1, */
/* in order to map [0,15] into [0,31] (O must remain 0, and 15 must give 31) */
static const ymu16 YmVolume4to5[ 16 ] = { 0,1,5,7,9,11,13,15,17,19,21,23,25,27,29,31 };

/* Table of unsigned 4 bit D/A output level for 3 channels as measured on a real ST */
static ymu16 volumetable_original[16][16][16] =
#include "ym2149_fixed_vol.h"

/* Corresponding table interpolated to 5 bit D/A output level (16 bits unsigned) */
static ymu16 ymout5_u16[32][32][32];

/* Same table, after conversion to signed results (same pointer, with different type) */
static yms16 *ymout5 = (yms16 *)ymout5_u16;



/*--------------------------------------------------------------*/
/* Other constants / macros					*/
/*--------------------------------------------------------------*/

/* Number of generated samples per frame (eg. 44Khz=882) */
#define SAMPLES_PER_FRAME	(nAudioFrequency/nScreenRefreshRate)

/* Current sound replay freq (usually 44100 Hz) */
#define YM_REPLAY_FREQ		nAudioFrequency

/* YM-2149 clock on all Atari models is 2 MHz (CPU freq / 4) */
/* Period counters for tone/noise/env are based on YM clock / 8 = 250 kHz */
#define YM_ATARI_CLOCK		(MachineClocks.YM_Freq)
#define YM_ATARI_CLOCK_COUNTER	(YM_ATARI_CLOCK / 8)

/* Merge/read the 3 volumes in a single integer (5 bits per volume) */
#define	YM_MERGE_VOICE(C,B,A)	( (C)<<10 | (B)<<5 | A )
#define	YM_MASK_1VOICE		0x1f
#define YM_MASK_A		0x1f
#define YM_MASK_B		(0x1f<<5)
#define YM_MASK_C		(0x1f<<10)


/* Constants for YM2149_Normalise_5bit_Table */
#define	YM_OUTPUT_LEVEL			0x7fff		/* amplitude of the final signal (0..65535 if centered, 0..32767 if not) */
#define YM_OUTPUT_CENTERED		false



/*--------------------------------------------------------------*/
/* Variables for the YM2149 emulator (need to be saved and	*/
/* restored in memory snapshots)				*/
/*--------------------------------------------------------------*/

/* Uncomment next line to write raw 250 kHz samples to a file 'hatari_250.wav' */
//#define YM_250_DEBUG


/* For our internal computations to convert down/up square wave signals into 0-31 volume, */
/* we consider that 'up' is 31 and 'down' is 0 */
#define	YM_SQUARE_UP		0x1f
#define	YM_SQUARE_DOWN		0x00

static ymu16	ToneA_per , ToneA_count , ToneA_val;
static ymu16	ToneB_per , ToneB_count , ToneB_val;
static ymu16	ToneC_per , ToneC_count , ToneC_val;
static ymu16	Noise_per , Noise_count , Noise_val;
static ymu16	Env_per , Env_count;
static ymu32	Env_pos;
static int	Env_shape;

static ymu32	mixerTA , mixerTB , mixerTC;
static ymu32	mixerNA , mixerNB , mixerNC;

static ymu32	RndRack;				/* current random seed */

static ymu16	EnvMask3Voices = 0;			/* mask is 0x1f for voices having an active envelope */
static ymu16	Vol3Voices = 0;				/* volume 0-0x1f for voices having a constant volume */
							/* volume is set to 0 if voice has an envelope in EnvMask3Voices */


/* Global variables that can be changed/read from other parts of Hatari */
uint8_t		SoundRegs[ 14 ];

int		YmVolumeMixing = YM_TABLE_MIXING;

int		YM2149_LPF_Filter = YM2149_LPF_FILTER_PWM;
// int		YM2149_LPF_Filter = YM2149_LPF_FILTER_NONE;	/* For debug */
int		YM2149_HPF_Filter = YM2149_HPF_FILTER_IIR;
// int		YM2149_HPF_Filter = YM2149_HPF_FILTER_NONE;	/* For debug */

//int		YM2149_Resample_Method = YM2149_RESAMPLE_METHOD_NEAREST;
//int		YM2149_Resample_Method = YM2149_RESAMPLE_METHOD_WEIGHTED_AVERAGE_2;
int		YM2149_Resample_Method = YM2149_RESAMPLE_METHOD_WEIGHTED_AVERAGE_N;

static double	pos_fract_nearest;			/* For YM2149_Next_Resample_Nearest */
static double	pos_fract_weighted_2;			/* For YM2149_Next_Resample_Weighted_Average_2 */
static uint32_t	pos_fract_weighted_n;			/* YM2149_Next_Resample_Weighted_Average_N */


bool		bEnvelopeFreqFlag;			/* Cleared each frame for YM saving */

int16_t		AudioMixBuffer[AUDIOMIXBUFFER_SIZE][2];	/* Ring buffer to store mixed audio output (YM2149, DMA sound, ...) */
int		AudioMixBuffer_pos_write;		/* Current writing position into above buffer */
int		AudioMixBuffer_pos_read;		/* Current reading position into above buffer */

int		nGeneratedSamples;			/* Generated samples since audio buffer update */

static int	AudioMixBuffer_pos_write_avi;		/* Current working index to save an AVI audio frame */

bool		Sound_BufferIndexNeedReset = false;


#define		YM_BUFFER_250_SIZE	32768		/* Size to store YM samples generated at 250 kHz (must be a power of 2) */
							/* As we fill YM_Buffer_250[] at least once per VBL (min freq = 50 Hz) */
							/* we can have 5000 YM samples per VBL. We use a slightly larger buffer */
							/* to have some kind of double buffering */
#define		YM_BUFFER_250_SIZE_MASK	( YM_BUFFER_250_SIZE - 1 )	/* To limit index values inside the ring buffer */
ymsample	YM_Buffer_250[ YM_BUFFER_250_SIZE ];	/* Ring buffer to store YM samples */
static int	YM_Buffer_250_pos_write;		/* Current writing position into above buffer */
static int	YM_Buffer_250_pos_read;			/* Current reading position into above buffer */

static uint64_t	YM2149_Clock_250;			/* 250 kHz counter */
static uint64_t	YM2149_Clock_250_CpuClock;		/* Corresponding value of CyclesGlobalClockCounter at the time YM2149_Clock_250 was updated */
static ymu16	YM2149_Freq_div_2 = 0;			/* Used for noise's generator which uses half the main freq (125 KHz) */



/* Some variables used for stats / debug */
#define		SOUND_STATS_SIZE	60
static int	Sound_Stats_Array[ SOUND_STATS_SIZE ];
static int	Sound_Stats_Index = 0;
static int	Sound_Stats_SamplePerVBL;


static CLOCKS_CYCLES_STRUCT	YM2149_ConvertCycles_250;


/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static ymsample	LowPassFilter		(ymsample x0);
static ymsample	PWMaliasFilter		(ymsample x0);

static void	interpolate_volumetable	(ymu16 volumetable[32][32][32]);

static void	YM2149_BuildModelVolumeTable(ymu16 volumetable[32][32][32]);
static void	YM2149_BuildLinearVolumeTable(ymu16 volumetable[32][32][32]);
static void	YM2149_Normalise_5bit_Table(ymu16 *in_5bit , yms16 *out_5bit, unsigned int Level, bool DoCenter);

static void	YM2149_EnvBuild		(void);
static void	Ym2149_BuildVolumeTable	(void);
static void	YM2149_UpdateClock_250	( uint64_t CpuClock );
static void	Ym2149_Init		(void);
static void	Ym2149_Reset		(void);

static ymu32	YM2149_RndCompute	(void);
static ymu16	YM2149_TonePer		(ymu8 rHigh , ymu8 rLow);
static ymu16	YM2149_NoisePer		(ymu8 rNoise);
static ymu16	YM2149_EnvPer		(ymu8 rHigh , ymu8 rLow);

static void	YM2149_Run		( uint64_t CPU_Clock );
static int	Sound_GenerateSamples	( uint64_t CPU_Clock);
static void	YM2149_DoSamples_250	( int SamplesToGenerate_250 );
#ifdef YM_250_DEBUG
static void	YM2149_DoSamples_250_Debug ( int SamplesToGenerate , int pos );
#endif


/*--------------------------------------------------------------*/
/* DC Adjuster							*/
/*--------------------------------------------------------------*/

/**
 * 6dB/octave first order HPF fc = (1.0-0.998)*44100/(2.0*pi)
 * Z pole = 0.99804 --> FS = 44100 Hz : fc=13.7 Hz (11 Hz meas)
 * a = (int32_t)(32768.0*(1.0 - pole)) :       a = 64 !!!
 * Input range: -32768 to 32767  Maximum step: +65536 or -65472
 */
ymsample	Subsonic_IIR_HPF_Left(ymsample x0)
{
	static	yms32	x1 = 0, y1 = 0, y0 = 0;

	if ( YM2149_HPF_Filter == YM2149_HPF_FILTER_NONE )
		return x0;

	y1 += ((x0 - x1)<<15) - (y0<<6);  /*  64*y0  */
	y0 = y1>>15;
	x1 = x0;

	return y0;
}


ymsample	Subsonic_IIR_HPF_Right(ymsample x0)
{
	static	yms32	x1 = 0, y1 = 0, y0 = 0;

	if ( YM2149_HPF_Filter == YM2149_HPF_FILTER_NONE )
		return x0;

	y1 += ((x0 - x1)<<15) - (y0<<6);  /*  64*y0  */
	y0 = y1>>15;
	x1 = x0;

	return y0;
}


/*--------------------------------------------------------------*/
/* Low Pass Filter routines.					*/
/*--------------------------------------------------------------*/

/**
 * Get coefficients for different Fs (C10 is in ST only):
 * Wc = 2*M_PI*4895.1;
 * Fs = 44100;
 * warp = Wc/tanf((Wc/2)/Fs);
 * b = Wc/(warp+Wc);
 * a = (Wc-warp)/(warp+Wc);
 *
 * #define B_z (yms32)( 0.2667*(1<<15))
 * #define A_z (yms32)(-0.4667*(1<<15))
 *
 * y0 = (B_z*(x0 + x1) - A_z*y0) >> 15;
 * x1 = x0;
 *
 * The Lowpass Filter formed by C10 = 0.1 uF
 * and
 * R8=1k // 1k*(65119-46602)/65119 // R9=10k // R10=5.1k //
 * (R12=470)*(100=Q1_HFE) = 206.865 ohms when YM2149 is High
 * and
 * R8=1k // R9=10k // R10=5.1k // (R12=470)*(100=Q1_HFE)
 *                        = 759.1   ohms when YM2149 is Low
 * High corner is 1/(2*pi*(0.1*10e-6)*206.865) fc = 7693.7 Hz
 * Low  corner is 1/(2*pi*(0.1*10e-6)*795.1)   fc = 2096.6 Hz
 * Notes:
 * - using STF reference designators R8 R9 R10 C10 (from dec 1986 schematics)
 * - using corresponding numbers from psgstrep and psgquart
 * - 65119 is the largest value in Paulo's psgstrep table
 * - 46602 is the largest value in Paulo's psgquart table
 * - this low pass filter uses the highest cutoff frequency
 *   on the STf (a slightly lower frequency is reasonable).
 *
 * A first order lowpass filter with a high cutoff frequency
 * is used when the YM2149 pulls high, and a lowpass filter
 * with a low cutoff frequency is used when R8 pulls low.
 */
static ymsample	LowPassFilter(ymsample x0)
{
	static	yms32 y0 = 0, x1 = 0;

	if (x0 >= y0)
	/* YM Pull up:   fc = 7586.1 Hz (44.1 KHz), fc = 8257.0 Hz (48 KHz) */
		y0 = (3*(x0 + x1) + (y0<<1)) >> 3;
	else
	/* R8 Pull down: fc = 1992.0 Hz (44.1 KHz), fc = 2168.0 Hz (48 KHz) */
		y0 = ((x0 + x1) + (6*y0)) >> 3;

	x1 = x0;
	return y0;
}

/**
 * This piecewise selective filter works by filtering the falling
 * edge of a sampled pulse-wave differently from the rising edge.
 *
 * Piecewise selective filtering is effective because harmonics on
 * one part of a wave partially define harmonics on other portions.
 *
 * Piecewise selective filtering can efficiently reduce aliasing
 * with minimal harmonic removal.
 *
 * I disclose this information into the public domain so that it
 * cannot be patented. May 23 2012 David Savinkoff.
 */
static ymsample	PWMaliasFilter(ymsample x0)
{
	static	yms32 y0 = 0, x1 = 0;

	if (x0 >= y0)
	/* YM Pull up   */
		y0 = x0;
	else
	/* R8 Pull down */
		y0 = (3*(x0 + x1) + (y0<<1)) >> 3;

	x1 = x0;
	return y0;
}



/*--------------------------------------------------------------*/
/* Build the volume conversion table used to simulate the	*/
/* behaviour of DAC used with the YM2149 in the atari ST.	*/
/* The final 32*32*32 table is built using a 16*16*16 table	*/
/* of all possible fixed volume combinations on a ST.		*/
/*--------------------------------------------------------------*/

static void	interpolate_volumetable(ymu16 volumetable[32][32][32])
{
	int	i, j, k;

	for (i = 1; i < 32; i += 2) { /* Copy 16 Panels to make a Block */
		for (j = 1; j < 32; j += 2) { /* Copy 16 Rows to make a Panel */
			for (k = 1; k < 32; k += 2) { /* Copy 16 Elements to make a Row */
				volumetable[i][j][k] = volumetable_original[(i-1)/2][(j-1)/2][(k-1)/2];
			}
			volumetable[i][j][0] = volumetable[i][j][1]; /* Move 0th Element */
			volumetable[i][j][1] = volumetable[i][j][3]; /* Move 1st Element */
			/* Interpolate 3rd Element */
			volumetable[i][j][3] = (ymu16)(0.5 + sqrt((double)volumetable[i][j][1] * volumetable[i][j][5]));
			for (k = 2; k < 32; k += 2) /* Interpolate Even Elements */
				volumetable[i][j][k] = (ymu16)(0.5 + sqrt((double)volumetable[i][j][k-1] * volumetable[i][j][k+1]));
		}
		for (k = 0; k < 32; k++) {
			volumetable[i][0][k] = volumetable[i][1][k]; /* Move 0th Row */
			volumetable[i][1][k] = volumetable[i][3][k]; /* Move 1st Row */
			/* Interpolate 3rd Row */
			volumetable[i][3][k] = (ymu16)(0.5 + sqrt((double)volumetable[i][1][k] * volumetable[i][5][k]));
		}
		for (j = 2; j < 32; j += 2) /* Interpolate Even Rows */
			for (k = 0; k < 32; k++)
				volumetable[i][j][k] = (ymu16)(0.5 + sqrt((double)volumetable[i][j-1][k] * volumetable[i][j+1][k]));
	}
	for (j = 0; j < 32; j++)
		for (k = 0; k < 32; k++) {
			volumetable[0][j][k] = volumetable[1][j][k]; /* Move 0th Panel */
			volumetable[1][j][k] = volumetable[3][j][k]; /* Move 1st Panel */
			/* Interpolate 3rd Panel */
			volumetable[3][j][k] = (ymu16)(0.5 + sqrt((double)volumetable[1][j][k] * volumetable[5][j][k]));
		}
	for (i = 2; i < 32; i += 2) /* Interpolate Even Panels */
		for (j = 0; j < 32; j++) /* Interpolate Even Panels */
			for (k = 0; k < 32; k++)
				volumetable[i][j][k] = (ymu16)(0.5 + sqrt((double)volumetable[i-1][j][k] * volumetable[i+1][j][k]));
}




/*-----------------------------------------------------------------------*/
/**
 * Build a linear version of the conversion table.
 * We use the mean of the 3 volumes converted to 16 bit values
 * (each value of ymout1c5bit is in [0,65535])
 */

static void	YM2149_BuildLinearVolumeTable(ymu16 volumetable[32][32][32])
{
	int	i, j, k;

	for (i = 0; i < 32; i++)
		for (j = 0; j < 32; j++)
			for (k = 0; k < 32; k++)
				volumetable[i][j][k] = (ymu16)( ((ymu32)ymout1c5bit[i] + ymout1c5bit[j] + ymout1c5bit[k]) / 3);
}




/*-----------------------------------------------------------------------*/
/**
 * Build a circuit analysed version of the conversion table.
 * David Savinkoff designed this algorithm by analysing data
 * measured by Paulo Simoes and Benjamin Gerard.
 * The numbers are arrived at by assuming a current steering
 * resistor ladder network and using the voltage divider rule.
 *
 * If one looks at the ST schematic of the YM2149, one sees
 * three sound pins tied together and attached to a 1000 ohm
 * resistor (1k) that has the other end grounded.
 * The 1k resistor is also in parallel with a 0.1 microfarad
 * capacitor (on the Atari ST, not STE or others). The voltage
 * developed across the 1K resistor is the output voltage which
 * I call Vout.
 *
 * The output of the YM2149 is modelled well as pullup resistors.
 * Thus, the three sound pins are seen as three parallel
 * computer-controlled, adjustable pull-up resistors.
 * To emulate the output of the YM2149, one must determine the
 * resistance values of the YM2149 relative to the 1k resistor,
 * which is done by the 'math model'.
 *
 * The AC + DC math model is:
 *
 * (MaxVol*WARP) / (1.0 + 1.0/(conductance_[i]+conductance_[j]+conductance_[k]))
 * or
 * (MaxVol*WARP) / (1.0 + 1.0/( 1/Ra +1/Rb  +1/Rc )) , Ra = channel A resistance
 *
 * Note that the first 1.0 in the formula represents the
 * normalized 1k resistor (1.0 * 1000 ohms = 1k).
 *
 * The YM2149 DC component model represents the output voltage
 * filtered of high frequency AC component, but DC component
 * remains.
 * The YM2149 DC component mode treats the voltage exactly as if
 * it were low pass filtered. This is more than what is required
 * to make 'quartet mode sound'. Simplicity leads to Generality!
 *
 * The DC component model model is:
 *
 * (MaxVol*WARP) / (2.0 + 1.0/( 1/Ra + 1/Rb  + 1/Rc))
 * or
 * (MaxVol*WARP*0.5) / (1.0 + 0.5/( 1/Ra + 1/Rb  + 1/Rc))
 *
 * Note that the 1.0 represents the normalized 1k resistor.
 * 0.5 represents 50% duty cycle for the parallel resistors
 * being summed (this effectively doubles the pull-up resistance).
 */

static void	YM2149_BuildModelVolumeTable(ymu16 volumetable[32][32][32])
{
#define MaxVol  65535.0               /* Normal Mode Maximum value in table */
#define FOURTH2 1.19                  /* Fourth root of two from YM2149 */
#define WARP    1.666666666666666667  /* measured as 1.65932 from 46602 */

	double conductance;
	double conductance_[32];
	int	i, j, k;

/**
 * YM2149 and R8=1k follows (2^-1/4)^(n-31) better when 2 voices are
 * summed (A+B or B+C or C+A) rather than individually (A or B or C):
 *   conductance = 2.0/3.0/(1.0-1.0/WARP)-2.0/3.0;
 * When taken into consideration with three voices.
 *
 * Note that the YM2149 does not use laser trimmed resistances, thus
 * has offsets that are added and/or multiplied with (2^-1/4)^(n-31).
 */
	conductance = 2.0/3.0/(1.0-1.0/WARP)-2.0/3.0; /* conductance = 1.0 */

/**
 * Because the YM current output (voltage source with series resistances)
 * is connected to a grounded resistor to develop the output voltage
 * (instead of a current to voltage converter), the output transfer
 * function is not linear. Thus:
 * 2.0*conductance_[n] = 1.0/(1.0-1.0/FOURTH2/(1.0/conductance + 1.0))-1.0;
 */
	for (i = 31; i >= 1; i--)
	{
		conductance_[i] = conductance/2.0;
		conductance = 1.0/(1.0-1.0/FOURTH2/(1.0/conductance + 1.0))-1.0;
	}
	conductance_[0] = 1.0e-8; /* Avoid divide by zero */

/**
 * YM2149 AC + DC components model:
 * (Note that Maxvol is 65119 in Simoes' table, 65535 in Gerard's)
 *
 * Sum the conductances as a function of a voltage divider:
 * Vout=Vin*Rout/(Rout+Rin)
 */
	for (i = 0; i < 32; i++)
		for (j = 0; j < 32; j++)
			for (k = 0; k < 32; k++)
			{
				volumetable[i][j][k] = (ymu16)(0.5+(MaxVol*WARP)/(1.0 +
					1.0/(conductance_[i]+conductance_[j]+conductance_[k])));
			}

/**
 * YM2149 DC component model:
 * R8=1k (pulldown) + YM//1K (pullup) with YM 50% duty PWM
 * (Note that MaxVol is 46602 in Paulo Simoes Quartet mode table)
 *
 *	for (i = 0; i < 32; i++)
 *		for (j = 0; j < 32; j++)
 *			for (k = 0; k < 32; k++)
 *			{
 *				volumetable[i][j][k] = (ymu16)(0.5+(MaxVol*WARP)/(1.0 +
 *					2.0/(conductance_[i]+conductance_[j]+conductance_[k])));
 *			}
 */
}




/*-----------------------------------------------------------------------*/
/**
 * Normalise and optionally center the volume table used to
 * convert the 3 volumes to a final signed 16 bit sample.
 * This allows to adapt the amplitude/volume of the samples and
 * to convert unsigned values to signed values.
 * - in_5bit contains 32*32*32 unsigned values in the range
 *	[0,65535].
 * - out_5bit will contain signed values
 * Possible values are :
 *	Level=65535 and DoCenter=TRUE -> [-32768,32767]
 *	Level=32767 and DoCenter=false -> [0,32767]
 *	Level=16383 and DoCenter=false -> [0,16383] (to avoid overflow with DMA sound on STe)
 */

static void	YM2149_Normalise_5bit_Table(ymu16 *in_5bit , yms16 *out_5bit, unsigned int Level, bool DoCenter)
{
	if ( Level )
	{
		int h;
		int Max = in_5bit[0x7fff];
		int Center = (Level+1)>>1;
		//fprintf ( stderr , "level %d max %d center %d\n" , Level, Max, Center );

		/* Change the amplitude of the signal to 'level' : [0,max] -> [0,level] */
		/* Then optionally center the signal around Level/2 */
		/* This means we go from sthg like [0,65535] to [-32768, 32767] if Level=65535 and DoCenter=TRUE */
		for (h=0; h<32*32*32; h++)
		{
			int tmp = in_5bit[h], res;
			res = tmp * Level / Max;

			if ( DoCenter )
				res -= Center;

			out_5bit[h] = res;
			//fprintf ( stderr , "h %d in %d out %d\n" , h , tmp , res );
		}
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Precompute all 16 possible envelopes.
 * Each envelope is made of 3 blocks of 32 volumes.
 */

static void	YM2149_EnvBuild ( void )
{
	int	env;
	int	block;
	int	vol=0 , inc=0;
	int	i;


	for ( env=0 ; env<16 ; env++ )				/* 16 possible envelopes */
		for ( block=0 ; block<3 ; block++ )		/* 3 blocks to define an envelope */
		{
			switch ( YmEnvDef[ env ][ block ] )
			{
				case ENV_GODOWN :	vol=31 ; inc=-1 ; break;
				case ENV_GOUP :		vol=0  ; inc=1 ; break;
				case ENV_DOWN :		vol=0  ; inc=0 ; break;
				case ENV_UP :		vol=31 ; inc=0 ; break;
			}

			for ( i=0 ; i<32 ; i++ )		/* 32 volumes per block */
			{
				YmEnvWaves[ env ][ block*32 + i ] = YM_MERGE_VOICE ( vol , vol , vol );
				vol += inc;
			}
		}
}



/*-----------------------------------------------------------------------*/
/**
 * Depending on the YM mixing method, build the table used to convert
 * the 3 YM volumes into a single sample.
 */

static void	Ym2149_BuildVolumeTable(void)
{
	/* Depending on the volume mixing method, we use a table based on real measures */
	/* or a table based on a linear volume mixing. */
	if ( YmVolumeMixing == YM_MODEL_MIXING )
		YM2149_BuildModelVolumeTable(ymout5_u16);	/* create 32*32*32 circuit analysed model of the volume table */
	else if ( YmVolumeMixing == YM_TABLE_MIXING )
		interpolate_volumetable(ymout5_u16);		/* expand the 16*16*16 values in volumetable_original to 32*32*32 */
	else
		YM2149_BuildLinearVolumeTable(ymout5_u16);	/* combine the 32 possible volumes */

	/* Normalise/center the values (convert from u16 to s16) */
	/* On STE/TT, we use YM_OUTPUT_LEVEL>>1 to avoid overflow with DMA sound */
	if (Config_IsMachineSTE() || Config_IsMachineTT())
		YM2149_Normalise_5bit_Table ( ymout5_u16[0][0] , ymout5 , (YM_OUTPUT_LEVEL>>1) , YM_OUTPUT_CENTERED );
	else
		YM2149_Normalise_5bit_Table ( ymout5_u16[0][0] , ymout5 , YM_OUTPUT_LEVEL , YM_OUTPUT_CENTERED );
}



/*-----------------------------------------------------------------------*/
/**
 * Convert a CPU clock value (as in CyclesGlobalClockCounter)
 * into a 250 kHz YM2149 clock.
 *
 * NOTE : we should not use this simple method :
 *	Clock_250 = CpuClock / ( 32 << nCpuFreqShift )
 * because it won't work if nCpuFreqShift is changed on the fly (when the
 * CPU goes from 8 MHz to 16 MHz in the case of the MegaSTE for example)
 *
 * To get the correct 250 kHZ clock, we must compute how many CpuClock units
 * elapsed since the previous call and convert this increment into
 * an increment for the 250 kHz clock
 * After each call the remainder will be saved to be used on the next call
 */

#if 0
/* integer version : use it when YM2149's clock is the same as CPU's clock (eg STF) */

static void	YM2149_UpdateClock_250_int ( uint64_t CpuClock )
{
	uint64_t		CpuClockDiff;
	uint64_t		YM_Div;
	uint64_t		YM_Inc;

	/* We divide CpuClockDiff by YM_Div to get a 250 Hz YM clock increment (YM_Div=32 for an STF with a 8 MHz CPU) */
	YM_Div = 32 << nCpuFreqShift;

//fprintf ( stderr , "ym_div %lu %f\n" , YM_Div , ((double)MachineClocks.CPU_Freq_Emul) / YM_ATARI_CLOCK_COUNTER );
	/* We update YM2149_Clock_250 only if enough CpuClock units elapsed (at least YM_Div) */
	CpuClockDiff = CpuClock - YM2149_Clock_250_CpuClock;
	if ( CpuClockDiff >= YM_Div )
	{
		YM_Inc = CpuClockDiff / YM_Div;			/* truncate to lower integer */
//fprintf ( stderr , "update_250  in div=%lu clock_cpu=%lu cpu_diff=%lu inc=%lu clock_250_in=%lu\n" , YM_Div, CpuClock, CpuClockDiff, YM_Inc, YM2149_Clock_250 );
		YM2149_Clock_250 += YM_Inc;
		YM2149_Clock_250_CpuClock = CpuClock - CpuClockDiff % YM_Div;
//fprintf ( stderr , "update_250 out div=%lu clock_cpu=%lu cpu_diff=%lu inc=%lu clock_250_in=%lu\n" , YM_Div, CpuClock, CpuClockDiff, YM_Inc, YM2149_Clock_250 );
	}

//fprintf ( stderr , "update_250 clock_cpu=%ld -> ym_inc=%ld clock_250=%ld clock_250_cpu_clock=%ld\n" , CpuClock , YM_Inc , YM2149_Clock_250 , YM2149_Clock_250_CpuClock );
}



/* floating point version : use it when YM2149's clock is different from CPU's clock (eg STE) */

static void	YM2149_UpdateClock_250_float ( uint64_t CpuClock )
{
	uint64_t		CpuClockDiff;
	double		YM_Div;
	uint64_t		YM_Inc;

	/* We divide CpuClockDiff by YM_Div to get a 250 Hz YM clock increment (YM_Div=32.0425 for an STE with a 8 MHz CPU) */
	YM_Div = ((double)MachineClocks.CPU_Freq_Emul) / YM_ATARI_CLOCK_COUNTER;

//fprintf ( stderr , "ym_div %f\n" , YM_Div );
	/* We update YM2149_Clock_250 only if enough CpuClock units elapsed (at least YM_Div) */
	CpuClockDiff = CpuClock - YM2149_Clock_250_CpuClock;
	if ( CpuClockDiff >= YM_Div )
	{
		YM_Inc = CpuClockDiff / YM_Div;			/* will truncate to lower integer when casting to uint64_t */
//fprintf ( stderr , "update_250  in div=%f clock_cpu=%lu cpu_diff=%lu inc=%lu clock_250_in=%lu\n" , YM_Div, CpuClock, CpuClockDiff, YM_Inc, YM2149_Clock_250 );
		YM2149_Clock_250 += YM_Inc;
		YM2149_Clock_250_CpuClock = CpuClock - round ( fmod ( CpuClockDiff , YM_Div ) );
//fprintf ( stderr , "update_250 out div=%f clock_cpu=%lu cpu_diff=%lu inc=%lu clock_250_in=%lu\n" , YM_Div, CpuClock, CpuClockDiff, YM_Inc, YM2149_Clock_250 );
	}

//fprintf ( stderr , "update_250 clock_cpu=%ld -> ym_inc=%ld clock_250=%ld clock_250_cpu_clock=%ld\n" , CpuClock , YM_Inc , YM2149_Clock_250 , YM2149_Clock_250_CpuClock );
}
#endif


static void	YM2149_UpdateClock_250_int_new ( uint64_t CpuClock )
{
	uint64_t		CpuClockDiff;


	CpuClockDiff = CpuClock - YM2149_Clock_250_CpuClock;
	ClocksTimings_ConvertCycles ( CpuClockDiff , MachineClocks.CPU_Freq_Emul , &YM2149_ConvertCycles_250 , YM_ATARI_CLOCK_COUNTER );

	YM2149_Clock_250 += YM2149_ConvertCycles_250.Cycles;
	YM2149_Clock_250_CpuClock = CpuClock;
//fprintf ( stderr , "update_250_new out clock_cpu=%lu cpu_diff=%lu inc=%lu rem=%lu clock_250_in=%lu\n" , CpuClock, CpuClockDiff, YM2149_ConvertCycles_250.Cycles, YM2149_ConvertCycles_250.Remainder , YM2149_Clock_250 );


//fprintf ( stderr , "update_250 clock_cpu=%ld -> ym_inc=%ld clock_250=%ld clock_250_cpu_clock=%ld\n" , CpuClock , YM2149_ConvertCycles_250.Cycles , YM2149_Clock_250 , YM2149_Clock_250_CpuClock );
}


/*
 * In case of STF/MegaST, we use the 'integer' version that should give less rounding
 * than the 'floating point' version. It should slightly faster too.
 * For other machines, we use the 'floating point' version because CPU and YM/DMA Audio don't
 * share the same clock.
 *
 * In the end, 'integer' and 'floating point' versions will sound the same because
 * floating point precision should be good enough to avoid rounding errors.
 */
static void	YM2149_UpdateClock_250 ( uint64_t CpuClock )
{
	if ( ConfigureParams.System.nMachineType == MACHINE_ST || ConfigureParams.System.nMachineType == MACHINE_MEGA_ST )
{
//		YM2149_UpdateClock_250_int ( CpuClock );
		YM2149_UpdateClock_250_int_new ( CpuClock );
}
	else
//		YM2149_UpdateClock_250_float ( CpuClock );
		YM2149_UpdateClock_250_int_new ( CpuClock );
}



/*-----------------------------------------------------------------------*/
/**
 * Init some internal tables for faster results (env, volume)
 * and reset the internal states.
 */
static void	Ym2149_Init(void)
{
	/* Build the 16 envelope shapes */
	YM2149_EnvBuild();

	/* Build the volume conversion table */
	Ym2149_BuildVolumeTable();

	/* Reset YM2149 internal states */
	Ym2149_Reset();

	/* Reset 250 Hz clock */
	YM2149_Clock_250 = 0;
	YM2149_Clock_250_CpuClock = CyclesGlobalClockCounter;

	/* Clear internal YM audio buffer at 250 kHz */
	memset ( YM_Buffer_250 , 0 , sizeof(YM_Buffer_250) );
	YM_Buffer_250_pos_write = 0;
	YM_Buffer_250_pos_read = 0;
}




/*-----------------------------------------------------------------------*/
/**
 * Reset all ym registers as well as the internal variables
 */

static void	Ym2149_Reset(void)
{
	int	i;

	for ( i=0 ; i<14 ; i++ )
		Sound_WriteReg ( i , 0 );

	Sound_WriteReg ( 7 , 0xff );

	/* Reset internal variables and counters */
	ToneA_per = ToneA_count = 0;
	ToneB_per = ToneB_count = 0;
	ToneC_per = ToneC_count = 0;
	Noise_per = Noise_count = 0;
	Env_per = Env_count = 0;
	Env_shape = Env_pos = 0;

	ToneA_val = ToneB_val = ToneC_val = Noise_val = YM_SQUARE_DOWN;

	RndRack = 1;
}



/*-----------------------------------------------------------------------*/
/**
 * Returns a pseudo random value, used to generate white noise.
 * As measured by David Savinkoff, the YM2149 uses a 17 stage LSFR with
 * 2 taps (17,14)
 */

static ymu32	YM2149_RndCompute(void)
{
	/*  17 stage, 2 taps (17, 14) LFSR */
	if (RndRack & 1)
	{
		RndRack = RndRack>>1 ^ 0x12000;		/* bits 17 and 14 are ones */
		return 0xffff;
	}
	else
	{	RndRack >>= 1;
		return 0;
	}
}



static ymu16	YM2149_TonePer(ymu8 rHigh , ymu8 rLow)
{
	ymu16	per;

	per = ( ( rHigh & 0x0f ) << 8 ) + rLow;
	return per;
}


static ymu16	YM2149_NoisePer(ymu8 rNoise)
{
	ymu16	per;

	per = rNoise & 0x1f;
	return per;
}


static ymu16	YM2149_EnvPer(ymu8 rHigh , ymu8 rLow)
{
	ymu16	per;

	per = ( rHigh << 8 ) + rLow;
	return per;
}



/*-----------------------------------------------------------------------*/
/**
 * Main function : compute the value of the next sample.
 * Mixes all 3 voices with tone+noise+env and apply low pass
 * filter if needed.
 * For maximum accuracy, this function emulates all single cycles at 250 kHz
 * As output we get a "raw" 250 kHz signal that will be later downsampled
 * to the chosen output frequency (eg 44.1 kHz)
 * Creating a complete 250 kHz signal allow to emulate effects that require
 * precise cycle accuracy (such as "syncsquare" used in maxYMiser v1.53)
 */
static void	YM2149_DoSamples_250 ( int SamplesToGenerate_250 )
{
	ymsample	sample;
	ymu32		bt;
	ymu16		Env3Voices;			/* 0x00CCBBAA */
	ymu16		Tone3Voices;			/* 0x00CCBBAA */
	int		pos;
	int		n;


//fprintf ( stderr , "ym2149_dosamples_250 in nb=%d ym_pos_wr=%d\n",SamplesToGenerate_250 , YM_Buffer_250_pos_write );

	/* We write new samples at position YM_Buffer_250_pos_write while we read them at the same time */
	/* at position YM_Buffer_250_pos_read (to create the output at YM_REPLAY_FREQ) */
	/* This means we must ensure YM_Buffer_250[] is large enough to avoid overwriting data */
	/* that are not read yet */
	pos = YM_Buffer_250_pos_write;

	/* Emulate as many internal YM cycles as needed to generate samples */
	for ( n=0 ; n<SamplesToGenerate_250 ; n++ )
	{
		/* Emulate 1 internal YM2149 cycle : increase all counters */
		/* As measured on a real YM2149, result for per==0 is the same as for per==1 */
		/* To obtain this in our code, counters are incremented first, then compared to per, */
		/* which gives the same result when per=1 and when per=0 */

		/* Special case for noise counter, it's increased at 125 KHz, not 250 KHz */
		YM2149_Freq_div_2 ^= 1;
		if ( YM2149_Freq_div_2 == 0 )
			Noise_count++;
		if ( Noise_count >= Noise_per )
		{
			Noise_count = 0;
			Noise_val = YM2149_RndCompute();/* 0 or 0xffff */
		}

//fprintf ( stderr , "ym2149_dosamples_250 max=%d n=%d ToneA_count=%d ToneA_per=%d val=%x pos=%d\n",SamplesToGenerate_250,n,ToneA_count,ToneA_per,ToneA_val,pos );
		/* Other counters are increased on every call, at 250 KHz */
		ToneA_count++;
		if ( ToneA_count >= ToneA_per )
		{
//fprintf ( stderr , "ym2149_dosamples_250 max=%d n=%d ToneA_count=%d ToneA_per=%d val=%x pos=%d toggle\n",SamplesToGenerate_250,n,ToneA_count,ToneA_per,ToneA_val,pos );
			ToneA_count = 0;
			ToneA_val ^= YM_SQUARE_UP;	/* 0 or 0x1f */
		}

		ToneB_count++;
		if ( ToneB_count >= ToneB_per )
		{
			ToneB_count = 0;
			ToneB_val ^= YM_SQUARE_UP;	/* 0 or 0x1f */
		}

		ToneC_count++;
		if ( ToneC_count >= ToneC_per )
		{
			ToneC_count = 0;
			ToneC_val ^= YM_SQUARE_UP;	/* 0 or 0x1f */
		}

		Env_count += 1;
		if ( Env_count >= Env_per )
		{
			Env_count = 0;
			Env_pos += 1;
			if ( Env_pos >= 3*32 )		/* blocks 0, 1 and 2 were used (Env_pos 0 to 95) */
				Env_pos -= 2*32;	/* replay/loop blocks 1 and 2 (Env_pos 32 to 95) */
		}

		/* Build 'sample' value with the values of tone/noise/volume/env */

		/* Get the 5 bits volume corresponding to the current envelope's position */
		Env3Voices = YmEnvWaves[ Env_shape ][ Env_pos ];
		Env3Voices &= EnvMask3Voices;			/* only keep volumes for voices using envelope */

		/* Tone3Voices will contain the output state of each voice : 0 or 0x1f */
		bt = (ToneA_val | mixerTA) & (Noise_val | mixerNA);	/* 0 or 0xffff */
		Tone3Voices = bt & YM_MASK_1VOICE;		/* 0 or 0x1f */

		bt = (ToneB_val | mixerTB) & (Noise_val | mixerNB);
		Tone3Voices |= ( bt & YM_MASK_1VOICE ) << 5;

		bt = (ToneC_val | mixerTC) & (Noise_val | mixerNC);
		Tone3Voices |= ( bt & YM_MASK_1VOICE ) << 10;

		/* Combine fixed volumes and envelope volumes and keep the resulting */
		/* volumes depending on the output state of each voice (0 or 0x1f) */
		Tone3Voices &= ( Env3Voices | Vol3Voices );

		sample = ymout5[ Tone3Voices ];			/* 16 bits signed value */

		/* Apply low pass filter ? */
		if ( YM2149_LPF_Filter == YM2149_LPF_FILTER_LPF_STF )
			sample = LowPassFilter ( sample );
		else if ( YM2149_LPF_Filter == YM2149_LPF_FILTER_PWM )
			sample = PWMaliasFilter ( sample );

		/* Store sample */
		YM_Buffer_250[ pos ] = sample;
		pos = ( pos + 1 ) & YM_BUFFER_250_SIZE_MASK;
	}


#ifdef YM_250_DEBUG
	/* write raw 250 kHz samples into a wav file */
	YM2149_DoSamples_250_Debug ( SamplesToGenerate_250 , YM_Buffer_250_pos_write );
#endif

	YM_Buffer_250_pos_write = pos;

//fprintf ( stderr , "ym2149_dosamples_250 out nb=%d ym_pos_wr=%d\n",SamplesToGenerate_250 , YM_Buffer_250_pos_write );
}


#ifdef YM_250_DEBUG
/*-----------------------------------------------------------------------*/
/**
 * Write raw 250 kHz samples into a wav sound file as "mono + signed 16 bit PCM + little endian"
 * This is used to compare sound before downsampling at native output freq (eg 44.1 kHz)
 * and to measure the quality of the downsampling method
 */
static void	YM2149_DoSamples_250_Debug ( int SamplesToGenerate , int pos )
{
	static uint8_t WavHeader[] =
	{
		/* RIFF chunk */
		'R', 'I', 'F', 'F',      /* "RIFF" (ASCII Characters) */
		0, 0, 0, 0,              /* Total Length Of Package To Follow (patched when file is closed) */
		'W', 'A', 'V', 'E',      /* "WAVE" (ASCII Characters) */
		/* Format chunk */
		'f', 'm', 't', ' ',      /* "fmt_" (ASCII Characters) */
		0x10, 0, 0, 0,           /* Length Of FORMAT Chunk (always 0x10) */
		0x01, 0,                 /* Always 0x01 */
		0x02, 0,                 /* Number of channels (2 for stereo) */
		0, 0, 0, 0,              /* Sample rate (patched when file header is written) */
		0, 0, 0, 0,              /* Bytes per second (patched when file header is written) */
		0x04, 0,                 /* Bytes per sample (4 = 16 bit stereo) */
		0x10, 0,                 /* Bits per sample (16 bit) */
		/* Data chunk */
		'd', 'a', 't', 'a',
		0, 0, 0, 0,              /* Length of data to follow (will be patched when file is closed) */
	};
	FILE		*file_ptr;
	int		val;
	ymsample	sample;
	int		n;
	static int	wav_size;


	if ( File_Exists ( "hatari_250.wav" ) )
	{
		file_ptr = fopen( "hatari_250.wav", "rb+");
		fseek ( file_ptr , 0 , SEEK_END );
	}
	else
	{
		file_ptr = fopen( "hatari_250.wav", "wb");
		/* Patch mono, 2 bytes per sample */
		WavHeader[22] = (uint8_t)0x01;
		WavHeader[32] = (uint8_t)0x02;

		/* Patch sample frequency in header structure */
		val = 250000;
		WavHeader[24] = (uint8_t)val;
		WavHeader[25] = (uint8_t)(val >> 8);
		WavHeader[26] = (uint8_t)(val >> 16);
		WavHeader[27] = (uint8_t)(val >> 24);
		/* Patch bytes per second in header structure */
		val = 250000 * 2;
		WavHeader[28] = (uint8_t)val;
		WavHeader[29] = (uint8_t)(val >> 8);
		WavHeader[30] = (uint8_t)(val >> 16);
		WavHeader[31] = (uint8_t)(val >> 24);

		fwrite ( &WavHeader, sizeof(WavHeader), 1, file_ptr );
	}

	for ( n=0 ; n<SamplesToGenerate ; n++ )
	{
		sample = SDL_SwapLE16 ( YM_Buffer_250[ pos ] );
		fwrite ( &sample , sizeof(sample) , 1 , file_ptr );
		pos = ( pos + 1 ) & YM_BUFFER_250_SIZE_MASK;
		wav_size += 2;
	}

	/* Update sizes in header */
	val = 12+24+8+wav_size-8;			/* RIFF size */
	WavHeader[4] = (uint8_t)val;
	WavHeader[5] = (uint8_t)(val >> 8);
	WavHeader[6] = (uint8_t)(val >> 16);
	WavHeader[7] = (uint8_t)(val >> 24);
	val = wav_size;					/* data size */
	WavHeader[40] = (uint8_t)val;
	WavHeader[41] = (uint8_t)(val >> 8);
	WavHeader[42] = (uint8_t)(val >> 16);
	WavHeader[43] = (uint8_t)(val >> 24);

	rewind ( file_ptr );
	fwrite ( &WavHeader, sizeof(WavHeader), 1, file_ptr );

	fclose ( file_ptr );
}
#endif


/*-----------------------------------------------------------------------*/
/**
 * Run internal YM2149 emulation, producing as much samples as needed
 * for this time range.
 * We compute how many CPU cycles passed since the previous call to YM2149_Run
 * (using CyclesGlobalClockCounter) and we convert this into a number
 * of internal YM2149 updates at 250 kHz.
 * When the CPU runs at 8 MHz, the YM2149 runs at 1/4 of this freq (2 MHz),
 * so it takes 32 CPU cycles to do 1 internal YM2149 update at 250 kHz.
 * (when cpu runs at higher freq, we must take nCpuFreqShift into account)
 *
 * On each call, we consider samples were already generated up to (and including) counter value
 * YM2149_Clock_250_prev. We must generate as many samples to reach (and include) YM2149_Clock_250.
 */
static void	YM2149_Run ( uint64_t CPU_Clock )
{
	uint64_t		YM2149_Clock_250_prev;
	int		YM2149_Nb_Updates_250;


	YM2149_Clock_250_prev = YM2149_Clock_250;
	YM2149_UpdateClock_250 ( CPU_Clock );

	YM2149_Nb_Updates_250 = YM2149_Clock_250 - YM2149_Clock_250_prev;

	if ( YM2149_Nb_Updates_250 > 0 )
	{
		YM2149_DoSamples_250 ( YM2149_Nb_Updates_250 );
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Downsample the YM2149 samples data from 250 KHz to YM_REPLAY_FREQ and
 * return the next sample to output.
 *
 * This method will choose the nearest sample from the input buffer
 * which can be not precise enough when frequencies from the input
 * buffer are much higher than YM_REPLAY_FREQ (see Nyquist rate)
 *
 * advantage : fast method
 * disadvantage : more aliasing when high frequency notes are played
 */
static ymsample	YM2149_Next_Resample_Nearest ( void )
{
	ymsample	sample;


	/* Get the nearest sample at pos_read or pos_read+1 */
	if ( pos_fract_nearest < 0.5 )
		sample = YM_Buffer_250[ YM_Buffer_250_pos_read ];
	else
		sample = YM_Buffer_250[ ( YM_Buffer_250_pos_read + 1 ) & YM_BUFFER_250_SIZE_MASK ];

	/* Increase fractional pos and integer pos */
	pos_fract_nearest += ( (double)YM_ATARI_CLOCK_COUNTER ) / YM_REPLAY_FREQ;

	YM_Buffer_250_pos_read = ( YM_Buffer_250_pos_read + (int)pos_fract_nearest ) & YM_BUFFER_250_SIZE_MASK;
	pos_fract_nearest -= (int)pos_fract_nearest;		/* 0 <= pos_fract_nearest < 1 */

	return sample;
}



/*-----------------------------------------------------------------------*/
/**
 * Downsample the YM2149 samples data from 250 KHz to YM_REPLAY_FREQ and
 * return the next sample to output.
 *
 * This method will do a weighted average between the 2 input samples
 * surrounding the theoretical position of the sample we want to generate
 *
 * It's a little slower than 'Resample_Nearest' but more accurate 
 */
static ymsample	YM2149_Next_Resample_Weighted_Average_2 ( void )
{
	ymsample	sample_before , sample_after;
	ymsample	sample;


	/* Get the 2 samples that surround pos_read and do a weighted average */
	sample_before = YM_Buffer_250[ YM_Buffer_250_pos_read ];
	sample_after = YM_Buffer_250[ ( YM_Buffer_250_pos_read + 1 ) & YM_BUFFER_250_SIZE_MASK ];
	sample = round ( ( 1.0 - pos_fract_weighted_2 ) * sample_before + pos_fract_weighted_2 * sample_after );
//fprintf ( stderr , "b=%04x a=%04x frac=%f -> res=%04x\n" , sample_before , sample_after , pos_fract_weighted_2 , sample );

	/* Increase fractional pos and integer pos */
	pos_fract_weighted_2 += ( (double)YM_ATARI_CLOCK_COUNTER ) / YM_REPLAY_FREQ;

	YM_Buffer_250_pos_read = ( YM_Buffer_250_pos_read + (int)pos_fract_weighted_2 ) & YM_BUFFER_250_SIZE_MASK;
	pos_fract_weighted_2 -= (int)pos_fract_weighted_2;	/* 0 <= pos_fract < 1 */

	return sample;
}



/*-----------------------------------------------------------------------*/
/**
 * Downsample the YM2149 samples data from 250 KHz to YM_REPLAY_FREQ and
 * return the next sample to output.
 *
 * This method will do a weighted average of all the sample from the input
 * buffer that surround an output sample (for example 250 KHz / 44.1 KHz would
 * do a weighted average on ~5.66 input samples)
 * It is based on the resample function used in MAME which computes
 * the average energy on an interval by summing samples (see src/emu/sound.c in MAME)
 *
 * It's slower than 'Weighted_Average_2' but it's more accurate as it uses all the
 * samples from the input buffer and works better when the input signal has some very
 * high frequencies (eg when YM voice uses period 0 to 6)
 *
 * For better accuracy without using floating point, fractional values are multiplied
 * by 0x10000 and stored using 32 or 64 bits : upper bits are the integer part and
 * lower 16 bits are the decimal part.
 */
 static ymsample	YM2149_Next_Resample_Weighted_Average_N ( void )
{
	uint32_t	interval_fract;
	int64_t		total;
	ymsample	sample;


	interval_fract = ( YM_ATARI_CLOCK_COUNTER * 0x10000LL ) / YM_REPLAY_FREQ;	/* 'LL' ensure the div is made on 64 bits */
	total = 0;

//fprintf ( stderr , "next 1 clock=%d freq=%d interval=%x  %d\n" , YM_ATARI_CLOCK_COUNTER , YM_REPLAY_FREQ , interval_fract , YM_Buffer_250_pos_read );

	if ( pos_fract_weighted_n )			/* start position : 0xffff <= pos_fract_weighted_n <= 0 */
	{
		total += ((int64_t)YM_Buffer_250[ YM_Buffer_250_pos_read ]) * ( 0x10000 - pos_fract_weighted_n );
		YM_Buffer_250_pos_read = ( YM_Buffer_250_pos_read + 1 ) & YM_BUFFER_250_SIZE_MASK;
		pos_fract_weighted_n -= 0x10000;	/* next sample */
	}

	pos_fract_weighted_n += interval_fract;		/* end position */

	while ( pos_fract_weighted_n & 0xffff0000 )	/* check integer part */
	{
		total += ((int64_t)YM_Buffer_250[ YM_Buffer_250_pos_read ]) * 0x10000;
		YM_Buffer_250_pos_read = ( YM_Buffer_250_pos_read + 1 ) & YM_BUFFER_250_SIZE_MASK;
		pos_fract_weighted_n -= 0x10000;	/* next sample */
	}

	if ( pos_fract_weighted_n )				/* partial end sample if 0xffff <= pos_fract_weighted_n < 0 */
	{
		total += ((int64_t)YM_Buffer_250[ YM_Buffer_250_pos_read ]) * pos_fract_weighted_n;
	}

//fprintf ( stderr , "next 2 %d\n" , YM_Buffer_250_pos_read );
	sample = total / interval_fract;
	return sample;
}



static ymsample	YM2149_NextSample_250 ( void )
{
	if ( YM2149_Resample_Method == YM2149_RESAMPLE_METHOD_WEIGHTED_AVERAGE_2 )
		return YM2149_Next_Resample_Weighted_Average_2 ();

	else if ( YM2149_Resample_Method == YM2149_RESAMPLE_METHOD_NEAREST )
		return YM2149_Next_Resample_Nearest ();

	else if ( YM2149_Resample_Method == YM2149_RESAMPLE_METHOD_WEIGHTED_AVERAGE_N )
		return YM2149_Next_Resample_Weighted_Average_N ();

	else
		return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Update internal variables (steps, volume masks, ...) each
 * time an YM register is changed.
 */
#define BIT_SHIFT 24
void Sound_WriteReg(int reg, uint8_t data)
{
	switch (reg)
	{
		case 0:
			SoundRegs[0] = data;
			ToneA_per = YM2149_TonePer ( SoundRegs[1] , SoundRegs[0] );
			break;
		case 1:
			SoundRegs[1] = data & 0x0f;
			ToneA_per = YM2149_TonePer ( SoundRegs[1] , SoundRegs[0] );
			break;
		case 2:
			SoundRegs[2] = data;
			ToneB_per = YM2149_TonePer ( SoundRegs[3] , SoundRegs[2] );
			break;
		case 3:
			SoundRegs[3] = data & 0x0f;
			ToneB_per = YM2149_TonePer ( SoundRegs[3] , SoundRegs[2] );
			break;
		case 4:
			SoundRegs[4] = data;
			ToneC_per = YM2149_TonePer ( SoundRegs[5] , SoundRegs[4] );
			break;
		case 5:
			SoundRegs[5] = data & 0x0f;
			ToneC_per = YM2149_TonePer ( SoundRegs[5] , SoundRegs[4] );
			break;
		case 6:
			SoundRegs[6] = data & 0x1f;
			Noise_per = YM2149_NoisePer ( SoundRegs[6] );
			break;

		case 7:
			SoundRegs[7] = data & 0x3f;			/* ignore bits 6 and 7 */
			mixerTA = (data&(1<<0)) ? 0xffff : 0;
			mixerTB = (data&(1<<1)) ? 0xffff : 0;
			mixerTC = (data&(1<<2)) ? 0xffff : 0;
			mixerNA = (data&(1<<3)) ? 0xffff : 0;
			mixerNB = (data&(1<<4)) ? 0xffff : 0;
			mixerNC = (data&(1<<5)) ? 0xffff : 0;
			break;

		case 8:
			SoundRegs[8] = data & 0x1f;
			if ( data & 0x10 )
			{
				EnvMask3Voices |= YM_MASK_A;		/* env ON */
				Vol3Voices &= ~YM_MASK_A;		/* fixed vol OFF */
			}
			else
			{
				EnvMask3Voices &= ~YM_MASK_A;		/* env OFF */
				Vol3Voices &= ~YM_MASK_A;		/* clear previous vol */
				Vol3Voices |= YmVolume4to5[ SoundRegs[8] ];	/* fixed vol ON */
			}
			break;

		case 9:
			SoundRegs[9] = data & 0x1f;
			if ( data & 0x10 )
			{
				EnvMask3Voices |= YM_MASK_B;		/* env ON */
				Vol3Voices &= ~YM_MASK_B;		/* fixed vol OFF */
			}
			else
			{
				EnvMask3Voices &= ~YM_MASK_B;		/* env OFF */
				Vol3Voices &= ~YM_MASK_B;		/* clear previous vol */
				Vol3Voices |= ( YmVolume4to5[ SoundRegs[9] ] ) << 5;	/* fixed vol ON */
			}
			break;

		case 10:
			SoundRegs[10] = data & 0x1f;
			if ( data & 0x10 )
			{
				EnvMask3Voices |= YM_MASK_C;		/* env ON */
				Vol3Voices &= ~YM_MASK_C;		/* fixed vol OFF */
			}
			else
			{
				EnvMask3Voices &= ~YM_MASK_C;		/* env OFF */
				Vol3Voices &= ~YM_MASK_C;		/* clear previous vol */
				Vol3Voices |= ( YmVolume4to5[ SoundRegs[10] ] ) << 10;	/* fixed vol ON */
			}
			break;

		case 11:
			SoundRegs[11] = data;
			Env_per = YM2149_EnvPer ( SoundRegs[12] , SoundRegs[11] );
			break;

		case 12:
			SoundRegs[12] = data;
			Env_per = YM2149_EnvPer ( SoundRegs[12] , SoundRegs[11] );
			break;

		case 13:
			SoundRegs[13] = data & 0xf;
			Env_pos = 0;					/* when writing to Env_shape, we must reset the Env_pos */
			Env_count = 0;					/* this also starts a new phase */
			Env_shape = SoundRegs[13];
			bEnvelopeFreqFlag = true;			/* used for YmFormat saving */
			break;

	}
}



/*-----------------------------------------------------------------------*/
/**
 * Init random generator, sound tables and envelopes
 * (called only once when Hatari starts)
 */
void Sound_Init(void)
{
	/* Build volume/env tables, ... */
	Ym2149_Init();

	Sound_Reset();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the sound emulation (called from Reset_ST() in reset.c)
 */
void Sound_Reset(void)
{
	/* Lock audio system before accessing variables which are used by the
	 * callback function, too! */
	Audio_Lock();

	/* Clear sound mixing buffer: */
	memset(AudioMixBuffer, 0, sizeof(AudioMixBuffer));

	/* Clear cycle counts, buffer index and register '13' flags */
	Cycles_SetCounter(CYCLES_COUNTER_SOUND, 0);
	bEnvelopeFreqFlag = false;

	AudioMixBuffer_pos_read = 0;
	/* We do not start with 0 here to fake some initial samples: */
	nGeneratedSamples = SoundBufferSize + SAMPLES_PER_FRAME;
	AudioMixBuffer_pos_write = nGeneratedSamples & AUDIOMIXBUFFER_SIZE_MASK;
	AudioMixBuffer_pos_write_avi = AudioMixBuffer_pos_write;
//fprintf ( stderr , "Sound_Reset SoundBufferSize %d SAMPLES_PER_FRAME %d nGeneratedSamples %d , AudioMixBuffer_pos_write %d\n" ,
//	SoundBufferSize , SAMPLES_PER_FRAME, nGeneratedSamples , AudioMixBuffer_pos_write );

	Ym2149_Reset();

	Audio_Unlock();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the sound buffer index variables.
 * Very important : this function should only be called by setting
 * Sound_BufferIndexNeedReset=true
 */
void Sound_ResetBufferIndex(void)
{
	Audio_Lock();
	nGeneratedSamples = SoundBufferSize + SAMPLES_PER_FRAME;
	AudioMixBuffer_pos_write =  (AudioMixBuffer_pos_read + nGeneratedSamples) & AUDIOMIXBUFFER_SIZE_MASK;
	AudioMixBuffer_pos_write_avi = AudioMixBuffer_pos_write;
//fprintf ( stderr , "Sound_ResetBufferIndex SoundBufferSize %d SAMPLES_PER_FRAME %d nGeneratedSamples %d , AudioMixBuffer_pos_write %d\n" ,
//	SoundBufferSize , SAMPLES_PER_FRAME, nGeneratedSamples , AudioMixBuffer_pos_write );
	Audio_Unlock();
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void Sound_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&ToneA_per, sizeof(ToneA_per));
	MemorySnapShot_Store(&ToneA_count, sizeof(ToneA_count));
	MemorySnapShot_Store(&ToneA_val, sizeof(ToneA_val));
	MemorySnapShot_Store(&ToneB_per, sizeof(ToneB_per));
	MemorySnapShot_Store(&ToneB_count, sizeof(ToneB_count));
	MemorySnapShot_Store(&ToneB_val, sizeof(ToneB_val));
	MemorySnapShot_Store(&ToneC_per, sizeof(ToneC_per));
	MemorySnapShot_Store(&ToneC_count, sizeof(ToneC_count));
	MemorySnapShot_Store(&ToneC_val, sizeof(ToneC_val));
	MemorySnapShot_Store(&Noise_per, sizeof(Noise_per));
	MemorySnapShot_Store(&Noise_count, sizeof(Noise_count));
	MemorySnapShot_Store(&Noise_val, sizeof(Noise_val));
	MemorySnapShot_Store(&Env_per, sizeof(Env_per));
	MemorySnapShot_Store(&Env_count, sizeof(Env_count));
	MemorySnapShot_Store(&Env_pos, sizeof(Env_pos));
	MemorySnapShot_Store(&Env_shape, sizeof(Env_shape));
	MemorySnapShot_Store(&mixerTA, sizeof(mixerTA));
	MemorySnapShot_Store(&mixerTB, sizeof(mixerTB));
	MemorySnapShot_Store(&mixerTC, sizeof(mixerTC));
	MemorySnapShot_Store(&mixerNA, sizeof(mixerNA));
	MemorySnapShot_Store(&mixerNB, sizeof(mixerNB));
	MemorySnapShot_Store(&mixerNC, sizeof(mixerNC));

	MemorySnapShot_Store(&RndRack, sizeof(RndRack));

	MemorySnapShot_Store(&EnvMask3Voices, sizeof(EnvMask3Voices));
	MemorySnapShot_Store(&Vol3Voices, sizeof(Vol3Voices));

	MemorySnapShot_Store(SoundRegs, sizeof(SoundRegs));

	MemorySnapShot_Store(&YM2149_Clock_250, sizeof(YM2149_Clock_250));
	MemorySnapShot_Store(&YM2149_Clock_250_CpuClock, sizeof(YM2149_Clock_250_CpuClock));

	MemorySnapShot_Store(&YmVolumeMixing, sizeof(YmVolumeMixing));

	MemorySnapShot_Store(&YM_Buffer_250, sizeof(YM_Buffer_250));
	MemorySnapShot_Store(&YM_Buffer_250_pos_write, sizeof(YM_Buffer_250_pos_write));
	MemorySnapShot_Store(&YM_Buffer_250_pos_read, sizeof(YM_Buffer_250_pos_read));
	MemorySnapShot_Store(&YM2149_ConvertCycles_250, sizeof(YM2149_ConvertCycles_250));

	MemorySnapShot_Store(&pos_fract_nearest, sizeof(pos_fract_nearest));
	MemorySnapShot_Store(&pos_fract_weighted_2, sizeof(pos_fract_weighted_2));
	MemorySnapShot_Store(&pos_fract_weighted_n, sizeof(pos_fract_weighted_n));
}


/*-----------------------------------------------------------------------*/
/**
 * Store how many samples were generated during one VBL
 */
static void Sound_Stats_Add ( int Samples_Nbr )
{
	Sound_Stats_Array[ Sound_Stats_Index++ ] = Samples_Nbr;
	if ( Sound_Stats_Index == SOUND_STATS_SIZE )
		Sound_Stats_Index = 0;
}



/*-----------------------------------------------------------------------*/
/**
 * Use all the numbers of samples per vbl to show an estimate of the
 * final number of generated samples during 1 second. This value should
 * stay as close as possible over time to the chosen audio frequency (eg 44100 Hz).
 * If not, it means the accuracy should be improved when generating YM samples
 */
void Sound_Stats_Show ( void )
{
	int i;
	double sum;
	double vbl_per_sec;
	double freq_gen;
	double freq_diff;
	static double diff_min=0, diff_max=0;

	sum = 0;
	for ( i=0 ; i<SOUND_STATS_SIZE ; i++ )
	      sum += Sound_Stats_Array[ i  ];

	sum = sum / SOUND_STATS_SIZE;

	vbl_per_sec = ClocksTimings_GetVBLPerSec ( ConfigureParams.System.nMachineType , nScreenRefreshRate );
	vbl_per_sec /= pow ( 2 , CLOCKS_TIMINGS_SHIFT_VBL );

	freq_gen = sum * vbl_per_sec;
	freq_diff = freq_gen - YM_REPLAY_FREQ;

	/* Update min/max values, ignore big changes */
	if ( ( freq_diff < 0 ) && ( freq_diff > -40 ) && ( freq_diff < diff_min ) )
		diff_min = freq_diff;

	if ( ( freq_diff > 0 ) && ( freq_diff < 40 ) && ( freq_diff > diff_max ) )
		diff_max = freq_diff;

	fprintf ( stderr , "Sound_Stats_Show vbl_per_sec=%.4f freq_gen=%.4f freq_diff=%.4f (min=%.4f max=%.4f)\n" ,
		  vbl_per_sec , freq_gen , freq_diff , diff_min , diff_max );
}



/*-----------------------------------------------------------------------*/
/**
 * Generate output samples for all channels (YM2149, DMA or crossbar) during this time-frame
 */
static int Sound_GenerateSamples(uint64_t CPU_Clock)
{
	int	idx;
	int	ym_margin;
	int	Sample_Nbr;

//fprintf ( stderr , "sound_gen in ym_pos_rd=%d ym_pos_wr=%d clock=%ld\n" , YM_Buffer_250_pos_read , YM_Buffer_250_pos_write , CPU_Clock );

	/* Run YM2149 emulation at 250 kHz to reach CPU_Clock counter value */
	/* This fills YM_Buffer_250[] and update YM_Buffer_250_pos_write */
	YM2149_Run ( CPU_Clock );

	ym_margin = ceil ( ((double)YM_ATARI_CLOCK_COUNTER) / nAudioFrequency ) + 2;
//fprintf ( stderr , "sound_gen margin=%d read_max=%d\n" , ym_margin , ( YM_Buffer_250_pos_write - ym_margin ) & YM_BUFFER_250_SIZE_MASK );

	Sample_Nbr = 0;
	idx = AudioMixBuffer_pos_write & AUDIOMIXBUFFER_SIZE_MASK;

	if (Config_IsMachineFalcon())
	{
		while ( ( ( YM_Buffer_250_pos_write - YM_Buffer_250_pos_read ) & YM_BUFFER_250_SIZE_MASK ) >= ym_margin )
		{
			AudioMixBuffer[idx][0] = AudioMixBuffer[idx][1] = Subsonic_IIR_HPF_Left( YM2149_NextSample_250() );
			idx = ( idx+1 ) & AUDIOMIXBUFFER_SIZE_MASK;
			Sample_Nbr++;
		}
		/* If Falcon emulation, crossbar does the job */
		if ( Sample_Nbr > 0 )
			Crossbar_GenerateSamples(AudioMixBuffer_pos_write, Sample_Nbr);
	}

	else if (!Config_IsMachineST())
	{
		while ( ( ( YM_Buffer_250_pos_write - YM_Buffer_250_pos_read ) & YM_BUFFER_250_SIZE_MASK ) >= ym_margin )
		{
			AudioMixBuffer[idx][0] = AudioMixBuffer[idx][1] = YM2149_NextSample_250();
			idx = ( idx+1 ) & AUDIOMIXBUFFER_SIZE_MASK;
			Sample_Nbr++;
		}
		/* If Ste or TT emulation, DmaSnd does mixing and filtering */
		if ( Sample_Nbr > 0 )
			DmaSnd_GenerateSamples(AudioMixBuffer_pos_write, Sample_Nbr);
	}

	else
	{
		while ( ( ( YM_Buffer_250_pos_write - YM_Buffer_250_pos_read ) & YM_BUFFER_250_SIZE_MASK ) >= ym_margin )
		{
			AudioMixBuffer[idx][0] = AudioMixBuffer[idx][1] = Subsonic_IIR_HPF_Left( YM2149_NextSample_250() );
			idx = ( idx+1 ) & AUDIOMIXBUFFER_SIZE_MASK;
			Sample_Nbr++;
		}
	}

	AudioMixBuffer_pos_write = (AudioMixBuffer_pos_write + Sample_Nbr) & AUDIOMIXBUFFER_SIZE_MASK;
	nGeneratedSamples += Sample_Nbr;
//fprintf ( stderr , "sound_gen out nb=%d ym_pos_rd=%d ym_pos_wr=%d clock=%ld\n" , Sample_Nbr , YM_Buffer_250_pos_read , YM_Buffer_250_pos_write , CPU_Clock );
	return Sample_Nbr;
}


/*-----------------------------------------------------------------------*/
/**
 * This is called to built samples up until this clock cycle
 * Sound_Update() can be called several times during a VBL
 */
void Sound_Update(uint64_t CPU_Clock)
{
	int pos_write_prev = AudioMixBuffer_pos_write;
	int Samples_Nbr;
	int nGeneratedSamples_before;

	/* Make sure that we don't interfere with the audio callback function */
	Audio_Lock();

	/* Generate samples */
	nGeneratedSamples_before = nGeneratedSamples;
	Samples_Nbr = Sound_GenerateSamples ( CPU_Clock );
	Sound_Stats_SamplePerVBL += Samples_Nbr;
//fprintf ( stderr , "sound update vbl=%d hbl=%d nbr=%d\n" , nVBLs , nHBL, Samples_Nbr );

	/* Check we don't fill the sound's ring buffer before it's played by Audio_Callback()	*/
	/* This should never happen, except if the system suffers major slowdown due to	other	*/
	/* processes or if we run in fast forward mode.						*/
	/* In the case of slowdown, we set Sound_BufferIndexNeedReset to "resync" the working	*/
	/* buffer's index AudioMixBuffer_pos_write with the system buffer's index		*/
	/* AudioMixBuffer_pos_read.								*/
	/* In the case of fast forward, we do nothing here, Sound_BufferIndexNeedReset will be	*/
	/* set when the user exits fast forward mode.						*/
	if ( ( Samples_Nbr > AUDIOMIXBUFFER_SIZE - nGeneratedSamples_before ) && ( ConfigureParams.System.bFastForward == false )
	    && ( ConfigureParams.Sound.bEnableSound == true ) )
	{
		static int logcnt = 0;
		if (logcnt++ < 50)
		{
			Log_Printf(LOG_WARN, "Your system is too slow, "
			           "some sound samples were not correctly emulated\n");
		}
		Sound_BufferIndexNeedReset = true;
	}

	/* Allow audio callback function to occur again */
	Audio_Unlock();

	/* Save to WAV file, if open */
	if (bRecordingWav)
		WAVFormat_Update(AudioMixBuffer, pos_write_prev, Samples_Nbr);
}


/*-----------------------------------------------------------------------*/
/**
 * On the end of each VBL, complete audio buffer up to the current value of CyclesGlobalClockCounter
 * As Sound_Update() could be called several times during the VBL, the audio
 * buffer might be already partially filled.
 * This function should be called from the VBL's handler (in video.c)
 */
void Sound_Update_VBL(void)
{
	Sound_Update ( CyclesGlobalClockCounter );			/* generate as many samples as needed to fill this VBL */
//fprintf ( stderr , "sound_update_vbl vbl=%d nbr=%d\n" , nVBLs, Sound_Stats_SamplePerVBL );

	/* Update some stats */
	Sound_Stats_Add ( Sound_Stats_SamplePerVBL );
//	Sound_Stats_Show ();

	/* Reset sound buffer if needed (after pause, fast forward, slow system, ...) */
	if ( Sound_BufferIndexNeedReset )
	{
		Sound_ResetBufferIndex ();
		Sound_BufferIndexNeedReset = false;
	}
	
	/* Record AVI audio frame is necessary */
	if ( bRecordingAvi )
	{
		int Len;

		Len = AudioMixBuffer_pos_write - AudioMixBuffer_pos_write_avi;	/* number of generated samples for this frame */
		if ( Len < 0 )
			Len += AUDIOMIXBUFFER_SIZE;			/* end of ring buffer was reached */

		Avi_RecordAudioStream ( AudioMixBuffer , AudioMixBuffer_pos_write_avi , Len );
	}

	AudioMixBuffer_pos_write_avi = AudioMixBuffer_pos_write;	/* save new position for next AVI audio frame */

	Sound_Stats_SamplePerVBL = 0;

	/* Clear write to register '13', used for YM file saving */
	bEnvelopeFreqFlag = false;
}


/*-----------------------------------------------------------------------*/
/**
 * Start recording sound, as .YM or .WAV output
 */
bool Sound_BeginRecording(char *pszCaptureFileName)
{
	bool bRet;

	if (!pszCaptureFileName || strlen(pszCaptureFileName) <= 3)
	{
		Log_Printf(LOG_ERROR, "Illegal sound recording file name!\n");
		return false;
	}

	/* Did specify .YM or .WAV? If neither report error */
	if (File_DoesFileExtensionMatch(pszCaptureFileName,".ym"))
		bRet = YMFormat_BeginRecording(pszCaptureFileName);
	else if (File_DoesFileExtensionMatch(pszCaptureFileName,".wav"))
		bRet = WAVFormat_OpenFile(pszCaptureFileName);
	else
	{
		Log_AlertDlg(LOG_ERROR, "Unknown Sound Recording format.\n"
		             "Please specify a .YM or .WAV output file.");
		bRet = false;
	}

	return bRet;
}


/*-----------------------------------------------------------------------*/
/**
 * End sound recording
 */
void Sound_EndRecording(void)
{
	/* Stop sound recording and close files */
	if (bRecordingYM)
		YMFormat_EndRecording();
	if (bRecordingWav)
		WAVFormat_CloseFile();
}


/*-----------------------------------------------------------------------*/
/**
 * Are we recording sound data?
 */
bool Sound_AreWeRecording(void)
{
	return (bRecordingYM || bRecordingWav);
}


/*-----------------------------------------------------------------------*/
/**
 * Rebuild volume conversion table
 */
void Sound_SetYmVolumeMixing(void)
{
	/* Build the volume conversion table */
	Ym2149_BuildVolumeTable();
}

