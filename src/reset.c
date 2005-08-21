/*
  Hatari

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Reset emulation state.
*/
char Reset_rcsid[] = "Hatari $Id: reset.c,v 1.10 2005-08-21 21:13:43 thothy Exp $";

#include "main.h"
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
#include "video.h"


/*-----------------------------------------------------------------------*/
/*
  Cold reset ST (reset memory, all registers and reboot)
*/
int Reset_Cold(void)
{
  Main_WarpMouse(sdlscrn->w/2, sdlscrn->h/2);  /* Set mouse pointer to the middle of the screen */

  return Reset_ST(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Warm reset ST (reset registers, leave in same state and reboot)
*/
int Reset_Warm(void)
{
  return Reset_ST(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Reset ST emulator states, chips, interrupts and registers
*/
int Reset_ST(BOOL bCold)
{
  if (bCold)
  {
    int ret;

    STMemory_Clear(0x00000000, 0x00400000);   /* Clear First 4Mb */
    STMemory_Clear(0x00e00000, 0x00ffffff);   /* Clear Upper memory */
    STMemory_WriteLong(4, 0x00fc0000);        /* Set reset vector */
    STMemory_WriteLong(0, 0x0000f000);        /* And reset stack pointer */

    Floppy_GetBootDrive();      /* Find which device to boot from(A: or C:) */
    Cart_LoadImage();           /* Load program into cartridge memory. Used for gemdos loading */

    ret = TOS_LoadImage();      /* Load TOS, writes into cartridge memory */
    if(ret)
    {
      return ret;               /* If we can not load a TOS image, return now! */
    }
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
  Screen_Reset();               /* Reset screen */
  M68000_Reset(bCold);          /* Reset CPU */

  /* And VBL interrupt, MUST always be one interrupt ready to trigger */
  Int_AddAbsoluteInterrupt(CYCLES_ENDLINE, INTERRUPT_VIDEO_ENDLINE);
  Int_AddAbsoluteInterrupt(CYCLES_HBL, INTERRUPT_VIDEO_HBL);
  Int_AddAbsoluteInterrupt(CYCLES_PER_FRAME, INTERRUPT_VIDEO_VBL);

  return 0;
}
