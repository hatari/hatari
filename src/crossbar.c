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
#include "microphone.h"
#include "stMemory.h"
#include "falcon/dsp.h"


#define DACBUFFER_SIZE  (MIXBUFFER_SIZE*2*64)

/* Crossbar internal functions */
static double Crossbar_DetectSampleRate(void);

/* Dma sound functions */
static void Crossbar_setDmaSound_Settings(void);
static void Crossbar_StartDmaSound_Handler(void);
static Uint32 Crossbar_DmaSnd_CheckForEndOfFrame(void);

/* Dsp Xmit functions */
static void Crossbar_SendDataToDspReceive(Uint32 value);
static void Crossbar_StartDspXmitHandler(void);

/* DAC functions */
static void Crossbar_SendDataToDAC(Sint16 value);

/* ADC functions */
static void Crossbar_StartAdcXmitHandler(void);

/* external data used by the MFP */
Uint16 nCbar_DmaSoundControl;

/* internal datas */
static Sint16 DacOutBuffer[DACBUFFER_SIZE];
static int nDacOutRdPos, nDacOutWrPos, nDacBufSamples;

static Uint32 nFrameStartAddr;		/* Sound frame start */
static Uint32 nFrameEndAddr;		/* Sound frame end */
static Uint32 nFrameCounter;		/* Counter in current sound frame */
static Uint32 nFrameLen;		/* Length of the frame */

static Uint32 dspRx_wordCount;		/* count number of words sent to DSP receiver (for RX frame computing) */
static Uint32 dspTx_wordCount;		/* count number of words received from DSP transmitter (for TX frame computing) */

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
	
	/* Stop DMA sound playing/record */
	IoMem_WriteByte(0xff8901,0);

	/* Clear DAC buffer */
	memset(DacOutBuffer, 0, sizeof(DacOutBuffer));
	nDacOutRdPos = nDacOutWrPos = nDacBufSamples = 0;

	/* ADC inits */
	microphone_ADC_is_started = 0;

	/* DSP inits */
	dspRx_wordCount = 0;
	dspTx_wordCount = 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void Crossbar_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&nCbar_DmaSoundControl, sizeof(nCbar_DmaSoundControl));
	MemorySnapShot_Store(&nFrameStartAddr, sizeof(nFrameStartAddr));
	MemorySnapShot_Store(&nFrameEndAddr, sizeof(nFrameEndAddr));
	MemorySnapShot_Store(&nFrameCounter, sizeof(nFrameCounter));
	MemorySnapShot_Store(&nFrameLen, sizeof(nFrameLen));
	MemorySnapShot_Store(&DacOutBuffer, sizeof(DacOutBuffer));
	MemorySnapShot_Store(&nDacOutRdPos, sizeof(nDacOutRdPos));
	MemorySnapShot_Store(&nDacOutWrPos, sizeof(nDacOutWrPos));
	MemorySnapShot_Store(&dspRx_wordCount, sizeof(dspRx_wordCount));
	MemorySnapShot_Store(&dspTx_wordCount, sizeof(dspTx_wordCount));
}


/*----------------------------------------------------------------------*/
/*	Hardware I/O functions						*/
/*----------------------------------------------------------------------*/

/**
 * Read byte from buffer interrupts (0xff8900).
 */
void Crossbar_BufferInter_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8900 DMA track control register read: 0x%02x\n", IoMem_ReadByte(0xff8900));
}

/**
 * Write byte to buffer interrupts (0xff8900).
 */
void Crossbar_BufferInter_WriteWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8900 DMA track control register write: 0x%02x\n", IoMem_ReadByte(0xff8900));
}

/**
 * Read byte from DMA control register (0xff8901).
 */
void Crossbar_DmaCtrlReg_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8901 DMA control register read: 0x%02x\n", IoMem_ReadByte(0xff8901));
}

/**
 * Write byte from DMA control register (0xff8901).
 */
void Crossbar_DmaCtrlReg_WriteWord(void)
{
	Uint16 nNewSndCtrl;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8901 DMA control register write: 0x%02x\n", IoMem_ReadByte(0xff8901));

	nNewSndCtrl = IoMem_ReadByte(0xff8901) & 3;

	if ((!(nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_PLAY)   && (nNewSndCtrl & CROSSBAR_SNDCTRL_PLAY)) ||
	    (!(nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_RECORD) && (nNewSndCtrl & CROSSBAR_SNDCTRL_RECORD)))
	{
		/* Turning on DMA sound emulation */
		nCbar_DmaSoundControl = nNewSndCtrl;
		Crossbar_setDmaSound_Settings();
		//Crossbar_StartDmaSound_Handler();
	}
	else if (((nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_PLAY)   && !(nNewSndCtrl & CROSSBAR_SNDCTRL_PLAY)) ||
	         ((nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_RECORD) && !(nNewSndCtrl & CROSSBAR_SNDCTRL_RECORD)))
	{
		nCbar_DmaSoundControl = nNewSndCtrl;
		fprintf(stderr, "Turning off DMA sound emulation.\n");
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
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8920 DMA track control register read: 0x%02x\n", IoMem_ReadByte(0xff8920));
}

/**
 * Write byte to DMA track control (0xff8920).
 */
void Crossbar_DmaTrckCtrl_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8920 DMA track control register write: 0x%02x\n", IoMem_ReadByte(0xff8920));
}

/**
 * Read word from sound mode register (0xff8921).
 */
void Crossbar_SoundModeCtrl_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "crossbar : $ff8921 snd mode read: 0x%02x\n", IoMem[0xff8921]);
}


/*-----------------------------------------------------------------------*/
/**
 * Write word to sound mode register (0xff8921).
 */
void Crossbar_SoundModeCtrl_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "crossbar : $ff8921 snd mode write: 0x%02x\n", IoMem[0xff8921]);
}


/**
 * Read word from Falcon Crossbar source controller (0xff8930).
 */
void Crossbar_SrcControler_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8930 Crossbar src read: 0x%04x\n", IoMem_ReadWord(0xff8930));
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
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8930 src write: 0x%04x\n", nCbSrc);

	/* Start DSP External input Interrupt */
	/* Todo : emulate the external port ? */

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
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8932 dst read: 0x%04x\n", IoMem_ReadWord(0xff8932));
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

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8932 dst write: 0x%04x\n", destCtrl);

	/* Start Microphone jack emulation */
	if (!microphone_ADC_is_started) { 
		microphone_ADC_is_started = 1;
#if HAVE_PORTAUDIO
		//Crossbar_StartAdcXmitHandler();
		//Microphone_Start((int)Crossbar_DetectSampleRate());
		//Microphone_Run();
#endif
	}
}

/**
 * Read byte from external clock divider register (0xff8934).
 */
void Crossbar_FreqDivExt_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8934 ext. clock divider read: 0x%02x\n", IoMem_ReadByte(0xff8934));
}

/**
 * Write byte to external clock divider register (0xff8934).
 */
void Crossbar_FreqDivExt_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8934 ext. clock divider write: 0x%02x\n", IoMem_ReadByte(0xff8934));
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void Crossbar_FreqDivInt_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8935 int. clock divider read: 0x%02x\n", IoMem_ReadByte(0xff8935));
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void Crossbar_FreqDivInt_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8935 int. clock divider write: 0x%02x\n", IoMem_ReadByte(0xff8935));
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
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8936 record track select read: 0x%02x\n", IoMem_ReadByte(0xff8936));
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
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8936 record track select write: 0x%02x\n", IoMem_ReadByte(0xff8936));
}

/**
 * Read byte from CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D convertor 
 */
void Crossbar_CodecInput_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8937 CODEC input read: 0x%02x\n", IoMem_ReadByte(0xff8937));
}

/**
 * Write byte to CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D convertor 
 */
void Crossbar_CodecInput_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8937 CODEC input write: 0x%02x\n", IoMem_ReadByte(0xff8937));
}

/**
 * Read byte from A/D converter input for L+R channel (0xff8938).
 *	Bit 1 :  Left (0 = Microphone ; 1 = PSG soundchip)
 *	Bit 0 : Right (0 = Microphone ; 1 = PSG soundchip)
 */
void Crossbar_AdcInput_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8938 ADC input read: 0x%02x\n", IoMem_ReadByte(0xff8938));
}

/**
 * Write byte to A/D converter input for L+R channel (0xff8938).
 *	Bit 1 :  Left (0 = Microphone ; 1 = PSG soundchip)
 *	Bit 0 : Right (0 = Microphone ; 1 = PSG soundchip)
 */
void Crossbar_AdcInput_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8938 ADC input write: 0x%02x\n", IoMem_ReadByte(0xff8938));
}

/**
 * Read byte from input amplifier register (0xff8939).
 * 	Bits LLLLRRRR
 * 	Amplification is in +1.5 dB steps
 */
void Crossbar_InputAmp_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8939 CODEC channel amplification read: 0x%04x\n", IoMem_ReadWord(0xff8939));
}

/**
 * Write byte to input amplifier register (0xff8939).
 * 	Bits LLLLRRRR
 * 	Amplification is in +1.5 dB steps
 */
void Crossbar_InputAmp_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8939 CODEC channel amplification write: 0x%04x\n", IoMem_ReadWord(0xff8939));
}

/**
 * Read word from output reduction register (0xff893a).
 * 	Bits LLLLRRRR
 * 	Reduction is in -1.5 dB steps
 */
void Crossbar_OutputReduct_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff893a CODEC channel attenuation read: 0x%04x\n", IoMem_ReadWord(0xff893a));
}

/**
 * Write word to channel reduction register (0xff893a).
 * 	Bits LLLLRRRR
 * 	Reduction is in -1.5 dB steps
 */
void Crossbar_OutputReduct_WriteWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff893a CODEC channel attenuation write: 0x%04x\n", IoMem_ReadWord(0xff893a));
}

/**
 * Read word from CODEC status register (0xff893c).
 * 	Bit 1 :  Left Channel Overflow (0/1)
 * 	Bit 0 : Right Channel Overflow (0/1)
 */
void Crossbar_CodecStatus_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff893c CODEC status read: 0x%04x\n", IoMem_ReadWord(0xff893c));
}

/**
 * Write word to CODEC status register (0xff893c).
 * 	Bit 1 :  Left Channel Overflow (0/1)
 * 	Bit 0 : Right Channel Overflow (0/1)
 */
void Crossbar_CodecStatus_WriteWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff893c CODEC status write: 0x%04x\n", IoMem_ReadWord(0xff893c));
}



/*----------------------------------------------------------------------*/
/*--------------------- DMA sound processing ---------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Check if end-of-frame has been reached and raise interrupts if needed.
 * Returns true if DMA sound processing should be stopped now and false
 * if it continues.
 */
Uint32 Crossbar_DmaSnd_CheckForEndOfFrame()
{
	if (nFrameCounter >= nFrameLen)
	{
		/* Raise end-of-frame interrupts (MFP-i7 and Time-A) */

		/* if MFP15_Int (I7) at end of replay/record buffer enabled */
		if (IoMem[0xff8900] & 3) {
			MFP_InputOnChannel(MFP_TIMER_GPIP7_BIT, MFP_IERA, &MFP_IPRA);
		}

		/* if TimerA_Int at end of replay/record buffer enabled */
		if (IoMem[0xff8900] & 0xc) {
			if (MFP_TACR == 0x08)       /* Is timer A in Event Count mode? */
				MFP_TimerA_EventCount_Interrupt();
		}

		if ((nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_PLAYLOOP) ||
		    (nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_RECORDLOOP))
		{
			Crossbar_setDmaSound_Settings();
			Crossbar_StartDmaSound_Handler();
			return true;
		}
		else
		{
			nCbar_DmaSoundControl &= ~CROSSBAR_SNDCTRL_PLAY;
			nCbar_DmaSoundControl &= ~CROSSBAR_SNDCTRL_RECORD;
			return true;
		}
	}

	return false;
}

/**
 * Set DMA sound start frame buffer, stop frame buffer, frame length
 */
void Crossbar_setDmaSound_Settings()
{
	/* DMA setings */
	nFrameStartAddr = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | (IoMem[0xff8907] & ~1);
	nFrameEndAddr = (IoMem[0xff890f] << 16) | (IoMem[0xff8911] << 8) | (IoMem[0xff8913] & ~1);
	nFrameLen = nFrameEndAddr - nFrameStartAddr;
	nFrameCounter = 0;
	
	if (nFrameLen <= 0)
	{
		Log_Printf(LOG_WARN, "crossbar DMA snd: Illegal buffer size (from 0x%x to 0x%x)\n",
		          nFrameStartAddr, nFrameEndAddr);
	}
}

/**
 * start a DMA sound xmit or receive "interrupt" at frequency parametered in the crossbar
 */
static void Crossbar_StartDmaSound_Handler()
{
	Uint16 nCbSrc = IoMem_ReadWord(0xff8930);
	int freq = 1;

	if ((nCbSrc & 0x6) == 0x00)
	{
		/* Internal 25.175 MHz clock */
		freq = 25175000 / Crossbar_DetectSampleRate();
	}
	else if ((nCbSrc & 0x6) == 0x40)
	{
		/* Internal 32 MHz clock */
		freq = 32000000 / Crossbar_DetectSampleRate();
	}

	Int_AddRelativeInterrupt(CPU_FREQ/freq/256, INT_CPU_CYCLE, INTERRUPT_DMASOUND_XMIT_RECEIVE);
}

/**
 * DMA sound xmit/receive interrupt processing
 */
void Crossbar_InterruptHandler_DmaSound(void)
{
	Sint16 value;
	Sint8 *pFrameStart;
	Uint32 nDmaSoundMode = IoMem[0xff8921];

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* process DMA sound replay or record */
	if ((IoMem[0xff8901] & 0x80) == 0) {
		/* DMA sound is in Replay mode */
		pFrameStart = (Sint8 *)&STRam[nFrameStartAddr];

		/* if 16 bits stereo mode */
		if (nDmaSoundMode & 0x40) {
			value = (Sint16) do_get_mem_word(&pFrameStart[nFrameCounter]);
			nFrameCounter += 2;
		}
		/* 8 bits stereo */
		else if ((nDmaSoundMode & 0xc0) == 0) {
			value = (Sint16) pFrameStart[nFrameCounter];
			nFrameCounter ++;
		}
		/* 8 bits mono */
		else {
			value = (Sint16) pFrameStart[nFrameCounter];
			nFrameCounter ++;
			/* Send sample to DAC (in mono mode, data is sent twice (for left and right channel) */
			Crossbar_SendDataToDAC(value*64);
		}
		
		/* Send sample to DAC */
		/* Todo : It should be sent to DMA OUT in the crossbar */
		Crossbar_SendDataToDAC(value*64);

	} 
	else {
		/* DMA sound is in Record mode */
		/* Todo : get value from DMA IN in the crossbar and write it into memory */
	}

	/* Restart the Int event handler */
	value = Crossbar_DmaSnd_CheckForEndOfFrame();
	if (value == false)
		Crossbar_StartDmaSound_Handler();
}


/*----------------------------------------------------------------------*/
/*------------------------- Crossbar functions -------------------------*/
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
/*--------------------- DSP Xmit processing ----------------------------*/
/*----------------------------------------------------------------------*/

/**
 * start a DSP xmit "interrupt" at frequency parametered in the crossbar
 */
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

/**
 * DSP xmit interrupt processing
 */
void Crossbar_InterruptHandler_DspXmit(void)
{
	Uint16 frame=0;
	Sint32 data;
	Uint16 tracks;

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* TODO: implementing of handshake mode */

	tracks = ((IoMem[0xff8920] & 3) + 1) * 2;
//	tracks = ((IoMem[0xff8936] & 3) + 1) * 2;
	if (dspTx_wordCount >= tracks) {
		frame = 1;
		dspTx_wordCount = 0;
	}

	dspTx_wordCount++;

	/* read data from DSP Xmit */
	data = DSP_SsiReadTxValue();

 	/* Send DSP data to the DAC */
	/* TODO : vérify that the DSP is connected to the DAC */
	Crossbar_SendDataToDAC(data);

	/* Send the frame status to the DSP SSI Xmit */
	DSP_SsiReceive_SC2(frame);

	/* Send the clock to the DSP SSI Xmit */
	DSP_SsiReceive_SCK(0);

	/* Restart the Int event handler */
	Crossbar_StartDspXmitHandler();
}

/*----------------------------------------------------------------------*/
/*--------------------- DSP Receive processing -------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Transmit data from crossbar to DSP receive.
 */
static void Crossbar_SendDataToDspReceive(Uint32 value)
{
	Uint16 frame=0;
	Uint16 tracks;

	/* TODO: implementing of handshake mode */

//	tracks = ((IoMem[0xff8920] & 3) + 1) * 2;
	tracks = ((IoMem[0xff8936] & 3) + 1) * 2;
	if (dspRx_wordCount >= tracks) {
		frame = 1;
		dspRx_wordCount = 0;
	}

	dspRx_wordCount++;

	/* Send sample to DSP receive */
	DSP_SsiWriteRxValue(value);

	/* Send the frame status to the DSP SSI receive */
	DSP_SsiReceive_SC1(frame);

	/* Send the clock to the DSP SSI receive */
	DSP_SsiReceive_SC0(0);
}


/*----------------------------------------------------------------------*/
/*--------------------- DMA IN processing ------------------------------*/
/*----------------------------------------------------------------------*/


/*----------------------------------------------------------------------*/
/*--------------------- DMA OUT processing -----------------------------*/
/*----------------------------------------------------------------------*/


/*----------------------------------------------------------------------*/
/*-------------------------- ADC processing ----------------------------*/
/*----------------------------------------------------------------------*/

/**
 * start an ADC xmit "interrupt" at frequency parametered in the crossbar
 */
static void Crossbar_StartAdcXmitHandler(void)
{
	int freq = 1;

	/* Internal 25.175 MHz clock only for ADC (Jack) */
	freq = 25175000 / Crossbar_DetectSampleRate();

	Int_AddRelativeInterrupt(CPU_FREQ/freq/256, INT_CPU_CYCLE, INTERRUPT_ADCXMIT);
}

/**
 * ADC xmit interrupt processing
 */
void Crossbar_InterruptHandler_ADCXmit(void)
{
	Uint16 nCbDst = IoMem_ReadWord(0xff8932);
	Sint16 sample = 0;
	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* TODO: implementing of handshake mode and start frame */

	/* Send sample to DSP receive */
	if ( (nCbDst & 0x60) == 0x60) {
		Crossbar_SendDataToDspReceive(sample);
	}
	
	/* Send sample to DAC */
	if ( (nCbDst & 0x6) == 0x6) {
		/* Todo : send data to DMA record */
	}

	/* Send sample to DMA record */
	if ( (nCbDst & 0x1800) == 0x1800) {
		Crossbar_SendDataToDAC(sample);
	}
	
	/* Nothing for external port for now */

	/* Restart the Int event handler */
	Crossbar_StartAdcXmitHandler();
}


/*----------------------------------------------------------------------*/
/*-------------------------- DAC processing ----------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Put sample from crossbar into the DAC buffer.
 */
static void Crossbar_SendDataToDAC(Sint16 value)
{
	/* Put sample into DAC buffer */
	/* Todo : verify if data is in the monitored track */
	DacOutBuffer[nDacOutWrPos] = value;
	nDacOutWrPos = (nDacOutWrPos + 1) % (DACBUFFER_SIZE);
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

	for (i = 0; (i < nSamplesToGenerate) && (fDacBufSamples >= 0.0); i++)
	{
		nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
		nDacOutRdPos = (((int)fDacBufRdPos) & -2) % (DACBUFFER_SIZE);

		MixBuffer[nBufIdx][0] = ((int)MixBuffer[nBufIdx][0]
		                        + (int)(DacOutBuffer[nDacOutRdPos+0])) / 2;
		MixBuffer[nBufIdx][1] = ((int)MixBuffer[nBufIdx][1]
		                        + (int)(DacOutBuffer[nDacOutRdPos+1])) / 2;

		fDacBufRdPos += FreqRatio;
		fDacBufSamples -= FreqRatio;
	}

	nDacOutRdPos = (((int)fDacBufRdPos) & -2) % (DACBUFFER_SIZE);

	if (fDacBufSamples > 0.0) {
		nDacBufSamples = (int)fDacBufSamples;
	}
	else {
		nDacBufSamples = 0;
	}
}

