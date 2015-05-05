/*
  Hatari - midi.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  MIDI communication.

  TODO:
   - Most bits in the ACIA's status + control registers are currently ignored.

  NOTE [NP] :
    In all accuracy, we should use a complete emulation of the acia serial line,
    as for the ikbd. But as the MIDI's baudrate is rather high and could require
    more resources to emulate at the bit level, we handle transfer 1 byte a time
    instead of sending each bit one after the other.
    This way, we only need a timer every 2560 cycles (instead of 256 cycles per bit).

    We handle a special case for the TX_EMPTY bit when reading SR : this bit should be set
    after TDR was copied into TSR, which is approximatively when the next bit should
    be transferred (256 cycles) (fix the program 'Notator')
*/
const char Midi_fileid[] = "Hatari midi.c : " __DATE__ " " __TIME__;

#include <SDL_types.h>

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "midi.h"
#include "file.h"
#include "acia.h"
#include "screen.h"
#include "video.h"


#define ACIA_SR_INTERRUPT_REQUEST  0x80
#define ACIA_SR_TX_EMPTY           0x02
#define ACIA_SR_RX_FULL            0x01

/* Delay to send/receive 1 byte through MIDI (in cpu cycles)
 * Serial line is set to 31250 bps, 1 start bit, 8 bits, 1 stop, no parity, which gives 256 cycles
 * per bit at 8 MHz, and 2560 cycles to transfer 10 bits
 */
#define	MIDI_TRANSFER_BIT_CYCLE		256
#define	MIDI_TRANSFER_BYTE_CYCLE	(MIDI_TRANSFER_BIT_CYCLE * 10)


static FILE *pMidiFhIn  = NULL;        /* File handle used for Midi input */
static FILE *pMidiFhOut = NULL;        /* File handle used for Midi output */
static Uint8 MidiControlRegister;
static Uint8 MidiStatusRegister;
static Uint8 nRxDataByte;
static Uint64 TDR_Write_Time;		/* Time of the last write in TDR fffc06 */
static Uint64 TDR_Empty_Time;		/* Time when TDR will be empty after a write to fffc06 (ie when TDR is transferred to TSR) */
static Uint64 TSR_Complete_Time;	/* Time when TSR will be completely transferred */



/**
 * Initialization: Open MIDI device.
 */
void Midi_Init(void)
{
	if (!ConfigureParams.Midi.bEnableMidi)
		return;

	if (ConfigureParams.Midi.sMidiOutFileName[0])
	{
		/* Open MIDI output file */
		pMidiFhOut = File_Open(ConfigureParams.Midi.sMidiOutFileName, "wb");
		if (!pMidiFhOut)
		{
			Log_AlertDlg(LOG_ERROR, "MIDI output file open failed. MIDI support disabled.");
			ConfigureParams.Midi.bEnableMidi = false;
			return;
		}
		setvbuf(pMidiFhOut, NULL, _IONBF, 0);    /* No output buffering! */
		LOG_TRACE(TRACE_MIDI, "MIDI: Opened file '%s' for output\n",
			 ConfigureParams.Midi.sMidiOutFileName);
	}
	if (ConfigureParams.Midi.sMidiInFileName[0])
	{
		/* Try to open MIDI input file */
		pMidiFhIn = File_Open(ConfigureParams.Midi.sMidiInFileName, "rb");
		if (!pMidiFhIn)
		{
			Log_AlertDlg(LOG_ERROR, "MIDI input file open failed. MIDI support disabled.");
			ConfigureParams.Midi.bEnableMidi = false;
			return;
		}
		setvbuf(pMidiFhIn, NULL, _IONBF, 0);    /* No input buffering! */
		LOG_TRACE(TRACE_MIDI, "MIDI: Opened file '%s' for input\n",
			 ConfigureParams.Midi.sMidiInFileName);
	}
}


/**
 * Close MIDI device.
 */
void Midi_UnInit(void)
{
	pMidiFhIn = File_Close(pMidiFhIn);
	pMidiFhOut = File_Close(pMidiFhOut);

	CycInt_RemovePendingInterrupt(INTERRUPT_MIDI);
}


/**
 * Reset MIDI emulation.
 */
void Midi_Reset(void)
{
//fprintf ( stderr , "midi reset\n" );
	MidiControlRegister = 0;
	MidiStatusRegister = ACIA_SR_TX_EMPTY;
	nRxDataByte = 1;
	TDR_Empty_Time = 0;
	TSR_Complete_Time = 0;

	/* Set timer */
	CycInt_AddRelativeInterrupt ( MIDI_TRANSFER_BYTE_CYCLE , INT_CPU_CYCLE , INTERRUPT_MIDI );
}


/**
 * Save/Restore snapshot of local variables
 */
void    MIDI_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(&MidiControlRegister, sizeof(MidiControlRegister));
	MemorySnapShot_Store(&MidiStatusRegister, sizeof(MidiStatusRegister));
	MemorySnapShot_Store(&nRxDataByte, sizeof(nRxDataByte));
	MemorySnapShot_Store(&TDR_Empty_Time, sizeof(TDR_Empty_Time));
	MemorySnapShot_Store(&TSR_Complete_Time, sizeof(TSR_Complete_Time));
}


/*-----------------------------------------------------------------------*/
/**
 * Check if the IRQ must be changed in SR.
 * When there's a change, we must change the IRQ line too.
 */
static void	MIDI_UpdateIRQ ( void )
{
	Uint8		irq_bit_new;

	irq_bit_new = 0;

	if ( ( ( MidiControlRegister & 0x80 ) == 0x80 ) 		/* Check for RX causes of interrupt */
	  && ( MidiStatusRegister & ACIA_SR_RX_FULL ) )
	  irq_bit_new = ACIA_SR_INTERRUPT_REQUEST;

	if ( ( ( MidiControlRegister & 0x60) == 0x20 )			/* Check for TX causes of interrupt */
	  && ( MidiStatusRegister & ACIA_SR_TX_EMPTY ) )
	  irq_bit_new = ACIA_SR_INTERRUPT_REQUEST;
	
	/* Update SR and IRQ line if a change happened */
	if ( ( MidiStatusRegister & ACIA_SR_INTERRUPT_REQUEST ) != irq_bit_new )
	{
		LOG_TRACE ( TRACE_MIDI, "midi update irq irq_new=%d VBL=%d HBL=%d\n" , irq_bit_new?1:0 , nVBLs , nHBL );

		if ( irq_bit_new )
		{
			/* Request interrupt by setting GPIP to low/0 */
			MFP_GPIP_Set_Line_Input ( MFP_GPIP_LINE_ACIA , MFP_GPIP_STATE_LOW );
			MidiStatusRegister |= ACIA_SR_INTERRUPT_REQUEST;
		}
		else
		{
			/* Clear interrupt request by setting GPIP to high/1 */
			MFP_GPIP_Set_Line_Input ( MFP_GPIP_LINE_ACIA , MFP_GPIP_STATE_HIGH );
			MidiStatusRegister &= ~ACIA_SR_INTERRUPT_REQUEST;
		}
	}
}



/**
 * Read MIDI status register ($FFFC04).
 */
void Midi_Control_ReadByte(void)
{
	ACIA_AddWaitCycles ();						/* Additional cycles when accessing the ACIA */

	/* Special case : if we wrote a byte into TDR, TX_EMPTY bit should be */
	/* set approximatively after the first bit was transferred using TSR */
	if ( ( ( MidiStatusRegister & ACIA_SR_TX_EMPTY ) == 0 )
	  && ( CyclesGlobalClockCounter > TDR_Empty_Time ) )						// OK avec 11 bits et 1 bit
	{
		MidiStatusRegister |= ACIA_SR_TX_EMPTY;

		/* Do we need to generate a transfer interrupt? */
		MIDI_UpdateIRQ ();
	}

//fprintf ( stderr , "midi read sr %x %lld %lld\n" , MidiStatusRegister , CyclesGlobalClockCounter , TDR_Write_Time );

	IoMem[0xfffc04] = MidiStatusRegister;

	LOG_TRACE ( TRACE_MIDI, "midi read fffc04 sr=0x%02x VBL=%d HBL=%d\n" , MidiStatusRegister , nVBLs , nHBL );
}


/**
 * Write to MIDI control register ($FFFC04).
 */
void Midi_Control_WriteByte(void)
{
	ACIA_AddWaitCycles ();						/* Additional cycles when accessing the ACIA */

	MidiControlRegister = IoMem[0xfffc04];

	LOG_TRACE ( TRACE_MIDI, "midi write fffc04 cr=0x%02x VBL=%d HBL=%d\n" , MidiControlRegister , nVBLs , nHBL );

	MIDI_UpdateIRQ ();
}


/**
 * Read MIDI data register ($FFFC06).
 */
void Midi_Data_ReadByte(void)
{
	LOG_TRACE ( TRACE_MIDI, "midi read fffc06 rdr=0x%02x VBL=%d HBL=%d\n" , nRxDataByte , nVBLs , nHBL );
//fprintf ( stderr , "midi rx %x\n" , nRxDataByte);

	ACIA_AddWaitCycles ();						/* Additional cycles when accessing the ACIA */

	IoMem[0xfffc06] = nRxDataByte;

	MidiStatusRegister &= ~ACIA_SR_RX_FULL;

	MIDI_UpdateIRQ ();
}


/**
 * Write to MIDI data register ($FFFC06).
 * We should determine precisely when TDR will be empty and when TSR will be transferred.
 * This is required to accurately emulate the TDRE bit in status register (fix the program 'Notator')
 */
void Midi_Data_WriteByte(void)
{
	Uint8 nTxDataByte;

	ACIA_AddWaitCycles ();						/* Additional cycles when accessing the ACIA */

	nTxDataByte = IoMem[0xfffc06];
	TDR_Write_Time = CyclesGlobalClockCounter;

	/* If TSR is already transferred, then TDR will be empty after 1 bit is transferred */
	/* If TSR is not completely transferred, then TDR will be empty 1 bit after TSR is transferred */
	if ( CyclesGlobalClockCounter >= TSR_Complete_Time )
	{
		TDR_Empty_Time = CyclesGlobalClockCounter + MIDI_TRANSFER_BIT_CYCLE;
		TSR_Complete_Time = CyclesGlobalClockCounter + MIDI_TRANSFER_BYTE_CYCLE;
	}
	else
	{
//fprintf ( stderr , "MIDI OVR %lld\n" , TSR_Complete_Time - CyclesGlobalClockCounter );
		TDR_Empty_Time = TSR_Complete_Time + MIDI_TRANSFER_BIT_CYCLE;
		TSR_Complete_Time += MIDI_TRANSFER_BYTE_CYCLE;
	}

	LOG_TRACE ( TRACE_MIDI, "midi write fffc06 tdr=0x%02x VBL=%d HBL=%d\n" , nTxDataByte , nVBLs , nHBL );
//fprintf ( stderr , "midi tx %x sr=%x\n" , nTxDataByte , MidiStatusRegister );

	MidiStatusRegister &= ~ACIA_SR_TX_EMPTY;

	MIDI_UpdateIRQ ();

	if (!ConfigureParams.Midi.bEnableMidi)
		return;

	if (pMidiFhOut)
	{
		int ret;

		/* Write the character to the output file: */
		ret = fputc(nTxDataByte, pMidiFhOut);

		/* If there was an error then stop the midi emulation */
		if (ret == EOF)
		{
			LOG_TRACE(TRACE_MIDI, "MIDI: write error -> stop MIDI\n");
			Midi_UnInit();
			return;
		}
	}
}


/**
 * Read and write MIDI interface data regularly
 */
void Midi_InterruptHandler_Update(void)
{
	int nInChar;

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* Special case : if we wrote a byte into TDR, TX_EMPTY bit should be */
	/* set when reaching TDR_Empty_Time */
	if ( ( ( MidiStatusRegister & ACIA_SR_TX_EMPTY ) == 0 )
	  && ( CyclesGlobalClockCounter > TDR_Empty_Time ) )
	{
		MidiStatusRegister |= ACIA_SR_TX_EMPTY;

		/* Do we need to generate a transfer interrupt? */
		MIDI_UpdateIRQ ();

		/* Flush outgoing data (not necessary ?) */
		// if (pMidiFhOut)
		//	fflush(pMidiFhOut);
	}

	/* Read the bytes in, if we have any */
	if (pMidiFhIn && File_InputAvailable(pMidiFhIn))
	{
		nInChar = fgetc(pMidiFhIn);
		if (nInChar != EOF)
		{
			LOG_TRACE(TRACE_MIDI, "MIDI: Read character -> $%x\n", nInChar);
			/* Copy into our internal queue */
			nRxDataByte = nInChar;
			MidiStatusRegister |= ACIA_SR_RX_FULL;

			/* Do we need to generate a receive interrupt? */
			MIDI_UpdateIRQ ();
		}
		else
		{
			LOG_TRACE(TRACE_MIDI, "MIDI: read error (doesn't stop MIDI)\n");
			clearerr(pMidiFhIn);
		}
	}

	/* Set timer */
	CycInt_AddRelativeInterrupt ( MIDI_TRANSFER_BYTE_CYCLE , INT_CPU_CYCLE , INTERRUPT_MIDI );
}

