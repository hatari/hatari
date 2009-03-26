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


static const double DmaSndFalcSampleRates[] =
{
	49170,
	32780,
	24585,
	19668,
	16390,
	14049,
	12292,
	10927,
	 9834,
	 8940,
	 8195,
	 7565,
	 7024,
	 6556,
	 6146,
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
	int nFalcClk = IoMem[0xff8935] & 0x0f;

	if (ConfigureParams.System.nMachineType == MACHINE_FALCON && nFalcClk != 0)
	{
		return DmaSndFalcSampleRates[nFalcClk-1];
	}
	else
	{
		return DmaSndSampleRates[nDmaSoundMode & 3];
	}
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
 * Returns TRUE if DMA sound processing should be stopped now and FALSE
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
			return TRUE;
		}
	}

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Mix DMA sound sample with the normal PSG sound samples.
 */
void DmaSnd_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate)
{
	double FreqRatio;
	int i;
	int nBufIdx;
	Sint8 *pFrameStart;

	if (!(nDmaSoundControl & DMASNDCTRL_PLAY))
		return;

	pFrameStart = (Sint8 *)&STRam[nFrameStartAddr];
	FreqRatio = DmaSnd_DetectSampleRate() / (double)SoundPlayBackFrequencies[OutputAudioFreqIndex];

	if (ConfigureParams.System.nMachineType == MACHINE_FALCON
	    && (nDmaSoundMode & DMASNDMODE_16BITSTEREO))
	{
		/* Stereo 16-bit */
		FreqRatio *= 4.0;
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[nBufIdx][0] = ((int)MixBuffer[nBufIdx][0]
			                        + (int)(*(Sint16*)&pFrameStart[((int)FrameCounter)&~1])) / 2;
			MixBuffer[nBufIdx][1] = ((int)MixBuffer[nBufIdx][1]
			                        + (int)(*(Sint16*)&pFrameStart[(((int)FrameCounter)&~1)+2])) / 2;
			FrameCounter += FreqRatio;
			if (DmaSnd_CheckForEndOfFrame(FrameCounter))
				break;
		}
	}
	else if (nDmaSoundMode & DMASNDMODE_MONO)  /* 8-bit stereo or mono? */
	{
		/* Mono 8-bit */
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[nBufIdx][0] = MixBuffer[nBufIdx][1] =
				((int)MixBuffer[nBufIdx][0] + (((int)pFrameStart[(int)FrameCounter]) << 8)) / 2;
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
			MixBuffer[nBufIdx][0] = ((int)MixBuffer[nBufIdx][0]
			                        + (((int)pFrameStart[((int)FrameCounter)&~1]) << 8)) / 2;
			MixBuffer[nBufIdx][1] = ((int)MixBuffer[nBufIdx][1]
			                        + (((int)pFrameStart[(((int)FrameCounter)&~1)+1]) << 8)) / 2;
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
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to sound control register (0xff8900).
 *
 * FIXME: add Falcon specific handler here...
 */
void DmaSnd_SoundControl_WriteWord(void)
{
	Uint16 nNewSndCtrl;

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
 * Read word from sound mode register (0xff8920).
 */
void DmaSnd_SoundMode_ReadWord(void)
{
	IoMem_WriteWord(0xff8920, nDmaSoundMode);
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to sound mode register (0xff8920).
 * Handling framework for Falcon specific bits by Matthias Arndt <marndt@asmsoftware.de>
 */
void DmaSnd_SoundMode_WriteWord(void)
{
	/* Handle Falcon specialities: STE and TT only have bits 7,1 and 0 in this register */

	/* Falcon has meaning in almost all bits of SND_SMC */
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
	{

		nDmaSoundMode = IoMem_ReadWord(0xff8920);
		/* FIXME: add code here to evaluate Falcon specific settings */


	} else {
		/* STE or TT - hopefully STFM emulation never gets here :)
		 * we maskout the Falcon only bits so we only hit bits that exist on a real STE
		 */
		nDmaSoundMode = (IoMem_ReadWord(0xff8920)&0x008F);
		/* we also write the masked value back into the emulated hw registers so we have a correct value there */
		IoMem_WriteWord(0xff8920,nDmaSoundMode);
	}
	//fprintf(stderr,"New sound mode = $%x\n", nDmaSoundMode);
}


/*-----------------------------------------------------------------------*/
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
	//fprintf(stderr, "MW data read: 0x%x\n", IoMem_ReadWord(0xff8922));
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

	//fprintf(stderr, "MW data write: 0x%x\n", IoMem_ReadWord(0xff8922));
}


/**
 * Read word from microwire mask register (0xff8924).
 */
void DmaSnd_MicrowireMask_ReadWord(void)
{
	/* Same as with data register, but mask is rotated, not shifted. */
	//fprintf(stderr, "MW mask read: 0x%x\n", IoMem_ReadWord(0xff8924));
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

	//fprintf(stderr, "MW mask write: 0x%x\n", IoMem_ReadWord(0xff8924));
}


/* ---------------------- Falcon sound subsystem ---------------------- */

/**
 * Read word from Falcon crossbar source register (0xff8930).
 */
void DmaSnd_CrossbarSrc_ReadWord(void)
{
}

/**
 * Write word to Falcon crossbar source register (0xff8930).
 */
void DmaSnd_CrossbarSrc_WriteWord(void)
{
}

/**
 * Read word from Falcon crossbar destination register (0xff8932).
 */
void DmaSnd_CrossbarDst_ReadWord(void)
{
}

/**
 * Write word to Falcon crossbar destination register (0xff8932).
 */
void DmaSnd_CrossbarDst_WriteWord(void)
{
}

/**
 * Read byte from external clock divider register (0xff8934).
 */
void DmaSnd_FreqDivExt_ReadByte(void)
{
}

/**
 * Write byte to external clock divider register (0xff8934).
 */
void DmaSnd_FreqDivExt_WriteByte(void)
{
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void DmaSnd_FreqDivInt_ReadByte(void)
{
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void DmaSnd_FreqDivInt_WriteByte(void)
{
}

/**
 * Read byte from track record control register (0xff8936).
 */
void DmaSnd_TrackRecCtrl_ReadByte(void)
{
}

/**
 * Write byte to track record control register (0xff8936).
 */
void DmaSnd_TrackRecCtrl_WriteByte(void)
{
}

/**
 * Read byte from CODEC input register (0xff8937).
 */
void DmaSnd_CodecInput_ReadByte(void)
{
}

/**
 * Write byte to CODEC input register (0xff8937).
 */
void DmaSnd_CodecInput_WriteByte(void)
{
}

/**
 * Read byte from A/D converter input register (0xff8938).
 */
void DmaSnd_AdcInput_ReadByte(void)
{
}

/**
 * Write byte to A/D converter input register (0xff8938).
 */
void DmaSnd_AdcInput_WriteByte(void)
{
}

/**
 * Read byte from input amplifier register (0xff8939).
 */
void DmaSnd_InputAmp_ReadByte(void)
{
}

/**
 * Write byte to input amplifier register (0xff8939).
 */
void DmaSnd_InputAmp_WriteByte(void)
{
}

/**
 * Read word from output reduction register (0xff893a).
 */
void DmaSnd_OutputReduct_ReadWord(void)
{
}

/**
 * Write word to output reduction register (0xff893a).
 */
void DmaSnd_OutputReduct_WriteWord(void)
{
}

/**
 * Read word from CODEC status register (0xff893c).
 */
void DmaSnd_CodecStatus_ReadWord(void)
{
}

/**
 * Write word to CODEC status register (0xff893c).
 */
void DmaSnd_CodecStatus_WriteWord(void)
{
}
