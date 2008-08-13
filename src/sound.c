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
  As with most 'sample' emulation it appears very quiet. We detect for any
  sample playback on a channel by a decay timer on the channel amplitude - this
  will remain high if the PSG register is constantly written to. We use this
  decay timer to boost the output of a sampled channel so the final sound is
  more even through-out.
  NOTE: If the emulator runs slower than 50fps it cannot update the buffers,
  but the sound thread still needs some data to play to prevent a 'pop'. The
  ONLY feasible solution is to play the same buffer again. I have tried all
  kinds of methods to play the sound 'slower', but this produces un-even timing
  in the sound and it simply doesn't work. If the emulator cannot keep the
  speed, users will have to turn off the sound - that's it.
*/

/* 2008/05/05	[NP]	Fix case where period is 0 for noise, sound or enveloppe.	*/
/*			In that case, a real ST sounds as if period was in fact 1.	*/
/*			(fix buggy sound replay in ESwat that set volume<0 and trigger	*/
/*			a badly initialised enveloppe with envper=0).			*/
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


const char Sound_rcsid[] = "Hatari $Id: sound.c,v 1.40 2008-08-13 22:26:50 npomarede Exp $";

#include <SDL_types.h>

#include "main.h"
#include "audio.h"
#include "cycles.h"
#include "dmaSnd.h"
#include "file.h"
#include "int.h"
#include "log.h"
#include "memorySnapShot.h"
#include "psg.h"
#include "sound.h"
#include "video.h"
#include "wavFormat.h"
#include "ymFormat.h"


#ifdef OLD_SOUND


#define LONGLONG Uint64

#define ENVELOPE_PERIOD(Fine,Coarse)  ((((Uint32)Coarse)<<8) + (Uint32)Fine)
#define NOISE_PERIOD(Freq)            (((((Uint32)Freq)&0x1f)<<11))
#define TONE_PERIOD(Fine,Coarse)      (((((Uint32)Coarse)&0x0f)<<8) + (Uint32)Fine)
#define MIXTABLE_SIZE    (256*8)        /* Large table, so don't overflow */
#define TONEFREQ_SHIFT   28             /* 4.28 fixed point */
#define NOISEFREQ_SHIFT  28             /* 4.28 fixed point */
#define ENVFREQ_SHIFT    16             /* 16.16 fixed */

#define SAMPLES_BUFFER_SIZE  1024
/* Number of generated samples per frame (eg. 44Khz=882) : */
#define SAMPLES_PER_FRAME  ((SoundPlayBackFrequencies[OutputAudioFreqIndex]+35)/nScreenRefreshRate)
/* Frequency of generated samples: */
#define SAMPLES_FREQ   (SoundPlayBackFrequencies[OutputAudioFreqIndex])
#define YM_FREQ        (2000000/SAMPLES_FREQ)      /* YM Frequency 2Mhz */


/* Original wave samples */
static int EnvelopeShapeValues[16*1024];                        /* Shape x Length(repeat 3rd/4th entries) */
/* Frequency and time period samples */
static Uint32 ChannelFreq[3], EnvelopeFreq, NoiseFreq;          /* Current frequency of each channel A,B,C,Envelope and Noise */
static int ChannelAmpDecayTime[3];                              /* Store counter to show if amplitude is changed to generate 'samples' */
static int Envelope[SAMPLES_BUFFER_SIZE],Noise[SAMPLES_BUFFER_SIZE];   /* Current sample for this time period */
/* Output channel data */
static int Channel_A_Buffer[SAMPLES_BUFFER_SIZE],Channel_B_Buffer[SAMPLES_BUFFER_SIZE],Channel_C_Buffer[SAMPLES_BUFFER_SIZE];
/* Use table to convert from (A+B+C) to clipped 8-bit for sound buffer */
static Sint8 MixTable[MIXTABLE_SIZE];                           /* -ve and +ve range */
static Sint8 *pMixTable = &MixTable[MIXTABLE_SIZE/2];           /* Signed index into above */
static int ActiveSndBufIdx;                                     /* Current working index into above mix buffer */
static int nSamplesToGenerate;                                  /* How many samples are needed for this time-frame */

/* global values */
bool bWriteEnvelopeFreq;                                        /* Did write to register '13' - causes frequency reset */
bool bWriteChannelAAmp, bWriteChannelBAmp, bWriteChannelCAmp;   /* Did write to amplitude registers? */
bool bEnvelopeFreqFlag;                                         /* As above, but cleared each frame for YM saving */
/* Buffer to store circular samples */
Sint8 MixBuffer[MIXBUFFER_SIZE];
int nGeneratedSamples;                                          /* Generated samples since audio buffer update */

Uint8	SoundRegs[ 14 ];					/* store YM regs 0 to 13 */


/*-----------------------------------------------------------------------*/
/* Envelope shape table */
typedef struct
{
	int WaveStart[4], WaveDelta[4];
} ENVSHAPE;

/* Envelope shapes */
static const ENVSHAPE EnvShapes[16] =
{
	{ {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  00xx  */
	{ {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  00xx  */
	{ {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  00xx  */
	{ {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  00xx  */
	{ {-128,-128,-128,-128},   {1, 0, 0, 0} },   /*  /_____  01xx  */
	{ {-128,-128,-128,-128},   {1, 0, 0, 0} },   /*  /_____  01xx  */
	{ {-128,-128,-128,-128},   {1, 0, 0, 0} },   /*  /_____  01xx  */
	{ {-128,-128,-128,-128},   {1, 0, 0, 0} },   /*  /_____  01xx  */
	{ {127,127,127,127},       {-1,-1,-1,-1} },  /*  \\\\\\  1000  */
	{ {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  1001  */
	{ {127,-128,127,-128},     {-1, 1,-1, 1} },  /*  \/\/\/  1010  */
	{ {127,127,127,127},       {-1, 0, 0, 0} },  /*  \~~~~~  1011  */
	{ {-128,-128,-128,-128},   {1, 1, 1, 1} },   /*  //////  1100  */
	{ {-128,127,127,127},      {1, 0, 0, 0} },   /*  /~~~~~  1101  */
	{ {-128,127,-128,127},     {1,-1, 1,-1} },   /*  /\/\/\  1110  */
	{ {-128,-128,-128,-128},   {1, 0, 0, 0} }    /*  /_____  1111  */
};

/* Square wave look up table */
static const int SquareWave[16] = { 127,127,127,127,127,127,127,127, -128,-128,-128,-128,-128,-128,-128,-128 };
/* LogTable */
static int LogTable[256];
static int LogTable16[16];
static int *pEnvelopeLogTable = &LogTable[128];


/*-----------------------------------------------------------------------*/
/**
 * Create Log tables
 */
static void Sound_CreateLogTables(void)
{
	float a;
	int i;

	/* Generate 'log' table for envelope output. It isn't quite a 'log' but it mimicks the ST */
	/* output very well */
	a = 1.0f;
	for (i = 0; i < 256; i++)
	{
		LogTable[255-i] = (int)(255*a);
		a /= 1.02f;
	}
	LogTable[0] = 0;

	/* And a 16 entry version(thanks to Nick for the '/= 1.5' bit) */
	/* This is VERY important for clear sample playback */
	a = 1.0f;
	for (i = 0; i < 15; i++)
	{
		LogTable16[15-i] = (int)(255*a);
		a /= 1.5f;
	}
	LogTable16[0] = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Limit integer between min/max range
 */
static int Sound_LimitInt(int Value, int MinRange, int MaxRange)
{
	if (Value < MinRange)
		Value = MinRange;
	else if (Value > MaxRange)
		Value = MaxRange;

	return Value;
}


/*-----------------------------------------------------------------------*/
/**
 * Create envelope shape, store to table
 * ( Wave is stored as 4 cycles, where cycles 1,2 are start and 3,4 are looped )
 */
static void Sound_CreateEnvelopeShape(const ENVSHAPE *pEnvShape,int *pEnvelopeValues)
{
	int i, j, Value;

	/* Create shape */
	for (i = 0; i < 4; i++)
	{
		Value = pEnvShape->WaveStart[i];        /* Set starting value for gradient */
		for (j = 0; j < 256; j++, Value += pEnvShape->WaveDelta[i])
			*pEnvelopeValues++ = Sound_LimitInt(Value, -128, 127);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Create YM2149 envelope shapes(x16)
 */
static void Sound_CreateEnvelopeShapes(void)
{
	int i;

	/* Create 'envelopes' for YM table */
	for (i = 0; i < 16; i++)
		Sound_CreateEnvelopeShape(&EnvShapes[i],&EnvelopeShapeValues[i*1024]);
}


/*-----------------------------------------------------------------------*/
/**
 * Create table to clip samples top 8-bit range
 * This keeps then 'signed', although many sound cards want 'unsigned' values,
 * but we keep them signed so we can vary the volume easily.
 */
static void Sound_CreateSoundMixClipTable(void)
{
	int i,v;

	/* Create table to 'clip' values to -128...127 */
	for (i = 0; i < MIXTABLE_SIZE; i++)
	{
		v = (float)(i-(MIXTABLE_SIZE/2)) * 0.3f;    /* Scale, to prevent clipping */
		if (v<-128)  v = -128;                      /* Limit -128..128 */
		if (v>127)  v = 127;
		MixTable[i] = v;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Init random generator, sound tables and envelopes
 */
static Uint32 RandomNum;

void Sound_Init(void)
{
	RandomNum = 1043618;	/* must be != 0 */
	Sound_CreateLogTables();
	Sound_CreateEnvelopeShapes();
	Sound_CreateSoundMixClipTable();

	Sound_Reset();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the sound emulation
 */
void Sound_Reset(void)
{
	int i;

	/* Lock audio system before accessing variables which are used by the
	 * callback function, too! */
	Audio_Lock();

	/* Clear sound mixing buffer: */
	memset(MixBuffer, 0, MIXBUFFER_SIZE);

	/* Clear cycle counts, buffer index and register '13' flags */
	Cycles_SetCounter(CYCLES_COUNTER_SOUND, 0);
	bEnvelopeFreqFlag = FALSE;
	bWriteEnvelopeFreq = FALSE;
	bWriteChannelAAmp = bWriteChannelBAmp = bWriteChannelCAmp = FALSE;

	CompleteSndBufIdx = 0;
	/* We do not start with 0 here to fake some initial samples: */
	nGeneratedSamples = SoundBufferSize + SAMPLES_PER_FRAME;
	ActiveSndBufIdx = nGeneratedSamples % MIXBUFFER_SIZE;

	/* Clear frequency counter */
	for (i = 0; i < 3; i++)
	{
		ChannelFreq[i] =
		    ChannelAmpDecayTime[i] = 0;
	}
	EnvelopeFreq = NoiseFreq = 0;

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
	Audio_Unlock();
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void Sound_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(ChannelFreq, sizeof(ChannelFreq));
	MemorySnapShot_Store(&EnvelopeFreq, sizeof(EnvelopeFreq));
	MemorySnapShot_Store(&NoiseFreq, sizeof(NoiseFreq));
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
	int Dec=1;
	int nSoundCycles;

	nSoundCycles = Cycles_GetCounter(CYCLES_COUNTER_SOUND);

	/* Check how many cycles have passed, as we use this to help find out if we are playing sample data */

	/* First, add decay to channel amplitude variables */
	if (nSoundCycles > (CYCLES_PER_FRAME/4))
		Dec = 16;                            /* Been long time between sound writes, must be normal tone sound */

	if (!bWriteChannelAAmp)                /* Not written to amplitude, decay value */
	{
		ChannelAmpDecayTime[0]-=Dec;
		if (ChannelAmpDecayTime[0]<0)  ChannelAmpDecayTime[0] = 0;
	}
	if (!bWriteChannelBAmp)
	{
		ChannelAmpDecayTime[1]-=Dec;
		if (ChannelAmpDecayTime[1]<0)  ChannelAmpDecayTime[1] = 0;
	}
	if (!bWriteChannelCAmp)
	{
		ChannelAmpDecayTime[2]-=Dec;
		if (ChannelAmpDecayTime[2]<0)  ChannelAmpDecayTime[2] = 0;
	}

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
 * Generate envelope wave for this time-frame
 */
static void Sound_GenerateEnvelope(unsigned char EnvShape, unsigned char Fine, unsigned char Coarse)
{
	int *pEnvelopeValues;
	Uint32 EnvelopePeriod, EnvelopeFreqDelta;
	int i;

	/* Find envelope details */
	if (bWriteEnvelopeFreq)
		EnvelopeFreq = 0;
	pEnvelopeValues = &EnvelopeShapeValues[ (EnvShape&0x0f)*1024 ]; /* Envelope shape values */
	EnvelopePeriod = ENVELOPE_PERIOD((Uint32)Fine, (Uint32)Coarse);

	if (EnvelopePeriod==0)                                          /* Handle div by zero */
		EnvelopePeriod = 1;					/* per=0 sounds like per=1 */

	EnvelopeFreqDelta = ((LONGLONG)YM_FREQ<<ENVFREQ_SHIFT) / (EnvelopePeriod);  /* 16.16 fixed point */

	/* Create envelope from current shape and frequency */
	for (i = 0; i < nSamplesToGenerate; i++)
	{
		Envelope[i] = pEnvelopeValues[EnvelopeFreq>>ENVFREQ_SHIFT]; /* Store envelope wave, already applied 'log' function */
		EnvelopeFreq += EnvelopeFreqDelta;
		if (EnvelopeFreq&0xfe000000)
			EnvelopeFreq = 0x02000000 | (EnvelopeFreq&0x01ffffff);  /* Keep in range 512-1024 once past 511! */
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Generate noise for this time-frame
 */
static inline Uint32 Random_Next(void)
{
	Uint32 Lo, Hi;

	Lo = 16807 * (Sint32)((Sint32)RandomNum & 0xffff);
	Hi = 16807 * (Sint32)((Uint32)RandomNum >> 16);
	Lo += (Hi & 0x7fff) << 16;
	if (Lo > 2147483647L)
	{
		Lo &= 2147483647L;
		++Lo;
	}
	Lo += Hi >> 15;
	if (Lo > 2147483647L)
	{
		Lo &= 2147483647L;
		++Lo;
	}
	RandomNum = Lo;
	return Lo;
}

static void Sound_GenerateNoise(unsigned char MixerControl, unsigned char NoiseGen)
{
	int NoiseValue;
	Uint32 NoisePeriod, NoiseFreqDelta;
	int i;

	NoisePeriod = NOISE_PERIOD((Uint32)NoiseGen);

	if (NoisePeriod==0)                              /* Handle div by zero */
		NoisePeriod = 1;				/* per=0 sounds like per=1 */

	NoiseFreqDelta = (((LONGLONG)YM_FREQ)<<NOISEFREQ_SHIFT) / NoisePeriod;  /* 4.28 fixed point */

	/* Generate noise samples */
	for (i = 0; i < nSamplesToGenerate; i++)
	{
		NoiseValue = Random_Next()%96;                 /* Get random value */
		if (SquareWave[NoiseFreq>>NOISEFREQ_SHIFT]<=0) /* Add to square wave at given frequency */
			NoiseValue = -NoiseValue;

		Noise[i] = NoiseValue;
		NoiseFreq += NoiseFreqDelta;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Generate channel of samples for this time-frame
 */
static void Sound_GenerateChannel(int *pBuffer, unsigned char ToneFine, unsigned char ToneCoarse, unsigned char Amplitude, unsigned char MixerControl, Uint32 *pChannelFreq, int MixMask)
{
	int *pNoise = Noise, *pEnvelope = Envelope;
	Uint32 ToneFreq = *pChannelFreq;
	Uint32 TonePeriod;
	Uint32 ToneFreqDelta;
	int i,Amp,Mix;
	int ToneOutput, NoiseOutput, MixerOutput, EnvelopeOutput, AmplitudeOutput;

	TonePeriod = TONE_PERIOD((Uint32)ToneFine, (Uint32)ToneCoarse);
	/* Find frequency of channel */
	if (TonePeriod==0)
//		TonePeriod = 1;					/* per=0 sounds like per=1 */
		ToneFreqDelta = 0;				/* Handle div by zero */
	else
		ToneFreqDelta = (((LONGLONG)YM_FREQ)<<TONEFREQ_SHIFT) / TonePeriod;    /* 4.28 fixed point */
	Amp = LogTable16[(Amplitude&0x0f)];
	Mix = (MixerControl>>MixMask)&9;                      /* Read I/O Mixer */

	/* Check if we are trying to play a 'sample' - we need to up the volume on these as they tend to be rather quiet */
	if ((Amplitude&0x10) == 0)                /* Fixed level amplitude? */
	{
		ChannelAmpDecayTime[MixMask]++;       /* Increment counter to find out if we are playing samples... */
		if (ChannelAmpDecayTime[MixMask]>16)
			ChannelAmpDecayTime[MixMask] = 16;  /* And limit */
	}

	for (i = 0; i < nSamplesToGenerate; i++)
	{
		/* Output from Tone Generator(0-255) */
		ToneOutput = SquareWave[ToneFreq>>TONEFREQ_SHIFT];

		/* Output from Noise Generator(0-255) */
		NoiseOutput = *pNoise++;
		/* Output from Mixer(combines Tone+Noise) */
		switch (Mix)
		{
		 case 0:    /* Has Noise and Tone */
			MixerOutput = NoiseOutput+ToneOutput;
			break;
		 case 1:    /* Has Noise */
			MixerOutput = NoiseOutput;
			break;
		 case 8:    /* Has Tone */
			MixerOutput = ToneOutput;
			break;

		 default:  /* This is used to emulate samples - should give no output, but ST gives set tone!!?? */
			/* MixerControl gets set to give a continuous tone and then then Amplitude */
			/* of channels A,B and C get changed with all other registers in the PSG */
			/* staying as zero's. This produces the sounds from Quartet, Speech, NoiseTracker etc...! */
			MixerOutput = 127;
		}

		EnvelopeOutput = pEnvelopeLogTable[*pEnvelope++];

		if ((Amplitude&0x10)==0)
		{
			AmplitudeOutput = Amp;          /* Fixed level amplitude */

			/* As with most emulators, sample playback is always 'quiet'. We check to see if */
			/* the amplitude of a channel is repeatedly changing and when this is detected we */
			/* scale the volume accordingly */
			if (ChannelAmpDecayTime[MixMask]>8)
				AmplitudeOutput <<= 1;        /* Scale up by a factor of 2 */
		}
		else
			AmplitudeOutput = EnvelopeOutput;

		*pBuffer++ = (MixerOutput*AmplitudeOutput)>>8;

		ToneFreq+=ToneFreqDelta;
	}

	/* Store back incremented frequency, for next call */
	*pChannelFreq = ToneFreq;
}


/*-----------------------------------------------------------------------*/
/**
 * Generate samples for all channels during this time-frame
 */
static void Sound_GenerateSamples(void)
{
	int *pChannelA=Channel_A_Buffer, *pChannelB=Channel_B_Buffer, *pChannelC=Channel_C_Buffer;
	int i;

	/* Anything to do? */
	if (nSamplesToGenerate>0)
	{
		/* Generate envelope/noise samples for this time */
		Sound_GenerateEnvelope(SoundRegs[PSG_REG_ENV_SHAPE],SoundRegs[PSG_REG_ENV_FINE],SoundRegs[PSG_REG_ENV_COARSE]);
		Sound_GenerateNoise(SoundRegs[PSG_REG_MIXER_CONTROL],SoundRegs[PSG_REG_NOISE_GENERATOR]);

		/* Generate 3 channels, store to separate buffer so can mix/clip */
		Sound_GenerateChannel(pChannelA,SoundRegs[PSG_REG_CHANNEL_A_FINE],SoundRegs[PSG_REG_CHANNEL_A_COARSE],SoundRegs[PSG_REG_CHANNEL_A_AMP],SoundRegs[PSG_REG_MIXER_CONTROL],&ChannelFreq[0],0);
		Sound_GenerateChannel(pChannelB,SoundRegs[PSG_REG_CHANNEL_B_FINE],SoundRegs[PSG_REG_CHANNEL_B_COARSE],SoundRegs[PSG_REG_CHANNEL_B_AMP],SoundRegs[PSG_REG_MIXER_CONTROL],&ChannelFreq[1],1);
		Sound_GenerateChannel(pChannelC,SoundRegs[PSG_REG_CHANNEL_C_FINE],SoundRegs[PSG_REG_CHANNEL_C_COARSE],SoundRegs[PSG_REG_CHANNEL_C_AMP],SoundRegs[PSG_REG_MIXER_CONTROL],&ChannelFreq[2],2);

		/* Mix channels together, using table to clip and convert to proper 8-bit type */
		for (i=0; i<nSamplesToGenerate; i++)
			MixBuffer[(i+ActiveSndBufIdx)%MIXBUFFER_SIZE] = pMixTable[(*pChannelA++) + (*pChannelB++) + (*pChannelC++)];

		DmaSnd_GenerateSamples(ActiveSndBufIdx, nSamplesToGenerate);

		ActiveSndBufIdx = (ActiveSndBufIdx + nSamplesToGenerate) % MIXBUFFER_SIZE;
		nGeneratedSamples += nSamplesToGenerate;

		/* Reset the write to register '13' flag */
		bWriteEnvelopeFreq = FALSE;
		/* And amplitude write flags */
		bWriteChannelAAmp = bWriteChannelBAmp = bWriteChannelCAmp = FALSE;
	}
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

	/* Clear write to register '13', used for YM file saving */
	bEnvelopeFreqFlag = FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Store the content of a PSG register and update the necessary variables.
 */
void Sound_WriteReg( int Reg , Uint8 Val )
{
	/* Store a local copy of the reg */
	SoundRegs[ Reg ] = Val;
	
	/* Writing to certain regs can require some actions */
	switch ( Reg )
	{

	 /* Check registers 8,9 and 10 which are 'amplitude' for each channel and
	  * store if wrote to (to check for sample playback) */
		case PSG_REG_CHANNEL_A_AMP:
			bWriteChannelAAmp = TRUE;
			break;
		case PSG_REG_CHANNEL_B_AMP:
			bWriteChannelBAmp = TRUE;
			break;
		case PSG_REG_CHANNEL_C_AMP:
			bWriteChannelCAmp = TRUE;
			break;

		case PSG_REG_ENV_SHAPE:			/* Whenever 'write' to register 13, cause envelope to reset */
			bEnvelopeFreqFlag = TRUE;
			bWriteEnvelopeFreq = TRUE;
			break;
	}

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
		return FALSE;
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
		bRet = FALSE;
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


#else	/* OLD_SOUND */

/*--------------------------------------------------------------*/
/* Possible YM1249 enveloppe shapes				*/
/*--------------------------------------------------------------*/

int		Env00xx[8]={ 1,0,0,0,0,0,0,0 };
int		Env01xx[8]={ 0,1,0,0,0,0,0,0 };
int		Env1000[8]={ 1,0,1,0,1,0,1,0 };
int		Env1001[8]={ 1,0,0,0,0,0,0,0 };
int		Env1010[8]={ 1,0,0,1,1,0,0,1 };
int		Env1011[8]={ 1,0,1,1,1,1,1,1 };
int		Env1100[8]={ 0,1,0,1,0,1,0,1 };
int		Env1101[8]={ 0,1,1,1,1,1,1,1 };
int		Env1110[8]={ 0,1,1,0,0,1,1,0 };
int		Env1111[8]={ 0,1,0,0,0,0,0,0 };
int		*EnvWave[16] = {	Env00xx,Env00xx,Env00xx,Env00xx,
					Env01xx,Env01xx,Env01xx,Env01xx,
					Env1000,Env1001,Env1010,Env1011,
					Env1100,Env1101,Env1110,Env1111};

static int	YmVolumeTable[16] = {62,161,265,377,580,774,1155,1575,2260,3088,4570,6233,9330,13187,21220,32767};


/* Variables for the YM2149 emulator */
Uint8		SoundRegs[14];

/* Number of generated samples per frame (eg. 44Khz=882) : */
#define SAMPLES_PER_FRAME  ((SoundPlayBackFrequencies[OutputAudioFreqIndex]+35)/nScreenRefreshRate)

/* Current sound replay freq (usually 44100 Hz) */
#define YM_REPLAY_FREQ   (SoundPlayBackFrequencies[OutputAudioFreqIndex])

/* YM-2149 clock on Atari ST is 2 MHz */
#define YM_ATARI_CLOCK                 2000000

ymu8		envData[16][2][16*2];

ymu32		stepA , stepB , stepC;
ymu32		posA , posB , posC;
ymu32		mixerTA , mixerTB , mixerTC;
ymu32		mixerNA , mixerNB , mixerNC;

ymu32		noiseStep;
ymu32		noisePos;
ymu32		currentNoise;

ymu32		envStep;
ymu32		envPos;
int		envPhase;
int		envShape;

int		volA , volB , volC , volE;
int		*pVolA , *pVolB , *pVolC;

ymu32		RndRack;				/* current random seed */


/* Variables for the DC adjuster / Low Pass Filter */
#define DC_ADJUST_BUFFERLEN		512		/* must be a power of 2 */

int		dc_buffer[DC_ADJUST_BUFFERLEN];
int		dc_pos;
int		dc_sum;
int		m_lowPassFilter[2];


/* global variables */
bool		UseLowPassFilter = FALSE;
bool		bEnvelopeFreqFlag;			/* Cleared each frame for YM saving */
Sint8		MixBuffer[MIXBUFFER_SIZE];
int		nGeneratedSamples;			/* Generated samples since audio buffer update */
int		nSamplesToGenerate;			/* How many samples are needed for this time-frame */
static int	ActiveSndBufIdx;			/* Current working index into above mix buffer */


/* Local functions */
static void	DcAdjuster_Reset(void);
static void	DcAdjuster_AddSample(int sample);
static int	DcAdjuster_GetDcLevel(void);
static void	LowPassFilter_Reset(void);
static int	LowPassFilter(int in);

static ymu8	*Ym2149_EnvInit(ymu8 *pEnv , int a , int b);
static void	Ym2149_Init(void);
static void	Ym2149_Reset(void);

static ymu32	YM2149_RndCompute(void);
static ymu32	Ym2149_ToneStepCompute(ymu8 rHigh , ymu8 rLow);
static ymu32	Ym2149_NoiseStepCompute(ymu8 rNoise);
static ymu32	Ym2149_EnvStepCompute(ymu8 rHigh , ymu8 rLow);
static ymsample	YM2149_NextSample(void);

static void	Sound_SetSamplesPassed(void);
static void	Sound_GenerateSamples(void);



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


static void	DcAdjuster_AddSample(int sample)
{
	dc_sum -= dc_buffer[dc_pos];
	dc_sum += sample;

	dc_buffer[dc_pos] = sample;
	dc_pos = (dc_pos+1)&(DC_ADJUST_BUFFERLEN-1);
}


static int	DcAdjuster_GetDcLevel(void)
{
	return dc_sum / DC_ADJUST_BUFFERLEN;
}


static void	LowPassFilter_Reset(void)
{
	m_lowPassFilter[0] = 0;
	m_lowPassFilter[1] = 0;
}


static int	LowPassFilter(int in)
{
	int	out;
 
	out = (m_lowPassFilter[0]>>2) + (m_lowPassFilter[1]>>1) + (in>>2);
	m_lowPassFilter[0] = m_lowPassFilter[1];
	m_lowPassFilter[1] = in;
	return out;
}



/*--------------------------------------------------------------*/
/* Various initialisations.					*/
/*--------------------------------------------------------------*/

static ymu8	*Ym2149_EnvInit(ymu8 *pEnv , int a , int b)
{
	int	i;
	int	d;

	d = b-a;
	a *= 15;
	for ( i=0 ; i<16 ; i++ )
	{
		*pEnv++ = (ymu8)a;
		a += d;
	}
	return pEnv;
}


static void	Ym2149_Init(void)
{
	int	i , env;
	ymu8	*pEnv;

	/* Adjust volumes */
	if ( YmVolumeTable[15] == 32767 )		/* not already initialized ? */
	{
		for ( i=0 ; i<16 ; i++ )
		{
			YmVolumeTable[i] = (YmVolumeTable[i]*2) / 6;
		}
	}

	/* Build the 16 enveloppe shapes */
	pEnv = &envData[0][0][0];
	for ( env=0 ; env<16 ; env++ )
	{
		int	*pse;
		int	phase;

		pse = EnvWave[env];
		for ( phase=0 ; phase<4 ; phase++ )
		{
			pEnv = Ym2149_EnvInit ( pEnv , pse[phase*2+0] , pse[phase*2+1] );
		}
	}

	/* Set volume voice pointers */
	pVolA = &volA;
	pVolB = &volB;
	pVolC = &volC;

	/* Reset YM2149 internal states */
	Ym2149_Reset();
}


static void	Ym2149_Reset(void)
{
	int	i;
	
	for ( i=0 ; i<14 ; i++ )
		Sound_WriteReg ( i , 0 );

	Sound_WriteReg ( 7 , 0xff );

	currentNoise = 0xffff;
	
	RndRack = 1;
	
	envShape = 0;
	envPhase = 0;
	envPos = 0;

	DcAdjuster_Reset ();
	LowPassFilter_Reset ();
}



/*--------------------------------------------------------------*/
/* Returns a pseudo random value, used to generate white noise.	*/
/*--------------------------------------------------------------*/

static ymu32	YM2149_RndCompute(void)
{
	ymu32	bit;
		
	bit = (RndRack&1) ^ ((RndRack>>2)&1);
	RndRack = (RndRack>>1) | (bit<<16);
	return (ymu32)(bit ? 0 : 0xffff);
}



/*--------------------------------------------------------------*/
/* Compute step for tone, noise and env, based on the input	*/
/* period.							*/
/*--------------------------------------------------------------*/

static ymu32	Ym2149_ToneStepCompute(ymu8 rHigh , ymu8 rLow)
{
	int	per;

	per = rHigh&15;
	per = (per<<8)+rLow;
	if (per<=5) 
		return 0;

#ifdef YM_INTEGER_ONLY
	yms64 step = YM_ATARI_CLOCK;
	step <<= (15+16-3);
	step /= (per * YM_REPLAY_FREQ);
#else
	ymfloat step = YM_ATARI_CLOCK;
	step /= ((ymfloat)per*8.0 * (ymfloat)YM_REPLAY_FREQ);
	step *= 32768.0*65536.0;
#endif

	return (ymu32)step;
}


static ymu32	Ym2149_NoiseStepCompute(ymu8 rNoise)
{
	int	per;

	per = (rNoise&0x1f);
	if (per<3)
		return 0;

#ifdef YM_INTEGER_ONLY
	yms64 step = YM_ATARI_CLOCK;
	step <<= (16-1-3);
	step /= (per * YM_REPLAY_FREQ);
#else
	ymfloat step = YM_ATARI_CLOCK;
	step /= ((ymfloat)per*8.0 * (ymfloat)YM_REPLAY_FREQ);
	step *= 65536.0/2.0;
#endif

	return (ymu32)step;
}


static ymu32	Ym2149_EnvStepCompute(ymu8 rHigh , ymu8 rLow)
{
	int	per;

	per = rHigh;
	per = (per<<8)+rLow;
	if (per<3)
		per=3;					/* needed for e-swat buggy replay */
//		return 0;

#ifdef YM_INTEGER_ONLY
	yms64 step = YM_ATARI_CLOCK;
	step <<= (16+16-9);
	step /= (per * YM_REPLAY_FREQ);
#else
	ymfloat step = YM_ATARI_CLOCK;
	step /= ((ymfloat)per*512.0 * (ymfloat)YM_REPLAY_FREQ);
	step *= 65536.0*65536.0;
#endif

	return (ymu32)step;
}



/*--------------------------------------------------------------*/
/* Main function : compute the value of the next sample.	*/
/* Mixes all 3 voices with tone+noise+env and apply low pass	*/
/* filter if needed.						*/
/*--------------------------------------------------------------*/

static ymsample	YM2149_NextSample(void)
{
	int	vol;
	int	bt;
	ymu32	bn;


	/* Noise value : 0 or 0xffff */
	if ( noisePos&0xffff0000 )
	{
		currentNoise ^= YM2149_RndCompute();
		noisePos &= 0xffff;
	}
	bn = currentNoise;

	/* Volume to apply if enveloppe is used */
	volE = YmVolumeTable[ envData[envShape][envPhase][envPos>>(32-5)] ];


	/* Tone+noise+env+DAC for three voices */
#if 0
	bt = ((((yms32)posA)>>31) | mixerTA) & (bn | mixerNA);
	vol  = (*pVolA)&bt;
	bt = ((((yms32)posB)>>31) | mixerTB) & (bn | mixerNB);
	vol += (*pVolB)&bt;
	bt = ((((yms32)posC)>>31) | mixerTC) & (bn | mixerNC);
	vol += (*pVolC)&bt;
#endif
	bt = ((((yms32)posA)>>31) | mixerTA) & (bn | mixerNA);
	if ( bt )		vol = *pVolA;
	else			vol = -*pVolA;
	
	bt = ((((yms32)posB)>>31) | mixerTB) & (bn | mixerNB);
	if ( bt )		vol = vol + *pVolB;
	else			vol = vol - *pVolB;
	
	bt = ((((yms32)posC)>>31) | mixerTC) & (bn | mixerNC);
	if ( bt )		vol = vol + *pVolC;
	else			vol = vol - *pVolC;

	/* Increment positions */
	posA += stepA;
	posB += stepB;
	posC += stepC;
	noisePos += noiseStep;
	envPos += envStep;
	if ( envPhase == 0 )
	{
		if ( envPos < envStep )
		{
			envPhase = 1;
		}
	}


	/* Apply low pass filter ? */
	if ( UseLowPassFilter )
	{
		DcAdjuster_AddSample ( vol );			/* normalize sound level */
		vol = LowPassFilter ( vol - DcAdjuster_GetDcLevel() );
	}

	return (ymsample)vol;
}



void	Sound_WriteReg( int reg , Uint8 data )
{
	switch (reg)
	{
		case 0:
			SoundRegs[0] = data&255;
			stepA = Ym2149_ToneStepCompute ( SoundRegs[1] , SoundRegs[0] );
			if (!stepA) posA = ((ymu32)1)<<31;	// Assume output always 1 if 0 period (for Digi-sample !)
			break;

		case 2:
			SoundRegs[2] = data&255;
			stepB = Ym2149_ToneStepCompute ( SoundRegs[3] , SoundRegs[2] );
			if (!stepB) posB = ((ymu32)1)<<31;	// Assume output always 1 if 0 period (for Digi-sample !)
			break;

		case 4:
			SoundRegs[4] = data&255;
			stepC = Ym2149_ToneStepCompute ( SoundRegs[5] , SoundRegs[4] );
			if (!stepC) posC = ((ymu32)1)<<31;	// Assume output always 1 if 0 period (for Digi-sample !)
			break;

		case 1:
			SoundRegs[1] = data&15;
			stepA = Ym2149_ToneStepCompute ( SoundRegs[1] , SoundRegs[0] );
			if (!stepA) posA = ((ymu32)1)<<31;	// Assume output always 1 if 0 period (for Digi-sample !)
			break;

		case 3:
			SoundRegs[3] = data&15;
			stepB = Ym2149_ToneStepCompute ( SoundRegs[3] , SoundRegs[2] );
			if (!stepB) posB = ((ymu32)1)<<31;	// Assume output always 1 if 0 period (for Digi-sample !)
			break;

		case 5:
			SoundRegs[5] = data&15;
			stepC = Ym2149_ToneStepCompute ( SoundRegs[5] , SoundRegs[4] );
			if (!stepC) posC = ((ymu32)1)<<31;	// Assume output always 1 if 0 period (for Digi-sample !)
			break;

		case 6:
			SoundRegs[6] = data&0x1f;
			noiseStep = Ym2149_NoiseStepCompute ( SoundRegs[6] );
			if (!noiseStep)
			{
				noisePos = 0;
				currentNoise = 0xffff;
			}
			break;

		case 7:
			SoundRegs[7] = data&255;
			mixerTA = (data&(1<<0)) ? 0xffff : 0;
			mixerTB = (data&(1<<1)) ? 0xffff : 0;
			mixerTC = (data&(1<<2)) ? 0xffff : 0;
			mixerNA = (data&(1<<3)) ? 0xffff : 0;
			mixerNB = (data&(1<<4)) ? 0xffff : 0;
			mixerNC = (data&(1<<5)) ? 0xffff : 0;
			break;

		case 8:
			SoundRegs[8] = data&31;
			volA = YmVolumeTable[data&15];
			if (data&0x10)
				pVolA = &volE;
			else
				pVolA = &volA;
			break;
		
		case 9:
			SoundRegs[9] = data&31;
			volB = YmVolumeTable[data&15];
			if (data&0x10)
				pVolB = &volE;
			else
				pVolB = &volB;
			break;
		
		case 10:
			SoundRegs[10] = data&31;
			volC = YmVolumeTable[data&15];
			if (data&0x10)
				pVolC = &volE;
			else
				pVolC = &volC;
			break;

		case 11:
			SoundRegs[11] = data&255;
			envStep = Ym2149_EnvStepCompute ( SoundRegs[12] , SoundRegs[11] );
			break;

		case 12:
			SoundRegs[12] = data&255;
			envStep = Ym2149_EnvStepCompute ( SoundRegs[12] , SoundRegs[11] );
			break;

		case 13:
			SoundRegs[13] = data&0xf;
			envPos = 0;
			envPhase = 0;
			envShape = data&0xf;
			bEnvelopeFreqFlag = TRUE;	/* used for YmFormat saving */
			break;

	}
}



/*-----------------------------------------------------------------------*/
/**
 * Init random generator, sound tables and envelopes
 */
void Sound_Init(void)
{
	Ym2149_Init();

	Sound_Reset();
}


/*-----------------------------------------------------------------------*/
/**
 * Reset the sound emulation
 */
void Sound_Reset(void)
{
	/* Lock audio system before accessing variables which are used by the
	 * callback function, too! */
	Audio_Lock();

	/* Clear sound mixing buffer: */
	memset(MixBuffer, 0, MIXBUFFER_SIZE);

	/* Clear cycle counts, buffer index and register '13' flags */
	Cycles_SetCounter(CYCLES_COUNTER_SOUND, 0);
	bEnvelopeFreqFlag = FALSE;
	
	CompleteSndBufIdx = 0;
	/* We do not start with 0 here to fake some initial samples: */
	nGeneratedSamples = SoundBufferSize + SAMPLES_PER_FRAME;
	ActiveSndBufIdx = nGeneratedSamples % MIXBUFFER_SIZE;

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
	Audio_Unlock();
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void Sound_MemorySnapShot_Capture(bool bSave)
{
	Uint32	dummy;
	/* Save/Restore details */
//	MemorySnapShot_Store(ChannelFreq, sizeof(ChannelFreq));
//	MemorySnapShot_Store(&EnvelopeFreq, sizeof(EnvelopeFreq));
//	MemorySnapShot_Store(&NoiseFreq, sizeof(NoiseFreq));
	
	MemorySnapShot_Store(&dummy, sizeof(dummy));
	MemorySnapShot_Store(&dummy, sizeof(dummy));
	MemorySnapShot_Store(&dummy, sizeof(dummy));
	MemorySnapShot_Store(&dummy, sizeof(dummy));
	MemorySnapShot_Store(&dummy, sizeof(dummy));
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
	int	nb;
	int	i = 0;
	
	nb = nSamplesToGenerate;
	if ( nb > 0 )
	{
		do
		{
			MixBuffer[(i+ActiveSndBufIdx)%MIXBUFFER_SIZE] = (Sint8)(YM2149_NextSample() >> 8);
			i++;		
		}
		while (--nb);
	
		DmaSnd_GenerateSamples(ActiveSndBufIdx, nSamplesToGenerate);

		ActiveSndBufIdx = (ActiveSndBufIdx + nSamplesToGenerate) % MIXBUFFER_SIZE;
		nGeneratedSamples += nSamplesToGenerate;
	}
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

	/* Clear write to register '13', used for YM file saving */
	bEnvelopeFreqFlag = FALSE;
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
		return FALSE;
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
		bRet = FALSE;
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


#endif	/* OLD_SOUND */

