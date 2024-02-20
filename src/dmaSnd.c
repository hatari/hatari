/*
  Hatari - dmaSnd.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

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
    $FF8922 (word) : Microwire Data Register
    $FF8924 (word) : Microwire Mask Register

  
  The Microwire and LMC 1992 commands :
    
    a command looks like: 10 CCC DDD DDD
    
    chipset address : 10
    command : 
	000 XXX XDD Mixing
		00 : DMA sound only
		01 : DMA sound + input 1 (YM2149 + AUDIOI, full frequency range)
		10 : DMA sound + input 2 (YM2149 + AUDIOI, Low Pass Filter) -> DMA sound only
		11 : DMA sound + input 3 (not connected) -> DMA sound only

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


const char DmaSnd_fileid[] = "Hatari dmaSnd.c";

#include <SDL_stdinc.h>		/* Required for M_PI */
#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "cycles.h"
#include "cycInt.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "sound.h"
#include "stMemory.h"
#include "crossbar.h"
#include "video.h"
#include "m68000.h"
#include "clocks_timings.h"

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
static int16_t DmaSnd_LowPassFilterLeft(int16_t in);
static int16_t DmaSnd_LowPassFilterRight(int16_t in);
static bool DmaSnd_LowPass;


uint16_t nDmaSoundControl;              /* Sound control register */

struct first_order_s  { float a1, b0, b1; };
struct second_order_s { float a1, a2, b0, b1, b2; };

struct dma_s {
	uint16_t soundMode;		/* Sound mode register */
	uint32_t frameStartAddr;	/* Sound frame start */
	uint32_t frameEndAddr;		/* Sound frame end */
	uint32_t frameCounterAddr;	/* Sound frame current address counter */

	/* Internal 8 byte FIFO */
	int8_t FIFO[ DMASND_FIFO_SIZE ];
	uint16_t FIFO_Pos;		/* from 0 to DMASND_FIFO_SIZE-1 */
	uint16_t FIFO_NbBytes;		/* from 0 to DMASND_FIFO_SIZE */

	int16_t FrameLeft;		/* latest values read from the FIFO */
	int16_t FrameRight;

	uint8_t  XSINT_Signal;		/* Value of the XSINT signal (connected to MFP) */
};

static int64_t	frameCounter_float = 0;
static bool	DmaInitSample = false;


struct microwire_s {
	uint16_t data;			/* Microwire Data register */
	uint16_t mask;			/* Microwire Mask register */
	uint16_t mwTransferSteps;	/* Microwire shifting counter */
	uint16_t pendingCyclesOver;	/* Number of delayed cycles for the interrupt */
	uint16_t mixing;		/* Mixing command */
	uint16_t bass;			/* Bass command */
	uint16_t treble;		/* Treble command */
	uint16_t masterVolume;		/* Master volume command */
	uint16_t leftVolume;		/* Left channel volume command */
	uint16_t rightVolume;		/* Right channel volume command */
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
static const uint16_t LMC1992_Master_Volume_Table[64] =
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
static const uint16_t LMC1992_LeftRight_Volume_Table[32] =
{
	  655,   825,  1039,  1308,  1646,  2072,  2609,  3285,  4135,  5206,  /* -40dB */
	 6554,  8250, 10387, 13076, 16462, 20724, 26090, 32846, 41350, 52057,  /* -20dB */
	65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535,  /*   0dB */
	65535, 65535                                                           /*   0dB */
};

/* Values for LMC1992 BASS and TREBLE */
static const int16_t LMC1992_Bass_Treble_Table[16] =
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

static void	DmaSnd_Update_XSINT_Line ( uint8_t Bit );

static void	DmaSnd_FIFO_Refill(void);
static int8_t	DmaSnd_FIFO_PullByte(void);
static void	DmaSnd_FIFO_SetStereo(void);

static int	DmaSnd_DetectSampleRate(void);
static void	DmaSnd_StartNewFrame(void);
static inline int DmaSnd_EndOfFrameReached(void);


/**
 * Reset DMA sound variables.
 */
void DmaSnd_Reset(bool bCold)
{
	nDmaSoundControl = 0;
	dma.soundMode = 0;

	/* [NP] Set start/end to 0 even on warm reset ? (fix 'Brace' by Diamond Design) */
	IoMem[0xff8903] = 0;				/* frame start addr = 0 */
	IoMem[0xff8905] = 0;
	IoMem[0xff8907] = 0;
	IoMem[0xff890f] = 0;				/* frame end addr = 0 */
	IoMem[0xff8911] = 0;
	IoMem[0xff8913] = 0;

	dma.FIFO_Pos = 0;
	dma.FIFO_NbBytes = 0;
	dma.FrameLeft = 0;
	dma.FrameRight = 0;

	DmaSnd_Update_XSINT_Line ( MFP_GPIP_STATE_LOW );	/* O/LOW=dma sound idle */

	if ( bCold )
	{
		/* Microwire has no reset signal, it will keep its values on warm reset */
		microwire.masterVolume = 7;		/* -80 dB ; TOS 1.62 will put 0x28 (ie 65535) = 0 dB (max volume) */
		microwire.leftVolume = 655;		/* -40 dB ; TOS 1.62 will put 0x14 (ie 65535) = 0 dB (max volume) */
		microwire.rightVolume = 655;		/* -40 db ; TOS 1.62 will put 0x14 (ie 65535) = 0 dB (max volume) */
		microwire.mixing = 0;
		microwire.bass = 6;			/* 0 dB (flat) */
		microwire.treble = 6;			/* 0 dB (flat) */
	}

	/* Initialise microwire LMC1992 IIR filter parameters */
	DmaSnd_Init_Bass_and_Treble_Tables();

	microwire.mwTransferSteps = 0;
	microwire.pendingCyclesOver = 8;
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
 * Update the value of the XSINT line ; this line is connected to TAI and to GPIP7
 * Depending on the transition, this can trigger MFP interrupt for Timer A or for GPIP7
 *  - Bit is set to 0/LOW when dma sound is idle
 *  - Bit is set to 1/HIGH when dma sound is playing
 *
 * Timer A input is associated to GPIP4. Under default TOS behaviour AER bit for GPIP4 is set to 0
 * (because it's also shared with ACIA's interrupt lines which are active low).
 * This means that each time XSINT goes to idle/0 (when reaching end of frame in single or repeat mode)
 * an interrupt will trigger on Timer A and Timer A event count mode can be used to count
 * the number of "end of frame" events
 */
static void DmaSnd_Update_XSINT_Line ( uint8_t Bit )
{
	dma.XSINT_Signal = Bit;
	MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE7 , Bit );
	MFP_TimerA_Set_Line_Input ( pMFP_Main , Bit );			/* Update events count / interrupt for timer A if needed */
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of the XSINT line
 *  0=dma sound is idle
 *  1=dma sound is playing
 */
uint8_t	DmaSnd_Get_XSINT_Line ( void )
{
	return dma.XSINT_Signal;
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
 *
 * NOTE : as verified on real STE, if frameEndAddr == frameStartAddr and
 * repeat is ON, then frame counter is increased anyway and end of frame
 * interrupt is not generated. In that case, the FIFO is updated
 * and sound should be played (this will be the same as playing a 2^24 bytes
 * sample) (eg 'A Little Bit Insane' demo by Lazer)
 */
static void DmaSnd_FIFO_Refill(void)
{
	/* If DMA sound is OFF, don't update the FIFO */
	if ( ( nDmaSoundControl & DMASNDCTRL_PLAY ) == 0)
		return;

	/* Refill the whole FIFO */
	while ( DMASND_FIFO_SIZE - dma.FIFO_NbBytes >= 2 )
	{
		/* Add one word to the FIFO */
		LOG_TRACE(TRACE_DMASND, "DMA snd fifo refill adr=%x pos %d nb %d %x %x\n", dma.frameCounterAddr , dma.FIFO_Pos , dma.FIFO_NbBytes ,
			STMemory_DMA_ReadByte ( dma.frameCounterAddr ) , STMemory_DMA_ReadByte ( dma.frameCounterAddr+1 ) );

		dma.FIFO[ ( dma.FIFO_Pos+dma.FIFO_NbBytes+0 ) & DMASND_FIFO_SIZE_MASK ] = (int8_t)STMemory_DMA_ReadByte ( dma.frameCounterAddr );	/* add upper byte of the word */
		dma.FIFO[ ( dma.FIFO_Pos+dma.FIFO_NbBytes+1 ) & DMASND_FIFO_SIZE_MASK ] = (int8_t)STMemory_DMA_ReadByte ( dma.frameCounterAddr+1 );	/* add lower byte of the word */

		dma.FIFO_NbBytes += 2;				/* One word more in the FIFO */

		/* Increase current frame address and check if we reached frame's end */
		dma.frameCounterAddr += 2;
		if ( dma.frameCounterAddr == dma.frameEndAddr )	/* end of frame reached, should we loop or stop dma ? */
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
 * a total of 313*8*VBL_PER_SEC=125326 bytes per sec read by the DMA. As the max freq
 * is 50066 Hz, the STE can play 100132 bytes per sec in stereo ; so on a real STE
 * the FIFO can never be empty while DMA is ON.
 * But on Hatari, if the user chooses an audio's output frequency that is much
 * lower than the current DMA freq, audio will be updated less frequently than
 * on each HBL and it could require to process more than DMASND_FIFO_SIZE in one
 * call to DmaSnd_GenerateSamples(). This is why we allow DmaSnd_FIFO_Refill()
 * to be called if FIFO is empty but DMA sound is still ON.
 * This way, sound remains correct even if the user uses very low output freq.
 */
static int8_t DmaSnd_FIFO_PullByte(void)
{
	int8_t	sample;

	if ( dma.FIFO_NbBytes == 0 )
	{
		DmaSnd_FIFO_Refill();
		if ( dma.FIFO_NbBytes == 0 )                    /* Refill didn't add any new bytes */
		{
			LOG_TRACE(TRACE_DMASND, "DMA snd fifo empty for pull\n" );
			return 0;
		}
	}


	LOG_TRACE(TRACE_DMASND, "DMA snd fifo pull pos %d nb %d %02x\n", dma.FIFO_Pos , dma.FIFO_NbBytes , (uint8_t)dma.FIFO[ dma.FIFO_Pos ] );

	sample = dma.FIFO[ dma.FIFO_Pos ];			/* Get oldest byte from the FIFO */
	dma.FIFO_Pos = (dma.FIFO_Pos+1) & DMASND_FIFO_SIZE_MASK;/* Pos to be pulled on next call */
	dma.FIFO_NbBytes--;					/* One byte less in the FIFO */

	return sample;
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
	uint16_t	NewPos;

	if ( dma.FIFO_Pos & 1 )
	{
		NewPos = (dma.FIFO_Pos+1) & DMASND_FIFO_SIZE_MASK;	/* skip the byte on odd address */

		if ( nDmaSoundControl & DMASNDCTRL_PLAY )	/* print a log if we change while playing */
			{ LOG_TRACE(TRACE_DMASND, "DMA snd switching to stereo mode while playing mono FIFO_pos %d->%d\n", dma.FIFO_Pos , NewPos ); }
		else
			{ LOG_TRACE(TRACE_DMASND, "DMA snd switching to stereo mode FIFO_pos %d->%d\n", dma.FIFO_Pos , NewPos ); }

		dma.FIFO_Pos = NewPos;

		if ( dma.FIFO_NbBytes > 0 )
			dma.FIFO_NbBytes--;			/* remove one byte if FIFO was not already empty */
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
 * It copies the start and end address from the I/O registers and set
 * the frame counter addr to the start of this new frame.
 *
 * NOTE : as verified on real STE, if frameEndAddr == frameStartAddr and
 * repeat is OFF, then DMA sound is turned off immediately and end of frame
 * interrupt is not generated (eg 'Amberstar cracktro' by DNT Crew / Fuzion)
 */
static void DmaSnd_StartNewFrame(void)
{
	dma.frameStartAddr = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | (IoMem[0xff8907] & ~1);
	dma.frameEndAddr = (IoMem[0xff890f] << 16) | (IoMem[0xff8911] << 8) | (IoMem[0xff8913] & ~1);

	dma.frameCounterAddr = dma.frameStartAddr;

	LOG_TRACE(TRACE_DMASND, "DMA snd new frame start=%x end=%x\n", dma.frameStartAddr, dma.frameEndAddr);

	if ( ( dma.frameStartAddr == dma.frameEndAddr ) && ( ( nDmaSoundControl & DMASNDCTRL_PLAYLOOP ) == 0 ) )
	{
		nDmaSoundControl &= ~DMASNDCTRL_PLAY;
		LOG_TRACE(TRACE_DMASND, "DMA snd stopped because new frame start=end=%x and repeat=off\n", dma.frameStartAddr);
		return;
	}

	/* DMA sound play : update XSINT */
	DmaSnd_Update_XSINT_Line ( MFP_GPIP_STATE_HIGH );	/* 1/HIGH=dma sound play */
}


/*-----------------------------------------------------------------------*/
/**
 * End-of-frame has been reached. Raise interrupts if needed.
 * Returns true if DMA sound processing should be stopped now and false
 * if it continues (DMA PLAYLOOP mode).
 *
 * NOTE : on early STE models the XSINT signal was directly connected
 * to MFP GPIP7 and to Timer A input (for event count mode).
 * On later revisions, as well as on TT, the signal to Timer A input
 * is now delayed by 8 shifts using a 74LS164 running at 2 MHz, which
 * is equivalent to 32 CPU cycles when the CPU runs at 8 MHz.
 * At the emulation level, we don't take into account this delay of
 * 32 CPU cycles, as it would add complexity and no program are known
 * so far to require this delay.
 */
static inline int DmaSnd_EndOfFrameReached(void)
{
	LOG_TRACE(TRACE_DMASND, "DMA snd end of frame\n");

	/* DMA sound idle : update XSINT */
	DmaSnd_Update_XSINT_Line ( MFP_GPIP_STATE_LOW );		/* O/LOW=dma sound idle */

	if (nDmaSoundControl & DMASNDCTRL_PLAYLOOP)
	{
		DmaSnd_StartNewFrame();					/* update XSINT */
	}
	else
	{
		nDmaSoundControl &= ~DMASNDCTRL_PLAY;
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
 * Divide by 4 to account for the STe YM volume table level;
 * ( STe sound at 1/2 amplitude to avoid overflow. )
 * ( lmc1992.right_gain and lmc1992.left_gain are  )
 * ( doubled to compensate. )
 * Divide by 4 to account for DmaSnd_LowPassFilter;
 * Multiply DMA sound by -1 because the LMC1992 inverts the signal
 * ( YM sign is +1 :: -1(op-amp) * -1(Lmc1992) ).
 */


void DmaSnd_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate)
{
	int i;
	int nBufIdx;
	int8_t MonoByte , LeftByte , RightByte;
	unsigned n;
	int64_t FreqRatio;


	/* DMA Audio OFF and FIFO empty : process YM2149's output */
	if ( !(nDmaSoundControl & DMASNDCTRL_PLAY) && ( dma.FIFO_NbBytes == 0 ) )
	{
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) & AUDIOMIXBUFFER_SIZE_MASK;

			switch (microwire.mixing) {
				case 1:
					/* DMA and YM2149 mixing */
					AudioMixBuffer[nBufIdx][0] = AudioMixBuffer[nBufIdx][0] + dma.FrameLeft * -((256*3/4)/4)/4;
					AudioMixBuffer[nBufIdx][1] = AudioMixBuffer[nBufIdx][1] + dma.FrameRight * -((256*3/4)/4)/4;
					break;
				default:
					/* mixing=0 DMA only */
					/* mixing=2 DMA and input 2 (YM2149 LPF) -> DMA */
					/* mixing=3 DMA and input 3 -> DMA */
					AudioMixBuffer[nBufIdx][0] = dma.FrameLeft * -((256*3/4)/4)/4;
					AudioMixBuffer[nBufIdx][1] = dma.FrameRight * -((256*3/4)/4)/4;
					break;
			}
		}

		/* Apply LMC1992 sound modifications (Bass and Treble) */
		DmaSnd_Apply_LMC ( nMixBufIdx , nSamplesToGenerate );

		return;
	}

	/* DMA Anti-alias filter */
	if (DmaSnd_DetectSampleRate() >  nAudioFrequency)
		DmaSnd_LowPass = true;
	else
		DmaSnd_LowPass = false;


	/* DMA Audio ON or FIFO not empty yet */

	/* Compute ratio between DMA's sound frequency and host computer's sound frequency, */
	/* use << 32 to simulate floating point precision */
	FreqRatio = ( ((int64_t)DmaSnd_DetectSampleRate()) << 32 ) / nAudioFrequency;

	if (dma.soundMode & DMASNDMODE_MONO)
	{
		/* Mono 8-bit */
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			if ( DmaInitSample )
			{
				MonoByte = DmaSnd_FIFO_PullByte ();
				dma.FrameLeft  = DmaSnd_LowPassFilterLeft( (int16_t)MonoByte );
				dma.FrameRight = DmaSnd_LowPassFilterRight( (int16_t)MonoByte );
				DmaInitSample = false;
			}

			nBufIdx = (nMixBufIdx + i) & AUDIOMIXBUFFER_SIZE_MASK;

			switch (microwire.mixing) {
				case 1:
					/* DMA and YM2149 mixing */
					AudioMixBuffer[nBufIdx][0] = AudioMixBuffer[nBufIdx][0] + dma.FrameLeft * -((256*3/4)/4)/4;
					break;
				default:
					/* mixing=0 DMA only */
					/* mixing=2 DMA and input 2 (YM2149 LPF) -> DMA */
					/* mixing=3 DMA and input 3 -> DMA */
					AudioMixBuffer[nBufIdx][0] = dma.FrameLeft * -((256*3/4)/4)/4;
					break;
			}

			AudioMixBuffer[nBufIdx][1] = AudioMixBuffer[nBufIdx][0];	/* right = left */

			/* Increase freq counter */
			frameCounter_float += FreqRatio;
			n = frameCounter_float >> 32;				/* number of samples to skip */
			while ( n > 0 )						/* pull as many bytes from the FIFO as needed */
			{
				MonoByte = DmaSnd_FIFO_PullByte ();
				dma.FrameLeft  = DmaSnd_LowPassFilterLeft( (int16_t)MonoByte );
				dma.FrameRight = DmaSnd_LowPassFilterRight( (int16_t)MonoByte );
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
			if ( DmaInitSample )
			{
				LeftByte = DmaSnd_FIFO_PullByte ();
				RightByte = DmaSnd_FIFO_PullByte ();
				dma.FrameLeft  = DmaSnd_LowPassFilterLeft( (int16_t)LeftByte );
				dma.FrameRight = DmaSnd_LowPassFilterRight( (int16_t)RightByte );
				DmaInitSample = false;
			}

			nBufIdx = (nMixBufIdx + i) & AUDIOMIXBUFFER_SIZE_MASK;

			switch (microwire.mixing) {
				case 1:
					/* DMA and YM2149 mixing */
					AudioMixBuffer[nBufIdx][0] = AudioMixBuffer[nBufIdx][0] + dma.FrameLeft * -((256*3/4)/4)/4;
					AudioMixBuffer[nBufIdx][1] = AudioMixBuffer[nBufIdx][1] + dma.FrameRight * -((256*3/4)/4)/4;
					break;
				default:
					/* mixing=0 DMA only */
					/* mixing=2 DMA and input 2 (YM2149 LPF) -> DMA */
					/* mixing=3 DMA and input 3 -> DMA */
					AudioMixBuffer[nBufIdx][0] = dma.FrameLeft * -((256*3/4)/4)/4;
					AudioMixBuffer[nBufIdx][1] = dma.FrameRight * -((256*3/4)/4)/4;
					break;
			}

			/* Increase freq counter */
			frameCounter_float += FreqRatio;
			n = frameCounter_float >> 32;				/* number of samples to skip */
			while ( n > 0 )						/* pull as many bytes from the FIFO as needed */
			{
				LeftByte = DmaSnd_FIFO_PullByte ();
				RightByte = DmaSnd_FIFO_PullByte ();
				dma.FrameLeft  = DmaSnd_LowPassFilterLeft( (int16_t)LeftByte );
				dma.FrameRight = DmaSnd_LowPassFilterRight( (int16_t)RightByte );
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
	int32_t sample;

	/* Apply LMC1992 sound modifications (Left, Right and Master Volume) */
	for (i = 0; i < nSamplesToGenerate; i++) {
		nBufIdx = (nMixBufIdx + i) & AUDIOMIXBUFFER_SIZE_MASK;

		sample = DmaSnd_IIRfilterL( Subsonic_IIR_HPF_Left( AudioMixBuffer[nBufIdx][0]));
		if (sample<-32767)						/* check for overflow to clip waveform */
			sample = -32767;
		else if (sample>32767)
			sample = 32767;
		AudioMixBuffer[nBufIdx][0] = sample;

		sample = DmaSnd_IIRfilterR( Subsonic_IIR_HPF_Right(AudioMixBuffer[nBufIdx][1]));
		if (sample<-32767)						/* check for overflow to clip waveform */
			sample = -32767;
		else if (sample>32767)
			sample = 32767;
		AudioMixBuffer[nBufIdx][1] = sample;
 	}
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
 * We should call it also in the case of the TT which uses the same DMA sound
 */
void DmaSnd_STE_HBL_Update(void)
{
	if ( !Config_IsMachineSTE() && !Config_IsMachineTT() )
		return;

	/* The DMA starts refilling the FIFO when display is OFF (eg cycle 376 in STE low res 50 Hz) */
	DmaSnd_FIFO_Refill ();

	/* If DMA sound is ON or FIFO is not empty, update sound */
	if  ( (nDmaSoundControl & DMASNDCTRL_PLAY) || ( dma.FIFO_NbBytes > 0 ) )
		Sound_Update ( CyclesGlobalClockCounter );

	/* As long as display is OFF, the DMA will refill the FIFO after playing some samples during the HBL */
	DmaSnd_FIFO_Refill ();
}


/*-----------------------------------------------------------------------*/
/**
 * Return current frame counter address (value is always even)
 */
static uint32_t DmaSnd_GetFrameCount(void)
{
	uint32_t nActCount;

	/* Update sound to get the current DMA frame address */
	Sound_Update ( CyclesGlobalClockCounter );

	if (nDmaSoundControl & DMASNDCTRL_PLAY)
		nActCount = dma.frameCounterAddr;
	else
		nActCount = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | (IoMem[0xff8907] & ~1);

	return nActCount;
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound control register (0xff8900).
 */
void DmaSnd_SoundControl_ReadWord(void)
{
	IoMem_WriteWord(0xff8900, nDmaSoundControl);

	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("DMA snd control read: 0x%04x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			nDmaSoundControl,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to sound control register (0xff8900).
 */
void DmaSnd_SoundControl_WriteWord(void)
{
	uint16_t DMASndCtrl_old;

	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("DMA snd control write: 0x%04x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadWord(0xff8900),
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	/* Before starting/stopping DMA sound, create samples up until this point with current values */
	Sound_Update ( Cycles_GetClockCounterOnWriteAccess() );

	DMASndCtrl_old = nDmaSoundControl;
	nDmaSoundControl = IoMem_ReadWord(0xff8900) & 3;

	if (!(DMASndCtrl_old & DMASNDCTRL_PLAY) && (nDmaSoundControl & DMASNDCTRL_PLAY))
	{
		LOG_TRACE(TRACE_DMASND, "DMA snd control write: starting dma sound output\n");
		DmaInitSample = true;
		frameCounter_float = 0;
		DmaSnd_StartNewFrame();				/* update XSINT + this can clear DMASNDCTRL_PLAY */
	}
	else if ((DMASndCtrl_old & DMASNDCTRL_PLAY) && !(nDmaSoundControl & DMASNDCTRL_PLAY))
	{
		LOG_TRACE(TRACE_DMASND, "DMA snd control write: stopping dma sound output\n");
		DmaSnd_Update_XSINT_Line ( MFP_GPIP_STATE_LOW );		/* O/LOW=dma sound idle */
	}
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
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("DMA snd frame start high: 0x%02x at pos %d/%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadByte(0xff8903) , dma.frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr  ,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	/* On STF/STE machines with <= 4MB of RAM, DMA addresses are limited to $3fffff */
	IoMem[ 0xff8903 ] &= DMA_MaskAddressHigh();
}

void DmaSnd_FrameStartMed_WriteByte(void)
{
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("DMA snd frame start med: 0x%02x at pos %d/%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadByte(0xff8905) , dma.frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr  ,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}

void DmaSnd_FrameStartLow_WriteByte(void)
{
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
 		LOG_TRACE_PRINT("DMA snd frame start low: 0x%02x at pos %d/%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadByte(0xff8907) , dma.frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr  ,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	/* DMA address must be word-aligned, bit 0 at $ff8907 is always 0 */
	IoMem[ 0xff8907 ] &= 0xfe;
}

void DmaSnd_FrameCountHigh_WriteByte(void)
{
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
 		LOG_TRACE_PRINT("DMA snd frame count high: 0x%02x at pos %d/%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadByte(0xff8909) , dma.frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr  ,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	/* On STF/STE machines with <= 4MB of RAM, DMA addresses are limited to $3fffff */
	IoMem[ 0xff8909 ] &= DMA_MaskAddressHigh();
}

void DmaSnd_FrameCountMed_WriteByte(void)
{
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
 		LOG_TRACE_PRINT("DMA snd frame count med: 0x%02x at pos %d/%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadByte(0xff890b) , dma.frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr  ,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}

void DmaSnd_FrameCountLow_WriteByte(void)
{
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
 		LOG_TRACE_PRINT("DMA snd frame count low: 0x%02x at pos %d/%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadByte(0xff890d) , dma.frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr  ,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}

void DmaSnd_FrameEndHigh_WriteByte(void)
{
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
 		LOG_TRACE_PRINT("DMA snd frame end high: 0x%02x at pos %d/%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadByte(0xff890f) , dma.frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr  ,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	/* On STF/STE machines with <= 4MB of RAM, DMA addresses are limited to $3fffff */
	IoMem[ 0xff890f ] &= DMA_MaskAddressHigh();
}

void DmaSnd_FrameEndMed_WriteByte(void)
{
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("DMA snd frame end med: 0x%02x at pos %d/%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadByte(0xff8911) , dma.frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr  ,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}

void DmaSnd_FrameEndLow_WriteByte(void)
{
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("DMA snd frame end low: 0x%02x at pos %d/%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadByte(0xff8913) , dma.frameCounterAddr - dma.frameStartAddr , dma.frameEndAddr - dma.frameStartAddr  ,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	/* DMA address must be word-aligned, bit 0 at $ff8613 is always 0 */
	IoMem[ 0xff8913 ] &= 0xfe;
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound mode register (0xff8921).
 */
void DmaSnd_SoundModeCtrl_ReadByte(void)
{
	IoMem_WriteByte(0xff8921, dma.soundMode);

	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("DMA snd mode read: 0x%02x video_cyc=%d %d@%d pc=%x instr_cycle %d\n", dma.soundMode,
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to sound mode register (0xff8921).
 */
void DmaSnd_SoundModeCtrl_WriteByte(void)
{
	uint16_t	SoundModeNew;

	SoundModeNew = IoMem_ReadByte(0xff8921);

	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("DMA snd mode write: 0x%02x mode=%s freq=%d video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			SoundModeNew, SoundModeNew & DMASNDMODE_MONO ? "mono" : "stereo" , DmaSndSampleRates[ SoundModeNew & 3 ],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

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
 * Microwire uses the MWK clock signal at 1 Mhz
 */
void DmaSnd_InterruptHandler_Microwire(void)
{
	int	i;
	uint16_t	cmd;
	int	cmd_len;

	/* If emulated computer is the Falcon, let's the crossbar Microwire code do the job. */
	if (Config_IsMachineFalcon())
	{
		Crossbar_InterruptHandler_Microwire();
		return;
	}
	
	/* How many cycle was this sound interrupt delayed (>= 0) */
	microwire.pendingCyclesOver += -INT_CONVERT_FROM_INTERNAL ( PendingInterruptCount , INT_CPU_CYCLE );

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* Shift the mask and data according to the number of cycles (8 cycles for a shift) */
	do
	{
		--microwire.mwTransferSteps;
			/* Shift data register until it becomes zero. */
		IoMem_WriteWord(0xff8922, microwire.data<<(16-microwire.mwTransferSteps));
			/* Rotate mask register */
		IoMem_WriteWord(0xff8924, (microwire.mask<<(16-microwire.mwTransferSteps))
								|(microwire.mask>>microwire.mwTransferSteps));
		/* 8 cycles for 1 shift */
		microwire.pendingCyclesOver -= 8;
	}
	while ((microwire.mwTransferSteps != 0) && (microwire.pendingCyclesOver >= 8) );

	/* Is the transfer finished ? */
	if (microwire.mwTransferSteps > 0)
	{
		/* No ==> start a new internal interrupt to continue to transfer the data */
		microwire.pendingCyclesOver = 8 - microwire.pendingCyclesOver;
		CycInt_AddRelativeInterrupt(microwire.pendingCyclesOver, INT_CPU8_CYCLE, INTERRUPT_DMASOUND_MICROWIRE);
	}
	else 
	{
		/* Yes : decode the address + command word according to the binary mask */
		/* According to LMC1992 doc, command starts with the first '1' bit in the mask */
		/* and ends when a '0' bits is received in the mask */
		/* If we get a bad command, we must scan the rest of the mask in case there's a valid */
		/* command in the remaining bits */
		/* TODO [NP] : to be really cycle accurate, we should decode the command at the same */
		/* time as we rotate mask/data, instead of doing it when 16 rotations were made. */
		/* But this would not be noticeable, so leave it like this for now */
		cmd = 0;
		cmd_len = 0;
		for ( i=15 ; i>=0 ; i-- )
			if ( microwire.mask & ( 1 << i ) )
			{
				/* Start of command found, wait for next '0' bit or end of mask */
				do
				{
					cmd <<= 1;
					cmd_len++;
					if ( microwire.data & ( 1 << i ) )
						cmd |= 1;
					i--;
				}
				while ( ( i >= 0 ) && ( microwire.mask & ( 1 << i ) ) );

				if ( ( cmd_len >= 11 )
				  && ( ( cmd >> ( cmd_len-2 ) ) & 0x03 ) == 0x02 )
					break;				/* We found a valid command */

				LOG_TRACE ( TRACE_DMASND, "Microwire bad command=0x%x len=%d ignored mask=0x%x data=0x%x\n", cmd , cmd_len , microwire.mask , microwire.data );
				if ( i < 0 )
					return;				/* All bits were tested, stop here */

				/* Check remaining bits for a possible command */
				cmd = 0;
				cmd_len = 0;
			}
//fprintf ( stderr , "mwire cmd=%x len=%d mask=%x data=%x\n" , cmd , cmd_len , microwire.mask , microwire.data );

		/* The LMC 1992 address (first 2 bits) should be "10", else we ignore the command */
		/* The address should be followed by at least 9 bits ; if more bits were received, */
		/* then only the latest 9 ones should be kept */
		if ( ( cmd_len < 11 )
		  || ( ( cmd >> ( cmd_len-2 ) ) & 0x03 ) != 0x02 )
		{
			LOG_TRACE ( TRACE_DMASND, "Microwire bad command=0x%x len=%d ignored mask=0x%x data=0x%x\n", cmd , cmd_len , microwire.mask , microwire.data );
			return;
		}

		/* Update the LMC 1992 commands */
		switch ( ( cmd >> 6 ) & 0x7 ) {
			case 0:
				/* Mixing command */
				LOG_TRACE ( TRACE_DMASND, "Microwire new mixing=0x%x\n", cmd & 0x3 );
				microwire.mixing = cmd & 0x3;
				break;
			case 1:
				/* Bass command */
				LOG_TRACE ( TRACE_DMASND, "Microwire new bass=0x%x\n", cmd & 0xf );
				microwire.bass = cmd & 0xf;
				DmaSnd_Set_Tone_Level(LMC1992_Bass_Treble_Table[microwire.bass], 
						      LMC1992_Bass_Treble_Table[microwire.treble]);
				break;
			case 2: 
				/* Treble command */
				LOG_TRACE ( TRACE_DMASND, "Microwire new trebble=0x%x\n", cmd & 0xf );
				microwire.treble = cmd & 0xf;
				DmaSnd_Set_Tone_Level(LMC1992_Bass_Treble_Table[microwire.bass], 
						      LMC1992_Bass_Treble_Table[microwire.treble]);
				break;
			case 3:
				/* Master volume command */
				LOG_TRACE ( TRACE_DMASND, "Microwire new master volume=0x%x\n", cmd & 0x3f );
				microwire.masterVolume = LMC1992_Master_Volume_Table[ cmd & 0x3f ];
				lmc1992.left_gain = (microwire.leftVolume * (uint32_t)microwire.masterVolume) * (2.0/(65536.0*65536.0));
				lmc1992.right_gain = (microwire.rightVolume * (uint32_t)microwire.masterVolume) * (2.0/(65536.0*65536.0));
				break;
			case 4:
				/* Right channel volume */
				LOG_TRACE ( TRACE_DMASND, "Microwire new right volume=0x%x\n", cmd & 0x1f );
				microwire.rightVolume = LMC1992_LeftRight_Volume_Table[ cmd & 0x1f ];
				lmc1992.right_gain = (microwire.rightVolume * (uint32_t)microwire.masterVolume) * (2.0/(65536.0*65536.0));
				break;
			case 5:
				/* Left channel volume */
				LOG_TRACE ( TRACE_DMASND, "Microwire new left volume=0x%x\n", cmd & 0x1f );
				microwire.leftVolume = LMC1992_LeftRight_Volume_Table[ cmd & 0x1f ];
				lmc1992.left_gain = (microwire.leftVolume * (uint32_t)microwire.masterVolume) * (2.0/(65536.0*65536.0));
				break;
			default:
				/* Do nothing */
				LOG_TRACE ( TRACE_DMASND, "Microwire unknown command=0x%x len=%d ignored mask=0x%x data=0x%x\n", cmd , cmd_len , microwire.mask , microwire.data );
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
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("Microwire data read: 0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadWord(0xff8922),
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
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
		microwire.pendingCyclesOver = 8;
		CycInt_AddRelativeInterrupt(microwire.pendingCyclesOver, INT_CPU8_CYCLE, INTERRUPT_DMASOUND_MICROWIRE);
	}

	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("Microwire data write: 0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadWord(0xff8922),
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}


/**
 * Read word from microwire mask register (0xff8924).
 */
void DmaSnd_MicrowireMask_ReadWord(void)
{
	/* Same as with data register, but mask is rotated, not shifted. */
	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("Microwire mask read: 0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadWord(0xff8924),
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
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

	if(LOG_TRACE_LEVEL(TRACE_DMASND))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("Microwire mask write: 0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
			IoMem_ReadWord(0xff8924),
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
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
static int16_t DmaSnd_LowPassFilterLeft(int16_t in)
{
	static	int16_t	lowPassFilter[2] = { 0, 0 };
	static	int16_t	out = 0;

	if (DmaSnd_LowPass)
		out = lowPassFilter[0] + (lowPassFilter[1]<<1) + in;
	else
		out = lowPassFilter[1] << 2;

	lowPassFilter[0] = lowPassFilter[1];
	lowPassFilter[1] = in;

	return out; /* Filter Gain = 4 */
}

/**
 * LowPass Filter Right
 */
static int16_t DmaSnd_LowPassFilterRight(int16_t in)
{
	static	int16_t	lowPassFilter[2] = { 0, 0 };
	static	int16_t	out = 0;

	if (DmaSnd_LowPass)
		out = lowPassFilter[0] + (lowPassFilter[1]<<1) + in;
	else
		out = lowPassFilter[1] << 2;

	lowPassFilter[0] = lowPassFilter[1];
	lowPassFilter[1] = in;

	return out; /* Filter Gain = 4 */
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

	/* g, fc, Fs must be positive real numbers > 0.0 */
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

	/* g, fc, Fs must be positive real numbers > 0.0 */
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
	lmc1992.left_gain = (microwire.leftVolume * (uint32_t)microwire.masterVolume) * (2.0/(65536.0*65536.0));
	lmc1992.right_gain = (microwire.rightVolume * (uint32_t)microwire.masterVolume) * (2.0/(65536.0*65536.0));
}


void DmaSnd_Info(FILE *fp, uint32_t dummy)
{
	if (Config_IsMachineST())
	{
		fprintf(fp, "ST doesn't include DMA!\n");
		return;
	}
	fprintf(fp, "$FF8900.b : Sound DMA control  : %02x\n", IoMem_ReadByte(0xff8900));
	fprintf(fp, "$FF8901.b : Sound DMA control  : %02x\n", IoMem_ReadByte(0xff8901));
	fprintf(fp, "$FF8903.b : Frame Start High   : %02x\n", IoMem_ReadByte(0xff8903));
	fprintf(fp, "$FF8905.b : Frame Start middle : %02x\n", IoMem_ReadByte(0xff8905));
	fprintf(fp, "$FF8907.b : Frame Start low    : %02x\n", IoMem_ReadByte(0xff8907));
	fprintf(fp, "$FF8909.b : Frame Count High   : %02x\n", IoMem_ReadByte(0xff8909));
	fprintf(fp, "$FF890B.b : Frame Count middle : %02x\n", IoMem_ReadByte(0xff890b));
	fprintf(fp, "$FF890D.b : Frame Count low    : %02x\n", IoMem_ReadByte(0xff890d));
	fprintf(fp, "$FF890F.b : Frame End High     : %02x\n", IoMem_ReadByte(0xff890f));
	fprintf(fp, "$FF8911.b : Frame End middle   : %02x\n", IoMem_ReadByte(0xff8911));
	fprintf(fp, "$FF8913.b : Frame End low      : %02x\n", IoMem_ReadByte(0xff8913));
	fprintf(fp, "\n");
	fprintf(fp, "$FF8920.b : Sound Mode Control : %02x\n", IoMem_ReadByte(0xff8920));
	fprintf(fp, "$FF8921.b : Sound Mode Control : %02x\n", IoMem_ReadByte(0xff8921));
	if (Config_IsMachineFalcon())
	{
		return;
	}
	fprintf(fp, "\n");
	fprintf(fp, "$FF8922.w : Microwire Data     : %04x\n", IoMem_ReadWord(0xff8922));
	fprintf(fp, "$FF8924.w : Microwire Mask     : %04x\n", IoMem_ReadWord(0xff8924));
}
