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
		00 : -12 dB
		01 : Sample and YM2149 mixing
		02 : Sample only
		03 : Reserved

	001 XXD DDD Bass
		0 000 : -12 dB
		0 110 :   0 dB
		1 100 : +12 dB
      
	002 XXD DDD Treble
		0 000 : -12 dB
		0 110 :   0 dB
		1 100 : +12 dB

	003 DDD DDD Master
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

Uint16 nDmaSoundControl;                /* Sound control register */


struct dma_s {
	Uint16 soundMode;		/* Sound mode register */
	Uint32 frameStartAddr;		/* Sound frame start */
	Uint32 frameEndAddr;		/* Sound frame end */
	double frameCounter;		/* Counter in current sound frame */
	int frameLen;			/* Length of the frame */
};

struct microwire_s {
	Uint16 data;			/* Microwire Data register */
	Uint16 mask;			/* Microwire Mask register */
	int mwTransferSteps;		/* Microwire shifting counter */
	Uint16 mixing;			/* Mixing command */
	Uint16 bass;			/* Bass command */
	Uint16 treble;			/* Treble command */
	Uint16 master;			/* Master command */
	Uint16 leftVolume;		/* Left channel volume command */
	Uint16 rightVolume;		/* Right channel volume command */
};

static struct dma_s dma;
static struct microwire_s microwire;


static const double DmaSndSampleRates[4] =
{
	6258.0,
	12517.0,
	25033.0,
	50066.0
};


/*-----------------------------------------------------------------------*/
/**
 * Reset DMA sound variables.
 */
void DmaSnd_Reset(bool bCold)
{
	nDmaSoundControl = 0;

	if (bCold)
	{
		dma.soundMode = 0;
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
}


static double DmaSnd_DetectSampleRate(void)
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

	dma.frameCounter = 0;
	dma.frameLen = dma.frameEndAddr - dma.frameStartAddr;

	if (dma.frameLen <= 0)
	{
		Log_Printf(LOG_WARN, "DMA snd: Illegal buffer size (from 0x%x to 0x%x)\n",
		          dma.frameStartAddr, dma.frameEndAddr);
		return;
	}

	/* To get smooth sound, set an "interrupt" for the end of the frame that
	 * updates the sound mix buffer. */
	nCyclesForFrame = dma.frameLen * (((double)CPU_FREQ) / DmaSnd_DetectSampleRate());
	if (!(dma.soundMode & DMASNDMODE_MONO))  /* Is it stereo? */
		nCyclesForFrame = nCyclesForFrame / 2;
	CycInt_AddRelativeInterrupt(nCyclesForFrame, INT_CPU_CYCLE, INTERRUPT_DMASOUND);
}


/*-----------------------------------------------------------------------*/
/**
 * Check if end-of-frame has been reached and raise interrupts if needed.
 * Returns true if DMA sound processing should be stopped now and false
 * if it continues.
 */
static inline int DmaSnd_CheckForEndOfFrame(int nframeCounter)
{
	if (nframeCounter >= dma.frameLen)
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
	double FreqRatio;
	int i;
	int nBufIdx, nFramePos;
	Sint8 *pFrameStart;

	if (!(nDmaSoundControl & DMASNDCTRL_PLAY))
		return;

	pFrameStart = (Sint8 *)&STRam[dma.frameStartAddr];
	FreqRatio = DmaSnd_DetectSampleRate() / (double)nAudioFrequency;

	if (dma.soundMode & DMASNDMODE_MONO)  /* 8-bit stereo or mono? */
	{
		/* Mono 8-bit */
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[nBufIdx][0] = MixBuffer[nBufIdx][1] =
				(int)MixBuffer[nBufIdx][0]
				+ 64 * (int)pFrameStart[(int)dma.frameCounter];
			dma.frameCounter += FreqRatio;
			if (DmaSnd_CheckForEndOfFrame(dma.frameCounter))
				break;
		}
	}
	else
	{
		/* Stereo 8-bit */
		FreqRatio *= 2.0;
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			nFramePos = ((int)dma.frameCounter) & ~1;
			MixBuffer[nBufIdx][0] = (int)MixBuffer[nBufIdx][0]
			                        + 64 * (int)pFrameStart[nFramePos];
			MixBuffer[nBufIdx][1] = (int)MixBuffer[nBufIdx][1]
			                        + 64 * (int)pFrameStart[nFramePos+1];
			dma.frameCounter += FreqRatio;
			if (DmaSnd_CheckForEndOfFrame(dma.frameCounter))
				break;
		}
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
		nActCount = dma.frameStartAddr + (int)dma.frameCounter;
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
	dma.soundMode = (IoMem_ReadByte(0xff8921) & 0x8F);
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
		/* The LMC 1992 address should be 10x xxx xxx */
		if ((microwire.data & 0x600) != 0x400)
			return;
		
		/* Update the LMC 1992 commands */
		switch ((microwire.data >> 6) & 7) {
			case 0:
				/* Mixing command */
				microwire.mixing = microwire.data & 0x3;
				break;
			case 1:
				/* Bass command */
				microwire.bass = microwire.data & 0xf;
				break;
			case 2: 
				/* Treble command */
				microwire.treble = microwire.data & 0xf;
				break;
			case 3:
				/* Master command */
				microwire.master = microwire.data & 0x3f;
				break;
			case 4:
				/* Right channel volume */
				microwire.rightVolume = microwire.data & 0x1f;
				break;
			case 5:
				/* Left channel volume */
				microwire.leftVolume = microwire.data & 0x1f;
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
