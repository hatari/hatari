/*
  Hatari - hatari-glue.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains some code to glue the UAE CPU core to the rest of the
  emulator and Hatari's "illegal" opcodes.
*/
static char rcsid[] = "Hatari $Id: hatari-glue.c,v 1.15 2003-03-31 13:34:29 thothy Exp $";


#include <stdio.h>

#include "../includes/main.h"
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

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif


int illegal_mem = FALSE;
int address_space_24 = TRUE;
int cpu_level = 0;              /* 68000 (default) */
int cpu_compatible = FALSE;

int requestedInterrupt = -1;

long STmem_size = 0x400000;     /* 4MB */
long TTmem_size = 0;


/* Reset custom chips */
void customreset(void)
{
  requestedInterrupt = -1;
}


/* Return interrupt number (1 - 7), -1 means no interrupt. */
int intlev(void)
{
  int ret = requestedInterrupt;
  requestedInterrupt = -1;

  return ret;
}


/* Initialize 680x0 emulation */
int Init680x0(void)
{
  memory_init();

  init_m68k();
  return TRUE;
}


/* Deinitialize 680x0 emulation */
void Exit680x0(void)
{
}


/* Reset and start 680x0 emulation */
void Start680x0(void)
{
  m68k_reset();
  m68k_go(TRUE);
}


/* Check if the CPU type has been changed */
void check_prefs_changed_cpu(int new_level, int new_compatible)
{
  if(cpu_level!=new_level || cpu_compatible!=new_compatible)
  {
    cpu_level = new_level;
    cpu_compatible = new_compatible;
    set_special(SPCFLAG_MODE_CHANGE);
    if (table68k)
      build_cpufunctbl ();
  }
}



/* ----------------------------------------------------------------------- */
/*
  We use an illegal opcode to set ConnectedDrives, as TOS (or an HD driver)
  alters this value we cannot set it on init.
  This opcode replaces the RTS at the end of the DMA bus boot routine
*/
unsigned long OpCode_ConnectedDrive(uae_u32 opcode)
{
  fprintf(stderr, "OpCode_ConnectedDrive handled (%x)\n",ConnectedDriveMask );
  /* Set connected drives */
  STMemory_WriteLong(0x4c2, ConnectedDriveMask);
  /* do an RTS (the opcode we replaced) */
  m68k_setpc(longget(m68k_areg(regs, 7)));
  m68k_areg(regs, 7) += 4;

  fill_prefetch_0();
  return 4;
}


/* ----------------------------------------------------------------------- */
/*
  Re-direct execution to old GEMDOS calls, used in 'cart.s'
*/
unsigned long OpCode_OldGemDos(uae_u32 opcode)
{
  m68k_setpc( STMemory_ReadLong(CART_OLDGEMDOS) );
  fill_prefetch_0();
  return 4;
}


/* ----------------------------------------------------------------------- */
/*
  Intercept GEMDOS calls (setup vector $84)
  The vector is setup when the gemdos-opcode is run the first time, by
  the cartridge routine. (after gemdos init, before booting floppies)

  After this, our vector will be used, and handled by the GemDOS_OpCode
  routine in gemdos.c
*/
unsigned long OpCode_GemDos(uae_u32 opcode)
{
  if(!bInitGemDOS)
  {
    /* Init on boot - see cartimg.c */
    GemDOS_Boot();

    /* We use this to get pointer to Line-A structure details
     * (to fix for extended VDI res) */
    LineABase = regs.regs[0];  /* D0 */
    FontBase = regs.regs[9];   /* A1 */
    VDI_LineA();
  }
  else
  {
    GemDOS_OpCode();    /* handler code in gemdos.c */
  }

  m68k_incpc(2);
  fill_prefetch_0();
  return 4;
}


/* ----------------------------------------------------------------------- */
/*
  Modify TimerD in GEMDos to gain more desktop performance

  Obviously, we need to emulate all timers correctly but GemDOS set's up Timer D at a very
  high rate(every couple of instructions). The interrupts isn't enabled but WinSTon still
  needs to process the interrupt table and this HALVES our frame rate!!! (It causes a cache
  reload each time). Some games actually reference this timer but don't set it up(eg Paradroid,
  Speedball I) so we simply intercept the Timer D setup code in GemDOS and fix the numbers
  with more 'laid-back' values. This still keeps 100% compatibility
*/
unsigned long OpCode_TimerD(uae_u32 opcode)
{
  /*fprintf(stderr, "OpCode_TimerD handled\n");*/
  m68k_dreg(regs,0)=3;	/* 3 = Select Timer D */
  m68k_dreg(regs,1)=7;	/* 1 = /4 for 9600 baud(used /200) */
  m68k_dreg(regs,2)=100;	/* 2 = 9600 baud(100) */
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
