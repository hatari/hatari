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
		- DAC (headphone, loudspeaker and monitor sound)
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
#include "dsp.h"


#define DACBUFFER_SIZE  (MIXBUFFER_SIZE)

/* Crossbar internal functions */
static double Crossbar_DetectSampleRate(Uint16 clock);
static void Crossbar_Recalculate_Cycles(void);

/* Dma_Play sound functions */
static void Crossbar_setDmaPlay_Settings(void);
static void Crossbar_StartDmaPlay_Handler(void);
static Uint32 Crossbar_DmaPlay_CheckForEndOfFrame(void);

/* Dsp Xmit functions */
static void Crossbar_SendDataToDspReceive(Uint32 value, Uint16 frame);
static void Crossbar_StartDspXmitHandler(void);

/* DAC functions */
static void Crossbar_SendDataToDAC(Sint16 value, Uint16 sample_pos);

/* ADC functions */
static void Crossbar_StartAdcXmitHandler(void);

/* external data used by the MFP */
Uint16 nCbar_DmaSoundControl;

/* internal datas */

static const double Ste_SampleRates[4] =
{
	6258.0,
	12517.0,
	25033.0,
	50066.0
};

static const double Falcon_SampleRates_25Mhz[15] =
{
	49170.0,
	32780.0,
	24585.0,
	19668.0,
	16390.0,
	14049.0,
	12292.0,
	10927.0,
	 9834.0,
	 8940.0,
	 8195.0,
	 7565.0,
	 7024.0,
	 6556.0,
	 6146.0
};

static const double Falcon_SampleRates_32Mhz[15] =
{
	62500.0,
	41666.0,
	31250.0,
	25000.0,
	20833.0,
	17857.0,
	15624.0,
	13889.0,
	12500.0,
	11363.0,
	10416.0,
	9615.0,
	8928.0,
	8333.0,
	7812.0
};

struct dma_s {
	Uint32 frameStartAddr;		/* Sound frame start */
	Uint32 frameEndAddr;		/* Sound frame end */
	Uint32 frameCounter;		/* Counter in current sound frame */
	Uint32 frameLen;		/* Length of the frame */
	Uint32 isRunning;		/* Is Playing / Recording ? */
	Uint32 loopMode;		/* Loop mode enabled ? */
	Uint32 currentFrame;		/* Current Frame Played / Recorded (in stereo, 2 frames = 1 track) */
	Uint32 timerA_int;		/* Timer A interrupt at end of Play / Record ? */
	Uint32 mfp15_int;		/* MFP-15 interrupt at end of Play / Record ? */
	Uint32 isConnectedToCodec;
	Uint32 isConnectedToDsp;
	Uint32 isConnectedToDma;
};

struct crossbar_s {
	Uint32 dmaSelected;		/* 1 = DMA Record; 0 = DMA Play */
	Uint32 playTracks;		/* number of tracks played */
	Uint32 recordTracks;		/* number of tracks recorded */
	Uint16 track_monitored;		/* track monitored by the DAC */
	Uint32 is16Bits;		/* 0 = 8 bits; 1 = 16 bits */
	Uint32 isStereo;		/* 0 = mono; 1 = stereo */
	Uint32 steFreq;			/* from 0 (6258 Hz) to 3 (50066 Hz) */
	Uint32 int_freq_divider;	/* internal frequency divider */
	Uint32 isDacMuted;		/* 0 = DAC is running; 1 = DAC is muted */
	double dspXmit_cycles;		/* cycles for dsp Xmit interrupt */ 
	double dspXmit_cycles_counterD; /* cycles counter for dsp Xmit interrupt (double) */ 
	Uint32 dspXmit_cycles_counterL; /* cycles counter for dsp Xmit interrupt (long) */ 
	double dmaPlay_cycles;		/* cycles for dma play interrupt */
	double dmaPlay_cycles_counterD;	/* cycles counter for dma play interrupt (double) */
	Uint32 dmaPlay_cycles_counterL;	/* cycles counter for dma play interrupt (long) */
	double adc_cycles;		/* cycles for ADC interrupt */
	Uint32 adc_cycles_counter;	/* cycles counter for ADC interrupt */
};

struct codec_s {
	Sint16 buffer_left[DACBUFFER_SIZE];
	Sint16 buffer_right[DACBUFFER_SIZE];
	Uint32 readPosition;
	Uint32 writePosition;
	Uint32 writeBufferSize;
	Uint32 isConnectedToCodec;
	Uint32 isConnectedToDsp;
	Uint32 isConnectedToDma;
};

struct dsp_s {
	Uint32 isTristated;		/* 0 = DSP is not tristated; 1 = DSP is tristated */
	Uint32 isInHandshakeMode;	/* 0 = not in hanshake mode; 1 = in hanshake mode */
	Uint32 isConnectedToCodec;
	Uint32 isConnectedToDsp;
	Uint32 isConnectedToDma;
	Uint32 wordCount;		/* count number of words received from DSP transmitter (for TX frame computing) */
};

static struct crossbar_s crossbar;
static struct dma_s dmaPlay;
static struct dma_s dmaRecord;
static struct codec_s dac;
static struct codec_s adc;
static struct dsp_s dspXmit;
static struct dsp_s dspReceive;


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
	
	/* Stop DMA sound playing / record */
	IoMem_WriteByte(0xff8901,0);
	dmaPlay.isRunning = 0;
	dmaPlay.loopMode = 0;
	dmaPlay.currentFrame = 0;
	dmaRecord.isRunning = 0;
	dmaRecord.loopMode = 0;
	dmaRecord.currentFrame = 0;
	crossbar.dmaSelected = 0;

	/* Clear DAC buffer */
	memset(dac.buffer_left, 0, sizeof(dac.buffer_left));
	memset(dac.buffer_right, 0, sizeof(dac.buffer_right));
	dac.readPosition = 0;
	dac.writePosition = 0;
	dac.writeBufferSize = 0;

	/* ADC inits */
	microphone_ADC_is_started = 0;

	/* DSP inits */
	dspXmit.wordCount = 0;

	/* Crossbar inits */
	crossbar.track_monitored = 0;
	crossbar.dmaPlay_cycles_counterL = 0;
	crossbar.dmaPlay_cycles_counterD = 0.0;
	crossbar.dspXmit_cycles_counterL = 0;
	crossbar.dspXmit_cycles_counterD = 0.0;
}

/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void Crossbar_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&nCbar_DmaSoundControl, sizeof(nCbar_DmaSoundControl));
	MemorySnapShot_Store(&dmaPlay, sizeof(dmaPlay));
	MemorySnapShot_Store(&dmaRecord, sizeof(dmaRecord));
	MemorySnapShot_Store(&crossbar, sizeof(crossbar));
	MemorySnapShot_Store(&dac, sizeof(dac));
	MemorySnapShot_Store(&dspXmit, sizeof(dspXmit));
	MemorySnapShot_Store(&dspReceive, sizeof(dspReceive));
}


/*----------------------------------------------------------------------*/
/*	Hardware I/O functions						*/
/*----------------------------------------------------------------------*/

/**
 * Read byte from buffer interrupts (0xff8900).
 */
void Crossbar_BufferInter_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8900 (Sound DMA control) read: 0x%02x\n", IoMem_ReadByte(0xff8900));
}

/**
 * Write byte to buffer interrupts (0xff8900).
 */
void Crossbar_BufferInter_WriteWord(void)
{
	Uint8 dmaCtrl = IoMem_ReadByte(0xff8900);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8900 (Sound DMA control) write: 0x%02x\n", dmaCtrl);

	dmaPlay.timerA_int   = (dmaCtrl & 0x4) >> 2;
	dmaPlay.mfp15_int    = (dmaCtrl & 0x1);
	dmaRecord.timerA_int = (dmaCtrl & 0x8) >> 3;
	dmaRecord.mfp15_int  = (dmaCtrl & 0x2) >> 1;
}

/**
 * Read byte from DMA control register (0xff8901).
 */
void Crossbar_DmaCtrlReg_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8901 (additional Sound DMA control) read: 0x%02x\n", IoMem_ReadByte(0xff8901));
}

/**
 * Write byte from DMA control register (0xff8901).
 */
void Crossbar_DmaCtrlReg_WriteWord(void)
{
	Uint8 sndCtrl = IoMem_ReadByte(0xff8901);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8901 (additional Sound DMA control) write: 0x%02x\n", sndCtrl);

	crossbar.dmaSelected = (sndCtrl & 0x80) >> 7;

	/* DMA Play mode */
	if (crossbar.dmaSelected == 0) {
		if ((dmaPlay.isRunning == 0) && (sndCtrl & 1))
		{
			/* Turning on DMA Play sound emulation */
			nCbar_DmaSoundControl = sndCtrl & 3;
			dmaPlay.isRunning = 1;
			dmaPlay.loopMode = (sndCtrl & 0x2) >> 1;
			Crossbar_setDmaPlay_Settings();
			Crossbar_StartDmaPlay_Handler();
		}
		else if (dmaPlay.isRunning && ((sndCtrl & 1) == 0))
		{
			/* Turning off DMA play sound emulation */
			dmaPlay.isRunning = 0;
			dmaPlay.loopMode = 0;
			nCbar_DmaSoundControl = sndCtrl & 3;
		}
	}
	/* DMA Record mode */
	else {
		if ((dmaRecord.isRunning == 0) && (sndCtrl & 10))
		{
			/* Turning on DMA record sound emulation */
			dmaRecord.isRunning = 1;
			dmaRecord.loopMode = (sndCtrl & 0x20) >> 5;
	//		TODO : add DMA record mode
	//		Crossbar_setDmaPlay_Settings();
	//		Crossbar_StartDmaPlay_Handler();
		}
		else if (dmaRecord.isRunning && ((sndCtrl & 10) == 0))
		{
			/* Turning off DMA record sound emulation */
			dmaRecord.isRunning = 0;
			dmaRecord.loopMode = 0;
		}
	}
}



/**
 * Read word from sound frame count high register (0xff8909).
 */
void Crossbar_FrameCountHigh_ReadByte(void)
{
	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		IoMem_WriteByte(0xff8909, (dmaPlay.frameStartAddr + dmaPlay.frameCounter) >> 16);
	}
	else {
		/* DMA Record selected */
		IoMem_WriteByte(0xff8909, (dmaRecord.frameStartAddr + dmaRecord.frameCounter) >> 16);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound frame count medium register (0xff890b).
 */
void Crossbar_FrameCountMed_ReadByte(void)
{
	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		IoMem_WriteByte(0xff890b, (dmaPlay.frameStartAddr + dmaPlay.frameCounter) >> 8);
	}
	else {
		/* DMA Record selected */
		IoMem_WriteByte(0xff890b, (dmaRecord.frameStartAddr + dmaRecord.frameCounter) >> 8);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Read word from sound frame count low register (0xff890d).
 */
void Crossbar_FrameCountLow_ReadByte(void)
{
	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		IoMem_WriteByte(0xff890d, (dmaPlay.frameStartAddr + dmaPlay.frameCounter));
	}
	else {
		/* DMA Record selected */
		IoMem_WriteByte(0xff890d, (dmaRecord.frameStartAddr + dmaRecord.frameCounter));
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Read byte from DMA track control (0xff8920).
 */
void Crossbar_DmaTrckCtrl_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8920 (sound mode control) read: 0x%02x\n", IoMem_ReadByte(0xff8920));
}

/**
 * Write byte to DMA track control (0xff8920).
 */
void Crossbar_DmaTrckCtrl_WriteByte(void)
{
	Uint8 sndCtrl = IoMem_ReadByte(0xff8920);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8920 (sound mode control) write: 0x%02x\n", sndCtrl);

	crossbar.playTracks = (sndCtrl & 3) + 1;
	crossbar.track_monitored = (sndCtrl & 30) >> 4;
}

/**
 * Read word from sound mode register (0xff8921).
 */
void Crossbar_SoundModeCtrl_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "crossbar : $ff8921 (additional sound mode control) read: 0x%02x\n", IoMem[0xff8921]);
}

/**
 * Write word to sound mode register (0xff8921).
 */
void Crossbar_SoundModeCtrl_WriteByte(void)
{
	Uint8 sndCtrl = IoMem_ReadByte(0xff8921);

	LOG_TRACE(TRACE_CROSSBAR, "crossbar : $ff8921 (additional sound mode control) write: 0x%02x\n", sndCtrl);

	crossbar.is16Bits = (sndCtrl & 0x40) >> 6;
	crossbar.isStereo = 1 - ((sndCtrl & 0x80) >> 7);
	crossbar.steFreq = sndCtrl & 0x3;

	Crossbar_Recalculate_Cycles();
}

/**
 * Read word from Falcon Crossbar source controller (0xff8930).
 */
void Crossbar_SrcControler_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8930 (source device) read: 0x%04x\n", IoMem_ReadWord(0xff8930));
}

/**
 * Write word to Falcon Crossbar source controller (0xff8930).
	Source: A/D Convertor                 BIT 15 14 13 12
	00 - 25.175Mhz clock -------------------------+--+
	01 - External clock --------------------------+--+
	10 - 32Mhz clock (Don't use) -----------------+--'

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

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8930 (source device) write: 0x%04x\n", nCbSrc);

	dspXmit.isTristated = 1 - ((nCbSrc & 0x80) >> 7);
	dspXmit.isInHandshakeMode = 1 - ((nCbSrc & 0x10) >> 4);

	Crossbar_Recalculate_Cycles();

	/* Start DSP External input Interrupt */
	/* Todo : emulate the external port ? */

	/* Start DSP out Playback Interrupt */
	Crossbar_StartDspXmitHandler();
}


/**
 * Read word from Falcon Crossbar destination controller (0xff8932).
 */
void Crossbar_DstControler_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8932 (destination device) read: 0x%04x\n", IoMem_ReadWord(0xff8932));
}

/**
 * Write word to Falcon Crossbar destination controller (0xff8932).
	Source: D/A Convertor                 BIT 15 14 13 12
	00 - DMA output ------------------------------+--+  |
	01 - DSP output ------------------------------+--+  |
	10 - External input --------------------------+--+  |
	11 - ADC input -------------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'

	Source: External OutPut               BIT 11 10  9  8
	0 - DSP OUT, 1 - All others ---------------'  |  |  |
	00 - DMA output ------------------------------+--+  |
	01 - DSP output ------------------------------+--+  |
	10 - External input --------------------------+--+  |
	11 - ADC input -------------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'

	Source: DSP-RECEIVE                   BIT  7  6  5  4
	0 - Tristate and disconnect DSP -----------+  |  |  |
	    (Only for external SSI use)            |  |  |  |
	1 - Connect DSP to multiplexer ------------'  |  |  |
	00 - DMA output ------------------------------+--+  |
	01 - DSP output ------------------------------+--+  |
	10 - External input --------------------------+--+  |
	11 - ADC input -------------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'

	Source: DMA-RECORD                    BIT  3  2  1  0
	0 - Handshaking on, dest DSP-XMIT ---------+  |  |  |
	1 - All -----------------------------------'  |  |  |
	00 - DMA output ------------------------------+--+  |
	01 - DSP output ------------------------------+--+  |
	10 - External input --------------------------+--+  |
	11 - ADC input -------------------------------+--'  |
	0 - Handshake on, 1 - Handshake off ----------------'
 */
void Crossbar_DstControler_WriteWord(void)
{
	Uint16 destCtrl = IoMem_ReadWord(0xff8932);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8932 (destination device) write: 0x%04x\n", destCtrl);

	dspReceive.isTristated = 1 - ((destCtrl & 0x80) >> 7);
	dspReceive.isInHandshakeMode = 1 - ((destCtrl & 0x10) >> 4);

	/* destinations devices connexions */
	dspReceive.isConnectedToCodec = (destCtrl & 0x60) == 0x60 ? 1 : 0;
	dspReceive.isConnectedToDsp   = (destCtrl & 0x60) == 0x20 ? 1 : 0;
	dspReceive.isConnectedToDma   = (destCtrl & 0x60) == 0x00 ? 1 : 0;

	dmaRecord.isConnectedToCodec = (destCtrl & 0x6) == 0x6 ? 1 : 0;
	dmaRecord.isConnectedToDsp   = (destCtrl & 0x6) == 0x2 ? 1 : 0;
	dmaRecord.isConnectedToDma   = (destCtrl & 0x6) == 0x0 ? 1 : 0;

	dac.isConnectedToCodec = (destCtrl & 0x6000) == 0x6000 ? 1 : 0;
	dac.isConnectedToDsp   = (destCtrl & 0x6000) == 0x2000 ? 1 : 0;
	dac.isConnectedToDma   = (destCtrl & 0x6000) == 0x0000 ? 1 : 0;

	/* sources devices connexions */
	dspXmit.isConnectedToCodec = (destCtrl & 0x6000) == 0x2000 ? 1 : 0;
	dspXmit.isConnectedToDsp   = (destCtrl & 0x60) == 0x20 ? 1 : 0;
	dspXmit.isConnectedToDma   = (destCtrl & 0x6) == 0x2 ? 1 : 0;

	dmaPlay.isConnectedToCodec = (destCtrl & 0x6000) == 0x0000 ? 1 : 0;
	dmaPlay.isConnectedToDsp   = (destCtrl & 0x60) == 0x00 ? 1 : 0;
	dmaPlay.isConnectedToDma   = (destCtrl & 0x6) == 0x0 ? 1 : 0;

	adc.isConnectedToCodec = (destCtrl & 0x6000) == 0x6000 ? 1 : 0;
	adc.isConnectedToDsp   = (destCtrl & 0x60) == 0x60 ? 1 : 0;
	adc.isConnectedToDma   = (destCtrl & 0x6) == 0x6 ? 1 : 0;

	/* Start Microphone jack emulation */
	if (!microphone_ADC_is_started) { 
		microphone_ADC_is_started = 1;
#if HAVE_PORTAUDIO
		//Crossbar_StartAdcXmitHandler();
		//Microphone_Start((int)Crossbar_DetectSampleRate(25));
		//Microphone_Run();
#endif
	}
}

/**
 * Read byte from external clock divider register (0xff8934).
 */
void Crossbar_FreqDivExt_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8934 (ext. clock divider) read: 0x%02x\n", IoMem_ReadByte(0xff8934));
}

/**
 * Write byte to external clock divider register (0xff8934).
 */
void Crossbar_FreqDivExt_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8934 (ext. clock divider) write: 0x%02x\n", IoMem_ReadByte(0xff8934));
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void Crossbar_FreqDivInt_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8935 (int. clock divider) read: 0x%02x\n", IoMem_ReadByte(0xff8935));
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void Crossbar_FreqDivInt_WriteByte(void)
{
	Uint8 clkDiv = IoMem_ReadByte(0xff8935);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8935 (int. clock divider) write: 0x%02x\n", clkDiv);

	crossbar.int_freq_divider = clkDiv & 0xf;
	Crossbar_Recalculate_Cycles();
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
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8936 (record track select) read: 0x%02x\n", IoMem_ReadByte(0xff8936));
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
	Uint8 recTrack = IoMem_ReadByte(0xff8936);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8936 (record track select) write: 0x%02x\n", recTrack);

	crossbar.recordTracks = recTrack & 3;
}

/**
 * Read byte from CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D convertor 
 */
void Crossbar_CodecInput_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8937 (CODEC input) read: 0x%02x\n", IoMem_ReadByte(0xff8937));
}

/**
 * Write byte to CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D convertor 
 */
void Crossbar_CodecInput_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8937 (CODEC input) write: 0x%02x\n", IoMem_ReadByte(0xff8937));
}

/**
 * Read byte from A/D converter input for L+R channel (0xff8938).
 *	Bit 1 :  Left (0 = Microphone ; 1 = PSG soundchip)
 *	Bit 0 : Right (0 = Microphone ; 1 = PSG soundchip)
 */
void Crossbar_AdcInput_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8938 (ADC input) read: 0x%02x\n", IoMem_ReadByte(0xff8938));
}

/**
 * Write byte to A/D converter input for L+R channel (0xff8938).
 *	Bit 1 :  Left (0 = Microphone ; 1 = PSG soundchip)
 *	Bit 0 : Right (0 = Microphone ; 1 = PSG soundchip)
 */
void Crossbar_AdcInput_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8938 (ADC input) write: 0x%02x\n", IoMem_ReadByte(0xff8938));
}

/**
 * Read byte from input amplifier register (0xff8939).
 * 	Bits LLLLRRRR
 * 	Amplification is in +1.5 dB steps
 */
void Crossbar_InputAmp_ReadByte(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8939 (CODEC channel amplification) read: 0x%04x\n", IoMem_ReadWord(0xff8939));
}

/**
 * Write byte to input amplifier register (0xff8939).
 * 	Bits LLLLRRRR
 * 	Amplification is in +1.5 dB steps
 */
void Crossbar_InputAmp_WriteByte(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8939 (CODEC channel amplification) write: 0x%04x\n", IoMem_ReadWord(0xff8939));
}

/**
 * Read word from output reduction register (0xff893a).
 * 	Bits LLLLRRRR
 * 	Reduction is in -1.5 dB steps
 */
void Crossbar_OutputReduct_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff893a (CODEC channel attenuation) read: 0x%04x\n", IoMem_ReadWord(0xff893a));
}

/**
 * Write word to channel reduction register (0xff893a).
 * 	Bits LLLLRRRR
 * 	Reduction is in -1.5 dB steps
 */
void Crossbar_OutputReduct_WriteWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff893a (CODEC channel attenuation) write: 0x%04x\n", IoMem_ReadWord(0xff893a));
}

/**
 * Read word from CODEC status register (0xff893c).
 * 	Bit 1 :  Left Channel Overflow (0/1)
 * 	Bit 0 : Right Channel Overflow (0/1)
 */
void Crossbar_CodecStatus_ReadWord(void)
{
//	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff893c (CODEC status) read: 0x%04x\n", IoMem_ReadWord(0xff893c));
}

/**
 * Write word to CODEC status register (0xff893c).
 * 	Bit 1 :  Left Channel Overflow (0/1)
 * 	Bit 0 : Right Channel Overflow (0/1)
 */
void Crossbar_CodecStatus_WriteWord(void)
{
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff893c (CODEC status) write: 0x%04x\n", IoMem_ReadWord(0xff893c));
}



/*----------------------------------------------------------------------*/
/*--------------------- DMA PLAY sound processing ----------------------*/
/*----------------------------------------------------------------------*/

/**
 * Check if end-of-frame has been reached and raise interrupts if needed.
 * Returns true if DMA sound processing should be stopped now and false
 * if it continues.
 */
static Uint32 Crossbar_DmaPlay_CheckForEndOfFrame(void)
{
	if (dmaPlay.frameCounter >= dmaPlay.frameLen)
	{
		/* Send a MFP15_Int (I7) at end of replay buffer if enabled */
		if (dmaPlay.mfp15_int) {
			MFP_InputOnChannel(MFP_TIMER_GPIP7_BIT, MFP_IERA, &MFP_IPRA);
		}

		/* Send a TimerA_Int at end of replay buffer if enabled */
		if (dmaPlay.timerA_int) {
			if (MFP_TACR == 0x08)       /* Is timer A in Event Count mode? */
				MFP_TimerA_EventCount_Interrupt();
		}

		if (dmaPlay.loopMode)
		{
			Crossbar_setDmaPlay_Settings();
			Crossbar_StartDmaPlay_Handler();
		}
		else
		{
			nCbar_DmaSoundControl &= 0xfffe;
			dmaPlay.isRunning = 0;
		}
		return true;
	}

	return false;
}

/**
 * Set DMA sound start frame buffer, stop frame buffer, frame length
 */
static void Crossbar_setDmaPlay_Settings(void)
{
	/* DMA setings */
	dmaPlay.frameStartAddr = (IoMem[0xff8903] << 16) | (IoMem[0xff8905] << 8) | (IoMem[0xff8907] & ~1);
	dmaPlay.frameEndAddr = (IoMem[0xff890f] << 16) | (IoMem[0xff8911] << 8) | (IoMem[0xff8913] & ~1);
	dmaPlay.frameLen = dmaPlay.frameEndAddr - dmaPlay.frameStartAddr;
	dmaPlay.frameCounter = 0;

	if (dmaPlay.frameLen <= 0)
	{
		Log_Printf(LOG_WARN, "crossbar DMA snd: Illegal buffer size (from 0x%x to 0x%x)\n",
		          dmaPlay.frameStartAddr, dmaPlay.frameEndAddr);
	}
}

/**
 * start a DMA PLAY "interrupt" at frequency parametered in the crossbar
 */
static void Crossbar_StartDmaPlay_Handler(void)
{
	Uint32 dma_cycles;

	/* If DMA play is connected to the DSP_IN in handshake mode, */
	/* there's no need to use a clock. The synchro is driven by the DSP. */
	if (dmaPlay.isConnectedToDsp && (dspReceive.isTristated == 0) && dspReceive.isInHandshakeMode) {
		return;
	}

	if (dmaPlay.isRunning) {
		crossbar.dmaPlay_cycles_counterD += crossbar.dmaPlay_cycles;
		dma_cycles = (Uint32) crossbar.dmaPlay_cycles_counterD - crossbar.dmaPlay_cycles_counterL;
		crossbar.dmaPlay_cycles_counterL += dma_cycles;
		Int_AddRelativeInterrupt((Uint32)dma_cycles, INT_CPU_CYCLE, INTERRUPT_DMASOUND_XMIT_RECEIVE);
	}
}

/**
 * DMA sound xmit interrupt processing
 */
void Crossbar_InterruptHandler_DmaPlay(void)
{
	Sint16 value, eightBits, mono = 0;
	Sint8  *pFrameStart;

	/* If DMA play is connected to the DSP_IN in handshake mode, */
	/* there's no interrupt to acknoledge. */
	if (dspReceive.isConnectedToDma && (dspReceive.isTristated == 0) && dspReceive.isInHandshakeMode) {
		/* Do nothing */
	}
	else {
		/* Remove this interrupt from list and re-order */
		Int_AcknowledgeInterrupt();
	}

	pFrameStart = (Sint8 *)&STRam[dmaPlay.frameStartAddr];

	/* 16 bits stereo mode ? */
	if (crossbar.is16Bits) {
		eightBits = 1;
		value = (Sint16)do_get_mem_word(&pFrameStart[dmaPlay.frameCounter]);
		dmaPlay.frameCounter += 2;
	}
	/* 8 bits stereo ? */
	else if (crossbar.isStereo) {
		eightBits = 64;
		value = (Sint16) pFrameStart[dmaPlay.frameCounter];
		dmaPlay.frameCounter ++;
	}
	/* 8 bits mono */
	else {
		eightBits = 64;
		mono = 1;
		value = (Sint16) pFrameStart[dmaPlay.frameCounter];
		dmaPlay.frameCounter ++;
	}
	
	/* Send sample to the DMA record ? */
	if (dmaPlay.isConnectedToDma) {
		/* TODO : sent data to DMA record */
	}

	/* Send sample to the DAC ? */
	if (dmaPlay.isConnectedToCodec) {
		Crossbar_SendDataToDAC(value * eightBits, dmaPlay.currentFrame);
		if (mono == 1) {
			/* increase dmaPlay.currentFrame for next sample */
			dmaPlay.currentFrame ++;
			Crossbar_SendDataToDAC(value * eightBits, dmaPlay.currentFrame);
		}
	}

	/* Send sample to the DSP in (non handshake mode) ? */
	if (dmaPlay.isConnectedToDsp) {
		/* New frame ? */
		if (dmaPlay.currentFrame == 0) {
			Crossbar_SendDataToDspReceive(value, 1);
		}
		else {
			Crossbar_SendDataToDspReceive(value, 0);
		}
	}

	/* increase dmaPlay.currentFrame for next sample */
	dmaPlay.currentFrame ++;
	if (dmaPlay.currentFrame >= (crossbar.playTracks * 2)) {
		dmaPlay.currentFrame = 0;
	}

	/* Restart the Int event handler */
	value = Crossbar_DmaPlay_CheckForEndOfFrame();
	if (value == false)
		Crossbar_StartDmaPlay_Handler();
}


/*----------------------------------------------------------------------*/
/*------------------------- Crossbar functions -------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Recalculates DSP Xmit, ADC, external and DMA Play cycles (for interrupt handlers)
 */
static void Crossbar_Recalculate_Cycles(void)
{
	Uint16 outputs = IoMem[0xff8930];
	double cycles = 1.0;

	/* Calculate DSP Xmit cycles */
	/* ------------------------- */

	if ((outputs & 0x60) == 0x00)
	{
		/* Internal 25.175 MHz clock */
		cycles = (double)CPU_FREQ / Crossbar_DetectSampleRate(25);
	}
	else if ((outputs & 0x60) == 0x40)
	{
		/* Internal 32 MHz clock */
		cycles = (double)CPU_FREQ / Crossbar_DetectSampleRate(32);
	}
	crossbar.dspXmit_cycles = cycles / (double)(crossbar.playTracks) / 2.0;

	/* Calculate DMA Play cycles */
	/* ------------------------- */

	if ((outputs & 0x6) == 0x0)
	{
		/* Internal 25.175 MHz clock */
		cycles = (double)CPU_FREQ / Crossbar_DetectSampleRate(25);
	}
	else if ((outputs & 0x6) == 0x4)
	{
		/* Internal 32 MHz clock */
		cycles = (double)CPU_FREQ / Crossbar_DetectSampleRate(32);
	}

	/* if stereo mode */
	if (crossbar.isStereo) {
		cycles = cycles / 2.0;
	}
	crossbar.dmaPlay_cycles = cycles / (double)(crossbar.playTracks);


	/* Calculate ADC cycles (Internal 25.175 MHz clock only for ADC (Jack input)) */
	/* -------------------------------------------------------------------------- */

	cycles = (double)CPU_FREQ / Crossbar_DetectSampleRate(25);
	crossbar.adc_cycles = cycles / (double)(crossbar.playTracks) / 2.0;


	/* Verify if the new frequency doesn't mute the DAC */
	crossbar.isDacMuted = 0;
	if ((crossbar.int_freq_divider == 0) && (crossbar.steFreq == 0))
		crossbar.isDacMuted = 1;

	if ((crossbar.int_freq_divider == 6) || (crossbar.int_freq_divider == 8) || 
	    (crossbar.int_freq_divider == 10) || (crossbar.int_freq_divider >= 12)) {
		crossbar.isDacMuted = 1;
	}
}

/**
 * Detect sample rate frequency
 *    clock : value of the internal clock (25 or 32).
 */
static double Crossbar_DetectSampleRate(Uint16 clock)
{
	/* Ste compatible sound */
	if (crossbar.int_freq_divider == 0) {
		return Ste_SampleRates[crossbar.steFreq];
	}

	/* 25 Mhz internal clock */
	if (clock == 25) {
		return Falcon_SampleRates_25Mhz[crossbar.int_freq_divider - 1];
	}

	/* 32 Mhz internal clock */
	return Falcon_SampleRates_32Mhz[crossbar.int_freq_divider - 1];
}



/*----------------------------------------------------------------------*/
/*--------------------- DSP Xmit processing ----------------------------*/
/*----------------------------------------------------------------------*/

/**
 * start a DSP xmit "interrupt" at frequency parametered in the crossbar
 */
static void Crossbar_StartDspXmitHandler(void)
{
	Uint32 dsp_Cycles;

	/* If DSP is in tristate mode */
	if (dspXmit.isTristated) {
		return;
	}

	/* Compute cycles needed by the interrupt */
	crossbar.dspXmit_cycles_counterD += crossbar.dspXmit_cycles;
	dsp_Cycles = (Uint32) crossbar.dspXmit_cycles_counterD - crossbar.dspXmit_cycles_counterL;
	crossbar.dspXmit_cycles_counterL += dsp_Cycles;
	Int_AddRelativeInterrupt((Uint32)dsp_Cycles, INT_CPU_CYCLE, INTERRUPT_DSPXMIT);
}

/**
 * DSP xmit interrupt processing
 */
void Crossbar_InterruptHandler_DspXmit(void)
{
	Uint16 frame=0;
	Sint32 data;

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* TODO: implementing of handshake mode */

	if (dspXmit.wordCount == 0) {
		frame = 1;
	}

	/* Send the frame status to the DSP SSI Xmit */
	DSP_SsiReceive_SC2(frame);

	/* Send the clock to the DSP SSI Xmit */
	DSP_SsiReceive_SCK(0);

	/* read data from DSP Xmit */
	data = DSP_SsiReadTxValue();

 	/* Send DSP data to the DAC ? */
	if (dspXmit.isConnectedToCodec) {
		Crossbar_SendDataToDAC(data, dspXmit.wordCount);
	}

 	/* Send DSP data to the DMA record ? */
	if (dspXmit.isConnectedToDma) {
		/* TODO : sent data to DMA record */
	}

 	/* Send DSP data to the DSP in ? */
	if (dspXmit.isConnectedToDsp) {
		Crossbar_SendDataToDspReceive(data, frame);
	}

	/* increase dspXmit.wordCount for next sample */
	dspXmit.wordCount++;
	if (dspXmit.wordCount >= (crossbar.playTracks * 2)) {
		dspXmit.wordCount = 0;
	}

	/* Restart the Int event handler */
	Crossbar_StartDspXmitHandler();
}

/*----------------------------------------------------------------------*/
/*--------------------- DSP Receive processing -------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Transmit data from crossbar to DSP receive.
 */
static void Crossbar_SendDataToDspReceive(Uint32 value, Uint16 frame)
{
	/* Verify that DSP IN is not tristated */
	if (dspReceive.isTristated) {
		return;
	}

	/* Send sample to DSP receive */
	DSP_SsiWriteRxValue(value);

	/* Send the frame status to the DSP SSI receive */
	/* only in non hanshake mode */
	if (dspReceive.isInHandshakeMode == 0) {
		DSP_SsiReceive_SC1(frame);
	}

	/* Send the clock to the DSP SSI receive */
	DSP_SsiReceive_SC0(0);
}


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
	Int_AddRelativeInterrupt((int) crossbar.adc_cycles, INT_CPU_CYCLE, INTERRUPT_ADCXMIT);
}

/**
 * ADC xmit interrupt processing
 */
void Crossbar_InterruptHandler_ADCXmit(void)
{
	Sint16 sample = 0;

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* TODO: implementing of handshake mode and start frame */

	/* Send sample to DSP receive ? */
	if (adc.isConnectedToDsp) {
		Crossbar_SendDataToDspReceive(sample, 0);
	}
	
	/* Send sample to DMA record ? */
	if (adc.isConnectedToDma) {
		/* Todo : send data to DMA record */
	}

	/* Send sample to DAC ? */
	if (adc.isConnectedToCodec) {
		Crossbar_SendDataToDAC(sample, 0);
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
 *    - value : sample value to play
 *    - sample_pos : position of the sample in the track (used to play the monitored track)
 */
static void Crossbar_SendDataToDAC(Sint16 value, Uint16 sample_pos)
{
	Uint16 track = crossbar.track_monitored * 2;

	if (crossbar.isDacMuted == 1) {
		value = 0;
	}

	if (sample_pos == track) {
		/* Left channel */
		dac.buffer_left[dac.writePosition] = value;
	}
	else if (sample_pos == track + 1) {
		/* Right channel */
		dac.buffer_right[dac.writePosition] = value;
		dac.writePosition = (dac.writePosition + 1) % (DACBUFFER_SIZE);
		dac.writeBufferSize += 1;
	}
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

	FreqRatio = Crossbar_DetectSampleRate(25) / (double)nAudioFrequency;
	fDacBufSamples = (double)dac.writeBufferSize;
	fDacBufRdPos = (double)dac.readPosition;

	for (i = 0; (i < nSamplesToGenerate) && (fDacBufSamples >= 0.0); i++)
	{
		nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
		dac.readPosition = ((int)fDacBufRdPos) % DACBUFFER_SIZE;

		MixBuffer[nBufIdx][0] = ((int)MixBuffer[nBufIdx][0] + (int)dac.buffer_left[dac.readPosition]) / 2;
		MixBuffer[nBufIdx][1] = ((int)MixBuffer[nBufIdx][1] + (int)dac.buffer_right[dac.readPosition]) / 2;

		fDacBufRdPos += FreqRatio;
		fDacBufSamples -= FreqRatio;
	}

	dac.readPosition = ((int)fDacBufRdPos) % DACBUFFER_SIZE;

	if (fDacBufSamples > 0.0) {
		dac.writeBufferSize = (int)fDacBufSamples;
	}
	else {
		dac.writeBufferSize = 0;
	}
}
