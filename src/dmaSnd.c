/*
  Hatari - dmaSnd.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  STE DMA sound emulation. Does not seem to be very hard at first glance,
  but since the DMA sound has to be mixed together with the PSG sound and
  the output frequency of the host computer differs from the DMA sound
  frequency, the copy function is a little bit complicated.
  The update function also triggers the ST interrupts (Timer A and MFP-i7)
  which are often used in ST programs for setting a new sound frame after
  the old one has finished.

  To support programs that write into the frame buffer while it's played,
  we should update dma sound on each video HBL.
  This is also how it works on a real STE : bytes are read by the DMA
  at the end of each HBL and stored in a small FIFO (8 bytes) that is sent
  to the DAC depending on the chosen DMA output freq.

  Falcon sound emulation is all taken into account in crossbar.c


  Hardware I/O registers:

    $FF8900 (word) : DMA sound control register
    $FF8903 (byte) : Frame Start Hi
    $FF8905 (byte) : Frame Start Mi
    $FF8907 (byte) : Frame Start Lo
    $FF8909 (byte) : Frame Count Hi
    $FF890B (byte) : Frame Count Mi
    $FF890D (byte) : Frame Count Lo
    $FF890F (byte) : Frame End Hi
    $FF8911 (byte) : Frame End Mi
    $FF8913 (byte) : Frame End Lo
    $FF8920 (word) : Sound Mode Control (frequency, mono/stereo)
    $FF8922 (byte) : Microwire Data Register
    $FF8924 (byte) : Microwire Mask Register

  
  The Microwire and LMC 1992 commands :
    
    a command looks like: 10 CCC DDD DDD
    
    chipset address : 10
    command : 
	000 XXX XDD Mixing
		00 : DMA and (YM2149 - 12dB) mixing
		01 : DMA and YM2149 mixing
		10 : DMA only
		11 : Reserved

	001 XXD DDD Bass
		0 000 : -12 dB
		0 110 :   0 dB
		1 100 : +12 dB
      
	002 XXD DDD Treble
		0 000 : -12 dB
		0 110 :   0 dB
		1 100 : +12 dB

	003 DDD DDD Master volume
		000 000 : -80 dB
		010 100 : -40 dB
		101 XXX :   0 dB
	
	004 XDD DDD Right channel volume
		00 000 : -40 dB
		01 010 : -20 dB
		10 1XX :   0 dB

	005 XDD DDD  Left channel volume
		00 000 : -40 dB
		01 010 : -20 dB
		10 1XX :   0 dB
	      
	Other : undefined

	LMC1992 IIR code Copyright by David Savinkoff 2010

	A first order bass filter is multiplied with a
	first order treble filter to make a single
	second order IIR shelf filter.

	Sound is stereo filtered by Boosting or Cutting
	the Bass and Treble by +/-12dB in 2dB steps.

	This filter sounds exactly as the Atari TT or STE.
	Sampling frequency = selectable
	Bass turnover = 118.276Hz    (8.2nF on LM1992 bass)
	Treble turnover = 8438.756Hz (8.2nF on LM1992 treble)
*/


const char DmaSnd_fileid[] = "Hatari dmaSnd.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "cycInt.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "sound.h"
#include "stMemory.h"

#define TONE_STEPS 13


/* Global variables that can be changed/read from other parts of Hatari */

static void DmaSnd_Apply_LMC(int nMixBufIdx, int nSamplesToGenerate);
static void DmaSnd_Set_Tone_Level(int set_bass, int set_treb);
static float DmaSnd_IIRfilterL(float xn);
static float DmaSnd_IIRfilterR(float xn);
static struct first_order_s *DmaSnd_Treble_Shelf(float g, float fc, float Fs);
static struct first_order_s *DmaSnd_Bass_Shelf(float g, float fc, float Fs);
static Sint16 DmaSnd_LowPassFilterLeft(Sint16 in);
static Sint16 DmaSnd_LowPassFilterRight(Sint16 in);
static bool DmaSnd_LowPass;


Uint16 nDmaSoundControl;                /* Sound control register */

struct first_order_s  { float a1, b0, b1; };
struct second_order_s { float a1, a2, b0, b1, b2; };

struct dma_s {
	Uint16 soundMode;		/* Sound mode register */
	Uint32 frameStartAddr;		/* Sound frame start */
	Uint32 frameEndAddr;		/* Sound frame end */
	Uint32 frameCounter_int;	/* Counter in current sound frame, integer part */
	Uint32 frameCounter_dec;	/* Counter in current sound frame, decimal part */
	Uint32 frameLen;		/* Length of the frame */
};
Sint64 frameCounter_float;


struct microwire_s {
	Uint16 data;			/* Microwire Data register */
	Uint16 mask;			/* Microwire Mask register */
	int mwTransferSteps;		/* Microwire shifting counter */
	Uint16 mixing;			/* Mixing command */
	Uint16 bass;			/* Bass command */
	Uint16 treble;			/* Treble command */
	Uint16 masterVolume;		/* Master volume command */
	Uint16 leftVolume;		/* Left channel volume command */
	Uint16 rightVolume;		/* Right channel volume command */
};

struct lmc1992_s {
	struct first_order_s bass_table[TONE_STEPS];
	struct first_order_s treb_table[TONE_STEPS];
	float coef[5];			/* IIR coefficients */
	float left_gain;
	float right_gain;
};

static struct dma_s dma;
static struct microwire_s microwire;
static struct lmc1992_s lmc1992;

/* dB = 20log(gain)  :  gain = antilog(dB/20)                                */
/* Table gain values = (int)(powf(10.0, dB/20.0)*65536.0 + 0.5)  2dB steps   */


/* Values for LMC1992 Master volume control (*65536) */
static const Uint16 LMC1992_Master_Volume_Table[64] =
{
	    7,     8,    10,    13,    16,    21,    26,    33,    41,    52,  /* -80dB */
	   66,    83,   104,   131,   165,   207,   261,   328,   414,   521,  /* -60dB */
	  655,   825,  1039,  1308,  1646,  2072,  2609,  3285,  4135,  5206,  /* -40dB */
	 6554,  8250, 10387, 13076, 16462, 20724, 26090, 32846, 41350, 52057,  /* -20dB */
	65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535,  /*   0dB */
	65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535,  /*   0dB */
	65535, 65535, 65535, 65535                                             /*   0dB */
};

/* Values for LMC1992 Left and right volume control (*65536) */
static const Uint16 LMC1992_LeftRight_Volume_Table[32] =
{
	  655,   825,  1039,  1308,  1646,  2072,  2609,  3285,  4135,  5206,  /* -40dB */
	 6554,  8250, 10387, 13076, 16462, 20724, 26090, 32846, 41350, 52057,  /* -20dB */
	65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535,  /*   0dB */
	65535, 65535                                                           /*   0dB */
};

/* Values for LMC1992 BASS and TREBLE */
static const Sint16 LMC1992_Bass_Treble_Table[16] =
{
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12, 12, 12
};

static const int DmaSndSampleRates[4] =
{
	6258, 12517, 25033, 50066
};


/*-----------------------------------------------------------------------*/
/**
 * Init DMA sound variables.
 */
void DmaSnd_Init(void)
{
	DmaSnd_Reset(1);
}

/**
 * Reset DMA sound variables.
 */
void DmaSnd_Reset(bool bCold)
{
	nDmaSoundControl = 0;

	if (bCold)
	{
		dma.soundMode = 3;
		microwire.masterVolume = 7;
		microwire.leftVolume = 655;
		microwire.rightVolume = 655;
		microwire.mixing = 0;
		microwire.bass = 6;
		microwire.treble = 6;
	}

	/* Initialise microwire LMC1992 IIR filter parameters */
	DmaSnd_Init_Bass_and_Treble_Tables();

	microwire.mwTransferSteps = 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void DmaSnd_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&nDmaSoundControl, sizeof(nDmaSoundControl));
	MemorySnapShot_Store(&dma, sizeof(dma));
	MemorySnapShot_Store(&microwire, sizeof(microwire));
	MemorySnapShot_Store(&lmc1992, sizeof(lmc1992));
}


static int DmaSnd_DetectSampleRate(void)
{
	return DmaSndSampleRates[dma.soundMode & 3];
}


/*-----------------------------------------------------------------------*/
/**
 * This function is called when a new sound frame is started.
 * It copies the start and end address from the I/O registers, calculates
 * the sample length, etc.
 */
static void DmaSnd_StartNewFrame(void)
{
//	int nCyclesForFrame;

	dma.frameStartAddr = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | (IoMem[0xff8907] & ~1);
	dma.frameEndAddr = (IoMem[0xff890f] << 16) | (IoMem[0xff8911] << 8) | (IoMem[0xff8913] & ~1);

	LOG_TRACE(TRACE_DMASND, "DMA snd new frame start=%x end=%x\n", dma.frameStartAddr, dma.frameEndAddr);

	dma.frameCounter_int = 0;
	dma.frameCounter_dec = 0;
	frameCounter_float = 0;

	dma.frameLen = dma.frameEndAddr - dma.frameStartAddr;

	if (dma.frameEndAddr <= dma.frameStartAddr)
	{
		Log_Printf(LOG_WARN, "DMA snd: Illegal buffer size (from 0x%x to 0x%x)\n",
		          dma.frameStartAddr, dma.frameEndAddr);
		dma.frameLen = 0;
		return;
	}

// [NP] No more need for a timer since dma sound is updated on each HBL
//	/* To get smooth sound, set an "interrupt" for the end of the frame that
//	 * updates the sound mix buffer. */
//	nCyclesForFrame = dma.frameLen * (CPU_FREQ / DmaSnd_DetectSampleRate());
//	if (!(dma.soundMode & DMASNDMODE_MONO))  /* Is it stereo? */
//		nCyclesForFrame = nCyclesForFrame / 2;
//	CycInt_AddRelativeInterrupt(nCyclesForFrame, INT_CPU_CYCLE, INTERRUPT_DMASOUND);
}


/*-----------------------------------------------------------------------*/
/**
 * End-of-frame has been reached. Raise interrupts if needed.
 * Returns true if DMA sound processing should be stopped now and false
 * if it continues (DMA PLAYLOOP mode).
 */
static inline int DmaSnd_EndOfFrameReached(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd end of frame\n");

	/* Raise end-of-frame interrupts (MFP-i7 and Time-A) */
	MFP_InputOnChannel(MFP_TIMER_GPIP7_BIT, MFP_IERA, &MFP_IPRA);
	if (MFP_TACR == 0x08)       /* Is timer A in Event Count mode? */
		MFP_TimerA_EventCount_Interrupt();

	if (nDmaSoundControl & DMASNDCTRL_PLAYLOOP)
	{
		DmaSnd_StartNewFrame();
	}
	else
	{
		nDmaSoundControl &= ~DMASNDCTRL_PLAY;

		/* DMA sound is stopped, remove interrupt */
		CycInt_RemovePendingInterrupt(INTERRUPT_DMASOUND);

		return true;
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Mix DMA sound sample with the normal PSG sound samples.
 * Note: We adjust the volume level of the 8-bit DMA samples to factor
 * 0.75 compared to the PSG sound samples.
 *
 * The following formula: -((256*3/4)/4)/4
 *
 * Multiply by 256 to convert 8 to 16 bits;
 * DMA sound is 3/4 level of YM sound;
 * Divide by 4 to account for MixBuffer[];
 * Divide by 4 to account for DmaSnd_LowPassFilter;
 * Multiply DMA sound by -1 because the LMC1992 inverts the signal
 * ( YM sign is +1 :: -1(op-amp) * -1(Lmc1992) ).
 */
void DmaSnd_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate)
{
#ifdef olddma
	Uint32 FreqRatio;
#else
	Sint64 FreqRatio;
#endif
	int i, intPart;
	int nBufIdx;
	Sint8 *pFrameStart;
	Sint16 FrameMono = 0, FrameLeft = 0, FrameRight = 0;
	unsigned n;

	if (!(nDmaSoundControl & DMASNDCTRL_PLAY))
	{
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;

			switch (microwire.mixing) {
				case 1:
					/* YM2149 */
					break;
				default:
					/* YM2149 - 12dB */
					MixBuffer[nBufIdx][0] /= 4;
					MixBuffer[nBufIdx][1] = MixBuffer[nBufIdx][0];	
					break;
			}
		}

		/* Apply LMC1992 sound modifications (Bass and Treble) */
		DmaSnd_Apply_LMC ( nMixBufIdx , nSamplesToGenerate );
		
		return;
	}

	pFrameStart = (Sint8 *)&STRam[dma.frameStartAddr];

	/* Compute ratio between hatari's sound frequency and host computer's sound frequency */
#ifdef olddma
	FreqRatio = (Uint32)(DmaSnd_DetectSampleRate() / (double)nAudioFrequency * 65536.0 + 0.5);
#else
FreqRatio = ( ((Sint64)DmaSnd_DetectSampleRate()) << 32 ) / nAudioFrequency;
dma.frameCounter_int = frameCounter_float >> 32;
#endif
	if (dma.soundMode & DMASNDMODE_MONO)
	{
		/* Mono 8-bit */

		n = dma.frameCounter_int;

		for (i = 0; i < nSamplesToGenerate; i++)
		{
			/* Is end of DMA buffer reached ? */
			if (dma.frameCounter_int >= dma.frameLen) {
				if (DmaSnd_EndOfFrameReached())
					break;
				else
				{	n = dma.frameCounter_int;
					pFrameStart = (Sint8 *)&STRam[dma.frameStartAddr];
				}
			}

			/* Apply anti-aliasing low pass filter ? (mono) */
			for ( ; n <= dma.frameCounter_int; n++) {
				FrameMono   =  DmaSnd_LowPassFilterLeft((Sint16)(pFrameStart[n]));
				/* No-Click */ DmaSnd_LowPassFilterRight((Sint16)(pFrameStart[n]));
			}

			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;

			switch (microwire.mixing) {
				case 1:
					/* DMA and YM2149 mixing */
					MixBuffer[nBufIdx][0] = MixBuffer[nBufIdx][0] + FrameMono * -((256*3/4)/4)/4;
					break;
				case 2:
					/* DMA sound only */
					MixBuffer[nBufIdx][0] = FrameMono * -((256*3/4)/4)/4;
					break;
				default:
					/* DMA and (YM2149 - 12dB) mixing */
					/* instead of 16462 (-12dB), we approximate by 16384 */
					MixBuffer[nBufIdx][0] = (FrameMono * -((256*3/4)/4)/4) +
								(((Sint32)MixBuffer[nBufIdx][0] * 16384)/65536);
					break;
			}
			MixBuffer[nBufIdx][1] = MixBuffer[nBufIdx][0];	

			/* Increase ratio pointer to next DMA sample */
#ifdef olddma
			dma.frameCounter_dec += FreqRatio;
			if (dma.frameCounter_dec >= 65536) {
				intPart = dma.frameCounter_dec >> 16;
				dma.frameCounter_int += intPart;
				dma.frameCounter_dec -= intPart* 65536;
			}
#else
			frameCounter_float += FreqRatio;
#endif
		}
	}
	else
	{
		/* Stereo 8-bit */

		FreqRatio *= 2;
		n = dma.frameCounter_int & ~1;

		for (i = 0; i < nSamplesToGenerate; i++)
		{
			/* Is end of DMA buffer reached ? */
			if ((dma.frameCounter_int | 1) >= dma.frameLen) {
				if (DmaSnd_EndOfFrameReached())
					break;
				else
				{	n = dma.frameCounter_int & ~1;
					pFrameStart = (Sint8 *)&STRam[dma.frameStartAddr];
				}
			}

			/* Apply anti-aliasing low pass filter ? (stereo) */
			for ( ; n <= (dma.frameCounter_int & ~1); n += 2) {
				FrameLeft  = DmaSnd_LowPassFilterLeft((Sint16)(pFrameStart[n]));
				FrameRight = DmaSnd_LowPassFilterRight((Sint16)(pFrameStart[n+1]));
			}

			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			
			switch (microwire.mixing) {
				case 1:
					/* DMA and YM2149 mixing */
					MixBuffer[nBufIdx][0] = MixBuffer[nBufIdx][0] + FrameLeft * -((256*3/4)/4)/4;
					MixBuffer[nBufIdx][1] = MixBuffer[nBufIdx][1] + FrameRight * -((256*3/4)/4)/4;
					break;
				case 2:
					/* DMA sound only */
					MixBuffer[nBufIdx][0] = FrameLeft * -((256*3/4)/4)/4;
					MixBuffer[nBufIdx][1] = FrameRight * -((256*3/4)/4)/4;
					break;
				default:
					/* DMA and (YM2149 - 12dB) mixing */
					/* instead of 16462 (-12dB), we approximate by 16384 */
					MixBuffer[nBufIdx][0] = (FrameLeft * -((256*3/4)/4)/4) +
								(((Sint32)MixBuffer[nBufIdx][0] * 16384)/65536);
					MixBuffer[nBufIdx][1] = (FrameRight * -((256*3/4)/4)/4) +
								(((Sint32)MixBuffer[nBufIdx][1] * 16384)/65536);
					break;
			}

			/* Increase ratio pointer to next DMA sample */
#ifdef olddma
			dma.frameCounter_dec += FreqRatio;
			if (dma.frameCounter_dec >= 65536) {
				intPart = dma.frameCounter_dec >> 16;
				dma.frameCounter_int += intPart;
				dma.frameCounter_dec -= intPart* 65536;
			}
#else
			frameCounter_float += FreqRatio;
#endif
		}
	}

	/* Apply LMC1992 sound modifications (Bass and Treble) */
	DmaSnd_Apply_LMC ( nMixBufIdx , nSamplesToGenerate );
}


/*-----------------------------------------------------------------------*/
/**
 * Apply LMC1992 sound modifications (Bass and Treble)
 * The Bass and Treble get samples at nAudioFrequency rate.
 * The tone control's sampling frequency must be at least 22050 Hz to sound good.
 */
static void DmaSnd_Apply_LMC(int nMixBufIdx, int nSamplesToGenerate)
{
	int nBufIdx;
	int i;

	/* Apply LMC1992 sound modifications (Left, Right and Master Volume) */
	for (i = 0; i < nSamplesToGenerate; i++) {
		nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
		MixBuffer[nBufIdx][0] = DmaSnd_IIRfilterL(MixBuffer[nBufIdx][0]);
		MixBuffer[nBufIdx][1] = DmaSnd_IIRfilterR(MixBuffer[nBufIdx][1]);
 	}
}


/*-----------------------------------------------------------------------*/
/**
 * DMA sound end of frame "interrupt". Used for updating the sound after
 * a frame has been finished.
 */
void DmaSnd_InterruptHandler(void)
{
	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* Update sound */
	Sound_Update(false);
}


/*-----------------------------------------------------------------------*/
/**
 * STE DMA sound is using an 8 bytes FIFO that is checked and filled on each HBL
 * (at 50066 Hz 8 bit stereo, the DMA requires approx 6.5 new bytes per HBL)
 * Calling Sound_Update on each HBL allows to emulate some programs that modify
 * the data between FrameStart and FrameEnd while DMA sound is ON
 * (eg the demo 'Mental Hangover' or the game 'Power Up Plus')
 * This function should be called from the HBL's handler (in video.c)
 */
void DmaSnd_STE_HBL_Update(void)
{
	if ( ConfigureParams.System.nMachineType != MACHINE_STE )
		return;

	/* If DMA sound is ON, update sound */
	if (nDmaSoundControl & DMASNDCTRL_PLAY)
		Sound_Update(false);
}


/*-----------------------------------------------------------------------*/
/**
 * Create actual position for frame count registers.
 */
static Uint32 DmaSnd_GetFrameCount(void)
{
	Uint32 nActCount;

	/* Update sound to get the current DMA frame address */
	Sound_Update(false);

	if (nDmaSoundControl & DMASNDCTRL_PLAY)
#ifdef olddma
	nActCount = dma.frameStartAddr + (int)dma.frameCounter_int;
#else
	nActCount = dma.frameStartAddr + (int)(frameCounter_float>>32);
#endif
	else
		nActCount = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | IoMem[0xff8907];

	nActCount &= ~1;

	return nActCount;
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound control register (0xff8900).
 */
void DmaSnd_SoundControl_ReadWord(void)
{
	IoMem_WriteWord(0xff8900, nDmaSoundControl);

	LOG_TRACE(TRACE_DMASND, "DMA snd control read: 0x%04x\n", nDmaSoundControl);
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to sound control register (0xff8900).
 */
void DmaSnd_SoundControl_WriteWord(void)
{
	Uint16 nNewSndCtrl;

	LOG_TRACE(TRACE_DMASND, "DMA snd control write: 0x%04x\n", IoMem_ReadWord(0xff8900));

        /* Before starting/stopping DMA sound, create samples up until this point with current values */
	Sound_Update(false);

	nNewSndCtrl = IoMem_ReadWord(0xff8900) & 3;

	if (!(nDmaSoundControl & DMASNDCTRL_PLAY) && (nNewSndCtrl & DMASNDCTRL_PLAY))
	{
		LOG_TRACE(TRACE_DMASND, "DMA snd control write: starting dma sound output\n");
		DmaSnd_StartNewFrame();
	}
	else if ((nDmaSoundControl & DMASNDCTRL_PLAY) && !(nNewSndCtrl & DMASNDCTRL_PLAY))
	{
		LOG_TRACE(TRACE_DMASND, "DMA snd control write: stopping dma sound output\n");
		/* DMA sound is stopped, remove interrupt */
		CycInt_RemovePendingInterrupt(INTERRUPT_DMASOUND);
	}

	nDmaSoundControl = nNewSndCtrl;
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound frame count high register (0xff8909).
 */
void DmaSnd_FrameCountHigh_ReadByte(void)
{
	IoMem_WriteByte(0xff8909, DmaSnd_GetFrameCount() >> 16);
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound frame count medium register (0xff890b).
 */
void DmaSnd_FrameCountMed_ReadByte(void)
{
	IoMem_WriteByte(0xff890b, DmaSnd_GetFrameCount() >> 8);
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound frame count low register (0xff890d).
 */
void DmaSnd_FrameCountLow_ReadByte(void)
{
	IoMem_WriteByte(0xff890d, DmaSnd_GetFrameCount());
}


/*-----------------------------------------------------------------------*/
/**
 * Write bytes to various registers with no action.
 */
void DmaSnd_FrameStartHigh_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd frame start high: 0x%02x\n", IoMem_ReadByte(0xff8903));
}

void DmaSnd_FrameStartMed_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd frame start med: 0x%02x\n", IoMem_ReadByte(0xff8905));
}

void DmaSnd_FrameStartLow_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd frame start low: 0x%02x\n", IoMem_ReadByte(0xff8907));
}

void DmaSnd_FrameCountHigh_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd frame count high: 0x%02x\n", IoMem_ReadByte(0xff8909));
}

void DmaSnd_FrameCountMed_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd frame count med: 0x%02x\n", IoMem_ReadByte(0xff890b));
}

void DmaSnd_FrameCountLow_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd frame count low: 0x%02x\n", IoMem_ReadByte(0xff890d));
}

void DmaSnd_FrameEndHigh_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd frame end high: 0x%02x\n", IoMem_ReadByte(0xff890f));
}

void DmaSnd_FrameEndMed_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd frame end med: 0x%02x\n", IoMem_ReadByte(0xff8911));
}

void DmaSnd_FrameEndLow_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd frame end low: 0x%02x\n", IoMem_ReadByte(0xff8913));

//LOG_TRACE(TRACE_DMASND, "DMA pos %d / %d\n", dma.frameCounter_int , dma.frameLen );
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound mode register (0xff8921).
 */
void DmaSnd_SoundModeCtrl_ReadByte(void)
{
	IoMem_WriteByte(0xff8921, dma.soundMode);

	LOG_TRACE(TRACE_DMASND, "DMA snd mode read: 0x%02x\n", dma.soundMode);
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to sound mode register (0xff8921).
 */
void DmaSnd_SoundModeCtrl_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd mode write: 0x%02x mode=%s freq=%d\n", IoMem_ReadByte(0xff8921),
		IoMem_ReadByte(0xff8921) & DMASNDMODE_MONO ? "mono" : "stereo" , DmaSndSampleRates[ IoMem_ReadByte(0xff8921) & 3 ]);

	/* STE or TT - hopefully STFM emulation never gets here :)
	 * We maskout to only hit bits that exist on a real STE
	 */
	dma.soundMode = (IoMem_ReadByte(0xff8921) & 0x8f);
	/* we also write the masked value back into the emulated hw registers so we have a correct value there */
	IoMem_WriteByte(0xff8921, dma.soundMode);
}

/* ---------------------- Microwire / LMC 1992  ---------------------- */

/**
 * Handle the shifting/rotating of the microwire registers
 * The microwire regs should be done after 16 usec = 32 NOPs = 128 cycles.
 * That means we have to shift 16 times with a delay of 8 cycles.
 */
void DmaSnd_InterruptHandler_Microwire(void)
{
	Uint8 i, bit;
	Uint16 saveData;
	
	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	--microwire.mwTransferSteps;

	/* Shift data register until it becomes zero. */
	if (microwire.mwTransferSteps > 1)
	{
		IoMem_WriteWord(0xff8922, microwire.data<<(16-microwire.mwTransferSteps));
	}
	else
	{
		/* Paradox XMAS 2004 demo continuesly writes to the data
		 * register, but still expects to read a zero inbetween,
		 * so we have to output a zero before we're really done
		 * with the transfer. */
		IoMem_WriteWord(0xff8922, 0);
	}

	/* Rotate mask register */
	IoMem_WriteWord(0xff8924, (microwire.mask<<(16-microwire.mwTransferSteps))
	                          |(microwire.mask>>microwire.mwTransferSteps));

	if (microwire.mwTransferSteps > 0)
	{
		CycInt_AddRelativeInterrupt(8, INT_CPU_CYCLE, INTERRUPT_DMASOUND_MICROWIRE);
	}
	else {
		/* Decode the address + command word according to the binary mask */
		bit = 0;
		saveData = microwire.data;
		microwire.data = 0;
		for (i=0; i<16; i++) {
			if ((microwire.mask >> i) & 1) {
				microwire.data += ((saveData >> i) & 1) << bit;
				bit ++;
			}
		}

		/* The LMC 1992 address should be 10 xxx xxx xxx */
		if ((microwire.data & 0x600) != 0x400)
			return;

		/* Update the LMC 1992 commands */
		switch ((microwire.data >> 6) & 0x7) {
			case 0:
				/* Mixing command */
				microwire.mixing = microwire.data & 0x3;
				break;
			case 1:
				/* Bass command */
				microwire.bass = microwire.data & 0xf;
				DmaSnd_Set_Tone_Level(LMC1992_Bass_Treble_Table[microwire.bass], 
						      LMC1992_Bass_Treble_Table[microwire.treble]);
				break;
			case 2: 
				/* Treble command */
				microwire.treble = microwire.data & 0xf;
				DmaSnd_Set_Tone_Level(LMC1992_Bass_Treble_Table[microwire.bass], 
						      LMC1992_Bass_Treble_Table[microwire.treble]);
				break;
			case 3:
				/* Master volume command */
				microwire.masterVolume = LMC1992_Master_Volume_Table[microwire.data & 0x3f];
				lmc1992.left_gain = (microwire.leftVolume * (Uint32)microwire.masterVolume) * (1.0/(65536.0*65536.0));
				lmc1992.right_gain = (microwire.rightVolume * (Uint32)microwire.masterVolume) * (1.0/(65536.0*65536.0));
				break;
			case 4:
				/* Right channel volume */
				microwire.rightVolume = LMC1992_LeftRight_Volume_Table[microwire.data & 0x1f];
				lmc1992.right_gain = (microwire.rightVolume * (Uint32)microwire.masterVolume) * (1.0/(65536.0*65536.0));
				break;
			case 5:
				/* Left channel volume */
				microwire.leftVolume = LMC1992_LeftRight_Volume_Table[microwire.data & 0x1f];
				lmc1992.left_gain = (microwire.leftVolume * (Uint32)microwire.masterVolume) * (1.0/(65536.0*65536.0));
				break;
			default:
				/* Do nothing */
				break;
		}
	}
}

/**
 * Read word from microwire data register (0xff8922).
 */
void DmaSnd_MicrowireData_ReadWord(void)
{
	/* Shifting is done in DmaSnd_InterruptHandler_Microwire! */
	LOG_TRACE(TRACE_DMASND, "Microwire data read: 0x%x\n", IoMem_ReadWord(0xff8922));
}


/**
 * Write word to microwire data register (0xff8922).
 */
void DmaSnd_MicrowireData_WriteWord(void)
{
	/* Only update, if no shift is in progress */
	if (!microwire.mwTransferSteps)
	{
		microwire.data = IoMem_ReadWord(0xff8922);
		/* Start shifting events to simulate a microwire transfer */
		microwire.mwTransferSteps = 16;
		CycInt_AddRelativeInterrupt(8, INT_CPU_CYCLE, INTERRUPT_DMASOUND_MICROWIRE);
	}

	LOG_TRACE(TRACE_DMASND, "Microwire data write: 0x%x\n", IoMem_ReadWord(0xff8922));
}


/**
 * Read word from microwire mask register (0xff8924).
 */
void DmaSnd_MicrowireMask_ReadWord(void)
{
	/* Same as with data register, but mask is rotated, not shifted. */
	LOG_TRACE(TRACE_DMASND,  "Microwire mask read: 0x%x\n", IoMem_ReadWord(0xff8924));
}


/**
 * Write word to microwire mask register (0xff8924).
 */
void DmaSnd_MicrowireMask_WriteWord(void)
{
	/* Only update, if no shift is in progress */
	if (!microwire.mwTransferSteps)
	{
		microwire.mask = IoMem_ReadWord(0xff8924);
	}

	LOG_TRACE(TRACE_DMASND, "Microwire mask write: 0x%x\n", IoMem_ReadWord(0xff8924));
}


/*-------------------Bass / Treble filter ---------------------------*/

/**
 * Left voice Filter for Bass/Treble.
 */
static float DmaSnd_IIRfilterL(float xn)
{
	static float data[2] = { 0.0, 0.0 };
	float a, yn;

	/* Input coefficients */
	/* biquad1  Note: 'a' coefficients are subtracted */
	a  = lmc1992.left_gain * xn;		/* a=g*xn;               */
	a -= lmc1992.coef[0] * data[0];		/* a1;  wn-1             */
	a -= lmc1992.coef[1] * data[1];		/* a2;  wn-2             */
						/* If coefficient scale  */
						/* factor = 0.5 then     */
						/* multiply by 2         */
	/* Output coefficients */
	yn  = lmc1992.coef[2] * a;		/* b0;                   */
	yn += lmc1992.coef[3] * data[0];	/* b1;                   */
	yn += lmc1992.coef[4] * data[1];	/* b2;                   */

	data[1] = data[0];			/* wn-1 -> wn-2;         */
	data[0] = a;				/* wn -> wn-1            */
	return yn;
}


/**
 * Right voice Filter for Bass/Treble.
 */
static float DmaSnd_IIRfilterR(float xn)
{
	static float data[2] = { 0.0, 0.0 };
	float a, yn;

	/* Input coefficients */
	/* biquad1  Note: 'a' coefficients are subtracted */
	a  = lmc1992.right_gain * xn;		/* a=g*xn;               */
	a -= lmc1992.coef[0]*data[0];		/* a1;  wn-1             */
	a -= lmc1992.coef[1]*data[1];		/* a2;  wn-2             */
						/* If coefficient scale  */
						/* factor = 0.5 then     */
						/* multiply by 2         */
	/* Output coefficients */
	yn  = lmc1992.coef[2]*a;		/* b0;                   */
	yn += lmc1992.coef[3]*data[0];		/* b1;                   */
	yn += lmc1992.coef[4]*data[1];		/* b2;                   */

	data[1] = data[0];			/* wn-1 -> wn-2;         */
	data[0] = a;				/* wn -> wn-1            */
	return yn;
}

/**
 * LowPass Filter Left
 */
static Sint16 DmaSnd_LowPassFilterLeft(Sint16 in)
{
	static	Sint16	loop[4] = { 0, 0, 0, 0 };
	static	Sint16	out = 0;
	static	int	count = 0;

	if (DmaSnd_LowPass)
	{
		if(--count < 0)
			count = 3;

		out -= loop[count];
		loop[count] = in;
		out += loop[count];

		return out; /* Filter Gain = 4 */
	}else
	{
		return in << 2;
	}
}

/**
 * LowPass Filter Right
 */
static Sint16 DmaSnd_LowPassFilterRight(Sint16 in)
{
	static	Sint16	loop[4] = { 0, 0, 0, 0 };
	static	Sint16	out = 0;
	static	int	count = 0;

	if (DmaSnd_LowPass)
	{
		if(--count < 0)
			count = 3;

		out -= loop[count];
		loop[count] = in;
		out += loop[count];

		return out; /* Filter Gain = 4 */
	}else
	{
		return in << 2;
	}
}

/**
 * Set Bass and Treble tone level
 */
static void DmaSnd_Set_Tone_Level(int set_bass, int set_treb)
{ 
	/* 13 levels; 0 through 12 correspond with -12dB to 12dB in 2dB steps */
	lmc1992.coef[0] = lmc1992.treb_table[set_treb].a1 + lmc1992.bass_table[set_bass].a1;
	lmc1992.coef[1] = lmc1992.treb_table[set_treb].a1 * lmc1992.bass_table[set_bass].a1;
	lmc1992.coef[2] = lmc1992.treb_table[set_treb].b0 * lmc1992.bass_table[set_bass].b0;
	lmc1992.coef[3] = lmc1992.treb_table[set_treb].b0 * lmc1992.bass_table[set_bass].b1 +
			  lmc1992.treb_table[set_treb].b1 * lmc1992.bass_table[set_bass].b0;
	lmc1992.coef[4] = lmc1992.treb_table[set_treb].b1 * lmc1992.bass_table[set_bass].b1;
}


/**
 * Compute the first order bass shelf
 */
static struct first_order_s *DmaSnd_Bass_Shelf(float g, float fc, float Fs)
{
	static struct first_order_s bass;
	float  a1;

	/* g, fc, Fs must be positve real numbers > 0.0 */
	if (g < 1.0)
		bass.a1 = a1 = (tanf(M_PI*fc/Fs) - g  ) / (tanf(M_PI*fc/Fs) + g  );
	else
		bass.a1 = a1 = (tanf(M_PI*fc/Fs) - 1.0) / (tanf(M_PI*fc/Fs) + 1.0);

	bass.b0 = (1.0 + a1) * (g - 1.0) / 2.0 + 1.0;
	bass.b1 = (1.0 + a1) * (g - 1.0) / 2.0 + a1;

	return &bass;
}


/**
 * Compute the first order treble shelf
 */
static struct first_order_s *DmaSnd_Treble_Shelf(float g, float fc, float Fs)
{
	static struct first_order_s treb;
	float  a1;

	/* g, fc, Fs must be positve real numbers > 0.0 */
	if (g < 1.0)
		treb.a1 = a1 = (g*tanf(M_PI*fc/Fs) - 1.0) / (g*tanf(M_PI*fc/Fs) + 1.0);
	else
		treb.a1 = a1 =   (tanf(M_PI*fc/Fs) - 1.0) /   (tanf(M_PI*fc/Fs) + 1.0);

	treb.b0 = 1.0 + (1.0 - a1) * (g - 1.0) / 2.0;
	treb.b1 = a1  + (a1 - 1.0) * (g - 1.0) / 2.0;

	return &treb;
}


/**
 * Compute the bass and treble tables (nAudioFrequency)
 */
void DmaSnd_Init_Bass_and_Treble_Tables(void)
{
	struct first_order_s *bass;
	struct first_order_s *treb;

	float  dB_adjusted, dB, g, fc_bt, fc_tt, Fs;
	int    n;

	fc_bt = 118.2763;
	fc_tt = 8438.756;
	Fs = (float)nAudioFrequency;

	if ((Fs < 8000.0) || (Fs > 96000.0))
		Fs = 44100.0;

	if (fc_tt > 0.5*0.8*Fs)
	{
		fc_tt = 0.5*0.8*Fs;
		dB_adjusted = 2.0 * 0.5*0.8*Fs/fc_tt;
	}else
	{
		dB_adjusted = 2.0;
	}

	for (dB = dB_adjusted*(TONE_STEPS-1)/2, n = TONE_STEPS; n--; dB -= dB_adjusted)
	{
		g = powf(10.0, dB/20.0);	/* 12dB to -12dB */

		treb = DmaSnd_Treble_Shelf(g, fc_tt, Fs);

		lmc1992.treb_table[n].a1 = treb->a1;
		lmc1992.treb_table[n].b0 = treb->b0;
		lmc1992.treb_table[n].b1 = treb->b1;
	}

	for (dB = 12.0, n = TONE_STEPS; n--; dB -= 2.0)
	{
		g = powf(10.0, dB/20.0);	/* 12dB to -12dB */

		bass = DmaSnd_Bass_Shelf(g, fc_bt, Fs);

		lmc1992.bass_table[n].a1 = bass->a1;
		lmc1992.bass_table[n].b0 = bass->b0;
		lmc1992.bass_table[n].b1 = bass->b1;
	}

	DmaSnd_Set_Tone_Level(LMC1992_Bass_Treble_Table[microwire.bass & 0xf], 
			      LMC1992_Bass_Treble_Table[microwire.treble & 0xf]);

	/* Initialize IIR Filter Gain and use as a Volume Control */
	lmc1992.left_gain = (microwire.leftVolume * (Uint32)microwire.masterVolume) * (1.0/(65536.0*65536.0));
	lmc1992.right_gain = (microwire.rightVolume * (Uint32)microwire.masterVolume) * (1.0/(65536.0*65536.0));

	/* Anti-alias filter is not required when nAudioFrequency == 50066 Hz */
	if (nAudioFrequency>50000 && nAudioFrequency<50100)
		DmaSnd_LowPass = false;
	else
		DmaSnd_LowPass = true;
}
