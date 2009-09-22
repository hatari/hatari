/*
  Hatari - Crossbar.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Falcon Crossbar (Matrice) emulation.
  input device:	- DSP transmit (SSI)
		- external DSP connector
		- ADC (micro + PSG chip)
		- DMA playback

  output device:- external DSP connector
		- DSP receive (SSI)
		- DAC (headphone, bipper, monitor sound)
		- DMA record

  There are 3 possible clocks :
		- internal clock 25,175 MHz (Ste compatible)
		- internal clock 32 MHz
		- external clock (DSP external port, up to 32 Mhz)

  Transfers between 2 devices can use handshaking or continuous mode 

  Hardware I/O registers:
    $FF8900 (byte) : Sound DMA control
    $FF8901 (byte) : Sound DMA control
    $FF8903 (byte) : Frame Start Hi
    $FF8905 (byte) : Frame Start Mi
    $FF8907 (byte) : Frame Start Lo
    $FF8909 (byte) : Frame Count Hi
    $FF890B (byte) : Frame Count Mi
    $FF890D (byte) : Frame Count Lo
    $FF890F (byte) : Frame End Hi
    $FF8911 (byte) : Frame End Mi
    $FF8913 (byte) : Frame End Lo
    $FF8920 (byte) : Sound Mode Control
    $FF8921 (byte) : Sound Mode Control
    $FF8930 (word) : DMA Crossbar Input Select Controller
    $FF8932 (word) : DMA Crossbar Output Select Controller
    $FF8934 (byte) : External Sync Frequency Divider
    $FF8935 (byte) : Internal Sync Frequency Divider
    $FF8936 (byte) : Record Track select
    $FF8937 (byte) : Codec Input Source
    $FF8938 (byte) : Codec ADC Input
    $FF8939 (byte) : Gain Settings Per Channel
    $FF893A (byte) : Attenuation Settings Per Channel
    $FF893C (word) : Codec Status
    $FF8940 (word) : GPIO Data Direction
    $FF8942 (word) : GPIO Data

    From Laurent :
    For now, I'll code DMA, DSP and DAC.
    ADC will be included later
*/

const char crossbar_fileid[] = "Hatari Crossbar.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "int.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "sound.h"
#include "crossbar.h"
#include "stMemory.h"
#include "falcon/dsp.h"


static inline int DmaSnd_CheckForEndOfFrame(void);
static double Crossbar_DetectSampleRate(void);
static void Crossbar_SendDataToDAC(Sint16 value);
static void Crossbar_SendDataToDspReceive(Uint32 value);

static void Crossbar_StartDspXmitHandler(void);
static void Crossbar_StartDmaXmitHandler(void);


/* external data used by the MFP */
Uint16 nCbar_DmaSoundControl;

/* internal datas */
static Sint16 DacOutBuffer[MIXBUFFER_SIZE*2];
static int nDacOutRdPos, nDacOutWrPos, nDacBufSamples;

static Uint16 nDmaSoundMode;		/* Sound mode register ($ff8921.b) */


static Uint32 nFrameStartAddr;		/* Sound frame start */
static Uint32 nFrameEndAddr;		/* Sound frame end */
static Uint32 nFrameCounter;		/* Counter in current sound frame */
static Uint32 nFrameLen;		/* Length of the frame */


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
 * Reset Crossbar variables.
 */
void Crossbar_Reset(bool bCold)
{
	nCbar_DmaSoundControl = 0;

	if (bCold)
	{
	}

	/* Clear DAC buffer */
	memset(DacOutBuffer, 0, sizeof(DacOutBuffer));
	nDacOutRdPos = nDacOutWrPos = 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void Crossbar_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&nCbar_DmaSoundControl, sizeof(nCbar_DmaSoundControl));
	MemorySnapShot_Store(&nDmaSoundMode, sizeof(nDmaSoundMode));
	MemorySnapShot_Store(&nFrameStartAddr, sizeof(nFrameStartAddr));
	MemorySnapShot_Store(&nFrameEndAddr, sizeof(nFrameEndAddr));
	MemorySnapShot_Store(&nFrameCounter, sizeof(nFrameCounter));
	MemorySnapShot_Store(&nFrameLen, sizeof(nFrameLen));
	MemorySnapShot_Store(&DacOutBuffer, sizeof(DacOutBuffer));
	MemorySnapShot_Store(&nDacOutRdPos, sizeof(nDacOutRdPos));
	MemorySnapShot_Store(&nDacOutWrPos, sizeof(nDacOutWrPos));
}


/*----------------------------------------------------------------------*/
/*	Hardware I/O functions						*/
/*----------------------------------------------------------------------*/

/**
 * Read byte from buffer interrupts (0xff8900).
 */
void Crossbar_BufferInter_ReadWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar DMA track control register read: 0x%02x\n", IoMem_ReadByte(0xff8900));
}

/**
 * Write byte to buffer interrupts (0xff8900).
 */
void Crossbar_BufferInter_WriteWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar DMA track control register write: 0x%02x\n", IoMem_ReadByte(0xff8900));
}

/**
 * Read byte from DMA control register (0xff8901).
 */
void Crossbar_DmaCtrlReg_ReadWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar DMA control register read: 0x%02x\n", IoMem_ReadByte(0xff8901));
}

/**
 * Write byte from DMA control register (0xff8901).
 */
void Crossbar_DmaCtrlReg_WriteWord(void)
{
	Uint16 nNewSndCtrl;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar DMA control register write: 0x%02x\n", IoMem_ReadByte(0xff8901));

	nNewSndCtrl = IoMem_ReadByte(0xff8901) & 3;

	if (!(nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_PLAY) && (nNewSndCtrl & CROSSBAR_SNDCTRL_PLAY))
	{
		//fprintf(stderr, "Turning on DMA sound emulation.\n");
		/* DMA setings */
		nFrameStartAddr = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | (IoMem[0xff8907] & ~1);
		nFrameEndAddr = (IoMem[0xff890f] << 16) | (IoMem[0xff8911] << 8) | (IoMem[0xff8913] & ~1);
		nFrameLen = nFrameEndAddr - nFrameStartAddr;
	
		if (nFrameLen <= 0)
		{
			Log_Printf(LOG_WARN, "crossbar DMA snd: Illegal buffer size (from 0x%x to 0x%x)\n",
				nFrameStartAddr, nFrameEndAddr);
			return;
		}
		nCbar_DmaSoundControl = nNewSndCtrl;
		Crossbar_StartDmaXmitHandler();
	}
	else if ((nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_PLAY) && !(nNewSndCtrl & CROSSBAR_SNDCTRL_PLAY))
	{
		nCbar_DmaSoundControl = nNewSndCtrl;
		//fprintf(stderr, "Turning off DMA sound emulation.\n");
	}

}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound frame count high register (0xff8909).
 */
void Crossbar_FrameCountHigh_ReadByte(void)
{
	IoMem_WriteByte(0xff8909, (nFrameStartAddr + nFrameCounter) >> 16);
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound frame count medium register (0xff890b).
 */
void Crossbar_FrameCountMed_ReadByte(void)
{
	IoMem_WriteByte(0xff890b, (nFrameStartAddr + nFrameCounter) >> 8);
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound frame count low register (0xff890d).
 */
void Crossbar_FrameCountLow_ReadByte(void)
{
	IoMem_WriteByte(0xff890d, (nFrameStartAddr + nFrameCounter));
}


/*-----------------------------------------------------------------------*/
/**
 * Read byte from DMA track control (0xff8920).
 */
void Crossbar_DmaTrckCtrl_ReadByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar DMA track control register read: 0x%02x\n", IoMem_ReadByte(0xff8920));
}

/**
 * Write byte to DMA track control (0xff8920).
 */
void Crossbar_DmaTrckCtrl_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar DMA track control register write: 0x%02x\n", IoMem_ReadByte(0xff8920));
}

/**
 * Read word from sound mode register (0xff8921).
 */
void Crossbar_SoundModeCtrl_ReadByte(void)
{
	IoMem_WriteByte(0xff8921, nDmaSoundMode);
	LOG_TRACE(TRACE_CROSSBAR, "crossbar snd mode read: 0x%02x\n", nDmaSoundMode);
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to sound mode register (0xff8921).
 */
void Crossbar_SoundModeCtrl_WriteByte(void)
{
	nDmaSoundMode = IoMem_ReadByte(0xff8921);
	LOG_TRACE(TRACE_CROSSBAR, "crossbar snd mode write: 0x%02x\n", nDmaSoundMode);
}


/**
 * Read word from Falcon Crossbar source controller (0xff8930).
 */
void Crossbar_SrcControler_ReadWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd Crossbar src read: 0x%04x\n", IoMem_ReadWord(0xff8930));
}

/**
 * Write word to Falcon Crossbar source controller (0xff8930).
	Source: A/D Convertor                 BIT 15 14 13 12
	1 - Connect, 0 - disconnect ---------------'  |  |  |
	00 - 25.175Mhz clock -------------------------+--+  |
	01 - External clock --------------------------+--+  |
	10 - 32Mhz clock (Don't use) -----------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'

	Source: External Input                BIT 11 10  9  8
	0 - DSP IN, 1 - All others ----------------'  |  |  |
	00 - 25.175Mhz clock -------------------------+--+  |
	01 - External clock --------------------------+--+  |
	10 - 32Mhz clock -----------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'

	Source: DSP-XMIT                      BIT  7  6  5  4
	0 - Tristate and disconnect DSP -----------+  |  |  |
	    (Only for external SSI use)            |  |  |  |
	1 - Connect DSP to multiplexer ------------'  |  |  |
	00 - 25.175Mhz clock -------------------------+--+  |
	01 - External clock --------------------------+--+  |
	10 - 32Mhz clock -----------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'

	Source: DMA-PLAYBACK                  BIT  3  2  1  0
	0 - Handshaking on, dest DSP-REC ----------+  |  |  |
	1 - Destination is not DSP-REC ------------'  |  |  |
	00 - 25.175Mhz clock -------------------------+--+  |
	01 - External clock --------------------------+--+  |
	10 - 32Mhz clock -----------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'
 */
void Crossbar_SrcControler_WriteWord(void)
{
	Uint16 nCbSrc = IoMem_ReadWord(0xff8930);
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd Crossbar src write: 0x%04x\n", nCbSrc);

	/* Start DSP External input Interrupt */
	/* Todo : emulate the external port ? */

	/* Start ADC input Interrupt */
	/* Todo : emulate the ADC input interrupt */
	
	/* Start Dma Playback Interrupt */
//	Crossbar_StartDmaXmitHandler();
	
	/* Start DSP out Playback Interrupt */
	if (nCbSrc & 0x80) {
		/* Dsp is not in tristate mode */
		Crossbar_StartDspXmitHandler();
	}
}


/**
 * Read word from Falcon Crossbar destination controller (0xff8932).
 */
void Crossbar_DstControler_ReadWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd Crossbar dst read: 0x%04x\n", IoMem_ReadWord(0xff8932));
}

/**
 * Write word to Falcon Crossbar destination controller (0xff8932).
	Source: A/D Convertor                 BIT 15 14 13 12
	1 - Connect, 0 - disconnect ---------------'  |  |  |
	00 - 25.175Mhz clock -------------------------+--+  |
	01 - External clock --------------------------+--+  |
	10 - 32Mhz clock (Don't use) -----------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'

	Source: External Input                BIT 11 10  9  8
	0 - DSP IN, 1 - All others ----------------'  |  |  |
	00 - 25.175Mhz clock -------------------------+--+  |
	01 - External clock --------------------------+--+  |
	10 - 32Mhz clock -----------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'

	Source: DSP-XMIT                      BIT  7  6  5  4
	0 - Tristate and disconnect DSP -----------+  |  |  |
	    (Only for external SSI use)            |  |  |  |
	1 - Connect DSP to multiplexer ------------'  |  |  |
	00 - 25.175Mhz clock -------------------------+--+  |
	01 - External clock --------------------------+--+  |
	10 - 32Mhz clock -----------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'

	Source: DMA-PLAYBACK                  BIT  3  2  1  0
	0 - Handshaking on, dest DSP-REC ----------+  |  |  |
	1 - Destination is not DSP-REC ------------'  |  |  |
	00 - 25.175Mhz clock -------------------------+--+  |
	01 - External clock --------------------------+--+  |
	10 - 32Mhz clock -----------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'
 */
void Crossbar_DstControler_WriteWord(void)
{
	Uint16 destCtrl = IoMem_ReadWord(0xff8932);

	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd Crossbar dst write: 0x%04x\n", destCtrl);
}

/**
 * Read byte from external clock divider register (0xff8934).
 */
void Crossbar_FreqDivExt_ReadByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd ext. clock divider read: 0x%02x\n", IoMem_ReadByte(0xff8934));
}

/**
 * Write byte to external clock divider register (0xff8934).
 */
void Crossbar_FreqDivExt_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd ext. clock divider write: 0x%02x\n", IoMem_ReadByte(0xff8934));
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void Crossbar_FreqDivInt_ReadByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd int. clock divider read: 0x%02x\n", IoMem_ReadByte(0xff8935));
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void Crossbar_FreqDivInt_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd int. clock divider write: 0x%02x\n", IoMem_ReadByte(0xff8935));
}

/**
 * Read byte from record track select register (0xff8936).
 *	0 = Record 1 track
 *	1 = Record 2 tracks
 *	2 = Record 3 tracks
 *	3 = Record 4 tracks
 */
void Crossbar_TrackRecSelect_ReadByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd record track select read: 0x%02x\n", IoMem_ReadByte(0xff8936));
}

/**
 * Write byte to record track select register (0xff8936).
 *	0 = Record 1 track
 *	1 = Record 2 tracks
 *	2 = Record 3 tracks
 *	3 = Record 4 tracks
 */
void Crossbar_TrackRecSelect_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd record track select write: 0x%02x\n", IoMem_ReadByte(0xff8936));
}

/**
 * Read byte from CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D convertor 
 */
void Crossbar_CodecInput_ReadByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd CODEC input read: 0x%02x\n", IoMem_ReadByte(0xff8937));
}

/**
 * Write byte to CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D convertor 
 */
void Crossbar_CodecInput_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd CODEC input write: 0x%02x\n", IoMem_ReadByte(0xff8937));
}

/**
 * Read byte from A/D converter input for L+R channel (0xff8938).
 *	Bit 1 :  Left (0 = Microphone ; 1 = PSG soundchip)
 *	Bit 0 : Right (0 = Microphone ; 1 = PSG soundchip)
 */
void Crossbar_AdcInput_ReadByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd ADC input read: 0x%02x\n", IoMem_ReadByte(0xff8938));
}

/**
 * Write byte to A/D converter input for L+R channel (0xff8938).
 *	Bit 1 :  Left (0 = Microphone ; 1 = PSG soundchip)
 *	Bit 0 : Right (0 = Microphone ; 1 = PSG soundchip)
 */
void Crossbar_AdcInput_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd ADC input write: 0x%02x\n", IoMem_ReadByte(0xff8938));
}

/**
 * Read byte from input amplifier register (0xff8939).
 * 	Bits LLLLRRRR
 * 	Amplification is in +1.5 dB steps
 */
void Crossbar_InputAmp_ReadByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd CODEC channel amplification read: 0x%04x\n", IoMem_ReadWord(0xff8939));
}

/**
 * Write byte to input amplifier register (0xff8939).
 * 	Bits LLLLRRRR
 * 	Amplification is in +1.5 dB steps
 */
void Crossbar_InputAmp_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd CODEC channel amplification write: 0x%04x\n", IoMem_ReadWord(0xff8939));
}

/**
 * Read word from output reduction register (0xff893a).
 * 	Bits LLLLRRRR
 * 	Reduction is in -1.5 dB steps
 */
void Crossbar_OutputReduct_ReadWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd CODEC channel attenuation read: 0x%04x\n", IoMem_ReadWord(0xff893a));
}

/**
 * Write word to channel reduction register (0xff893a).
 * 	Bits LLLLRRRR
 * 	Reduction is in -1.5 dB steps
 */
void Crossbar_OutputReduct_WriteWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd CODEC channel attenuation write: 0x%04x\n", IoMem_ReadWord(0xff893a));
}

/**
 * Read word from CODEC status register (0xff893c).
 * 	Bit 1 :  Left Channel Overflow (0/1)
 * 	Bit 0 : Right Channel Overflow (0/1)
 */
void Crossbar_CodecStatus_ReadWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd CODEC status read: 0x%04x\n", IoMem_ReadWord(0xff893c));
}

/**
 * Write word to CODEC status register (0xff893c).
 * 	Bit 1 :  Left Channel Overflow (0/1)
 * 	Bit 0 : Right Channel Overflow (0/1)
 */
void Crossbar_CodecStatus_WriteWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Falcon snd CODEC status write: 0x%04x\n", IoMem_ReadWord(0xff893c));
}


/*----------------------------------------------------------------------*/
/* ---------------------- Falcon sound subsystem ---------------------- */
/*----------------------------------------------------------------------*/

/**
 * Detect sample rate frequency
 */
static double Crossbar_DetectSampleRate(void)
{
	int nFalcClk = IoMem[0xff8935] & 0x0f;

	if (nFalcClk != 0)
	{
		return DmaSndFalcSampleRates[nFalcClk-1];
	}

	return DmaSndSampleRates[IoMem[0xff8921] & 3];
}



/*----------------------------------------------------------------------*/
/*			DSP Xmit processing				*/
/*----------------------------------------------------------------------*/

static void Crossbar_StartDspXmitHandler(void)
{
	Uint16 nCbSrc = IoMem_ReadWord(0xff8930);
	int freq = 1;

	if ((nCbSrc & 0x60) == 0x00)
	{
		/* Internal 25.175 MHz clock */
		freq = 25175000 / Crossbar_DetectSampleRate();
	}
	else if ((nCbSrc & 0x60) == 0x40)
	{
		/* Internal 32 MHz clock */
		freq = 32000000 / Crossbar_DetectSampleRate();
	}

	Int_AddRelativeInterrupt(CPU_FREQ/freq/256, INT_CPU_CYCLE, INTERRUPT_DSPXMIT);
}


void Crossbar_InterruptHandler_DspXmit(void)
{
	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* TODO: implementing of handshake mode and start frame */

	/* Send sample to DMA sound */
	Crossbar_SendDataToDAC(DSP_SsiReadTxValue());

	/* Send the new frame start status to the DSP SSI */
	DSP_SsiReceive_SC2(0);

	/* Send the clock to the DSP SSI */
	/* Todo : vérify that the DSP is connected to the DAC */
	DSP_SsiReceive_SC0(0);

	/* Restart the Int event handler */
	Crossbar_StartDspXmitHandler();
}

/*----------------------------------------------------------------------*/
/*			DSP Receive processing				*/
/*----------------------------------------------------------------------*/

/**
 * Transmit data from crossbar to DSP receive.
 */
void Crossbar_SendDataToDspReceive(Uint32 value)
{
}

/*----------------------------------------------------------------------*/
/*			DMA Playback processing				*/
/*----------------------------------------------------------------------*/

/**
 * Check if end-of-frame has been reached and raise interrupts if needed.
 * Returns true if DMA sound processing should be stopped now and false
 * if it continues.
 */
static inline int DmaSnd_CheckForEndOfFrame()
{
	if (nFrameCounter >= nFrameLen)
	{
		/* Raise end-of-frame interrupts (MFP-i7 and Time-A) */
		MFP_InputOnChannel(MFP_TIMER_GPIP7_BIT, MFP_IERA, &MFP_IPRA);
		if (MFP_TACR == 0x08)       /* Is timer A in Event Count mode? */
			MFP_TimerA_EventCount_Interrupt();

		if (nCbar_DmaSoundControl & 2)
		{
			nFrameCounter = 0;
			nFrameStartAddr = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | (IoMem[0xff8907] & ~1);
			nFrameEndAddr = (IoMem[0xff890f] << 16) | (IoMem[0xff8911] << 8) | (IoMem[0xff8913] & ~1);
			nFrameLen = nFrameEndAddr - nFrameStartAddr;
	
			if (nFrameLen <= 0)
			{
				Log_Printf(LOG_WARN, "crossbar DMA snd: Illegal buffer size (from 0x%x to 0x%x)\n",
					nFrameStartAddr, nFrameEndAddr);
				return -1;
			}
			Crossbar_StartDmaXmitHandler();
			return true;
		}
		else
		{
			nCbar_DmaSoundControl &= ~CROSSBAR_SNDCTRL_PLAY;
			return true;
		}
	}

	return false;
}

void Crossbar_StartDmaXmitHandler()
{
	Uint16 nCbSrc = IoMem_ReadWord(0xff8930);
	int freq = 1;

	/* Set an "interrupt" to get DMA value at correct time and send it to crossbar */
	if ((nCbSrc & 0x6) == 0x0)
	{
		/* Internal 25.175 MHz clock */
		freq = 25175000 / Crossbar_DetectSampleRate();
	}
	else if ((nCbSrc & 0x6) == 0x4)
	{
		/* Internal 32 MHz clock */
		freq = 32000000 / Crossbar_DetectSampleRate();
	}

	Int_AddRelativeInterrupt(CPU_FREQ/freq/256, INT_CPU_CYCLE, INTERRUPT_DMAXMIT);
}

void Crossbar_InterruptHandler_DmaXmit(void)
{
	Sint16 value;
	Sint8 *pFrameStart;

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	pFrameStart = (Sint8 *)&STRam[nFrameStartAddr];

	/* if 16 bits stereo mode */
	if (nDmaSoundMode & 0x40) {
		value = (Sint16) do_get_mem_word(&pFrameStart[nFrameCounter]);
		nFrameCounter ++;
	}
	/* 8 bits stereo */
	else if ((nDmaSoundMode & 0xc0) == 0) {
		value = (Sint16) pFrameStart[nFrameCounter];
		nFrameCounter ++;
	}
	/* 8 bits mono */
	else {
		value = (Sint16) pFrameStart[nFrameCounter/2];
		nFrameCounter ++; /*todo: 1/2 */
	}

	/* Send sample to DAC */
	Crossbar_SendDataToDAC(value*64);

	/* Restart the Int event handler */
	value = DmaSnd_CheckForEndOfFrame();
	if (value == false)
		Crossbar_StartDmaXmitHandler();
}


/*----------------------------------------------------------------------*/
/*			DAC processing 					*/
/*----------------------------------------------------------------------*/

/**
 * Put sample from crossbar into the DAC buffer.
 */
void Crossbar_SendDataToDAC(Sint16 value)
{
	/* Put sample into DAC buffer */
	/* Todo : verify if data is in the monitored track */
	DacOutBuffer[nDacOutWrPos] = value;
	nDacOutWrPos = (nDacOutWrPos + 1) % (MIXBUFFER_SIZE*2);
	nDacBufSamples += 1;
}

/**
 * Mix DAC sound sample with the normal PSG sound samples.
 * (Called by sound.c)
 */
void Crossbar_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate)
{
	double FreqRatio, fDacBufSamples, fDacBufRdPos;
	int i;
	int nBufIdx;

	FreqRatio = (double)Crossbar_DetectSampleRate() / (double)nAudioFrequency;
	FreqRatio *= 2.0;  /* Stereo */

	fDacBufSamples = (double)nDacBufSamples;
	fDacBufRdPos = (double)nDacOutRdPos;

	for (i = 0; (i < nSamplesToGenerate) && (fDacBufSamples > 0.0); i++)
	{
		nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
		nDacOutRdPos = (((int)fDacBufRdPos) & -2) % (MIXBUFFER_SIZE*2);

		MixBuffer[nBufIdx][0] = ((int)MixBuffer[nBufIdx][0]
		                        + (int)(DacOutBuffer[nDacOutRdPos+0])) / 2;
		MixBuffer[nBufIdx][1] = ((int)MixBuffer[nBufIdx][1]
		                        + (int)(DacOutBuffer[nDacOutRdPos+1])) / 2;

		fDacBufRdPos += FreqRatio;
		fDacBufSamples -= FreqRatio;
	}

	if (fDacBufSamples > 0.0)
		nDacBufSamples = (int)fDacBufSamples;
	else
		nDacBufSamples = 0;
}

