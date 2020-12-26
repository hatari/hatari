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
const char Midi_fileid[] = "Hatari midi.c";

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

/* Delay to send/receive 1 byte through MIDI (in cpu cycles at x1, x2 or x4 speed)
 * Serial line is set to 31250 bps, 1 start bit, 8 bits, 1 stop, no parity, which gives 256 cycles
 * per bit at 8 MHz, and 2560 cycles to transfer 10 bits
 */
#define	MIDI_TRANSFER_BIT_CYCLE		( 256 << nCpuFreqShift )
#define	MIDI_TRANSFER_BYTE_CYCLE	(MIDI_TRANSFER_BIT_CYCLE * 10)

static Uint8 MidiControlRegister;
static Uint8 MidiStatusRegister;
static Uint8 nRxDataByte;
static Uint64 TDR_Write_Time;		/* Time of the last write in TDR fffc06 */
static Uint64 TDR_Empty_Time;		/* Time when TDR will be empty after a write to fffc06 (ie when TDR is transferred to TSR) */
static Uint64 TSR_Complete_Time;	/* Time when TSR will be completely transferred */


/*
** Host MIDI I/O
*/
static bool Midi_Host_Open(void);
static void Midi_Host_Close(void);
static int  Midi_Host_ReadByte(void);
static bool Midi_Host_WriteByte(Uint8 byte);

#ifndef HAVE_PORTMIDI
static FILE *pMidiFhIn  = NULL;    /* File handle used for Midi input */
static FILE *pMidiFhOut = NULL;    /* File handle used for Midi output */
#else
#include "portmidi.h"
#define INPUT_BUFFER_SIZE  1024		 // PortMidi handles buffering
static PmStream* midiIn  = NULL;	 // current midi input port
static PmStream* midiOut = NULL;	 // current midi output port
static int numInputs  = 0;				 // number of available input ports
static int numOutputs = 0;				 // number of available output ports
static const PmDeviceInfo** inports  = NULL;	// array of available input ports
static const PmDeviceInfo** outports = NULL;	// array of available output ports

static bool Midi_Host_SwitchPort(const char* portName, bool forInput);
static int Midi_GetDataLength(Uint8 status);
static int Midi_SplitEvent(PmEvent* midiEvent, Uint8* msg);
static PmEvent* Midi_BuildEvent(Uint8 byte);
#endif


/**
 * Initialization: Open MIDI device.
 */
void Midi_Init(void)
{
	if (ConfigureParams.Midi.bEnableMidi)
	{
		if (!Midi_Host_Open())
		{
			Log_AlertDlg(LOG_ERROR, "MIDI i/o open failed. MIDI support disabled.");
			ConfigureParams.Midi.bEnableMidi = false;
		}
	}
}

/**
 * Close MIDI device.
 */
void Midi_UnInit(void)
{
	Midi_Host_Close();
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
			MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_ACIA , MFP_GPIP_STATE_LOW );
			MidiStatusRegister |= ACIA_SR_INTERRUPT_REQUEST;
		}
		else
		{
			/* Clear interrupt request by setting GPIP to high/1 */
			MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_ACIA , MFP_GPIP_STATE_HIGH );
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
	bool ok;
	
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

	ok = Midi_Host_WriteByte(nTxDataByte);

	/* If there was an error then stop the midi emulation */
	if (!ok)
	{
		LOG_TRACE(TRACE_MIDI, "MIDI: write error -> stop MIDI\n");
		Midi_UnInit();
		return;
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
	nInChar = Midi_Host_ReadByte();
	if (nInChar != EOF)
	{
		LOG_TRACE(TRACE_MIDI, "MIDI: Read character -> $%x\n", nInChar);
		/* Copy into our internal queue */
		nRxDataByte = nInChar;
		MidiStatusRegister |= ACIA_SR_RX_FULL;

		/* Do we need to generate a receive interrupt? */
		MIDI_UpdateIRQ ();
	}
#ifndef HAVE_PORTMIDI
	else if (pMidiFhIn)
	{
		LOG_TRACE(TRACE_MIDI, "MIDI: read error (doesn't stop MIDI)\n");
		clearerr(pMidiFhIn);
	}
#endif

	/* Set timer */
	CycInt_AddRelativeInterrupt ( MIDI_TRANSFER_BYTE_CYCLE , INT_CPU_CYCLE , INTERRUPT_MIDI );
}


// ============================================================================
// Host MIDI I/O
// ============================================================================

/**
 * open MIDI streams
 * return true for no errors
 */
static bool Midi_Host_Open(void)
{
#ifndef HAVE_PORTMIDI
	if (ConfigureParams.Midi.sMidiOutFileName[0])
	{
		/* Open MIDI output file */
		pMidiFhOut = File_Open(ConfigureParams.Midi.sMidiOutFileName, "wb");
		if (!pMidiFhOut)
			return false;
		setvbuf(pMidiFhOut, NULL, _IONBF, 0);    /* No output buffering! */
		LOG_TRACE(TRACE_MIDI, "MIDI: Opened file '%s' for output\n",
			 ConfigureParams.Midi.sMidiOutFileName);
	}
	if (ConfigureParams.Midi.sMidiInFileName[0])
	{
		/* Try to open MIDI input file */
		pMidiFhIn = File_Open(ConfigureParams.Midi.sMidiInFileName, "rb");
		if (!pMidiFhIn)
			return false;
		setvbuf(pMidiFhIn, NULL, _IONBF, 0);    /* No input buffering! */
		LOG_TRACE(TRACE_MIDI, "MIDI: Opened file '%s' for input\n",
			 ConfigureParams.Midi.sMidiInFileName);
	}
#else
	/* Need to always get MIDI device info, for MIDI setup dialog */
	int i;
	int iindex = 0;
	int oindex = 0;
	int numPorts = 0;
	
	if (Pm_Initialize() != pmNoError)
	{
		LOG_TRACE(TRACE_MIDI, "MIDI: PortMidi initialization failed\n");
		return false;
	}
	// -- get rid of earlier portmidi descriptor arrays (if allocated)
	// -- the information may be stale (USB Midi etc)
	if (inports)
		free (inports);
	if (outports)
		free (outports);
	inports = outports = NULL;
	numInputs = numOutputs = 0;

	// -- count number of input and output ports
	numPorts = Pm_CountDevices();
	for (i = 0; i < numPorts; i++)
	{
		const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
		if (info->input)
			numInputs++;
		else if (info->output)
			numOutputs++;
	}

	// -- allocate descriptor arrays
	inports  = malloc(numInputs  * sizeof(PmDeviceInfo*));
	outports = malloc(numOutputs * sizeof(PmDeviceInfo*));
	
	// -- populate descriptor arrays
	for (i = 0; i < numPorts; i++)
	{
		const PmDeviceInfo* info = Pm_GetDeviceInfo(i);
		if (info)
		{
			LOG_TRACE(TRACE_MIDI, "MIDI: device %d: '%s'\n", i, info->name);
			if (info->input)
				inports[iindex++] = info;
			if (info->output)
				outports[oindex++] = info;
		}
		else
			LOG_TRACE(TRACE_MIDI, "MIDI: info disappeared for device %d!\n", i);
	}

	// -- open input and output ports according to configuration
	if (ConfigureParams.Midi.sMidiInPortName[0])
		Midi_Host_SwitchPort(ConfigureParams.Midi.sMidiInPortName, true);
	if (ConfigureParams.Midi.sMidiOutPortName[0])
		Midi_Host_SwitchPort(ConfigureParams.Midi.sMidiOutPortName, false);
#endif

	return true;
}


/* ---------------------------------------------------------------------------- */
/**
 * close MIDI streams
 */
static void Midi_Host_Close(void)
{
#ifndef HAVE_PORTMIDI
	pMidiFhIn = File_Close(pMidiFhIn);
	pMidiFhOut = File_Close(pMidiFhOut);
#else
	if (midiIn)
		Pm_Close(midiIn);
	if (midiOut)
		Pm_Close(midiOut);
	midiIn = midiOut = NULL;

	// Can't terminate PM or free descriptor arrays as this gets
	// called by any write errors and GUI won't then work.
	// Pm_Terminate();
#endif
}



/* ---------------------------------------------------------------------------- */
/**
 * returns port name for input or output port at 'index'
 */
const char* Midi_Host_GetPortName(int index, bool forInput)
{
#ifdef HAVE_PORTMIDI
	if (forInput && index < numInputs)
		return inports[index]->name;
	else if (!forInput && index < numOutputs)
		return outports[index]->name;
#endif

	return NULL;
}

/* ---------------------------------------------------------------------------- */
/**
 * returns port descriptor array index for input or output 'portName'
 */
int Midi_Host_GetPortIndex(const char* portName, bool forInput)
{
#ifdef HAVE_PORTMIDI
	int i = 0;
	int numPorts = forInput ? numInputs : numOutputs;
	const PmDeviceInfo** ports = forInput ? inports : outports;

	if (ports)
	{
		for (i = 0; i < numPorts; i++)
			if (!strcmp(portName, ports[i]->name))
				return i;
	}
#endif

	return -1;
}

/**
 * closes current midi port (if any) and opens 'portName' if MIDI enabled
 * returns true if successful, false otherwise
 */
#ifdef HAVE_PORTMIDI
static bool Midi_Host_SwitchPort(const char* portName, bool forInput)
{
	int i, count;
	bool err;

	if (!ConfigureParams.Midi.bEnableMidi)
		return false;

	// -- find PortMidi index for 'portName'
	count = Pm_CountDevices();
	for (i = 0; i < count; i++)
	{
		const PmDeviceInfo* info = Pm_GetDeviceInfo(i);
		if (info)
		{
			if (forInput && !info->input)
				continue;
			else if (!forInput && info->input)
				continue;
			if (!strcmp(info->name, portName))
				break;
		}
	}
	if (i >= count)
		return false;

	// -- close current port in any case, then try open new one
	if (forInput == true)
	{
		if (midiIn) {
			Pm_Close(midiIn);
			midiIn = NULL;
		}
		err = (Pm_OpenInput(&midiIn, i, NULL, INPUT_BUFFER_SIZE, NULL, NULL) == pmNoError);
		LOG_TRACE(TRACE_MIDI, "MIDI: input port %d '%s' open %s\n", i, portName, err ? "succeeded" : "failed");
		return err;
	}
	else
	{
		if (midiOut) {
			Pm_Close(midiOut);
			midiOut = NULL;
		}
		err = (Pm_OpenOutput(&midiOut, i, NULL, 0, NULL, NULL, 0) == pmNoError);
		LOG_TRACE(TRACE_MIDI, "MIDI: output port %d '%s' open %s\n", i, portName, err ? "succeeded" : "failed");
		return err;
	}

	return false;
}
#endif

/**
 * returns byte from input stream, or EOF if it is empty
 */
static int Midi_Host_ReadByte(void)
{
#ifndef HAVE_PORTMIDI
	if (pMidiFhIn && File_InputAvailable(pMidiFhIn))
		return fgetc(pMidiFhIn);
	else return EOF;
#else
	static Uint8 msg[4];
	static Uint8 ibyte = 0;
	static int bytesAvailable = 0;

	if (midiIn)
	{
		// -- we have not yet returned all bytes from previous event
		if (bytesAvailable > 0)
		{
			bytesAvailable--;
			return msg[ibyte++];
		}

		// -- read new event (if any)
		else if (Pm_Poll(midiIn) == TRUE) {
			PmEvent midiEvent;
			if (Pm_Read(midiIn, &midiEvent, 1) != 1)
				return EOF;
			if ((bytesAvailable = Midi_SplitEvent(&midiEvent, msg)) > 0)
			{
				bytesAvailable--;
				ibyte = 1;
				return msg[0];
			}
		}
	}

	// -- no more midi data
	return EOF;
#endif
}


/**
 * writes 'byte' into output stream, returns true on success
 */
static bool Midi_Host_WriteByte(Uint8 byte)
{
#ifndef HAVE_PORTMIDI
	if (pMidiFhOut)
	{
		/* Write the character to the output file: */
		int ret = fputc(byte, pMidiFhOut);
		return (ret != EOF);
	}
#else
	if (midiOut)
	{
		PmEvent* midiEvent = Midi_BuildEvent(byte);
		if (midiEvent)
		{
			const char *msg;
			PmError error = Pm_Write(midiOut, midiEvent, 1);
			if (error == pmNoError || error == pmGotData)
				return true;
			msg = Pm_GetErrorText(error);
			Log_Printf(LOG_WARN, "MIDI: PortMidi write error %d: '%s'\n",
				  error, msg);
			return false;
		}
		return true;
	}
#endif

	return false;
}


#ifdef HAVE_PORTMIDI
// ============================================================================
// PortMidi utils
//
// PortMidi (as most native MIDI APIs) operate on complete MIDI messages
// we need therefore handle running status, variable number of data bytes
// and sysex correctly.
//
// ============================================================================

/**
 * return number of databytes that should accompany 'status' byte
 * four bytes for sysex is a special case to simplify Midi_BuildEvent()
 */
static int Midi_GetDataLength(Uint8 status)
{
	static const Uint8 dataLength[] = { 2,2,2,2,1,2,2, 4,1,2,1,0,0,0,0 };

	if (status >= 0xF8 || status == 0)
		return 0;
	if (status >= 0xF0)
		return dataLength[(status & 0x0F) + 7];
	return dataLength[(status >> 4) - 8];
}

/**
 * collect bytes until valid MIDI event has been formed / four bytes of sysex data has been gathered
 * returns PmEvent when done, or NULL if it needs still more data
 * see MIDI 1.0 Detailed Spec 4.2, pages A-1..A-2 for discussion on running status
 */
static PmEvent* Midi_BuildEvent(Uint8 byte)
{
	static const Uint8 shifts[] = { 0,8,16,24 };
	static PmEvent midiEvent = { 0,0 };
	static Uint32 midimsg;
//	static Uint8 runningStatus = 0;
	static Uint8 bytesToWait = 0;
	static Uint8 bytesCollected = 0;
	static bool processingSysex = false;

	// -- status byte
	if (byte & 0x80)
	{
		if (byte >= 0xF8)
		{
			midiEvent.message = Pm_Message(byte,0,0);
			return &midiEvent;
		}
		else
		{
			processingSysex = false;
			if (byte >= 0xF0)
			{
//				runningStatus = 0;
				if (byte == 0xF0)
				{
					processingSysex = true;
					bytesCollected = 1;
				}
				else if (byte == 0xF7)
				{
					midiEvent.message = midimsg | (((Uint32)byte) << shifts[bytesCollected]);
					midimsg = bytesToWait = bytesCollected = 0;
					return &midiEvent;
				}
				else
					bytesCollected = 0;
			}
			else
			{
//				runningStatus = byte;
				bytesCollected = 0;
			}
		}
		midimsg = byte;
		bytesToWait = Midi_GetDataLength(byte);
	}

	// -- data byte
	else
	{
		if (processingSysex)
			midimsg |= ((Uint32)byte) << shifts[bytesCollected++];
		else
			midimsg |= ((Uint32)byte) << shifts[++bytesCollected];
		if (bytesCollected >= bytesToWait)
		{
			midiEvent.message = midimsg;
			midimsg = 0;
			bytesCollected = 0;
			bytesToWait = processingSysex ? 4 : 0;
			return &midiEvent;
		}
	}
	
	return NULL;
}


/**
 * extracts raw bytes from 'midiEvent' into 'msg'
 * returns number of bytes available in 'msg'
 *
 * this method is required for sysex handling
 * native framework has already handled running status
 */
static int Midi_SplitEvent(PmEvent* midiEvent, Uint8* msg)
{
	static bool processingSysex = false;
	PmMessage midiMessage = midiEvent->message;
	int i, bytesAvailable = 0;

	msg[0] = Pm_MessageStatus(midiMessage);

	// -- sysex start or continuation
	if ((msg[0] == 0xF0) || (msg[0] < 0x80))
	{
		if (msg[0] == 0xF0)
			processingSysex = true;

		if (processingSysex)
		{
			for (i = 0; i <= 3; i++)
			{
				msg[i] = midiMessage & 0xFF;
				bytesAvailable = i;
				if (msg[i] == 0xF7)
				{
					processingSysex = false;
					break;
				}
				midiMessage >>= 8;
			}
		}
		else bytesAvailable = -1;
	}

	// -- non-sysex
	else
	{
		if (msg[0] < 0xF8) // non-realtime
		{
			processingSysex = false;
			midiMessage >>= 8;
			bytesAvailable = Midi_GetDataLength(msg[0]);
			for (i = 1; i <= bytesAvailable; i++)
			{
				msg[i] = midiMessage & 0xFF;
				midiMessage >>= 8;
			}
		}
	}

	return bytesAvailable + 1;
}
#endif
