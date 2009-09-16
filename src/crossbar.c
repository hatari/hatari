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
#include "dmaSnd.h"
#include "crossbar.h"
#include "stMemory.h"
#include "falcon/dsp.h"


/*-----------------------------------------------------------------------*/
/**
 * Reset Crossbar variables.
 */
void Crossbar_Reset(bool bCold)
{
	if (bCold)
	{
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void Crossbar_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
//	MemorySnapShot_Store(&nDmaSoundControl, sizeof(nDmaSoundControl));
// 	MemorySnapShot_Store(&nDmaSoundMode, sizeof(nDmaSoundMode));
}


/*-----------------------------------------------------------------------*/
/**
 * Read byte from DMA track control (0xff8920).
 */
void Crossbar_DmaTrckCtrl_ReadByte(void)
{
//	IoMem_WriteByte(0xff8920, nDmaSoundMode);

	LOG_TRACE(TRACE_DMASND, "DMA sound mode register read: 0x%02x\n", IoMem_ReadByte(0xff8920));
}

/**
 * Write byte to DMA track control (0xff8920).
 */
void Crossbar_DmaTrckCtrl_WriteByte(void)
{
//	IoMem_WriteByte(0xff8920, nDmaSoundMode);

	LOG_TRACE(TRACE_DMASND, "DMA sound mode register write: 0x%02x\n", IoMem_ReadByte(0xff8920));
}

/* ---------------------- Falcon sound subsystem ---------------------- */


static void Crossbar_StartDspXmitHandler(void)
{
	Uint16 nCbSrc = IoMem_ReadWord(0xff8930);
	int nFreq;
	int nClkDiv;

	/* Ignore when DSP XMIT is connected to external port */
	if ((nCbSrc & 0x80) == 0x00)
		return;

	nClkDiv = 256 * ((IoMem_ReadByte(0xff8935) & 0x0f) + 1);

	if ((nCbSrc & 0x60) == 0x00)
	{
		/* Internal 25.175 MHz clock */
		nFreq = 25175000 / nClkDiv;
		Int_AddRelativeInterrupt((8013000+nFreq/2)/nFreq/2, INT_CPU_CYCLE, INTERRUPT_DSPXMIT);
	}
	else if ((nCbSrc & 0x60) == 0x20)
	{
		/* Internal 32 MHz clock */
		nFreq = 32000000 / nClkDiv;
		Int_AddRelativeInterrupt((8013000+nFreq/2)/nFreq/2, INT_CPU_CYCLE, INTERRUPT_DSPXMIT);
	}

	/* Send sample to DMA sound */
	DmaSnd_ReceiveSoundFromDAC(DSP_SsiReadTxValue());
}


void Crossbar_InterruptHandler_DspXmit(void)
{
	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* TODO: Trigger SSI transmit interrupt in the DSP and fetch the data,
	 *       then distribute the data to the destinations */

	DSP_SsiReceive_SC2(0);
	DSP_SsiReceiveSerialClock();

	/* Restart the Int event handler */
	Crossbar_StartDspXmitHandler();

}


/**
 * Read word from Falcon Crossbar source controller (0xff8930).
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
void Crossbar_SrcControler_ReadWord(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd Crossbar src read: 0x%04x\n", IoMem_ReadWord(0xff8930));
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

	LOG_TRACE(TRACE_DMASND, "Falcon snd Crossbar src write: 0x%04x\n", nCbSrc);

	Crossbar_StartDspXmitHandler();
}

/**
 * Read word from Falcon Crossbar destination controller (0xff8932).
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
void Crossbar_DstControler_ReadWord(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd Crossbar dst read: 0x%04x\n", IoMem_ReadWord(0xff8932));
}

/**
 * Write word to Falcon Crossbar destination controller (0xff8932).
 */
void Crossbar_DstControler_WriteWord(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd Crossbar dst write: 0x%04x\n", IoMem_ReadWord(0xff8932));
}

/**
 * Read byte from external clock divider register (0xff8934).
 */
void Crossbar_FreqDivExt_ReadByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd ext. clock divider read: 0x%02x\n", IoMem_ReadByte(0xff8934));
}

/**
 * Write byte to external clock divider register (0xff8934).
 */
void Crossbar_FreqDivExt_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd ext. clock divider write: 0x%02x\n", IoMem_ReadByte(0xff8934));
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void Crossbar_FreqDivInt_ReadByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd int. clock divider read: 0x%02x\n", IoMem_ReadByte(0xff8935));
}

/**
 * Write byte to internal clock divider register (0xff8935).
 */
void Crossbar_FreqDivInt_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd int. clock divider write: 0x%02x\n", IoMem_ReadByte(0xff8935));
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
	LOG_TRACE(TRACE_DMASND, "Falcon snd record track select read: 0x%02x\n", IoMem_ReadByte(0xff8936));
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
	LOG_TRACE(TRACE_DMASND, "Falcon snd record track select write: 0x%02x\n", IoMem_ReadByte(0xff8936));
}

/**
 * Read byte from CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D convertor 
 */
void Crossbar_CodecInput_ReadByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd CODEC input read: 0x%02x\n", IoMem_ReadByte(0xff8937));
}

/**
 * Write byte to CODEC input source from 16 bit adder (0xff8937).
 *	Bit 1 : source = multiplexer
 *	Bit 0 : source = A/D convertor 
 */
void Crossbar_CodecInput_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd CODEC input write: 0x%02x\n", IoMem_ReadByte(0xff8937));
}

/**
 * Read byte from A/D converter input for L+R channel (0xff8938).
 *	Bit 1 :  Left (0 = Microphone ; 1 = PSG soundchip)
 *	Bit 0 : Right (0 = Microphone ; 1 = PSG soundchip)
 */
void Crossbar_AdcInput_ReadByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd ADC input read: 0x%02x\n", IoMem_ReadByte(0xff8938));
}

/**
 * Write byte to A/D converter input for L+R channel (0xff8938).
 *	Bit 1 :  Left (0 = Microphone ; 1 = PSG soundchip)
 *	Bit 0 : Right (0 = Microphone ; 1 = PSG soundchip)
 */
void Crossbar_AdcInput_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd ADC input write: 0x%02x\n", IoMem_ReadByte(0xff8938));
}

/**
 * Read byte from input amplifier register (0xff8939).
 * 	Bits LLLLRRRR
 * 	Amplification is in +1.5 dB steps
 */
void Crossbar_InputAmp_ReadByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd CODEC channel amplification read: 0x%04x\n", IoMem_ReadWord(0xff8939));
}

/**
 * Write byte to input amplifier register (0xff8939).
 * 	Bits LLLLRRRR
 * 	Amplification is in +1.5 dB steps
 */
void Crossbar_InputAmp_WriteByte(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd CODEC channel amplification write: 0x%04x\n", IoMem_ReadWord(0xff8939));
}

/**
 * Read word from output reduction register (0xff893a).
 * 	Bits LLLLRRRR
 * 	Reduction is in -1.5 dB steps
 */
void Crossbar_OutputReduct_ReadWord(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd CODEC channel attenuation read: 0x%04x\n", IoMem_ReadWord(0xff893a));
}

/**
 * Write word to channel reduction register (0xff893a).
 * 	Bits LLLLRRRR
 * 	Reduction is in -1.5 dB steps
 */
void Crossbar_OutputReduct_WriteWord(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd CODEC channel attenuation write: 0x%04x\n", IoMem_ReadWord(0xff893a));
}

/**
 * Read word from CODEC status register (0xff893c).
 * 	Bit 1 :  Left Channel Overflow (0/1)
 * 	Bit 0 : Right Channel Overflow (0/1)
 */
void Crossbar_CodecStatus_ReadWord(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd CODEC status read: 0x%04x\n", IoMem_ReadWord(0xff893c));
}

/**
 * Write word to CODEC status register (0xff893c).
 * 	Bit 1 :  Left Channel Overflow (0/1)
 * 	Bit 0 : Right Channel Overflow (0/1)
 */
void Crossbar_CodecStatus_WriteWord(void)
{
	LOG_TRACE(TRACE_DMASND, "Falcon snd CODEC status write: 0x%04x\n", IoMem_ReadWord(0xff893c));
}
