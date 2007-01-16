/*
  Hatari - midi.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  MIDI communication.
  Note that this code is far from being perfect. However, it is already
  enough to let some ST programs (e.g. the game Pirates!) use the host's midi
  system.

  TODO:
   - Midi input (??)
   - No exact timing yet: Sending MIDI data should probably rather be done
     with an "interrupt" from int.c (just like it is done by the code in
     ikbd.c).
   - Most bits in the ACIA's status + control registers are currently ignored.
   - Check when we have to clear the ACIA_SR_INTERRUPT_REQUEST bit in the
     ACIA status register (it is currently done when reading or writing to
     the data register, but probably it should rather be done when reading the
     status register?).
*/
const char Midi_rcsid[] = "Hatari $Id: midi.c,v 1.8 2007-01-16 18:42:59 thothy Exp $";

#include <SDL_types.h>

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "m68000.h"
#include "mfp.h"
#include "midi.h"


#define ACIA_SR_INTERRUPT_REQUEST  0x80


#define MIDI_DEBUG 0

#if MIDI_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


static FILE *pMidiOutFileHandle = NULL;         /* Used for Midi output */
static Uint8 MidiControlRegister;
static Uint8 MidiStatusRegister;


/*-----------------------------------------------------------------------*/
/**
 * Initialization: Open MIDI device.
 */
void Midi_Init(void)
{
	MidiStatusRegister = 2;

	if (ConfigureParams.Midi.bEnableMidi)
	{
		/* Open MIDI file... */
		pMidiOutFileHandle = fopen(ConfigureParams.Midi.szMidiOutFileName, "wb");

		if (!pMidiOutFileHandle)
		{
			fprintf(stderr, "Failed to open %s.\n", ConfigureParams.Midi.szMidiOutFileName);
			ConfigureParams.Midi.bEnableMidi = FALSE;
			return;
		}

		Dprintf(("Opened midi file %s.\n", ConfigureParams.Midi.szMidiOutFileName));
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Close MIDI device.
 */
void Midi_UnInit(void)
{
	if (pMidiOutFileHandle)
	{
		/* Close MIDI file... */
		fclose(pMidiOutFileHandle);
		pMidiOutFileHandle = NULL;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Read MIDI status register ($FFFC04).
 */
void Midi_Control_ReadByte(void)
{
	/* Dprintf(("Midi_ReadControl : $%x.\n", MidiStatusRegister)); */

	/* ACIA registers need wait states - but the value seems to vary in certain cases */
	M68000_WaitState(8);

	IoMem[0xfffc04] = MidiStatusRegister;
}


/*-----------------------------------------------------------------------*/
/**
 * Read MIDI data register ($FFFC06).
 */
void Midi_Data_ReadByte(void)
{
	Dprintf(("Midi_ReadData : $%x.\n", 1));

	/* ACIA registers need wait states - but the value seems to vary in certain cases */
	M68000_WaitState(8);

	MidiStatusRegister &= ~ACIA_SR_INTERRUPT_REQUEST;

	IoMem[0xfffc06] = 1;        /* Should be this? */
}


/*-----------------------------------------------------------------------*/
/**
 * Write to MIDI control register ($FFFC04).
 */
void Midi_Control_WriteByte(void)
{
	/* ACIA registers need wait states - but the value seems to vary in certain cases */
	M68000_WaitState(8);

	MidiControlRegister = IoMem[0xfffc04];

	Dprintf(("Midi_WriteControl($%x)\n", MidiControlRegister));

	/* Do we need to generate a transfer interrupt? */
	if ((MidiControlRegister & 0xA0) == 0xA0)
	{
		Dprintf(("WriteControl: Transfer interrupt!\n"));

		/* Acknowledge in MFP circuit, pass bit,enable,pending */
		MFP_InputOnChannel(MFP_ACIA_BIT, MFP_IERB, &MFP_IPRB);

		MidiStatusRegister |= ACIA_SR_INTERRUPT_REQUEST;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Write to MIDI data register ($FFFC06).
 */
void Midi_Data_WriteByte(void)
{
	Uint8 dataByte;

	/* ACIA registers need wait states - but the value seems to vary in certain cases */
	M68000_WaitState(8);

	dataByte = IoMem[0xfffc06];

	Dprintf(("Midi_WriteData($%x)\n", dataByte));

	MidiStatusRegister &= ~ACIA_SR_INTERRUPT_REQUEST;

	if (!ConfigureParams.Midi.bEnableMidi)
		return;

	if (pMidiOutFileHandle)
	{
		int ret;
		
		/* Write the character to the output file: */
		ret = fputc(dataByte, pMidiOutFileHandle);
		
		/* If there was an error then stop the midi emulation */
		if (ret == EOF)
		{
			Midi_UnInit();
			return;
		}

		/* Do not queue the midi data! */
		fflush(pMidiOutFileHandle);
	}

	/* Do we need to generate a transfer interrupt? */
	if ((MidiControlRegister & 0xA0) == 0xA0)
	{
		Dprintf(("WriteData: Transfer interrupt!\n"));

		/* Acknowledge in MFP circuit, pass bit,enable,pending */
		MFP_InputOnChannel(MFP_ACIA_BIT, MFP_IERB, &MFP_IPRB);

		MidiStatusRegister |= ACIA_SR_INTERRUPT_REQUEST;
	}
}
