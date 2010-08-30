/*
  Hatari - sound.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

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

  The new version of the sound core uses some code/ideas from the following GPL projects :
    - tone and noise steps computations are from StSound 1.2 by Arnaud Carré (Leonard/Oxygene)
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
/*			have sound.c independant of psg.c (to ease replacement of	*/
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
/* volume5=volume4*2+1, except for volumes 0 and 1 which become 0 and 2, */
/* in order to map [0,15] into [0,31] (O must remain 0, and 15 must give 31) */
static const ymu16 YmVolume4to5[ 16 ] = { 0,2,5,7,9,11,13,15,17,19,21,23,25,27,29,31 };

/* Table of unsigned 4 bit D/A output level for 3 channels as measured on a real ST */
static ymu16 volumetable_original[ 16 * 16 * 16 ] =
#include "ym2149_fixed_vol.h"

/* Corresponding table interpolated to 5 bit D/A output level (16 bits unsigned) */
static ymu16 ymout5_u16[ 32 * 32 * 32 ];

/* Same table, after conversion to signed results (same pointer, with different type) */
static yms16 *ymout5 = (yms16 *)ymout5_u16;



/*--------------------------------------------------------------*/
/* Other constants / macros					*/
/*--------------------------------------------------------------*/

/* Number of generated samples per frame (eg. 44Khz=882) */
#define SAMPLES_PER_FRAME  ((nAudioFrequency+35)/nScreenRefreshRate)

/* Current sound replay freq (usually 44100 Hz) */
#define YM_REPLAY_FREQ   nAudioFrequency

/* YM-2149 clock on Atari ST is 2 MHz */
#define YM_ATARI_CLOCK		(CPU_FREQ / 4)


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
/* Variables for the DC adjuster / Low Pass Filter		*/
/*--------------------------------------------------------------*/
#define DC_ADJUST_BUFFERLEN		512		/* must be a power of 2 */

static ymsample	dc_buffer[DC_ADJUST_BUFFERLEN];
static int	dc_pos;
static int	dc_sum;
static ymsample	m_lowPassFilter[2];



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

int		YmVolumeMixing = YM_LINEAR_MIXING;
bool		UseLowPassFilter = false;

bool		bEnvelopeFreqFlag;			/* Cleared each frame for YM saving */

Sint16		MixBuffer[MIXBUFFER_SIZE][2];
int		nGeneratedSamples;			/* Generated samples since audio buffer update */
int		nSamplesToGenerate;			/* How many samples are needed for this time-frame */
static int	ActiveSndBufIdx;			/* Current working index into above mix buffer */



/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static void	DcAdjuster_Reset	(void);
static void	DcAdjuster_AddSample	(ymsample sample);
static ymsample	DcAdjuster_GetDcLevel	(void);
static void	LowPassFilter_Reset	(void);
static ymsample	LowPassFilter		(ymsample in);

static int	volumetable_get		(int i, int j, int k);
static void	volumetable_set		(ymu16 *volumetable, int i, int j, int k, int val);
static int	volumetable_interpolate	(int y1, int y2);
static void	interpolate_volumetable	(ymu16 *out);

static void	YM2149_BuildLinearVolumeTable(ymu16 *out);
static void	YM2149_Normalise_5bit_Table(ymu16 *in_5bit , yms16 *out_5bit, unsigned int Level, bool DoCenter);

static void	YM2149_EnvBuild		(void);
static void	Ym2149_Init		(void);
static void	Ym2149_Reset		(void);

static ymu32	YM2149_RndCompute	(void);
static ymu32	Ym2149_ToneStepCompute	(ymu8 rHigh , ymu8 rLow);
static ymu32	Ym2149_NoiseStepCompute	(ymu8 rNoise);
static ymu32	Ym2149_EnvStepCompute	(ymu8 rHigh , ymu8 rLow);
static ymsample	YM2149_NextSample	(void);

static void	Sound_SetSamplesPassed	(void);
static void	Sound_GenerateSamples	(void);



/*--------------------------------------------------------------*/
/* DC Adjuster / Low Pass Filter routines.			*/
/*--------------------------------------------------------------*/

static void	DcAdjuster_Reset(void)
{
	int	i;

	for (i=0 ; i<DC_ADJUST_BUFFERLEN ; i++)
		dc_buffer[i] = 0;

	dc_pos = 0;
	dc_sum = 0;
}


static void	DcAdjuster_AddSample(ymsample sample)
{
	dc_sum -= dc_buffer[dc_pos];
	dc_sum += sample;

	dc_buffer[dc_pos] = sample;
	dc_pos = (dc_pos+1)&(DC_ADJUST_BUFFERLEN-1);
}


static ymsample	DcAdjuster_GetDcLevel(void)
{
	return dc_sum / DC_ADJUST_BUFFERLEN;
}


static void	LowPassFilter_Reset(void)
{
	m_lowPassFilter[0] = 0;
	m_lowPassFilter[1] = 0;
}


static ymsample	LowPassFilter(ymsample in)
{
	ymsample	out;
 
	out = (m_lowPassFilter[0]>>2) + (m_lowPassFilter[1]>>1) + (in>>2);
	m_lowPassFilter[0] = m_lowPassFilter[1];
	m_lowPassFilter[1] = in;
	return out;
}



/*--------------------------------------------------------------*/
/* Build the volume conversion table used to simulate the	*/
/* behaviour of DAC used with the YM2149 in the atari ST.	*/
/* The final 32*32*32 table is built using a 16*16*16 table	*/
/* of all possible fixed volume combinations on a ST.		*/
/*--------------------------------------------------------------*/

static int	volumetable_get(int i, int j, int k)
{
	/* access at boundary finds the last value instead of the first */
	if (i == 16)
		i = 15;
	if (j == 16)
		j = 15;
	if (k == 16)
		k = 15;

	return volumetable_original[i + 16 * j + 16 * 16 * k];
}

static void	volumetable_set(ymu16 *volumetable, int i, int j, int k, int val)
{
	volumetable[i + 32 * j + 32 * 32 * k] = val;
}

/* the table is exponential in nature. These weighing factors approximate
 * that the value in-between needs to be closer to the lower value in y2 */
static int	volumetable_interpolate(int y1, int y2)
{
	int erpolate;
	erpolate = (y1 * 4 + y2 * 6) / 10u;
	if (erpolate > 65535)
	{
//		fprintf ( stderr , "sature>:%d %d %d\n",erpolate,y1,y2 );
		erpolate = 65535;
	}
	if (erpolate < 0)
	{
//		fprintf ( stderr , "sature<:%d %d %d\n",erpolate,y1,y2 );
		erpolate = 0;
	}
	return erpolate;
}

static void	interpolate_volumetable(ymu16 *out)
{
	int i, j, k;
	int i1, i2;

	/* we are doing 4-dimensional interpolation here. For each
	* known measurement point, we must find 8 new values. These values occur
	* as follows:
	*
	* - one at the exact same position
	* - one half-way in i direction
	* - one half-way in j direction
	* - one half-way in k direction
	* - one half-way in i+j direction
	* - one half-way in i+k direction
	* - one half-way in j+k direction
	* - one half-way in i+j+k direction
	*
	* The algorithm currently is very simplistic. Probably more points should be
	* weighted in the multicomponented directions, for instance i+j+k should be
	* an average of all surrounding data points. This probably doesn't matter much,
	* though. This is because the only way to reach those locations is to modulate
	* more than one voice with the envelope, and this is rare.
	*/

	for (i = 0; i < 16; i++)
	{
		for (j = 0; j < 16; j++)
		{
			for (k = 0; k < 16; k++)
			{
				i1 = volumetable_get(i, j, k);
				/* copy value unchanged to new position */
				volumetable_set(out,i*2, j*2, k*2, i1);
 
				/* interpolate in i direction */
				i2 = volumetable_get(i + 1, j, k);
				volumetable_set(out,i*2 + 1, j*2, k*2, volumetable_interpolate(i1, i2));

				/* interpolate in j direction */
				i2 = volumetable_get(i, j+1, k);
				volumetable_set(out,i*2, j*2 + 1, k*2, volumetable_interpolate(i1, i2));

				/* interpolate in k direction */
				i2 = volumetable_get(i, j, k+1);
				volumetable_set(out,i*2, j*2, k*2+1, volumetable_interpolate(i1, i2));

				/* interpolate in i + j direction */
				i2 = volumetable_get(i + 1, j + 1, k);
				volumetable_set(out,i*2 + 1, j*2 + 1, k*2, volumetable_interpolate(i1, i2));

				/* interpolate in i + k direction */
				i2 = volumetable_get(i + 1, j, k + 1);
				volumetable_set(out,i*2 + 1, j*2, k*2 + 1, volumetable_interpolate(i1, i2));

				/* interpolate in j + k direction */
				i2 = volumetable_get(i, j + 1, k + 1);
				volumetable_set(out,i*2, j*2 + 1, k*2 + 1, volumetable_interpolate(i1, i2));

				/* interpolate in i + j + k direction */
				i2 = volumetable_get(i + 1, j + 1, k + 1);
				volumetable_set(out,i*2 + 1, j*2 + 1, k*2 + 1, volumetable_interpolate(i1, i2));
			}
		}
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Build a linear version of the conversion table.
 * We use the mean of the 3 volumes converted to 16 bit values
 * (each value of ymout1c5bit is in [0,65535])
 */

static void	YM2149_BuildLinearVolumeTable(ymu16 *out)
{
	int	i, j, k;
	int	res;

	for (i = 0; i < 32; i++)
		for (j = 0; j < 32; j++)
			for (k = 0; k < 32; k++)
			{
				res = ( ymout1c5bit[ i ] + ymout1c5bit[ j ] + ymout1c5bit[ k ] ) / 3;
				volumetable_set ( out, i, j, k, res );
			}
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
 */

static void	YM2149_Normalise_5bit_Table(ymu16 *in_5bit , yms16 *out_5bit, unsigned int Level, bool DoCenter)
{
	if ( Level )
	{
		int h;
		int Max = in_5bit[0x7fff];
		int Center = Level>>1;
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
 * Init some internal tables for faster results (env, volume)
 * and reset the internal states.
 */

static void	Ym2149_Init(void)
{
	/* Build the 16 envelope shapes */
	YM2149_EnvBuild();

	/* Depending on the volume mixing method, we use a table based on real measures */
	/* or a table based on a linear volume mixing. */
	if ( YmVolumeMixing == YM_TABLE_MIXING )
		interpolate_volumetable(ymout5_u16);	/* expand the 16*16*16 values in volumetable_original to 32*32*32 */
	else
		YM2149_BuildLinearVolumeTable(ymout5_u16);	/* combine the 32 possible volumes */

	/* Normalise/center the values (convert from u16 to s16) */
	YM2149_Normalise_5bit_Table ( ymout5_u16 , ymout5 , YM_OUTPUT_LEVEL , YM_OUTPUT_CENTERED );

	/* Reset YM2149 internal states */
	Ym2149_Reset();
}



/*-----------------------------------------------------------------------*/
/**
 * Reset all ym registers as well as the internal varaibles
 */

static void	Ym2149_Reset(void)
{
	int	i;

	for ( i=0 ; i<14 ; i++ )
		Sound_WriteReg ( i , 0 );

	Sound_WriteReg ( 7 , 0xff );

	currentNoise = 0xffff;

	RndRack = 1;

	envShape = 0;
	envPos = 0;

	DcAdjuster_Reset ();
	LowPassFilter_Reset ();
}



/*-----------------------------------------------------------------------*/
/**
 * Returns a pseudo random value, used to generate white noise.
 */

static ymu32	YM2149_RndCompute(void)
{
	ymu32	bit;

	bit = (RndRack&1) ^ ((RndRack>>2)&1);
	RndRack = (RndRack>>1) | (bit<<16);
	return (bit ? 0 : 0xffff);
}



/*-----------------------------------------------------------------------*/
/**
 * Compute steps for tone, noise and env, based on the input
 * period.
 */

static ymu32	Ym2149_ToneStepCompute(ymu8 rHigh , ymu8 rLow)
{
	int	per;
	yms64	step;

	per = rHigh&15;
	per = (per<<8)+rLow;
	if (per <= 5)
		return 0;

	step = YM_ATARI_CLOCK;
	step <<= (15+16-3);
	step /= (per * YM_REPLAY_FREQ);

	return step;
}


static ymu32	Ym2149_NoiseStepCompute(ymu8 rNoise)
{
	int	per;
	yms64	step;

	per = (rNoise&0x1f);
	if (per<3)
		return 0;

	step = YM_ATARI_CLOCK;
	step <<= (16-1-3);
	step /= (per * YM_REPLAY_FREQ);

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
 */

static ymsample	YM2149_NextSample(void)
{
	ymsample	sample;
	int		bt;
	ymu32		bn;
	ymu16		Env3Voices;
	ymu16		Tone3Voices;


	/* Noise value : 0 or 0xffff */
	if ( noisePos&0xffff0000 )
	{
		currentNoise ^= YM2149_RndCompute();
		noisePos &= 0xffff;
	}
	bn = currentNoise;				/* 0 or 0xffff */

	/* Get the 5 bits volume corresponding to the current envelope's position */
	Env3Voices = YmEnvWaves[ envShape ][ envPos>>24 ];	/* integer part of envPos is in bits 24-31 */
	Env3Voices &= EnvMask3Voices;			/* only keep volumes for voices using envelope */

//fprintf ( stderr , "env %x %x %x\n" , Env3Voices , envStep , envPos );

	/* Tone3Voices will contain the output state of each voice : 0 or 0x1f */
	bt = ((((yms32)posA)>>31) | mixerTA) & (bn | mixerNA);	/* 0 or 0xffff */
	Tone3Voices = bt & YM_MASK_1VOICE;			/* 0 or 0x1f */
	bt = ((((yms32)posB)>>31) | mixerTB) & (bn | mixerNB);
	Tone3Voices |= ( bt & YM_MASK_1VOICE ) << 5;
	bt = ((((yms32)posC)>>31) | mixerTC) & (bn | mixerNC);
	Tone3Voices |= ( bt & YM_MASK_1VOICE ) << 10;

	/* Combine fixed volumes and envelope volumes and keep the resulting */
	/* volumes depending on the output state of each voice (0 or 0x1f) */
	Tone3Voices &= ( Env3Voices | Vol3Voices );

	/* D/A conversion of the 3 volumes into a sample using a precomputed conversion table */
	sample = ymout5[ Tone3Voices ];			/* 16 bits signed value */


	/* Increment positions */
	posA += stepA;
	posB += stepB;
	posC += stepC;
	noisePos += noiseStep;

	envPos += envStep;
	if ( envPos >= (3*32) << 24 )			/* blocks 0, 1 and 2 were used (envPos 0 to 95) */
		envPos -= (2*32) << 24;			/* replay/loop blocks 1 and 2 (envPos 32 to 95) */

	DcAdjuster_AddSample(sample);			/* Calculate DC level */
	sample = sample - DcAdjuster_GetDcLevel();	/* normalize sound level */

	/* Apply low pass filter ? */
	if ( UseLowPassFilter )
	{
		sample = LowPassFilter(sample);
	}

	return sample;
}



/*-----------------------------------------------------------------------*/
/**
 * Update internal variables (steps, volume masks, ...) each
 * time an YM register is changed.
 */

void	Sound_WriteReg( int reg , Uint8 data )
{
	switch (reg)
	{
		case 0:
			SoundRegs[0] = data;
			stepA = Ym2149_ToneStepCompute ( SoundRegs[1] , SoundRegs[0] );
			if (!stepA) posA = 1u<<31;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 1:
			SoundRegs[1] = data & 0x0f;
			stepA = Ym2149_ToneStepCompute ( SoundRegs[1] , SoundRegs[0] );
			if (!stepA) posA = 1u<<31;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 2:
			SoundRegs[2] = data;
			stepB = Ym2149_ToneStepCompute ( SoundRegs[3] , SoundRegs[2] );
			if (!stepB) posB = 1u<<31;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 3:
			SoundRegs[3] = data & 0x0f;
			stepB = Ym2149_ToneStepCompute ( SoundRegs[3] , SoundRegs[2] );
			if (!stepB) posB = 1u<<31;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 4:
			SoundRegs[4] = data;
			stepC = Ym2149_ToneStepCompute ( SoundRegs[5] , SoundRegs[4] );
			if (!stepC) posC = 1u<<31;		// Assume output always 1 if 0 period (for Digi-sample)
			break;

		case 5:
			SoundRegs[5] = data & 0x0f;
			stepC = Ym2149_ToneStepCompute ( SoundRegs[5] , SoundRegs[4] );
			if (!stepC) posC = 1u<<31;		// Assume output always 1 if 0 period (for Digi-sample)
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
//fprintf ( stderr , "Sound_Reset SoundBufferSize %d SAMPLES_PER_FRAME %d nGeneratedSamples %d , ActiveSndBufIdx %d\n" ,
//	SoundBufferSize , SAMPLES_PER_FRAME, nGeneratedSamples , ActiveSndBufIdx );

	Ym2149_Reset();

	Audio_Unlock();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the sound buffer index variables.
 */
void Sound_ResetBufferIndex(void)
{
	Audio_Lock();
	nGeneratedSamples = SoundBufferSize + SAMPLES_PER_FRAME;
	ActiveSndBufIdx =  (CompleteSndBufIdx + nGeneratedSamples) % MIXBUFFER_SIZE;
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
 */
static void Sound_SetSamplesPassed(void)
{
	int nSampleCycles;
	int nSamplesPerFrame;
	int nSoundCycles;

	nSoundCycles = Cycles_GetCounter(CYCLES_COUNTER_SOUND);

	/* 160256 cycles per VBL, 44Khz = 882 samples per VBL */
	/* 882/160256 samples per clock cycle */
	nSamplesPerFrame = SAMPLES_PER_FRAME;

	nSamplesToGenerate = nSoundCycles * nSamplesPerFrame / CYCLES_PER_FRAME;
	if (nSamplesToGenerate > nSamplesPerFrame)
		nSamplesToGenerate = nSamplesPerFrame;

	nSampleCycles = nSamplesToGenerate * CYCLES_PER_FRAME / nSamplesPerFrame;
	nSoundCycles -= nSampleCycles;
	Cycles_SetCounter(CYCLES_COUNTER_SOUND, nSoundCycles);

	if (nSamplesToGenerate > MIXBUFFER_SIZE - nGeneratedSamples)
	{
		nSamplesToGenerate = MIXBUFFER_SIZE - nGeneratedSamples;
		if (nSamplesToGenerate < 0)
			nSamplesToGenerate = 0;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Generate samples for all channels during this time-frame
 */
static void Sound_GenerateSamples(void)
{
	int	i;
	int	idx;

	if (nSamplesToGenerate <= 0)
		return;

	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
	{
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			idx = (ActiveSndBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[idx][0] = MixBuffer[idx][1] = YM2149_NextSample();
		}
 		/* If Falcon emulation, crossbar does the job */
 		Crossbar_GenerateSamples(ActiveSndBufIdx, nSamplesToGenerate);
	}
	else if (ConfigureParams.System.nMachineType != MACHINE_ST)
	{
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			idx = (ActiveSndBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[idx][0] = MixBuffer[idx][1] = (YM2149_NextSample() >> 1);
		}
 		/* If Ste or TT emulation, DmaSnd does the job */
 		DmaSnd_GenerateSamples(ActiveSndBufIdx, nSamplesToGenerate);
	}
	else if (ConfigureParams.System.nMachineType == MACHINE_ST)
	{
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			idx = (ActiveSndBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[idx][0] = MixBuffer[idx][1] = YM2149_NextSample();
		}
		/* If ST emulation, DmaSnd doesn't do the job
		DmaSnd_GenerateSamples(ActiveSndBufIdx, nSamplesToGenerate); */
 	}

	ActiveSndBufIdx = (ActiveSndBufIdx + nSamplesToGenerate) % MIXBUFFER_SIZE;
	nGeneratedSamples += nSamplesToGenerate;
}


/*-----------------------------------------------------------------------*/
/**
 * This is called to built samples up until this clock cycle
 */
void Sound_Update(void)
{
	int OldSndBufIdx = ActiveSndBufIdx;

	/* Make sure that we don't interfere with the audio callback function */
	Audio_Lock();

	/* Find how many to generate */
	Sound_SetSamplesPassed();
	/* And generate */
	Sound_GenerateSamples();

	/* Allow audio callback function to occur again */
	Audio_Unlock();

	/* Save to WAV file, if open */
	if (bRecordingWav)
		WAVFormat_Update(MixBuffer, OldSndBufIdx, nSamplesToGenerate);
}


/*-----------------------------------------------------------------------*/
/**
 * On each VBL (50fps) complete samples.
 */
void Sound_Update_VBL(void)
{
	Sound_Update();

	/* Record audio frame is necessary */
	if ( bRecordingAvi )
	{
		int SamplePerVbl = nAudioFrequency / nScreenRefreshRate;
		int PrevIndex;

		PrevIndex = ActiveSndBufIdx - SamplePerVbl;
		if ( PrevIndex < 0 )
			PrevIndex += MIXBUFFER_SIZE;

		Avi_RecordAudioStream ( MixBuffer , PrevIndex , SamplePerVbl );
	}

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

