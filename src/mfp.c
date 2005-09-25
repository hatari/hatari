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
char MFP_rcsid[] = "Hatari $Id: mfp.c,v 1.20 2005-09-25 21:32:25 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "fdc.h"
#include "ikbd.h"
#include "int.h"
#include "ioMem.h"
#include "joy.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "psg.h"
#include "rs232.h"
#include "sound.h"
#include "tos.h"
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


/*-----------------------------------------------------------------------*/
/* Set clock times for each instruction, see '68000 timing' pages for details */
#define  ROUND_CYCLES_TO4(var)  (((int)(var)+3)&0xfffffffc)


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
static int TimerAClockCycles=0;
static int TimerBClockCycles=0;
static int TimerCClockCycles=0;
static int TimerDClockCycles=0;

BOOL bAppliedTimerDPatch;             /* TRUE if the Timer-D patch has been applied */
static int nTimerDFakeValue;          /* Faked Timer-D data register for the Timer-D patch */


/*
 Number of CPU cycles for Timer C+D
 These figures were based on 50Hz=160256cycles, so 200Hz=40064
 Now, Timer C set on a delay of 192($C0) and a preset DIV of 64 is 200Hz
 This makes the table entry 208.66666*192=40064(200Hz)
*/
static float MFPTimerToCPUCycleTable[] = {
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

  bAppliedTimerDPatch = FALSE;

  MFP_GPIP = 0xff;
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
static void MFP_Exception(int Interrupt)
{
  unsigned int Vec;

  Vec = (unsigned int)(MFP_VR&0xf0)<<2;
  Vec += Interrupt<<2;
  M68000_Exception(Vec);
}


/*-----------------------------------------------------------------------*/
/*
  Test interrupt request to see if can cause exception,return TRUE if pass vector
*/
static BOOL MFP_InterruptRequest(int nMfpException, unsigned char Bit, unsigned char *pPendingReg, unsigned char MaskRegister,
                                 unsigned char PriorityMaskLow, unsigned char PriorityMaskHigh, unsigned char *pInServiceReg)
{
  /* Are any higher priority interupts in service? */
  if ( ((MFP_ISRA&PriorityMaskLow)==0) && ((MFP_ISRB&PriorityMaskHigh)==0) )
  {
    /* Is masked? */
    if (MaskRegister&Bit)
    {
      MakeSR();
      /* CPU allows interrupt of an MFP level? */
      if (6 > FIND_IPL)
      {
        *pPendingReg &= ~Bit;           /* Clear pending bit */
        MFP_UpdateFlags();

        /* Are we in 'auto' interrupt or 'manual'? */
        if (MFP_VR&0x08)                /* Software End-of-Interrupt (SEI) */
          *pInServiceReg |= Bit;        /* Set interrupt in service register */
        else
          *pInServiceReg &= ~Bit;       /* Clear interrupt in service register */

        /* Call interrupt, adds in base (default 0x100) */
        MFP_Exception(nMfpException);
        return(TRUE);
      }
    }
  }

  return(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Check 'pending' registers to see if any MFP interrupts need servicing.
  Request interrupt if necessary.
*/
void MFP_CheckPendingInterrupts(void)
{
  if ((MFP_IPRA & 0xb5) == 0 && (MFP_IPRB & 0xf0) == 0)
  { 
    /* Should never get here, but if do just clear flag (see 'MFP_UpdateFlags') */
    unset_special(SPCFLAG_MFP);
    return;
  }

  if (MFP_IPRA & MFP_TIMER_GPIP7_BIT)   /* Check MFP GPIP7 interrupt (bit 7) */
    MFP_InterruptRequest(MFP_EXCEPT_GPIP7, MFP_TIMER_GPIP7_BIT, &MFP_IPRA, MFP_IMRA, 0x80, 0x00, &MFP_ISRA);

  if (MFP_IPRA & MFP_TIMER_A_BIT)       /* Check Timer A (bit 5) */
    MFP_InterruptRequest(MFP_EXCEPT_TIMERA, MFP_TIMER_A_BIT, &MFP_IPRA, MFP_IMRA, 0xe0, 0x00, &MFP_ISRA);

  if (MFP_IPRA & MFP_RCVBUFFULL_BIT)    /* Check Receive buffer full (bit 4) */
    MFP_InterruptRequest(MFP_EXCEPT_RECBUFFULL, MFP_RCVBUFFULL_BIT, &MFP_IPRA, MFP_IMRA, 0xf0, 0x00, &MFP_ISRA);

  if (MFP_IPRA & MFP_TRNBUFEMPTY_BIT)   /* Check transmit buffer empty (bit 2) */
    MFP_InterruptRequest(MFP_EXCEPT_TRANSBUFFEMPTY, MFP_TRNBUFEMPTY_BIT, &MFP_IPRA, MFP_IMRA, 0xfb, 0x00, &MFP_ISRA);

  if (MFP_IPRA & MFP_TIMER_B_BIT)       /* Check Timer B (bit 0) */
    MFP_InterruptRequest(MFP_EXCEPT_TIMERB, MFP_TIMER_B_BIT, &MFP_IPRA, MFP_IMRA, 0xff, 0x00, &MFP_ISRA);

  if (MFP_IPRB & MFP_FDCHDC_BIT)        /* Check FDC (bit 7) */
    MFP_InterruptRequest(MFP_EXCEPT_GPIP5, MFP_FDCHDC_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0x80, &MFP_ISRB);

  if (MFP_IPRB & MFP_ACIA_BIT)          /* Check ACIA (Keyboard or MIDI) (bit 6) */
    MFP_InterruptRequest(MFP_EXCEPT_ACIA, MFP_ACIA_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0xc0, &MFP_ISRB);

  if (MFP_IPRB & MFP_TIMER_C_BIT)       /* Check Timer C (bit 5) */
    MFP_InterruptRequest(MFP_EXCEPT_TIMERC, MFP_TIMER_C_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0xe0, &MFP_ISRB);

  if (MFP_IPRB & MFP_TIMER_D_BIT)       /* Check Timer D (bit 4) */
    MFP_InterruptRequest(MFP_EXCEPT_TIMERD, MFP_TIMER_D_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0xf0, &MFP_ISRB);
}


/*-----------------------------------------------------------------------*/
/*
  This is called whenever the MFP_IPRA or MFP_IPRB registers are modified.
  We set the special flag SPCFLAG_MFP accordingly (to say if an MFP interrupt
  is to be checked) so we only have one compare during the decode
  instruction loop.
*/
void MFP_UpdateFlags(void)
{
  if( MFP_IPRA|MFP_IPRB )
  {
    set_special(SPCFLAG_MFP);
  }
  else
  {
    unset_special(SPCFLAG_MFP);
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
static int MFP_StartTimer_AB(unsigned char TimerControl, unsigned int TimerData, int Handler, BOOL bFirstTimer)
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
static int MFP_StartTimer_CD(unsigned char TimerControl, unsigned int TimerData, int Handler, BOOL bFirstTimer)
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
static unsigned char MFP_ReadTimer_AB(unsigned char TimerControl, unsigned char MainCounter, int TimerCycles, int Handler)
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
static unsigned char MFP_ReadTimerCD(unsigned char TimerControl,unsigned char TimerData,  unsigned char MainCounter, int TimerCycles, int Handler)
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



/*-----------------------------------------------------------------------*/
/*
  Handle read from GPIP pins register (0xfffa01).

  - Bit 0 is the BUSY signal of the printer port, it is SET if no printer
    is connected or on BUSY. Therefor we should assume it to be 0 in Hatari
    when a printer is emulated.
  - Bit 7 is monochrome monitor detection signal. On STE it is also XORed with
    the DMA sound play bit.
*/
void MFP_GPIP_ReadByte(void)
{
	if (!bUseHighRes)
		MFP_GPIP |= 0x80;       /* Color monitor -> set top bit */
	else
		MFP_GPIP &= ~0x80;
	if (nDmaSoundControl & DMASNDCTRL_PLAY)
		MFP_GPIP ^= 0x80;       /* Top bit is XORed with DMA sound control play bit */

	if (ConfigureParams.Printer.bEnablePrinting)
	{
		/* Signal that printer is not busy */
		MFP_GPIP &= ~1;
	}
	else
	{
		MFP_GPIP |= 1;

		/* Printer BUSY bit is also used by parallel port joystick adapters as fire button */
		if (ConfigureParams.Joysticks.Joy[JOYID_PARPORT1].nJoystickMode != JOYSTICK_DISABLED)
		{
			/* Fire pressed? */
			if (Joy_GetStickData(JOYID_PARPORT1) & 0x80)
				MFP_GPIP &= ~1;
		}
	}

	IoMem[0xfffa01] = MFP_GPIP;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from active edge register (0xfffa03).
*/
void MFP_ActiveEdge_ReadByte(void)
{
	IoMem[0xfffa03] = MFP_AER;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from data direction register (0xfffa05).
*/
void MFP_DataDirection_ReadByte(void)
{
	IoMem[0xfffa05] = MFP_DDR;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from interupt enable register A (0xfffa07).
*/
void MFP_EnableA_ReadByte(void)
{
	IoMem[0xfffa07] = MFP_IERA;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from interupt enable register B (0xfffa09).
*/
void MFP_EnableB_ReadByte(void)
{
	IoMem[0xfffa09] = MFP_IERB;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from interupt pending register A (0xfffa0b).
*/
void MFP_PendingA_ReadByte(void)
{
	IoMem[0xfffa0b] = MFP_IPRA;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from interupt pending register A (0xfffa0d).
*/
void MFP_PendingB_ReadByte(void)
{
	IoMem[0xfffa0d] = MFP_IPRB;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from interupt in service register A (0xfffa0f).
*/
void MFP_InServiceA_ReadByte(void)
{
	IoMem[0xfffa0f] = MFP_ISRA;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from interupt in service register B (0xfffa11).
*/
void MFP_InServiceB_ReadByte(void)
{
	IoMem[0xfffa11] = MFP_ISRB;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from interupt mask register A (0xfffa13).
*/
void MFP_MaskA_ReadByte(void)
{
	IoMem[0xfffa13] = MFP_IMRA;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from interupt mask register B (0xfffa15).
*/
void MFP_MaskB_ReadByte(void)
{
	IoMem[0xfffa15] = MFP_IMRB;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from MFP vector register (0xfffa17).
*/
void MFP_VectorReg_ReadByte(void)
{
	IoMem[0xfffa17] = MFP_VR;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from timer A control register (0xfffa19).
*/
void MFP_TimerACtrl_ReadByte(void)
{
	IoMem[0xfffa19] = MFP_TACR;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from timer B control register (0xfffa1b).
*/
void MFP_TimerBCtrl_ReadByte(void)
{
	IoMem[0xfffa1b] = MFP_TBCR;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from timer C/D control register (0xfffa1d).
*/
void MFP_TimerCDCtrl_ReadByte(void)
{
	IoMem[0xfffa1d] = MFP_TCDCR;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from timer A data register (0xfffa1f).
*/
void MFP_TimerAData_ReadByte(void)
{
	if (MFP_TACR != 8)          /* Is event count? Need to re-calculate counter */
		MFP_ReadTimerA();       /* Stores result in 'MFP_TA_MAINCOUNTER' */

	IoMem[0xfffa1f] = MFP_TA_MAINCOUNTER;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from timer B data register (0xfffa21).
*/
void MFP_TimerBData_ReadByte(void)
{
	if (MFP_TBCR != 8)          /* Is event count? Need to re-calculate counter */
		MFP_ReadTimerB();       /* Stores result in 'MFP_TB_MAINCOUNTER' */

	IoMem[0xfffa21] = MFP_TB_MAINCOUNTER;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from timer C data register (0xfffa23).
*/
void MFP_TimerCData_ReadByte(void)
{
	MFP_ReadTimerC();        /* Stores result in 'MFP_TC_MAINCOUNTER' */

	IoMem[0xfffa23] = MFP_TC_MAINCOUNTER;
}

/*-----------------------------------------------------------------------*/
/*
  Handle read from timer D data register (0xfffa25).
*/
void MFP_TimerDData_ReadByte(void)
{
	Uint32 pc = m68k_getpc();

	if (ConfigureParams.System.bPatchTimerD && pc >= TosAddress && pc <= TosAddress + TosSize)
	{
		/* Trick the tos to believe it was changed: */
		IoMem[0xfffa25] = nTimerDFakeValue;
	}
	else
	{
		MFP_ReadTimerD();        /* Stores result in 'MFP_TD_MAINCOUNTER' */
		IoMem[0xfffa25] = MFP_TD_MAINCOUNTER;
	}
}


/*-----------------------------------------------------------------------*/
/*
  Handle write to GPIP register (0xfffa01).
*/
void MFP_GPIP_WriteByte(void)
{
	/* Nothing... */
	/*fprintf(stderr, "Write to GPIP: %x\n", (int)IoMem[0xfffa01]);*/
	/*MFP_GPIP = IoMem[0xfffa01];*/   /* TODO: What are the GPIP pins good for? */
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to AER (0xfffa03).
*/
void MFP_ActiveEdge_WriteByte(void)
{
	MFP_AER = IoMem[0xfffa03];
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to data direction register (0xfffa05).
*/
void MFP_DataDirection_WriteByte(void)
{
	MFP_DDR = IoMem[0xfffa05];
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to interrupt enable register A (0xfffa07).
*/
void MFP_EnableA_WriteByte(void)
{
	MFP_IERA = IoMem[0xfffa07];
	MFP_IPRA &= MFP_IERA;
	MFP_UpdateFlags();
	/* We may have enabled Timer A or B, check */
	MFP_StartTimerA();
	MFP_StartTimerB();
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to interrupt enable register B (0xfffa09).
*/
void MFP_EnableB_WriteByte(void)
{
	MFP_IERB = IoMem[0xfffa09];
	MFP_IPRB &= MFP_IERB;
	MFP_UpdateFlags();
	/* We may have enabled Timer C or D, check */
	MFP_StartTimerC();
	MFP_StartTimerD();
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to interrupt pending register A (0xfffa0b).
*/
void MFP_PendingA_WriteByte(void)
{
	MFP_IPRA &= IoMem[0xfffa0b];        /* Cannot set pending bits - only clear via software */
	MFP_UpdateFlags();                  /* Check if any interrupts pending */
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to interrupt pending register B (0xfffa0d).
*/
void MFP_PendingB_WriteByte(void)
{
	MFP_IPRB &= IoMem[0xfffa0d];
	MFP_UpdateFlags();                  /* Check if any interrupts pending */
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to interrupt in service register A (0xfffa0f).
*/
void MFP_InServiceA_WriteByte(void)
{
	MFP_ISRA &= IoMem[0xfffa0f];        /* Cannot set in-service bits - only clear via software */
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to interrupt in service register B (0xfffa11).
*/
void MFP_InServiceB_WriteByte(void)
{
	MFP_ISRB &= IoMem[0xfffa11];        /* Cannot set in-service bits - only clear via software */
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to interrupt mask register A (0xfffa13).
*/
void MFP_MaskA_WriteByte(void)
{
	MFP_IMRA = IoMem[0xfffa13];
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to interrupt mask register B (0xfffa15).
*/
void MFP_MaskB_WriteByte(void)
{
	MFP_IMRB = IoMem[0xfffa15];
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to MFP vector register (0xfffa17).
*/
void MFP_VectorReg_WriteByte(void)
{
	Uint8 old_vr;
	old_vr = MFP_VR;                    /* Copy for checking if set mode */
	MFP_VR = IoMem[0xfffa17];
	if ((MFP_VR^old_vr) & 0x08)         /* Test change in end-of-interrupt mode */
	{
		if (MFP_VR & 0x08)              /* Mode did change but was it to automatic mode? (ie bit is a zero) */
		{                               /* We are now in automatic mode, so clear all in-service bits! */
			MFP_ISRA = 0;
			MFP_ISRB = 0;
		}
	}
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to timer A control register (0xfffa19).
*/
void MFP_TimerACtrl_WriteByte(void)
{
	Uint8 old_tacr;
	old_tacr = MFP_TACR;                /* Remember old control state */
	MFP_TACR = IoMem[0xfffa19] & 0x0f;  /* Mask, Fish (auto160) writes into top nibble! */
	if ((MFP_TACR^old_tacr) & 0x0f)     /* Check if Timer A control changed */
		MFP_StartTimerA();              /* Reset timers if need to */
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to timer B control register (0xfffa1b).
*/
void MFP_TimerBCtrl_WriteByte(void)
{
	Uint8 old_tbcr;
	old_tbcr = MFP_TBCR;                /* Remember old control state */
	MFP_TBCR = IoMem[0xfffa1b] & 0x0f;  /* Mask, Fish (auto160) writes into top nibble! */
	if ((MFP_TBCR^old_tbcr) & 0x0f)     /* Check if Timer B control changed */
		MFP_StartTimerB();              /* Reset timers if need to */
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to timer C/D control register (0xfffa1d).
*/
void MFP_TimerCDCtrl_WriteByte(void)
{
	Uint8 old_tcdcr;

	old_tcdcr = MFP_TCDCR;              /* Remember old control state */
	MFP_TCDCR = IoMem[0xfffa1d];        /* Store new one */

	if ((MFP_TCDCR^old_tcdcr) & 0x70)   /* Check if Timer C control changed */
		MFP_StartTimerC();              /* Reset timers if need to */

	if ((MFP_TCDCR^old_tcdcr) & 0x07)   /* Check if Timer D control changed */
	{
		Uint32 pc = m68k_getpc();

		/* Need to change baud rate of RS232 emulation? */
		if (ConfigureParams.RS232.bEnableRS232)
		{
			RS232_SetBaudRateFromTimerD();
		}

		if (ConfigureParams.System.bPatchTimerD && !bAppliedTimerDPatch
		    && pc >= TosAddress && pc <= TosAddress + TosSize)
		{
			/* Slow down Timer-D if set from TOS for the first time to gain more
			 * desktop performance.
			 * Obviously, we need to emulate all timers correctly but TOS sets up
			 * Timer-D at a very high rate (every couple of instructions). The
			 * interrupt isn't enabled but the emulator still needs to process the
			 * interrupt table and this HALVES our frame rate!!!
			 * Some games actually reference this timer but don't set it up
			 * (eg Paradroid, Speedball I) so we simply intercept the Timer-D setup
			 * code in TOS and fix the numbers with more 'laid-back' values.
			 * This still keeps 100% compatibility */
			MFP_TCDCR = IoMem[0xfffa1d] = (IoMem[0xfffa1d] & 0xf0) | 7;
			bAppliedTimerDPatch = TRUE;
		}
		MFP_StartTimerD();              /* Reset timers if need to */
	}
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to timer A data register (0xfffa1f).
*/
void MFP_TimerAData_WriteByte(void)
{
	MFP_TADR = IoMem[0xfffa1f];         /* Store into data register */
	if (MFP_TACR == 0)                  /* Now check if timer is running - if so do not set */
	{
		MFP_TA_MAINCOUNTER = MFP_TADR;  /* Timer is off, store to main counter */
		MFP_StartTimerA();              /* Add our interrupt */
	}
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to timer B data register (0xfffa21).
*/
void MFP_TimerBData_WriteByte(void)
{
	MFP_TBDR = IoMem[0xfffa21];         /* Store into data register */
	if (MFP_TBCR == 0)                  /* Now check if timer is running - if so do not set */
	{
		MFP_TB_MAINCOUNTER = MFP_TBDR;  /* Timer is off, store to main counter */
		MFP_StartTimerB();              /* Add our interrupt */
	}
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to timer C data register (0xfffa23).
*/
void MFP_TimerCData_WriteByte(void)
{
	MFP_TCDR = IoMem[0xfffa23];         /* Store into data register */
	if ((MFP_TCDCR&0x70) == 0)          /* Now check if timer is running - if so do not set */
	{
		MFP_StartTimerC();              /* Add our interrupt */
	}
}

/*-----------------------------------------------------------------------*/
/*
  Handle write to timer D data register (0xfffa25).
*/
void MFP_TimerDData_WriteByte(void)
{
	Uint32 pc = m68k_getpc();

	/* Need to change baud rate of RS232 emulation? */
	if (ConfigureParams.RS232.bEnableRS232 && (IoMem[0xfffa1d] & 0x07))
	{
		RS232_SetBaudRateFromTimerD();
	}

	/* Patch Timer-D for better performance? */
	if (ConfigureParams.System.bPatchTimerD && pc >= TosAddress && pc <= TosAddress + TosSize)
	{
		nTimerDFakeValue = IoMem[0xfffa25];
		IoMem[0xfffa25] = 0x64;         /* Slow down the useless Timer-D setup from the bios */
	}

	MFP_TDDR = IoMem[0xfffa25];         /* Store into data register */
	if ((MFP_TCDCR&0x07) == 0)          /* Now check if timer is running - if so do not set */
	{
		MFP_StartTimerD();              /* Add our interrupt */
	}
}
