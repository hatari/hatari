/*
  Hatari - dmaSnd.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  STE DMA sound emulation. Does not seem to be very hard at first glance,
  but since the DMA sound has to be mixed together with the PSG sound and
  the output frequency of the host computer differs from the DMA sound
  frequency, the copy function is a little bit complicated.
  For reducing copy latency, we set a "interrupt" with Int_AddRelativeInterrupt
  to occur just after a sound frame should be finished. There we update the
  sound. The update function also triggers the ST interrupts (Timer A and
  MFP-i7) which are often used in ST programs for setting a new sound frame
  after the old one has finished.

  Falcon sound emulation is all taken into account in crossbar.c

  The microwire interface is not emulated (yet).

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
*/
const char DmaSnd_fileid[] = "Hatari dmaSnd.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "int.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "sound.h"
#include "stMemory.h"

Uint16 nDmaSoundControl;                /* Sound control register */

static Uint16 nDmaSoundMode;            /* Sound mode register */
static Uint16 nMicrowireData;           /* Microwire Data register */
static Uint16 nMicrowireMask;           /* Microwire Mask register */
static int nMwTransferSteps;

static Uint32 nFrameStartAddr;          /* Sound frame start */
static Uint32 nFrameEndAddr;            /* Sound frame end */
static double FrameCounter;             /* Counter in current sound frame */
static int nFrameLen;                   /* Length of the frame */

static const double DmaSndSampleRates[4] =
{
	6258,
	12517,
	25033,
	50066
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
		nDmaSoundMode = 0;
	}

	nMwTransferSteps = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void DmaSnd_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&nDmaSoundControl, sizeof(nDmaSoundControl));
	MemorySnapShot_Store(&nDmaSoundMode, sizeof(nDmaSoundMode));
	MemorySnapShot_Store(&nFrameStartAddr, sizeof(nFrameStartAddr));
	MemorySnapShot_Store(&nFrameEndAddr, sizeof(nFrameEndAddr));
	MemorySnapShot_Store(&FrameCounter, sizeof(FrameCounter));
	MemorySnapShot_Store(&nFrameLen, sizeof(nFrameLen));
	MemorySnapShot_Store(&nMicrowireData, sizeof(nMicrowireData));
	MemorySnapShot_Store(&nMicrowireMask, sizeof(nMicrowireMask));
	MemorySnapShot_Store(&nMwTransferSteps, sizeof(nMwTransferSteps));
}


static double DmaSnd_DetectSampleRate(void)
{
	return DmaSndSampleRates[nDmaSoundMode & 3];
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

	nFrameStartAddr = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | (IoMem[0xff8907] & ~1);
	nFrameEndAddr = (IoMem[0xff890f] << 16) | (IoMem[0xff8911] << 8) | (IoMem[0xff8913] & ~1);

	FrameCounter = 0;
	nFrameLen = nFrameEndAddr - nFrameStartAddr;

	if (nFrameLen <= 0)
	{
		Log_Printf(LOG_WARN, "DMA snd: Illegal buffer size (from 0x%x to 0x%x)\n",
		          nFrameStartAddr, nFrameEndAddr);
		return;
	}

	/* To get smooth sound, set an "interrupt" for the end of the frame that
	 * updates the sound mix buffer. */
	nCyclesForFrame = nFrameLen * (8013000.0 / DmaSnd_DetectSampleRate());
	if (!(nDmaSoundMode & DMASNDMODE_MONO))  /* Is it stereo? */
		nCyclesForFrame = nCyclesForFrame / 2;
	Int_AddRelativeInterrupt(nCyclesForFrame, INT_CPU_CYCLE, INTERRUPT_DMASOUND);
}


/*-----------------------------------------------------------------------*/
/**
 * Check if end-of-frame has been reached and raise interrupts if needed.
 * Returns true if DMA sound processing should be stopped now and false
 * if it continues.
 */
static inline int DmaSnd_CheckForEndOfFrame(int nFrameCounter)
{
	if (nFrameCounter >= nFrameLen)
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

	pFrameStart = (Sint8 *)&STRam[nFrameStartAddr];
	FreqRatio = DmaSnd_DetectSampleRate() / (double)nAudioFrequency;

	if (nDmaSoundMode & DMASNDMODE_MONO)  /* 8-bit stereo or mono? */
	{
		/* Mono 8-bit */
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[nBufIdx][0] = MixBuffer[nBufIdx][1] =
				(int)MixBuffer[nBufIdx][0]
				+ 64 * (int)pFrameStart[(int)FrameCounter];
			FrameCounter += FreqRatio;
			if (DmaSnd_CheckForEndOfFrame(FrameCounter))
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
			nFramePos = ((int)FrameCounter) & ~1;
			MixBuffer[nBufIdx][0] = (int)MixBuffer[nBufIdx][0]
			                        + 64 * (int)pFrameStart[nFramePos];
			MixBuffer[nBufIdx][1] = (int)MixBuffer[nBufIdx][1]
			                        + 64 * (int)pFrameStart[nFramePos+1];
			FrameCounter += FreqRatio;
			if (DmaSnd_CheckForEndOfFrame(FrameCounter))
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
	Int_AcknowledgeInterrupt();

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
		nActCount = nFrameStartAddr + (int)FrameCounter;
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
	IoMem_WriteByte(0xff8921, nDmaSoundMode);

	LOG_TRACE(TRACE_DMASND, "DMA snd mode read: 0x%02x\n", nDmaSoundMode);
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
	nDmaSoundMode = (IoMem_ReadByte(0xff8921)&0x8F);
	/* we also write the masked value back into the emulated hw registers so we have a correct value there */
	IoMem_WriteByte(0xff8921,nDmaSoundMode);
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
	Int_AcknowledgeInterrupt();

	--nMwTransferSteps;

	/* Shift data register until it becomes zero. */
	if (nMwTransferSteps > 1)
	{
		IoMem_WriteWord(0xff8922, nMicrowireData<<(16-nMwTransferSteps));
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
	IoMem_WriteWord(0xff8924, (nMicrowireMask<<(16-nMwTransferSteps))
	                          |(nMicrowireMask>>nMwTransferSteps));

	if (nMwTransferSteps > 0)
	{
		Int_AddRelativeInterrupt(8, INT_CPU_CYCLE, INTERRUPT_DMASOUND_MICROWIRE);
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
	if (!nMwTransferSteps)
	{
		nMicrowireData = IoMem_ReadWord(0xff8922);
		/* Start shifting events to simulate a microwire transfer */
		nMwTransferSteps = 16;
		Int_AddRelativeInterrupt(8, INT_CPU_CYCLE, INTERRUPT_DMASOUND_MICROWIRE);
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
	if (!nMwTransferSteps)
	{
		nMicrowireMask = IoMem_ReadWord(0xff8924);
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
