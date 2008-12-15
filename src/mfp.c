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

/* 2007/04/18	[NP]	- Better values for MFPTimerToCPUCycleTable.			*/
/*			- Don't restart the timers in MFP_EnableA_WriteByte and		*/
/*			MFP_EnableB_WriteByte, this gives wrong results.		*/
/* 2007/05/05	[NP]	- When a timer is looping (counter reaches 0), we must use	*/
/*			PendingCyclesOver to restart it with Int_AddRelativeInterrupt.	*/
/*			PendingCyclesOver is the value of  PendingInterruptCount when	*/
/*			the timer expired.						*/
/*			- MFP_ReadTimer_AB/CD was wrong (returned the elapsed counter	*/
/*			changes since start, instead of the remaining counter value).	*/
/*			(ULM DSOTS Demos and Overscan Demos).				*/
/* 2007/09/25	[NP]	Replace printf by calls to HATARI_TRACE.			*/
/* 2007/10/21	[NP]	Use 'Int_AddRelativeInterruptWithOffset' when an MFP timer is	*/
/*			looping. Gives better accuracy when using '4' as a divisor.	*/
/*			(fix ULM DSOTS Demos and Overscan Demos).			*/
/* 2007/10/24	[NP]	Handle the possibility to resume a timer after stopping it.	*/
/*			After writing 0 to ctrl, writing a >0 in ctrl should continue	*/
/*			the timer with the value that was stored in data reg when timer	*/
/*			was stopped. The value is saved in MFP_Tx_MAINCOUNTER whenever	*/
/*			0 is written in ctrl reg (Froggies Over The Fence by STCNX).	*/
/* 2007/10/28	[NP]	Function 'Int_ResumeStoppedInterrupt' to better handle the	*/
/*			possibility to resume a timer that was stopped with ctrl=0	*/
/*			(ST CNX screen in Punish Your Machine).				*/
/* 2007/12/27	[NP]	When adding a new MFP interrupt (ctrl != 0 ), we must take	*/
/*			into account the number of cycles of the current instruction, as*/
/*			well as the accumulated wait state cycles, else the int counter	*/
/*			will be started between 8 - 20 cycles earlier, which can break	*/
/*			some too strict code : the int counter must start after the	*/
/*			current instruction is processed, not before. The write is	*/
/*			considered effective 4 cycles before the end of the current	*/
/*			instruction.							*/
/*			(fix ULM Dark Side Of The Spoon and Decade Demo's Wow Scroll 2).*/
/* 2008/02/06	[NP]	Handle "fast" timers as those started by the TOS for the RS232	*/
/*			baud rate generator. In that case, the timers could be too fast	*/
/*			to be handled by the CPU, which means PendingCyclesOver can be	*/
/*			>= INT_CONVERT_TO_INTERNAL ( TimerClockCycles , INT_MFP_CYCLE )	*/
/*			and this will give wrong results when the timer restarts if	*/
/*			we call Int_AddRelativeInterruptWithOffset. We use a modulo to	*/
/*			limit PendingCyclesOver to not more than the number of cycles	*/
/*			of one int (which means we "skip" the ints that	could not be	*/
/*			processed).							*/
/* 2008/03/08	[NP]	Add traces when writing to vector register fffa17.		*/
/*			Use M68000_INT_MFP when calling M68000_Exception().		*/
/* 2008/04/17	[NP]	Handle the case where Timer B is in event count mode and the	*/
/*			content of $fffa21 is updated by the end of line signal while a	*/
/*			read instruction at addr $fffa21 occurs at the same time (before*/
/*			calling MFP_TimerB_EventCount_Interrupt).			*/
/*			In that case, we need to return MFP_TB_MAINCOUNTER - 1.		*/
/*			(fix B.I.G. Demo Screen 1).					*/
/*			FIXME : this should be handled by Cycles_GetCounterOnReadAccess	*/
/*			but it's not correctly implemented at the moment.		*/
/* 2008/04/20	[NP]	In the TRACE call in 'MFP_Exception', replace 'get_long' by	*/
/*			'STMemory_ReadLong' because 'get_long' produced a bus error	*/
/*			if we were not already in supervisor mode when the mfp exception*/
/*			occured. This could cause bus error when restoring snapshot	*/
/*			of a gemdos program for example if trace mode was activated.	*/
/* 2008/07/12	[NP]	When stopping an active timer just when the internal data	*/
/*			counter is going from 1 to 0, the internal data counter will be	*/
/*			set to 0 (=256) instead of being reloaded with the original	*/
/*			data value. In case no new value is written to the data reg,	*/
/*			this means a write > 0 to the control reg will restart the timer*/
/*			with a counter of 256 ! (fix timer saving routine used by	*/
/*			ST Cnx in the Punish Your Machine and the Froggies Over The	*/
/*			Fence (although this routine is in fact buggy)).		*/
/* 2008/09/13	[NP]	Add some traces when stopping a timer and changing data reg.	*/
/*			Don't apply timer D patch if timer D ctrl reg is 0.		*/
/* 2008/10/04	[NP]	In MFP_TimerBData_ReadByte, test for overlap only when nHBL	*/
/*			is between nStartHBL and nEndHBL (fix Wolfenstein 3D intro).	*/
/*			In event count mode for timer A and B, set data reg to 255 when	*/
/*			data reg was 0 (which in fact means 256).			*/
/* 2008/10/16	[NP]	No need to set data reg to 255 when decrementing a data reg that*/
/*			was 0, this is already what is implicitly done, because data	*/
/*			reg for timer A/B is Uint8 (revert 2008/10/04 changes).		*/
/* 2008/12/11	[NP]	In MFP_CheckPendingInterrupts(), returns TRUE or FALSE instead	*/
/*			of void, depending on whether at least one MFP interrupt was	*/
/*			allowed or not.							*/




const char MFP_rcsid[] = "Hatari $Id: mfp.c,v 1.56 2008-12-15 18:55:11 npomarede Exp $";

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
#include "stMemory.h"
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

/* MFP Registers */
Uint8 MFP_GPIP;                     /* General Purpose Pins */
Uint8 MFP_VR;                       /* Vector Register  0xfffa17 */
Uint8 MFP_IERA,MFP_IERB;            /* Interrupt Enable Registers A,B  0xfffa07,0xfffa09 */
Uint8 MFP_IPRA,MFP_IPRB;            /* Interrupt Pending Registers A,B  0xfffa0b,0xfffa0d */
Uint8 MFP_TACR,MFP_TBCR;            /* Timer A,B Control Registers */

static Uint8 MFP_TCDCR;             /* C+D Control Registers */
static Uint8 MFP_AER,MFP_DDR;       /* Active Edge Register, Data Direction Register */
static Uint8 MFP_ISRA,MFP_ISRB;     /* Interrupt In-Service Registers A,B  0xfffa0f,0xfffa11 */
static Uint8 MFP_IMRA,MFP_IMRB;     /* Interrupt Mask Registers A,B  0xfffa13,0xfffa15 */
static Uint8 MFP_TADR,MFP_TBDR;     /* Timer A,B Data Registers */
static Uint8 MFP_TCDR,MFP_TDDR;     /* Timer C,D Data Registers */
static Uint8 MFP_TA_MAINCOUNTER;    /* Timer A Main Counter (internal to MFP) */
static Uint8 MFP_TB_MAINCOUNTER;    /* Timer B Main Counter */
static Uint8 MFP_TC_MAINCOUNTER;    /* Timer C Main Counter (these are temp's, set when read as) */
static Uint8 MFP_TD_MAINCOUNTER;    /* Timer D Main Counter (as done via interrupts) */

/* CPU clock cycle counts for each timer */
static int TimerAClockCycles=0;
static int TimerBClockCycles=0;
static int TimerCClockCycles=0;
static int TimerDClockCycles=0;

/* If a timer is stopped then restarted later without writing to the data register, */
/* we must resume the timer from where we left in the interrupts table, instead of */
/* computing a new number of clock cycles to restart the interrupt. */
static bool TimerACanResume = FALSE;
static bool TimerBCanResume = FALSE;
static bool TimerCCanResume = FALSE;
static bool TimerDCanResume = FALSE;

bool bAppliedTimerDPatch;           /* TRUE if the Timer-D patch has been applied */
static int nTimerDFakeValue;        /* Faked Timer-D data register for the Timer-D patch */

static int PendingCyclesOver = 0;   /* >= 0 value, used to "loop" a timer when data counter reaches 0 */

static const Uint16 MFPDiv[] =
{
	0,
	4,
	10,
	16,
	50,
	64,
	100,
	200
};

/* Convert data/ctrl register to a number of mfp cycles */
#define MFP_REG_TO_CYCLES(data,ctrl)	( data * MFPDiv[ ctrl&0x7 ] )
/* Determine the data register corresponding to a number of mfp cycles/ctrl register */
/* (we round to the closest higher integer) */
#define MFP_CYCLE_TO_REG(cyc,ctrl)	( ( cyc + MFPDiv[ ctrl&0x7 ] - 1 ) / MFPDiv[ ctrl&0x7 ] )
//#define MFP_CYCLE_TO_REG(cyc,ctrl)	( cyc / MFPDiv[ ctrl&0x7 ] )


/*-----------------------------------------------------------------------*/
/**
 * Reset all MFP variables and start interrupts on their way!
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
	MFP_TA_MAINCOUNTER = MFP_TB_MAINCOUNTER = 0;
	MFP_TC_MAINCOUNTER = MFP_TD_MAINCOUNTER = 0;

	/* Clear counters */
	TimerAClockCycles = TimerBClockCycles = 0;
	TimerCClockCycles = TimerDClockCycles = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void MFP_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&MFP_GPIP, sizeof(MFP_GPIP));
	MemorySnapShot_Store(&MFP_AER, sizeof(MFP_AER));
	MemorySnapShot_Store(&MFP_DDR, sizeof(MFP_DDR));
	MemorySnapShot_Store(&MFP_IERA, sizeof(MFP_IERA));
	MemorySnapShot_Store(&MFP_IERB, sizeof(MFP_IERB));
	MemorySnapShot_Store(&MFP_IPRA, sizeof(MFP_IPRA));
	MemorySnapShot_Store(&MFP_IPRB, sizeof(MFP_IPRB));
	MemorySnapShot_Store(&MFP_ISRA, sizeof(MFP_ISRA));
	MemorySnapShot_Store(&MFP_ISRB, sizeof(MFP_ISRB));
	MemorySnapShot_Store(&MFP_IMRA, sizeof(MFP_IMRA));
	MemorySnapShot_Store(&MFP_IMRB, sizeof(MFP_IMRB));
	MemorySnapShot_Store(&MFP_VR, sizeof(MFP_VR));
	MemorySnapShot_Store(&MFP_TACR, sizeof(MFP_TACR));
	MemorySnapShot_Store(&MFP_TBCR, sizeof(MFP_TBCR));
	MemorySnapShot_Store(&MFP_TCDCR, sizeof(MFP_TCDCR));
	MemorySnapShot_Store(&MFP_TADR, sizeof(MFP_TADR));
	MemorySnapShot_Store(&MFP_TBDR, sizeof(MFP_TBDR));
	MemorySnapShot_Store(&MFP_TCDR, sizeof(MFP_TCDR));
	MemorySnapShot_Store(&MFP_TDDR, sizeof(MFP_TDDR));
	MemorySnapShot_Store(&MFP_TA_MAINCOUNTER, sizeof(MFP_TA_MAINCOUNTER));
	MemorySnapShot_Store(&MFP_TB_MAINCOUNTER, sizeof(MFP_TB_MAINCOUNTER));
	MemorySnapShot_Store(&MFP_TC_MAINCOUNTER, sizeof(MFP_TC_MAINCOUNTER));
	MemorySnapShot_Store(&MFP_TD_MAINCOUNTER, sizeof(MFP_TD_MAINCOUNTER));
	MemorySnapShot_Store(&TimerAClockCycles, sizeof(TimerAClockCycles));
	MemorySnapShot_Store(&TimerBClockCycles, sizeof(TimerBClockCycles));
	MemorySnapShot_Store(&TimerCClockCycles, sizeof(TimerCClockCycles));
	MemorySnapShot_Store(&TimerDClockCycles, sizeof(TimerDClockCycles));
	MemorySnapShot_Store(&TimerACanResume, sizeof(TimerACanResume));
	MemorySnapShot_Store(&TimerBCanResume, sizeof(TimerBCanResume));
	MemorySnapShot_Store(&TimerCCanResume, sizeof(TimerCCanResume));
	MemorySnapShot_Store(&TimerDCanResume, sizeof(TimerDCanResume));
}


/*-----------------------------------------------------------------------*/
/**
 * Call MFP interrupt - NOTE when the MFP is in Auto interrupt (AEI), the MFP
 * puts the interrupt number on the data bus and then the 68000 reads it, multiplies
 * it by 4 and adds in a base(usually 0x100) to give the vector. Some programs
 * change this offset, eg RoboCod. This offset is stored in the top 4 bits of register
 * 0xfffa17(0x40 is the default=0x100)
 * Many thanks to Steve Bak for that one!
 */
static void MFP_Exception(int Interrupt)
{
	unsigned int Vec;

	Vec = (unsigned int)(MFP_VR&0xf0)<<2;
	Vec += Interrupt<<2;

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_EXCEPTION ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "mfp excep int=%d vec=0x%x new_pc=0x%x video_cyc=%d %d@%d\n" ,
			Interrupt, Vec, STMemory_ReadLong ( Vec ), nFrameCycles, nLineCycles, nHBL );
	}

	M68000_Exception ( Vec , M68000_EXCEPTION_SRC_INT_MFP );
}


/*-----------------------------------------------------------------------*/
/**
 * This is called whenever the MFP_IPRA or MFP_IPRB registers are modified.
 * We set the special flag SPCFLAG_MFP accordingly (to say if an MFP interrupt
 * is to be checked) so we only have one compare during the decode
 * instruction loop.
 */
static void MFP_UpdateFlags(void)
{
	if (MFP_IPRA|MFP_IPRB)
	{
		M68000_SetSpecial(SPCFLAG_MFP);
	}
	else
	{
		M68000_UnsetSpecial(SPCFLAG_MFP);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Test interrupt request to see if can cause exception,return TRUE if pass vector
 */
static bool MFP_InterruptRequest(int nMfpException, Uint8 Bit, Uint8 *pPendingReg, Uint8 MaskRegister,
                                 Uint8 PriorityMaskLow, Uint8 PriorityMaskHigh, Uint8 *pInServiceReg)
{
	/* Are any higher priority interupts in service? */
	if (((MFP_ISRA&PriorityMaskLow) == 0) && ((MFP_ISRB&PriorityMaskHigh) == 0))
	{
		/* Is masked? */
		if (MaskRegister&Bit)
		{
			/* CPU allows interrupt of an MFP level? */
			if (6 > FIND_IPL)
			{
				*pPendingReg &= ~Bit;           /* Clear pending bit */
				MFP_UpdateFlags();

				/* Are we in 'auto' interrupt or 'manual'? */
				if (MFP_VR&0x08)                /* Software End-of-Interrupt (SEI) */
					*pInServiceReg |= Bit;      /* Set interrupt in service register */
				else
					*pInServiceReg &= ~Bit;     /* Clear interrupt in service register */

				/* Call interrupt, adds in base (default 0x100) */
				MFP_Exception(nMfpException);
				return TRUE;
			}
		}
	}

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Check 'pending' registers to see if any MFP interrupts need servicing.
 * Request interrupt if necessary.
 * Return TRUE if at least one MFP interrupt was allowed, else return FALSE.
 */
bool MFP_CheckPendingInterrupts(void)
{
	int	InterruptPossible;


	if ((MFP_IPRA & 0xb5) == 0 && (MFP_IPRB & 0xfb) == 0)
	{
		/* Should never get here, but if do just clear flag (see 'MFP_UpdateFlags') */
		M68000_UnsetSpecial(SPCFLAG_MFP);
		return FALSE;
	}


	InterruptPossible = FALSE;

	if (MFP_IPRA & MFP_TIMER_GPIP7_BIT)   /* Check MFP GPIP7 interrupt (bit 7) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_GPIP7, MFP_TIMER_GPIP7_BIT, &MFP_IPRA, MFP_IMRA, 0x80, 0x00, &MFP_ISRA);

	if (MFP_IPRA & MFP_TIMER_A_BIT)       /* Check Timer A (bit 5) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_TIMERA, MFP_TIMER_A_BIT, &MFP_IPRA, MFP_IMRA, 0xe0, 0x00, &MFP_ISRA);

	if (MFP_IPRA & MFP_RCVBUFFULL_BIT)    /* Check Receive buffer full (bit 4) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_RECBUFFULL, MFP_RCVBUFFULL_BIT, &MFP_IPRA, MFP_IMRA, 0xf0, 0x00, &MFP_ISRA);

	if (MFP_IPRA & MFP_TRNBUFEMPTY_BIT)   /* Check transmit buffer empty (bit 2) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_TRANSBUFFEMPTY, MFP_TRNBUFEMPTY_BIT, &MFP_IPRA, MFP_IMRA, 0xfc, 0x00, &MFP_ISRA);

	if (MFP_IPRA & MFP_TIMER_B_BIT)       /* Check Timer B (bit 0) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_TIMERB, MFP_TIMER_B_BIT, &MFP_IPRA, MFP_IMRA, 0xff, 0x00, &MFP_ISRA);


	if (MFP_IPRB & MFP_FDCHDC_BIT)        /* Check FDC (bit 7) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_GPIP5, MFP_FDCHDC_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0x80, &MFP_ISRB);

	if (MFP_IPRB & MFP_ACIA_BIT)          /* Check ACIA (Keyboard or MIDI) (bit 6) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_ACIA, MFP_ACIA_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0xc0, &MFP_ISRB);

	if (MFP_IPRB & MFP_TIMER_C_BIT)       /* Check Timer C (bit 5) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_TIMERC, MFP_TIMER_C_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0xe0, &MFP_ISRB);

	if (MFP_IPRB & MFP_TIMER_D_BIT)       /* Check Timer D (bit 4) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_TIMERD, MFP_TIMER_D_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0xf0, &MFP_ISRB);

	if (MFP_IPRB & MFP_GPU_DONE_BIT)      /* Check GPU done (bit 3) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_GPIP3, MFP_GPU_DONE_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0xf8, &MFP_ISRB);

	if (MFP_IPRB & MFP_GPIP_1_BIT)        /* Check (Falcon) Centronics ACK / (ST) RS232 DCD (bit 1) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_GPIP1, MFP_GPIP_1_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0xfe, &MFP_ISRB);

	if (MFP_IPRB & MFP_GPIP_0_BIT)        /* Check Centronics BUSY (bit 0) */
		InterruptPossible |= MFP_InterruptRequest(MFP_EXCEPT_GPIP0, MFP_GPIP_0_BIT, &MFP_IPRB, MFP_IMRB, 0xff, 0xff, &MFP_ISRB);

	return InterruptPossible;
}


/*-----------------------------------------------------------------------*/
/**
 * Interrupt Channel is active, set pending bit so can be serviced
 */
void MFP_InputOnChannel(Uint8 Bit, Uint8 EnableBit, Uint8 *pPendingReg)
{
	/* Input has occurred on MFP channel, set interrupt pending to request interrupt when able */
	if (EnableBit&Bit)
		*pPendingReg |= Bit;           /* Set bit */
	else
		*pPendingReg &= ~Bit;          /* Clear bit */
	MFP_UpdateFlags();
}


/*-----------------------------------------------------------------------*/
/**
 * Generate Timer A Interrupt when in Event Count mode
 */
void MFP_TimerA_EventCount_Interrupt(void)
{
	if (MFP_TA_MAINCOUNTER == 1)			/* Timer expired? If so, generate interrupt */
	{
		MFP_TA_MAINCOUNTER = MFP_TADR;		/* Reload timer from data register */

		/* Acknowledge in MFP circuit, pass bit,enable,pending */
		MFP_InputOnChannel(MFP_TIMER_A_BIT,MFP_IERA,&MFP_IPRA);
	}
	else
	{
		MFP_TA_MAINCOUNTER--;			/* Decrement timer main counter */
		/* As MFP_TA_MAINCOUNTER is Uint8, when we decrement MFP_TA_MAINCOUNTER=0 */
		/* we go to MFP_TA_MAINCOUNTER=255, which is the wanted behaviour because */
		/* data reg = 0 means 256 in fact. So, the next 2 lines are redundant. */
/*		if ( MFP_TA_MAINCOUNTER < 0 )
			MFP_TA_MAINCOUNTER = 255;
*/
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Generate Timer B Interrupt when in Event Count mode
 */
void MFP_TimerB_EventCount_Interrupt(void)
{
	if (MFP_TB_MAINCOUNTER == 1)			/* Timer expired? If so, generate interrupt */
	{
		MFP_TB_MAINCOUNTER = MFP_TBDR;		/* Reload timer from data register */

		/* Acknowledge in MFP circuit, pass bit,enable,pending */
		MFP_InputOnChannel(MFP_TIMER_B_BIT,MFP_IERA,&MFP_IPRA);
	}
	else
	{
		MFP_TB_MAINCOUNTER--;			/* Decrement timer main counter */
		/* As MFP_TB_MAINCOUNTER is Uint8, when we decrement MFP_TB_MAINCOUNTER=0 */
		/* we go to MFP_TB_MAINCOUNTER=255, which is the wanted behaviour because */
		/* data reg = 0 means 256 in fact. So, the next 2 lines are redundant. */
/*		if ( MFP_TB_MAINCOUNTER < 0 )
			MFP_TB_MAINCOUNTER = 255;
*/
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer A or B - EventCount mode is done in HBL handler to time correctly
 */
static int MFP_StartTimer_AB(Uint8 TimerControl, Uint16 TimerData, int Handler,
                             bool bFirstTimer, bool *pTimerCanResume)
{
	int TimerClockCycles = 0;

	/* Is timer in delay mode (ctrl = 0-7) ? */
	/* If we are in event-count mode (ctrl = 8) ignore this (done on HBL) */
	if (TimerControl <= 7)
	{
		/* Find number of CPU cycles for when timer is due (include preset
		 * and counter). As timer occurs very often we multiply by counter
		 * to speed up emulator */
		if (TimerData == 0)             /* Data=0 is actually Data=256 */
			TimerData = 256;
		TimerClockCycles = MFP_REG_TO_CYCLES ( TimerData, TimerControl );

		if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_START ) )
		{
			int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
			int nLineCycles = nFrameCycles % nCyclesPerLine;
			HATARI_TRACE_PRINT ( "mfp start AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n" ,
			                     Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                     nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles,
			                     bFirstTimer?"true":"false" , *pTimerCanResume?"true":"false" );
		}

		/* And add to our internal interrupt list, if timer cycles is zero
		 * then timer is stopped */
		Int_RemovePendingInterrupt(Handler);
		if (TimerClockCycles)
		{
			if ( ( *pTimerCanResume == TRUE ) && ( bFirstTimer == TRUE ) )	/* we can't resume if the timer is auto restarting after an interrupt */
			{
				Int_ResumeStoppedInterrupt ( Handler );
			}
			else
			{
				int	AddCurCycles = INT_CONVERT_TO_INTERNAL ( CurrentInstrCycles + nWaitStateCycles - 4 , INT_CPU_CYCLE );

				/* Start timer from now? If not continue timer using PendingCycleOver */
				if (bFirstTimer)
					Int_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, AddCurCycles);
				else
				{
					int	TimerClockCyclesInternal = INT_CONVERT_TO_INTERNAL ( TimerClockCycles , INT_MFP_CYCLE );

					/* In case we miss more than one int, we must correct the delay for the next one */
					if ( PendingCyclesOver > TimerClockCyclesInternal )
						PendingCyclesOver = PendingCyclesOver % TimerClockCyclesInternal;

					Int_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, -PendingCyclesOver);
				}

				*pTimerCanResume = TRUE;		/* timer was set, resume is possible if stop/start it later */
			}
		}

		else	/* Ctrl was 0 -> timer is stopped */
		{
			/* do nothing, only print some traces */
			if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_START ) )
			{
				int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
				int nLineCycles = nFrameCycles % nCyclesPerLine;
				HATARI_TRACE_PRINT ( "mfp stop AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n" ,
			                     Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                     nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles,
			                     bFirstTimer?"true":"false" , *pTimerCanResume?"true":"false" );
			}
		}
	}

	else	/* timer control > 7 */
	{
		/* Make sure no outstanding interrupts in list if channel is disabled */
		Int_RemovePendingInterrupt(Handler);
	}

	if (TimerControl == 8 )				/* event count mode */
	{
		/* do nothing, only print some traces */

		if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_START ) )
		{
			int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
			int nLineCycles = nFrameCycles % nCyclesPerLine;
			HATARI_TRACE_PRINT ( "mfp start AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n" ,
			                     Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                     nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles,
			                     bFirstTimer?"true":"false" , *pTimerCanResume?"true":"false" );
		}


	}

	return TimerClockCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer C or D
 */
static int MFP_StartTimer_CD(Uint8 TimerControl, Uint16 TimerData, int Handler,
                             bool bFirstTimer, bool *pTimerCanResume)
{
	int TimerClockCycles = 0;

	/* Is timer in delay mode ? */
	if ((TimerControl&0x7) != 0)
	{
		/* Find number of cycles for when timer is due (include preset and
		 * counter). As timer occurs very often we multiply by counter to
		 * speed up emulator */
		if (TimerData == 0)             /* Data=0 is actually Data=256 */
			TimerData = 256;
		TimerClockCycles = MFP_REG_TO_CYCLES ( TimerData, TimerControl );

		if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_START ) )
		{
			int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
			int nLineCycles = nFrameCycles % nCyclesPerLine;
			HATARI_TRACE_PRINT ( "mfp start CD handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n" ,
			                     Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                     nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles,
			                     bFirstTimer?"true":"false" , *pTimerCanResume?"true":"false" );
		}

		/* And add to our internal interrupt list, if timer cycles is zero
		 * then timer is stopped */
		Int_RemovePendingInterrupt(Handler);
		if (TimerClockCycles)
		{
			if ( ( *pTimerCanResume == TRUE ) && ( bFirstTimer == TRUE ) )	/* we can't resume if the timer is auto restarting after an interrupt */
			{
				Int_ResumeStoppedInterrupt ( Handler );
			}
			else
			{
				int	AddCurCycles = INT_CONVERT_TO_INTERNAL ( CurrentInstrCycles + nWaitStateCycles - 4 , INT_CPU_CYCLE );

				/* Start timer from now? If not continue timer using PendingCycleOver */
				if (bFirstTimer)
					Int_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, AddCurCycles);
				else
				{
					int	TimerClockCyclesInternal = INT_CONVERT_TO_INTERNAL ( TimerClockCycles , INT_MFP_CYCLE );

					/* In case we miss more than one int, we must correct the delay for the next one */
					if ( PendingCyclesOver > TimerClockCyclesInternal )
						PendingCyclesOver = PendingCyclesOver % TimerClockCyclesInternal;

					Int_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, -PendingCyclesOver);
				}

				*pTimerCanResume = TRUE;		/* timer was set, resume is possible if stop/start it later */
			}
		}
	}

	else	/* timer control is 0 */
	{
		if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_START ) )
		{
			int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
			int nLineCycles = nFrameCycles % nCyclesPerLine;
			HATARI_TRACE_PRINT ( "mfp stop CD handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n" ,
			                     Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                     nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles,
			                     bFirstTimer?"true":"false" , *pTimerCanResume?"true":"false" );
		}

		/* Make sure no outstanding interrupts in list if channel is disabled */
		Int_RemovePendingInterrupt(Handler);
	}

	return TimerClockCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer A or B - If in EventCount MainCounter already has correct value
 */
static Uint8 MFP_ReadTimer_AB(Uint8 TimerControl, Uint8 MainCounter, int TimerCycles, int Handler, bool TimerIsStopping)
{
//	int TimerCyclesPassed;

	/* Find TimerAB count, if no interrupt or not in delay mode assume
	 * in Event Count mode so already up-to-date as kept by HBL */
	if (Int_InterruptActive(Handler) && (TimerControl > 0) && (TimerControl <= 7))
	{
		/* Find cycles passed since last interrupt */
		//TimerCyclesPassed = TimerCycles - Int_FindCyclesPassed ( Handler, INT_MFP_CYCLE );
		MainCounter = MFP_CYCLE_TO_REG ( Int_FindCyclesPassed ( Handler, INT_MFP_CYCLE ), TimerControl );
		//fprintf ( stderr , "mfp read AB passed %d count %d\n" , TimerCyclesPassed, MainCounter );
	}

	/* If the timer is stopped when the internal mfp data reg is already < 1 */
	/* then the data reg will be 0 (=256) next time the timer will be restarted */
	/* if no write is made to the data reg before */
	if ( TimerIsStopping )
	{
		if ( Int_FindCyclesPassed ( Handler, INT_MFP_CYCLE ) < MFP_REG_TO_CYCLES ( 1 , TimerControl ) )
		{
			MainCounter = 0;			/* internal mfp counter becomes 0 (=256) */
			HATARI_TRACE ( HATARI_TRACE_MFP_READ , "mfp read AB handler=%d stopping timer while data reg between 1 and 0 : forcing data to 256\n" ,
					Handler );
		}
	}

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_READ ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "mfp read AB handler=%d data=%d ctrl=%d timer_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
		                     Handler, MainCounter, TimerControl, TimerCycles,
		                     nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}

	return MainCounter;
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer C or D
 */
static Uint8 MFP_ReadTimerCD(Uint8 TimerControl, Uint8 TimerData, Uint8 MainCounter, int TimerCycles, int Handler, bool TimerIsStopping)
{
//	int TimerCyclesPassed;

	/* Find TimerCD count. If timer is off, MainCounter already contains
	 * the latest value */
	if (Int_InterruptActive(Handler))
	{
		/* Find cycles passed since last interrupt */
		//TimerCyclesPassed = TimerCycles - Int_FindCyclesPassed ( Handler, INT_MFP_CYCLE );
		MainCounter = MFP_CYCLE_TO_REG ( Int_FindCyclesPassed ( Handler, INT_MFP_CYCLE ), TimerControl);
		//fprintf ( stderr , "mfp read CD passed %d count %d\n" , TimerCyclesPassed, MainCounter );
	}

	/* If the timer is stopped when the internal mfp data reg is already < 1 */
	/* then the data reg will be 0 (=256) next time the timer will be restarted */
	/* if no write is made to the data reg before */
	if ( TimerIsStopping )
	{
		if ( Int_FindCyclesPassed ( Handler, INT_MFP_CYCLE ) < MFP_REG_TO_CYCLES ( 1 , TimerControl ) )
		{
			MainCounter = 0;			/* internal mfp counter becomes 0 (=256) */
			HATARI_TRACE ( HATARI_TRACE_MFP_READ , "mfp read CD handler=%d stopping timer while data reg between 1 and 0 : forcing data to 256\n" ,
					Handler );
		}
	}

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_READ ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "mfp read CD handler=%d data=%d ctrl=%d timer_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
		                     Handler, MainCounter, TimerControl, TimerCycles,
		                     nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}

	return MainCounter;
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer A
 */
static void MFP_StartTimerA(void)
{
	TimerAClockCycles = MFP_StartTimer_AB(MFP_TACR, MFP_TA_MAINCOUNTER,
	                                      INTERRUPT_MFP_TIMERA, TRUE, &TimerACanResume);
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer A
 */
static void MFP_ReadTimerA(bool TimerIsStopping)
{
	MFP_TA_MAINCOUNTER = MFP_ReadTimer_AB(MFP_TACR, MFP_TA_MAINCOUNTER,
	                                      TimerAClockCycles, INTERRUPT_MFP_TIMERA, TimerIsStopping);
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer B
 * (This does not start the EventCount mode time as this is taken care
 *  of by the HBL)
 */
static void MFP_StartTimerB(void)
{
	TimerBClockCycles = MFP_StartTimer_AB(MFP_TBCR, MFP_TB_MAINCOUNTER,
	                                      INTERRUPT_MFP_TIMERB, TRUE, &TimerBCanResume);
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer B
 */
static void MFP_ReadTimerB(bool TimerIsStopping)
{
	MFP_TB_MAINCOUNTER = MFP_ReadTimer_AB(MFP_TBCR, MFP_TB_MAINCOUNTER,
	                                      TimerBClockCycles, INTERRUPT_MFP_TIMERB, TimerIsStopping);
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer C
 */
static void MFP_StartTimerC(void)
{
	TimerCClockCycles = MFP_StartTimer_CD((MFP_TCDCR>>4)&7, MFP_TC_MAINCOUNTER,
	                                      INTERRUPT_MFP_TIMERC , TRUE, &TimerCCanResume);
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer C
 */
static void MFP_ReadTimerC(bool TimerIsStopping)
{
	MFP_TC_MAINCOUNTER = MFP_ReadTimerCD((MFP_TCDCR>>4)&7, MFP_TCDR, MFP_TC_MAINCOUNTER,
	                                     TimerCClockCycles, INTERRUPT_MFP_TIMERC, TimerIsStopping);
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer D
 */
static void MFP_StartTimerD(void)
{
	TimerDClockCycles = MFP_StartTimer_CD(MFP_TCDCR&7, MFP_TD_MAINCOUNTER,
	                                      INTERRUPT_MFP_TIMERD, TRUE, &TimerDCanResume);
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer D
 */
static void MFP_ReadTimerD(bool TimerIsStopping)
{
	MFP_TD_MAINCOUNTER = MFP_ReadTimerCD(MFP_TCDCR&7, MFP_TDDR, MFP_TD_MAINCOUNTER,
	                                     TimerDClockCycles, INTERRUPT_MFP_TIMERD, TimerIsStopping);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle Timer A Interrupt
 */
void MFP_InterruptHandler_TimerA(void)
{
	/* Number of internal cycles we went over for this timer ( <= 0 ),
	 * used when timer expires and needs to be restarted */
	PendingCyclesOver = -PendingInterruptCount;		/* >= 0 */

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit,enable,pending */
	if ((MFP_TACR&0xf) != 0)            /* Is timer OK? */
		MFP_InputOnChannel(MFP_TIMER_A_BIT, MFP_IERA, &MFP_IPRA);

	/* Start next interrupt, if need one - from current cycle count */
	TimerAClockCycles = MFP_StartTimer_AB(MFP_TACR, MFP_TADR, INTERRUPT_MFP_TIMERA, FALSE, &TimerACanResume);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle Timer B Interrupt
 */
void MFP_InterruptHandler_TimerB(void)
{
	/* Number of internal cycles we went over for this timer ( <= 0 ),
	 * used when timer expires and needs to be restarted */
	PendingCyclesOver = -PendingInterruptCount;		/* >= 0 */

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	if ((MFP_TBCR&0xf) != 0)            /* Is timer OK? */
		MFP_InputOnChannel(MFP_TIMER_B_BIT, MFP_IERA, &MFP_IPRA);

	/* Start next interrupt, if need one - from current cycle count */
	TimerBClockCycles = MFP_StartTimer_AB(MFP_TBCR, MFP_TBDR, INTERRUPT_MFP_TIMERB, FALSE, &TimerBCanResume);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle Timer C Interrupt
 */
void MFP_InterruptHandler_TimerC(void)
{
	/* Number of internal cycles we went over for this timer ( <= 0 ),
	 * used when timer expires and needs to be restarted */
	PendingCyclesOver = -PendingInterruptCount;		/* >= 0 */

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	if ((MFP_TCDCR&0x70) != 0)          /* Is timer OK? */
		MFP_InputOnChannel(MFP_TIMER_C_BIT, MFP_IERB, &MFP_IPRB);

	/* Start next interrupt, if need one - from current cycle count */
	TimerCClockCycles = MFP_StartTimer_CD((MFP_TCDCR>>4)&7, MFP_TCDR, INTERRUPT_MFP_TIMERC, FALSE, &TimerCCanResume);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle Timer D Interrupt
 */
void MFP_InterruptHandler_TimerD(void)
{
	/* Number of internal cycles we went over for this timer ( <= 0 ),
	 * used when timer expires and needs to be restarted */
	PendingCyclesOver = -PendingInterruptCount;		/* >= 0 */

	/* Remove this interrupt from list and re-order */
	Int_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	if ((MFP_TCDCR&0x07) != 0)          /* Is timer OK? */
		MFP_InputOnChannel(MFP_TIMER_D_BIT, MFP_IERB, &MFP_IPRB);

	/* Start next interrupt, if need one - from current cycle count */
	TimerDClockCycles = MFP_StartTimer_CD(MFP_TCDCR&7, MFP_TDDR, INTERRUPT_MFP_TIMERD, FALSE, &TimerDCanResume);
}



/*-----------------------------------------------------------------------*/
/**
 * Handle read from GPIP pins register (0xfffa01).
 *
 * - Bit 0 is the BUSY signal of the printer port, it is SET if no printer
 *   is connected or on BUSY. Therefor we should assume it to be 0 in Hatari
 *   when a printer is emulated.
 * - Bit 1 is used for RS232: DCD
 * - Bit 2 is used for RS232: CTS
 * - Bit 3 is used by the blitter for signalling when its done.
 * - Bit 4 is used by the ACIAs.
 * - Bit 5 is used by the floppy controller / ACSI DMA
 * - Bit 6 is used for RS232: RI
 * - Bit 7 is monochrome monitor detection signal. On STE it is also XORed with
 *   the DMA sound play bit.
 */
void MFP_GPIP_ReadByte(void)
{
	M68000_WaitState(4);

	if (!bUseHighRes)
		MFP_GPIP |= 0x80;   /* Color monitor -> set top bit */
	else
		MFP_GPIP &= ~0x80;
	if (nDmaSoundControl & DMASNDCTRL_PLAY)
		MFP_GPIP ^= 0x80;   /* Top bit is XORed with DMA sound control play bit */

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

	FDC_GpipRead();

	IoMem[0xfffa01] = MFP_GPIP;

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_READ ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "mfp read gpip fa01=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_GPIP, nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from active edge register (0xfffa03).
 */
void MFP_ActiveEdge_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa03] = MFP_AER;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from data direction register (0xfffa05).
 */
void MFP_DataDirection_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa05] = MFP_DDR;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interupt enable register A (0xfffa07).
 */
void MFP_EnableA_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa07] = MFP_IERA;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interupt enable register B (0xfffa09).
 */
void MFP_EnableB_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa09] = MFP_IERB;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interupt pending register A (0xfffa0b).
 */
void MFP_PendingA_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa0b] = MFP_IPRA;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interupt pending register A (0xfffa0d).
 */
void MFP_PendingB_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa0d] = MFP_IPRB;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interupt in service register A (0xfffa0f).
 */
void MFP_InServiceA_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa0f] = MFP_ISRA;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interupt in service register B (0xfffa11).
 */
void MFP_InServiceB_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa11] = MFP_ISRB;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interupt mask register A (0xfffa13).
 */
void MFP_MaskA_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa13] = MFP_IMRA;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interupt mask register B (0xfffa15).
 */
void MFP_MaskB_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa15] = MFP_IMRB;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from MFP vector register (0xfffa17).
 */
void MFP_VectorReg_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa17] = MFP_VR;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from timer A control register (0xfffa19).
 */
void MFP_TimerACtrl_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa19] = MFP_TACR;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from timer B control register (0xfffa1b).
 */
void MFP_TimerBCtrl_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa1b] = MFP_TBCR;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from timer C/D control register (0xfffa1d).
 */
void MFP_TimerCDCtrl_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa1d] = MFP_TCDCR;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from timer A data register (0xfffa1f).
 */
void MFP_TimerAData_ReadByte(void)
{
	M68000_WaitState(4);

	if (MFP_TACR != 8)          		/* Is event count? Need to re-calculate counter */
		MFP_ReadTimerA(FALSE);		/* Stores result in 'MFP_TA_MAINCOUNTER' */

	IoMem[0xfffa1f] = MFP_TA_MAINCOUNTER;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from timer B data register (0xfffa21).
 */
void MFP_TimerBData_ReadByte(void)
{
	Uint8 TB_count;

	M68000_WaitState(4);

	if (MFP_TBCR != 8)			/* Is event count? Need to re-calculate counter */
		MFP_ReadTimerB(FALSE);		/* Stores result in 'MFP_TB_MAINCOUNTER' */

	/* Special case when reading $fffa21, we need to test if the current read instruction */
	/* overlaps the horizontal video position where $fffa21 is changed */
	else
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int pos_start , pos_read;

		/* Cycle position of the start of the current instruction */
		pos_start = nFrameCycles % nCyclesPerLine;
		/* Cycle position of the read for the current instruction (approximatively, we consider */
		/* the read happens after 4 cycles (due to MFP wait states in that case)) */
		/* This is quite a hack, but hard to do without proper 68000 read cycle emulation */
		if ( CurrentInstrCycles <= 8 )			/* move.b (a0),d0 / cmp.b (a0),d0 ... */
			pos_read = pos_start + 4;		/* wait state */
		else						/* cmp.b $fa21.w,d0 (BIG Demo) ... */
			pos_read = pos_start + 8;		/* more time needed to compute the effective address */

		TB_count = MFP_TB_MAINCOUNTER;			/* default value */

		/* If Timer B's change happens before the read cycle of the current instruction, we must return */
		/* the current value - 1 (because MFP_TimerB_EventCount_Interrupt was not called yet) */
		if ( (nHBL >= nStartHBL ) && ( nHBL < nEndHBL )	/* ensure display is ON and timer B can happen */
			&& ( LineTimerBCycle > pos_start ) && ( LineTimerBCycle < pos_read ) )
		{
			HATARI_TRACE ( HATARI_TRACE_MFP_READ , "mfp read TB overlaps pos_start=%d TB_pos=%d pos_read=%d nHBL=%d \n",
					pos_start, LineTimerBCycle, pos_read , nHBL );

			TB_count--;
			if ( TB_count == 0 )			/* going from 1 to 0 : timer restart, reload data reg */
				TB_count = MFP_TBDR;
			/* Going from 0 to -1 : data reg is in fact going from 256 to 255. As TB_count is Uint8, */
			/* this is already what we get when we decrement TB_count=0. So, the next 2 lines are redundant. */
/*			else if ( TB_count < 0 )
				TB_count = 255;
*/
		}

		HATARI_TRACE ( HATARI_TRACE_MFP_READ , "mfp read TB data=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
					TB_count, nFrameCycles, pos_start, nHBL, M68000_GetPC(), CurrentInstrCycles );
		IoMem[0xfffa21] = TB_count;
		return;
	}

	IoMem[0xfffa21] = MFP_TB_MAINCOUNTER;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from timer C data register (0xfffa23).
 */
void MFP_TimerCData_ReadByte(void)
{
	M68000_WaitState(4);

	MFP_ReadTimerC(FALSE);		/* Stores result in 'MFP_TC_MAINCOUNTER' */

	IoMem[0xfffa23] = MFP_TC_MAINCOUNTER;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from timer D data register (0xfffa25).
 */
void MFP_TimerDData_ReadByte(void)
{
	Uint32 pc = M68000_GetPC();

	M68000_WaitState(4);

	if (ConfigureParams.System.bPatchTimerD && pc >= TosAddress && pc <= TosAddress + TosSize)
	{
		/* Trick the tos to believe it was changed: */
		IoMem[0xfffa25] = nTimerDFakeValue;
	}
	else
	{
		MFP_ReadTimerD(FALSE);	/* Stores result in 'MFP_TD_MAINCOUNTER' */
		IoMem[0xfffa25] = MFP_TD_MAINCOUNTER;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle write to GPIP register (0xfffa01).
 */
void MFP_GPIP_WriteByte(void)
{
	M68000_WaitState(4);

	/* Nothing... */
	/*fprintf(stderr, "Write to GPIP: %x\n", (int)IoMem[0xfffa01]);*/
	/*MFP_GPIP = IoMem[0xfffa01];*/   /* TODO: What are the GPIP pins good for? */
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to AER (0xfffa03).
 */
void MFP_ActiveEdge_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_AER = IoMem[0xfffa03];
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to data direction register (0xfffa05).
 */
void MFP_DataDirection_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_DDR = IoMem[0xfffa05];
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt enable register A (0xfffa07).
 */
void MFP_EnableA_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IERA = IoMem[0xfffa07];
	MFP_IPRA &= MFP_IERA;
	MFP_UpdateFlags();
	/* We may have enabled Timer A or B, check */
	/* [NP] No check, restarting the timer is wrong */
//	MFP_StartTimerA();
//	MFP_StartTimerB();
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt enable register B (0xfffa09).
 */
void MFP_EnableB_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IERB = IoMem[0xfffa09];
	MFP_IPRB &= MFP_IERB;
	MFP_UpdateFlags();
	/* We may have enabled Timer C or D, check */
	/* [NP] No check, restarting the timer is wrong */
//	MFP_StartTimerC();
//	MFP_StartTimerD();
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt pending register A (0xfffa0b).
 */
void MFP_PendingA_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IPRA &= IoMem[0xfffa0b];        /* Cannot set pending bits - only clear via software */
	MFP_UpdateFlags();                  /* Check if any interrupts pending */
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt pending register B (0xfffa0d).
 */
void MFP_PendingB_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IPRB &= IoMem[0xfffa0d];
	MFP_UpdateFlags();                  /* Check if any interrupts pending */
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt in service register A (0xfffa0f).
 */
void MFP_InServiceA_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_ISRA &= IoMem[0xfffa0f];        /* Cannot set in-service bits - only clear via software */
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt in service register B (0xfffa11).
 */
void MFP_InServiceB_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_ISRB &= IoMem[0xfffa11];        /* Cannot set in-service bits - only clear via software */
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt mask register A (0xfffa13).
 */
void MFP_MaskA_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IMRA = IoMem[0xfffa13];
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt mask register B (0xfffa15).
 */
void MFP_MaskB_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IMRB = IoMem[0xfffa15];
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to MFP vector register (0xfffa17).
 */
void MFP_VectorReg_WriteByte(void)
{
	Uint8 old_vr;

	M68000_WaitState(4);

	old_vr = MFP_VR;                    /* Copy for checking if set mode */
	MFP_VR = IoMem[0xfffa17];

	if ((MFP_VR^old_vr) & 0x08)         /* Test change in end-of-interrupt mode */
	{
		/* Mode did change but was it to automatic mode? (ie bit is a zero) */
		if (!(MFP_VR & 0x08))
		{
			/* We are now in automatic mode, so clear all in-service bits! */
			MFP_ISRA = 0;
			MFP_ISRB = 0;
		}
	}

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_WRITE ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "mfp write vector reg fa17=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_VR, nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}

}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to timer A control register (0xfffa19).
 */
void MFP_TimerACtrl_WriteByte(void)
{
	Uint8 new_tacr;

	M68000_WaitState(4);

	new_tacr = IoMem[0xfffa19] & 0x0f;  /* FIXME : ignore bit 4 (reset) ? */

	if ( MFP_TACR != new_tacr )         /* Timer control changed */
	{
		/* If we stop a timer which was in delay mode, we need to store
		 * the current value of the counter to be able to read it or to
		 * continue from where we left if the timer is restarted later
		 * without writing to the data register. */
		if ((new_tacr == 0) && (MFP_TACR >=1) && (MFP_TACR <= 7))
			MFP_ReadTimerA(TRUE);	/* Store result in 'MFP_TA_MAINCOUNTER' */

		MFP_TACR = new_tacr;            /* set to new value before calling MFP_StartTimer */
		MFP_StartTimerA();              /* start/stop timer depending on control reg */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to timer B control register (0xfffa1b).
 */
void MFP_TimerBCtrl_WriteByte(void)
{
	Uint8 new_tbcr;

	M68000_WaitState(4);

	new_tbcr = IoMem[0xfffa1b] & 0x0f;  /* FIXME : ignore bit 4 (reset) ? */

	if (MFP_TBCR != new_tbcr)           /* Timer control changed */
	{
		/* If we stop a timer which was in delay mode, we need to store
		 * the current value of the counter to be able to read it or to
		 * continue from where we left if the timer is restarted later
		 * without writing to the data register. */
		if ((new_tbcr == 0) && (MFP_TBCR >= 1) && (MFP_TBCR <= 7))
			MFP_ReadTimerB(TRUE);	/* Store result in 'MFP_TB_MAINCOUNTER' */

		MFP_TBCR = new_tbcr;            /* set to new value before calling MFP_StartTimer */
		MFP_StartTimerB();              /* start/stop timer depending on control reg */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to timer C/D control register (0xfffa1d).
 */
void MFP_TimerCDCtrl_WriteByte(void)
{
	Uint8 new_tcdcr;
	Uint8 old_tcdcr;

	M68000_WaitState(4);

	new_tcdcr = IoMem[0xfffa1d];
	old_tcdcr = MFP_TCDCR;
//fprintf ( stderr , "write fa1d new %x old %x\n" , IoMem[0xfffa1d] , MFP_TCDCR );

	if ((old_tcdcr & 0x70) != (new_tcdcr & 0x70))	/* Timer C control changed */
	{
		/* If we stop a timer which was in delay mode, we need to store
		 * the current value of the counter to be able to read it or to
		 * continue from where we left if the timer is restarted later
		 * without writing to the data register. */
		if ((new_tcdcr & 0x70) == 0)
			MFP_ReadTimerC(TRUE);		/* Store result in 'MFP_TC_MAINCOUNTER' */

		MFP_TCDCR = ( new_tcdcr & 0x70 ) | ( old_tcdcr & 0x07 );	/* we set TCCR and keep old TDDR in case we need to read it below */
		MFP_StartTimerC();			/* start/stop timer depending on control reg */
	}

	if ((old_tcdcr & 0x07) != (new_tcdcr & 0x07))	/* Timer D control changed */
	{
		Uint32 pc = M68000_GetPC();

		/* Need to change baud rate of RS232 emulation? */
		if (ConfigureParams.RS232.bEnableRS232)
		{
			RS232_SetBaudRateFromTimerD();
		}

		if (ConfigureParams.System.bPatchTimerD && !bAppliedTimerDPatch
		        && pc >= TosAddress && pc <= TosAddress + TosSize)
		{
			/* Slow down Timer-D if set from TOS for the first time to gain
			 * more desktop performance.
			 * Obviously, we need to emulate all timers correctly but TOS sets
			 * up Timer-D at a very high rate (every couple of instructions).
			 * The interrupt isn't enabled but the emulator still needs to
			 * process the interrupt table and this HALVES our frame rate!!!
			 * Some games actually reference this timer but don't set it up
			 * (eg Paradroid, Speedball I) so we simply intercept the Timer-D
			 * setup code in TOS and fix the numbers with more 'laid-back'
			 * values. This still keeps 100% compatibility */
			if ( new_tcdcr & 0x07 )			/* apply patch only if timer D is being started */
			{
				new_tcdcr = IoMem[0xfffa1d] = (IoMem[0xfffa1d] & 0xf0) | 7;
				bAppliedTimerDPatch = TRUE;
			}
		}

		/* If we stop a timer which was in delay mode, we need to store the current value */
		/* of the counter to be able to read it or to continue from where we left if the timer is */
		/* restarted later without writing to the data register. */
		if ((new_tcdcr & 0x07) == 0)
			MFP_ReadTimerD(TRUE);	/* Stores result in 'MFP_TD_MAINCOUNTER' */

		MFP_TCDCR = new_tcdcr;		/* set to new value before calling MFP_StartTimer */
		MFP_StartTimerD();		/* start/stop timer depending on control reg */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to timer A data register (0xfffa1f).
 */
void MFP_TimerAData_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_TADR = IoMem[0xfffa1f];         /* Store into data register */

	if (MFP_TACR == 0)                  /* Now check if timer is running - if so do not set */
	{
		MFP_TA_MAINCOUNTER = MFP_TADR;  /* Timer is off, store to main counter */
		TimerACanResume = FALSE;        /* we need to set a new int when timer start */
	}

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_WRITE ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "mfp write data reg A fa1f=0x%x new counter=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_TADR, MFP_TA_MAINCOUNTER, nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to timer B data register (0xfffa21).
 */
void MFP_TimerBData_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_TBDR = IoMem[0xfffa21];         /* Store into data register */

	if (MFP_TBCR == 0)                  /* Now check if timer is running - if so do not set */
	{
		MFP_TB_MAINCOUNTER = MFP_TBDR;  /* Timer is off, store to main counter */
		TimerBCanResume = FALSE;        /* we need to set a new int when timer start */
	}

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_WRITE ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "mfp write data reg B fa21=0x%x new counter=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_TBDR, MFP_TB_MAINCOUNTER, nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to timer C data register (0xfffa23).
 */
void MFP_TimerCData_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_TCDR = IoMem[0xfffa23];         /* Store into data register */

	if ((MFP_TCDCR&0x70) == 0)          /* Now check if timer is running - if so do not set */
	{
		MFP_TC_MAINCOUNTER = MFP_TCDR;  /* Timer is off, store to main counter */
		TimerCCanResume = FALSE;        /* we need to set a new int when timer start */
	}

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_WRITE ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "mfp write data reg C fa23=0x%x new counter=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_TCDR, MFP_TC_MAINCOUNTER, nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to timer D data register (0xfffa25).
 */
void MFP_TimerDData_WriteByte(void)
{
	Uint32 pc = M68000_GetPC();

	M68000_WaitState(4);

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
		MFP_TD_MAINCOUNTER = MFP_TDDR;  /* Timer is off, store to main counter */
		TimerDCanResume = FALSE;        /* we need to set a new int when timer start */
	}

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_MFP_WRITE ) )
	{
		int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);
		int nLineCycles = nFrameCycles % nCyclesPerLine;
		HATARI_TRACE_PRINT ( "mfp write data reg D fa25=0x%x new counter=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_TDDR, MFP_TD_MAINCOUNTER, nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	}
}
