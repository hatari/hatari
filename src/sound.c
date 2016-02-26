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



const char Sound_fileid[] = "Hatari sound.c : " __DATE__ " " __TIME__;

#include <SDL_types.h>

#include "main.h"
#include "audio.h"
#include "cycles.h"
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
#define SAMPLES_PER_FRAME  (nAudioFrequency/nScreenRefreshRate)

/* Current sound replay freq (usually 44100 Hz) */
#define YM_REPLAY_FREQ   nAudioFrequency

/* YM-2149 clock on all Atari models is 2 MHz */
#define YM_ATARI_CLOCK		(MachineClocks.YM_Freq)


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

static ymu32	stepA , stepB , stepC;
static ymu32	posA , posB , posC;
static ymu32	mixerTA , mixerTB , mixerTC;
static ymu32	mixerNA , mixerNB , mixerNC;

static ymu32	noiseStep;
static ymu32	noisePos;
static ymu32	currentNoise;
static ymu32	RndRack;				/* current random seed */

static ymu32	envStep;
static ymu32	envPos;
static int	envShape;

static ymu16	EnvMask3Voices = 0;			/* mask is 0x1f for voices having an active envelope */
static ymu16	Vol3Voices = 0;				/* volume 0-0x1f for voices having a constant volume */
							/* volume is set to 0 if voice has an envelope in EnvMask3Voices */


/* Global variables that can be changed/read from other parts of Hatari */
Uint8		SoundRegs[ 14 ];

int		YmVolumeMixing = YM_TABLE_MIXING;
bool		UseLowPassFilter = false;

bool		bEnvelopeFreqFlag;			/* Cleared each frame for YM saving */

Sint16		MixBuffer[MIXBUFFER_SIZE][2];
int		nGeneratedSamples;			/* Generated samples since audio buffer update */
static int	ActiveSndBufIdx;			/* Current working index into above mix buffer */
static int	ActiveSndBufIdxAvi;			/* Current working index to save an AVI audio frame */

static yms64	SamplesPerFrame_unrounded = 0;		/* Number of samples for the current VBL, with simulated fractional part */
static int 	SamplesPerFrame;			/* Number of samples to generate for the current VBL */
static int	CurrentSamplesNb = 0;			/* Number of samples already generated for the current VBL */

bool		Sound_BufferIndexNeedReset = false;


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
static void	Ym2149_Init		(void);
static void	Ym2149_Reset		(void);

static ymu32	YM2149_RndCompute	(void);
static ymu32	Ym2149_ToneStepCompute	(ymu8 rHigh , ymu8 rLow);
static ymu32	Ym2149_NoiseStepCompute	(ymu8 rNoise);
static ymu32	Ym2149_EnvStepCompute	(ymu8 rHigh , ymu8 rLow);
static ymsample	YM2149_NextSample	(void);

static int	Sound_SetSamplesPassed(bool FillFrame);
static void	Sound_GenerateSamples(int SamplesToGenerate);



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

	y1 += ((x0 - x1)<<15) - (y0<<6);  /*  64*y0  */
	y0 = y1>>15;
	x1 = x0;

	return y0;
}


ymsample	Subsonic_IIR_HPF_Right(ymsample x0)
{
	static	yms32	x1 = 0, y1 = 0, y0 = 0;

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
	if ( (ConfigureParams.System.nMachineType == MACHINE_STE) || (ConfigureParams.System.nMachineType == MACHINE_MEGA_STE)
		|| (ConfigureParams.System.nMachineType == MACHINE_TT) )
		YM2149_Normalise_5bit_Table ( ymout5_u16[0][0] , ymout5 , (YM_OUTPUT_LEVEL>>1) , YM_OUTPUT_CENTERED );
	else
		YM2149_Normalise_5bit_Table ( ymout5_u16[0][0] , ymout5 , YM_OUTPUT_LEVEL , YM_OUTPUT_CENTERED );
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

	posA = 0;
	posB = 0;
	posC = 0;

	currentNoise = 0xffff;

	RndRack = 1;

	envShape = 0;
	envPos = 0;
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



/*-----------------------------------------------------------------------*/
/**
 * Compute tone's step based on the input period.
 * Although for tone we should have the same result when per==0 and per==1,
 * this gives some very sharp and unpleasant sounds in the emulation.
 * To get a better sound, we consider all per<=5 to give step=0, which will
 * produce a constant output at value '1'. This should be handled with some
 * proper filters to remove high frequencies as on a real ST (where per<=9
 * gives nearly no audible sound).
 * A common replay freq of 44.1 kHz will also not be high enough to correctly
 * render possible tone's freq of 125 or 62.5 kHz (when per==1 or per==2)
 */

static ymu32	Ym2149_ToneStepCompute(ymu8 rHigh , ymu8 rLow)
{
	int	per;
	yms64	step;

	per = rHigh&15;
	per = (per<<8)+rLow;

#if 0							/* need some high freq filters for this to work correctly */
	if ( per == 0 )
		per = 1;				/* result for Per=0 is the same as for Per=1 */	
#else
	if  (per <= (int)(YM_ATARI_CLOCK/(YM_REPLAY_FREQ*7)) )
		return 0;				/* discard frequencies higher than 80% of nyquist rate. */
#endif

	step = YM_ATARI_CLOCK;
	step <<= 24;

	step /= (per * 8 * YM_REPLAY_FREQ);		/* 0x5ab9 < step < 0x5ab3f46 at 44.1 kHz */

	return step;
}



/*-----------------------------------------------------------------------*/
/**
 * Compute noise's step based on the input period.
 * On a real STF, we get the same result when per==0 and per==1.
 * A common replay freq of 44.1 kHz will not be high enough to correctly
 * render possible noise's freq of 125 or 62.5 kHz (when per==1 or per==2).
 * With a random wave such as noise, this means that with a replay freq
 * of 44.1 kHz, per==1 and per==2 (as well as per==3) will sound the same :
 * 	per==1   step=0x2d59fa3   freq=125 kHz
 * 	per==2   step=0x16acfd1   freq=62.5 kHz
 * 	per==3   step=0x0f1dfe1   freq=41.7 kHz
 */

static ymu32	Ym2149_NoiseStepCompute(ymu8 rNoise)
{
	int	per;
	yms64	step;

	per = (rNoise&0x1f);

	if ( per == 0 )
		per = 1;				/* result for Per=0 is the same as for Per=1 */	

	step = YM_ATARI_CLOCK;
	step <<= 24;

	step /= (per * 16 * YM_REPLAY_FREQ);		/* 0x17683f < step < 0x2d59fa3 at 44.1 kHz */

	return step;
}



/*-----------------------------------------------------------------------*/
/**
 * Compute envelope's step. The envelope is made of different patterns
 * of 32 volumes. In each pattern, the volume is changed at frequency
 * Fe = MasterClock / ( 8 * EnvPer ).
 * In our case, we use a lower replay freq ; between 2 consecutive calls
 * to envelope's generation, the internal counter will advance 'step'
 * units, where step = MasterClock / ( 8 * EnvPer * YM_REPLAY_FREQ )
 * As 'step' requires floating point to be stored, we use left shifting
 * to multiply 'step' by a fixed amount. All operations are made with
 * shifted values ; to get the final value, we must right shift the
 * result. We use '<<24', which gives 8 bits for the integer part, and
 * the equivalent of 24 bits for the fractional part.
 * Since we're using large numbers, we temporarily use 64 bits integer
 * to avoid overflow and keep largest precision possible.
 * On a real STF, we get the same result when per==0 and per==1.
 */

static ymu32	Ym2149_EnvStepCompute(ymu8 rHigh , ymu8 rLow)
{
	yms64	per;
	yms64	step;

	per = rHigh;
	per = (per<<8)+rLow;

	step = YM_ATARI_CLOCK;
	step <<= 24;

	if ( per == 0 )
		per = 1;				/* result for Per=0 is the same as for Per=1 */	

	step /= (8 * per * YM_REPLAY_FREQ);		/* 0x5ab < step < 0x5ab3f46 at 44.1 kHz */

	return step;
}



/*-----------------------------------------------------------------------*/
/**
 * Main function : compute the value of the next sample.
 * Mixes all 3 voices with tone+noise+env and apply low pass
 * filter if needed.
 * All operations are done with integer math, using <<24 to simulate
 * floating point precision : upper 8 bits are the integer part, lower 24
 * are the fractional part.
 * Tone is a square wave with 2 states 0 or 1. If integer part of posX is
 * even (bit24=0) we consider output is 0, else (bit24=1) we consider
 * output is 1. This gives the value of bt for one voice after extending it
 * to all 0 bits or all 1 bits using a '-'
 */

static ymsample	YM2149_NextSample(void)
{
	ymsample	sample;
	ymu32		bt;
	ymu32		bn;
	ymu16		Env3Voices;			/* 0x00CCBBAA */
	ymu16		Tone3Voices;			/* 0x00CCBBAA */


	/* Noise value : 0 or 0xffff */
	if ( noisePos&0xff000000 )			/* integer part > 0 */
	{
		currentNoise = YM2149_RndCompute();
		noisePos &= 0xffffff;			/* keep fractional part of noisePos */
	}
	bn = currentNoise;				/* 0 or 0xffff */

	/* Get the 5 bits volume corresponding to the current envelope's position */
	Env3Voices = YmEnvWaves[ envShape ][ envPos>>24 ];	/* integer part of envPos is in bits 24-31 */
	Env3Voices &= EnvMask3Voices;			/* only keep volumes for voices using envelope */

//fprintf ( stderr , "env %x %x %x\n" , Env3Voices , envStep , envPos );

	/* Tone3Voices will contain the output state of each voice : 0 or 0x1f */
	bt = -( (posA>>24) & 1);			/* 0 if bit24=0 or 0xffffffff if bit24=1 */
	bt = (bt | mixerTA) & (bn | mixerNA);		/* 0 or 0xffff */
	Tone3Voices = bt & YM_MASK_1VOICE;		/* 0 or 0x1f */
	bt = -( (posB>>24) & 1);
	bt = (bt | mixerTB) & (bn | mixerNB);
	Tone3Voices |= ( bt & YM_MASK_1VOICE ) << 5;
	bt = -( (posC>>24) & 1);
	bt = (bt | mixerTC) & (bn | mixerNC);
	Tone3Voices |= ( bt & YM_MASK_1VOICE ) << 10;

	/* Combine fixed volumes and envelope volumes and keep the resulting */
	/* volumes depending on the output state of each voice (0 or 0x1f) */
	Tone3Voices &= ( Env3Voices | Vol3Voices );

	/* D/A conversion of the 3 volumes into a sample using a precomputed conversion table */

	if (stepA == 0  &&  (Tone3Voices & YM_MASK_A) > 1)
		Tone3Voices -= 1;     /* Voice A AC component removed; Transient DC component remains */

	if (stepB == 0  &&  (Tone3Voices & YM_MASK_B) > 1<<5)
		Tone3Voices -= 1<<5;  /* Voice B AC component removed; Transient DC component remains */

	if (stepC == 0  &&  (Tone3Voices & YM_MASK_C) > 1<<10)
		Tone3Voices -= 1<<10; /* Voice C AC component removed; Transient DC component remains */

	sample = ymout5[ Tone3Voices ];			/* 16 bits signed value */


	/* Increment positions */
	posA += stepA;
	posB += stepB;
	posC += stepC;
	noisePos += noiseStep;

	envPos += envStep;
	if ( envPos >= (3*32) << 24 )			/* blocks 0, 1 and 2 were used (envPos 0 to 95) */
		envPos -= (2*32) << 24;			/* replay/loop blocks 1 and 2 (envPos 32 to 95) */

	/* Apply low pass filter ? */
	if ( UseLowPassFilter )
		return LowPassFilter(sample);
	else
		return PWMaliasFilter(sample);
}



/*-----------------------------------------------------------------------*/
/**
 * Update internal variables (steps, volume masks, ...) each
 * time an YM register is changed.
 */
#define BIT_SHIFT 24
void	Sound_WriteReg( int reg , Uint8 data )
{
	switch (reg)
	{
		case 0:
			SoundRegs[0] = data;
			stepA = Ym2149_ToneStepCompute ( SoundRegs[1] , SoundRegs[0] );
			if (!stepA) posA = 1u<<BIT_SHIFT;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 1:
			SoundRegs[1] = data & 0x0f;
			stepA = Ym2149_ToneStepCompute ( SoundRegs[1] , SoundRegs[0] );
			if (!stepA) posA = 1u<<BIT_SHIFT;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 2:
			SoundRegs[2] = data;
			stepB = Ym2149_ToneStepCompute ( SoundRegs[3] , SoundRegs[2] );
			if (!stepB) posB = 1u<<BIT_SHIFT;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 3:
			SoundRegs[3] = data & 0x0f;
			stepB = Ym2149_ToneStepCompute ( SoundRegs[3] , SoundRegs[2] );
			if (!stepB) posB = 1u<<BIT_SHIFT;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 4:
			SoundRegs[4] = data;
			stepC = Ym2149_ToneStepCompute ( SoundRegs[5] , SoundRegs[4] );
			if (!stepC) posC = 1u<<BIT_SHIFT;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 5:
			SoundRegs[5] = data & 0x0f;
			stepC = Ym2149_ToneStepCompute ( SoundRegs[5] , SoundRegs[4] );
			if (!stepC) posC = 1u<<BIT_SHIFT;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 6:
			SoundRegs[6] = data & 0x1f;
			noiseStep = Ym2149_NoiseStepCompute ( SoundRegs[6] );
			if (!noiseStep)
			{
				noisePos = 0;
				currentNoise = 0xffff;
			}
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
			envStep = Ym2149_EnvStepCompute ( SoundRegs[12] , SoundRegs[11] );
			break;

		case 12:
			SoundRegs[12] = data;
			envStep = Ym2149_EnvStepCompute ( SoundRegs[12] , SoundRegs[11] );
			break;

		case 13:
			SoundRegs[13] = data & 0xf;
			envPos = 0;					/* when writing to EnvShape, we must reset the EnvPos */
			envShape = SoundRegs[13];
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
	memset(MixBuffer, 0, sizeof(MixBuffer));

	/* Clear cycle counts, buffer index and register '13' flags */
	Cycles_SetCounter(CYCLES_COUNTER_SOUND, 0);
	bEnvelopeFreqFlag = false;

	CompleteSndBufIdx = 0;
	/* We do not start with 0 here to fake some initial samples: */
	nGeneratedSamples = SoundBufferSize + SAMPLES_PER_FRAME;
	ActiveSndBufIdx = nGeneratedSamples % MIXBUFFER_SIZE;
	SamplesPerFrame = SAMPLES_PER_FRAME;
	CurrentSamplesNb = 0;
	ActiveSndBufIdxAvi = ActiveSndBufIdx;
//fprintf ( stderr , "Sound_Reset SoundBufferSize %d SAMPLES_PER_FRAME %d nGeneratedSamples %d , ActiveSndBufIdx %d\n" ,
//	SoundBufferSize , SAMPLES_PER_FRAME, nGeneratedSamples , ActiveSndBufIdx );

	Ym2149_Reset();

	Audio_Unlock();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the sound buffer index variables.
 * Very important : this function should only be called by setting
 * Sound_BufferIndexNeedReset=true ; sound buffer index should be reset
 * only after the sound for the whole VBL was updated (CurrentSamplesNb returns to 0)
 * else it will alter the value of DMA Frame Count ($ff8909/0b/0d) and
 * could cause crashes in some programs.
 */
void Sound_ResetBufferIndex(void)
{
	Audio_Lock();
	nGeneratedSamples = SoundBufferSize + SAMPLES_PER_FRAME;
	ActiveSndBufIdx =  (CompleteSndBufIdx + nGeneratedSamples) % MIXBUFFER_SIZE;
	SamplesPerFrame = SAMPLES_PER_FRAME;
	CurrentSamplesNb = 0;
	ActiveSndBufIdxAvi = ActiveSndBufIdx;
//fprintf ( stderr , "Sound_ResetBufferIndex SoundBufferSize %d SAMPLES_PER_FRAME %d nGeneratedSamples %d , ActiveSndBufIdx %d\n" ,
//	SoundBufferSize , SAMPLES_PER_FRAME, nGeneratedSamples , ActiveSndBufIdx );
	Audio_Unlock();
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void Sound_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&stepA, sizeof(stepA));
	MemorySnapShot_Store(&stepB, sizeof(stepB));
	MemorySnapShot_Store(&stepC, sizeof(stepC));
	MemorySnapShot_Store(&posA, sizeof(posA));
	MemorySnapShot_Store(&posB, sizeof(posB));
	MemorySnapShot_Store(&posC, sizeof(posC));

	MemorySnapShot_Store(&mixerTA, sizeof(mixerTA));
	MemorySnapShot_Store(&mixerTB, sizeof(mixerTB));
	MemorySnapShot_Store(&mixerTC, sizeof(mixerTC));
	MemorySnapShot_Store(&mixerNA, sizeof(mixerNA));
	MemorySnapShot_Store(&mixerNB, sizeof(mixerNB));
	MemorySnapShot_Store(&mixerNC, sizeof(mixerNC));

	MemorySnapShot_Store(&noiseStep, sizeof(noiseStep));
	MemorySnapShot_Store(&noisePos, sizeof(noisePos));
	MemorySnapShot_Store(&currentNoise, sizeof(currentNoise));
	MemorySnapShot_Store(&RndRack, sizeof(RndRack));

	MemorySnapShot_Store(&envStep, sizeof(envStep));
	MemorySnapShot_Store(&envPos, sizeof(envPos));
	MemorySnapShot_Store(&envShape, sizeof(envShape));

	MemorySnapShot_Store(&EnvMask3Voices, sizeof(EnvMask3Voices));
	MemorySnapShot_Store(&Vol3Voices, sizeof(Vol3Voices));

	MemorySnapShot_Store(SoundRegs, sizeof(SoundRegs));

	// MemorySnapShot_Store(&YmVolumeMixing, sizeof(YmVolumeMixing));
	// MemorySnapShot_Store(&UseLowPassFilter, sizeof(UseLowPassFilter));
}


/*-----------------------------------------------------------------------*/
/**
 * Find how many samples to generate and store in 'nSamplesToGenerate'
 * Also update sound cycles counter to store how many we actually did
 * so generates set amount each frame.
 * If FillFrame is true, this means we reach the end of the VBL and me must
 * add as many samples as necessary to get a total of SamplesPerFrame
 * for this VBL.
 */
static int Sound_SetSamplesPassed(bool FillFrame)
{
	int nSoundCycles;
	int SamplesToGenerate;				/* How many samples are needed for this time-frame */

	nSoundCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);

	/* example : 160256 cycles per VBL, 44Khz = 882 samples per VBL at 50 Hz */
	/* 882/160256 samples per cpu clock cycle */

	/* Total number of samples that we should have at this point of the VBL */
	SamplesToGenerate = nSoundCycles * SamplesPerFrame
		/ ClocksTimings_GetCyclesPerVBL ( ConfigureParams.System.nMachineType , nScreenRefreshRate );

//if (SamplesToGenerate > SamplesPerFrame )
//fprintf ( stderr , "over run %d %d\n" , SamplesPerFrame , SamplesToGenerate );

	if (SamplesToGenerate > SamplesPerFrame)
		SamplesToGenerate = SamplesPerFrame;

	SamplesToGenerate -= CurrentSamplesNb;		/* don't count samples that were already generated up to now */
	if ( SamplesToGenerate < 0 )
		SamplesToGenerate = 0;


	/* If we're called from the VBL interrupt (FillFrame==true), we must ensure we have */
	/* an exact total of SamplesPerFrame samples during a full VBL (we take into account */
	/* the samples that were already generated during this VBL) */
	if ( FillFrame )
	{
		SamplesToGenerate = SamplesPerFrame - CurrentSamplesNb;	/* how many samples are missing to reach SamplesPerFrame */
		if ( SamplesToGenerate < 0 )
			SamplesToGenerate = 0;
	}

	/* Check we don't fill the sound's ring buffer before it's played by Audio_Callback()	*/
	/* This should never happen, except if the system suffers major slowdown due to	other	*/
	/* processes or if we run in fast forward mode.						*/
	/* In the case of slowdown, we set Sound_BufferIndexNeedReset to "resync" the working	*/
	/* buffer's index ActiveSndBufIdx with the system buffer's index CompleteSndBufIdx.	*/
	/* In the case of fast forward, we do nothing here, Sound_BufferIndexNeedReset will be	*/
	/* set when the user exits fast forward mode.						*/
	if ( ( SamplesToGenerate > MIXBUFFER_SIZE - nGeneratedSamples ) && ( ConfigureParams.System.bFastForward == false )
	    && ( ConfigureParams.Sound.bEnableSound == true ) )
	{
		Log_Printf ( LOG_WARN , "Your system is too slow, some sound samples were not correctly emulated\n" );
		Sound_BufferIndexNeedReset = true;
	}

//fprintf ( stderr , "vbl %d hbl %d samp_gen %d / %d frac %lx\n" , nVBLs , nHBL , SamplesToGenerate , SamplesPerFrame , (long int)SamplesPerFrame_unrounded );

	return SamplesToGenerate;
}


/*-----------------------------------------------------------------------*/
/**
 * Generate samples for all channels during this time-frame
 */
static void Sound_GenerateSamples(int SamplesToGenerate)
{
	int	i;
	int	idx;

	if (SamplesToGenerate <= 0)
		return;

	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
	{
		for (i = 0; i < SamplesToGenerate; i++)
		{
			idx = (ActiveSndBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[idx][0] = MixBuffer[idx][1] = Subsonic_IIR_HPF_Left( YM2149_NextSample() );
		}
 		/* If Falcon emulation, crossbar does the job */
 		Crossbar_GenerateSamples(ActiveSndBufIdx, SamplesToGenerate);
	}
	else if (ConfigureParams.System.nMachineType != MACHINE_ST)
	{
		for (i = 0; i < SamplesToGenerate; i++)
		{
			idx = (ActiveSndBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[idx][0] = MixBuffer[idx][1] = YM2149_NextSample();
		}
 		/* If Ste or TT emulation, DmaSnd does mixing and filtering */
 		DmaSnd_GenerateSamples(ActiveSndBufIdx, SamplesToGenerate);
	}
	else if (ConfigureParams.System.nMachineType == MACHINE_ST)
	{
		for (i = 0; i < SamplesToGenerate; i++)
		{
			idx = (ActiveSndBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[idx][0] = MixBuffer[idx][1] = Subsonic_IIR_HPF_Left( YM2149_NextSample() );
		}
 	}

	ActiveSndBufIdx = (ActiveSndBufIdx + SamplesToGenerate) % MIXBUFFER_SIZE;
	nGeneratedSamples += SamplesToGenerate;
	CurrentSamplesNb += SamplesToGenerate;				/* number of samples generated for current VBL */
}


/*-----------------------------------------------------------------------*/
/**
 * This is called to built samples up until this clock cycle
 * Sound_Update can be called several times during a VBL ; we must ensure
 * that we generate exactly SamplesPerFrame samples between 2 calls
 * to Sound_Update_VBL.
 */
void Sound_Update(bool FillFrame)
{
	int OldSndBufIdx = ActiveSndBufIdx;
	int SamplesToGenerate;

	/* Make sure that we don't interfere with the audio callback function */
	Audio_Lock();

	/* Find how many samples to generate */
	SamplesToGenerate = Sound_SetSamplesPassed( FillFrame );

	/* And generate */
	Sound_GenerateSamples( SamplesToGenerate );

	/* Allow audio callback function to occur again */
	Audio_Unlock();

	/* Save to WAV file, if open */
	if (bRecordingWav)
		WAVFormat_Update(MixBuffer, OldSndBufIdx, SamplesToGenerate);
}


/*-----------------------------------------------------------------------*/
/**
 * On the end of each VBL, complete audio buffer to reach SamplesPerFrame samples.
 * As Sound_Update(false) could be called several times during the VBL, the audio
 * buffer might be already partially filled.
 * We must first complete the buffer using the same value of SamplesPerFrame
 * by calling Sound_Update(true) ; then we can compute a new value for
 * SamplesPerFrame that will be used for the next VBL to come.
 */
void Sound_Update_VBL(void)
{
	Sound_Update(true);					/* generate as many samples as needed to fill this VBL */
//fprintf ( stderr , "vbl done %d %d\n" , SamplesPerFrame , CurrentSamplesNb );

	CurrentSamplesNb = 0;					/* VBL is complete, reset counter for next VBL */

	/*Compute a fractional equivalent of SamplesPerFrame for the next VBL, to avoid rounding propagation */
	SamplesPerFrame_unrounded += (yms64) ClocksTimings_GetSamplesPerVBL ( ConfigureParams.System.nMachineType ,
			nScreenRefreshRate , nAudioFrequency );
	SamplesPerFrame = SamplesPerFrame_unrounded >> 28;		/* use integer part */
	SamplesPerFrame_unrounded &= 0x0fffffff;			/* keep fractional part in the lower 28 bits */

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

		Len = ActiveSndBufIdx - ActiveSndBufIdxAvi;	/* number of generated samples for this frame */
		if ( Len < 0 )
			Len += MIXBUFFER_SIZE;			/* end of ring buffer was reached */

		Avi_RecordAudioStream ( MixBuffer , ActiveSndBufIdxAvi , Len );
	}

	ActiveSndBufIdxAvi = ActiveSndBufIdx;			/* save new position for next AVI audio frame */

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

