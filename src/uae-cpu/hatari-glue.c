/*
  Hatari - hatari-glue.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains some code to glue the UAE CPU core to the rest of the
  emulator and Hatari's "illegal" opcodes.
*/
const char HatariGlue_rcsid[] = "Hatari $Id: hatari-glue.c,v 1.29 2008-02-24 20:10:48 thothy Exp $";


#include <stdio.h>

#include "../includes/main.h"
#include "../includes/configuration.h"
#include "../includes/int.h"
#include "../includes/tos.h"
#include "../includes/gemdos.h"
#include "../includes/cart.h"
#include "../includes/vdi.h"
#include "../includes/stMemory.h"

#include "sysdeps.h"
#include "maccess.h"
#include "memory.h"
#include "newcpu.h"
#include "hatari-glue.h"

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif


struct uae_prefs currprefs, changed_prefs;

int pendingInterrupts = 0;


/* Reset custom chips */
void customreset(void)
{
  pendingInterrupts = 0;
}


/* Return interrupt number (1 - 7), -1 means no interrupt.
 * Note that the interrupt stays pending if it can't be executed yet
 * due to the interrupt level field in the SR. */
int intlev(void)
{
  /* There are only VBL and HBL autovector interrupts in the ST... */
  assert((pendingInterrupts & ~((1<<4)|(1<<2))) == 0);

  if(pendingInterrupts & (1 << 4))          /* VBL interrupt? */
  {
    if(regs.intmask < 4)
      pendingInterrupts &= ~(1 << 4);
    return 4;
  }
  else if(pendingInterrupts & (1 << 2))     /* HBL interrupt? */
  {
    if(regs.intmask < 2)
      pendingInterrupts &= ~(1 << 2);
    return 2;
  }

  return -1;
}


/* Initialize 680x0 emulation */
int Init680x0(void)
{
  currprefs.cpu_level = changed_prefs.cpu_level = ConfigureParams.System.nCpuLevel;
  currprefs.cpu_compatible = changed_prefs.cpu_compatible = ConfigureParams.System.bCompatibleCpu;
  currprefs.address_space_24 = changed_prefs.address_space_24 = TRUE;

  init_m68k();
  return TRUE;
}


/* Deinitialize 680x0 emulation */
void Exit680x0(void)
{
  memory_uninit();

  free(table68k);
  table68k = NULL;
}


/* Check if the CPU type has been changed */
void check_prefs_changed_cpu(void)
{
  if (currprefs.cpu_level != changed_prefs.cpu_level
      || currprefs.cpu_compatible != changed_prefs.cpu_compatible)
  {
    currprefs.cpu_level = changed_prefs.cpu_level;
    currprefs.cpu_compatible = changed_prefs.cpu_compatible;
    set_special(SPCFLAG_MODE_CHANGE);
    build_cpufunctbl ();
  }
}


/* ----------------------------------------------------------------------- */
/*
  This function will be called at system init by the cartridge routine
  (after gemdos init, before booting floppies).
  
  The GEMDOS vector (#$84) is setup and we also initialize the connected
  drive mask and Line-A  variables (for an extended VDI resolution) from here.
*/
unsigned long OpCode_SysInit(uae_u32 opcode)
{
  /* Initialize the connected drive mask */
  STMemory_WriteLong(0x4c2, ConnectedDriveMask);

  if(!bInitGemDOS)
  {
    /* Init on boot - see cart.c */
    GemDOS_Boot();

    /* Update LineA for extended VDI res
     * D0: LineA base, A1: Font base
     */
    VDI_LineA(regs.regs[0], regs.regs[9]);
  }

  m68k_incpc(2);
  fill_prefetch_0();
  return 4;
}


/* ----------------------------------------------------------------------- */
/*
  Intercept GEMDOS calls

  Used for GEMDOS HD emulation (see gemdos.c).
*/
unsigned long OpCode_GemDos(uae_u32 opcode)
{
  GemDOS_OpCode();    /* handler code in gemdos.c */

  m68k_incpc(2);
  fill_prefetch_0();
  return 4;
}


/*-----------------------------------------------------------------------*/
/*
  This is called after completion of each VDI call
*/
unsigned long OpCode_VDI(uae_u32 opcode)
{
  VDI_Complete();

  /* Set PC back to where originated from to continue instruction decoding */
  m68k_setpc(VDI_OldPC);

  fill_prefetch_0();
  return 4;
}
