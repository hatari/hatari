/*
  Hatari

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Reset emulation state.
*/
const char Reset_rcsid[] = "Hatari $Id: reset.c,v 1.26 2008-03-07 00:41:55 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "cart.h"
#include "dmaSnd.h"
#include "fdc.h"
#include "floppy.h"
#include "gemdos.h"
#include "ikbd.h"
#include "int.h"
#include "m68000.h"
#include "misc.h"
#include "mfp.h"
#include "psg.h"
#include "reset.h"
#include "screen.h"
#include "sound.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"
#include "video.h"
#include "falcon/videl.h"


/*-----------------------------------------------------------------------*/
/**
 * Reset ST emulator states, chips, interrupts and registers.
 * Return zero or negative TOS image load error code.
 */
static int Reset_ST(BOOL bCold)
{
	if (bCold)
	{
		int ret;

		Floppy_GetBootDrive();      /* Find which device to boot from (A: or C:) */

		ret = TOS_LoadImage();      /* Load TOS, writes into cartridge memory */
		if (ret)
			return ret;               /* If we can not load a TOS image, return now! */

		Cart_ResetImage();          /* Load cartridge program into ROM memory. */
	}
	Int_Reset();                  /* Reset interrupts */
	MFP_Reset();                  /* Setup MFP chip */
	Video_Reset();                /* Reset video */

	GemDOS_Reset();               /* Reset GEMDOS emulation */
	if (bCold)
	{
		FDC_Reset();                /* Reset FDC */
	}

	DmaSnd_Reset(bCold);          /* Reset DMA sound */
	PSG_Reset();                  /* Reset PSG */
	Sound_Reset();                /* Reset Sound */
	IKBD_Reset(bCold);            /* Keyboard */
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON && !bUseVDIRes)
		VIDEL_reset();
	else
		Screen_Reset();               /* Reset screen */
	M68000_Reset(bCold);          /* Reset CPU */

	/* Start HBL and VBL interrupts */
	Video_StartInterrupts();

	Int_AddAbsoluteInterrupt(FDC_DELAY_CYCLES, INT_CPU_CYCLE, INTERRUPT_FDC);

	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Cold reset ST (reset memory, all registers and reboot)
 */
int Reset_Cold(void)
{
	Main_WarpMouse(sdlscrn->w/2, sdlscrn->h/2);  /* Set mouse pointer to the middle of the screen */

	return Reset_ST(TRUE);
}


/*-----------------------------------------------------------------------*/
/**
 * Warm reset ST (reset registers, leave in same state and reboot)
 */
int Reset_Warm(void)
{
	return Reset_ST(FALSE);
}
