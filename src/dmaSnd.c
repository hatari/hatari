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
const char DmaSnd_rcsid[] = "Hatari $Id: dmaSnd.c,v 1.12 2007-02-17 18:43:40 thothy Exp $";

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "int.h"
#include "ioMem.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "sound.h"


Uint16 nDmaSoundControl;                /* Sound control register */

static Uint16 nDmaSoundMode;            /* Sound mode register */
static Uint16 nMicrowireData;           /* Microwire Data register */
static Uint16 nMicrowireMask;           /* Microwire Mask register */
static int nMwTransferSteps;

static Uint32 nFrameStartAddr;          /* Sound frame start */
static Uint32 nFrameEndAddr;            /* Sound frame end */
static double FrameCounter;             /* Counter in current sound frame */
static int nFrameLen;                   /* Length of the frame */

static const int DmaSndSampleRates[4] =
{
	6258,
	12517,
	25033,
	50066
};


static const int DmaSndFalcSampleRates[] =
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
void DmaSnd_Reset(BOOL bCold)
{
	nDmaSoundControl = 0;

	if (bCold)
	{
		nDmaSoundMode = 0;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void DmaSnd_MemorySnapShot_Capture(BOOL bSave)
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
		return (double)DmaSndFalcSampleRates[nFalcClk-1];
	}
	else
	{
		return (double)DmaSndSampleRates[nDmaSoundMode & 3];
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
	Int_AddRelativeInterrupt(nCyclesForFrame, INTERRUPT_DMASOUND);
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
	    && (nDmaSoundMode & 0x40))
	{
		/* Stereo 16-bit */
		FreqRatio *= 4.0;
		for (i = 0; i < nSamplesToGenerate; i++)
		{
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[nBufIdx] = ((int)MixBuffer[nBufIdx] + (int)pFrameStart[((int)FrameCounter)&~1]
			                      + (int)pFrameStart[(((int)FrameCounter)&~1)+2]) / 3;
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
			MixBuffer[nBufIdx] = ((int)MixBuffer[nBufIdx] + (int)pFrameStart[(int)FrameCounter]) / 2;
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
			MixBuffer[nBufIdx] = ((int)MixBuffer[nBufIdx] + (int)pFrameStart[((int)FrameCounter)&~1]
			                      + (int)pFrameStart[(((int)FrameCounter)&~1)+1]) / 3;
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

	/* Make sure that emulated microwire isn't busy forever */
	nMwTransferSteps = 0;
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
 */
/* FIXME: add Falcon specific handler here... */
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
 * Read word from microwire data register (0xff8922).
 */
void DmaSnd_MicrowireData_ReadWord(void)
{
	/* The Microwire shifts the data register bit by bit until it is zero
	 * within 16 usec. We don't emulate the time, only the shift on read access,
	 * so this is not very accurate yet. */
	if (nMwTransferSteps > 0)
	{
		IoMem_WriteWord(0xff8922, IoMem_ReadWord(0xff8922)<<2);
		nMwTransferSteps -= 2;
	}
	else
	{
		IoMem_WriteWord(0xff8922, 0);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to microwire data register (0xff8922).
 */
void DmaSnd_MicrowireData_WriteWord(void)
{
	nMicrowireData = IoMem_ReadWord(0xff8922);
	nMwTransferSteps = 16;      /* To simulate a microwire transfer */
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from microwire mask register (0xff8924).
 */
void DmaSnd_MicrowireMask_ReadWord(void)
{
	/* Same as with data register, but mask register is rotated, not  shifted. */
	if (nMwTransferSteps > 0)
	{
		IoMem_WriteWord(0xff8924, (IoMem_ReadWord(0xff8924)<<2)|(IoMem_ReadWord(0xff8924)>>14));
		nMwTransferSteps -= 2;
	}
	else
	{
		IoMem_WriteWord(0xff8924, nMicrowireMask);
	}

}


/*-----------------------------------------------------------------------*/
/**
 * Write word to microwire mask register (0xff8924).
 */
void DmaSnd_MicrowireMask_WriteWord(void)
{
	nMicrowireMask = IoMem_ReadWord(0xff8924);
}
