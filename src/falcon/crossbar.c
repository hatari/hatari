/*
  Hatari - Crossbar.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Falcon Crossbar (Matrice) emulation.
  input device:
		- DSP transmit (SSI)
		- external DSP connector
		- ADC (micro + PSG chip)
		- DMA playback

  output device:
		- external DSP connector
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
    $FF893A (word) : Attenuation Settings Per Channel
    $FF893C (word) : Codec Status
    $FF8940 (word) : GPIO Data Direction
    $FF8942 (word) : GPIO Data


    Crossbar schematics:

       - one receiving device can be connected to only one source device
       - one source device can be connected to multiple receiving device

                           Source devices
                                               CROSSBAR
                             EXT INPUT ---O------O------O-----O
                              CHANNEL     |      |      |     |
                                          |      |      |     |
                                 DSP   ---O------O------O-----O
                              TRANSMIT    |      |      |     |
                                          |      |      |     |
   Mic L -----|                  DMA   ---O------O------O-----O
          /---|XOR ----|\     PLAYBACK    |      |      |     |
   PSG --|             | \                |      |      |     |
          \---|        | /-------X--------O------O------O-----O
   Mic R -----|XOR ----|/        |        |      |      |     |
                       ADC       |        |      DMA    DSP   EXT OUTPUT      Receiving Devices
                                 |        |    RECEIVE CHANNEL
                                 |        |
                              -----------------
                               \      +      /
                                \-----------/
                                      |
                                      |
                                    -----
                                    \   / DAC
                                     \ /
                                      |
                                      |
                                Output to:
                                   - header,
                                   - internal speaker,
                                   - monitor speaker
*/

const char crossbar_fileid[] = "Hatari Crossbar.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "cycInt.h"
#include "m68000.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "sound.h"
#include "crossbar.h"
#include "microphone.h"
#include "stMemory.h"
#include "dsp.h"
#include "clocks_timings.h"



#define DACBUFFER_SIZE    2048
#define DECIMAL_PRECISION 65536


/* Crossbar internal functions */
static int  Crossbar_DetectSampleRate(Uint16 clock);
static void Crossbar_Start_InterruptHandler_25Mhz(void);
static void Crossbar_Start_InterruptHandler_32Mhz(void);

/* Dma_Play sound functions */
static void Crossbar_setDmaPlay_Settings(void);
static void Crossbar_Process_DMAPlay_Transfer(void);

/* Dma_Record sound functions */
static void Crossbar_setDmaRecord_Settings(void);
void Crossbar_SendDataToDmaRecord(Sint16 value);
static void Crossbar_Process_DMARecord_HandshakeMode(void);

/* Dsp Xmit functions */
static void Crossbar_SendDataToDspReceive(Uint32 value, Uint16 frame);
static void Crossbar_Process_DSPXmit_Transfer(void);

/* DAC functions */
static void Crossbar_SendDataToDAC(Sint16 value, Uint16 sample_pos);

/* ADC functions */
static void Crossbar_Process_ADCXmit_Transfer(void);

/* external data used by the MFP */
Uint16 nCbar_DmaSoundControl;

/* internal datas */

/* dB = 20log(gain)  :  gain = antilog(dB/20)                                  */
/* Table gain values = (int)(powf(10.0, dB/20.0)*65536.0 + 0.5)  1.5dB steps   */

/* Values for Codec's ADC volume control (* DECIMAL_PRECISION) */
/* PSG must be amplified by 2.66.. before mixing with crossbar */
/* The ADC table values are multiplied by 2'2/3 and divided    */
/* by 4 (later multplied by 4) eg 43691 = 65536 * 2.66.. / 4.0 */
static const Uint16 Crossbar_ADC_volume_table[16] =
{
	3276,   3894,   4628,   5500,   6537,   7769,   9234,   10975,
	13043,  15502,  18424,  21897,  26025,  30931,  36761,  43691
};

/* Values for Codec's DAC volume control (* DECIMAL_PRECISION) */
static const Uint16 Crossbar_DAC_volume_table[16] =
{
	65535,  55142,  46396,  39037,  32846,  27636,  23253,  19565,
	16462,  13851,  11654,  9806,   8250,   6942,   5841,   4915
};

static const int Ste_SampleRates[4] =
{
	6258,
	12517,
	25033,
	50066
};

static const int Falcon_SampleRates_25Mhz[15] =
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
	 6146
};

static const int Falcon_SampleRates_32Mhz[15] =
{
	62500,
	41666,
	31250,
	25000,
	20833,
	17857,
	15624,
	13889,
	12500,
	11363,
	10416,
	9615,
	8928,
	8333,
	7812
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
	Uint32 isConnectedToDspInHandShakeMode;
	Uint32 isConnectedToDma;
	Uint32 handshakeMode_Frame;	/* state of the frame in handshake mode */
};

struct crossbar_s {
	Uint32 dmaSelected;		/* 1 = DMA Record; 0 = DMA Play */
	Uint32 playTracks;		/* number of tracks played */
	Uint32 recordTracks;		/* number of tracks recorded */
	Uint16 track_monitored;		/* track monitored by the DAC */
	Uint32 is16Bits;		/* 0 = 8 bits; 1 = 16 bits */
	Uint32 isStereo;		/* 0 = mono; 1 = stereo */
	Uint32 steFreq;			/* from 0 (6258 Hz) to 3 (50066 Hz) */
	Uint32 isInSteFreqMode;		/* 0 = Falcon frequency mode ; 1 = Ste frequency mode */
	Uint32 int_freq_divider;	/* internal frequency divider */
	Uint32 isDacMuted;		/* 0 = DAC is running; 1 = DAC is muted */
	Uint32 dspXmit_freq;		/* 0 = 25 Mhz ; 1 = external clock ; 2 = 32 Mhz */
	Uint32 dmaPlay_freq;		/* 0 = 25 Mhz ; 1 = external clock ; 2 = 32 Mhz */
	Uint16 codecInputSource;	/* codec input source */
	Uint16 codecAdcInput;		/* codec ADC input */
	Uint16 gainSettingLeft;		/* Left channel gain for ADC */
	Uint16 gainSettingRight;	/* Right channel gain for ADC */
	Uint16 attenuationSettingLeft;	/* Left channel attenuation for DAC */
	Uint16 attenuationSettingRight;	/* Right channel attenuation for DAC */
	Uint16 microphone_ADC_is_started;

	Uint32 clock25_cycles;		/* cycles for 25 Mzh interrupt */
	Uint32 clock25_cycles_decimal;  /* decimal part of cycles counter for 25 Mzh interrupt (*DECIMAL_PRECISION) */
	Uint32 clock25_cycles_counter;  /* Cycle counter for 25 Mhz interrupts */
	Uint32 pendingCyclesOver25;	/* Number of delayed cycles for the interrupt */
	Uint32 clock32_cycles;		/* cycles for 32 Mzh interrupt */
	Uint32 clock32_cycles_decimal;  /* decimal part of cycles counter for 32 Mzh interrupt (*DECIMAL_PRECISION) */
	Uint32 clock32_cycles_counter;  /* Cycle counter for 32 Mhz interrupts */
	Uint32 pendingCyclesOver32;	/* Number of delayed cycles for the interrupt */
	Sint64 frequence_ratio;		/* Ratio between host computer's sound frequency and hatari's sound frequency */
	Sint64 frequence_ratio2;	/* Ratio between hatari's sound frequency and host computer's sound frequency */

	Uint32 dmaPlay_CurrentFrameStart;   /* current DmaPlay Frame start ($ff8903 $ff8905 $ff8907) */
	Uint32 dmaPlay_CurrentFrameCount;   /* current DmaRecord Frame start ($ff8903 $ff8905 $ff8907) */
	Uint32 dmaPlay_CurrentFrameEnd;     /* current DmaRecord Frame start ($ff8903 $ff8905 $ff8907) */
	Uint32 dmaRecord_CurrentFrameStart; /* current DmaPlay Frame end ($ff890f $ff8911 $ff8913) */
	Uint32 dmaRecord_CurrentFrameCount; /* current DmaRecord Frame start ($ff8903 $ff8905 $ff8907) */
	Uint32 dmaRecord_CurrentFrameEnd;   /* current DmaRecord Frame end ($ff890f $ff8911 $ff8913) */
	Uint32 adc2dac_readBufferPosition;  /* read position for direct adc->dac transfer */
	Sint64 adc2dac_readBufferPosition_float; /* float value of read position for direct adc->dac transfer index */

	Uint32 save_special_transfer;		/* Used in a special undocumented transfer mode (dsp sent is not in handshake mode and dsp receive is in handshake mode) */
};

struct codec_s {
	Sint16 buffer_left[DACBUFFER_SIZE];
	Sint16 buffer_right[DACBUFFER_SIZE];
	Sint64 readPosition_float;
	Uint32 readPosition;
	Uint32 writePosition;
	Uint32 isConnectedToCodec;
	Uint32 isConnectedToDsp;
	Uint32 isConnectedToDma;
	Uint32 wordCount;
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

/**
 * Reset Crossbar variables.
 */
void Crossbar_Reset(bool bCold)
{
	nCbar_DmaSoundControl = 0;

	/* Stop DMA sound playing / record */
	IoMem_WriteByte(0xff8901,0);
	dmaPlay.isRunning = 0;
	dmaPlay.loopMode = 0;
	dmaPlay.currentFrame = 0;
	dmaPlay.isConnectedToDspInHandShakeMode = 0;
	dmaPlay.handshakeMode_Frame = 0;
	dmaRecord.isRunning = 0;
	dmaRecord.loopMode = 0;
	dmaRecord.currentFrame = 0;
	dmaRecord.isConnectedToDspInHandShakeMode = 0;
	dmaRecord.handshakeMode_Frame = 0;

	/* DAC inits */
	memset(dac.buffer_left, 0, sizeof(dac.buffer_left));
	memset(dac.buffer_right, 0, sizeof(dac.buffer_right));
	dac.readPosition_float = 0;
	dac.readPosition = 0;
	dac.writePosition = 0;

	/* ADC inits */
	memset(adc.buffer_left, 0, sizeof(adc.buffer_left));
	memset(adc.buffer_right, 0, sizeof(adc.buffer_right));
	adc.readPosition_float = 0;
	adc.readPosition = 0;
	adc.writePosition = 0;

	/* DSP inits */
	dspXmit.wordCount = 0;

	/* Crossbar inits */
	crossbar.clock25_cycles = 160;
	crossbar.clock25_cycles_decimal = 0;
	crossbar.clock25_cycles_counter = 0;
	crossbar.pendingCyclesOver25 = 0;
	crossbar.clock32_cycles = 160;
	crossbar.clock32_cycles_decimal = 0;
	crossbar.clock32_cycles_counter = 0;
	crossbar.pendingCyclesOver32 = 0;
	crossbar.frequence_ratio = 0;
	crossbar.frequence_ratio2 = 0;

	crossbar.dmaSelected = 0;
	crossbar.track_monitored = 0;
	crossbar.isInSteFreqMode = 1;
	crossbar.int_freq_divider = 0;
	crossbar.steFreq = 3;
	crossbar.playTracks = 1;
	crossbar.is16Bits = 0;
	crossbar.isStereo = 1;
	crossbar.codecInputSource = 3;
	crossbar.codecAdcInput = 3;
	crossbar.gainSettingLeft = 3276;
	crossbar.gainSettingRight = 3276;
	crossbar.attenuationSettingLeft = 65535;
	crossbar.attenuationSettingRight = 65535;
	crossbar.adc2dac_readBufferPosition = 0;
	crossbar.adc2dac_readBufferPosition_float = 0;

	/* Start 25 Mhz and 32 Mhz Clocks */
	Crossbar_Recalculate_Clocks_Cycles();
	Crossbar_Start_InterruptHandler_25Mhz();
	Crossbar_Start_InterruptHandler_32Mhz();

	/* Start Microphone jack emulation */
	if (crossbar.microphone_ADC_is_started == 0) {
		crossbar.microphone_ADC_is_started = Microphone_Start((int)nAudioFrequency);
	}

	/* Initialize special transfer mode */
	crossbar.save_special_transfer = 0;

	/* Initialize Crossbar values after reboot */
	IoMem_WriteByte(0xff8900,0x05);
	IoMem_WriteByte(0xff8903,0xff);
	IoMem_WriteByte(0xff8905,0xff);
	IoMem_WriteByte(0xff8907,0xfe);
	IoMem_WriteByte(0xff8909,0xff);
	IoMem_WriteByte(0xff890b,0xff);
	IoMem_WriteByte(0xff890d,0xfe);
	IoMem_WriteByte(0xff890f,0xff);
	IoMem_WriteByte(0xff8911,0xff);
	IoMem_WriteByte(0xff8913,0xfe);
	IoMem_WriteWord(0xff893c,0x2401);
}

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
	MemorySnapShot_Store(&adc, sizeof(adc));
	MemorySnapShot_Store(&dspXmit, sizeof(dspXmit));
	MemorySnapShot_Store(&dspReceive, sizeof(dspReceive));
}


/*----------------------------------------------------------------------*/
/*	Hardware I/O functions						*/
/*----------------------------------------------------------------------*/

/**
 * Write byte to Microwire Mask register(0xff8924).
 * Note: On Falcon, the Microwire is not present.
 *       But for compatibility with the STe, Atari implemented the Microwire
 *       as follow (when one writes at the following address):
 *       $ff8922: always reads 0 for any value written at this address
 *       $ff8924: NOT the value, then 8 cycles later, NOT the value again to its initial value.
 */
void Crossbar_Microwire_WriteWord(void)
{
	Uint16 microwire = IoMem_ReadWord(0xff8924);
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8924 (MicroWire Mask) write: 0x%04x\n", microwire);

	/* NOT the value and store it */
	microwire = ~microwire;
	IoMem_WriteWord(0xff8924, microwire);
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8924 (MicroWire Mask) NOT value: 0x%04x\n", microwire);

	/* Start a new Microwire interrupt */
	CycInt_AddRelativeInterrupt(8, INT_CPU_CYCLE, INTERRUPT_DMASOUND_MICROWIRE);
}

/**
 * Crossbar Microwire mask interrupt, called from dmaSnd.c
 */
void Crossbar_InterruptHandler_Microwire(void)
{
	Uint16 microwire = IoMem_ReadWord(0xff8924);

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* NOT the value again to it's original value and store it */
	microwire = ~microwire;
	IoMem_WriteWord(0xff8924, microwire);
	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8924 (MicroWire Mask) NOT value to original: 0x%04x\n", microwire);
}

/**
 * Write byte to buffer interrupts (0xff8900).
 */
void Crossbar_BufferInter_WriteByte(void)
{
	Uint8 dmaCtrl = IoMem_ReadByte(0xff8900);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8900 (Sound DMA control) write: 0x%02x\n", dmaCtrl);

	dmaPlay.timerA_int   = (dmaCtrl & 0x4) >> 2;
	dmaPlay.mfp15_int    = (dmaCtrl & 0x1);
	dmaRecord.timerA_int = (dmaCtrl & 0x8) >> 3;
	dmaRecord.mfp15_int  = (dmaCtrl & 0x2) >> 1;
}

/**
 * Write byte from DMA control register (0xff8901).
 */
void Crossbar_DmaCtrlReg_WriteByte(void)
{
	Uint8 sndCtrl = IoMem_ReadByte(0xff8901);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8901 (additional Sound DMA control) write: 0x%02x\n", sndCtrl);

	crossbar.dmaSelected = (sndCtrl & 0x80) >> 7;

	/* DMA Play mode */
	if ((dmaPlay.isRunning == 0) && (sndCtrl & CROSSBAR_SNDCTRL_PLAY))
	{
		/* Turning on DMA Play sound emulation */
		dmaPlay.isRunning = 1;
		nCbar_DmaSoundControl = sndCtrl;
		dmaPlay.loopMode = (sndCtrl & 0x2) >> 1;
		Crossbar_setDmaPlay_Settings();
	}
	else if (dmaPlay.isRunning && ((sndCtrl & CROSSBAR_SNDCTRL_PLAY) == 0))
	{
		/* Create samples up until this point with current values */
		Sound_Update(false);

		/* Turning off DMA play sound emulation */
		dmaPlay.isRunning = 0;
		dmaPlay.loopMode = 0;
		nCbar_DmaSoundControl = sndCtrl;
	}

	/* DMA Record mode */
	if ((dmaRecord.isRunning == 0) && (sndCtrl & CROSSBAR_SNDCTRL_RECORD))
	{
		/* Turning on DMA record sound emulation */
		dmaRecord.isRunning = 1;
		nCbar_DmaSoundControl = sndCtrl;
		dmaRecord.loopMode = (sndCtrl & 0x20) >> 5;
		Crossbar_setDmaRecord_Settings();
	}
	else if (dmaRecord.isRunning && ((sndCtrl & CROSSBAR_SNDCTRL_RECORD) == 0))
	{
		/* Turning off DMA record sound emulation */
		dmaRecord.isRunning = 0;
		dmaRecord.loopMode = 0;
		nCbar_DmaSoundControl = sndCtrl;
	}
}


/**
 * Read byte from sound frame start high register (0xff8903).
 */
void Crossbar_FrameStartHigh_ReadByte(void)
{
	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		IoMem_WriteByte(0xff8903, crossbar.dmaPlay_CurrentFrameStart >> 16);
	}
	else {
		/* DMA Record selected */
		IoMem_WriteByte(0xff8903, crossbar.dmaRecord_CurrentFrameStart >> 16);
	}
}

/**
 * Write byte to sound frame start high register (0xff8903).
 */
void Crossbar_FrameStartHigh_WriteByte(void)
{
	Uint32 addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8903 (Sound frame start high) write: 0x%02x\n", IoMem_ReadByte(0xff8903));

	addr = (IoMem_ReadByte(0xff8903) << 16) + (IoMem_ReadByte(0xff8905) << 8) + IoMem_ReadByte(0xff8907);

	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		crossbar.dmaPlay_CurrentFrameStart = addr & ~1;
	}
	else {
		/* DMA Record selected */
		crossbar.dmaRecord_CurrentFrameStart = addr & ~1;
	}
}

/**
 * Read byte from sound frame start medium register (0xff8905).
 */
void Crossbar_FrameStartMed_ReadByte(void)
{
	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		IoMem_WriteByte(0xff8905, crossbar.dmaPlay_CurrentFrameStart >> 8);
	}
	else {
		/* DMA Record selected */
		IoMem_WriteByte(0xff8905, crossbar.dmaRecord_CurrentFrameStart >> 8);
	}
}

/**
 * Write byte to sound frame start medium register (0xff8905).
 */
void Crossbar_FrameStartMed_WriteByte(void)
{
	Uint32 addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8905 (Sound frame start med) write: 0x%02x\n", IoMem_ReadByte(0xff8905));

	addr = (IoMem_ReadByte(0xff8903) << 16) + (IoMem_ReadByte(0xff8905) << 8) + IoMem_ReadByte(0xff8907);

	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		crossbar.dmaPlay_CurrentFrameStart = addr & ~1;
	}
	else {
		/* DMA Record selected */
		crossbar.dmaRecord_CurrentFrameStart = addr & ~1;
	}
}

/**
 * Read byte from sound frame start low register (0xff8907).
 */
void Crossbar_FrameStartLow_ReadByte(void)
{
	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		IoMem_WriteByte(0xff8907, crossbar.dmaPlay_CurrentFrameStart);
	}
	else {
		/* DMA Record selected */
		IoMem_WriteByte(0xff8907, crossbar.dmaRecord_CurrentFrameStart);
	}
}

/**
 * Write byte to sound frame start low register (0xff8907).
 */
void Crossbar_FrameStartLow_WriteByte(void)
{
	Uint32 addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8907 (Sound frame start low) write: 0x%02x\n", IoMem_ReadByte(0xff8907));

	addr = (IoMem_ReadByte(0xff8903) << 16) + (IoMem_ReadByte(0xff8905) << 8) + IoMem_ReadByte(0xff8907);

	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		crossbar.dmaPlay_CurrentFrameStart = addr & ~1;
	}
	else {
		/* DMA Record selected */
		crossbar.dmaRecord_CurrentFrameStart = addr & ~1;
	}
}

/*-----------------------------------------------------------------------*/

/**
 * Read byte from sound frame count high register (0xff8909).
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

/**
 * Write byte to sound frame count high register (0xff8909).
 */
void Crossbar_FrameCountHigh_WriteByte(void)
{
	Uint32 addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8909 (Sound frame count high) write: 0x%02x\n", IoMem_ReadByte(0xff8909));

	/* Compute frameCounter current address */
	addr = (IoMem_ReadByte(0xff8909) << 16) + (IoMem_ReadByte(0xff890b) << 8) + IoMem_ReadByte(0xff890d);

	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		crossbar.dmaPlay_CurrentFrameCount = addr;
	}
	else {
		/* DMA Record selected */
		crossbar.dmaRecord_CurrentFrameCount = addr;
	}
}

/**
 * Read byte from sound frame count medium register (0xff890b).
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

/**
 * Write byte to sound frame count medium register (0xff890b).
 */
void Crossbar_FrameCountMed_WriteByte(void)
{
	Uint32 addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff890b (Sound frame count med) write: 0x%02x\n", IoMem_ReadByte(0xff890b));

	/* Compute frameCounter current address */
	addr = (IoMem_ReadByte(0xff8909) << 16) + (IoMem_ReadByte(0xff890b) << 8) + IoMem_ReadByte(0xff890d);

	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		crossbar.dmaPlay_CurrentFrameCount = addr;
	}
	else {
		/* DMA Record selected */
		crossbar.dmaRecord_CurrentFrameCount = addr;
	}
}

/**
 * Read byte from sound frame count low register (0xff890d).
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

/**
 * Write byte to sound frame count low register (0xff890d).
 */
void Crossbar_FrameCountLow_WriteByte(void)
{
	Uint32 addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff890d (Sound frame count low) write: 0x%02x\n", IoMem_ReadByte(0xff890d));

	/* Compute frameCounter current address */
	addr = (IoMem_ReadByte(0xff8909) << 16) + (IoMem_ReadByte(0xff890b) << 8) + IoMem_ReadByte(0xff890d);

	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		crossbar.dmaPlay_CurrentFrameCount = addr;
	}
	else {
		/* DMA Record selected */
		crossbar.dmaRecord_CurrentFrameCount = addr;
	}
}

/*-----------------------------------------------------------------------*/

/**
 * Read byte from sound frame end high register (0xff890f).
 */
void Crossbar_FrameEndHigh_ReadByte(void)
{
	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		IoMem_WriteByte(0xff890f, crossbar.dmaPlay_CurrentFrameEnd >> 16);
	}
	else {
		/* DMA Record selected */
		IoMem_WriteByte(0xff890f, crossbar.dmaRecord_CurrentFrameEnd >> 16);
	}
}

/**
 * Write byte to sound frame end high register (0xff890f).
 */
void Crossbar_FrameEndHigh_WriteByte(void)
{
	Uint32 addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff890f (Sound frame end high) write: 0x%02x\n", IoMem_ReadByte(0xff890f));

	addr = (IoMem_ReadByte(0xff890f) << 16) + (IoMem_ReadByte(0xff8911) << 8) + IoMem_ReadByte(0xff8913);

	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		crossbar.dmaPlay_CurrentFrameEnd = addr & ~1;
	}
	else {
		/* DMA Record selected */
		crossbar.dmaRecord_CurrentFrameEnd = addr & ~1;
	}
}

/**
 * Read byte from sound frame end medium register (0xff8911).
 */
void Crossbar_FrameEndMed_ReadByte(void)
{
	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		IoMem_WriteByte(0xff8911, crossbar.dmaPlay_CurrentFrameEnd >> 8);
	}
	else {
		/* DMA Record selected */
		IoMem_WriteByte(0xff8911, crossbar.dmaRecord_CurrentFrameEnd >> 8);
	}
}

/**
 * Write byte to sound frame end medium register (0xff8911).
 */
void Crossbar_FrameEndMed_WriteByte(void)
{
	Uint32 addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8911 (Sound frame end med) write: 0x%02x\n", IoMem_ReadByte(0xff8911));

	addr = (IoMem_ReadByte(0xff890f) << 16) + (IoMem_ReadByte(0xff8911) << 8) + IoMem_ReadByte(0xff8913);

	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		crossbar.dmaPlay_CurrentFrameEnd = addr & ~1;
	}
	else {
		/* DMA Record selected */
		crossbar.dmaRecord_CurrentFrameEnd = addr & ~1;
	}
}

/**
 * Read byte from sound frame end low register (0xff8913).
 */
void Crossbar_FrameEndLow_ReadByte(void)
{
	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		IoMem_WriteByte(0xff8913, crossbar.dmaPlay_CurrentFrameEnd);
	}
	else {
		/* DMA Record selected */
		IoMem_WriteByte(0xff8913, crossbar.dmaRecord_CurrentFrameEnd);
	}
}

/**
 * Write byte to sound frame end low register (0xff8913).
 */
void Crossbar_FrameEndLow_WriteByte(void)
{
	Uint32 addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8913 (Sound frame end low) write: 0x%02x\n", IoMem_ReadByte(0xff8913));

	addr = (IoMem_ReadByte(0xff890f) << 16) + (IoMem_ReadByte(0xff8911) << 8) + IoMem_ReadByte(0xff8913);

	if (crossbar.dmaSelected == 0) {
		/* DMA Play selected */
		crossbar.dmaPlay_CurrentFrameEnd = addr & ~1;
	}
	else {
		/* DMA Record selected */
		crossbar.dmaRecord_CurrentFrameEnd = addr & ~1;
	}
}

/*-----------------------------------------------------------------------*/
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
 * Write word to sound mode register (0xff8921).
 */
void Crossbar_SoundModeCtrl_WriteByte(void)
{
	Uint8 sndCtrl = IoMem_ReadByte(0xff8921);

	LOG_TRACE(TRACE_CROSSBAR, "crossbar : $ff8921 (additional sound mode control) write: 0x%02x\n", sndCtrl);

	crossbar.is16Bits = (sndCtrl & 0x40) >> 6;
	crossbar.isStereo = 1 - ((sndCtrl & 0x80) >> 7);
	crossbar.steFreq = sndCtrl & 0x3;

	Crossbar_Recalculate_Clocks_Cycles();
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

	dspXmit.isTristated = 1 - ((nCbSrc >> 7) & 0x1);
	dspXmit.isInHandshakeMode = 1 - ((nCbSrc >> 4) & 0x1);

	crossbar.dspXmit_freq = (nCbSrc >> 5) & 0x3;
	crossbar.dmaPlay_freq = (nCbSrc >> 1) & 0x3;
}

/**
 * Write word to Falcon Crossbar destination controller (0xff8932).
	Source: D/A Convertor                 BIT 15 14 13 12
	00 - DMA output ------------------------------+--+
	01 - DSP output ------------------------------+--+
	10 - External input --------------------------+--+
	11 - ADC input -------------------------------+--'

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

	dmaPlay.isConnectedToDspInHandShakeMode = (((destCtrl >> 4) & 7) == 0 ? 1 : 0);
	dmaPlay.handshakeMode_Frame = dmaPlay.isConnectedToDspInHandShakeMode;

	dmaRecord.isConnectedToDspInHandShakeMode = ((destCtrl & 0xf) == 2 ? 1 : 0);
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
void Crossbar_FreqDivInt_WriteByte(void)
{
	Uint8 clkDiv = IoMem_ReadByte(0xff8935);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8935 (int. clock divider) write: 0x%02x\n", clkDiv);

	crossbar.int_freq_divider = clkDiv & 0xf;
	Crossbar_Recalculate_Clocks_Cycles();
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
 * Write byte to CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D convertor
 */
void Crossbar_CodecInput_WriteByte(void)
{
	Uint8 inputSource = IoMem_ReadByte(0xff8937);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8937 (CODEC input) write: 0x%02x\n", IoMem_ReadByte(0xff8937));

	crossbar.codecInputSource = inputSource & 3;
}

/**
 * Write byte to A/D converter input for L+R channel (0xff8938).
 *	Bit 1 :  Left (0 = Microphone ; 1 = PSG soundchip)
 *	Bit 0 : Right (0 = Microphone ; 1 = PSG soundchip)
 */
void Crossbar_AdcInput_WriteByte(void)
{
	Uint8 input = IoMem_ReadByte(0xff8938);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8938 (ADC input) write: 0x%02x\n", IoMem_ReadByte(0xff8938));

	crossbar.codecAdcInput = input & 3;
}

/**
 * Write byte to input amplifier register (amplification for ADC device) (0xff8939).
 * 	Bits LLLLRRRR
 * 	Amplification is in +1.5 dB steps
 */
void Crossbar_InputAmp_WriteByte(void)
{
	Uint8 amplification = IoMem_ReadByte(0xff8939);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8939 (CODEC channel amplification) write: 0x%02x\n", IoMem_ReadByte(0xff8939));

	crossbar.gainSettingLeft = Crossbar_ADC_volume_table[amplification >> 4];
	crossbar.gainSettingRight = Crossbar_ADC_volume_table[amplification & 0xf];
}

/**
 * Write byte to channel reduction register (attenuation for DAC device) (0xff893a).
 * 	Bits XXXXLLLL RRRRXXXX
 * 	Reduction is in -1.5 dB steps
 */
void Crossbar_OutputReduct_WriteWord(void)
{
	Uint16 reduction = IoMem_ReadWord(0xff893a);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff893a (CODEC channel attenuation) write: 0x%04x\n", reduction);

	crossbar.attenuationSettingLeft = Crossbar_DAC_volume_table[(reduction >> 8) & 0x0f];
	crossbar.attenuationSettingRight = Crossbar_DAC_volume_table[(reduction >> 4) & 0x0f];
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
/*------------------------- Crossbar functions -------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Recalculates internal clocks 25 Mhz and 32 Mhz cycles
 */
void Crossbar_Recalculate_Clocks_Cycles(void)
{
	double cyclesClk;

	crossbar.clock25_cycles_counter = 0;
	crossbar.clock32_cycles_counter = 0;

	/* Calculate 25 Mhz clock cycles */
#ifdef OLD_CPU_SHIFT
	cyclesClk = ((double)CPU_FREQ / Crossbar_DetectSampleRate(25)) / (double)(crossbar.playTracks) / 2.0;
#else
	/* Take nCpuFreqShift into account to keep a constant sound rate at all cpu freq */
	cyclesClk = ((double)( ( ( MachineClocks.CPU_Freq / 2 ) << nCpuFreqShift ) ) / Crossbar_DetectSampleRate(25)) / (double)(crossbar.playTracks) / 2.0;
#endif
	crossbar.clock25_cycles = (int)(cyclesClk);
	crossbar.clock25_cycles_decimal = (int)((cyclesClk - (double)(crossbar.clock25_cycles)) * (double)DECIMAL_PRECISION);

	/* Calculate 32 Mhz clock cycles */
#ifdef OLD_CPU_SHIFT
	cyclesClk = ((double)CPU_FREQ / Crossbar_DetectSampleRate(32)) / (double)(crossbar.playTracks) / 2.0;
#else
	/* Take nCpuFreqShift into account to keep a constant sound rate at all cpu freq */
	cyclesClk = ((double)( ( ( MachineClocks.CPU_Freq / 2 ) << nCpuFreqShift ) ) / Crossbar_DetectSampleRate(32)) / (double)(crossbar.playTracks) / 2.0;
#endif
	crossbar.clock32_cycles = (int)(cyclesClk);
	crossbar.clock32_cycles_decimal = (int)((cyclesClk - (double)(crossbar.clock32_cycles)) * (double)DECIMAL_PRECISION);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : Recalculate_clock_Cycles\n");
	LOG_TRACE(TRACE_CROSSBAR, "           clock25 : %d\n", crossbar.clock25_cycles);
	LOG_TRACE(TRACE_CROSSBAR, "           clock32 : %d\n", crossbar.clock32_cycles);

	/* Verify if the new frequency doesn't mute the DAC */
	crossbar.isDacMuted = 0;
	if ((crossbar.int_freq_divider == 0) && (crossbar.steFreq == 0))
		crossbar.isDacMuted = 1;

	if ((crossbar.int_freq_divider == 6) || (crossbar.int_freq_divider == 8) ||
	    (crossbar.int_freq_divider == 10) || (crossbar.int_freq_divider >= 12)) {
		crossbar.isDacMuted = 1;
		LOG_TRACE(TRACE_CROSSBAR, "           DAC is muted\n");
	}

	// Compute Ratio between host computer sound frequency and Hatari's sound frequency.
	Crossbar_Compute_Ratio();
}

/**
 * 	Compute Ratio between host computer sound frequency and Hatari's DAC sound frequency and
 * 	ratio between hatari's DAC sound frequency and host's sound frequency.
 * 	Both values use << 32 to simulate floating point precision
 *	Can be called by audio.c if a sound frequency value is changed in the parameter GUI.
 */
void Crossbar_Compute_Ratio(void)
{
	crossbar.frequence_ratio = ( ((Sint64)Crossbar_DetectSampleRate(25)) << 32) / nAudioFrequency;
	crossbar.frequence_ratio2 = ( ((Sint64)nAudioFrequency) << 32) / Crossbar_DetectSampleRate(25);
}

/**
 * Detect sample rate frequency
 *    clock : value of the internal clock (25 or 32).
 */
static int Crossbar_DetectSampleRate(Uint16 clock)
{
	/* Ste compatible sound */
	if (crossbar.int_freq_divider == 0) {
		crossbar.isInSteFreqMode = 1;
		return Ste_SampleRates[crossbar.steFreq];
	}

	crossbar.isInSteFreqMode = 0;

	/* 25 Mhz internal clock */
	if (clock == 25)
		return Falcon_SampleRates_25Mhz[crossbar.int_freq_divider - 1];

	/* 32 Mhz internal clock */
	return Falcon_SampleRates_32Mhz[crossbar.int_freq_divider - 1];
}

/**
 * Start internal 25 Mhz clock interrupt.
 */
static void Crossbar_Start_InterruptHandler_25Mhz(void)
{
	Uint32 cycles_25;

	cycles_25 = crossbar.clock25_cycles;
	crossbar.clock25_cycles_counter += crossbar.clock25_cycles_decimal;

	if (crossbar.clock25_cycles_counter >= DECIMAL_PRECISION) {
		crossbar.clock25_cycles_counter -= DECIMAL_PRECISION;
		cycles_25 ++;
	}

	if (crossbar.pendingCyclesOver25 >= cycles_25) {
		crossbar.pendingCyclesOver25 -= cycles_25;
		cycles_25 = 0;
	}
	else {
		cycles_25 -= crossbar.pendingCyclesOver25;
		crossbar.pendingCyclesOver25 = 0;
	}

	CycInt_AddRelativeInterrupt(cycles_25, INT_CPU_CYCLE, INTERRUPT_CROSSBAR_25MHZ);
}

/**
 * Start internal 32 Mhz clock interrupt.
 */
static void Crossbar_Start_InterruptHandler_32Mhz(void)
{
	Uint32 cycles_32;

	cycles_32 = crossbar.clock32_cycles;
	crossbar.clock32_cycles_counter += crossbar.clock32_cycles_decimal;

	if (crossbar.clock32_cycles_counter >= DECIMAL_PRECISION) {
		crossbar.clock32_cycles_counter -= DECIMAL_PRECISION;
		cycles_32 ++;
	}

	if (crossbar.pendingCyclesOver32 >= cycles_32){
		crossbar.pendingCyclesOver32 -= cycles_32;
		cycles_32 = 0;
	}
	else {
		cycles_32 -= crossbar.pendingCyclesOver32;
		crossbar.pendingCyclesOver32 = 0;
	}

	CycInt_AddRelativeInterrupt(cycles_32, INT_CPU_CYCLE, INTERRUPT_CROSSBAR_32MHZ);
}


/**
 * Execute transfers for internal 25 Mhz clock.
 */
void Crossbar_InterruptHandler_25Mhz(void)
{
	/* How many cycle was this sound interrupt delayed (>= 0) */
	crossbar.pendingCyclesOver25 += -INT_CONVERT_FROM_INTERNAL ( PendingInterruptCount , INT_CPU_CYCLE );

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* If transfer mode is in Ste mode, use only this clock for all the transfers */
	if (crossbar.isInSteFreqMode) {
		Crossbar_Process_DSPXmit_Transfer();
		Crossbar_Process_DMAPlay_Transfer();
		Crossbar_Process_ADCXmit_Transfer();

		/* Restart the 25 Mhz clock interrupt */
		Crossbar_Start_InterruptHandler_25Mhz();
		return;
	}

	Crossbar_Process_ADCXmit_Transfer();

	/* DSP Play transfer ? */
	if (crossbar.dspXmit_freq == CROSSBAR_FREQ_25MHZ) {
		Crossbar_Process_DSPXmit_Transfer();
	}

	/* DMA Play transfer ? */
	if (crossbar.dmaPlay_freq == CROSSBAR_FREQ_25MHZ) {
		Crossbar_Process_DMAPlay_Transfer();
	}

	/* Restart the 25 Mhz clock interrupt */
	Crossbar_Start_InterruptHandler_25Mhz();
}

/**
 * Execute transfers for internal 32 Mhz clock.
 */
void Crossbar_InterruptHandler_32Mhz(void)
{
	/* How many cycle was this sound interrupt delayed (>= 0) */
	crossbar.pendingCyclesOver32 += -INT_CONVERT_FROM_INTERNAL ( PendingInterruptCount , INT_CPU_CYCLE );

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* If transfer mode is in Ste mode, don't use this clock for all the transfers */
	if (crossbar.isInSteFreqMode) {
		/* Restart the 32 Mhz clock interrupt */
		Crossbar_Start_InterruptHandler_32Mhz();
		return;
	}

	/* DSP Play transfer ? */
	if (crossbar.dspXmit_freq == CROSSBAR_FREQ_32MHZ) {
		Crossbar_Process_DSPXmit_Transfer();
	}

	/* DMA Play transfer ? */
	if (crossbar.dmaPlay_freq == CROSSBAR_FREQ_32MHZ) {
		Crossbar_Process_DMAPlay_Transfer();
	}

	/* Restart the 32 Mhz clock interrupt */
	Crossbar_Start_InterruptHandler_32Mhz();
}


/*----------------------------------------------------------------------*/
/*--------------------- DSP Xmit processing ----------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Process DSP xmit to crossbar transfer
 */
static void Crossbar_Process_DSPXmit_Transfer(void)
{
	Uint16 frame=0;
	Sint32 data;

	/* If DSP Xmit is tristated, do nothing */
	if (dspXmit.isTristated)
		return;

	/* Is DSP Xmit connected to DMA Record in handshake mode ? */
	if (dmaRecord.isConnectedToDspInHandShakeMode) {
		Crossbar_Process_DMARecord_HandshakeMode();
		return;
	}

	/* Is DSP Xmit connected to something ? */
	if (!dspXmit.isConnectedToCodec && !dspXmit.isConnectedToDma && !dspXmit.isConnectedToDsp)
		return;

	if (dspXmit.wordCount == 0) {
		frame = 1;
	}

	/* Send the frame status to the DSP SSI Xmit */
	DSP_SsiReceive_SC2(frame);

	/* Send the clock to the DSP SSI Xmit */
	DSP_SsiReceive_SCK();

	/* read data from DSP Xmit */
	data = DSP_SsiReadTxValue();

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : DSP --> Crossbar transfer\t0x%06x\n", data);

 	/* Send DSP data to the DAC ? */
	if (dspXmit.isConnectedToCodec) {
		Crossbar_SendDataToDAC(data, dspXmit.wordCount);
	}

 	/* Send DSP data to the DMA record ? */
	if (dspXmit.isConnectedToDma) {
		Crossbar_SendDataToDmaRecord(data);
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
	if (dmaPlay.handshakeMode_Frame == 0) {
		DSP_SsiReceive_SC1(frame);
	}

	dmaPlay.handshakeMode_Frame = 0;

	/* Send the clock to the DSP SSI receive */
	DSP_SsiReceive_SC0();
}

/*----------------------------------------------------------------------*/
/*--------------------- DMA PLAY sound processing ----------------------*/
/*----------------------------------------------------------------------*/

/**
 * Set DMA Play sound start frame buffer, stop frame buffer, frame length
 */
static void Crossbar_setDmaPlay_Settings(void)
{
	/* DMA setings */
	dmaPlay.frameStartAddr = crossbar.dmaPlay_CurrentFrameStart;
	dmaPlay.frameEndAddr = crossbar.dmaPlay_CurrentFrameEnd;
	dmaPlay.frameLen = dmaPlay.frameEndAddr - dmaPlay.frameStartAddr;
//	dmaPlay.frameCounter = crossbar.dmaPlay_CurrentFrameCount - crossbar.dmaPlay_CurrentFrameStart;
	dmaPlay.frameCounter = 0;

	if (dmaPlay.frameEndAddr <= dmaPlay.frameStartAddr)
	{
		Log_Printf(LOG_WARN, "crossbar DMA Play: Illegal buffer size (from 0x%06x to 0x%06x)\n",
		          dmaPlay.frameStartAddr, dmaPlay.frameEndAddr);
	}
}

/**
 * Process DMA Play transfer to crossbar
 */
static void Crossbar_Process_DMAPlay_Transfer(void)
{
	Uint16 temp, increment_frame;
	Sint16 value, eightBits;
	Sint8  *pFrameStart;
	Uint8  dmaCtrlReg;

	/* if DMA play is not running, return */
	if (dmaPlay.isRunning == 0)
		return;

	pFrameStart = (Sint8 *)&STRam[dmaPlay.frameStartAddr];
	increment_frame = 0;

	/* 16 bits stereo mode ? */
	if (crossbar.is16Bits) {
		eightBits = 1;
		value = (Sint16)do_get_mem_word(&pFrameStart[dmaPlay.frameCounter]);
		increment_frame = 2;
	}
	/* 8 bits stereo ? */
	else if (crossbar.isStereo) {
		eightBits = 64;
		value = (Sint16) pFrameStart[dmaPlay.frameCounter];
		increment_frame = 1;
	}
	/* 8 bits mono */
	else {
		eightBits = 64;
		value = (Sint16) pFrameStart[dmaPlay.frameCounter];
		if ((dmaPlay.currentFrame & 1) == 0) {
			increment_frame = 1;
		}
	}

	if (dmaPlay.isConnectedToDspInHandShakeMode) {
		/* Handshake mode */
		if (dmaPlay.handshakeMode_Frame == 0)
			return;

		dmaPlay.frameCounter += increment_frame;

		/* Special undocumented transfer mode :
		   When DMA Play --> DSP Receive is in HandShake mode at 32 Mhz,
		   datas are shifted 2 bits on the left after the transfer.
		   This occurs with all demos using the Mpeg2 player from nocrew (amanita, LostBlubb, Wait, ...)
		*/
		if (crossbar.dmaPlay_freq == CROSSBAR_FREQ_32MHZ) {
			temp = (crossbar.save_special_transfer<<2) + ((value & 0xc000)>>14);
			crossbar.save_special_transfer = value;
			value = temp;
		}
	}
	else {
		/* Non Handshake mode */
		dmaPlay.frameCounter += increment_frame;
	}

	/* Send sample to the DMA record ? */
	if (dmaPlay.isConnectedToDma) {
		LOG_TRACE(TRACE_CROSSBAR, "Crossbar : DMA Play --> DMA record\n");
		Crossbar_SendDataToDmaRecord(value);
	}

	/* Send sample to the DAC ? */
	if (dmaPlay.isConnectedToCodec) {
		LOG_TRACE(TRACE_CROSSBAR, "Crossbar : DMA Play --> DAC\n");
		Crossbar_SendDataToDAC(value * eightBits, dmaPlay.currentFrame);
	}

	/* Send sample to the DSP in ? */
	if (dmaPlay.isConnectedToDsp) {
		LOG_TRACE(TRACE_CROSSBAR, "Crossbar : DMA Play --> DSP record\n");
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

	/* Check if end-of-frame has been reached and raise interrupts if needed. */
	if (dmaPlay.frameCounter >= dmaPlay.frameLen)
	{
		/* Send a MFP15_Int (I7) at end of replay buffer if enabled */
		if (dmaPlay.mfp15_int) {
			MFP_InputOnChannel ( MFP_INT_GPIP7 , 0 );
			LOG_TRACE(TRACE_CROSSBAR, "Crossbar : MFP15 (IT7) interrupt from DMA play\n");
		}

		/* Send a TimerA_Int at end of replay buffer if enabled */
		if (dmaPlay.timerA_int) {
			if (MFP_TACR == 0x08) {       /* Is timer A in Event Count mode? */
				MFP_TimerA_EventCount_Interrupt();
				LOG_TRACE(TRACE_CROSSBAR, "Crossbar : MFP Timer A interrupt from DMA play\n");
			}
		}

		if (dmaPlay.loopMode) {
			Crossbar_setDmaPlay_Settings();
		}
		else {
			/* Create samples up until this point with current values */
			Sound_Update(false);

			dmaCtrlReg = IoMem_ReadByte(0xff8901) & 0xfe;
			IoMem_WriteByte(0xff8901, dmaCtrlReg);

			/* Turning off DMA play sound emulation */
			dmaPlay.isRunning = 0;
			dmaPlay.loopMode = 0;
			nCbar_DmaSoundControl = dmaCtrlReg;
		}
	}
}

/**
 * Function called when DmaPlay is in handshake mode */
void Crossbar_DmaPlayInHandShakeMode(void)
{
	dmaPlay.handshakeMode_Frame = 1;
}

/*----------------------------------------------------------------------*/
/*--------------------- DMA Record processing --------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Set DMA Record sound start frame buffer, stop frame buffer, frame length
 */
static void Crossbar_setDmaRecord_Settings(void)
{
	/* DMA setings */
	dmaRecord.frameStartAddr = crossbar.dmaRecord_CurrentFrameStart;
	dmaRecord.frameEndAddr = crossbar.dmaRecord_CurrentFrameEnd;
	dmaRecord.frameLen = dmaRecord.frameEndAddr - dmaRecord.frameStartAddr;
//	dmaRecord.frameCounter = crossbar.dmaRecord_CurrentFrameCount - crossbar.dmaRecord_CurrentFrameStart;
	dmaRecord.frameCounter = 0;

	if (dmaRecord.frameEndAddr <= dmaRecord.frameStartAddr) {
		Log_Printf(LOG_WARN, "crossbar DMA Record: Illegal buffer size (from 0x%06x to 0x%06x)\n",
		          dmaRecord.frameStartAddr, dmaRecord.frameEndAddr);
	}
}

/**
 * DMA Record processing
 */
void Crossbar_SendDataToDmaRecord(Sint16 value)
{
	Sint8  *pFrameStart;
	Uint8  dmaCtrlReg;

	if (dmaRecord.isRunning == 0) {
		return;
	}

	pFrameStart = (Sint8 *)&STRam[dmaRecord.frameStartAddr];

	/* 16 bits stereo mode ? */
	if (crossbar.is16Bits) {
		do_put_mem_word(&pFrameStart[dmaRecord.frameCounter], value);
		dmaRecord.frameCounter += 2;
	}
	/* 8 bits stereo ? */
	else if (crossbar.isStereo) {
		do_put_mem_word(&pFrameStart[dmaRecord.frameCounter], value);
		dmaRecord.frameCounter += 2;
//		pFrameStart[dmaRecord.frameCounter] = (Uint8)value;
//		dmaRecord.frameCounter ++;
	}
	/* 8 bits mono */
	else {
		pFrameStart[dmaRecord.frameCounter] = (Uint8)value;
		dmaRecord.frameCounter ++;
	}

	/* Check if end-of-frame has been reached and raise interrupts if needed. */
	if (dmaRecord.frameCounter >= dmaRecord.frameLen)
	{
		/* Send a MFP15_Int (I7) at end of record buffer if enabled */
		if (dmaRecord.mfp15_int) {
			MFP_InputOnChannel ( MFP_INT_GPIP7 , 0 );
			LOG_TRACE(TRACE_CROSSBAR, "Crossbar : MFP15 (IT7) interrupt from DMA record\n");
		}

		/* Send a TimerA_Int at end of record buffer if enabled */
		if (dmaRecord.timerA_int) {
			if (MFP_TACR == 0x08)       /* Is timer A in Event Count mode? */
				MFP_TimerA_EventCount_Interrupt();
			LOG_TRACE(TRACE_CROSSBAR, "Crossbar : MFP Timer A interrupt from DMA record\n");
		}

		if (dmaRecord.loopMode) {
			Crossbar_setDmaRecord_Settings();
		}
		else {
			dmaCtrlReg = IoMem_ReadByte(0xff8901) & 0xef;
			IoMem_WriteByte(0xff8901, dmaCtrlReg);

			/* Turning off DMA record sound emulation */
			dmaRecord.isRunning = 0;
			dmaRecord.loopMode = 0;
			nCbar_DmaSoundControl = dmaCtrlReg;
		}
	}
}


/**
 * Process DMA Record connected to DSP Xmit in HandShake mode.
 * In this special case, DMA Record is the "master" and Dsp Xmit is the "slave".
 */
static void Crossbar_Process_DMARecord_HandshakeMode(void)
{
	Sint16 data;

	/* If DMA record is activated and is running */
	if (dmaRecord.isRunning == 0) {
		return;
	}

	/* If DSP frame is activated (SC2 pin of the SSI port) */
	if (dmaRecord.handshakeMode_Frame == 0) {
		return;
	}

	/* Send the clock to the DSP SSI Xmit */
	DSP_SsiReceive_SCK();

	/* read data from DSP Xmit */
	data = DSP_SsiReadTxValue();
	dmaRecord.handshakeMode_Frame = 0;

	Crossbar_SendDataToDmaRecord(data);
}

/**
 * Get the frame value from DSP SSI (handshake mode only)
 */
void Crossbar_DmaRecordInHandShakeMode_Frame(Uint32 frame)
{
	dmaRecord.handshakeMode_Frame = frame;
}


/*----------------------------------------------------------------------*/
/*-------------------------- ADC processing ----------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Get datas recorded by the microphone and convert them into falcon internal frequency
 *    - micro_bufferL : left track recorded by the microphone
 *    - micro_bufferR : right track recorded by the microphone
 *    - microBuffer_size : buffers size
 */
void Crossbar_GetMicrophoneDatas(Sint16 *micro_bufferL, Sint16 *micro_bufferR, Uint32 microBuffer_size)
{
	Uint32 i, size, bufferIndex;
	Sint64 idxPos;

	size = (microBuffer_size * crossbar.frequence_ratio>>32);
	bufferIndex = 0;
	idxPos = 0;

	for (i = 0; i < size; i++) {
		adc.writePosition = (adc.writePosition + 1) % DACBUFFER_SIZE;

		adc.buffer_left[adc.writePosition] = micro_bufferL[bufferIndex];
		adc.buffer_right[adc.writePosition] = micro_bufferR[bufferIndex];

		idxPos += crossbar.frequence_ratio2;
		bufferIndex += idxPos>>32;
		idxPos  &= 0xffffffff;			/* only keep the fractional part */
	}
}

/**
 * Process ADC transfer to crossbar
 */
static void Crossbar_Process_ADCXmit_Transfer(void)
{
	Sint16 sample;
	Uint16 frame;

	/* swap from left to right channel or right to left channel */
	adc.wordCount = 1 - adc.wordCount;

	/* Left Channel */
	if (adc.wordCount == 0) {
		sample = adc.buffer_left[adc.readPosition];
		frame = 1;
	}
	else {
		sample = adc.buffer_right[adc.readPosition];
		adc.readPosition = (adc.readPosition + 1) % DACBUFFER_SIZE;
		frame = 0;
	}

	/* Send sample to DSP receive ? */
	if (adc.isConnectedToDsp) {
		Crossbar_SendDataToDspReceive(sample, frame);
	}

	/* Send sample to DMA record ? */
	if (adc.isConnectedToDma) {
		Crossbar_SendDataToDmaRecord(sample);
	}

	/* Send sample to DAC ? */
	if (adc.isConnectedToCodec) {
		Crossbar_SendDataToDAC(sample, adc.wordCount);
	}
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

	if (sample_pos == track) {
		/* Left channel */
		dac.buffer_left[dac.writePosition] = value;
	}
	else if (sample_pos == track + 1) {
		/* Right channel */
		dac.buffer_right[dac.writePosition] = value;
		dac.writePosition = (dac.writePosition + 1) % (DACBUFFER_SIZE);
	}
}

/**
 * Mix PSG sound with microphone sound in ADC.
 * Also mix ADC sound sample with the crossbar DAC samples.
 * (Called by sound.c)
 */
void Crossbar_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate)
{
	int i, j, nBufIdx;
	int n;
	Sint16 adc_leftData, adc_rightData, dac_LeftData, dac_RightData;

	if (crossbar.isDacMuted) {
		/* Output sound = 0 */
		for (i = 0; i < nSamplesToGenerate; i++) {
			nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;
			MixBuffer[nBufIdx][0] = 0;
			MixBuffer[nBufIdx][1] = 0;
		}

		/* Counters are refreshed for when DAC becomes unmuted */
		dac.readPosition = dac.writePosition;
		crossbar.adc2dac_readBufferPosition = adc.writePosition;
		return;
	}

	for (i = 0; i < nSamplesToGenerate; i++)
	{
		nBufIdx = (nMixBufIdx + i) % MIXBUFFER_SIZE;

		/* ADC mixing (PSG sound or microphone sound for left and right channels) */
		switch (crossbar.codecAdcInput) {
			case 0:
			default: /* Just here to remove compiler's warnings */
				/* Microphone sound for left and right channels */
				adc_leftData = adc.buffer_left[crossbar.adc2dac_readBufferPosition];
				adc_rightData = adc.buffer_right[crossbar.adc2dac_readBufferPosition];
				break;
			case 1:
				/* Microphone sound for left channel, PSG sound for right channel */
				adc_leftData = adc.buffer_left[crossbar.adc2dac_readBufferPosition];
				adc_rightData = MixBuffer[nBufIdx][1];
				break;
			case 2:
				/* PSG sound for left channel, microphone sound for right channel */
				adc_leftData = MixBuffer[nBufIdx][0];
				adc_rightData = adc.buffer_right[crossbar.adc2dac_readBufferPosition];
				break;
			case 3:
				/* PSG sound for left and right channels */
				adc_leftData = MixBuffer[nBufIdx][0];
				adc_rightData = MixBuffer[nBufIdx][1];
				break;
		}

		/* DAC mixing (direct ADC + crossbar) */
		switch (crossbar.codecInputSource) {
			case 0:
			default: /* Just here to remove compiler's warnings */
				/* No sound */
				dac_LeftData  = 0;
				dac_RightData = 0;
				break;
			case 1:
				/* direct ADC->DAC sound only ADC*4/65536 */
				dac_LeftData = (adc_leftData * crossbar.gainSettingLeft) >> 14;
				dac_RightData = (adc_rightData * crossbar.gainSettingRight) >> 14;
				break;
			case 2:
				/* Crossbar->DAC sound only */
				dac_LeftData = dac.buffer_left[dac.readPosition];
				dac_RightData = dac.buffer_right[dac.readPosition];
				break;
			case 3:
				/* Mixing Direct ADC sound with Crossbar->DMA sound */
				dac_LeftData = ((adc_leftData * crossbar.gainSettingLeft) >> 14) +
						dac.buffer_left[dac.readPosition];
				dac_RightData = ((adc_rightData  * crossbar.gainSettingRight) >> 14) +
						dac.buffer_right[dac.readPosition];
				break;
		}

		MixBuffer[nBufIdx][0] = (dac_LeftData * crossbar.attenuationSettingLeft) >> 16;
		MixBuffer[nBufIdx][1] = (dac_RightData * crossbar.attenuationSettingRight) >> 16;

		/* Upgrade dac's buffer read pointer */
		dac.readPosition_float += crossbar.frequence_ratio;
		n = dac.readPosition_float >> 32;				/* number of samples to skip */

		if (n) {
			// It becomes safe to zero old data if tail has moved
			for (j=0; j<n; j++) {
				dac.buffer_left[(dac.readPosition+j) % DACBUFFER_SIZE] = 0;
				dac.buffer_right[(dac.readPosition+j) % DACBUFFER_SIZE] = 0;
			}
		}

		dac.readPosition = (dac.readPosition + n) % DACBUFFER_SIZE;
		dac.readPosition_float &= 0xffffffff;			/* only keep the fractional part */

		/* Upgrade adc->dac's buffer read pointer */
		crossbar.adc2dac_readBufferPosition_float += crossbar.frequence_ratio;
		n = crossbar.adc2dac_readBufferPosition_float >> 32;				/* number of samples to skip */
		crossbar.adc2dac_readBufferPosition = (crossbar.adc2dac_readBufferPosition + n) % DACBUFFER_SIZE;
		crossbar.adc2dac_readBufferPosition_float &= 0xffffffff;			/* only keep the fractional part */
	}
}


/**
 * display the Crossbar registers values (for debugger info command)
 */
void Crossbar_Info(FILE *fp, Uint32 dummy)
{
	char matrixDMA[5], matrixDAC[5], matrixDSP[5], matrixEXT[5];
	char frqDMA[11], frqDAC[11], frqDSP[11], frqEXT[11];
	char frqSTE[30], frq25Mhz[30], frq32Mhz[30];
	char dataSize[15];

	if (ConfigureParams.System.nMachineType != MACHINE_FALCON) {
		fprintf(fp, "Not Falcon - no Crossbar!\n");
		return;
	}

	fprintf(fp, "$FF8900.b : Sound DMA control                     : %02x\n", IoMem_ReadByte(0xff8900));
	fprintf(fp, "$FF8901.b : Sound DMA control                     : %02x\n", IoMem_ReadByte(0xff8901));
	fprintf(fp, "$FF8903.b : Frame Start High                      : %02x\n", IoMem_ReadByte(0xff8903));
	fprintf(fp, "$FF8905.b : Frame Start middle                    : %02x\n", IoMem_ReadByte(0xff8905));
	fprintf(fp, "$FF8907.b : Frame Start low                       : %02x\n", IoMem_ReadByte(0xff8907));
	fprintf(fp, "$FF8909.b : Frame Count High                      : %02x\n", IoMem_ReadByte(0xff8909));
	fprintf(fp, "$FF890B.b : Frame Count middle                    : %02x\n", IoMem_ReadByte(0xff890b));
	fprintf(fp, "$FF890D.b : Frame Count low                       : %02x\n", IoMem_ReadByte(0xff890d));
	fprintf(fp, "$FF890F.b : Frame End High                        : %02x\n", IoMem_ReadByte(0xff890f));
	fprintf(fp, "$FF8911.b : Frame End middle                      : %02x\n", IoMem_ReadByte(0xff8911));
	fprintf(fp, "$FF8913.b : Frame End low                         : %02x\n", IoMem_ReadByte(0xff8913));
	fprintf(fp, "\n");
	fprintf(fp, "$FF8920.b : Sound Mode Control                    : %02x\n", IoMem_ReadByte(0xff8920));
	fprintf(fp, "$FF8921.b : Sound Mode Control                    : %02x\n", IoMem_ReadByte(0xff8921));
	fprintf(fp, "$FF8930.w : DMA Crossbar Input Select Controller  : %04x\n", IoMem_ReadWord(0xff8930));
	fprintf(fp, "$FF8932.w : DMA Crossbar Output Select Controller : %04x\n", IoMem_ReadWord(0xff8932));
	fprintf(fp, "\n");
	fprintf(fp, "$FF8934.b : External Sync Frequency Divider       : %02x\n", IoMem_ReadByte(0xff8934));
	fprintf(fp, "$FF8935.b : Internal Sync Frequency Divider       : %02x\n", IoMem_ReadByte(0xff8935));
	fprintf(fp, "$FF8936.b : Record Track select                   : %02x\n", IoMem_ReadByte(0xff8936));
	fprintf(fp, "$FF8937.b : Codec Input Source                    : %02x\n", IoMem_ReadByte(0xff8937));
	fprintf(fp, "$FF8938.b : Codec ADC Input                       : %02x\n", IoMem_ReadByte(0xff8938));
	fprintf(fp, "$FF8939.b : Gain Settings Per Channel             : %02x\n", IoMem_ReadByte(0xff8939));
	fprintf(fp, "$FF893A.b : Attenuation Settings Per Channel      : %02x\n", IoMem_ReadByte(0xff893a));
	fprintf(fp, "$FF893C.w : Codec Status                          : %04x\n", IoMem_ReadWord(0xff893c));
	fprintf(fp, "$FF8940.w : GPIO Data Direction                   : %04x\n", IoMem_ReadWord(0xff8940));
	fprintf(fp, "$FF8942.w : GPIO Data                             : %04x\n", IoMem_ReadWord(0xff8942));
	fprintf(fp, "\n");

	/* DAC connexion */
	switch ((IoMem_ReadWord(0xff8932) >> 13) & 0x3) {
		case 0 :
			/* DAC connexion with DMA Playback */
			if ((IoMem_ReadWord(0xff8930) & 0x1) == 1)
				strcpy(matrixDAC, "OOXO");
			else
				strcpy(matrixDAC, "OOHO");
			break;
		case 1 :
			/* DAC connexion with DSP Transmit */
			if ((IoMem_ReadWord(0xff8930) & 0x10) == 0x10)
				strcpy(matrixDAC, "OXOO");
			else
				strcpy(matrixDAC, "OHOO");
			break;
		case 2 :
			/* DAC connexion with External Input */
			if ((IoMem_ReadWord(0xff8930) & 0x100) == 0x100)
				strcpy(matrixDAC, "XOOO");
			else
				strcpy(matrixDAC, "HOOO");
			break;
		case 3 :
			/* DAC connexion with ADC */
			strcpy(matrixDAC, "OOOX");
			break;
	}

	/* DMA connexion */
	switch (IoMem_ReadWord(0xff8932) & 0x7) {
		case 0 : strcpy(matrixDMA, "OOHO"); break;
		case 1 : strcpy(matrixDMA, "OOXO"); break;
		case 2 : strcpy(matrixDMA, "OHOO"); break;
		case 3 : strcpy(matrixDMA, "OXOO"); break;
		case 4 : strcpy(matrixDMA, "HOOO"); break;
		case 5 : strcpy(matrixDMA, "XOOO"); break;
		case 6 : strcpy(matrixDMA, "OOOH"); break;
		case 7 : strcpy(matrixDMA, "OOOX"); break;
	}

	/* DSP connexion */
	switch ((IoMem_ReadWord(0xff8932) >> 4) & 0x7) {
		case 0 : strcpy(matrixDSP, "OOHO"); break;
		case 1 : strcpy(matrixDSP, "OOXO"); break;
		case 2 : strcpy(matrixDSP, "OHOO"); break;
		case 3 : strcpy(matrixDSP, "OXOO"); break;
		case 4 : strcpy(matrixDSP, "HOOO"); break;
		case 5 : strcpy(matrixDSP, "XOOO"); break;
		case 6 : strcpy(matrixDSP, "OOOH"); break;
		case 7 : strcpy(matrixDSP, "OOOX"); break;
	}

	/* External input connexion */
	switch ((IoMem_ReadWord(0xff8932) >> 8) & 0x7) {
		case 0 : strcpy(matrixEXT, "OOHO"); break;
		case 1 : strcpy(matrixEXT, "OOXO"); break;
		case 2 : strcpy(matrixEXT, "OHOO"); break;
		case 3 : strcpy(matrixEXT, "OXOO"); break;
		case 4 : strcpy(matrixEXT, "HOOO"); break;
		case 5 : strcpy(matrixEXT, "XOOO"); break;
		case 6 : strcpy(matrixEXT, "OOOH"); break;
		case 7 : strcpy(matrixEXT, "OOOX"); break;
	}

	if ((IoMem_ReadByte(0xff8935) & 0xf) == 0) {
		strcpy(frqDSP, "(STe Freq)");
		strcpy(frqDMA, "(STe Freq)");
		strcpy(frqEXT, "(STe Freq)");
		strcpy(frqDAC, "(STe Freq)");
	}
	else {
		/* DSP Clock */
		switch ((IoMem_ReadWord(0xff8930) >> 5) & 0x3) {
			case 0: strcpy(frqDSP, " (25 Mhz) "); break;
			case 1: strcpy(frqDSP, "(External)"); break;
			case 2: strcpy(frqDSP, " (32 Mhz) "); break;
			default:  strcpy(frqDSP, "undefined "); break;
		}

		/* DMA Clock */
		switch ((IoMem_ReadWord(0xff8930) >> 1) & 0x3) {
			case 0: strcpy(frqDMA, " (25 Mhz) "); break;
			case 1: strcpy(frqDMA, "(External)"); break;
			case 2: strcpy(frqDMA, " (32 Mhz) "); break;
			default:  strcpy(frqDMA, "undefined "); break;
		}

		/* External Clock */
		switch ((IoMem_ReadWord(0xff8930) >> 9) & 0x3) {
			case 0: strcpy(frqEXT, " (25 Mhz) "); break;
			case 1: strcpy(frqEXT, "(External)"); break;
			case 2: strcpy(frqEXT, " (32 Mhz) "); break;
			default:  strcpy(frqEXT, "undefined "); break;
		}

		/* DAC Clock */
		strcpy(frqDAC, " (25 Mhz) ");
	}

	/* data size */
	switch ((IoMem_ReadByte(0xff8921) >> 6) & 0x3) {
		case 0: strcpy (dataSize, "8 bits stereo"); break;
		case 1: strcpy (dataSize, "16 bits stereo"); break;
		case 2: strcpy (dataSize, "8 bits mono"); break;
		default: strcpy (dataSize, "undefined"); break;
	}

	/* STE, 25Mhz and 32 Mhz sound frequencies */
	if ((IoMem_ReadByte(0xff8935) & 0xf) == 0) {
		sprintf(frqSTE, "Ste Freq    : %d Khz", Ste_SampleRates[IoMem_ReadByte(0xff8921) & 0x3]);
		strcpy (frq25Mhz, "25 Mhz Freq : - Khz");
		strcpy (frq32Mhz, "32 Mzh Freq : - Khz");
	}
	else {
		strcpy (frqSTE, "Ste Freq    : - Khz");
		sprintf(frq25Mhz, "25 Mhz Freq : %d Khz", Falcon_SampleRates_25Mhz[(IoMem_ReadByte(0xff8935) & 0xf) - 1]);
		sprintf(frq32Mhz, "32 Mzh Freq : %d Khz", Falcon_SampleRates_32Mhz[(IoMem_ReadByte(0xff8935) & 0xf) - 1]);
	}

	/* Display the crossbar Matrix */
	fprintf(fp, "           INPUT\n");
	fprintf(fp, "External Imp  ---%c------%c------%c------%c\n", matrixDAC[0], matrixDMA[0], matrixDSP[0], matrixEXT[0]);
	fprintf(fp, "%s       |      |      |      |    O = no connexion\n", frqEXT);
	fprintf(fp, "                 |      |      |      |    X = connexion\n");
	fprintf(fp, "Dsp Transmit  ---%c------%c------%c------%c    H = Handshake connexion\n", matrixDAC[1], matrixDMA[1], matrixDSP[1], matrixEXT[1]);
	fprintf(fp, "%s       |      |      |      |\n", frqDSP);
	fprintf(fp, "                 |      |      |      |    %s\n", dataSize);
	fprintf(fp, "DMA PlayBack  ---%c------%c------%c------%c\n", matrixDAC[2], matrixDMA[2], matrixDSP[2], matrixEXT[2]);
	fprintf(fp, "%s       |      |      |      |    Sound Freq :\n", frqDMA);
	fprintf(fp, "                 |      |      |      |      %s\n", frqSTE);
	fprintf(fp, "ADC           ---%c------%c------%c------%c      %s\n", matrixDAC[3], matrixDMA[3], matrixDSP[3], matrixEXT[3], frq25Mhz);
	fprintf(fp, "%s       |      |      |      |      %s\n", frqDAC, frq32Mhz);
	fprintf(fp, "                 |      |      |      |\n");
	fprintf(fp, "                DAC    DMA    DSP   External     OUTPUT\n");
	fprintf(fp, "                     Record  Record   Out\n");
	fprintf(fp, "\n");
}

