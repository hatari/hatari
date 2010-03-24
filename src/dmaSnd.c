/*
  Hatari - dmaSnd.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  STE DMA sound emulation. Does not seem to be very hard at first glance,
  but since the DMA sound has to be mixed together with the PSG sound and
  the output frequency of the host computer differs from the DMA sound
  frequency, the copy function is a little bit complicated.
  For reducing copy latency, we set a cycle-interrupt event with
  CycInt_AddRelativeInterrupt to occur just after a sound frame should be
  finished. There we update the sound.
  The update function also triggers the ST interrupts (Timer A and MFP-i7)
  which are often used in ST programs for setting a new sound frame after
  the old one has finished.

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

	The IIR combines a first order bass filter
	with a first order treble filter to make a single
	second order IIR shelving filter. An extra
	multiply is used to control the final volume.

	Sound is stereo filtered by Boosting or Cutting
	the Bass and Treble by +/-12dB in 2dB steps.

	This filter sounds exactly as the Atari TT or STE.
	Sampling frequency = 50066 or 25033 Hz
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


static void DmaSnd_Init_Bass_and_Treble_Tables(void);
static void DmaSnd_Set_Tone_Level(int set_bass, int set_treb);
static float DmaSnd_IIRfilterL(float xn);
static float DmaSnd_IIRfilterR(float xn);
static struct first_order_s *DmaSnd_Treble_Shelf(float g, float fc, float Fs);
static struct first_order_s *DmaSnd_Bass_Shelf(float g, float fc, float Fs);
static Sint16 DmaSnd_LowPassFilterLeft(Sint16 in);
static Sint16 DmaSnd_LowPassFilterMono(Sint16 in);
static Sint16 DmaSnd_LowPassFilterRight(Sint16 in);


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
	float coef[5];			/* IIR coefs */
	/*float gain;*/			/* IIR gain*/
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
	CPU_FREQ / 1280,	/* 6258  Hz */
	CPU_FREQ / 640,		/* 12517 Hz */
	CPU_FREQ / 320,		/* 25033 Hz */
	CPU_FREQ / 160		/* 50066 Hz */
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

	/* Initialise LMC1992 IIR filter parameters */
/*	lmc1992.gain = 1.0;*/
	DmaSnd_Init_Bass_and_Treble_Tables();

	if (bCold)
	{
		dma.soundMode = 3;
		microwire.masterVolume = 65535;
		microwire.leftVolume = 65535;
		microwire.rightVolume = 65535;
		microwire.mixing = 0;
		microwire.bass = 7;
		microwire.treble = 7;
		DmaSnd_Set_Tone_Level(LMC1992_Bass_Treble_Table[microwire.bass], 
				      LMC1992_Bass_Treble_Table[microwire.treble]);
	}

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
	int nCyclesForFrame;

	dma.frameStartAddr = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | (IoMem[0xff8907] & ~1);
	dma.frameEndAddr = (IoMem[0xff890f] << 16) | (IoMem[0xff8911] << 8) | (IoMem[0xff8913] & ~1);

	dma.frameCounter_int = 0;
	dma.frameCounter_dec = 0;
	
	dma.frameLen = dma.frameEndAddr - dma.frameStartAddr;

	if (dma.frameLen <= 0)
	{
		Log_Printf(LOG_WARN, "DMA snd: Illegal buffer size (from 0x%x to 0x%x)\n",
		          dma.frameStartAddr, dma.frameEndAddr);
		return;
	}

	/* To get smooth sound, set an "interrupt" for the end of the frame that
	 * updates the sound mix buffer. */
	nCyclesForFrame = dma.frameLen * (CPU_FREQ / DmaSnd_DetectSampleRate());
	if (!(dma.soundMode & DMASNDMODE_MONO))  /* Is it stereo? */
		nCyclesForFrame = nCyclesForFrame / 2;
	CycInt_AddRelativeInterrupt(nCyclesForFrame, INT_CPU_CYCLE, INTERRUPT_DMASOUND);
}


/*-----------------------------------------------------------------------*/
/**
 * End-of-frame has been reached. Raise interrupts if needed.
 * Returns true if DMA sound processing should be stopped now and false
 * if it continues (DMA PLAYLOOP mode).
 */
static inline int DmaSnd_EndOfFrameReached(void)
{
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
		return true;
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Mix DMA sound sample with the normal PSG sound samples.
 * Note: We adjust the volume level of the 8-bit DMA samples to factor
 * 0.5 compared to the PSG sound samples, this seems to be quite similar
 * to a real STE (and since we got to divide it by 2 again for adding them
 * to the YM samples and multiply by 256 to get to 16-bit, we simply multiply
 * them by 64 in total).
 */
void DmaSnd_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate)
{
	Uint32 FreqRatio, n;
	int i, intPart;
	int nBufIdx, nFramePos;
	Sint8 *pFrameStart;

	if (!(nDmaSoundControl & DMASNDCTRL_PLAY))
		return;

	pFrameStart = (Sint8 *)&STRam[dma.frameStartAddr];

	/* Compute ratio between hatari's sound frequency and host computer's sound frequency */
	FreqRatio = (Uint32)(DmaSnd_DetectSampleRate() / (double)nAudioFrequency * 65536.0);
				
	if (dma.soundMode & DMASNDMODE_MONO)
	{
		/* Mono 8-bit */

		/* Apply anti-aliasing low pass filter ? (mono) */
		if ( /* UseLowPassFilter && */ (dma.soundMode == 3))
		{
			for (n = dma.frameStartAddr; n <= dma.frameEndAddr; n++) {
				pFrameStart[n] = DmaSnd_LowPassFilterMono(pFrameStart[n]);
			}
		}

		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			switch (microwire.mixing) {
				case 0:
					/* DMA and (YM2149 - 12dB) mixing */
					/* instead of 16462 (-12dB), we approximate by 16384 */
					MixBuffer[nBufIdx][0] = ((Sint16)pFrameStart[dma.frameCounter_int] * 128) + 
								(((Sint32)MixBuffer[nBufIdx][0] * 16384)/65536);
					break;
				case 1:
					/* DMA and YM2149 mixing */
					MixBuffer[nBufIdx][0] = MixBuffer[nBufIdx][0] + (Sint16)pFrameStart[dma.frameCounter_int] * 128;	
					break;
				case 2:
					/* DMA sound only */
					MixBuffer[nBufIdx][0] = ((Sint16)pFrameStart[dma.frameCounter_int]) * 128;
					break;
				case 3:
				default:
					/* Reserved, do nothing */
					return;
					break;
			}
			MixBuffer[nBufIdx][1] = MixBuffer[nBufIdx][0];	

			/* Increase ratio pointer to next DMA sample */
			dma.frameCounter_dec += FreqRatio;
			if (dma.frameCounter_dec >= 65536) {
				intPart = dma.frameCounter_dec >> 16;
				dma.frameCounter_int += intPart;
				dma.frameCounter_dec -= intPart* 65536;
			}

			/* Is end of DMA buffer reached ? */
			if (dma.frameCounter_int >= dma.frameLen) {
				if (DmaSnd_EndOfFrameReached())
					break;
			}
		}
	}
	else
	{
		/* Stereo 8-bit */

		/* Apply anti-aliasing low pass filter ? (stereo) */
		if ( /* UseLowPassFilter && */ (dma.soundMode == 3))
		{
			for (n = dma.frameStartAddr; n <= dma.frameEndAddr; n += 2 ) {
				pFrameStart[n]   = DmaSnd_LowPassFilterLeft(pFrameStart[n]);
				pFrameStart[n+1] = DmaSnd_LowPassFilterRight(pFrameStart[n+1]);
			}
		}

		FreqRatio *= 2;

		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			nFramePos = (dma.frameCounter_int) & ~1;
			
			switch (microwire.mixing) {
				case 0:
					/* DMA and (YM2149 - 12dB) mixing */
					/* instead of 16462 (-12dB), we approximate by 16384 */
					MixBuffer[nBufIdx][0] = ((Sint16)pFrameStart[nFramePos] * 128) + 
								(((Sint32)MixBuffer[nBufIdx][0] * 16384) / 65536);
					MixBuffer[nBufIdx][1] = ((Sint16)pFrameStart[nFramePos+1] * 128) + 
								(((Sint32)MixBuffer[nBufIdx][1] * 16384) / 65536);
					break;
				case 1:
					/* DMA and YM2149 mixing */
					MixBuffer[nBufIdx][0] = MixBuffer[nBufIdx][0] + (Sint16)pFrameStart[nFramePos] * 128;	
					MixBuffer[nBufIdx][1] = MixBuffer[nBufIdx][1] + (Sint16)pFrameStart[nFramePos+1] * 128;	
					break;
				case 2:
					/* DMA sound only */
					MixBuffer[nBufIdx][0] = ((Sint16)pFrameStart[nFramePos]) * 128;
					MixBuffer[nBufIdx][1] = ((Sint16)pFrameStart[nFramePos+1]) * 128;
					break;
				case 3:
				default:
					/* Reserved, do nothing */
					return;
					break;
			}

			/* Increase ratio pointer to next DMA sample */
			dma.frameCounter_dec += FreqRatio;
			if (dma.frameCounter_dec >= 65536) {
				intPart = dma.frameCounter_dec >> 16;
				dma.frameCounter_int += intPart;
				dma.frameCounter_dec -= intPart* 65536;
			}

			/* Is end of DMA buffer reached ? */
			if (dma.frameCounter_int >= dma.frameLen) {
				if (DmaSnd_EndOfFrameReached())
					break;
			}
		}
	}

	/* Apply LMC1992 sound modifications (Bass and Treble) 
	   The Bass and Treble get samples at nAudioFrequency rate and that is all that matters.
	           So the following is true but is not what is happening here:

	   The tone control's sampling frequency must be a least 25033 Hz to sound good. 
	   Thus the 12517Hz DAC samples should be doubled before being sent to the tone controls. 
	   IIRfilter() is called twice with the same data at 12517Hz DAC. 
	   IIRfilter() is called four times with the same data at 6258Hz DAC. 
	*/
	for (i = 0; i < nSamplesToGenerate; i++) {
		nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
		MixBuffer[nBufIdx][0] = DmaSnd_IIRfilterL(MixBuffer[nBufIdx][0]);
		MixBuffer[nBufIdx][1] = DmaSnd_IIRfilterR(MixBuffer[nBufIdx][1]);
 	}

	/* Apply LMC1992 sound modifications (Left, Right and Master Volume) */
	for (i = 0; i < nSamplesToGenerate; i++) {
		nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
		MixBuffer[nBufIdx][0] = (((MixBuffer[nBufIdx][0] * microwire.leftVolume) >> 16) * microwire.masterVolume) >> 16;
		MixBuffer[nBufIdx][1] = (((MixBuffer[nBufIdx][1] * microwire.rightVolume) >> 16) * microwire.masterVolume) >> 16;
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
	Sound_Update();
}


/*-----------------------------------------------------------------------*/
/**
 * Create actual position for frame count registers.
 */
static Uint32 DmaSnd_GetFrameCount(void)
{
	Uint32 nActCount;

	if (nDmaSoundControl & DMASNDCTRL_PLAY)
		nActCount = dma.frameStartAddr + (int)dma.frameCounter_int;
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

	LOG_TRACE(TRACE_DMASND, "DMA snd control write: 0x%04x\n", nDmaSoundControl);
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
	LOG_TRACE(TRACE_DMASND, "DMA snd mode write: 0x%02x\n", IoMem_ReadWord(0xff8921));

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
				break;
			case 4:
				/* Right channel volume */
				microwire.rightVolume = LMC1992_LeftRight_Volume_Table[microwire.data & 0x1f];
				break;
			case 5:
				/* Left channel volume */
				microwire.leftVolume = LMC1992_LeftRight_Volume_Table[microwire.data & 0x1f];
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


/*-----------------------------------------------------------------------*/
/**
 * Write word to sound control register (0xff8900).
 */
void DmaSnd_SoundControl_WriteWord(void)
{
	Uint16 nNewSndCtrl;

	LOG_TRACE(TRACE_DMASND, "DMA snd control write: 0x%04x\n", IoMem_ReadWord(0xff8900));

	nNewSndCtrl = IoMem_ReadWord(0xff8900) & 3;

	if (!(nDmaSoundControl & DMASNDCTRL_PLAY) && (nNewSndCtrl & DMASNDCTRL_PLAY))
	{
		//fprintf(stderr, "Turning on DMA sound emulation.\n");
		DmaSnd_StartNewFrame();
	}
	else if ((nDmaSoundControl & DMASNDCTRL_PLAY) && !(nNewSndCtrl & DMASNDCTRL_PLAY))
	{
		//fprintf(stderr, "Turning off DMA sound emulation.\n");
	}

	nDmaSoundControl = nNewSndCtrl;
}


/*-------------------Bass / Treble filter ---------------------------*/

/**
 * Left voice Filter for Bass/Treble.
 */
static float DmaSnd_IIRfilterL(float xn)
{
	static float data[] = { 0.0, 0.0 };
	float a, yn;

	/* Input coefficients */
	/* biquad1  Note: 'a' coefficients are subtracted */
	a  = /*lmc1992.gain * */ xn;		/* a=g*xn;               */
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
	static float data[] = { 0.0, 0.0 };
	float a, yn;

	/* Input coefficients */
	/* biquad1  Note: 'a' coefficients are subtracted */
	a  = /*lmc1992.gain * */ xn;		/* a=g*xn;               */
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
 * LowPass Filter Mono
 */
static Sint16 DmaSnd_LowPassFilterMono(Sint16 in)
{
	static int lowPassFilter[2];
	Sint16	out;
 
	out = (lowPassFilter[0]>>2) + (lowPassFilter[1]>>1) + (in>>2);
	lowPassFilter[0] = lowPassFilter[1];
	lowPassFilter[1] = in;
	return out;
}

/**
 * LowPass Filter Left
 */
static Sint16 DmaSnd_LowPassFilterLeft(Sint16 in)
{
	static int lowPassFilter[2];
	Sint16	out;
 
	out = (lowPassFilter[0]>>2) + (lowPassFilter[1]>>1) + (in>>2);
	lowPassFilter[0] = lowPassFilter[1];
	lowPassFilter[1] = in;
	return out;
}

/**
 * LowPass Filter Right
 */
static Sint16 DmaSnd_LowPassFilterRight(Sint16 in)
{
	static int lowPassFilter[2];
	Sint16	out;
 
	out = (lowPassFilter[0]>>2) + (lowPassFilter[1]>>1) + (in>>2);
	lowPassFilter[0] = lowPassFilter[1];
	lowPassFilter[1] = in;
	return out;
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
static void DmaSnd_Init_Bass_and_Treble_Tables(void)
{
	struct first_order_s *bass;
	struct first_order_s *treb;

	float  dB, g, fc_bt, fc_tt, Fs;
	int    n;

	for (dB = 12.0, n = TONE_STEPS; n--; dB -= 2.0)
	{
		g = powf(10.0, dB / 20.0);	/* 12dB to -12dB */
		fc_bt = 118.2763;
		fc_tt = 8438.756;
		Fs = (float)nAudioFrequency;

		bass = DmaSnd_Bass_Shelf(g, fc_bt, Fs);

		lmc1992.bass_table[n].a1 = bass->a1;
		lmc1992.bass_table[n].b0 = bass->b0;
		lmc1992.bass_table[n].b1 = bass->b1;

		treb = DmaSnd_Treble_Shelf(g, fc_tt, Fs);

		lmc1992.treb_table[n].a1 = treb->a1;
		lmc1992.treb_table[n].b0 = treb->b0;
		lmc1992.treb_table[n].b1 = treb->b1;
	}
}
