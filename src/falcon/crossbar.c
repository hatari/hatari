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


Additional notes [NP] :
  Some registers are not fully described in Atari's documentation.
  The following results were measured on a real Falcon :

  - Once audio DMA is playing or recording, it is not possible to change the loop mode by writing at $FF8901.
    If play mode was started with loop mode, clearing bit 1 at $FF8901 will have no effect, loop mode will remain active.
    It's only when play or record are stopped then started again that the loop bit will be taken into account.

  - SOUNDINT/SNDINT interrupt description is not fully accurate in the "Falcon030 Service Guide, Oct 1992" :
      "- SINT/SNDINT : This output is low when sound DMA is active and high otherwise.
	It will make a high to low transition at the beginning of a frame of sound data
	and a low to high transition at the end of frame. This signal can be programmed
	to come from either the record or play channels
      - SCNT/SOUNDINT : This output is similar to SINT/SNDINT but wider."

    As measured on a real Falcon, value of SOUNDINT/SNDINT depends on the content of $FF8900
     - If bit 0 is cleared at $FF8900 then SNDINT will always be 1, whether DMA is playing or not. This means
       that when reading GPIP bit 7 at $FFFA01 it will always be 1 and there's no way to tell if DMA play is active or not.
     - If bit 0 is set at $FF8900 the SNDINT will be 0 when DMA is playing and 1 when DMA is idle
    Similar behaviour applies for Timer A Input bit and for record mode.

    SNDINT is connected to MFP's GPIP7 and SOUNDINT is connected to MFP's TAI.
    By setting the corresponding bit in AER (bit 7 for GPIP7 and bit 4 for TAI) it is then possible to have an interrupt
    that triggers on start of frame (when AER bit=0) or on end of frame (when AER bit=1)

    In loop mode, DMA signal will briefly goes from active to idle then active again ; this transition to "idle" allows
    to have an interrupt at the end of each sample or at the start of the next sample, depending on AER value.

    See Crossbar_Update_DMA_Sound_Line() for more details

  */

const char crossbar_fileid[] = "Hatari Crossbar.c";

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "cycles.h"
#include "cycInt.h"
#include "dmaSnd.h"
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
#include "video.h"



#define DACBUFFER_SIZE    2048
#define DECIMAL_PRECISION 65536


/* Values for SOUNDINT DMA signal : 0/LOW=DMA active  1/HIGH=DMA idle */
/* SNDINT use the same values as SOUNDINT, so we use the same define's */
#define CROSSBAR_SOUNDINT_STATE_LOW	0
#define CROSSBAR_SOUNDINT_STATE_HIGH	1


/* Crossbar internal functions */
static int  Crossbar_DetectSampleRate(uint16_t clock);
static void Crossbar_Start_InterruptHandler_25Mhz(void);
static void Crossbar_Start_InterruptHandler_32Mhz(void);

/* Dma_Play sound functions */
static void Crossbar_setDmaPlay_Settings(void);
static void Crossbar_Process_DMAPlay_Transfer(void);

/* Dma_Record sound functions */
static void Crossbar_setDmaRecord_Settings(void);
void Crossbar_SendDataToDmaRecord(int16_t value);
static void Crossbar_Process_DMARecord_HandshakeMode(void);

/* Dsp Xmit functions */
static void Crossbar_SendDataToDspReceive(uint32_t value, uint16_t frame);
static void Crossbar_Process_DSPXmit_Transfer(void);

/* DAC functions */
static void Crossbar_SendDataToDAC(int16_t value, uint16_t sample_pos);

/* ADC functions */
static void Crossbar_Process_ADCXmit_Transfer(void);

/* external data used by the MFP */
uint16_t nCbar_DmaSoundControl;

/* internal data */

/* dB = 20log(gain)  :  gain = antilog(dB/20)                                  */
/* Table gain values = (int)(powf(10.0, dB/20.0)*65536.0 + 0.5)  1.5dB steps   */

/* Values for Codec's ADC volume control (* DECIMAL_PRECISION) */
/* PSG must be amplified by 2.66.. before mixing with crossbar */
/* The ADC table values are multiplied by 2'2/3 and divided    */
/* by 4 (later multiplied by 4) eg 43691 = 65536 * 2.66.. / 4.0 */
static const uint16_t Crossbar_ADC_volume_table[16] =
{
	3276,   3894,   4628,   5500,   6537,   7769,   9234,   10975,
	13043,  15502,  18424,  21897,  26025,  30931,  36761,  43691
};

/* Values for Codec's DAC volume control (* DECIMAL_PRECISION) */
static const uint16_t Crossbar_DAC_volume_table[16] =
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
	uint32_t frameStartAddr;		/* Sound frame start */
	uint32_t frameEndAddr;		/* Sound frame end */
	uint32_t frameCounter;		/* Counter in current sound frame */
	uint32_t frameLen;		/* TODO: Remove when it's ok to break memory snapshots (was: Length of the frame) */
	uint32_t isRunning;		/* Is Playing / Recording ? */
	uint32_t loopMode;		/* Loop mode enabled ? */
	uint32_t currentFrame;		/* Current Frame Played / Recorded (in stereo, 2 frames = 1 track) */
	uint32_t timerA_int;		/* Timer A interrupt at end of Play / Record ? */
	uint32_t mfp15_int;		/* MFP-15 interrupt at end of Play / Record ? */
	uint32_t isConnectedToCodec;
	uint32_t isConnectedToDsp;
	uint32_t isConnectedToDspInHandShakeMode;
	uint32_t isConnectedToDma;
	uint32_t handshakeMode_Frame;	/* state of the frame in handshake mode */
	uint32_t handshakeMode_masterClk;	/* 0 = crossbar master clock ; 1 = DSP master clock */
};

struct crossbar_s {
	uint32_t dmaSelected;		/* 1 = DMA Record; 0 = DMA Play */
	uint32_t playTracks;		/* number of tracks played */
	uint32_t recordTracks;		/* number of tracks recorded */
	uint16_t track_monitored;		/* track monitored by the DAC */
	uint32_t is16Bits;		/* 0 = 8 bits; 1 = 16 bits */
	uint32_t isStereo;		/* 0 = mono; 1 = stereo */
	uint32_t steFreq;			/* from 0 (6258 Hz) to 3 (50066 Hz) */
	uint32_t isInSteFreqMode;		/* 0 = Falcon frequency mode ; 1 = Ste frequency mode */
	uint32_t int_freq_divider;	/* internal frequency divider */
	uint32_t isDacMuted;		/* 0 = DAC is running; 1 = DAC is muted */
	uint32_t dspXmit_freq;		/* 0 = 25 Mhz ; 1 = external clock ; 2 = 32 Mhz */
	uint32_t dmaPlay_freq;		/* 0 = 25 Mhz ; 1 = external clock ; 2 = 32 Mhz */
	uint16_t codecInputSource;	/* codec input source */
	uint16_t codecAdcInput;		/* codec ADC input */
	uint16_t gainSettingLeft;		/* Left channel gain for ADC */
	uint16_t gainSettingRight;	/* Right channel gain for ADC */
	uint16_t attenuationSettingLeft;	/* Left channel attenuation for DAC */
	uint16_t attenuationSettingRight;	/* Right channel attenuation for DAC */
	uint16_t microphone_ADC_is_started;

	uint32_t clock25_cycles;		/* cycles for 25 Mzh interrupt */
	uint32_t clock25_cycles_decimal;  /* decimal part of cycles counter for 25 Mzh interrupt (*DECIMAL_PRECISION) */
	uint32_t clock25_cycles_counter;  /* Cycle counter for 25 Mhz interrupts */
	uint32_t pendingCyclesOver25;	/* Number of delayed cycles for the interrupt */
	uint32_t clock32_cycles;		/* cycles for 32 Mzh interrupt */
	uint32_t clock32_cycles_decimal;  /* decimal part of cycles counter for 32 Mzh interrupt (*DECIMAL_PRECISION) */
	uint32_t clock32_cycles_counter;  /* Cycle counter for 32 Mhz interrupts */
	uint32_t pendingCyclesOver32;	/* Number of delayed cycles for the interrupt */
	int64_t frequence_ratio;		/* Ratio between host computer's sound frequency and hatari's sound frequency */
	int64_t frequence_ratio2;	/* Ratio between hatari's sound frequency and host computer's sound frequency */

	uint32_t dmaPlay_CurrentFrameStart;   /* current DmaPlay Frame start ($ff8903 $ff8905 $ff8907) */
	uint32_t dmaPlay_CurrentFrameCount;   /* current DmaRecord Frame start ($ff8903 $ff8905 $ff8907) */
	uint32_t dmaPlay_CurrentFrameEnd;     /* current DmaRecord Frame start ($ff8903 $ff8905 $ff8907) */
	uint32_t dmaRecord_CurrentFrameStart; /* current DmaRecord Frame end ($ff890f $ff8911 $ff8913) */
	uint32_t dmaRecord_CurrentFrameCount; /* current DmaRecord Frame start ($ff8903 $ff8905 $ff8907) */
	uint32_t dmaRecord_CurrentFrameEnd;   /* current DmaRecord Frame end ($ff890f $ff8911 $ff8913) */
	uint32_t adc2dac_readBufferPosition;  /* read position for direct adc->dac transfer */
	int64_t adc2dac_readBufferPosition_float; /* float value of read position for direct adc->dac transfer index */

	uint32_t save_special_transfer;		/* Used in a special undocumented transfer mode (dsp sent is not in handshake mode and dsp receive is in handshake mode) */

	uint8_t  SNDINT_Signal;		/* Value of the SNDINT signal (connected to MFP's GPIP7) */
	uint8_t  SOUNDINT_Signal;		/* Value of the SOUNDINT signal (connected to MFP's Timer A input) */
};

struct codec_s {
	int16_t buffer_left[DACBUFFER_SIZE];
	int16_t buffer_right[DACBUFFER_SIZE];
	int64_t readPosition_float;
	uint32_t readPosition;
	uint32_t writePosition;
	uint32_t isConnectedToCodec;
	uint32_t isConnectedToDsp;
	uint32_t isConnectedToDma;
	uint32_t wordCount;
};

struct dsp_s {
	uint32_t isTristated;		/* 0 = DSP is not tristated; 1 = DSP is tristated */
	uint32_t isInHandshakeMode;	/* 0 = not in handshake mode; 1 = in handshake mode */
	uint32_t isConnectedToCodec;
	uint32_t isConnectedToDsp;
	uint32_t isConnectedToDma;
	uint32_t wordCount;		/* count number of words received from DSP transmitter (for TX frame computing) */
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
	dmaPlay.handshakeMode_masterClk = 0;
	dmaRecord.isRunning = 0;
	dmaRecord.loopMode = 0;
	dmaRecord.currentFrame = 0;
	dmaRecord.isConnectedToDspInHandShakeMode = 0;
	dmaRecord.handshakeMode_Frame = 0;
	dmaRecord.handshakeMode_masterClk = 0;

	/* DMA stopped, force SNDINT/SOUNDINT to 1/HIGH (idle) */
	crossbar.SNDINT_Signal = MFP_GPIP_STATE_HIGH;
	crossbar.SOUNDINT_Signal = MFP_GPIP_STATE_HIGH;
	MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE7 , crossbar.SNDINT_Signal );
	MFP_TimerA_Set_Line_Input ( pMFP_Main , crossbar.SOUNDINT_Signal );


	/* DAC inits */
	memset(dac.buffer_left, 0, sizeof(dac.buffer_left));
	memset(dac.buffer_right, 0, sizeof(dac.buffer_right));
	dac.readPosition_float = 0;
	dac.readPosition = 0;
	dac.writePosition = (dac.readPosition+DACBUFFER_SIZE/2)%DACBUFFER_SIZE;

	/* ADC inits */
	memset(adc.buffer_left, 0, sizeof(adc.buffer_left));
	memset(adc.buffer_right, 0, sizeof(adc.buffer_right));
	adc.readPosition_float = 0;
	adc.readPosition = 0;
	adc.writePosition = (adc.readPosition+DACBUFFER_SIZE/2)%DACBUFFER_SIZE;

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

	/* After restoring, update the clock/freq counters */
	if ( !bSave )
		Crossbar_Recalculate_Clocks_Cycles();
}


/*----------------------------------------------------------------------*/
/*	Hardware I/O functions						*/
/*----------------------------------------------------------------------*/

/*-----------------------------------------------------------------------*/
/**
 * Update the value of the SNDINT/SOUNDINT lines
 *  - SNDINT is the same line as SINT on the DMA chip and is connected to MFP's GPIP7
 *  - SOUNDINT is the same line as SCNT on the DMA chip and is connected to MFP's TAI
 *
 * Description from the "Falcon030 Service Guide, Oct 1992" :
 *  - SINT/SNDINT : This output is low when sound DMA is active and high otherwise.
 *    It will make a high to low transition at the beginning of a frame of sound data
 *    and a low to high transition at the end of frame. This signal can be programmed
 *    to come from either the record or play channels
 *  - SCNT/SOUNDINT : This output is similar to SINT/SNDINT but wider.
 *
 * Depending on the transition and MFP's AER, this can trigger MFP interrupt for Timer A or for GPIP7
 *  - Bit is set to 0/LOW when dma sound is playing / recording
 *  - Bit is set to 1/HIGH when dma sound is idle
 *
 * As measured on a real Falcon, if corresponding bit is set in FF8900 then SNDINT/SOUNDINT
 * will be updated to HIGH or LOW when idle or active
 * If bit is clear in FF8900 then SNDINT/SOUNDINT will always remain HIGH, even when
 * DMA is playing or recording.
 *
 * Timer A input is using AER bit 4, GPIP7 is using AER bit 7
 *
 * Under default TOS configuration, AER bit4=0, so Timer A input will trigger
 * an interrupt at the start of a frame.
 *
 * This is different (opposite) from the STE/TT, where bit is set to 1/HIGH when playing
 * and 0/LOW when idle. So, under default TOS configuration STE/TT will trigger
 * Timer A interrupt at the end of a frame.
 *
 */

static void Crossbar_Update_DMA_Sound_Line ( bool PlayMode , uint8_t Bit )
{
	bool	SetGPIP7 , SetTAI;

	if ( PlayMode )
	{
		SetGPIP7 = dmaPlay.mfp15_int;
		SetTAI   = dmaPlay.timerA_int;
	}
	else
	{
		SetGPIP7 = dmaRecord.mfp15_int;
		SetTAI   = dmaRecord.timerA_int;
	}

	/* If mfp15_int is set we use the value of Bit, else line is always high */
	if ( SetGPIP7 )
		crossbar.SNDINT_Signal = Bit;
	else
		crossbar.SNDINT_Signal = CROSSBAR_SOUNDINT_STATE_HIGH;

	/* If timerA_int is set we use the value of Bit, else line is always high */
	if ( SetTAI )
		crossbar.SOUNDINT_Signal = Bit;
	else
		crossbar.SOUNDINT_Signal = CROSSBAR_SOUNDINT_STATE_HIGH;


	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : MFP GPIP7 set bit=%d VBL=%d HBL=%d\n", crossbar.SNDINT_Signal , nVBLs , nHBL);
	MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE7 , crossbar.SNDINT_Signal );

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : MFP TAI set bit=%d VBL=%d HBL=%d\n", crossbar.SOUNDINT_Signal , nVBLs , nHBL);
	MFP_TimerA_Set_Line_Input ( pMFP_Main , crossbar.SOUNDINT_Signal );			/* Update events count / interrupt for timer A if needed */
}

static void Crossbar_Play_Update_DMA_Sound_Line_Active ( void )
{
	Crossbar_Update_DMA_Sound_Line ( true , CROSSBAR_SOUNDINT_STATE_LOW );
}


static void Crossbar_Play_Update_DMA_Sound_Line_Idle ( void )
{
	Crossbar_Update_DMA_Sound_Line ( true , CROSSBAR_SOUNDINT_STATE_HIGH );
}


static void Crossbar_Record_Update_DMA_Sound_Line_Active ( void )
{
	Crossbar_Update_DMA_Sound_Line ( false , CROSSBAR_SOUNDINT_STATE_LOW );
}


static void Crossbar_Record_Update_DMA_Sound_Line_Idle ( void )
{
	Crossbar_Update_DMA_Sound_Line ( false , CROSSBAR_SOUNDINT_STATE_HIGH );
}



/*-----------------------------------------------------------------------*/
/**
 * Return the value of the SNDINT line, used to update MFP's GPIP7
 */
uint8_t Crossbar_Get_SNDINT_Line (void)
{
	return crossbar.SNDINT_Signal;
}


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
	uint16_t microwire = IoMem_ReadWord(0xff8924);
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
	uint16_t microwire = IoMem_ReadWord(0xff8924);

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
	uint8_t dmaCtrl = IoMem_ReadByte(0xff8900);

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
	uint8_t sndCtrl = IoMem_ReadByte(0xff8901);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8901 (additional Sound DMA control) write: 0x%02x VBL=%d HBL=%d\n", sndCtrl, nVBLs , nHBL );

	crossbar.dmaSelected = (sndCtrl & 0x80) >> 7;

	/* DMA Play mode */
	if ((dmaPlay.isRunning == 0) && (sndCtrl & CROSSBAR_SNDCTRL_PLAY))
	{
		/* Turning on DMA Play sound emulation */
		dmaPlay.isRunning = 1;
		dmaPlay.loopMode = (sndCtrl & 0x2) >> 1;
		nCbar_DmaSoundControl = sndCtrl;
		Crossbar_setDmaPlay_Settings();
	}
	else if (dmaPlay.isRunning && ((sndCtrl & CROSSBAR_SNDCTRL_PLAY) == 0))
	{
		/* Create samples up until this point with current values */
		Sound_Update ( Cycles_GetClockCounterOnWriteAccess() );

		/* Turning off DMA play sound emulation */
		dmaPlay.isRunning = 0;
		dmaPlay.loopMode = 0;
		nCbar_DmaSoundControl = sndCtrl;
		Crossbar_Play_Update_DMA_Sound_Line_Idle ();			/* 1/HIGH=dma sound play idle */
	}

	/* DMA Record mode */
	if ((dmaRecord.isRunning == 0) && (sndCtrl & CROSSBAR_SNDCTRL_RECORD))
	{
		/* Turning on DMA record sound emulation */
		dmaRecord.isRunning = 1;
		dmaRecord.loopMode = (sndCtrl & 0x20) >> 5;
		nCbar_DmaSoundControl = sndCtrl;
		Crossbar_setDmaRecord_Settings();
	}
	else if (dmaRecord.isRunning && ((sndCtrl & CROSSBAR_SNDCTRL_RECORD) == 0))
	{
		/* Turning off DMA record sound emulation */
		dmaRecord.isRunning = 0;
		dmaRecord.loopMode = 0;
		nCbar_DmaSoundControl = sndCtrl;
		Crossbar_Record_Update_DMA_Sound_Line_Idle ();			/* O/LOW=dma sound record idle */
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
	uint32_t addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8903 (Sound frame start high) write: 0x%02x VBL=%d HBL=%d\n", IoMem_ReadByte(0xff8903) , nVBLs , nHBL);

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
	uint32_t addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8905 (Sound frame start med) write: 0x%02x VBL=%d HBL=%d\n", IoMem_ReadByte(0xff8905) , nVBLs , nHBL);

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
	uint32_t addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8907 (Sound frame start low) write: 0x%02x VBL=%d HBL=%d\n", IoMem_ReadByte(0xff8907) , nVBLs , nHBL);

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
	uint32_t addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8909 (Sound frame count high) write: 0x%02x VBL=%d HBL=%d\n", IoMem_ReadByte(0xff8909) , nVBLs , nHBL);

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
	uint32_t addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff890b (Sound frame count med) write: 0x%02x VBL=%d HBL=%d\n", IoMem_ReadByte(0xff890b) , nVBLs , nHBL);

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
	uint32_t addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff890d (Sound frame count low) write: 0x%02x VBL=%d HBL=%d\n", IoMem_ReadByte(0xff890d) , nVBLs , nHBL);

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
	uint32_t addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff890f (Sound frame end high) write: 0x%02x VBL=%d HBL=%d\n", IoMem_ReadByte(0xff890f) , nVBLs , nHBL);

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
	uint32_t addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8911 (Sound frame end med) write: 0x%02x VBL=%d HBL=%d\n", IoMem_ReadByte(0xff8911) , nVBLs , nHBL);

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
	uint32_t addr;

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8913 (Sound frame end low) write: 0x%02x VBL=%d HBL=%d\n", IoMem_ReadByte(0xff8913) , nVBLs , nHBL);

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
	uint8_t sndCtrl = IoMem_ReadByte(0xff8920);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8920 (sound mode control) write: 0x%02x\n", sndCtrl);

	crossbar.playTracks = (sndCtrl & 3) + 1;
	crossbar.track_monitored = (sndCtrl & 30) >> 4;
}

/**
 * Write word to sound mode register (0xff8921).
 */
void Crossbar_SoundModeCtrl_WriteByte(void)
{
	uint8_t sndCtrl = IoMem_ReadByte(0xff8921);

	LOG_TRACE(TRACE_CROSSBAR, "crossbar : $ff8921 (additional sound mode control) write: 0x%02x\n", sndCtrl);

	crossbar.is16Bits = (sndCtrl & 0x40) >> 6;
	crossbar.isStereo = 1 - ((sndCtrl & 0x80) >> 7);
	crossbar.steFreq = sndCtrl & 0x3;

	Crossbar_Recalculate_Clocks_Cycles();
}

/**
 * Write word to Falcon Crossbar source controller (0xff8930).
	Source: A/D Converter                 BIT 15 14 13 12
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
	uint16_t nCbSrc = IoMem_ReadWord(0xff8930);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8930 (source device) write: 0x%04x\n", nCbSrc);

	dspXmit.isTristated = 1 - ((nCbSrc >> 7) & 0x1);
	dspXmit.isInHandshakeMode = 1 - ((nCbSrc >> 4) & 0x1);

	crossbar.dspXmit_freq = (nCbSrc >> 5) & 0x3;
	crossbar.dmaPlay_freq = (nCbSrc >> 1) & 0x3;
}

/**
 * Write word to Falcon Crossbar destination controller (0xff8932).
	Source: D/A Converter                 BIT 15 14 13 12
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
	uint16_t destCtrl = IoMem_ReadWord(0xff8932);

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
	dmaPlay.handshakeMode_masterClk = 0;

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
	uint8_t clkDiv = IoMem_ReadByte(0xff8935);

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
	uint8_t recTrack = IoMem_ReadByte(0xff8936);

	LOG_TRACE(TRACE_CROSSBAR, "Crossbar : $ff8936 (record track select) write: 0x%02x\n", recTrack);

	crossbar.recordTracks = recTrack & 3;
}

/**
 * Write byte to CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D converter
 */
void Crossbar_CodecInput_WriteByte(void)
{
	uint8_t inputSource = IoMem_ReadByte(0xff8937);

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
	uint8_t input = IoMem_ReadByte(0xff8938);

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
	uint8_t amplification = IoMem_ReadByte(0xff8939);

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
	uint16_t reduction = IoMem_ReadWord(0xff893a);

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
	/* Take nCpuFreqShift into account to keep a constant sound rate at all cpu freq */
	cyclesClk = ((double)( MachineClocks.CPU_Freq_Emul ) / Crossbar_DetectSampleRate(25)) / (double)(crossbar.playTracks) / 2.0;

	crossbar.clock25_cycles = (int)(cyclesClk);
	crossbar.clock25_cycles_decimal = (int)((cyclesClk - (double)(crossbar.clock25_cycles)) * (double)DECIMAL_PRECISION);
//fprintf ( stderr , "freq_25=%d cyclesclk=%f cyc_int=%d cyc_float=%d\n",Crossbar_DetectSampleRate(25), cyclesClk, crossbar.clock25_cycles, crossbar.clock25_cycles_decimal);

	/* Calculate 32 Mhz clock cycles */
	/* Take nCpuFreqShift into account to keep a constant sound rate at all cpu freq */
	cyclesClk = ((double)( MachineClocks.CPU_Freq_Emul ) / Crossbar_DetectSampleRate(32)) / (double)(crossbar.playTracks) / 2.0;

	crossbar.clock32_cycles = (int)(cyclesClk);
	crossbar.clock32_cycles_decimal = (int)((cyclesClk - (double)(crossbar.clock32_cycles)) * (double)DECIMAL_PRECISION);
//fprintf ( stderr , "freq_32=%d cyclesclk=%f cyc_int=%d cyc_float=%d\n",Crossbar_DetectSampleRate(32), cyclesClk, crossbar.clock25_cycles, crossbar.clock25_cycles_decimal);

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

	// Ensure dac.writePosition is correctly set based on current dac.readPosition
	// -> force dac.wordCount=0 to update dac.writePosition on next call to Crossbar_GenerateSamples()
	dac.wordCount = 0;
}

/**
 * 	Compute Ratio between host computer sound frequency and Hatari's DAC sound frequency and
 * 	ratio between hatari's DAC sound frequency and host's sound frequency.
 * 	Both values use << 32 to simulate floating point precision
 *	Can be called by audio.c if a sound frequency value is changed in the parameter GUI.
 */
void Crossbar_Compute_Ratio(void)
{
	crossbar.frequence_ratio = ( ((int64_t)Crossbar_DetectSampleRate(25)) << 32) / nAudioFrequency;
	crossbar.frequence_ratio2 = ( ((int64_t)nAudioFrequency) << 32) / Crossbar_DetectSampleRate(25);
}

/**
 * Detect sample rate frequency
 *    clock : value of the internal clock (25 or 32).
 */
static int Crossbar_DetectSampleRate(uint16_t clock)
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
	uint32_t cycles_25;

//fprintf ( stderr , "start int25 %x %x %x %x\n" , crossbar.clock25_cycles, crossbar.clock25_cycles_counter, crossbar.clock25_cycles_decimal, crossbar.pendingCyclesOver25 );
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
	uint32_t cycles_32;

//fprintf ( stderr , "start int32 %x %x %x %x\n" , crossbar.clock32_cycles, crossbar.clock32_cycles_counter, crossbar.clock32_cycles_decimal, crossbar.pendingCyclesOver32 );
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
//fprintf ( stderr , "int25 %x\n" , crossbar.pendingCyclesOver25 );
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
//fprintf ( stderr , "int32 %x\n" , crossbar.pendingCyclesOver32 );
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
	uint16_t frame=0;
	int32_t data;

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
static void Crossbar_SendDataToDspReceive(uint32_t value, uint16_t frame)
{
	/* Verify that DSP IN is not tristated */
	if (dspReceive.isTristated) {
		return;
	}

	/* Send sample to DSP receive */
	DSP_SsiWriteRxValue(value);

	/* Send the frame status to the DSP SSI receive */
	/* only in non handshake mode */
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
	/* DMA settings */
	dmaPlay.frameStartAddr = crossbar.dmaPlay_CurrentFrameStart;
	dmaPlay.frameEndAddr = crossbar.dmaPlay_CurrentFrameEnd;
	dmaPlay.frameLen = dmaPlay.frameEndAddr - dmaPlay.frameStartAddr;  /* TODO: Remove later */
//	dmaPlay.frameCounter = crossbar.dmaPlay_CurrentFrameCount - crossbar.dmaPlay_CurrentFrameStart;
	dmaPlay.frameCounter = 0;

	if (dmaPlay.frameEndAddr <= dmaPlay.frameStartAddr)
	{
		Log_Printf(LOG_WARN, "crossbar DMA Play: Illegal buffer size (from 0x%06x to 0x%06x)\n",
		          dmaPlay.frameStartAddr, dmaPlay.frameEndAddr);
	}

	/* DMA sound play : update SNDINT */
	Crossbar_Play_Update_DMA_Sound_Line_Active ();				/* 0/LOW=dma sound play ON */
}

/**
 * Process DMA Play transfer to crossbar
 */
static void Crossbar_Process_DMAPlay_Transfer(void)
{
	uint16_t temp, increment_frame;
	int16_t value, eightBits;
	uint32_t nFramePos;
	uint8_t  dmaCtrlReg;

	/* if DMA play is not running, return */
	if (dmaPlay.isRunning == 0)
		return;

	nFramePos = (dmaPlay.frameStartAddr + dmaPlay.frameCounter) & (DMA_MaskAddressHigh() << 16 | 0xffff);
	increment_frame = 0;

	/* 16 bits stereo mode ? */
	if (crossbar.is16Bits) {
		eightBits = 1;
		value = (int16_t)STMemory_DMA_ReadWord(nFramePos);
		increment_frame = 2;
	}
	/* 8 bits stereo ? */
	else if (crossbar.isStereo) {
		eightBits = 64;
		value = (int8_t)STMemory_DMA_ReadByte(nFramePos);
		increment_frame = 1;
	}
	/* 8 bits mono */
	else {
		eightBits = 64;
		value = (int8_t)STMemory_DMA_ReadByte(nFramePos);
		if ((dmaPlay.currentFrame & 1) == 0) {
			increment_frame = 1;
		}
	}

//fprintf ( stderr , "cbar %x %x %x\n" , dmaPlay.frameCounter , value , increment_frame );
//	if (dmaPlay.isConnectedToDspInHandShakeMode && dmaPlay.handshakeMode_Frame != 0) {
	if (dmaPlay.isConnectedToDspInHandShakeMode == 1 && dmaPlay.handshakeMode_masterClk == 1) {
		/* Handshake mode */
		if (dmaPlay.handshakeMode_Frame == 0)
			return;

		dmaPlay.frameCounter += increment_frame;

		/* Special undocumented transfer mode :
		   When DMA Play --> DSP Receive is in HandShake mode at 32 Mhz,
		   data are shifted 2 bits on the left after the transfer.
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
	if (dmaPlay.frameStartAddr + dmaPlay.frameCounter >= dmaPlay.frameEndAddr)
	{
		/* DMA sound idle : update SNDINT */
		Crossbar_Play_Update_DMA_Sound_Line_Idle ();				/* 1/HIGH=dma sound play idle */

		if (dmaPlay.loopMode) {
			Crossbar_setDmaPlay_Settings();				/* start a new frame */
		}
		else {
//fprintf ( stderr , "cbar %x %x %x end\n" , dmaPlay.frameStartAddr , dmaPlay.frameCounter , dmaPlay.frameEndAddr );
			/* Create samples up until this point with current values */
			Sound_Update ( CyclesGlobalClockCounter );

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
	dmaPlay.handshakeMode_masterClk = 1;
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
	/* DMA settings */
	dmaRecord.frameStartAddr = crossbar.dmaRecord_CurrentFrameStart;
	dmaRecord.frameEndAddr = crossbar.dmaRecord_CurrentFrameEnd;
	dmaRecord.frameLen = dmaRecord.frameEndAddr - dmaRecord.frameStartAddr;  /* TODO: Remove later */
//	dmaRecord.frameCounter = crossbar.dmaRecord_CurrentFrameCount - crossbar.dmaRecord_CurrentFrameStart;
	dmaRecord.frameCounter = 0;

	if (dmaRecord.frameEndAddr <= dmaRecord.frameStartAddr) {
		Log_Printf(LOG_WARN, "crossbar DMA Record: Illegal buffer size (from 0x%06x to 0x%06x)\n",
		          dmaRecord.frameStartAddr, dmaRecord.frameEndAddr);
	}

	/* DMA sound record : update SNDINT */
	Crossbar_Record_Update_DMA_Sound_Line_Active ();			/* 0/LOW=dma sound record ON */
}

/**
 * DMA Record processing
 */
void Crossbar_SendDataToDmaRecord(int16_t value)
{
	uint32_t nFramePos;
	uint8_t  dmaCtrlReg;

	if (dmaRecord.isRunning == 0) {
		return;
	}

	nFramePos = (dmaRecord.frameStartAddr + dmaRecord.frameCounter) & (DMA_MaskAddressHigh() << 16 | 0xffff);

	/* 16 bits stereo mode ? */
	if (crossbar.is16Bits) {
		STMemory_DMA_WriteWord(nFramePos, value);
		dmaRecord.frameCounter += 2;
	}
	/* 8 bits stereo ? */
	else if (crossbar.isStereo) {
		STMemory_DMA_WriteWord(nFramePos, value);
		dmaRecord.frameCounter += 2;
//		pFrameStart[dmaRecord.frameCounter] = (uint8_t)value;
//		dmaRecord.frameCounter ++;
	}
	/* 8 bits mono */
	else {
		STMemory_DMA_WriteByte(nFramePos, value);
		dmaRecord.frameCounter ++;
	}

	/* Check if end-of-frame has been reached and raise interrupts if needed. */
	if (dmaRecord.frameStartAddr + dmaRecord.frameCounter >= dmaRecord.frameEndAddr)
	{
		/* DMA sound idle : update SNDINT */
		Crossbar_Record_Update_DMA_Sound_Line_Idle ();			/* 1/HIGH=dma sound record idle */

		if (dmaRecord.loopMode) {
			Crossbar_setDmaRecord_Settings();			/* start a new frame */
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
	int16_t data;

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
void Crossbar_DmaRecordInHandShakeMode_Frame(uint32_t frame)
{
	dmaRecord.handshakeMode_Frame = frame;
}


/*----------------------------------------------------------------------*/
/*-------------------------- ADC processing ----------------------------*/
/*----------------------------------------------------------------------*/

/**
 * Get data recorded by the microphone and convert them into falcon internal frequency
 *    - micro_bufferL : left track recorded by the microphone
 *    - micro_bufferR : right track recorded by the microphone
 *    - microBuffer_size : buffers size
 */
void Crossbar_GetMicrophoneDatas(int16_t *micro_bufferL, int16_t *micro_bufferR, uint32_t microBuffer_size)
{
	uint32_t i, size, bufferIndex;
	int64_t idxPos;

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
	int16_t sample;
	uint16_t frame;

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
static void Crossbar_SendDataToDAC(int16_t value, uint16_t sample_pos)
{
	uint16_t track = crossbar.track_monitored * 2;

//fprintf ( stderr , "datadac %x %x\n" , value , dac.writePosition );
	/* Increase counter for each sample received by the DAC */
	dac.wordCount++;

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
	int i, nBufIdx;
	int n;
	int16_t adc_leftData, adc_rightData, dac_LeftData, dac_RightData;
	int16_t dac_read_left, dac_read_right;

//fprintf ( stderr , "gen %03x %03x %03x %03x\n" , dac.writePosition , dac.readPosition , (dac.writePosition-dac.readPosition)%DACBUFFER_SIZE , nSamplesToGenerate );
//fprintf ( stderr,  "codecAdcInput %d wordCount %d codecInputSource %d\n" , crossbar.codecAdcInput, dac.wordCount, crossbar.codecInputSource);
//uint32_t read_pos_in = dac.readPosition;
//uint64_t read_pos_float_in = dac.readPosition_float;
//fprintf ( stderr , "gen_in read_pos=%d read_pos_f=%lx ratio=%lx\n" , read_pos_in,read_pos_float_in,crossbar.frequence_ratio );

	if (crossbar.isDacMuted) {
		/* Output sound = 0 */
		for (i = 0; i < nSamplesToGenerate; i++) {
			nBufIdx = (nMixBufIdx + i) & AUDIOMIXBUFFER_SIZE_MASK;
			AudioMixBuffer[nBufIdx][0] = 0;
			AudioMixBuffer[nBufIdx][1] = 0;
		}

		/* Counters are refreshed for when DAC becomes unmuted */
		dac.readPosition = (dac.writePosition-DACBUFFER_SIZE/2)%DACBUFFER_SIZE;
		crossbar.adc2dac_readBufferPosition = adc.writePosition;
		return;
	}

	for (i = 0; i < nSamplesToGenerate; i++)
	{
		nBufIdx = (nMixBufIdx + i) & AUDIOMIXBUFFER_SIZE_MASK;

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
				adc_rightData = AudioMixBuffer[nBufIdx][1];
				break;
			case 2:
				/* PSG sound for left channel, microphone sound for right channel */
				adc_leftData = AudioMixBuffer[nBufIdx][0];
				adc_rightData = adc.buffer_right[crossbar.adc2dac_readBufferPosition];
				break;
			case 3:
				/* PSG sound for left and right channels */
				adc_leftData = AudioMixBuffer[nBufIdx][0];
				adc_rightData = AudioMixBuffer[nBufIdx][1];
				break;
		}

		/* DAC mixing (direct ADC + crossbar) */
		/* If DAC didn't receive any data, we force left/right value to 0 */
		if ( dac.wordCount == 0 )			/* Nothing received */
		{
			dac_read_left = 0;
			dac_read_right = 0;
		}
		else
		{
			dac_read_left = dac.buffer_left[dac.readPosition];
			dac_read_right = dac.buffer_right[dac.readPosition];
		}
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
				dac_LeftData = dac_read_left;
				dac_RightData = dac_read_right;
				break;
			case 3:
				/* Mixing Direct ADC sound with Crossbar->DMA sound */
				dac_LeftData = ((adc_leftData * crossbar.gainSettingLeft) >> 14) +
						dac_read_left;
				dac_RightData = ((adc_rightData * crossbar.gainSettingRight) >> 14) +
						dac_read_right;
				break;
		}

		AudioMixBuffer[nBufIdx][0] = (dac_LeftData * crossbar.attenuationSettingLeft) >> 16;
		AudioMixBuffer[nBufIdx][1] = (dac_RightData * crossbar.attenuationSettingRight) >> 16;

		/* Upgrade dac's buffer read pointer */
		dac.readPosition_float += crossbar.frequence_ratio;
		n = dac.readPosition_float >> 32;				/* number of samples to skip */

#if 0
		if (n) {
			// It becomes safe to zero old data if tail has moved
			for (j=0; j<n; j++) {
				dac.buffer_left[(dac.readPosition+j) % DACBUFFER_SIZE] = 0;
				dac.buffer_right[(dac.readPosition+j) % DACBUFFER_SIZE] = 0;
			}
		}
#endif

		dac.readPosition = (dac.readPosition + n) % DACBUFFER_SIZE;
		dac.readPosition_float &= 0xffffffff;			/* only keep the fractional part */
//read_pos_float_in += crossbar.frequence_ratio;
//fprintf ( stderr , "gen_one i=%d read_pos=%x read_pos_f=%lx ratio=%lx n=%d read_pos_f_total=%lx\n" , i, dac.readPosition,dac.readPosition_float,crossbar.frequence_ratio, n, read_pos_float_in );

		/* Upgrade adc->dac's buffer read pointer */
		crossbar.adc2dac_readBufferPosition_float += crossbar.frequence_ratio;
		n = crossbar.adc2dac_readBufferPosition_float >> 32;				/* number of samples to skip */
		crossbar.adc2dac_readBufferPosition = (crossbar.adc2dac_readBufferPosition + n) % DACBUFFER_SIZE;
		crossbar.adc2dac_readBufferPosition_float &= 0xffffffff;			/* only keep the fractional part */
	}

//fprintf ( stderr , "gen_out read_pos_delta=%x\n" , dac.readPosition-read_pos_in );

	/* If the DAC didn't receive any data since last call to Crossbar_GenerateSamples() */
	/* then we need to adjust dac.writePosition to be always ahead of dac.readPosition */
	if ( dac.wordCount == 0 )
	{
//		fprintf ( stderr , "fix writepos %x (readpos %x)\n" , (dac.readPosition+DACBUFFER_SIZE/2)%DACBUFFER_SIZE , dac.readPosition );
		dac.writePosition = (dac.readPosition+DACBUFFER_SIZE/2)%DACBUFFER_SIZE;
	}
	dac.wordCount = 0;
}


/**
 * display the Crossbar registers values (for debugger info command)
 */
void Crossbar_Info(FILE *fp, uint32_t dummy)
{
	const char *matrixDMA, *matrixDSP, *matrixEXT, *matrixDAC;
	char frqDMA[11], frqDAC[11], frqDSP[11], frqEXT[11];
	char frqSTE[30], frq25Mhz[30], frq32Mhz[30];
	char dataSize[15];
	static const char *matrix_tab[8] = {
		"OOHO",
		"OOXO",
		"OHOO",
		"OXOO",
		"HOOO",
		"XOOO",
		"OOOH",
		"OOOX"
	};

	if (!Config_IsMachineFalcon()) {
		fprintf(fp, "Not Falcon - no Crossbar!\n");
		return;
	}
	DmaSnd_Info(fp, 0);
	fprintf(fp, "\n");
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

	/* DAC connection */
	switch ((IoMem_ReadWord(0xff8932) >> 13) & 0x3) {
		case 0 :
			/* DAC connection with DMA Playback */
			if ((IoMem_ReadWord(0xff8930) & 0x1) == 1)
				matrixDAC = "OOXO";
			else
				matrixDAC = "OOHO";
			break;
		case 1 :
			/* DAC connection with DSP Transmit */
			if ((IoMem_ReadWord(0xff8930) & 0x10) == 0x10)
				matrixDAC = "OXOO";
			else
				matrixDAC = "OHOO";
			break;
		case 2 :
			/* DAC connection with External Input */
			if ((IoMem_ReadWord(0xff8930) & 0x100) == 0x100)
				matrixDAC = "XOOO";
			else
				matrixDAC = "HOOO";
			break;
		default: /* case 3 */
			/* DAC connection with ADC */
			matrixDAC = "OOOX";
			break;
	}

	/* DMA connection */
	matrixDMA = matrix_tab[IoMem_ReadWord(0xff8932) & 0x7];

	/* DSP connection */
	matrixDSP = matrix_tab[(IoMem_ReadWord(0xff8932) >> 4) & 0x7];

	/* External input connection */
	matrixEXT = matrix_tab[(IoMem_ReadWord(0xff8932) >> 8) & 0x7];

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
	fprintf(fp, "%s       |      |      |      |    O = no connection\n", frqEXT);
	fprintf(fp, "                 |      |      |      |    X = connection\n");
	fprintf(fp, "Dsp Transmit  ---%c------%c------%c------%c    H = Handshake connection\n", matrixDAC[1], matrixDMA[1], matrixDSP[1], matrixEXT[1]);
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

