/*
  Hatari - mfp.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  MFP - Multi Functional Peripheral. In emulation terms it's the 'chip from
  hell' - most differences between a real machine and an emulator are down to
  this chip. It seems very simple at first but the implementation is very
  difficult.
  The following code is the very accurate for an ST emulator as it is able to
  perform Spectrum 512 raster effects as well as simulate the quirks found in
  the chip. The easiest way to 'see' the MFP chip is to look at the diagram.
  It shows the main details of the chip's behaviour with regard to interrupts
  and pending/service bits.
*/
static char rcsid[] = "Hatari $Id: mfp.c,v 1.5 2003-03-08 11:29:53 thothy Exp $";

#include "main.h"
#include "debug.h"
#include "decode.h"
#include "fdc.h"
#include "ikbd.h"
#include "int.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "misc.h"
#include "psg.h"
#include "screen.h"
#include "shortcut.h"
#include "sound.h"
#include "stMemory.h"
#include "ymFormat.h"
#include "video.h"


/*
  MFP interrupt channel circuit:-

  EdgeRegister   EnableRegister                         MaskRegister             SBit
        |                |                                     |                     |
        |                |                                     |                     |          ------------------------
        |                |         ------------------------    ---\                  |---\      |                      |
        |                o--\      |                      |        AND---o----------------AND---| S InterruptInService |
        ---\             |   AND---| S InterruptPending O |-------/      |           |---/      |                      |
            XOR----------)--/      |          R           |              |           |          ------------------------
Input -----/             |         ------------------------              |           |
                         |                    |                   InterruptRequest   |
                        NOT                  OR                                      |
                         |                  |  |                                     |
                         --------------------  --------------------------------------o--- PassVector
*/

#define  IGNORE_FDC_PRIORITY    0xff    /* Set 0xff if FDC has higher priority than Keyboard/Midi, or 0x7f if not - Dragon's Breath will now work! */

/* MFP Registers */
unsigned char MFP_GPIP;               /* General Purpose Pins */
unsigned char MFP_AER,MFP_DDR;        /* Active Edge Register, Data Direction Register */
unsigned char MFP_IERA,MFP_IERB;      /* Interrupt Enable Registers A,B  0xfffa07,0xfffa09 */
unsigned char MFP_IPRA,MFP_IPRB;      /* Interrupt Pending Registers A,B  0xfffa0b,0xfffa0d */
unsigned char MFP_ISRA,MFP_ISRB;      /* Interrupt In-Service Registers A,B  0xfffa0f,0xfffa11 */
unsigned char MFP_IMRA,MFP_IMRB;      /* Interrupt Mask Registers A,B  0xfffa13,0xfffa15 */
unsigned char MFP_VR;                 /* Vector Register  0xfffa17 */
unsigned char MFP_TACR,MFP_TBCR,MFP_TCDCR;  /* Timer A,B,C+D Control Registers */
unsigned char MFP_TADR,MFP_TBDR;      /* Timer A,B Data Registers */
unsigned char MFP_TCDR,MFP_TDDR;      /* Timer C,D Data Registers */
unsigned char MFP_TA_MAINCOUNTER;     /* Timer A Main Counter (internal to MFP) */
unsigned char MFP_TB_MAINCOUNTER;     /* Timer B Main Counter */
unsigned char MFP_TC_MAINCOUNTER;     /* Timer C Main Counter (these are temp's, set when read as) */
unsigned char MFP_TD_MAINCOUNTER;     /* Timer D Main Counter (as done via interrupts) */

/* CPU clock cycle counts for each timer */
int TimerAClockCycles=0;
int TimerBClockCycles=0;
int TimerCClockCycles=0;
int TimerDClockCycles=0;

/*
 Number of CPU cycles for Timer C+D
 These figures were based on 50Hz=160256cycles, so 200Hz=40064
 Now, Timer C set on a delay of 192($C0) and a preset DIV of 64 is 200Hz
 This makes the table entry 208.66666*192=40064(200Hz)
*/
float MFPTimerToCPUCycleTable[] = {
   0,             /* Timer Stop */
   13.04166667f,  /* Div by 4  */
   32.60416667f,  /* Div by 10 */
   52.16666667f,  /* Div by 16 */
  163.02083333f,  /* Div by 50 */
  208.66666667f,  /* Div by 64 */
  326.04166667f,  /* Div by 100 */
  652.08333333f   /* Div by 200 */
};


/*-----------------------------------------------------------------------*/
/*
  Reset all MFP variables and start interrupts on their way!
*/
void MFP_Reset(void)
{
  /* Reset MFP internal variables */
  MFP_GPIP = 0xff;          /* Set GPIP register (all 1's = no interrupts) */
  MFP_AER = MFP_DDR = 0;
  MFP_IERA = MFP_IERB = 0;
  MFP_IPRA = MFP_IPRB = 0;
  MFP_ISRA = MFP_ISRB = 0;
  MFP_IMRA = MFP_IMRB = 0;
  MFP_VR = 0;
  MFP_TACR = MFP_TBCR = MFP_TCDCR = 0;
  MFP_TADR = MFP_TBDR = 0;
  MFP_TCDR = MFP_TDDR = 0;
  MFP_TA_MAINCOUNTER = MFP_TB_MAINCOUNTER = MFP_TC_MAINCOUNTER = MFP_TD_MAINCOUNTER = 0;

  /* Clear counters */
  TimerAClockCycles = TimerBClockCycles = TimerCClockCycles = TimerDClockCycles = 0;
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void MFP_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  MemorySnapShot_Store(&MFP_GPIP,sizeof(MFP_GPIP));
  MemorySnapShot_Store(&MFP_AER,sizeof(MFP_AER));
  MemorySnapShot_Store(&MFP_DDR,sizeof(MFP_DDR));
  MemorySnapShot_Store(&MFP_IERA,sizeof(MFP_IERA));
  MemorySnapShot_Store(&MFP_IERB,sizeof(MFP_IERB));
  MemorySnapShot_Store(&MFP_IPRA,sizeof(MFP_IPRA));
  MemorySnapShot_Store(&MFP_IPRB,sizeof(MFP_IPRB));
  MemorySnapShot_Store(&MFP_ISRA,sizeof(MFP_ISRA));
  MemorySnapShot_Store(&MFP_ISRB,sizeof(MFP_ISRB));
  MemorySnapShot_Store(&MFP_IMRA,sizeof(MFP_IMRA));
  MemorySnapShot_Store(&MFP_IMRB,sizeof(MFP_IMRB));
  MemorySnapShot_Store(&MFP_VR,sizeof(MFP_VR));
  MemorySnapShot_Store(&MFP_TACR,sizeof(MFP_TACR));
  MemorySnapShot_Store(&MFP_TBCR,sizeof(MFP_TBCR));
  MemorySnapShot_Store(&MFP_TCDCR,sizeof(MFP_TCDCR));
  MemorySnapShot_Store(&MFP_TADR,sizeof(MFP_TADR));
  MemorySnapShot_Store(&MFP_TBDR,sizeof(MFP_TBDR));
  MemorySnapShot_Store(&MFP_TCDR,sizeof(MFP_TCDR));
  MemorySnapShot_Store(&MFP_TDDR,sizeof(MFP_TDDR));
  MemorySnapShot_Store(&MFP_TA_MAINCOUNTER,sizeof(MFP_TA_MAINCOUNTER));
  MemorySnapShot_Store(&MFP_TB_MAINCOUNTER,sizeof(MFP_TB_MAINCOUNTER));
  MemorySnapShot_Store(&MFP_TC_MAINCOUNTER,sizeof(MFP_TC_MAINCOUNTER));
  MemorySnapShot_Store(&MFP_TD_MAINCOUNTER,sizeof(MFP_TD_MAINCOUNTER));
  MemorySnapShot_Store(&TimerAClockCycles,sizeof(TimerAClockCycles));
  MemorySnapShot_Store(&TimerBClockCycles,sizeof(TimerBClockCycles));
  MemorySnapShot_Store(&TimerCClockCycles,sizeof(TimerCClockCycles));
  MemorySnapShot_Store(&TimerDClockCycles,sizeof(TimerDClockCycles));
}


/*-----------------------------------------------------------------------*/
/*
  Call MFP interrupt - NOTE when the MFP is in Auto interrupt (AEI), the MFP
  puts the interrupt number on the data bus and then the 68000 reads it, multiplies
  it by 4 and adds in a base(usually 0x100) to give the vector. Some programs
  change this offset, eg RoboCod. This offset is stored in the top 4 bits of register
  0xfffa17(0x40 is the default=0x100)
  Many thanks to Steve Bak for that one!
*/
void MFP_Exception(int Interrupt)
{
  unsigned int Vec;

  Vec = (unsigned int)(MFP_VR&0xf0)<<2;
  Vec += Interrupt<<2;
  ExceptionVector = Vec;
  M68000_Exception();
}


/*-----------------------------------------------------------------------*/
/*
  Test interrupt request to see if can cause exception,return TRUE if pass vector
*/
BOOL InterruptRequest(int Exception,unsigned char Bit,unsigned long EnableAddr,unsigned char *pPendingReg,unsigned char MaskRegister,unsigned char PriorityMaskLow,unsigned char PriorityMaskHigh,unsigned char *pInServiceReg)
{
  /* Are any higher priority interupts in service? */
  if ( ((MFP_ISRA&PriorityMaskLow)==0) && ((MFP_ISRB&PriorityMaskHigh)==0) )
  {
    MakeSR();
    /* Is masked? */
    if (MaskRegister&Bit)
    {
#if 0
      if (7>FIND_IPL)
      {
        *pPendingReg &= ~Bit;        /* Clear pending bit */
        MFP_UpdateFlags();
      }
#endif

      /* CPU allows interrupt of an MFP level? */
      if (6>FIND_IPL)
      {
        *pPendingReg &= ~Bit;           /* Clear pending bit */
        MFP_UpdateFlags();

        /* Are we in 'auto' interrupt or 'manual'? */
        if (MFP_VR&0x08)                /* Software End-of-Interrupt (SEI) */
          *pInServiceReg |= Bit;        /* Set interrupt in service register */
        else
          *pInServiceReg &= ~Bit;       /* Clear interrupt in service register */

        /* Call interrupt, adds in base (default 0x100) */
        MFP_Exception(Exception);
        return(TRUE);
      }
    }
  }

  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  MFP circuit has pending interrupt, request
*/
void MFP_RequestInterrupt_TimerA(void)
{
  InterruptRequest(MFP_EXCEPT_TIMERA,MFP_TIMER_A_BIT,MFP_IERA,&MFP_IPRA,MFP_IMRA,0xe0,0x00&IGNORE_FDC_PRIORITY,&MFP_ISRA);
}
void MFP_RequestInterrupt_TimerB(void)
{
  InterruptRequest(MFP_EXCEPT_TIMERB,MFP_TIMER_B_BIT,MFP_IERA,&MFP_IPRA,MFP_IMRA,0xff,0x00&IGNORE_FDC_PRIORITY,&MFP_ISRA);
}
void MFP_RequestInterrupt_TimerC(void)
{
  InterruptRequest(MFP_EXCEPT_TIMERC,MFP_TIMER_C_BIT,MFP_IERB,&MFP_IPRB,MFP_IMRB,0xff,0xe0&IGNORE_FDC_PRIORITY,&MFP_ISRB);
}
void MFP_RequestInterrupt_TimerD(void)
{
  InterruptRequest(MFP_EXCEPT_TIMERD,MFP_TIMER_D_BIT,MFP_IERB,&MFP_IPRB,MFP_IMRB,0xff,0xf0&IGNORE_FDC_PRIORITY,&MFP_ISRB);
}
void MFP_RequestInterrupt_Keyboard(void)
{
  InterruptRequest(MFP_EXCEPT_KEYBOARD,MFP_KEYBOARD_BIT,MFP_IERB,&MFP_IPRB,MFP_IMRB,0xff,0xc0&IGNORE_FDC_PRIORITY,&MFP_ISRB);
}
void MFP_RequestInterrupt_Floppy(void)
{
  InterruptRequest(MFP_EXCEPT_GPIP5,MFP_FDCHDC_BIT,MFP_IERB,&MFP_IPRB,MFP_IMRB,0xff,0x80&IGNORE_FDC_PRIORITY,&MFP_ISRB);
}


/*-----------------------------------------------------------------------*/
/*
  Check 'pending' registers to see if any MFP interrupts need servicing
*/
void MFP_CheckPendingInterrupts(void)
{
 unsigned short i;

 i = (MFP_IPRA<<8) | MFP_IPRB;

 if( (i&0x21f0)==0 )
  {    /* Should never get here, but if do just clear flag (see 'MFP_UpdateFlags') */
   PendingInterruptFlag &= CLEAR_PENDING_INTERRUPT_FLAG_MFP;
   return;
  }

 if( i&0x2000 )  MFP_RequestInterrupt_TimerA();    /* Check Timer A (bit 5) */
 if( i&0x0100 )  MFP_RequestInterrupt_TimerB();    /* Check Timer B (bit 0) */
 if( i&0x0080 )  MFP_RequestInterrupt_Floppy();    /* Check FDC (bit 7) */
 if( i&0x0040 )  MFP_RequestInterrupt_Keyboard();  /* Check Keyboard (bit 6) */
 if( i&0x0020 )  MFP_RequestInterrupt_TimerC();    /* Check Timer C (bit 5) */
 if( i&0x0010 )  MFP_RequestInterrupt_TimerD();    /* Check Timer D (bit 4) */
}


/*-----------------------------------------------------------------------*/
/*
  This is called whenever the MFP_IPRA or MFP_IPRB registers are modified.
  We set the 'PendingInterruptFlag' accordingly (to say if an MFP interrupt
  is to be checked) so we only have one compare during the decode
  instruction loop.
*/
void MFP_UpdateFlags(void)
{
 if( MFP_IPRA|MFP_IPRB )
   {
    PendingInterruptFlag |= PENDING_INTERRUPT_FLAG_MFP;
   }
  else
   {
    PendingInterruptFlag &= CLEAR_PENDING_INTERRUPT_FLAG_MFP;
   }
}


/*-----------------------------------------------------------------------*/
/*
  Interrupt Channel is active, set pending bit so can be serviced
*/
void MFP_InputOnChannel(unsigned char Bit,unsigned char EnableBit,unsigned char *pPendingReg)
{
  /* Input has occurred on MFP channel, set interrupt pending to request interrupt when able */
  if (EnableBit&Bit)
    *pPendingReg |= Bit;           /* Set bit */
  else
    *pPendingReg &= ~Bit;          /* Clear bit */
  MFP_UpdateFlags();
}


/*-----------------------------------------------------------------------*/
/*
  Generate Timer A Interrupt when in Event Count mode
*/
void MFP_TimerA_EventCount_Interrupt(void)
{
  if (MFP_TA_MAINCOUNTER==1) {          /* Timer expired? If so, generate interrupt */
    MFP_TA_MAINCOUNTER = MFP_TADR;      /* Reload timer from data register */

    /* Acknowledge in MFP circuit, pass bit,enable,pending */
    MFP_InputOnChannel(MFP_TIMER_A_BIT,MFP_IERA,&MFP_IPRA);
  }
  else
    MFP_TA_MAINCOUNTER--;               /* Subtract timer main counter */
}


/*-----------------------------------------------------------------------*/
/*
  Generate Timer B Interrupt when in Event Count mode
*/
void MFP_TimerB_EventCount_Interrupt(void)
{
  if (MFP_TB_MAINCOUNTER==1) {          /* Timer expired? If so, generate interrupt */
    MFP_TB_MAINCOUNTER = MFP_TBDR;      /* Reload timer from data register */

    /* Acknowledge in MFP circuit, pass bit,enable,pending */
    MFP_InputOnChannel(MFP_TIMER_B_BIT,MFP_IERA,&MFP_IPRA);
  }
  else
    MFP_TB_MAINCOUNTER--;               /* Subtract timer main counter */
}


/*-----------------------------------------------------------------------*/
/*
  Start Timer A or B - EventCount mode is done in HBL handler to time correctly
*/
int MFP_StartTimer_AB(unsigned char TimerControl, unsigned int TimerData, int Handler, BOOL bFirstTimer)
{
  int TimerClockCycles = 0;

  /* If we are in event-count mode ignore this(done on HBL) */
  if (TimerControl!=0x08) {
    /* Find number of CPU cycles for when timer is due(include preset and counter) */
    /* As timer occurs very often we multiply by counter to speed up emulator */
    if (TimerData==0)                   /* Data=0 is actually Data=256 */
      TimerData = 256;
    TimerClockCycles = ROUND_CYCLES_TO4( TimerData*MFPTimerToCPUCycleTable[TimerControl&0x7] );

    /* And add to our internal interrupt list, if timer cycles is zero then timer is stopped */
    Int_RemovePendingInterrupt(Handler);
    if (TimerClockCycles) {
      /* Start timer from now? If not continue timer so from original offset */
      if (bFirstTimer)
        nCyclesOver = 0;
      Int_AddRelativeInterrupt(TimerClockCycles,Handler);
    }
  }
  else {
    /* Make sure no outstanding interrupts in list if channel is disabled */
    Int_RemovePendingInterrupt(Handler);
  }

  return(TimerClockCycles);
}


/*-----------------------------------------------------------------------*/
/*
  Start Timer C or D
*/
int MFP_StartTimer_CD(unsigned char TimerControl, unsigned int TimerData, int Handler, BOOL bFirstTimer)
{
  int TimerClockCycles = 0;

  /* Is timer on? */
  if ((TimerControl&0x7)!=0) {
    /* Find number of cycles for when timer is due(include preset and counter) */
    /* As timer occurs very often we multiply by counter to speed up emulator */
    if (TimerData==0)                   /* Data=0 is actually Data=256 */
      TimerData = 256;
    TimerClockCycles = ROUND_CYCLES_TO4( TimerData*MFPTimerToCPUCycleTable[TimerControl&0x7] );

    /* And add to our internal interrupt list, if timer cycles is zero then timer is stopped */
    Int_RemovePendingInterrupt(Handler);
    if (TimerClockCycles) {
      /* Start timer from now? If not continue timer so from original offset */
      if (bFirstTimer)
        nCyclesOver = 0;
      Int_AddRelativeInterrupt(TimerClockCycles,Handler);
    }
  }
  else {
    /* Make sure no outstanding interrupts in list if channel is disabled */
    Int_RemovePendingInterrupt(Handler);
  }

  return(TimerClockCycles);
}


/*-----------------------------------------------------------------------*/
/*
  Read Timer A or B - If in EventCount MainCounter already has correct value
*/
unsigned char MFP_ReadTimer_AB(unsigned char TimerControl, unsigned char MainCounter, int TimerCycles, int Handler)
{
  int TimerCyclesPassed;

  /* Find TimerAB count, if no interrupt assume in Event Count mode so already up-to-date as kept by HBL */
  if (Int_InterruptActive(Handler)) {
    /* Find cycles passed since last interrupt */
    TimerCyclesPassed = TimerCycles-Int_FindCyclesPassed(Handler);
    MainCounter = TimerCyclesPassed/(MFPTimerToCPUCycleTable[TimerControl&0x7]);
  }
  
  return(MainCounter);
}


/*-----------------------------------------------------------------------*/
/*
  Read Timer C or D
*/
unsigned char MFP_ReadTimerCD(unsigned char TimerControl,unsigned char TimerData,  unsigned char MainCounter, int TimerCycles, int Handler)
{
  int TimerCyclesPassed;

  /* Find TimerCD count. If not one then timer should be off and can find count from main counter */
  if (Int_InterruptActive(Handler)) {
    /* Find cycles passed since last interrupt */
    TimerCyclesPassed = TimerCycles-Int_FindCyclesPassed(Handler);
    MainCounter = TimerCyclesPassed/(MFPTimerToCPUCycleTable[TimerControl&0x7]);
  }
  else {
    MainCounter = TimerData;
  }

  return(MainCounter);
}


/*-----------------------------------------------------------------------*/
/*
  Start Timer A
  (This does not start the EventCount mode time as this is taken care of by the HBL)
*/
void MFP_StartTimerA(void)
{
  TimerAClockCycles = MFP_StartTimer_AB(MFP_TACR,MFP_TADR,INTERRUPT_MFP_TIMERA,TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Read Timer A
*/
void MFP_ReadTimerA(void)
{
  MFP_TA_MAINCOUNTER = MFP_ReadTimer_AB(MFP_TACR,MFP_TA_MAINCOUNTER,TimerAClockCycles,INTERRUPT_MFP_TIMERA);
}


/*-----------------------------------------------------------------------*/
/*
  Start Timer B
  (This does not start the EventCount mode time as this is taken care of by the HBL)
*/
void MFP_StartTimerB(void)
{
  TimerBClockCycles = MFP_StartTimer_AB(MFP_TBCR,MFP_TBDR,INTERRUPT_MFP_TIMERB,TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Read Timer B
*/
void MFP_ReadTimerB(void)
{
  MFP_TB_MAINCOUNTER = MFP_ReadTimer_AB(MFP_TBCR,MFP_TB_MAINCOUNTER,TimerBClockCycles,INTERRUPT_MFP_TIMERB);
}


/*-----------------------------------------------------------------------*/
/*
  Start Timer C
*/
void MFP_StartTimerC(void)
{
  TimerCClockCycles = MFP_StartTimer_CD(MFP_TCDCR>>4,MFP_TCDR,INTERRUPT_MFP_TIMERC,TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Read Timer C
*/
void MFP_ReadTimerC(void)
{
  MFP_TC_MAINCOUNTER = MFP_ReadTimerCD(MFP_TCDCR>>4,MFP_TCDR,MFP_TC_MAINCOUNTER,TimerCClockCycles,INTERRUPT_MFP_TIMERC);
}


/*-----------------------------------------------------------------------*/
/*
  Start Timer D
*/
void MFP_StartTimerD(void)
{
  TimerDClockCycles = MFP_StartTimer_CD(MFP_TCDCR,MFP_TDDR,INTERRUPT_MFP_TIMERD,TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Read Timer D
*/
void MFP_ReadTimerD(void)
{
  MFP_TD_MAINCOUNTER = MFP_ReadTimerCD(MFP_TCDCR,MFP_TDDR,MFP_TC_MAINCOUNTER,TimerDClockCycles,INTERRUPT_MFP_TIMERD);
}


/*-----------------------------------------------------------------------*/
/*
  Handle Timer A Interrupt
*/
void MFP_InterruptHandler_TimerA(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  /* Acknowledge in MFP circuit, pass bit,enable,pending */
  if ((MFP_TACR&0xf)!=0)                /* Is timer OK? */
    MFP_InputOnChannel(MFP_TIMER_A_BIT,MFP_IERA,&MFP_IPRA);

  /* Start next interrupt, if need one - from current cycle count */
  TimerAClockCycles = MFP_StartTimer_AB(MFP_TACR,MFP_TADR,INTERRUPT_MFP_TIMERA,FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Handle Timer B Interrupt
*/
void MFP_InterruptHandler_TimerB(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  /* Acknowledge in MFP circuit, pass bit, enable, pending */
  if ((MFP_TBCR&0xf)!=0)                /* Is timer OK? */
    MFP_InputOnChannel(MFP_TIMER_B_BIT,MFP_IERA,&MFP_IPRA);

  /* Start next interrupt, if need one - from current cycle count */
  TimerBClockCycles = MFP_StartTimer_AB(MFP_TBCR,MFP_TBDR,INTERRUPT_MFP_TIMERB,FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Handle Timer C Interrupt
*/
void MFP_InterruptHandler_TimerC(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  /* Acknowledge in MFP circuit, pass bit, enable, pending */
  if ((MFP_TCDCR&0x70)!=0)              /* Is timer OK? */
    MFP_InputOnChannel(MFP_TIMER_C_BIT,MFP_IERB,&MFP_IPRB);

  /* Start next interrupt, if need one - from current cycle count */
  TimerCClockCycles = MFP_StartTimer_CD(MFP_TCDCR>>4,MFP_TCDR,INTERRUPT_MFP_TIMERC,FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Handle Timer D Interrupt
*/
void MFP_InterruptHandler_TimerD(void)
{
  /* Remove this interrupt from list and re-order */
  Int_AcknowledgeInterrupt();

  /* Acknowledge in MFP circuit, pass bit, enable, pending */
  if ((MFP_TCDCR&0x07)!=0)              /* Is timer OK? */
    MFP_InputOnChannel(MFP_TIMER_D_BIT,MFP_IERB,&MFP_IPRB);

  /* Start next interrupt, if need one - from current cycle count */
  TimerDClockCycles = MFP_StartTimer_CD(MFP_TCDCR,MFP_TDDR,INTERRUPT_MFP_TIMERD,FALSE);
}

