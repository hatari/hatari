/*
  Hatari

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Reset emulation state.
*/
const char Reset_fileid[] = "Hatari reset.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "cart.h"
#include "dmaSnd.h"
#include "crossbar.h"
#include "fdc.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "acia.h"
#include "ikbd.h"
#include "cycInt.h"
#include "m68000.h"
#include "mfp.h"
#include "midi.h"
#include "psg.h"
#include "reset.h"
#include "screen.h"
#include "sound.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"
#include "nvram.h"
#include "video.h"
#include "falcon/videl.h"
#include "falcon/dsp.h"
#include "debugcpu.h"
#include "debugdsp.h"

/*-----------------------------------------------------------------------*/
/**
 * Reset ST emulator states, chips, interrupts and registers.
 * Return zero or negative TOS image load error code.
 */
static int Reset_ST(bool bCold)
{
	if (bCold)
	{
		int ret;

		Floppy_GetBootDrive();      /* Find which device to boot from (A: or C:) */

		ret = TOS_LoadImage();      /* Load TOS, writes into cartridge memory */
		if (ret)
			return ret;               /* If we can not load a TOS image, return now! */

		Cart_ResetImage();          /* Load cartridge program into ROM memory. */
		Cart_Patch();
	}
	CycInt_Reset();               /* Reset interrupts */
	MFP_Reset();                  /* Setup MFP chip */
	Video_Reset();                /* Reset video */
	VDI_Reset();                  /* Reset internal VDI variables */
	NvRam_Reset();                /* reset NvRAM (video) settings */

	GemDOS_Reset();               /* Reset GEMDOS emulation */
	if (bCold)
	{
		FDC_Reset( bCold );	/* Reset FDC */
	}
	Floppy_Reset();			/* Reset Floppy */

	if (ConfigureParams.System.nMachineType == MACHINE_FALCON
	    || ConfigureParams.System.nMachineType == MACHINE_TT)
	{
		Ncr5380_Reset();
	}

	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
	{
		DSP_Reset();                  /* Reset the DSP */
		Crossbar_Reset(bCold);        /* Reset Crossbar sound */
	}
	else
		DmaSnd_Reset(bCold);          /* Reset DMA sound */

	PSG_Reset();                  /* Reset PSG */
	Sound_Reset();                /* Reset Sound */
	ACIA_Reset( ACIA_Array );     /* ACIA */
	IKBD_Reset(bCold);            /* Keyboard (after ACIA) */
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON && !bUseVDIRes)
		VIDEL_reset();
	else
		Screen_Reset();               /* Reset screen */
	M68000_Reset(bCold);          /* Reset CPU */

	DebugCpu_SetDebugging();      /* Re-set debugging flag if needed */
	DebugDsp_SetDebugging();

	Midi_Reset();

	/* Start HBL, Timer B and VBL interrupts with a 0 cycle delay */
	Video_StartInterrupts( 0 );

	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Cold reset ST (reset memory, all registers and reboot)
 */
int Reset_Cold(void)
{
	/* Set mouse pointer to the middle of the screen */
	Main_WarpMouse(sdlscrn->w/2, sdlscrn->h/2, false);

	return Reset_ST(true);
}


/*-----------------------------------------------------------------------*/
/**
 * Warm reset ST (reset registers, leave in same state and reboot)
 */
int Reset_Warm(void)
{
	return Reset_ST(false);
}
