/*
  Hatari - mfp.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

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
/*			occurred. This could cause bus error when restoring snapshot	*/
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
/* 2008/12/11	[NP]	In MFP_CheckPendingInterrupts(), returns true or false instead	*/
/*			of void, depending on whether at least one MFP interrupt was	*/
/*			allowed or not.							*/
/* 2009/03/28	[NP]	Handle bit 3 of AER for timer B (fix Seven Gates Of Jambala).	*/
/* 2010/07/26	[NP]	In MFP_StartTimer_AB, when ctrl reg is in pulse width mode,	*/
/*			clear bit 3 to emulate it as in delay mode. This is not		*/
/*			completely correct as we should also emulate GPIO 3/4, but it	*/
/*			helps running some programs (fix the game Erik).		*/
/* 2013/02/24	[NP]	- In MFP_CheckPendingInterrupts, don't check all the MFP ints,	*/
/*			stop as soon as the highest interrupt is found (simultaneous	*/
/*			interrupts could be processed during the same cycle and were	*/
/*			stacked/executed in the reverse order, from lowest to highest	*/
/*			priority, which was wrong).					*/
/*			- Use MFP_ProcessIRQ to separate the MFP's IRQ signal handling	*/
/*			and the	exception processing at the CPU level.			*/
/* 2013/03/01	[NP]	When MFP_IRQ goes from 0 to 1, the resulting signal is visible	*/
/*			to the CPU only 4 cycles later (fix Audio Artistic Demo by	*/
/*			Big Alec and the games Super Hang On, Super Monaco GP, Bolo).	*/
/* 2013/03/10	[NP]	Improve the MFP_IRQ 4 cycle delay by taking into account the	*/
/*			time at which the timer expired during the CPU instruction	*/
/*			(fix Reset part in Decade Demo, High Fidelity Dreams by Aura).	*/
/* 2013/03/14	[NP]	When writing to the MFP's registers, take the write cycles into	*/
/*			account when updating MFP_IRQ_Time (properly fix Super Hang On).*/
/* 2013/04/11	[NP]	Handle the IACK cycles, interrupts can change during the first	*/
/*			12 cycles of an MFP exception (fix Anomaly Demo Menu by MJJ Prod*/
/*			and sample intro in the game The Final Conflict).		*/
/* 2013/04/21	[NP]	Handle the case where several MFP interrupts happen during the	*/
/*			same CPU instruction but at different sub-cycles. We must take	*/
/*			into account only the oldest interrupts to choose the highest	*/
/*			one (fix Fuzion CD Menus 77, 78, 84).				*/
/* 2015/02/27	[NP]	Better support for GPIP/AER/DDR and trigerring an interrupt	*/
/*			when AER is changed (fix the MIDI programs  Realtime and M	*/
/*			by Eric Ameres, which toggle bit 0 in AER).			*/
/* 2015/04/08	[NP]	When an interrupt happens on timers A/B/C/D, take into account	*/
/*			PendingCyclesOver to determine if a 4 cycle delay should be	*/
/*			added or not (depending on when it happened during the CPU	*/
/*			instruction).							*/

const char MFP_fileid[] = "Hatari mfp.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "crossbar.h"
#include "fdc.h"
#include "ikbd.h"
#include "hatari-glue.h"
#include "cycInt.h"
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
#include "vdi.h"
#include "screen.h"
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


/*
  Emulation Note :
  - MFP emulation doesn't run in parallel with the CPU emulation as it would take too much resources.
    Instead, MFP emulation is called each time a CPU instruction is completely processed.
    The drawback is that several MFP interrupts can happen during a single CPU instruction (especially
    for long ones like MOVEM or DIV). In that case, we should not choose the highest priority interrupt
    among all the interrupts, but we should keep only the interrupts that chronologically happened first
    during this CPU instruction (and ignore the other interrupts' requests for this CPU instruction).
  - When the MFP's main IRQ signal goes from 0 to 1, the signal is not immediately visible to the CPU, but only
    4 cycles later. This 4 cycle delay should be taken into account, depending at what time the signal
    went to 1 in the corresponding CPU instruction (the 4 cycle delay can be "included" in the CPU instruction
    in some cases)
  - When an interrupt happens in the MFP, an exception will be started in the CPU. Then after 12 cycles an IACK
    sequence will be started by the CPU to request the interrupt vector from the MFP. During those 12 cycles,
    it is possible that a new higher priority MFP interrupt happens and in that case we must replace the MFP
    vector number that was initially computed at the start of the exception with the new one.
    This is also after the IACK sequence that in service / pending bits must be handled for this MFP's interrupt.
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
static bool TimerACanResume = false;
static bool TimerBCanResume = false;
static bool TimerCCanResume = false;
static bool TimerDCanResume = false;

static bool bAppliedTimerDPatch;    /* true if the Timer-D patch has been applied */
static int nTimerDFakeValue;        /* Faked Timer-D data register for the Timer-D patch */

static int PendingCyclesOver = 0;   /* >= 0 value, used to "loop" a timer when data counter reaches 0 */


#define	MFP_IRQ_DELAY_TO_CPU		4		/* When MFP_IRQ is set, it takes 4 CPU cycles before it's visible to the CPU */

static int	MFP_Current_Interrupt = -1;
static Uint8	MFP_IRQ = 0;
static Uint64	MFP_IRQ_Time = 0;
static Uint8	MFP_IRQ_CPU = 0;			/* Value of MFP_IRQ as seen by the CPU. There's a 4 cycle delay */
							/* between a change of MFP_IRQ and its visibility at the CPU side */
bool		MFP_UpdateNeeded = false;		/* When set to true, main CPU loop should call MFP_UpdateIRQ() */
static Uint64	MFP_Pending_Time_Min;			/* Clock value of the oldest pending int since last MFP_UpdateIRQ() */
static Uint64	MFP_Pending_Time[ MFP_INT_MAX+1 ];	/* Clock value when pending is set to 1 for each non-masked int */

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


/* Interrupt number associated to each line of the GPIP */
static const int MFP_GPIP_LineToIntNumber[] = { MFP_INT_GPIP0 , MFP_INT_GPIP1 , MFP_INT_GPIP2 , MFP_INT_GPIP3,
	MFP_INT_GPIP4 , MFP_INT_GPIP5 , MFP_INT_GPIP6 , MFP_INT_GPIP7 };



/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static Uint8	MFP_ConvertIntNumber ( int Interrupt , Uint8 **pMFP_IER , Uint8 **pMFP_IPR , Uint8 **pMFP_ISR , Uint8 **pMFP_IMR );
static void	MFP_Exception ( int Interrupt );
static bool	MFP_InterruptRequest ( int Int , Uint8 Bit , Uint8 IPRx , Uint8 IMRx , Uint8 PriorityMaskA , Uint8 PriorityMaskB );
static int	MFP_CheckPendingInterrupts ( void );
static void	MFP_GPIP_Update_Interrupt ( Uint8 GPIP_old , Uint8 GPIP_new , Uint8 AER_old , Uint8 AER_new , Uint8 DDR_old , Uint8 DDR_new );



/*-----------------------------------------------------------------------*/
/**
 * Reset all MFP variables and start interrupts on their way!
 */
void MFP_Reset(void)
{
	int	i;

	/* Reset MFP internal variables */

	bAppliedTimerDPatch = false;

	MFP_GPIP = 0;
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

	/* Clear IRQ */
	MFP_Current_Interrupt = -1;
	MFP_IRQ = 0;
	MFP_IRQ_CPU = 0;
	MFP_IRQ_Time = 0;
	MFP_UpdateNeeded = false;
	MFP_Pending_Time_Min = UINT64_MAX;
	for ( i=0 ; i<=MFP_INT_MAX ; i++ )
		MFP_Pending_Time[ i ] = UINT64_MAX;
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
	MemorySnapShot_Store(&MFP_Current_Interrupt, sizeof(MFP_Current_Interrupt));
	MemorySnapShot_Store(&MFP_IRQ, sizeof(MFP_IRQ));
	MemorySnapShot_Store(&MFP_IRQ_Time, sizeof(MFP_IRQ_Time));
	MemorySnapShot_Store(&MFP_IRQ_CPU, sizeof(MFP_IRQ_CPU));
	MemorySnapShot_Store(&MFP_UpdateNeeded, sizeof(MFP_UpdateNeeded));
	MemorySnapShot_Store(&MFP_Pending_Time_Min, sizeof(MFP_Pending_Time_Min));
	MemorySnapShot_Store(&MFP_Pending_Time, sizeof(MFP_Pending_Time));
}



/*-----------------------------------------------------------------------*/
/**
 * Given an MFP interrupt number, return a pointer to the corresponding
 * registers handling this interrupt, as well as the binary value
 * to set/clear these registers.
 * If an input pointer is NULL, we don't return the corresponding register.
 */
static Uint8 MFP_ConvertIntNumber ( int Interrupt , Uint8 **pMFP_IER , Uint8 **pMFP_IPR , Uint8 **pMFP_ISR , Uint8 **pMFP_IMR )
{
	Uint8	Bit;

	if ( Interrupt > 7 )
	{
		Bit = 1 << ( Interrupt - 8 );
		if ( pMFP_IER )		*pMFP_IER = &MFP_IERA;
		if ( pMFP_IPR )		*pMFP_IPR = &MFP_IPRA;
		if ( pMFP_ISR )		*pMFP_ISR = &MFP_ISRA;
		if ( pMFP_IMR )		*pMFP_IMR = &MFP_IMRA;
	}
	else
	{
		Bit = 1 << Interrupt;
		if ( pMFP_IER ) 	*pMFP_IER = &MFP_IERB;
		if ( pMFP_IPR )		*pMFP_IPR = &MFP_IPRB;
		if ( pMFP_ISR )		*pMFP_ISR = &MFP_ISRB;
		if ( pMFP_IMR )		*pMFP_IMR = &MFP_IMRB;
	}

	return Bit;
}


/*-----------------------------------------------------------------------*/
/**
 * Call the MFP exception associated to the current MFP interrupt 0-15.
 * When the MFP sets its IRQ signal, it will put the interrupt vector number
 * on the data bus ; the 68000 will read it during the IACK cycle
 * and multiply it by 4 to get the address of the exception handler.
 * The upper 4 bits of the vector number are stored in the VR register 0xfffa17
 * (default value is 0x40, which gives exceptions' handlers located at 0x100 in RAM)
 */
static void MFP_Exception ( int Interrupt )
{
	unsigned int VecNr;

	VecNr = ( MFP_VR & 0xf0 ) + Interrupt;

	if (LOG_TRACE_LEVEL(TRACE_MFP_EXCEPTION))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp excep int=%d vec=0x%x new_pc=0x%x video_cyc=%d %d@%d\n" ,
			Interrupt, VecNr * 4, STMemory_ReadLong ( VecNr * 4 ), FrameCycles, LineCycles, HblCounterVideo );
	}

#ifndef WINUAE_FOR_HATARI
	M68000_Exception(VecNr, M68000_EXC_SRC_INT_MFP);
#else
	M68000_Exception(EXCEPTION_NR_MFP_DSP, M68000_EXC_SRC_INT_MFP);
#endif
}




/*-----------------------------------------------------------------------*/
/**
 * Get the value of the MFP IRQ signal as seen from the CPU side.
 * When MFP_IRQ is changed in the MFP, the new value is visible on the
 * CPU side after MFP_IRQ_DELAY_TO_CPU.
 * MFP_IRQ_CPU holds the value seen by the CPU, it's updated with the value
 * of MFP_IRQ when MFP_IRQ_DELAY_TO_CPU cycles passed.
 */
Uint8	MFP_GetIRQ_CPU ( void )
{
	return MFP_IRQ_CPU;
}




/*-----------------------------------------------------------------------*/
/**
 * A change in MFP_IRQ is visible to the CPU only after MFP_IRQ_DELAY_TO_CPU
 * cycles. This function will update MFP_IRQ_CPU if the delay has expired.
 *
 * This function is called from the CPU emulation part when SPCFLAG_MFP is set.
 *
 * TODO : for now, we check the delay only when MFP_IRQ goes to 1, but this should be
 * handled too when MFP_IRQ goes to 0 (need to be measured on STF)
 */
void	MFP_DelayIRQ ( void )
{
	if ( MFP_IRQ == 1 )
	{
		if ( CyclesGlobalClockCounter - MFP_IRQ_Time >= MFP_IRQ_DELAY_TO_CPU )
		{
			MFP_IRQ_CPU = MFP_IRQ;
			M68000_UnsetSpecial ( SPCFLAG_MFP );	/* Update done, unset special MFP flag */
		}
	}

	else	/* MFP_IRQ == 0, no delay for now */
	{
		MFP_IRQ_CPU = MFP_IRQ;
		M68000_UnsetSpecial ( SPCFLAG_MFP );		/* Update done, unset special MFP flag */
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Return the vector number associated to the current MFP interrupt.
 * MFP_ProcessIACK is called 12 cycles after the start of the 68000 exception.
 * We must call MFP_UpdateIRQ just before the IACK cycles to update
 * MFP_Current_Interrupt in case a higher MFP interrupt happened
 * or pending bit was set twice for the same interrupt during those 12 cycles (rare case)
 */
int	MFP_ProcessIACK ( int OldVecNr )
{
	Uint8	*pPendingReg;
	Uint8	*pInServiceReg;
	Uint8	Bit;
	int	NewVecNr;


	/* Check if MFP interrupt vector number changed before IACK */
	MFP_UpdateIRQ ( CyclesGlobalClockCounter );

	NewVecNr = ( MFP_VR & 0xf0 ) + MFP_Current_Interrupt;

	/* Print traces if VecNr changed just before IACK */
	if ( LOG_TRACE_LEVEL(TRACE_MFP_EXCEPTION) && ( OldVecNr != NewVecNr ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp iack change old_vec=0x%x new_vec=0x%x new_pc=0x%x video_cyc=%d %d@%d\n" ,
			OldVecNr * 4, NewVecNr * 4, STMemory_ReadLong ( NewVecNr * 4 ) , FrameCycles, LineCycles, HblCounterVideo );
	}
 
	Bit = MFP_ConvertIntNumber ( MFP_Current_Interrupt , NULL , &pPendingReg , &pInServiceReg , NULL );

	*pPendingReg &= ~Bit;			/* Clear pending bit */

	/* Are we in 'auto' interrupt or 'manual' ? */
	if ( MFP_VR & 0x08 )			/* Software End-of-Interrupt (SEI) */
		*pInServiceReg |= Bit;		/* Set interrupt in service register */
	else
		*pInServiceReg &= ~Bit;		/* Clear interrupt in service register */

	MFP_UpdateIRQ ( CyclesGlobalClockCounter );

	return NewVecNr;			/* Vector number */
}




/*-----------------------------------------------------------------------*/
/**
 * This function is called from the CPU emulation part when SPCFLAG_MFP is set.
 * If the MFP's IRQ signal is set, we check that SR allows a level 6 interrupt,
 * and if so, we call MFP_Exception.
 * If SR doesn't allow an MFP interrupt, MFP's pending requests will be
 * processed later when SR allows it.
 *
 * Important timing note : when the MFP's IRQ signal is set, it's visible to
 * the CPU only 4 cycles later. Depending if the signal happens during a CPU
 * instruction or just before processing a new instruction, this delay will
 * not always be necessary.
 *
 * Instead of using CycInt_AddRelativeInterrupt to simulate this 4 cycles delay,
 * we use MFP_IRQ_Time to delay the exception processing until 4 cycles have
 * passed.
 */
bool	MFP_ProcessIRQ ( void )
{
//fprintf ( stderr , "process irq %d %lld %lld - ipr %x %x imr %x %x isr %x %x\n" , MFP_IRQ , CyclesGlobalClockCounter , MFP_IRQ_Time ,  MFP_IPRA , MFP_IPRB , MFP_IMRA , MFP_IMRB , MFP_ISRA , MFP_ISRB );

	if ( MFP_IRQ == 1 )
	{
		if ( CyclesGlobalClockCounter - MFP_IRQ_Time < MFP_IRQ_DELAY_TO_CPU )	/* Is it time to trigger the exception ? */
		{
			return false;				/* For now, return without calling an exception (and try again later) */
		}

		if (regs.intmask < 6)
		{
			/* The exception is possible ; pending / in service bits will be handled in MFP_ProcessIACK() */
			MFP_Exception ( MFP_Current_Interrupt );
			return true;
		}
	}

	return false;
}



/*-----------------------------------------------------------------------*/
/**
 * Update the MFP IRQ signal when IERx, IPRx, ISRx or IMRx are modified.
 * We set the special flag SPCFLAG_MFP accordingly (to say if an MFP interrupt
 * is to be processed) so we only have one compare to call MFP_ProcessIRQ
 * during the CPU's decode instruction loop.
 * If MFP_IRQ goes from 0 to 1, we update MFP_IRQ_Time to correctly emulate
 * the 4 cycle delay before MFP_IRQ is visible to the CPU.
 *
 * When MFP_UpdateIRQ() is called after writing to an MFP's register, Event_Time
 * will be the time of the write cycle.
 * When MFP_UpdateIRQ is called from the main CPU loop after processing the
 * internal timers, Event_Time will be 0 and we must use MFP_Pending_Time[ NewInt ].
 * This way, MFP_IRQ_Time should always be correct to check the delay in MFP_ProcessIRQ().
 */
void MFP_UpdateIRQ ( Uint64 Event_Time )
{
	int	NewInt;

//fprintf ( stderr , "updirq0 %d - ipr %x %x imr %x %x isr %x %x\n" , MFP_IRQ , MFP_IPRA , MFP_IPRB , MFP_IMRA , MFP_IMRB , MFP_ISRA , MFP_ISRB );

	if ( ( MFP_IPRA & MFP_IMRA ) | ( MFP_IPRB & MFP_IMRB ) )
	{
		NewInt = MFP_CheckPendingInterrupts ();
		
		if ( NewInt >= 0 )
		{
			if ( MFP_IRQ == 0 )			/* MFP IRQ goes from 0 to 1 */
			{
				if ( Event_Time != 0 )
					MFP_IRQ_Time = Event_Time;
				else
					MFP_IRQ_Time = MFP_Pending_Time[ NewInt ];
			}

			MFP_IRQ = 1;
			MFP_Current_Interrupt = NewInt;
		}
		else
			MFP_IRQ = 0;				/* Pending interrupts are blocked by in-service interrupts */
	}
	else
	{
		MFP_IRQ = 0;
	}

//fprintf ( stderr , "updirq1 %d %lld - ipr %x %x imr %x %x isr %x %x\n" , MFP_IRQ , MFP_IRQ_Time , MFP_IPRA , MFP_IPRB , MFP_IMRA , MFP_IMRB , MFP_ISRA , MFP_ISRB );
#ifndef WINUAE_FOR_HATARI
	if ( MFP_IRQ == 1 )
	{
		M68000_SetSpecial(SPCFLAG_MFP);
	}
	else
		M68000_UnsetSpecial(SPCFLAG_MFP);
#else
	M68000_SetSpecial(SPCFLAG_MFP);				/* CPU part should call MFP_Delay_IRQ() */
#endif

	/* Update IRQ is done, reset Time_Min and UpdateNeeded */
	MFP_Pending_Time_Min = UINT64_MAX;
	MFP_UpdateNeeded = false;
}


/*-----------------------------------------------------------------------*/
/**
 * Test if interrupt 'Bit' is set in pending and mask register.
 * Also check that no higher priority interrupt is in service.
 * Depending on the interrupt, we check either IPRA/IMRA or IPRB/IMRB
 * @return true if the MFP interrupt request is allowed
 */
static bool MFP_InterruptRequest ( int Int , Uint8 Bit , Uint8 IPRx , Uint8 IMRx , Uint8 PriorityMaskA , Uint8 PriorityMaskB )
{
//fprintf ( stderr , "mfp int req %d %x %x %X %x %x %x %x\n" , Int , Bit , IPRx , IMRx , PriorityMaskA , PriorityMaskB , MFP_ISRA , MFP_ISRB );

	if ( ( IPRx & IMRx & Bit ) 					/* Interrupt is pending and not masked */
	    && ( MFP_Pending_Time[ Int ] <= MFP_Pending_Time_Min ) )	/* Process pending requests in chronological time */
	{
		/* Are any higher priority interrupts in service ? */
		if ( ( ( MFP_ISRA & PriorityMaskA ) == 0 ) && ( ( MFP_ISRB & PriorityMaskB ) == 0 ) )
			return true;				/* No higher int in service */
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Check if any MFP interrupts can be serviced.
 * @return MFP interrupt number for the highest interrupt allowed, else return -1.
 */
static int MFP_CheckPendingInterrupts ( void )
{
	if ( MFP_InterruptRequest ( MFP_INT_GPIP7 , MFP_GPIP7_BIT, MFP_IPRA, MFP_IMRA, 0x80, 0x00 ) )		/* Check MFP GPIP7 interrupt (bit 7) */
		return MFP_INT_GPIP7;
	
	if ( MFP_InterruptRequest ( MFP_INT_GPIP6 , MFP_GPIP6_BIT, MFP_IPRA, MFP_IMRA, 0xc0, 0x00 ) )		/* Check MFP GPIP6 interrupt (bit 6) */
		return MFP_INT_GPIP6;
	
	if ( MFP_InterruptRequest ( MFP_INT_TIMER_A , MFP_TIMER_A_BIT, MFP_IPRA, MFP_IMRA, 0xe0, 0x00 ) )	/* Check Timer A (bit 5) */
		return MFP_INT_TIMER_A;

	if ( MFP_InterruptRequest ( MFP_INT_RCV_BUF_FULL , MFP_RCV_BUF_FULL_BIT, MFP_IPRA, MFP_IMRA, 0xf0, 0x00 ) )	/* Check Receive buffer full (bit 4) */
		return MFP_INT_RCV_BUF_FULL;

	if ( MFP_InterruptRequest ( MFP_INT_RCV_ERR , MFP_RCV_ERR_BIT, MFP_IPRA, MFP_IMRA, 0xf8, 0x00 ) )	/* Check Receive error (bit 3) */
		return MFP_INT_RCV_ERR;

	if ( MFP_InterruptRequest ( MFP_INT_TRN_BUF_EMPTY , MFP_TRN_BUF_EMPTY_BIT, MFP_IPRA, MFP_IMRA, 0xfc, 0x00 ) )	/* Check Transmit buffer empty (bit 2) */
		return MFP_INT_TRN_BUF_EMPTY;

	if ( MFP_InterruptRequest ( MFP_INT_TRN_ERR , MFP_TRN_ERR_BIT, MFP_IPRA, MFP_IMRA, 0xfe, 0x00 ) )	/* Check Transmit error empty (bit 1) */
		return MFP_INT_TRN_ERR;

	if ( MFP_InterruptRequest ( MFP_INT_TIMER_B , MFP_TIMER_B_BIT, MFP_IPRA, MFP_IMRA, 0xff, 0x00 ) )	/* Check Timer B (bit 0) */
		return MFP_INT_TIMER_B;

	if ( MFP_InterruptRequest ( MFP_INT_GPIP5 , MFP_GPIP5_BIT, MFP_IPRB, MFP_IMRB, 0xff, 0x80 ) )		/* Check GPIP5 = FDC (bit 7) */
		return MFP_INT_GPIP5;

	if ( MFP_InterruptRequest ( MFP_INT_GPIP4 , MFP_GPIP4_BIT, MFP_IPRB, MFP_IMRB, 0xff, 0xc0 ) )		/* Check GPIP4 = ACIA (Keyboard or MIDI) (bit 6) */
		return MFP_INT_GPIP4;

	if ( MFP_InterruptRequest ( MFP_INT_TIMER_C , MFP_TIMER_C_BIT, MFP_IPRB, MFP_IMRB, 0xff, 0xe0 ) )	/* Check Timer C (bit 5) */
		return MFP_INT_TIMER_C;

	if ( MFP_InterruptRequest ( MFP_INT_TIMER_D , MFP_TIMER_D_BIT, MFP_IPRB, MFP_IMRB, 0xff, 0xf0 ) )	/* Check Timer D (bit 4) */
		return MFP_INT_TIMER_D;

	if ( MFP_InterruptRequest ( MFP_INT_GPIP3 , MFP_GPIP3_BIT, MFP_IPRB, MFP_IMRB, 0xff, 0xf8 ) )		/* Check GPIP3 = GPU/Blitter (bit 3) */
		return MFP_INT_GPIP3;

	if ( MFP_InterruptRequest ( MFP_INT_GPIP2 , MFP_GPIP2_BIT, MFP_IPRB, MFP_IMRB, 0xff, 0xfc ) )		/* Check GPIP2 (bit 2) */
		return MFP_INT_GPIP2;

	if ( MFP_InterruptRequest ( MFP_INT_GPIP1 , MFP_GPIP1_BIT, MFP_IPRB, MFP_IMRB, 0xff, 0xfe ) )		/* Check (Falcon) Centronics ACK / (ST) RS232 DCD (bit 1) */
		return MFP_INT_GPIP1;

	if ( MFP_InterruptRequest ( MFP_INT_GPIP0 , MFP_GPIP0_BIT, MFP_IPRB, MFP_IMRB, 0xff, 0xff ) )		/* Check Centronics BUSY (bit 0) */
		return MFP_INT_GPIP0;

	return -1;						/* No pending interrupt */
}


/*-----------------------------------------------------------------------*/
/**
 * If interrupt channel is active, set pending bit so it can be serviced
 * later.
 * As internal timers are processed after the current CPU instruction was
 * emulated, we use Interrupt_Delayed_Cycles to compute the precise time
 * at which the timer expired (it could be during the previous instruction).
 * This allows to correctly handle the 4 cycle MFP_IRQ delay in MFP_ProcessIRQ().
 *
 * As we can have several inputs during one CPU instruction, not necessarily
 * sorted by Interrupt_Delayed_Cycles, we must call MFP_UpdateIRQ() only later
 * in the main CPU loop, when all inputs were received, to choose the oldest
 * input's event time.
 */
void	MFP_InputOnChannel ( int Interrupt , int Interrupt_Delayed_Cycles )
{
	Uint8	*pEnableReg;
	Uint8	*pPendingReg;
	Uint8	*pMaskReg;
	Uint8	Bit;

//fprintf ( stderr , "mfp input %d delay %d clock %lld\n" , Interrupt , Interrupt_Delayed_Cycles , CyclesGlobalClockCounter );
	Bit = MFP_ConvertIntNumber ( Interrupt , &pEnableReg , &pPendingReg , NULL , &pMaskReg );

	/* Input has occurred on MFP channel, set interrupt pending to request service when able */
	if ( *pEnableReg & Bit )
	{
		/* Print traces if pending bits changed just before IACK */
		if ( LOG_TRACE_LEVEL(TRACE_MFP_EXCEPTION) && ( CPU_IACK == true ) )
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			if ( *pPendingReg & Bit )
				LOG_TRACE_PRINT("mfp input, pending set again during iack for int=%d, skip one interrupt video_cyc=%d %d@%d\n" ,
					Interrupt , FrameCycles, LineCycles, HblCounterVideo );
			else
				LOG_TRACE_PRINT("mfp input, new pending set during iack for int=%d video_cyc=%d %d@%d\n" ,
					Interrupt , FrameCycles, LineCycles, HblCounterVideo );
		}

		/* Set pending bit and event's time */
		*pPendingReg |= Bit;
		MFP_Pending_Time[ Interrupt ] = CyclesGlobalClockCounter - Interrupt_Delayed_Cycles;

		/* Store the time of the most ancient non-masked pending=1 event */
		if ( ( *pMaskReg & Bit ) && ( MFP_Pending_Time[ Interrupt ] < MFP_Pending_Time_Min ) )
			MFP_Pending_Time_Min = MFP_Pending_Time[ Interrupt ];
	}
	else
		*pPendingReg &= ~Bit;				/* Clear bit */

	MFP_UpdateNeeded = true;				/* Tell main CPU loop to call MFP_UpdateIRQ() */
}


/*-----------------------------------------------------------------------*/
/**
 * Update the interrupt status of the GPIP when the GPIP, AER or DDR
 * registers are changed.
 * Only lines defined as input in DDR can generate an interrupt.
 * Each input line is XORed with the corresponding AER bit to choose
 * if the interrupt should be triggered on 1->0 transition or 0->1.
 * 
 * NOTE : In most case, only the input line will change, but because input line
 * and AER are XORed, this means that an interrupt can trigger too
 * if AER is changed ! ('M' and 'Realtime' are doing bset #0,$fffa03
 * then bclr #0,$fffa03)
 */
static void	MFP_GPIP_Update_Interrupt ( Uint8 GPIP_old , Uint8 GPIP_new , Uint8 AER_old , Uint8 AER_new , Uint8 DDR_old , Uint8 DDR_new )
{
	Uint8	State_old;
	Uint8	State_new;
	int	Bit;
	Uint8	BitMask;

	State_old = GPIP_old ^ AER_old;
	State_new = GPIP_new ^ AER_new;

	/* For each line, check if it's defined as input in DDR (0=input 1=output) */
	/* and if the state is changing (0->1 or 1->0) */
	for ( Bit=0 ; Bit<8 ; Bit++ )
	{
		BitMask = 1<<Bit;
		if ( ( ( DDR_new & BitMask ) == 0 )		/* Line set as input */
		  && ( ( State_old & BitMask ) != ( State_new & BitMask ) ) )
		{
			/* If AER=0, trigger on 1->0 ; if AER=1, trigger on 0->1 */
			/* -> so, we trigger if AER=GPIP_new */
			if ( ( GPIP_new & BitMask ) == ( AER_new & BitMask ) )
			{
//fprintf ( stderr , "gpip int bit=%d %d->%d\n" , Bit , (State_old & BitMask)>>Bit , (State_new & BitMask)>>Bit );
				MFP_InputOnChannel ( MFP_GPIP_LineToIntNumber[ Bit ] , 0 );
			}
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Change the state of one of the external lines connected to the GPIP.
 * Only lines configured as input in DDR can be changed.
 * If the new state is different from the previous one, we update GPIP and
 * we request an interrupt on the corresponding channel.
 */
void	MFP_GPIP_Set_Line_Input ( Uint8 LineNr , Uint8 Bit )
{
	Uint8	Mask;
	Uint8	GPIP_old;

	Mask = 1 << LineNr;

	/* Check that corresponding line is defined as input in DDR (0=input 1=output) */
	/* and that the bit is changing */
	if ( ( ( MFP_DDR & Mask ) == 0 )
	  && ( ( MFP_GPIP & Mask ) != ( Bit << LineNr ) ) )
	{
		GPIP_old = MFP_GPIP;

		if ( Bit )
		{
			MFP_GPIP |= Mask;
		}
		else
		{
			MFP_GPIP &= ~Mask;
			/* TODO : For now, assume AER=0 and to an interrupt on 1->0 transition */
//			MFP_InputOnChannel ( MFP_GPIP_LineToIntNumber[ LineNr ] , 0 );
		}

		/* Update possible interrupts after changing GPIP */
		MFP_GPIP_Update_Interrupt ( GPIP_old , MFP_GPIP , MFP_AER , MFP_AER , MFP_DDR , MFP_DDR );
	}
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
		MFP_InputOnChannel ( MFP_INT_TIMER_A , 0 );
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
void MFP_TimerB_EventCount_Interrupt ( int Delayed_Cycles )
{
	LOG_TRACE(TRACE_VIDEO_HBL , "mfp/video timer B new event count %d, delay=%d\n" , MFP_TB_MAINCOUNTER-1 , Delayed_Cycles );

	if (MFP_TB_MAINCOUNTER == 1)			/* Timer expired? If so, generate interrupt */
	{
		MFP_TB_MAINCOUNTER = MFP_TBDR;		/* Reload timer from data register */

		/* Acknowledge in MFP circuit, pass bit,enable,pending */
		MFP_InputOnChannel ( MFP_INT_TIMER_B , Delayed_Cycles );
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
static int MFP_StartTimer_AB(Uint8 TimerControl, Uint16 TimerData, interrupt_id Handler,
                             bool bFirstTimer, bool *pTimerCanResume)
{
	int TimerClockCycles = 0;


	/* When in pulse width mode, handle as in delay mode */
	/* (this is not completely correct, as we should also handle GPIO 3/4 in pulse mode) */
	if ( TimerControl > 8 )
	{
		if (LOG_TRACE_LEVEL(TRACE_MFP_START))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("mfp start AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d pulse mode->delay mode\n",
			                Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
		}

		TimerControl &= 0x07;			/* clear bit 3, pulse width mode -> delay mode */
	}


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

		/* [NP] FIXME : Temporary fix for Lethal Xcess calibration routine to remove top border : */
		/* the routine expects that the delay is not always stable, there must be a small */
		/* jitter due to the clock difference between CPU and MFP */
		if ( ( M68000_GetPC() == 0x14d78 ) && ( STMemory_ReadLong ( 0x14d6c ) == 0x11faff75 ) )
		{
//			fprintf ( stderr , "mfp add jitter %d\n" , TimerClockCycles );
			TimerClockCycles += rand()%5-2;		/* add jitter for wod2 */
		}

		if (LOG_TRACE_LEVEL(TRACE_MFP_START))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("mfp start AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n",
			                Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
			                bFirstTimer?"true":"false", *pTimerCanResume?"true":"false");
		}

		/* And add to our internal interrupt list, if timer cycles is zero
		 * then timer is stopped */
		CycInt_RemovePendingInterrupt(Handler);
		if (TimerClockCycles)
		{
			if ((*pTimerCanResume == true) && (bFirstTimer == true))	/* we can't resume if the timer is auto restarting after an interrupt */
			{
				CycInt_ResumeStoppedInterrupt ( Handler );
			}
			else
			{
				int	AddCurCycles = INT_CONVERT_TO_INTERNAL ( CurrentInstrCycles + WaitStateCycles - 4 , INT_CPU_CYCLE );

				/* Start timer from now? If not continue timer using PendingCycleOver */
				if (bFirstTimer)
					CycInt_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, AddCurCycles);
				else
				{
					int	TimerClockCyclesInternal = INT_CONVERT_TO_INTERNAL ( TimerClockCycles , INT_MFP_CYCLE );

					/* In case we miss more than one int, we must correct the delay for the next one */
					if ( PendingCyclesOver > TimerClockCyclesInternal )
						PendingCyclesOver = PendingCyclesOver % TimerClockCyclesInternal;

					CycInt_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, -PendingCyclesOver);
				}

				*pTimerCanResume = true;		/* timer was set, resume is possible if stop/start it later */
			}
		}

		else	/* Ctrl was 0 -> timer is stopped */
		{
			/* do nothing, only print some traces */
			if (LOG_TRACE_LEVEL(TRACE_MFP_START))
			{
				int FrameCycles, HblCounterVideo, LineCycles;
				Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
				LOG_TRACE_PRINT("mfp stop AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n",
				                Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
				                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
				                bFirstTimer?"true":"false", *pTimerCanResume?"true":"false");
			}
		}
	}


	else if (TimerControl == 8 )				/* event count mode */
	{
		/* Make sure no outstanding interrupts in list if channel is disabled */
		CycInt_RemovePendingInterrupt(Handler);

		if ( Handler == INTERRUPT_MFP_TIMERB )		/* we're starting timer B event count mode */
		{
			/* Store start cycle for handling interrupt in video.c */
			TimerBEventCountCycleStart = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
		}

		if (LOG_TRACE_LEVEL(TRACE_MFP_START))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("mfp start AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n",
			                Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
			                bFirstTimer?"true":"false", *pTimerCanResume?"true":"false");
		}
	}

	return TimerClockCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer C or D
 */
static int MFP_StartTimer_CD(Uint8 TimerControl, Uint16 TimerData, interrupt_id Handler,
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

		if ( LOG_TRACE_LEVEL( TRACE_MFP_START ) )
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("mfp start CD handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n" ,
			                     Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                     FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
			                     bFirstTimer?"true":"false" , *pTimerCanResume?"true":"false" );
		}

		/* And add to our internal interrupt list, if timer cycles is zero
		 * then timer is stopped */
		CycInt_RemovePendingInterrupt(Handler);
		if (TimerClockCycles)
		{
			if ((*pTimerCanResume == true) && (bFirstTimer == true))	/* we can't resume if the timer is auto restarting after an interrupt */
			{
				CycInt_ResumeStoppedInterrupt ( Handler );
			}
			else
			{
				int	AddCurCycles = INT_CONVERT_TO_INTERNAL ( CurrentInstrCycles + WaitStateCycles - 4 , INT_CPU_CYCLE );

				/* Start timer from now? If not continue timer using PendingCycleOver */
				if (bFirstTimer)
					CycInt_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, AddCurCycles);
				else
				{
					int	TimerClockCyclesInternal = INT_CONVERT_TO_INTERNAL ( TimerClockCycles , INT_MFP_CYCLE );

					/* In case we miss more than one int, we must correct the delay for the next one */
					if ( PendingCyclesOver > TimerClockCyclesInternal )
						PendingCyclesOver = PendingCyclesOver % TimerClockCyclesInternal;

					CycInt_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, -PendingCyclesOver);
				}

				*pTimerCanResume = true;		/* timer was set, resume is possible if stop/start it later */
			}
		}
	}

	else	/* timer control is 0 */
	{
		if ( LOG_TRACE_LEVEL( TRACE_MFP_START ) )
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("mfp stop CD handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s resume=%s\n" ,
			                     Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                     FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
			                     bFirstTimer?"true":"false" , *pTimerCanResume?"true":"false" );
		}

		/* Make sure no outstanding interrupts in list if channel is disabled */
		CycInt_RemovePendingInterrupt(Handler);
	}

	return TimerClockCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer A or B - If in EventCount MainCounter already has correct value
 */
static Uint8 MFP_ReadTimer_AB(Uint8 TimerControl, Uint8 MainCounter, int TimerCycles, interrupt_id Handler, bool TimerIsStopping)
{
//	int TimerCyclesPassed;

	/* Find TimerAB count, if no interrupt or not in delay mode assume
	 * in Event Count mode so already up-to-date as kept by HBL */
	if (CycInt_InterruptActive(Handler) && (TimerControl > 0) && (TimerControl <= 7))
	{
		/* Find cycles passed since last interrupt */
		//TimerCyclesPassed = TimerCycles - CycInt_FindCyclesPassed ( Handler, INT_MFP_CYCLE );
		MainCounter = MFP_CYCLE_TO_REG ( CycInt_FindCyclesPassed ( Handler, INT_MFP_CYCLE ), TimerControl );
		//fprintf ( stderr , "mfp read AB passed %d count %d\n" , TimerCyclesPassed, MainCounter );
	}

	/* If the timer is stopped when the internal mfp data reg is already < 1 */
	/* then the data reg will be 0 (=256) next time the timer will be restarted */
	/* if no write is made to the data reg before */
	if ( TimerIsStopping )
	{
		if ( CycInt_FindCyclesPassed ( Handler, INT_MFP_CYCLE ) < MFP_REG_TO_CYCLES ( 1 , TimerControl ) )
		{
			MainCounter = 0;			/* internal mfp counter becomes 0 (=256) */
			LOG_TRACE(TRACE_MFP_READ , "mfp read AB handler=%d stopping timer while data reg between 1 and 0 : forcing data to 256\n" ,
					Handler );
		}
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp read AB handler=%d data=%d ctrl=%d timer_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
		                     Handler, MainCounter, TimerControl, TimerCycles,
		                     FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	return MainCounter;
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer C or D
 */
static Uint8 MFP_ReadTimerCD(Uint8 TimerControl, Uint8 TimerData, Uint8 MainCounter, int TimerCycles, interrupt_id Handler, bool TimerIsStopping)
{
//	int TimerCyclesPassed;

	/* Find TimerCD count. If timer is off, MainCounter already contains
	 * the latest value */
	if (CycInt_InterruptActive(Handler))
	{
		/* Find cycles passed since last interrupt */
		//TimerCyclesPassed = TimerCycles - CycInt_FindCyclesPassed ( Handler, INT_MFP_CYCLE );
		MainCounter = MFP_CYCLE_TO_REG ( CycInt_FindCyclesPassed ( Handler, INT_MFP_CYCLE ), TimerControl);
		//fprintf ( stderr , "mfp read CD passed %d count %d\n" , TimerCyclesPassed, MainCounter );
	}

	/* If the timer is stopped when the internal mfp data reg is already < 1 */
	/* then the data reg will be 0 (=256) next time the timer will be restarted */
	/* if no write is made to the data reg before */
	if ( TimerIsStopping )
	{
		if ( CycInt_FindCyclesPassed ( Handler, INT_MFP_CYCLE ) < MFP_REG_TO_CYCLES ( 1 , TimerControl ) )
		{
			MainCounter = 0;			/* internal mfp counter becomes 0 (=256) */
			LOG_TRACE(TRACE_MFP_READ , "mfp read CD handler=%d stopping timer while data reg between 1 and 0 : forcing data to 256\n" ,
					Handler );
		}
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp read CD handler=%d data=%d ctrl=%d timer_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
		                     Handler, MainCounter, TimerControl, TimerCycles,
		                     FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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
	                                      INTERRUPT_MFP_TIMERA, true, &TimerACanResume);
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
	                                      INTERRUPT_MFP_TIMERB, true, &TimerBCanResume);
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
	                                      INTERRUPT_MFP_TIMERC , true, &TimerCCanResume);
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
	                                      INTERRUPT_MFP_TIMERD, true, &TimerDCanResume);
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
	CycInt_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit,enable,pending */
	if ((MFP_TACR&0xf) != 0)            /* Is timer OK? */
		MFP_InputOnChannel ( MFP_INT_TIMER_A , INT_CONVERT_FROM_INTERNAL ( PendingCyclesOver , INT_CPU_CYCLE ) );

	/* Start next interrupt, if need one - from current cycle count */
	TimerAClockCycles = MFP_StartTimer_AB(MFP_TACR, MFP_TADR, INTERRUPT_MFP_TIMERA, false, &TimerACanResume);
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
	CycInt_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	if ((MFP_TBCR&0xf) != 0)            /* Is timer OK? */
		MFP_InputOnChannel ( MFP_INT_TIMER_B , INT_CONVERT_FROM_INTERNAL ( PendingCyclesOver , INT_CPU_CYCLE ) );

	/* Start next interrupt, if need one - from current cycle count */
	TimerBClockCycles = MFP_StartTimer_AB(MFP_TBCR, MFP_TBDR, INTERRUPT_MFP_TIMERB, false, &TimerBCanResume);
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
	CycInt_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	if ((MFP_TCDCR&0x70) != 0)          /* Is timer OK? */
		MFP_InputOnChannel ( MFP_INT_TIMER_C , INT_CONVERT_FROM_INTERNAL ( PendingCyclesOver , INT_CPU_CYCLE ) );

	/* Start next interrupt, if need one - from current cycle count */
	TimerCClockCycles = MFP_StartTimer_CD((MFP_TCDCR>>4)&7, MFP_TCDR, INTERRUPT_MFP_TIMERC, false, &TimerCCanResume);
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
	CycInt_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	if ((MFP_TCDCR&0x07) != 0)          /* Is timer OK? */
		MFP_InputOnChannel ( MFP_INT_TIMER_D , INT_CONVERT_FROM_INTERNAL ( PendingCyclesOver , INT_CPU_CYCLE ) );

	/* Start next interrupt, if need one - from current cycle count */
	TimerDClockCycles = MFP_StartTimer_CD(MFP_TCDCR&7, MFP_TDDR, INTERRUPT_MFP_TIMERD, false, &TimerDCanResume);
}



/*-----------------------------------------------------------------------*/
/**
 * Handle read from GPIP pins register (0xfffa01).
 *
 * - Bit 0 is the BUSY signal of the printer port, it is SET if no printer
 *   is connected or on BUSY. Therefore we should assume it to be 0 in Hatari
 *   when a printer is emulated.
 * - Bit 1 is used for RS232: DCD
 * - Bit 2 is used for RS232: CTS
 * - Bit 3 is used by the blitter (busy/idle state)
 * - Bit 4 is used by the ACIAs (keyboard and midi)
 * - Bit 5 is used by the FDC / HDC
 * - Bit 6 is used for RS232: RI
 * - Bit 7 is monochrome monitor detection signal. On STE it is also XORed with
 *   the DMA sound play bit.
 *
 * When reading GPIP, output lines (DDR=1) should return the last value that was written,
 * only input lines (DDR=0) should be updated.
 */
void MFP_GPIP_ReadByte(void)
{
	Uint8	gpip_new;

	M68000_WaitState(4);

	gpip_new = MFP_GPIP;

	if (!bUseHighRes)
		gpip_new |= 0x80;	/* Color monitor -> set top bit */
	else
		gpip_new &= ~0x80;
	
	if (nDmaSoundControl & DMASNDCTRL_PLAY)
		gpip_new ^= 0x80;	/* Top bit is XORed with DMA sound control play bit (Ste/TT emulation mode)*/
	if (nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_PLAY || nCbar_DmaSoundControl & CROSSBAR_SNDCTRL_RECORD)
		gpip_new ^= 0x80;	/* Top bit is XORed with Falcon crossbar DMA sound control play bit (Falcon emulation mode) */

	if (ConfigureParams.Printer.bEnablePrinting)
	{
		/* Signal that printer is not busy */
		gpip_new &= ~1;
	}
	else
	{
		gpip_new |= 1;

		/* Printer BUSY bit is also used by parallel port joystick adapters as fire button */
		if (ConfigureParams.Joysticks.Joy[JOYID_PARPORT1].nJoystickMode != JOYSTICK_DISABLED)
		{
			/* Fire pressed? */
			if (Joy_GetStickData(JOYID_PARPORT1) & 0x80)
				gpip_new &= ~1;
		}
	}

	gpip_new &= ~MFP_DDR;					/* New input bits */

	MFP_GPIP = ( MFP_GPIP & MFP_DDR ) | gpip_new; 		/* Keep output bits unchanged and update input bits */

	IoMem[0xfffa01] = MFP_GPIP;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp read gpip fa01=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_GPIP, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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
 * Handle read from interrupt enable register A (0xfffa07).
 */
void MFP_EnableA_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa07] = MFP_IERA;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interrupt enable register B (0xfffa09).
 */
void MFP_EnableB_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa09] = MFP_IERB;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interrupt pending register A (0xfffa0b).
 */
void MFP_PendingA_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa0b] = MFP_IPRA;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interrupt pending register A (0xfffa0d).
 */
void MFP_PendingB_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa0d] = MFP_IPRB;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interrupt in service register A (0xfffa0f).
 */
void MFP_InServiceA_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa0f] = MFP_ISRA;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interrupt in service register B (0xfffa11).
 */
void MFP_InServiceB_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa11] = MFP_ISRB;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interrupt mask register A (0xfffa13).
 */
void MFP_MaskA_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xfffa13] = MFP_IMRA;
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from interrupt mask register B (0xfffa15).
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
		MFP_ReadTimerA(false);		/* Stores result in 'MFP_TA_MAINCOUNTER' */

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

	/* Is it event count mode or not? */
	if (MFP_TBCR != 8)
	{
		/* Not event count mode, so handle as normal timer
		 * and store result in 'MFP_TB_MAINCOUNTER' */
		MFP_ReadTimerB(false);
	}
	else if (bUseVDIRes)
	{
		/* HBLs are disabled in VDI mode, but TOS expects to read a 1. */
		MFP_TB_MAINCOUNTER = 1;
	}
	/* Special case when reading $fffa21, we need to test if the current read instruction */
	/* overlaps the horizontal video position where $fffa21 is changed */
	else
	{
		int FrameCycles, HblCounterVideo;
		int pos_start , pos_read;

		/* Cycle position of the start of the current instruction */
		//pos_start = nFrameCycles % nCyclesPerLine;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &pos_start );
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
			LOG_TRACE(TRACE_MFP_READ , "mfp read TB overlaps pos_start=%d TB_pos=%d pos_read=%d nHBL=%d \n",
					pos_start, LineTimerBCycle, pos_read , HblCounterVideo );

			TB_count--;
			if ( TB_count == 0 )			/* going from 1 to 0 : timer restart, reload data reg */
				TB_count = MFP_TBDR;
			/* Going from 0 to -1 : data reg is in fact going from 256 to 255. As TB_count is Uint8, */
			/* this is already what we get when we decrement TB_count=0. So, the next 2 lines are redundant. */
/*			else if ( TB_count < 0 )
				TB_count = 255;
*/
		}

		LOG_TRACE(TRACE_MFP_READ , "mfp read TB data=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
					TB_count, FrameCycles, pos_start, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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

	MFP_ReadTimerC(false);		/* Stores result in 'MFP_TC_MAINCOUNTER' */

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
		MFP_ReadTimerD(false);	/* Stores result in 'MFP_TD_MAINCOUNTER' */
		IoMem[0xfffa25] = MFP_TD_MAINCOUNTER;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle write to GPIP register (0xfffa01).
 *
 * Only line configured as ouput in DDR can be changed (0=input 1=output)
 * When reading GPIP, output lines should return the last value that was written,
 * only input lines should be updated.
 */
void MFP_GPIP_WriteByte(void)
{
	Uint8	GPIP_new;
	Uint8	GPIP_old = MFP_GPIP;

	M68000_WaitState(4);

	GPIP_new = IoMem[0xfffa01] & MFP_DDR;			/* New output bits */

	MFP_GPIP = ( MFP_GPIP & ~MFP_DDR ) | GPIP_new;		/* Keep input bits unchanged and update output bits */

	/* Update possible interrupts after changing GPIP */
	MFP_GPIP_Update_Interrupt ( GPIP_old , MFP_GPIP , MFP_AER , MFP_AER , MFP_DDR , MFP_DDR );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to AER (0xfffa03)
 *
 * Special case for bit 3 :
 * Bit 3 of AER is linked to timer B in event count mode.
 *  - If bit 3=0, timer B triggers on end of line when display goes off.
 *  - If bit 3=1, timer B triggers on start of line when display goes on.
 */
void MFP_ActiveEdge_WriteByte(void)
{
	Uint8	AER_old;

	M68000_WaitState(4);

	AER_old = MFP_AER;
	MFP_AER = IoMem[0xfffa03];

	/* Update possible interrupts after changing AER */
	MFP_GPIP_Update_Interrupt ( MFP_GPIP , MFP_GPIP , AER_old , MFP_AER , MFP_DDR , MFP_DDR );


	/* Special case when changing bit 3 : we need to update the position of the timer B interrupt for 'event count' mode */
	if ( ( AER_old & ( 1 << 3 ) ) != ( MFP_AER & ( 1 << 3 ) ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		int LineTimerBCycle_old = LineTimerBCycle;

		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

		/* 0 -> 1, timer B is now counting start of line events (cycle 56+28) */
		if ( ( AER_old & ( 1 << 3 ) ) == 0 )
		{
			LineTimerBCycle = Video_TimerB_GetPos ( HblCounterVideo );

			LOG_TRACE((TRACE_VIDEO_HBL | TRACE_MFP_WRITE),
					"mfp/video AER bit 3 0->1, timer B triggers on start of line,"
					" old_pos=%d new_pos=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n",
					LineTimerBCycle_old, LineTimerBCycle,
					FrameCycles, LineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles);
		}

		/* 1 -> 0, timer B is now counting end of line events (cycle 376+28) */
		else if ( ( AER_old & ( 1 << 3 ) ) != 0 )
		{
			LineTimerBCycle = Video_TimerB_GetPos ( HblCounterVideo );

			LOG_TRACE((TRACE_VIDEO_HBL | TRACE_MFP_WRITE),
					"mfp/video AER bit 3 1->0, timer B triggers on end of line,"
					" old_pos=%d new_pos=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n",
					LineTimerBCycle_old, LineTimerBCycle,
					FrameCycles, LineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles);
		}

		/* Timer B position changed, update the next interrupt */
		if ( LineTimerBCycle_old != LineTimerBCycle )
			Video_AddInterruptTimerB ( LineTimerBCycle );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle write to data direction register (0xfffa05).
 */
void MFP_DataDirection_WriteByte(void)
{
	Uint8	DDR_old;

	M68000_WaitState(4);

	DDR_old = MFP_DDR;
	MFP_DDR = IoMem[0xfffa05];

	/* Update possible interrupts after changing AER */
	MFP_GPIP_Update_Interrupt ( MFP_GPIP , MFP_GPIP , MFP_AER , MFP_AER , DDR_old , MFP_DDR );
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
	MFP_UpdateIRQ ( Cycles_GetClockCounterOnWriteAccess() );
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
	MFP_UpdateIRQ ( Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt pending register A (0xfffa0b).
 */
void MFP_PendingA_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IPRA &= IoMem[0xfffa0b];				/* Cannot set pending bits - only clear via software */
	MFP_UpdateIRQ ( Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt pending register B (0xfffa0d).
 */
void MFP_PendingB_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IPRB &= IoMem[0xfffa0d];				/* Cannot set pending bits - only clear via software */
	MFP_UpdateIRQ ( Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt in service register A (0xfffa0f).
 */
void MFP_InServiceA_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_ISRA &= IoMem[0xfffa0f];        			/* Cannot set in-service bits - only clear via software */
	MFP_UpdateIRQ ( Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt in service register B (0xfffa11).
 */
void MFP_InServiceB_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_ISRB &= IoMem[0xfffa11];        			/* Cannot set in-service bits - only clear via software */
	MFP_UpdateIRQ ( Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt mask register A (0xfffa13).
 */
void MFP_MaskA_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IMRA = IoMem[0xfffa13];
	MFP_UpdateIRQ ( Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to interrupt mask register B (0xfffa15).
 */
void MFP_MaskB_WriteByte(void)
{
	M68000_WaitState(4);

	MFP_IMRB = IoMem[0xfffa15];
	MFP_UpdateIRQ ( Cycles_GetClockCounterOnWriteAccess() );
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
			MFP_UpdateIRQ ( Cycles_GetClockCounterOnWriteAccess() );
		}
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp write vector reg fa17=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_VR, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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
			MFP_ReadTimerA(true);	/* Store result in 'MFP_TA_MAINCOUNTER' */

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
			MFP_ReadTimerB(true);	/* Store result in 'MFP_TB_MAINCOUNTER' */

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
			MFP_ReadTimerC(true);		/* Store result in 'MFP_TC_MAINCOUNTER' */

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
				bAppliedTimerDPatch = true;
			}
		}

		/* If we stop a timer which was in delay mode, we need to store the current value */
		/* of the counter to be able to read it or to continue from where we left if the timer is */
		/* restarted later without writing to the data register. */
		if ((new_tcdcr & 0x07) == 0)
			MFP_ReadTimerD(true);	/* Stores result in 'MFP_TD_MAINCOUNTER' */

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
		TimerACanResume = false;        /* we need to set a new int when timer start */
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp write data reg A fa1f=0x%x new counter=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_TADR, MFP_TA_MAINCOUNTER, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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
		TimerBCanResume = false;        /* we need to set a new int when timer start */
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp write data reg B fa21=0x%x new counter=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_TBDR, MFP_TB_MAINCOUNTER, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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
		TimerCCanResume = false;        /* we need to set a new int when timer start */
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp write data reg C fa23=0x%x new counter=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_TCDR, MFP_TC_MAINCOUNTER, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
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
		TimerDCanResume = false;        /* we need to set a new int when timer start */
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp write data reg D fa25=0x%x new counter=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			MFP_TDDR, MFP_TD_MAINCOUNTER, FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}
