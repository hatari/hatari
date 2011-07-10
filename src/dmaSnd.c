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

#define DMASND_FIFO_SIZE	8			/* 8 bytes : size of the DMA Audio's FIFO, filled on every HBL */
#define DMASND_FIFO_SIZE_MASK	(DMASND_FIFO_SIZE-1)	/* mask to keep FIFO_pos in 0-7 range */


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
Sint64 frameCounter_float;	// rename
Sint8 FIFO[ DMASND_FIFO_SIZE ];
Uint16 FIFO_Pos = 0;			/* from 0 to DMASND_FIFO_SIZE-1 */
Uint16 FIFO_NbBytes = 0;		/* from 0 to DMASND_FIFO_SIZE */
Uint32 frameCounterAddr;		/* Sound frame current address counter */
Sint64 FreqRatio;
bool InitSample;


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



/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static void	DmaSnd_FIFO_Refill(void);
static Sint8	DmaSnd_FIFO_PullByte(void);
static void	DmaSnd_FIFO_ReadByte( Sint8 *pMono);
static void	DmaSnd_FIFO_ReadWord( Sint8 *pLeft , Sint8 *pRight );
static void	DmaSnd_FIFO_SetStereo(void);

static int	DmaSnd_DetectSampleRate(void);
static void	DmaSnd_StartNewFrame(void);
static inline int DmaSnd_EndOfFrameReached(void);


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
		dma.soundMode = 0;
		FIFO_Pos = 0;
		FIFO_NbBytes = 0;
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


/*-----------------------------------------------------------------------*/
/**
 * This function is called on every HBL to ensure the DMA Audio's FIFO
 * is kept full.
 * In Hatari, the FIFO is handled like a ring buffer (to avoid memcopying bytes
 * inside the FIFO when a byte is pushed/pulled).
 * Note that the DMA fetches words, not bytes, so we read new data only
 * when 2 bytes or more are missing.
 * When end of frame is reached, we continue with a new frame if loop mode
 * is on, else we stop DMA Audio.
 */
static void DmaSnd_FIFO_Refill(void)
{
	/* If DMA sound is OFF, don't update the FIFO */
	if ( ( nDmaSoundControl & DMASNDCTRL_PLAY ) == 0)
		return;

	/* If End Address == Start Address, don't update the FIFO */
	if (dma.frameEndAddr == dma.frameStartAddr)
		return;
	
	/* Refill the whole FIFO */
	while ( DMASND_FIFO_SIZE - FIFO_NbBytes >= 2 )
	{
		/* Add one word to the FIFO */
		LOG_TRACE(TRACE_DMASND, "DMA snd fifo refill adr=%x pos %d nb %d %x %x\n", frameCounterAddr , FIFO_Pos , FIFO_NbBytes ,
			STRam[ frameCounterAddr ] , STRam[ frameCounterAddr+1 ] );

		FIFO[ ( FIFO_Pos+FIFO_NbBytes+0 ) & DMASND_FIFO_SIZE_MASK ] = (Sint8)STRam[ frameCounterAddr ];		/* add upper byte of the word */
		FIFO[ ( FIFO_Pos+FIFO_NbBytes+1 ) & DMASND_FIFO_SIZE_MASK ] = (Sint8)STRam[ frameCounterAddr+1 ];	/* add lower byte of the word */

		FIFO_NbBytes += 2;				/* One word more in the FIFO */

		/* Increase current frame address and check if we reached frame's end */
		frameCounterAddr += 2;
		if ( frameCounterAddr == dma.frameEndAddr )	/* end of frame reached, should we loop or stop dma ? */
		{
			if ( DmaSnd_EndOfFrameReached() )
				break;				/* Loop mode off, dma audio is now turned off */
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Pull one sample/byte from the DMA Audio's FIFO and decrease the number of
 * remaining bytes.
 * If the FIFO is empty, return 0 (empty sample)
 * Note : on a real STE, the 8 bytes FIFO is refilled on each HBL, which gives
 * a total of 313*8=125326 bytes per sec read by the DMA. As the max freq
 * is 50066 Hz, the STE can play 100132 bytes per sec in stereo ; so on a real STE
 * the FIFO can never be empty while DMA is ON.
 * But on Hatari, if the user chooses an audio's output frequency that is much
 * lower than the current DMA freq, audio will be updated less frequently than
 * on each HBL and it could require to process more than DMASND_FIFO_SIZE in one
 * call to DmaSnd_GenerateSamples(). This is why we allow DmaSnd_FIFO_Refill()
 * to be called if FIFO is empty but DMA sound is still ON.
 * This way, sound remains correct even if the user uses very low output freq.
 */
static Sint8 DmaSnd_FIFO_PullByte(void)
{
	Sint8	sample;

	if ( FIFO_NbBytes == 0 )
	{
		/* If DMA sound is OFF, don't update the FIFO */
		if ( ( nDmaSoundControl & DMASNDCTRL_PLAY ) == 0)
		{
			LOG_TRACE(TRACE_DMASND, "DMA snd fifo empty for pull\n" );
			return 0;
		}
		else
			DmaSnd_FIFO_Refill();
	}


LOG_TRACE(TRACE_DMASND, "DMA snd fifo pull pos %d nb %d %02x\n", FIFO_Pos , FIFO_NbBytes , (Uint8)FIFO[ FIFO_Pos ] );

	sample = FIFO[ FIFO_Pos ];				/* Get oldest byte from the FIFO */
	FIFO_Pos = (FIFO_Pos+1) & DMASND_FIFO_SIZE_MASK;	/* Pos to be pulled on next call */
	FIFO_NbBytes--;						/* One byte less in the FIFO */

	return sample;
}


/*-----------------------------------------------------------------------*/
/**
 * Read 1 sample/byte from the DMA Audio's FIFO without pulling it
 * from the FIFO. This is used in 'mono' mode.
 * If the FIFO is empty, return 0 (empty sample)
 */
static void DmaSnd_FIFO_ReadByte( Sint8 *pMono)
{
	if ( FIFO_NbBytes == 0 )
	{
		*pMono = 0;
		return;
	}

	*pMono = FIFO[ FIFO_Pos ];				/* Get oldest byte from the FIFO */
}


/*-----------------------------------------------------------------------*/
/**
 * Read 2 samples/1 word from the DMA Audio's FIFO without pulling them
 * from the FIFO. This is used in 'stereo' mode.
 * If the FIFO is empty, return 0 (empty samples)
 */
static void DmaSnd_FIFO_ReadWord( Sint8 *pLeft , Sint8 *pRight )
{
	if ( FIFO_NbBytes == 0 )
	{
		*pLeft = 0;
		*pRight = 0;
	}

	/* In stereo mode, FIFO_pos is even and can be 0, 2, 4 or 6 -> no need to mask with DMASND_FIFO_SIZE_MASK */
	*pLeft = FIFO[ FIFO_Pos ];
	*pRight = FIFO[ FIFO_Pos+1 ];
}


/*-----------------------------------------------------------------------*/
/**
 * In case a program switches from mono to stereo, we must ensure that
 * FIFO_pos is on even boundary to keep Left/Right bytes in the correct
 * order (Left byte should be on even addresses and Right byte on odd ones).
 * If this is not the case, we skip one byte.
 */
static void DmaSnd_FIFO_SetStereo(void)
{
	Uint16	NewPos;

	if ( FIFO_Pos & 1 )
	{
		NewPos = (FIFO_Pos+1) & DMASND_FIFO_SIZE_MASK;	/* skip the byte on odd address */

		if ( nDmaSoundControl & DMASNDCTRL_PLAY )	/* print a log if we change while playing */
			{ LOG_TRACE(TRACE_DMASND, "DMA snd switching to stereo mode while playing mono FIFO_pos %d->%d\n", FIFO_Pos , NewPos ); }
		else
			{ LOG_TRACE(TRACE_DMASND, "DMA snd switching to stereo mode FIFO_pos %d->%d\n", FIFO_Pos , NewPos ); }

		FIFO_Pos = NewPos;

		if ( FIFO_NbBytes > 0 )
			FIFO_NbBytes--;				/* remove one byte if FIFO was not already empty */
	}
	
}


/*-----------------------------------------------------------------------*/
/**
 * Returns the frequency corresponding to the 2 lower bits of dma.soundMode
 */
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

	frameCounterAddr = dma.frameStartAddr;

#ifdef nofifo
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
#endif

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
	int i;
	int nBufIdx;
	Sint8 	MonoByte , LeftByte , RightByte;
	static Sint16 FrameMono = 0, FrameLeft = 0, FrameRight = 0;
	unsigned n;


	/* DMA Audio OFF and FIFO empty : process YM2149's output */
	if ( !(nDmaSoundControl & DMASNDCTRL_PLAY) && ( FIFO_NbBytes == 0 ) )
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


	/* DMA Audio ON */

	/* Compute ratio between hatari's sound frequency and host computer's sound frequency */
	FreqRatio = ( ((Sint64)DmaSnd_DetectSampleRate()) << 32 ) / nAudioFrequency;

	if (dma.soundMode & DMASNDMODE_MONO)
	{
		/* Mono 8-bit */
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			if ( InitSample )
			{
				MonoByte = DmaSnd_FIFO_PullByte ();
				FrameMono      = DmaSnd_LowPassFilterLeft( (Sint16)MonoByte );
				/* No-Click */   DmaSnd_LowPassFilterRight( (Sint16)MonoByte );
				InitSample = false;
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

			MixBuffer[nBufIdx][1] = MixBuffer[nBufIdx][0];		/* right = left */

			/* Increase freq counter */
			frameCounter_float += FreqRatio;
			n = frameCounter_float >> 32;				/* number of samples to skip */
			while ( n > 0 )						/* pull as many bytes from the FIFO as needed */
			{
				MonoByte = DmaSnd_FIFO_PullByte ();
				FrameMono      = DmaSnd_LowPassFilterLeft( (Sint16)MonoByte );
				/* No-Click */   DmaSnd_LowPassFilterRight( (Sint16)MonoByte );
				n--;
			}
			frameCounter_float &= 0xffffffff;			/* only keep the fractional part */
		}
	}
	else
	{
		/* Stereo 8-bit */
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			if ( InitSample )
			{
				LeftByte = DmaSnd_FIFO_PullByte ();
				RightByte = DmaSnd_FIFO_PullByte ();
				FrameLeft  = DmaSnd_LowPassFilterLeft( (Sint16)LeftByte );
				FrameRight = DmaSnd_LowPassFilterRight( (Sint16)RightByte );
				InitSample = false;
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

			/* Increase freq counter */
			frameCounter_float += FreqRatio;
			n = frameCounter_float >> 32;				/* number of samples to skip */
			while ( n > 0 )						/* pull as many bytes from the FIFO as needed */
			{
				LeftByte = DmaSnd_FIFO_PullByte ();
				RightByte = DmaSnd_FIFO_PullByte ();
				FrameLeft  = DmaSnd_LowPassFilterLeft( (Sint16)LeftByte );
				FrameRight = DmaSnd_LowPassFilterRight( (Sint16)RightByte );
				n--;
			}
			frameCounter_float &= 0xffffffff;			/* only keep the fractional part */
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
 * We first check if the FIFO needs to be refilled, then we call Sound_Update.
 * This function should be called from the HBL's handler (in video.c)
 */
void DmaSnd_STE_HBL_Update(void)
{
	if ( ( ConfigureParams.System.nMachineType != MACHINE_STE )
	  && ( ConfigureParams.System.nMachineType != MACHINE_MEGA_STE ) )
		return;


	/* The DMA starts refilling the FIFO when display is OFF (eg cycle 376 in low res 50 Hz) */
	DmaSnd_FIFO_Refill ();

	/* If DMA sound is ON or FIFO is not empty, update sound */
	if  ( (nDmaSoundControl & DMASNDCTRL_PLAY) || ( FIFO_NbBytes > 0 ) )
		Sound_Update(false);

	/* As long as display is OFF, the DMA will refill the FIFO after playing some samples during the HBL */
	DmaSnd_FIFO_Refill ();
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
#ifdef nofifo
 #ifdef olddma
	nActCount = dma.frameStartAddr + (int)dma.frameCounter_int;
 #else
	nActCount = dma.frameStartAddr + (int)(frameCounter_float>>32);
 #endif
#else
	nActCount = frameCounterAddr;
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
		InitSample = true;
		frameCounter_float = 0;
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
LOG_TRACE(TRACE_DMASND, "DMA pos %d / %d\n", frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr );
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

LOG_TRACE(TRACE_DMASND, "DMA pos %d / %d\n", frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr );
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
	Uint16	SoundModeNew;

	SoundModeNew = IoMem_ReadByte(0xff8921);

	LOG_TRACE(TRACE_DMASND, "DMA snd mode write: 0x%02x mode=%s freq=%d\n", SoundModeNew,
		SoundModeNew & DMASNDMODE_MONO ? "mono" : "stereo" , DmaSndSampleRates[ SoundModeNew & 3 ]);

	/* We maskout to only bits that exist on a real STE */
	SoundModeNew &= 0x8f;

	/* Are we switching from mono to stereo ? */
	if ( ( dma.soundMode & DMASNDMODE_MONO ) && ( ( SoundModeNew & DMASNDMODE_MONO ) == 0 ) )
		DmaSnd_FIFO_SetStereo ();

	dma.soundMode = SoundModeNew;
	/* We also write the masked value back into the emulated hw registers so we have a correct value there */
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
