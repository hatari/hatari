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
char DmaSnd_rcsid[] = "Hatari $Id: dmaSnd.c,v 1.1 2005-08-06 12:32:09 thothy Exp $";

#include "main.h"
#include "audio.h"
#include "dmaSnd.h"
#include "int.h"
#include "ioMem.h"
#include "mfp.h"
#include "sound.h"


Uint16 nDmaSoundControl;                /* Sound control register */
Uint16 nDmaSoundMode;                   /* Sound mode register */

static Uint32 nFrameStartAddr;          /* Sound frame start */
static Uint32 nFrameEndAddr;            /* Sound frame end */
static double FrameCounter;             /* Counter in current sound frame */
static int nFrameLen;                   /* Length of the frame */

static int DmaSndSampleRates[4] =
{
	6258,
	12517,
	25033,
	50066
};


/*-----------------------------------------------------------------------*/
/*
  This function is called when a new sound frame is started.
  It copies the start and end address from the I/O registers, calculates
  the sample length, etc.
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
	nCyclesForFrame = nFrameLen * (8013000.0 / (double)DmaSndSampleRates[nDmaSoundMode & 3]);
	if (!(nDmaSoundMode & DMASNDMODE_MONO))  /* Is it stereo? */
		nCyclesForFrame = nCyclesForFrame / 2;
	Int_AddRelativeInterrupt(nCyclesForFrame, INTERRUPT_DMASOUND);
}


/*-----------------------------------------------------------------------*/
/*
  Check if end-of-frame has been reached and raise interrupts if needed.
  Returns TRUE if DMA sound processing should be stopped now and FALSE
  if it continues.
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
/*
  Mix DMA sound sample with the normal PSG sound samples.
*/
void DmaSnd_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate)
{
	double FreqRatio;
	int i;
	int nBufIdx;
	Sint8 *pFrameStart;

	if (!(nDmaSoundControl & DMASNDCTRL_PLAY))
		return;

	pFrameStart = &STRam[nFrameStartAddr];
	FreqRatio = (double)DmaSndSampleRates[nDmaSoundMode & 3]
	            / (double)SoundPlayBackFrequencies[OutputAudioFreqIndex];

	if (nDmaSoundMode & DMASNDMODE_MONO)  /* Stereo or mono? */
	{
		/* Mono */
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
		/* Stereo */
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
/*
  DMA sound end of frame "interrupt". Used for updating the sound after
  a frame has been finished.
*/
void DmaSnd_InterruptHandler(void)
{
	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Update sound */
	Sound_Update();
}


/*-----------------------------------------------------------------------*/
/*
  Create actual position for frame count registers.
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
/*
  Read word from sound control register (0xff8900).
*/
void DmaSnd_SoundControl_ReadWord(void)
{
	IoMem_WriteWord(0xff8900, nDmaSoundControl);
}


/*-----------------------------------------------------------------------*/
/*
  Write word to sound control register (0xff8900).
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
/*
  Read word from sound frame count high register (0xff8909).
*/
void DmaSnd_FrameCountHigh_ReadByte(void)
{
	IoMem_WriteByte(0xff8909, DmaSnd_GetFrameCount() >> 16);
}


/*-----------------------------------------------------------------------*/
/*
  Read word from sound frame count medium register (0xff890b).
*/
void DmaSnd_FrameCountMed_ReadByte(void)
{
	IoMem_WriteByte(0xff890b, DmaSnd_GetFrameCount() >> 8);
}


/*-----------------------------------------------------------------------*/
/*
  Read word from sound frame count low register (0xff890d).
*/
void DmaSnd_FrameCountLow_ReadByte(void)
{
	IoMem_WriteByte(0xff890d, DmaSnd_GetFrameCount());
}


/*-----------------------------------------------------------------------*/
/*
  Read word from sound mode register (0xff8920).
*/
void DmaSnd_SoundMode_ReadWord(void)
{
	IoMem_WriteWord(0xff8920, nDmaSoundMode);
}


/*-----------------------------------------------------------------------*/
/*
  Write word to sound mode register (0xff8920).
*/
void DmaSnd_SoundMode_WriteWord(void)
{
	nDmaSoundMode = IoMem_ReadWord(0xff8920);
	//fprintf(stderr,"New sound mode = $%x\n", nDmaSoundMode);
}


/*-----------------------------------------------------------------------*/
/*
  Read word from microwire data register (0xff8922).
*/
void DmaSnd_MicrowireData_ReadWord(void)
{
	/* Temporary hack to get TOS 1.06 and 1.62 working... */
	IoMem_WriteWord(0xff8922, 0);
}
