
#include <stdio.h>

#include "../includes/main.h"
#include "../includes/int.h"
#include "../includes/tos.h"
#include "../includes/gemdos.h"
#include "../includes/cart.h"

#ifndef UAESYSDEPS
#include "sysdeps.h"
#endif
#ifndef UAEMEMORY
#include "memory.h"
#endif
#include "maccess.h"
#include "newcpu.h"

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif


int illegal_mem = FALSE;
int address_space_24 = 1;
int cpu_level = 0;              /* 68000 (default) */
int cpu_compatible = 0;

long STmem_size = 0x400000;     /* 4MB */
long TTmem_size = 0;


/* Reset custom chips */
void customreset(void)
{
 /* Taken from Reset_ST in reset.c: */
 Int_Reset();                           /* Reset interrupts */
 MFP_Reset();                           /* Setup MFP chip */
 Video_Reset();                         /* Reset video */
 PSG_Reset();                           /* Reset PSG */
 Sound_Reset();                         /* Reset Sound */
 IKBD_Reset(FALSE);                     /* Keyboard */
 Screen_Reset();                        /* Reset screen */

 /* And VBL interrupt, MUST always be one interrupt ready to trigger */
 Int_AddAbsoluteInterrupt(CYCLES_ENDLINE,INTERRUPT_VIDEO_ENDLINE);
 Int_AddAbsoluteInterrupt(CYCLES_HBL,INTERRUPT_VIDEO_HBL);
 Int_AddAbsoluteInterrupt(CYCLES_PER_FRAME,INTERRUPT_VIDEO_VBL);
}


/* Initialize 680x0 emulation, CheckROM() must have been called first */
int Init680x0(void)
{
  memory_init();

  init_m68k();
#ifdef USE_COMPILER
  compiler_init();
#endif
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


/* ----------------------------------------------------------------------- */
/*
  We use an illegal opcode to set ConnectedDrives, as TOS clears this
  value we cannot set it on init.
*/
unsigned long OpCode_ConnectedDrive(uae_u32 opcode)
{
  /* Set connected drives */
  STMemory_WriteWord(0x4c2, ConnectedDriveMask); 
  m68k_incpc(2);
  fill_prefetch_0();
  return 4;
}

/* ----------------------------------------------------------------------- */
/*
  Re-direct execution to old GEM calls, used in 'cart.s'
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
    GemDOS_Boot();      /* Init on boot - see cartimg.c */    
  else 
    GemDOS_OpCode();    /* handler code in gemdos.c */

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
