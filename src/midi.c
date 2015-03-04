/*
  Hatari - midi.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  MIDI communication.
  Note that this code is far from being perfect. However, it is already
  enough to let some ST programs (e.g. the game Pirates!) use the host's midi
  system.

  TODO:
   - Most bits in the ACIA's status + control registers are currently ignored.
*/
const char Midi_fileid[] = "Hatari midi.c : " __DATE__ " " __TIME__;

#include <SDL_types.h>

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "m68000.h"
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
 * Serial line is set to 31250 bps, 8 bits, 1 stop, no parity, which gives 256 cycles
 * per bit at 8 MHz, and 2308 cycles to transfer 9 bits
 */
#define	MIDI_TRANSFER_CYCLE		2308


static FILE *pMidiFhIn  = NULL;        /* File handle used for Midi input */
static FILE *pMidiFhOut = NULL;        /* File handle used for Midi output */
static Uint8 MidiControlRegister;
static Uint8 MidiStatusRegister;
static Uint8 nRxDataByte;


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
	MidiControlRegister = 0;
	MidiStatusRegister = ACIA_SR_TX_EMPTY;
	nRxDataByte = 1;

	if (ConfigureParams.Midi.bEnableMidi)
		CycInt_AddRelativeInterrupt(MIDI_TRANSFER_CYCLE, INT_CPU_CYCLE, INTERRUPT_MIDI);
	else
		CycInt_RemovePendingInterrupt (INTERRUPT_MIDI);
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
 */
void Midi_Data_WriteByte(void)
{
	Uint8 nTxDataByte;

	ACIA_AddWaitCycles ();						/* Additional cycles when accessing the ACIA */

	nTxDataByte = IoMem[0xfffc06];

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

	/* Flush outgoing data */
	if (!(MidiStatusRegister & ACIA_SR_TX_EMPTY))
	{
		MidiStatusRegister |= ACIA_SR_TX_EMPTY;

		/* Do we need to generate a transfer interrupt? */
		MIDI_UpdateIRQ ();

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

	CycInt_AddRelativeInterrupt(MIDI_TRANSFER_CYCLE, INT_CPU_CYCLE, INTERRUPT_MIDI);
}

