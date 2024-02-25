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
/*			reg for timer A/B is uint8_t (revert 2008/10/04 changes).		*/
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
/* 2015/02/27	[NP]	Better support for GPIP/AER/DDR and triggering an interrupt	*/
/*			when AER is changed (fix the MIDI programs  Realtime and M	*/
/*			by Eric Ameres, which toggle bit 0 in AER).			*/
/* 2015/04/08	[NP]	When an interrupt happens on timers A/B/C/D, take into account	*/
/*			PendingCyclesOver to determine if a 4 cycle delay should be	*/
/*			added or not (depending on when it happened during the CPU	*/
/*			instruction).							*/
/* 2022/01/07	[NP]	Improve accuracy when reading Timer Data reg and don't use	*/
/*			CycInt_ResumeStoppedInterrupt() anymore (fix ST CNX screen	*/
/*			in Punish Your Machine when saving MFP registers by doing very	*/
/*			fast start/stop on each MFP timer)				*/
/* 2022/01/27	[NP]	Call MFP_UpdateTimers / CycInt_Process before accessing any MFP	*/
/*			registers, to ensure MFP timers are updated in chronological	*/
/*			order (fix the game Super Hang On, where 'bclr #0,$fffffa0f'	*/
/*			to clear Timer B ISR sometimes happens at the same time that	*/
/*			Timer C expires, which used the wrong ISR value and gave	*/
/*			flickering raster colors)					*/


const char MFP_fileid[] = "Hatari mfp.c";

#include <stdint.h>		/* Needed for UINT64_MAX */
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
#include "cycles.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "psg.h"
#include "rs232.h"
#include "sound.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"
#include "video.h"
#include "ncr5380.h"
#include "clocks_timings.h"
#include "acia.h"
#include "utils.h"


/*
  MC68901 MultiFuncion Peripheral (MFP)

  References :
   - MC68901 datasheet by Motorola
   - Boards schematics for ST/STE/MegaST/MegaSTE/TT/Falcon


  Main MFP : 48 Pin version for STF/megaSTF/STe/megaSTE
  -----------------------------------------------------

                                                 -----------
                                            R/W -| 1    48 |- CS(INV)
                                         RS1/A1 -| 2    47 |- DS(INV)
                                         RS2/A2 -| 3    46 |- DTACK(INV)
                                         RS3/A3 -| 4    45 |- IACK(INV)
                                         RS4/A4 -| 5    44 |- D7
                                         RS5/A5 -| 6    43 |- D6
                          TC : connected to TDO -| 7    42 |- D5
              SO : connected to Send on RS-232C -| 8    41 |- D4
           SI : connected to Receive on RS-232C -| 9    40 |- D3
                          RC : connected to TDO -| 10   39 |- D2
                                            VCC -| 11   38 |- D1
                                  not connected -| 12   37 |- D0
                            TAO : not connected -| 13   36 |- GND
                            TBO : not connected -| 14   35 |- CLK : connected to 4 MHz
                            TCO : not connected -| 15   34 |- IEI(INV) : on TT connected to IEO on TT MFP, else connected to GND
                   TDO : connected to TC and RC -| 16   33 |- IEO(INV) : not connected
                          XTAL2 : not connected -| 17   32 |- IRQ(INV)
                XTAL1 : connected to 2.4576 MHz -| 18   31 |- RR(INV) : not connected
  TAI : ST connected to IO0, else DMA SOUND INT -| 19   30 |- TR(INV) : not connected
                    TBI : connected to video DE -| 20   29 |- I7 : GPIP 7, see note below
                                     RESET(INV) -| 21   28 |- I6 : GPIP 6, see note below
                    I0 : GPIP 0, see note below -| 22   27 |- I5 : GPIP 5, see note below
                    I1 : GPIP 1, see note below -| 23   26 |- I4 : GPIP 4, see note below
                    I2 : GPIP 2, see note below -| 24   25 |- I3 : GPIP 3, see note below
                                                 -----------


  Main MFP : 52 Pin version for TT/Falcon
  ---------------------------------------

                                                 -----------
                                  not connected -| 1    52 |- CS(INV)
                                            R/W -| 2    51 |- DS(INV)
                                         RS1/A1 -| 3    50 |- DTACK(INV
                                         RS2/A2 -| 4    49 |- IACK(INV
                                         RS3/A3 -| 5    48 |- D7
                                         RS4/A4 -| 6    47 |- D6
                                         RS5/A5 -| 7    46 |- D5
                          TC : connected to TDO -| 8    45 |- D4
          SO : connected to Transmit on RS-232C -| 9    44 |- D3
           SI : connected to Receive on RS-232C -| 10   43 |- D2
                          RC : connected to TDO -| 11   42 |- D1
                                            VCC -| 12   41 |- D0
                                  not connected -| 13   40 |- GND
                                  not connected -| 14   39 |- CLK : connected to 4 MHz
                            TAO : not connected -| 15   38 |- IEI(INV) : on TT connected to IEO on TT MFP, else connected to GND
                            TBO : not connected -| 16   37 |- IEO(INV) : not connected
                            TCO : not connected -| 17   36 |- IRQ(INV)
                   TDO : connected to TC and RC -| 18   35 |- RR(INV) : not connected
                          XTAL2 : not connected -| 19   34 |- TR(INV) : not connected
                XTAL1 : connected to 2.4576 MHz -| 20   33 |- not connected
                                  not connected -| 21   32 |- I7 : GPIP 7, see note below
  TAI : ST connected to IO0, else DMA SOUND INT -| 22   31 |- I6 : GPIP 6, see note below
                    TBI : connected to video DE -| 23   30 |- I5 : GPIP 5, see note below
                                     RESET(INV) -| 24   29 |- I4 : GPIP 4, see note below
                    I0 : GPIP 0, see note below -| 25   28 |- I3 : GPIP 3, see note below
                    I1 : GPIP 1, see note below -| 26   27 |- I2 : GPIP 2, see note below
                                                 -----------


  TT 2nd MFP : 52 Pin version
  ---------------------------

                                                 -----------
                                  not connected -| 1    52 |- CS(INV)
                                            R/W -| 2    51 |- DS(INV)
                                         RS1/A1 -| 3    50 |- DTACK(INV
                                         RS2/A2 -| 4    49 |- IACK(INV
                                         RS3/A3 -| 5    48 |- D7
                                         RS4/A4 -| 6    47 |- D6
                                         RS5/A5 -| 7    46 |- D5
                          TC : connected to TDO -| 8    45 |- D4
    SO : connected to Transmit on Serial Port D -| 9    44 |- D3
     SI : connected to Receive on Serial Port D -| 10   43 |- D2
                          RC : connected to TDO -| 11   42 |- D1
                                            VCC -| 12   41 |- D0
                                  not connected -| 13   40 |- GND
                                  not connected -| 14   39 |- CLK : connected to 4 MHz
                            TAO : not connected -| 15   38 |- IEI(INV) : connected to GND
                            TBO : not connected -| 16   37 |- IEO(INV) : connected to IEI on Main MFP
                      TCO : connected to TCCLKX -| 17   36 |- IRQ(INV) : connected to IRQ on Main MFP
                   TDO : connected to TC and RC -| 18   35 |- RR(INV) : not connected
                          XTAL2 : not connected -| 19   34 |- TR(INV) : not connected
                XTAL1 : connected to 2.4576 MHz -| 20   33 |- not connected
                                  not connected -| 21   32 |- I7 : GPIP 7, see note below
                       TAI : connected to GND ? -| 22   31 |- I6 : GPIP 6, see note below
                    TBI : connected to video DE -| 23   30 |- I5 : GPIP 5, see note below
                                     RESET(INV) -| 24   29 |- I4 : GPIP 4, see note below
                    I0 : GPIP 0, see note below -| 25   28 |- I3 : GPIP 3, see note below
                    I1 : GPIP 1, see note below -| 26   27 |- I2 : GPIP 2, see note below
                                                 -----------



  MFP interrupt channel circuit:

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

  - The TT uses 2 MFPs which are daisy chained using IEI and IEO signals (as described in the 68901's documentation)
    In that case, the TT's specific MFP (accessible between $FFFA81 and $FFFAAF) has the highest priority
    and the "normal" MFP (accessible between $FFFA01 and $FFFA2F) has the lowest priority

  - Each MFP has 8 GPIP bits used to connect signals from external devices/ports :

    Main MFP :
      0 : parallel port, busy signal (0=not busy, 1=busy)
      1 : rs232 port, data carrier detect (DCD) signal
      2 : rs232 port, clear to send (CTS) signal
      3 : STF/STE/TT : blitter, active low (0=IRQ set, 1=IRQ not set)
          Falcon : DSP, active low (0=HREQ set, 1=HREQ not set)
      4 : ACIAs 6850 (ikbd and midi), active low (0=IRQ set for ACIA 1 and/or 2, 1=IRQ not set for neither ACIA 1 nor 2)
      5 : FDC/HD, active low (0=IRQ set, 1=IRQ not set)
          STF/STE/megaSTE/TT : FDC/ACSI (0=IRQ set for FDC and/or ACSI, 1=IRQ not set for FDC nor ACSI)
          Falcon : FDC/IDE/SCSI (0=IRQ set for FDC and/or IDE and/or SCSI, 1=IRQ not set for FDC nor IDE nor SCSI)
      6 : rs232 port, ring indicator (RI) signal
      7 : monochrome monitor detect (0=monochrome, 1=color) and/or dma sound (0=idle, 1=play)
          STF : monochrome monitor detect (0=monochrome, 1=color)
          STE/TT : monochrome monitor detect XOR dma sound play
          Falcon : dma sound play/record (0=idle, 1=play/record)

    TT MFP :
      0 : connected to external I/O pin
      1 : connected to external I/O pin
      2 : SCC DMA controller, active low (0=IRQ set, 1=IRQ not set)
      3 : SCC serial port B, ring indicator (RI) signal
      4 : Internal floppy drive pin 34, NOT disk change (DC) signal (0=inserted 1=ejected)
      5 : SCSI DMA controller, active low (0=IRQ set, 1=IRQ not set)
      6 : RTC MC146818A, active low (0=IRQ set, 1=IRQ not set)
      7 : SCSI NCR5380, active *high* (1=IRQ set, 0=IRQ not set)

*/

/*-----------------------------------------------------------------------*/


#define TRACE_MFP                (1ll<<55)



MFP_STRUCT		MFP_Array[ MFP_MAX_NB ];
MFP_STRUCT		*pMFP_Main;
MFP_STRUCT		*pMFP_TT;


#define	PATCH_TIMER_TDDR_FAKE		0x64		/* TDDR value to slow down timer D */


static int PendingCyclesOver = 0;   			/* >= 0 value, used to "loop" a timer when data counter reaches 0 */


bool		MFP_UpdateNeeded = false;		/* When set to true, main CPU loop should call MFP_UpdateIRQ() */

#define	MFP_IRQ_DELAY_TO_CPU		4		/* When MFP_IRQ is set, it takes 4 CPU cycles before it's visible to the CPU */


static const uint16_t MFPDiv[] =
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

static void	MFP_Init_Pointers ( MFP_STRUCT *pAllMFP );
static void	MFP_Reset ( MFP_STRUCT *pMFP );

static uint8_t	MFP_ConvertIntNumber ( MFP_STRUCT *pMFP , int16_t Interrupt , uint8_t **pMFP_IER , uint8_t **pMFP_IPR , uint8_t **pMFP_ISR , uint8_t **pMFP_IMR );
static void	MFP_UpdateTimers ( MFP_STRUCT *pMFP , uint64_t Clock );
static void	MFP_Exception ( MFP_STRUCT *pMFP , int16_t Interrupt );
static bool	MFP_ProcessIRQ ( MFP_STRUCT *pMFP );
static void	MFP_UpdateIRQ ( MFP_STRUCT *pMFP , uint64_t Event_Time );
static bool	MFP_InterruptRequest ( MFP_STRUCT *pMFP , int Int , uint8_t Bit , uint8_t IPRx , uint8_t IMRx , uint8_t PriorityMaskA , uint8_t PriorityMaskB );
static int	MFP_CheckPendingInterrupts ( MFP_STRUCT *pMFP );
static void	MFP_GPIP_Update_Interrupt ( MFP_STRUCT *pMFP , uint8_t GPIP_old , uint8_t GPIP_new , uint8_t AER_old , uint8_t AER_new , uint8_t DDR_old , uint8_t DDR_new );

static uint8_t	MFP_Main_Compute_GPIP7 ( void );
static uint8_t	MFP_Main_Compute_GPIP_LINE_ACIA ( void );
static void	MFP_GPIP_ReadByte_Main ( MFP_STRUCT *pMFP );
static void	MFP_GPIP_ReadByte_TT ( MFP_STRUCT *pMFP );



/*-----------------------------------------------------------------------*/
/**
 * Convert a number of CPU cycles running at CPU_Freq_Emul to a number of
 * MFP timer cycles running at MFP_Timer_Freq (XTAL)
 */
int	MFP_ConvertCycle_CPU_MFP_TIMER ( int CPU_Cycles )
{
	int	MFP_Cycles;

	MFP_Cycles = (int)( ( (uint64_t)CPU_Cycles * MachineClocks.MFP_Timer_Freq ) / MachineClocks.CPU_Freq_Emul );
	return MFP_Cycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Convert a number of MFP timer cycles running at MFP_Timer_Freq (XTAL)
 * to a number of CPU cycles running at CPU_Freq_Emul
 */
int	MFP_ConvertCycle_MFP_TIMER_CPU ( int MFP_Cycles )
{
	int	CPU_Cycles;

	CPU_Cycles = (int)( ( (uint64_t)MFP_Cycles * MachineClocks.CPU_Freq_Emul ) / MachineClocks.MFP_Timer_Freq );
	return CPU_Cycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Init the 2 MFPs ; the 2nd MFP is only used in TT mode
 * This is called only once, when the emulator starts.
 */
void	MFP_Init ( MFP_STRUCT *pAllMFP )
{
	int	i;


	LOG_TRACE ( TRACE_MFP , "mfp init\n" );

	for ( i=0 ; i<MFP_MAX_NB ; i++ )
	{
		memset ( (void *)&(pAllMFP[ i ]) , 0 , sizeof ( MFP_STRUCT ) );
	}

	/* Set the default common callback functions + other pointers */
	MFP_Init_Pointers ( pAllMFP );
}



/*-----------------------------------------------------------------------*/
/**
 * Init some functions/memory pointers for each MFP.
 * This is called at Init and when restoring a memory snapshot.
 */
static void	MFP_Init_Pointers ( MFP_STRUCT *pAllMFP )
{
	int	i;


	for ( i=0 ; i<MFP_MAX_NB ; i++ )
	{
		/* Set the default common callback functions */
	}

	strcpy ( pAllMFP[ 0 ].NameSuffix , "" );	/* No suffix for main MFP */
	strcpy ( pAllMFP[ 1 ].NameSuffix , "_tt" );

	pMFP_Main = &(pAllMFP[ 0 ]);
	pMFP_TT = &(pAllMFP[ 1 ]);
}




/*-----------------------------------------------------------------------*/
/**
 * Reset all MFP variables and start interrupts on their way!
 */
void	MFP_Reset_All ( void )
{
	int	i;

	for ( i=0 ; i<MFP_MAX_NB ; i++ )
	{
		MFP_Reset ( &(MFP_Array[ i ]) );
	}

}



static void	MFP_Reset ( MFP_STRUCT *pMFP )
{
	int	i;

	pMFP->GPIP = 0;
	pMFP->AER = 0;
	pMFP->DDR = 0;
	pMFP->IERA = 0;
	pMFP->IERB = 0;
	pMFP->IPRA = 0;
	pMFP->IPRB = 0;
	pMFP->ISRA = 0;
	pMFP->ISRB = 0;
	pMFP->IMRA = 0;
	pMFP->IMRB = 0;
	pMFP->VR = 0;
	pMFP->TACR = 0;
	pMFP->TBCR = 0;
	pMFP->TCDCR = 0;
	pMFP->TADR = 0;
	pMFP->TBDR = 0;
	pMFP->TCDR = 0;
	pMFP->TDDR = 0;

	pMFP->TA_MAINCOUNTER = 0;
	pMFP->TB_MAINCOUNTER = 0;
	pMFP->TC_MAINCOUNTER = 0;
	pMFP->TD_MAINCOUNTER = 0;

	/* Clear counters */
	// TODO drop those 4 variables, as they are not really used in MFP_ReadTimer_xx
	pMFP->TimerAClockCycles = 0;
	pMFP->TimerBClockCycles = 0;
	pMFP->TimerCClockCycles = 0;
	pMFP->TimerDClockCycles = 0;

	pMFP->PatchTimerD_Done = 0;

	/* Clear input on timers A and B */
	pMFP->TAI = 0;
	pMFP->TBI = 0;

	/* Clear IRQ */
	pMFP->Current_Interrupt = -1;
	pMFP->IRQ = 0;
	pMFP->IRQ_CPU = 0;
	pMFP->IRQ_Time = 0;
	pMFP->Pending_Time_Min = UINT64_MAX;
	for ( i=0 ; i<=MFP_INT_MAX ; i++ )
		pMFP->Pending_Time[ i ] = UINT64_MAX;
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void	MFP_MemorySnapShot_Capture ( bool bSave )
{
	MFP_STRUCT	*pMFP;
	int		i;
	int		n;

	MemorySnapShot_Store(&MFP_UpdateNeeded, sizeof(MFP_UpdateNeeded));

	/* Save/Restore each MFP */
	for ( n=0 ; n<MFP_MAX_NB ; n++ )
	{
		pMFP = &(MFP_Array[ n ]);

		MemorySnapShot_Store(&(pMFP->GPIP), sizeof(pMFP->GPIP));
		MemorySnapShot_Store(&(pMFP->AER), sizeof(pMFP->AER));
		MemorySnapShot_Store(&(pMFP->DDR), sizeof(pMFP->DDR));
		MemorySnapShot_Store(&(pMFP->IERA), sizeof(pMFP->IERA));
		MemorySnapShot_Store(&(pMFP->IERB), sizeof(pMFP->IERB));
		MemorySnapShot_Store(&(pMFP->IPRA), sizeof(pMFP->IPRA));
		MemorySnapShot_Store(&(pMFP->IPRB), sizeof(pMFP->IPRB));
		MemorySnapShot_Store(&(pMFP->ISRA), sizeof(pMFP->ISRA));
		MemorySnapShot_Store(&(pMFP->ISRB), sizeof(pMFP->ISRB));
		MemorySnapShot_Store(&(pMFP->IMRA), sizeof(pMFP->IMRA));
		MemorySnapShot_Store(&(pMFP->IMRB), sizeof(pMFP->IMRB));
		MemorySnapShot_Store(&(pMFP->VR), sizeof(pMFP->VR));
		MemorySnapShot_Store(&(pMFP->TACR), sizeof(pMFP->TACR));
		MemorySnapShot_Store(&(pMFP->TBCR), sizeof(pMFP->TBCR));
		MemorySnapShot_Store(&(pMFP->TCDCR), sizeof(pMFP->TCDCR));
		MemorySnapShot_Store(&(pMFP->TADR), sizeof(pMFP->TADR));
		MemorySnapShot_Store(&(pMFP->TBDR), sizeof(pMFP->TBDR));
		MemorySnapShot_Store(&(pMFP->TCDR), sizeof(pMFP->TCDR));
		MemorySnapShot_Store(&(pMFP->TDDR), sizeof(pMFP->TDDR));

		MemorySnapShot_Store(&(pMFP->TA_MAINCOUNTER), sizeof(pMFP->TA_MAINCOUNTER));
		MemorySnapShot_Store(&(pMFP->TB_MAINCOUNTER), sizeof(pMFP->TB_MAINCOUNTER));
		MemorySnapShot_Store(&(pMFP->TC_MAINCOUNTER), sizeof(pMFP->TC_MAINCOUNTER));
		MemorySnapShot_Store(&(pMFP->TD_MAINCOUNTER), sizeof(pMFP->TD_MAINCOUNTER));

		MemorySnapShot_Store(&(pMFP->TimerAClockCycles), sizeof(pMFP->TimerAClockCycles));
		MemorySnapShot_Store(&(pMFP->TimerBClockCycles), sizeof(pMFP->TimerBClockCycles));
		MemorySnapShot_Store(&(pMFP->TimerCClockCycles), sizeof(pMFP->TimerCClockCycles));
		MemorySnapShot_Store(&(pMFP->TimerDClockCycles), sizeof(pMFP->TimerDClockCycles));

		MemorySnapShot_Store(&(pMFP->PatchTimerD_Done), sizeof(pMFP->PatchTimerD_Done));
		MemorySnapShot_Store(&(pMFP->PatchTimerD_TDDR_old), sizeof(pMFP->PatchTimerD_TDDR_old));

		MemorySnapShot_Store(&(pMFP->Current_Interrupt), sizeof(pMFP->Current_Interrupt));
		MemorySnapShot_Store(&(pMFP->IRQ), sizeof(pMFP->IRQ));
		MemorySnapShot_Store(&(pMFP->IRQ_CPU), sizeof(pMFP->IRQ_CPU));
		MemorySnapShot_Store(&(pMFP->IRQ_Time), sizeof(pMFP->IRQ_Time));
		MemorySnapShot_Store(&(pMFP->Pending_Time_Min), sizeof(pMFP->Pending_Time_Min));
		MemorySnapShot_Store(&PendingCyclesOver, sizeof(PendingCyclesOver));
		for ( i=0 ; i<=MFP_INT_MAX ; i++ )
			MemorySnapShot_Store(&(pMFP->Pending_Time[ i ]), sizeof(pMFP->Pending_Time[ i ]));
	}

	if ( !bSave )					/* If restoring */
		MFP_Init_Pointers ( MFP_Array );	/* Restore pointers */
}



/*-----------------------------------------------------------------------*/
/**
 * Given an MFP interrupt number, return a pointer to the corresponding
 * registers handling this interrupt, as well as the binary value
 * to set/clear these registers.
 * If an input pointer is NULL, we don't return the corresponding register.
 */
static uint8_t	MFP_ConvertIntNumber ( MFP_STRUCT *pMFP , int16_t Interrupt , uint8_t **pMFP_IER , uint8_t **pMFP_IPR , uint8_t **pMFP_ISR , uint8_t **pMFP_IMR )
{
	uint8_t	Bit;

	if ( Interrupt > 7 )
	{
		Bit = 1 << ( Interrupt - 8 );
		if ( pMFP_IER )		*pMFP_IER = &(pMFP->IERA);
		if ( pMFP_IPR )		*pMFP_IPR = &(pMFP->IPRA);
		if ( pMFP_ISR )		*pMFP_ISR = &(pMFP->ISRA);
		if ( pMFP_IMR )		*pMFP_IMR = &(pMFP->IMRA);
	}
	else
	{
		Bit = 1 << Interrupt;
		if ( pMFP_IER ) 	*pMFP_IER = &(pMFP->IERB);
		if ( pMFP_IPR )		*pMFP_IPR = &(pMFP->IPRB);
		if ( pMFP_ISR )		*pMFP_ISR = &(pMFP->ISRB);
		if ( pMFP_IMR )		*pMFP_IMR = &(pMFP->IMRB);
	}

	return Bit;
}




/*-----------------------------------------------------------------------*/
/**
 * Update the internal CycInt counters to check if some MFP timers expired.
 * This should be called before accessing the MFP registers for read or write
 * to ensure MFP events are processed in chronological order.
 * This should only be called when CPU runs in Cycle Exact mode, because
 * we need accurate cycles counting when calling CycInt functions during
 * the processing of an instruction.
 */
static void	MFP_UpdateTimers ( MFP_STRUCT *pMFP , uint64_t Clock )
{
//fprintf ( stderr , "mfp update timers clock=%"PRIu64"\n" , Clock );
	if ( !CpuRunCycleExact )
		return;

CycInt_From_Opcode = true;			/* TEMP for CYCLES_COUNTER_VIDEO, see cycInt.c */
	CycInt_Process_Clock ( Clock );
	if ( MFP_UpdateNeeded == true )
		MFP_UpdateIRQ ( pMFP , Clock );
CycInt_From_Opcode = false;			/* TEMP for CYCLES_COUNTER_VIDEO, see cycInt.c */
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
static void	MFP_Exception ( MFP_STRUCT *pMFP , int16_t Interrupt )
{
	unsigned int VecNr;

	VecNr = ( pMFP->VR & 0xf0 ) + Interrupt;

	if (LOG_TRACE_LEVEL(TRACE_MFP_EXCEPTION))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s excep int=%d vec=0x%x new_pc=0x%x video_cyc=%d %d@%d\n" ,
			pMFP->NameSuffix, Interrupt, VecNr * 4, STMemory_ReadLong ( VecNr * 4 ), FrameCycles, LineCycles, HblCounterVideo );
	}

	M68000_Exception(EXCEPTION_NR_MFP_DSP, M68000_EXC_SRC_INT_MFP);
}




/*-----------------------------------------------------------------------*/
/**
 * Get the value of the MFP IRQ signal as seen from the CPU side.
 * When MFP_IRQ is changed in the MFP, the new value is visible on the
 * CPU side after MFP_IRQ_DELAY_TO_CPU.
 * MFP_IRQ_CPU holds the value seen by the CPU, it's updated with the value
 * of MFP_IRQ when MFP_IRQ_DELAY_TO_CPU cycles passed.
 *
 * When the machine is a TT, we combine the IRQ from the 2 MFPs
 */
uint8_t	MFP_GetIRQ_CPU ( void )
{
//fprintf ( stderr , "mfp get irq %d\n" , pMFP_Main->IRQ_CPU );
	if ( !Config_IsMachineTT() )			/* Only 1 MFP */
 		return pMFP_Main->IRQ_CPU;
	else						/* 2nd MFP is only in TT machine */
		return pMFP_Main->IRQ_CPU | pMFP_TT->IRQ_CPU;
}




/*-----------------------------------------------------------------------*/
/**
 * A change in MFP_IRQ is visible to the CPU only after MFP_IRQ_DELAY_TO_CPU
 * cycles. This function will update MFP_IRQ_CPU if the delay has expired.
*
 * When the machine is a TT, we update MFP_IRQ_CPU for pMFP_Main and pMFP_TT
 *
 * This function is called from the CPU emulation part when SPCFLAG_MFP is set.
 *
 * TODO : for now, we check the delay only when MFP_IRQ goes to 1, but this should be
 * handled too when MFP_IRQ goes to 0 (need to be measured on STF)
 */
void	MFP_DelayIRQ ( void )
{
	MFP_STRUCT	*pMFP;
	int		Nb_MFP;
	int		Nb_Unset;
	int		i;


	Nb_MFP = 1;					/* Only 1 MFP by default */
	if ( Config_IsMachineTT() )			/* 2 MFPs for TT */
		Nb_MFP = 2;

	/* Process all the MFPs */
	Nb_Unset = 0;
	for ( i=0 ; i<Nb_MFP ; i++ )
	{
		if ( i == 0 )
			pMFP = pMFP_Main;
		else
			pMFP = pMFP_TT;

		if ( pMFP->IRQ == 1 )
		{
			if ( CyclesGlobalClockCounter - pMFP->IRQ_Time >= MFP_IRQ_DELAY_TO_CPU )
			{
				pMFP->IRQ_CPU = pMFP->IRQ;
				Nb_Unset++;
			}
		}
		else	/* MFP_IRQ == 0, no delay for now */
		{
			pMFP->IRQ_CPU = pMFP->IRQ;
			Nb_Unset++;
		}
	}

	/* If we have as many "unset" as the number of MFP, we unset special MFP flag */
	if ( Nb_Unset == Nb_MFP )
		M68000_UnsetSpecial ( SPCFLAG_MFP );
}


/*-----------------------------------------------------------------------*/
/**
 * Return the vector number associated to the current MFP interrupt.
 * MFP_ProcessIACK is called 12 cycles after the start of the 68000 exception.
 * We must call MFP_UpdateIRQ just before the IACK cycles to update
 * MFP_Current_Interrupt in case a higher MFP interrupt happened
 * or pending bit was set twice for the same interrupt during those 12 cycles (rare case)
 *
 * TT MFP has higher priority than main MFP, so IACK should be checked on TT MFP first if IRQ is set
 */
int	MFP_ProcessIACK ( int OldVecNr )
{
	MFP_STRUCT	*pMFP;
	uint8_t		*pPendingReg;
	uint8_t		*pInServiceReg;
	uint8_t		Bit;
	int		NewVecNr;

	/* If IRQ is set on TT MFP then we process IACK for TT MFP */
	/* Else we process IACK for main MFP */
// TODO : If pMFP->IRQ = 0 after updating then return -1 and do a "spurious interrupt" in the cpu ?

	if ( Config_IsMachineTT() && pMFP_TT->IRQ )
		pMFP = pMFP_TT;
	else
		pMFP = pMFP_Main;

	/* Check if MFP interrupt vector number changed before IACK */
	MFP_UpdateIRQ ( pMFP , CyclesGlobalClockCounter );


	NewVecNr = ( pMFP->VR & 0xf0 ) + pMFP->Current_Interrupt;

	/* Print traces if VecNr changed just before IACK */
	if ( LOG_TRACE_LEVEL(TRACE_MFP_EXCEPTION) && ( OldVecNr != NewVecNr ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s iack change old_vec=0x%x new_vec=0x%x new_pc=0x%x video_cyc=%d %d@%d\n" ,
			pMFP->NameSuffix, OldVecNr * 4, NewVecNr * 4, STMemory_ReadLong ( NewVecNr * 4 ) , FrameCycles, LineCycles, HblCounterVideo );
	}
 
	Bit = MFP_ConvertIntNumber ( pMFP , pMFP->Current_Interrupt , NULL , &pPendingReg , &pInServiceReg , NULL );

	*pPendingReg &= ~Bit;			/* Clear pending bit */

	/* Are we in 'auto' interrupt or 'manual' ? */
	if ( pMFP->VR & 0x08 )			/* Software End-of-Interrupt (SEI) */
		*pInServiceReg |= Bit;		/* Set interrupt in service register */
	else
		*pInServiceReg &= ~Bit;		/* Clear interrupt in service register */

	MFP_UpdateIRQ ( pMFP , CyclesGlobalClockCounter );

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
 *
 * When the machine is a TT, both MFP are daisy chained using IEI and IEO signals
 * and the TT MFP has higher priority than the main MFP. So we must check TT MFP first.
 */
bool	MFP_ProcessIRQ_All ( void )
{
	/* 2nd MFP is only in TT machine and has higher priority than main MFP */
	if ( Config_IsMachineTT() && MFP_ProcessIRQ ( pMFP_TT ) )
		return true;				/* IRQ set on TT MFP, stop here */

	/* 1st MFP is common to all machines */
	return MFP_ProcessIRQ ( pMFP_Main );
}


static bool	MFP_ProcessIRQ ( MFP_STRUCT *pMFP )
{
//fprintf ( stderr , "process irq=%d clock=%"PRIu64" irq_time=%"PRIu64" - ipr %x %x imr %x %x isr %x %x\n" , pMFP->IRQ , CyclesGlobalClockCounter , pMFP->IRQ_Time ,  pMFP->IPRA , pMFP->IPRB , pMFP->IMRA , pMFP->IMRB , pMFP->ISRA , pMFP->ISRB );

	if ( pMFP->IRQ == 1 )
	{
		if ( CyclesGlobalClockCounter - pMFP->IRQ_Time < MFP_IRQ_DELAY_TO_CPU )	/* Is it time to trigger the exception ? */
		{
			return false;				/* For now, return without calling an exception (and try again later) */
		}

		if (regs.intmask < 6)
		{
			/* The exception is possible ; pending / in service bits will be handled in MFP_ProcessIACK() */
			MFP_Exception ( pMFP , pMFP->Current_Interrupt );
			return true;
		}
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Update the MFP IRQ signal for all the MFP
 */
void	MFP_UpdateIRQ_All ( uint64_t Event_Time )
{
	/* 2nd MFP is only in TT machine */
	if ( Config_IsMachineTT() )
		MFP_UpdateIRQ ( pMFP_TT , Event_Time );

	/* 1st MFP is common to all machines */
	MFP_UpdateIRQ ( pMFP_Main , Event_Time );
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
static void	MFP_UpdateIRQ ( MFP_STRUCT *pMFP , uint64_t Event_Time )
{
	int	NewInt = -1;

//fprintf ( stderr , "updirq in irq=%d event_time=%"PRIu64" - ipr %x %x imr %x %x isr %x %x - clock=%"PRIu64"\n" , pMFP->IRQ , Event_Time , pMFP->IPRA , pMFP->IPRB , pMFP->IMRA , pMFP->IMRB , pMFP->ISRA , pMFP->ISRB , CyclesGlobalClockCounter  );

	if ( ( pMFP->IPRA & pMFP->IMRA ) | ( pMFP->IPRB & pMFP->IMRB ) )
	{
		NewInt = MFP_CheckPendingInterrupts ( pMFP );
		
		if ( NewInt >= 0 )
		{
			if ( pMFP->IRQ == 0 )			/* MFP IRQ goes from 0 to 1 */
			{
				if ( Event_Time != 0 )
					pMFP->IRQ_Time = Event_Time;
				else
					pMFP->IRQ_Time = pMFP->Pending_Time[ NewInt ];
			}

			pMFP->IRQ = 1;
			pMFP->Current_Interrupt = NewInt;
		}
		else
			pMFP->IRQ = 0;				/* Pending interrupts are blocked by in-service interrupts */
	}
	else
	{
		pMFP->IRQ = 0;
	}

//fprintf ( stderr , "updirq out irq=%d irq_time=%"PRIu64" newint=%d - ipr %x %x imr %x %x isr %x %x - clock=%"PRIu64"\n" , pMFP->IRQ , pMFP->IRQ_Time , NewInt , pMFP->IPRA , pMFP->IPRB , pMFP->IMRA , pMFP->IMRB , pMFP->ISRA , pMFP->ISRB , CyclesGlobalClockCounter );
	M68000_SetSpecial ( SPCFLAG_MFP );			/* CPU part should call MFP_Delay_IRQ() */

	/* Update IRQ is done, reset Time_Min and UpdateNeeded */
	pMFP->Pending_Time_Min = UINT64_MAX;
	MFP_UpdateNeeded = false;
}


/*-----------------------------------------------------------------------*/
/**
 * Test if interrupt 'Bit' is set in pending and mask register.
 * Also check that no higher priority interrupt is in service.
 * Depending on the interrupt, we check either IPRA/IMRA or IPRB/IMRB
 * @return true if the MFP interrupt request is allowed
 */
static bool	MFP_InterruptRequest ( MFP_STRUCT *pMFP , int Int , uint8_t Bit , uint8_t IPRx , uint8_t IMRx , uint8_t PriorityMaskA , uint8_t PriorityMaskB )
{
//fprintf ( stderr , "mfp int req %d %x %x %X %x %x %x %x\n" , Int , Bit , IPRx , IMRx , PriorityMaskA , PriorityMaskB , pMFP->ISRA , pMFP->ISRB );

	if ( ( IPRx & IMRx & Bit ) 					/* Interrupt is pending and not masked */
	    && ( pMFP->Pending_Time[ Int ] <= pMFP->Pending_Time_Min ) )/* Process pending requests in chronological time */
	{
		/* Are any higher priority interrupts in service ? */
		if ( ( ( pMFP->ISRA & PriorityMaskA ) == 0 ) && ( ( pMFP->ISRB & PriorityMaskB ) == 0 ) )
			return true;				/* No higher int in service */
	}

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Check if any MFP interrupts can be serviced.
 * @return MFP interrupt number for the highest interrupt allowed, else return -1.
 */
static int	MFP_CheckPendingInterrupts ( MFP_STRUCT *pMFP )
{
	if ( pMFP->IPRA & pMFP->IMRA )					/* Check we have non masked pending ints */
	{
		if ( MFP_InterruptRequest ( pMFP , MFP_INT_GPIP7 , MFP_GPIP7_BIT, pMFP->IPRA, pMFP->IMRA, 0x80, 0x00 ) )		/* Check MFP GPIP7 interrupt (bit 7) */
			return MFP_INT_GPIP7;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_GPIP6 , MFP_GPIP6_BIT, pMFP->IPRA, pMFP->IMRA, 0xc0, 0x00 ) )		/* Check MFP GPIP6 interrupt (bit 6) */
			return MFP_INT_GPIP6;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_TIMER_A , MFP_TIMER_A_BIT, pMFP->IPRA, pMFP->IMRA, 0xe0, 0x00 ) )	/* Check Timer A (bit 5) */
			return MFP_INT_TIMER_A;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_RCV_BUF_FULL , MFP_RCV_BUF_FULL_BIT, pMFP->IPRA, pMFP->IMRA, 0xf0, 0x00 ) )	/* Check Receive buffer full (bit 4) */
			return MFP_INT_RCV_BUF_FULL;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_RCV_ERR , MFP_RCV_ERR_BIT, pMFP->IPRA, pMFP->IMRA, 0xf8, 0x00 ) )	/* Check Receive error (bit 3) */
			return MFP_INT_RCV_ERR;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_TRN_BUF_EMPTY , MFP_TRN_BUF_EMPTY_BIT, pMFP->IPRA, pMFP->IMRA, 0xfc, 0x00 ) )	/* Check Transmit buffer empty (bit 2) */
			return MFP_INT_TRN_BUF_EMPTY;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_TRN_ERR , MFP_TRN_ERR_BIT, pMFP->IPRA, pMFP->IMRA, 0xfe, 0x00 ) )	/* Check Transmit error empty (bit 1) */
			return MFP_INT_TRN_ERR;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_TIMER_B , MFP_TIMER_B_BIT, pMFP->IPRA, pMFP->IMRA, 0xff, 0x00 ) )	/* Check Timer B (bit 0) */
			return MFP_INT_TIMER_B;
	}

	if ( pMFP->IPRB & pMFP->IMRB )					/* Check we have non masked pending ints */
	{
		if ( MFP_InterruptRequest ( pMFP , MFP_INT_GPIP5 , MFP_GPIP5_BIT, pMFP->IPRB, pMFP->IMRB, 0xff, 0x80 ) )		/* Check GPIP5 = FDC (bit 7) */
			return MFP_INT_GPIP5;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_GPIP4 , MFP_GPIP4_BIT, pMFP->IPRB, pMFP->IMRB, 0xff, 0xc0 ) )		/* Check GPIP4 = ACIA (Keyboard or MIDI) (bit 6) */
			return MFP_INT_GPIP4;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_TIMER_C , MFP_TIMER_C_BIT, pMFP->IPRB, pMFP->IMRB, 0xff, 0xe0 ) )	/* Check Timer C (bit 5) */
			return MFP_INT_TIMER_C;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_TIMER_D , MFP_TIMER_D_BIT, pMFP->IPRB, pMFP->IMRB, 0xff, 0xf0 ) )	/* Check Timer D (bit 4) */
			return MFP_INT_TIMER_D;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_GPIP3 , MFP_GPIP3_BIT, pMFP->IPRB, pMFP->IMRB, 0xff, 0xf8 ) )		/* Check GPIP3 = GPU/Blitter (bit 3) */
			return MFP_INT_GPIP3;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_GPIP2 , MFP_GPIP2_BIT, pMFP->IPRB, pMFP->IMRB, 0xff, 0xfc ) )		/* Check GPIP2 (bit 2) */
			return MFP_INT_GPIP2;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_GPIP1 , MFP_GPIP1_BIT, pMFP->IPRB, pMFP->IMRB, 0xff, 0xfe ) )		/* Check (Falcon) Centronics ACK / (ST) RS232 DCD (bit 1) */
			return MFP_INT_GPIP1;

		if ( MFP_InterruptRequest ( pMFP , MFP_INT_GPIP0 , MFP_GPIP0_BIT, pMFP->IPRB, pMFP->IMRB, 0xff, 0xff ) )		/* Check Centronics BUSY (bit 0) */
			return MFP_INT_GPIP0;
	}

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
void	MFP_InputOnChannel ( MFP_STRUCT *pMFP , int Interrupt , int Interrupt_Delayed_Cycles )
{
	uint8_t	*pEnableReg;
	uint8_t	*pPendingReg;
	uint8_t	*pMaskReg;
	uint8_t	Bit;

//fprintf ( stderr , "mfp input %d delay %d clock %"PRIu64"\n" , Interrupt , Interrupt_Delayed_Cycles , CyclesGlobalClockCounter );
	Bit = MFP_ConvertIntNumber ( pMFP , Interrupt , &pEnableReg , &pPendingReg , NULL , &pMaskReg );

	/* Input has occurred on MFP channel, set interrupt pending to request service when able */
	if ( *pEnableReg & Bit )
	{
		/* Print traces if pending bits changed just before IACK */
		if ( LOG_TRACE_LEVEL(TRACE_MFP_EXCEPTION) && ( CPU_IACK == true ) )
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			if ( *pPendingReg & Bit )
				LOG_TRACE_PRINT("mfp%s input, pending set again during iack for int=%d, skip one interrupt video_cyc=%d %d@%d\n" ,
					pMFP->NameSuffix , Interrupt , FrameCycles, LineCycles, HblCounterVideo );
			else
				LOG_TRACE_PRINT("mfp%s input, new pending set during iack for int=%d video_cyc=%d %d@%d\n" ,
					pMFP->NameSuffix , Interrupt , FrameCycles, LineCycles, HblCounterVideo );
		}

		/* Set pending bit and event's time */
		*pPendingReg |= Bit;
		pMFP->Pending_Time[ Interrupt ] = CyclesGlobalClockCounter - Interrupt_Delayed_Cycles;

		/* Store the time of the most ancient non-masked pending=1 event */
		if ( ( *pMaskReg & Bit ) && ( pMFP->Pending_Time[ Interrupt ] < pMFP->Pending_Time_Min ) )
			pMFP->Pending_Time_Min = pMFP->Pending_Time[ Interrupt ];
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
static void	MFP_GPIP_Update_Interrupt ( MFP_STRUCT *pMFP , uint8_t GPIP_old , uint8_t GPIP_new , uint8_t AER_old , uint8_t AER_new , uint8_t DDR_old , uint8_t DDR_new )
{
	uint8_t	State_old;
	uint8_t	State_new;
	int	Bit;
	uint8_t	BitMask;

//fprintf ( stderr , "gpip upd gpip_old=%x gpip_new=%x aer_old=%x aer_new=%x ddr_old=%x ddr_new=%x\n" , GPIP_old, GPIP_new, AER_old, AER_new, DDR_old, DDR_new );
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
				MFP_InputOnChannel ( pMFP , MFP_GPIP_LineToIntNumber[ Bit ] , 0 );
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
void	MFP_GPIP_Set_Line_Input ( MFP_STRUCT *pMFP , uint8_t LineNr , uint8_t Bit )
{
	uint8_t	Mask;
	uint8_t	GPIP_old;

	Mask = 1 << LineNr;

//fprintf ( stderr , "gpip set0 mask=%x bit=%d ddr=%x gpip=%x\n", Mask, Bit, pMFP->DDR, pMFP->GPIP );

	/* Special case when changing bit 7 of the main MFP : depending on the machine type, */
	/* this can be a combination of several signals. So we override Bit with its new value */
	if ( ( pMFP == pMFP_Main ) && ( LineNr == MFP_GPIP_LINE7 ) )
		Bit = MFP_Main_Compute_GPIP7 ();

	/* Special case when changing bit 4 of the main MFP : this line is connected */
	/* to the 2 ACIA's IRQ lines at the same time */
	if ( ( pMFP == pMFP_Main ) && ( LineNr == MFP_GPIP_LINE_ACIA ) )
		Bit = MFP_Main_Compute_GPIP_LINE_ACIA ();

	/* Check that corresponding line is defined as input in DDR (0=input 1=output) */
	/* and that the bit is changing */
	if ( ( ( pMFP->DDR & Mask ) == 0 )
	  && ( ( pMFP->GPIP & Mask ) != ( Bit << LineNr ) ) )
	{
		GPIP_old = pMFP->GPIP;

		if ( Bit )
		{
			pMFP->GPIP |= Mask;
		}
		else
		{
			pMFP->GPIP &= ~Mask;
		}

		/* Update possible interrupts after changing GPIP */
		MFP_GPIP_Update_Interrupt ( pMFP , GPIP_old , pMFP->GPIP , pMFP->AER , pMFP->AER , pMFP->DDR , pMFP->DDR );
	}
//fprintf ( stderr , "gpip set gpip_old=%x gpip_new=%x\n" , GPIP_old , pMFP->GPIP );
}


/*-----------------------------------------------------------------------*/
/**
 * Change input line for Timer A (TAI) and generate an interrupt when in event count mode
 * and counter reaches 1.
 * TAI is associated to AER GPIP4
 */
void	MFP_TimerA_Set_Line_Input ( MFP_STRUCT *pMFP , uint8_t Bit )
{
	uint8_t	AER_bit;
//fprintf ( stderr , "MFP_TimerA_Set_Line_Input bit=%d TAI=%d TACR=%d AER=%d\n" , Bit , pMFP->TAI, pMFP->TACR, ( pMFP->AER >> 4 ) & 1 );

	if ( pMFP->TAI == Bit )
		return;					/* No change */
	pMFP->TAI = Bit;				/* Update TAI value */

	if ( pMFP->TACR != 0x08 )			/* Not in event count mode */
		return;					/* Do nothing */

	AER_bit = ( pMFP->AER >> 4 ) & 1;		/* TAI is associated to AER GPIP4 */
	if ( Bit != AER_bit )				/* See MFP_GPIP_Update_Interrupt : we detect a transition */
		return;					/* when AER=Bit */

	if ( pMFP->TA_MAINCOUNTER == 1)			/* Timer expired? If so, generate interrupt */
	{
		pMFP->TA_MAINCOUNTER = pMFP->TADR;	/* Reload timer from data register */

		/* Acknowledge in MFP circuit, pass bit,enable,pending */
		MFP_InputOnChannel ( pMFP , MFP_INT_TIMER_A , 0 );
	}
	else
	{
		pMFP->TA_MAINCOUNTER--;			/* Decrement timer main counter */
		/* As TA_MAINCOUNTER is uint8_t, when we decrement TA_MAINCOUNTER=0 */
		/* we go to TA_MAINCOUNTER=255, which is the wanted behaviour because */
		/* data reg = 0 means 256 in fact. So, the next 2 lines are redundant. */
/*		if ( TA_MAINCOUNTER < 0 )
			TA_MAINCOUNTER = 255;
*/
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Generate Timer A Interrupt when in Event Count mode
 * TODO : this should be replaced by using MFP_TimerA_Set_Line_Input
 * to take AER into account
 */
void	MFP_TimerA_EventCount( MFP_STRUCT *pMFP )
{
	if ( pMFP->TACR != 0x08 )			/* Not in event count mode */
		return;					/* Do nothing */

	if ( pMFP->TA_MAINCOUNTER == 1)			/* Timer expired? If so, generate interrupt */
	{
		pMFP->TA_MAINCOUNTER = pMFP->TADR;	/* Reload timer from data register */

		/* Acknowledge in MFP circuit, pass bit,enable,pending */
		MFP_InputOnChannel ( pMFP , MFP_INT_TIMER_A , 0 );
	}
	else
	{
		pMFP->TA_MAINCOUNTER--;			/* Decrement timer main counter */
		/* As TA_MAINCOUNTER is uint8_t, when we decrement TA_MAINCOUNTER=0 */
		/* we go to TA_MAINCOUNTER=255, which is the wanted behaviour because */
		/* data reg = 0 means 256 in fact. So, the next 2 lines are redundant. */
/*		if ( TA_MAINCOUNTER < 0 )
			TA_MAINCOUNTER = 255;
*/
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Generate Timer B Interrupt when in Event Count mode
 */
void	MFP_TimerB_EventCount ( MFP_STRUCT *pMFP , int Delayed_Cycles )
{
	if ( pMFP->TBCR != 0x08 )			/* Not in event count mode */
		return;					/* Do nothing */

	/* Video DE signal is connected to Timer B on the main MFP and also on the TT MFP */
	LOG_TRACE(TRACE_VIDEO_HBL , "mfp%s/video timer B new event count %d, delay=%d\n" , pMFP->NameSuffix , pMFP->TB_MAINCOUNTER-1 , Delayed_Cycles );

	if ( pMFP->TB_MAINCOUNTER == 1 )		/* Timer expired? If so, generate interrupt */
	{
		pMFP->TB_MAINCOUNTER = pMFP->TBDR;	/* Reload timer from data register */

		/* Acknowledge in MFP circuit, pass bit,enable,pending */
		MFP_InputOnChannel ( pMFP , MFP_INT_TIMER_B , Delayed_Cycles );
	}
	else
	{
		pMFP->TB_MAINCOUNTER--;			/* Decrement timer main counter */
		/* As TB_MAINCOUNTER is uint8_t, when we decrement TB_MAINCOUNTER=0 */
		/* we go to TB_MAINCOUNTER=255, which is the wanted behaviour because */
		/* data reg = 0 means 256 in fact. So, the next 2 lines are redundant. */
/*		if ( TB_MAINCOUNTER < 0 )
			TB_MAINCOUNTER = 255;
*/
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer A or B - EventCount mode is done in HBL handler to time correctly
 */
static uint32_t MFP_StartTimer_AB ( MFP_STRUCT *pMFP , uint8_t TimerControl, uint16_t TimerData, interrupt_id Handler,
                             bool bFirstTimer)
{
	uint32_t TimerClockCycles = 0;


	/* When in pulse width mode, handle as in delay mode */
	/* (this is not completely correct, as we should also handle GPIO 3/4 in pulse mode) */
	if ( TimerControl > 8 )
	{
		if (LOG_TRACE_LEVEL(TRACE_MFP_START))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("mfp%s start AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d pulse mode->delay mode\n",
			                pMFP->NameSuffix , Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
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
		if ( ( M68000_GetPC() == 0x14d72 ) && ( STMemory_ReadLong ( 0x14d6c ) == 0x11faff75 ) )
		{
//			fprintf ( stderr , "mfp add jitter %d\n" , TimerClockCycles );
			TimerClockCycles += Hatari_rand()%5-2;	/* add jitter for wod2 */
		}

		if (LOG_TRACE_LEVEL(TRACE_MFP_START))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("mfp%s start AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s\n",
			                pMFP->NameSuffix , Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
			                bFirstTimer?"true":"false");
		}

		/* And add to our internal interrupt list, if timer cycles is zero
		 * then timer is stopped */
		CycInt_RemovePendingInterrupt(Handler);
		if (TimerClockCycles)
		{
			/* Start timer from now? If not continue timer using PendingCycleOver */
			if (bFirstTimer)
			{
				CycInt_AddRelativeInterrupt(TimerClockCycles, INT_MFP_CYCLE, Handler);
			}
			else
			{
				int64_t	TimerClockCyclesInternal = INT_CONVERT_TO_INTERNAL ( (int64_t)TimerClockCycles , INT_MFP_CYCLE );

				/* In case we miss more than one int, we must correct the delay for the next one */
				if ( (int64_t)PendingCyclesOver > TimerClockCyclesInternal )
					PendingCyclesOver = PendingCyclesOver % TimerClockCyclesInternal;

				CycInt_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, -PendingCyclesOver);
			}
		}

		else	/* Ctrl was 0 -> timer is stopped */
		{
			/* do nothing, only print some traces */
			if (LOG_TRACE_LEVEL(TRACE_MFP_START))
			{
				int FrameCycles, HblCounterVideo, LineCycles;
				Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
				LOG_TRACE_PRINT("mfp%s stop AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s\n",
				                pMFP->NameSuffix , Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
				                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
				                bFirstTimer?"true":"false");
			}
		}
	}


	else if (TimerControl == 8 )				/* event count mode */
	{
		/* Make sure no outstanding interrupts in list if channel is disabled */
		CycInt_RemovePendingInterrupt(Handler);

		if ( ( Handler == INTERRUPT_MFP_MAIN_TIMERB )		/* we're starting timer B event count mode */
		  || ( Handler == INTERRUPT_MFP_TT_TIMERB ) )
		{
			/* Store start cycle for handling interrupt in video.c */
			TimerBEventCountCycleStart = Cycles_GetCounterOnWriteAccess(CYCLES_COUNTER_VIDEO);
		}

		if (LOG_TRACE_LEVEL(TRACE_MFP_START))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("mfp%s start AB handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s\n",
			                pMFP->NameSuffix , Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
			                bFirstTimer?"true":"false");
		}
	}

	return TimerClockCycles;
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer C or D
 */
static uint32_t MFP_StartTimer_CD (  MFP_STRUCT *pMFP , uint8_t TimerControl, uint16_t TimerData, interrupt_id Handler,
                             bool bFirstTimer)
{
	uint32_t TimerClockCycles = 0;

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
			LOG_TRACE_PRINT("mfp%s start CD handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s\n" ,
			                     pMFP->NameSuffix , Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                     FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
			                     bFirstTimer?"true":"false" );
		}

		/* And add to our internal interrupt list, if timer cycles is zero
		 * then timer is stopped */
		CycInt_RemovePendingInterrupt(Handler);
		if (TimerClockCycles)
		{
			/* Start timer from now? If not continue timer using PendingCycleOver */
			if (bFirstTimer)
			{
				CycInt_AddRelativeInterrupt(TimerClockCycles, INT_MFP_CYCLE, Handler);
			}
			else
			{
				int64_t	TimerClockCyclesInternal = INT_CONVERT_TO_INTERNAL ( (int64_t)TimerClockCycles , INT_MFP_CYCLE );
				/* In case we miss more than one int, we must correct the delay for the next one */
				if ( (int64_t)PendingCyclesOver > TimerClockCyclesInternal )
					PendingCyclesOver = PendingCyclesOver % TimerClockCyclesInternal;

				CycInt_AddRelativeInterruptWithOffset(TimerClockCycles, INT_MFP_CYCLE, Handler, -PendingCyclesOver);
			}
		}
	}

	else	/* timer control is 0 */
	{
		if ( LOG_TRACE_LEVEL( TRACE_MFP_START ) )
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("mfp%s stop CD handler=%d data=%d ctrl=%d timer_cyc=%d pending_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d first=%s\n" ,
			                     pMFP->NameSuffix , Handler, TimerData, TimerControl, TimerClockCycles, PendingCyclesOver,
			                     FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles,
			                     bFirstTimer?"true":"false" );
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
static uint8_t	MFP_ReadTimer_AB ( MFP_STRUCT *pMFP , uint8_t TimerControl, uint8_t MainCounter, uint32_t TimerCycles, interrupt_id Handler, bool TimerIsStopping)
{
	/* Find TimerAB count, if no interrupt or not in delay mode assume
	 * in Event Count mode so already up-to-date as kept by HBL */
	if (CycInt_InterruptActive(Handler) && (TimerControl > 0) && (TimerControl <= 7))
	{
		/* Find cycles passed since last interrupt */
		MainCounter = MFP_CYCLE_TO_REG ( CycInt_FindCyclesRemaining ( Handler, INT_MFP_CYCLE ), TimerControl );
//fprintf ( stderr , "mfp read AB count %d int_cyc=%d\n" , MainCounter , CycInt_FindCyclesRemaining ( Handler, INT_MFP_CYCLE ) );
	}

	/* If the timer is stopped when the internal mfp data reg is already < 1 */
	/* then the data reg will be 0 (=256) next time the timer will be restarted */
	/* if no write is made to the data reg before */
	if ( TimerIsStopping )
	{
		if ( CycInt_FindCyclesRemaining ( Handler, INT_MFP_CYCLE ) < MFP_REG_TO_CYCLES ( 1 , TimerControl ) )
		{
			MainCounter = 0;			/* internal mfp counter becomes 0 (=256) */
			LOG_TRACE(TRACE_MFP_READ , "mfp%s read AB handler=%d stopping timer while data reg between 1 and 0 : forcing data to 256\n" ,
					pMFP->NameSuffix , Handler );
		}
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read AB handler=%d data=%d ctrl=%d timer_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
		                     pMFP->NameSuffix , Handler, MainCounter, TimerControl, TimerCycles,
		                     FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	return MainCounter;
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer C or D
 */
static uint8_t	MFP_ReadTimer_CD ( MFP_STRUCT *pMFP , uint8_t TimerControl, uint8_t TimerData, uint8_t MainCounter, uint32_t TimerCycles, interrupt_id Handler, bool TimerIsStopping)
{
	/* Find TimerCD count. If timer is off, MainCounter already contains the latest value */
	if (CycInt_InterruptActive(Handler))
	{
		/* Find cycles passed since last interrupt */
		MainCounter = MFP_CYCLE_TO_REG ( CycInt_FindCyclesRemaining ( Handler, INT_MFP_CYCLE ), TimerControl );
//fprintf ( stderr , "mfp read CD count %d int_cyc=%d\n" , MainCounter , CycInt_FindCyclesRemaining ( Handler, INT_MFP_CYCLE ) );
	}

	/* If the timer is stopped when the internal mfp data reg is already < 1 */
	/* then the data reg will be 0 (=256) next time the timer will be restarted */
	/* if no write is made to the data reg before */
	if ( TimerIsStopping )
	{
		if ( CycInt_FindCyclesRemaining ( Handler, INT_MFP_CYCLE ) < MFP_REG_TO_CYCLES ( 1 , TimerControl ) )
		{
			MainCounter = 0;			/* internal mfp counter becomes 0 (=256) */
			LOG_TRACE(TRACE_MFP_READ , "mfp%s read CD handler=%d stopping timer while data reg between 1 and 0 : forcing data to 256\n" ,
					pMFP->NameSuffix , Handler );
		}
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read CD handler=%d data=%d ctrl=%d timer_cyc=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
		                     pMFP->NameSuffix , Handler, MainCounter, TimerControl, TimerCycles,
		                     FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	return MainCounter;
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer A
 */
static void	MFP_StartTimerA ( MFP_STRUCT *pMFP )
{
	pMFP->TimerAClockCycles = MFP_StartTimer_AB ( pMFP , pMFP->TACR , pMFP->TA_MAINCOUNTER ,
		pMFP == pMFP_Main ? INTERRUPT_MFP_MAIN_TIMERA : INTERRUPT_MFP_TT_TIMERA , true );
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer A
 */
static void	MFP_ReadTimerA ( MFP_STRUCT *pMFP , bool TimerIsStopping)
{
	pMFP->TA_MAINCOUNTER = MFP_ReadTimer_AB ( pMFP , pMFP->TACR , pMFP->TA_MAINCOUNTER , pMFP->TimerAClockCycles ,
		pMFP == pMFP_Main ? INTERRUPT_MFP_MAIN_TIMERA : INTERRUPT_MFP_TT_TIMERA , TimerIsStopping);
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer B
 * (This does not start the EventCount mode time as this is taken care
 *  of by the HBL)
 */
static void	MFP_StartTimerB ( MFP_STRUCT *pMFP )
{
	pMFP->TimerBClockCycles = MFP_StartTimer_AB ( pMFP , pMFP->TBCR , pMFP->TB_MAINCOUNTER ,
		pMFP == pMFP_Main ? INTERRUPT_MFP_MAIN_TIMERB : INTERRUPT_MFP_TT_TIMERB , true );
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer B
 */
static void	MFP_ReadTimerB ( MFP_STRUCT *pMFP , bool TimerIsStopping )
{
	pMFP->TB_MAINCOUNTER = MFP_ReadTimer_AB ( pMFP , pMFP->TBCR , pMFP->TB_MAINCOUNTER , pMFP->TimerBClockCycles ,
		pMFP == pMFP_Main ? INTERRUPT_MFP_MAIN_TIMERB : INTERRUPT_MFP_TT_TIMERB , TimerIsStopping);
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer C
 */
static void	MFP_StartTimerC ( MFP_STRUCT *pMFP )
{
	pMFP->TimerCClockCycles = MFP_StartTimer_CD ( pMFP , (pMFP->TCDCR>>4)&7 , pMFP->TC_MAINCOUNTER ,
		pMFP == pMFP_Main ? INTERRUPT_MFP_MAIN_TIMERC : INTERRUPT_MFP_TT_TIMERC , true );
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer C
 */
static void	MFP_ReadTimerC ( MFP_STRUCT *pMFP , bool TimerIsStopping )
{
	pMFP->TC_MAINCOUNTER = MFP_ReadTimer_CD ( pMFP , (pMFP->TCDCR>>4)&7 , pMFP->TCDR , pMFP->TC_MAINCOUNTER , pMFP->TimerCClockCycles ,
		pMFP == pMFP_Main ? INTERRUPT_MFP_MAIN_TIMERC : INTERRUPT_MFP_TT_TIMERC , TimerIsStopping);
}


/*-----------------------------------------------------------------------*/
/**
 * Start Timer D
 */
static void	MFP_StartTimerD ( MFP_STRUCT *pMFP )
{
	pMFP->TimerDClockCycles = MFP_StartTimer_CD ( pMFP , pMFP->TCDCR&7 , pMFP->TD_MAINCOUNTER ,
		pMFP == pMFP_Main ? INTERRUPT_MFP_MAIN_TIMERD : INTERRUPT_MFP_TT_TIMERD , true );
}


/*-----------------------------------------------------------------------*/
/**
 * Read Timer D
 */
static void	MFP_ReadTimerD ( MFP_STRUCT *pMFP , bool TimerIsStopping )
{
	pMFP->TD_MAINCOUNTER = MFP_ReadTimer_CD ( pMFP , pMFP->TCDCR&7 , pMFP->TDDR , pMFP->TD_MAINCOUNTER , pMFP->TimerDClockCycles ,
		pMFP == pMFP_Main ? INTERRUPT_MFP_MAIN_TIMERD : INTERRUPT_MFP_TT_TIMERD , TimerIsStopping);
}


/*-----------------------------------------------------------------------*/
/**
 * Handle Timer A Interrupt
 */
static void	MFP_InterruptHandler_TimerA ( MFP_STRUCT *pMFP , interrupt_id Handler )
{
	/* Number of internal cycles we went over for this timer ( <= 0 ),
	 * used when timer expires and needs to be restarted */
	PendingCyclesOver = -PendingInterruptCount;		/* >= 0 */

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit,enable,pending */
	if ( ( pMFP->TACR & 0xf ) != 0 )		/* Is timer OK? */
		MFP_InputOnChannel ( pMFP , MFP_INT_TIMER_A , INT_CONVERT_FROM_INTERNAL ( PendingCyclesOver , INT_CPU_CYCLE ) );

	/* Start next interrupt, if need one - from current cycle count */
	pMFP->TimerAClockCycles = MFP_StartTimer_AB ( pMFP , pMFP->TACR , pMFP->TADR , Handler , false );
}


void	MFP_Main_InterruptHandler_TimerA ( void )
{
	MFP_InterruptHandler_TimerA ( pMFP_Main , INTERRUPT_MFP_MAIN_TIMERA );
}


void	MFP_TT_InterruptHandler_TimerA ( void )
{
	MFP_InterruptHandler_TimerA ( pMFP_TT , INTERRUPT_MFP_TT_TIMERA );
}


/*-----------------------------------------------------------------------*/
/**
 * Handle Timer B Interrupt
 */
static void	MFP_InterruptHandler_TimerB ( MFP_STRUCT *pMFP , interrupt_id Handler )
{
	/* Number of internal cycles we went over for this timer ( <= 0 ),
	 * used when timer expires and needs to be restarted */
	PendingCyclesOver = -PendingInterruptCount;		/* >= 0 */

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	if ( ( pMFP->TBCR & 0xf ) != 0 )		/* Is timer OK? */
		MFP_InputOnChannel ( pMFP , MFP_INT_TIMER_B , INT_CONVERT_FROM_INTERNAL ( PendingCyclesOver , INT_CPU_CYCLE ) );

	/* Start next interrupt, if need one - from current cycle count */
	pMFP->TimerBClockCycles = MFP_StartTimer_AB ( pMFP , pMFP->TBCR , pMFP->TBDR , Handler , false );
}


void	MFP_Main_InterruptHandler_TimerB ( void )
{
	MFP_InterruptHandler_TimerB ( pMFP_Main , INTERRUPT_MFP_MAIN_TIMERB );
}


void	MFP_TT_InterruptHandler_TimerB ( void )
{
	MFP_InterruptHandler_TimerB ( pMFP_TT , INTERRUPT_MFP_TT_TIMERB );
}


/*-----------------------------------------------------------------------*/
/**
 * Handle Timer C Interrupt
 */
static void	MFP_InterruptHandler_TimerC ( MFP_STRUCT *pMFP , interrupt_id Handler )
{
	/* Number of internal cycles we went over for this timer ( <= 0 ),
	 * used when timer expires and needs to be restarted */
	PendingCyclesOver = -PendingInterruptCount;		/* >= 0 */

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	if ( ( pMFP->TCDCR & 0x70 ) != 0 )		/* Is timer OK? */
		MFP_InputOnChannel ( pMFP , MFP_INT_TIMER_C , INT_CONVERT_FROM_INTERNAL ( PendingCyclesOver , INT_CPU_CYCLE ) );

	/* Start next interrupt, if need one - from current cycle count */
	pMFP->TimerCClockCycles = MFP_StartTimer_CD ( pMFP , (pMFP->TCDCR>>4)&7 , pMFP->TCDR , Handler , false );
}


void	MFP_Main_InterruptHandler_TimerC ( void )
{
	MFP_InterruptHandler_TimerC ( pMFP_Main , INTERRUPT_MFP_MAIN_TIMERC );
}


void	MFP_TT_InterruptHandler_TimerC ( void )
{
	MFP_InterruptHandler_TimerC ( pMFP_TT , INTERRUPT_MFP_TT_TIMERC );
}


/*-----------------------------------------------------------------------*/
/**
 * Handle Timer D Interrupt
 */
static void	MFP_InterruptHandler_TimerD ( MFP_STRUCT *pMFP , interrupt_id Handler )
{
	/* Number of internal cycles we went over for this timer ( <= 0 ),
	 * used when timer expires and needs to be restarted */
	PendingCyclesOver = -PendingInterruptCount;		/* >= 0 */

	/* Remove this interrupt from list and re-order */
	CycInt_AcknowledgeInterrupt();

	/* Acknowledge in MFP circuit, pass bit, enable, pending */
	if ( ( pMFP->TCDCR & 0x07 ) != 0 )		/* Is timer OK? */
		MFP_InputOnChannel ( pMFP , MFP_INT_TIMER_D , INT_CONVERT_FROM_INTERNAL ( PendingCyclesOver , INT_CPU_CYCLE ) );

	/* Start next interrupt, if need one - from current cycle count */
	pMFP->TimerDClockCycles = MFP_StartTimer_CD ( pMFP , pMFP->TCDCR&7 , pMFP->TDDR , Handler , false );
}


void	MFP_Main_InterruptHandler_TimerD ( void )
{
	MFP_InterruptHandler_TimerD ( pMFP_Main , INTERRUPT_MFP_MAIN_TIMERD );
	RS232_Update();
}


void	MFP_TT_InterruptHandler_TimerD ( void )
{
	MFP_InterruptHandler_TimerD ( pMFP_TT , INTERRUPT_MFP_TT_TIMERD );
}


/*-----------------------------------------------------------------------*/
/**
 * Handle read from GPIP register (0xfffa01 or 0xfffa81)
 * We call a different function depending on the MFP (Main or TT) as all
 * the bits are specific
 */
void	MFP_GPIP_ReadByte ( void )
{
	if ( IoAccessCurrentAddress == 0xfffa01 )
		MFP_GPIP_ReadByte_Main ( pMFP_Main );
	else
		MFP_GPIP_ReadByte_TT ( pMFP_TT );
}


/*-----------------------------------------------------------------------*/
/**
 * Compute value of GPIP bit 7 for main MFP.
 * This bit can be a combination of the monochrome monitor signal and
 * the dma sound status (depending on the machine type)
 */
uint8_t    MFP_Main_Compute_GPIP7 ( void )
{
	uint8_t	Bit;

	if (Config_IsMachineFalcon())
	{
		if (Crossbar_Get_SNDINT_Line())
			Bit = 1;
		else
			Bit = 0;

		/* Sparrow / TOS 2.07 still use monitor detection via GPIP7 */
		if (TosVersion == 0x207 && !Video_Get_MONO_Line())
			Bit ^= 1;
	}
	else
	{
		if (!Video_Get_MONO_Line())
			Bit  = 1;		/* Color monitor : set bit 7 */
		else
			Bit = 0;		/* Monochrome monitor : clear bit 7 */

		/* In case of STE/TT, bit 7 is XORed with DMA sound XSINT signal */
		if ( ( Config_IsMachineSTE() || Config_IsMachineTT() ) && DmaSnd_Get_XSINT_Line() )
			Bit ^= 1;
	}

	return Bit;
}


/*-----------------------------------------------------------------------*/
/**
 * Compute value of GPIP bit 4 for main MFP.
 * This bit is a OR between the 2 ACIA's IRQ for IKBD and Midi
 * NOTE : signal is active low on ACIA, IRQ is set when line's value is 0
 */
uint8_t    MFP_Main_Compute_GPIP_LINE_ACIA ( void )
{
	uint8_t	Bit;

	/* If any of the ACIA has IRQ set, set IRQ on GPIP 4 */
	if ( ( pACIA_IKBD->Get_Line_IRQ ( pACIA_IKBD ) == 0 )
	    || ( pACIA_MIDI->Get_Line_IRQ ( pACIA_MIDI ) == 0 ) )
		Bit = 0;			/* IRQ ON */

	/* If both ACIA have IRQ clear */
	else
		Bit = 1;			/* IRQ OFF */

	return Bit;
}


/*-----------------------------------------------------------------------*/
/**
 * Handle read from main MFP GPIP pins register (0xfffa01).
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
 * - Bit 7 is monochrome monitor detection signal and/or dma sound. On STE and TT it is
 *   also XORed with the DMA sound play bit. On Falcon it is only the DMA sound play bit
 *
 * When reading GPIP, output lines (DDR=1) should return the last value that was written,
 * only input lines (DDR=0) should be updated.
 */
void	MFP_GPIP_ReadByte_Main ( MFP_STRUCT *pMFP )
{
	uint8_t	gpip_new;

	M68000_WaitState(4);

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	gpip_new = pMFP->GPIP;

	/* Bit 7 */
	if ( MFP_Main_Compute_GPIP7() )
		gpip_new |= 0x80;		/* set bit 7 */
	else
		gpip_new &= ~0x80;		/* clear bit 7 */

	/* Bit 4 */
	if ( MFP_Main_Compute_GPIP_LINE_ACIA() )
		gpip_new |= 0x10;		/* set bit 4 */
	else
		gpip_new &= ~0x10;		/* clear bit 4 */

	/* Bit 0 */
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

	gpip_new &= ~pMFP->DDR;					/* New input bits */
	pMFP->GPIP = ( pMFP->GPIP & pMFP->DDR ) | gpip_new; 	/* Keep output bits unchanged and update input bits */

	IoMem[IoAccessCurrentAddress] = pMFP->GPIP;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read gpip %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from TT MFP GPIP pins register (0xfffa81)
 *
 *
 * When reading GPIP, output lines (DDR=1) should return the last value that was written,
 * only input lines (DDR=0) should be updated.
 */
void	MFP_GPIP_ReadByte_TT ( MFP_STRUCT *pMFP )
{
	uint8_t	gpip_new;

	M68000_WaitState(4);

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	/* TODO : handle all bits, bit 7 is scsi, bit 4 is DC signal, other bits default to 1 for now */
	gpip_new = pMFP->GPIP;
	gpip_new |= 0x6f;					/* force bits 0-3 and 5-6 to 1 */

	gpip_new &= ~pMFP->DDR;					/* New input bits */
	pMFP->GPIP = ( pMFP->GPIP & pMFP->DDR ) | gpip_new; 	/* Keep output bits unchanged and update input bits */

	IoMem[IoAccessCurrentAddress] = pMFP->GPIP;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read gpip %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Active Edge Register AER (0xfffa03 or 0xfffa83)
 */
void	MFP_ActiveEdge_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa03 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->AER;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read aer %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Data Direction Register DDR (0xfffa05 or 0xfffa85)
 */
void	MFP_DataDirection_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa05 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->DDR;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read ddr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Interrupt Enable Register A IERA (0xfffa07 or 0xfffa87)
 */
void	MFP_EnableA_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa07 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->IERA;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read iera %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Interrupt Enable Register B IERB (0xfffa09 or 0xfffa89)
 */
void	MFP_EnableB_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa09 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->IERB;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read ierb %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Interrupt Pending Register A IPRA (0xfffa0b or 0xfffa8b)
 */
void	MFP_PendingA_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa0b )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->IPRA;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read ipra %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Interrupt Pending Register B IPRB (0xfffa0d or 0xfffa8d)
 */
void	MFP_PendingB_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa0d )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->IPRB;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read iprb %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Interrupt In Service Register A ISRA (0xfffa0f or 0xfffa8f)
 */
void	MFP_InServiceA_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa0f )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->ISRA;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read isra %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Interrupt In Service Register B ISRB (0xfffa11 or 0xfffa91)
 */
void	MFP_InServiceB_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa11 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->ISRB;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read isrb %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Interrupt Mask Register A IMRA (0xfffa13 or 0xfffa93)
 */
void	MFP_MaskA_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa13 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->IMRA;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read imra %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Interrupt Mask Register B IMRB (0xfffa15 or 0xfffa95)
 */
void	MFP_MaskB_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa15 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->IMRB;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read imrb %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from MFP Vector Register VR (0xfffa17 or 0xfffa97)
 */
void	MFP_VectorReg_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa17 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->VR;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read vr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Timer A Control Register TACR (0xfffa19 or 0xfffa99)
 */
void	MFP_TimerACtrl_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa19 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->TACR;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read tacr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Timer B Control Register TBCR (0xfffa1b or 0Xfffa9b)
 */
void	MFP_TimerBCtrl_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa1b )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->TBCR;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read tbcr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Timer C/D Control Register TCDCR (0xfffa1d or 0xfffa9d)
 */
void	MFP_TimerCDCtrl_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa1d )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	IoMem[IoAccessCurrentAddress] = pMFP->TCDCR;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read tcdcr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Timer A Data Egister TADR (0xfffa1f or 0xfffa9f)
 */
void	MFP_TimerAData_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa1f )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( pMFP->TACR != 8 )				/* Is event count? Need to update counter */
		MFP_ReadTimerA ( pMFP , false );	/* Store result in 'TA_MAINCOUNTER' */

	IoMem[IoAccessCurrentAddress] = pMFP->TA_MAINCOUNTER;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read tadr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Timer B Data Register TBDR (0xfffa21 or 0xfffaa1)
 */
void MFP_TimerBData_ReadByte(void)
{
	MFP_STRUCT	*pMFP;
	uint8_t		TB_count;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa21 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	/* Is it event count mode or not? */
	if ( pMFP->TBCR != 8 )
	{
		/* Not event count mode, so handle as normal timer
		 * and store result in 'TB_MAINCOUNTER' */
		MFP_ReadTimerB ( pMFP , false );
	}
	else	/* Video DE signal is connected to Timer B on both MFPs */
	{
		if (bUseVDIRes)
		{
			/* HBLs are disabled in VDI mode, but TOS expects to read a 1 */
			pMFP->TB_MAINCOUNTER = 1;
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
			pos_start >>= nCpuFreqShift;

			/* Cycle position of the read for the current instruction (approximately, we consider */
			/* the read happens after 4 cycles (due to MFP wait states in that case)) */
			/* This is quite a hack, but hard to do without proper 68000 read cycle emulation */
			if ( CurrentInstrCycles <= 8 )			/* move.b (a0),d0 / cmp.b (a0),d0 ... */
				pos_read = pos_start + 4;		/* wait state */
			else						/* cmp.b $fa21.w,d0 (BIG Demo) ... */
				pos_read = pos_start + 8;		/* more time needed to compute the effective address */

			TB_count = pMFP->TB_MAINCOUNTER;		/* default value */

			/* If Timer B's change happens before the read cycle of the current instruction, we must return */
			/* the current value - 1 (because MFP_TimerB_EventCount_Interrupt was not called yet) */
			/* NOTE This is only needed when CpuRunCycleExact=false ; when CpuRunCycleExact=true, MFP_UpdateTimers() */
			/* was already called above and MFP_TimerB_EventCount_Interrupt should have been called too if needed */
			/* so TB_count should already be the correct value */
			if ( !CpuRunCycleExact
				&& (nHBL >= nStartHBL ) && ( nHBL < nEndHBL )	/* ensure display is ON and timer B can happen */
				&& ( LineTimerBPos > pos_start ) && ( LineTimerBPos < pos_read ) )
			{
				LOG_TRACE(TRACE_MFP_READ , "mfp%s read tbdr overlaps pos_start=%d TB_pos=%d pos_read=%d nHBL=%d \n",
						pMFP->NameSuffix , pos_start, LineTimerBPos, pos_read , HblCounterVideo );

				TB_count--;
				if ( TB_count == 0 )			/* going from 1 to 0 : timer restart, reload data reg */
					TB_count = pMFP->TBDR;
				/* Going from 0 to -1 : data reg is in fact going from 256 to 255. As TB_count is uint8_t, */
				/* this is already what we get when we decrement TB_count=0. So, the next 2 lines are redundant. */
	/*			else if ( TB_count < 0 )
					TB_count = 255;
	*/
			}

			LOG_TRACE(TRACE_MFP_READ , "mfp%s read tbdr data=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n" ,
						pMFP->NameSuffix , TB_count, FrameCycles, pos_start, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
			IoMem[IoAccessCurrentAddress] = TB_count;
			return;
		}
	}

	IoMem[IoAccessCurrentAddress] = pMFP->TB_MAINCOUNTER;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read tbdr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Timer C Data Register TCDR (0xfffa23 or 0xfffaa3)
 */
void	MFP_TimerCData_ReadByte(void)
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa23 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	MFP_ReadTimerC ( pMFP , false );		/* Store result in 'TC_MAINCOUNTER' */

	IoMem[IoAccessCurrentAddress] = pMFP->TC_MAINCOUNTER;

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read tcdr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle read from Timer D Data Register TDDR (0xfffa25 or 0xfffaa5)
 */
void	MFP_TimerDData_ReadByte ( void )
{
	MFP_STRUCT	*pMFP;
	uint32_t		pc = M68000_GetPC();

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa25 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before reading the register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	/* Special case for the main MFP when bPatchTimerD is used */
	/* NOTE : in TT mode TOS also starts useless timer D on the TT MFP, so we should restore */
	/* 0xfffa25/0xfffaa5 for Main MFP and TT MFP when bPatchTimerD is enabled */
	if ( ConfigureParams.System.bPatchTimerD && pc >= TosAddress && pc <= TosAddress + TosSize )
	{
		/* Trick the tos to believe TDDR was not changed */
		IoMem[IoAccessCurrentAddress] = pMFP->PatchTimerD_TDDR_old;
	}

	else
	{
		MFP_ReadTimerD ( pMFP , false );	/* Store result in 'TD_MAINCOUNTER' */
		IoMem[IoAccessCurrentAddress] = pMFP->TD_MAINCOUNTER;
	}

	if ( LOG_TRACE_LEVEL( TRACE_MFP_READ ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s read tddr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle write to GPIP register (0xfffa01 or 0xfffa81)
 *
 * Only line configured as output in DDR can be changed (0=input 1=output)
 * When reading GPIP, output lines should return the last value that was written,
 * only input lines should be updated.
 */
void	MFP_GPIP_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;
	uint8_t		GPIP_old;
	uint8_t		GPIP_new;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa01 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write gpip %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	GPIP_old = pMFP->GPIP;
	GPIP_new = IoMem[IoAccessCurrentAddress] & pMFP->DDR;	/* New output bits */
	pMFP->GPIP = ( GPIP_old & ~pMFP->DDR ) | GPIP_new;	/* Keep input bits unchanged and update output bits */

	/* Update possible interrupts after changing GPIP */
	MFP_GPIP_Update_Interrupt ( pMFP , GPIP_old , pMFP->GPIP , pMFP->AER , pMFP->AER , pMFP->DDR , pMFP->DDR );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Active Edge Register AER (0xfffa03 or 0xfffa83)
 *
 * Special case for bit 3 of main MFP (0xfffa03) :
 * Bit 3 of AER is linked to timer B in event count mode.
 *  - If bit 3=0, timer B triggers on end of line when display goes off.
 *  - If bit 3=1, timer B triggers on start of line when display goes on.
 */
void	MFP_ActiveEdge_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;
	uint8_t		AER_old;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa03 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write aer %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	AER_old = pMFP->AER;
	pMFP->AER = IoMem[IoAccessCurrentAddress];

	/* Update possible interrupts after changing AER */
	MFP_GPIP_Update_Interrupt ( pMFP , pMFP->GPIP , pMFP->GPIP , AER_old , pMFP->AER , pMFP->DDR , pMFP->DDR );

	/* Special case when changing bit 3 in main MFP : */
	/* Video DE signal is connected to Timer B on the main MFP */
	/* We need to update the position of the timer B interrupt for 'event count' mode */
	if ( IoAccessCurrentAddress == 0xfffa03 )
	{
		if ( ( AER_old & ( 1 << 3 ) ) != ( pMFP->AER & ( 1 << 3 ) ) )
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			int LineTimerBPos_old = LineTimerBPos;

			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

			/* 0 -> 1, timer B is now counting start of line events (cycle 56+28) */
			if ( ( AER_old & ( 1 << 3 ) ) == 0 )
			{
				LineTimerBPos = Video_TimerB_GetPos ( HblCounterVideo );

				LOG_TRACE((TRACE_VIDEO_HBL | TRACE_MFP_WRITE),
						"mfp/video aer bit 3 0->1, timer B triggers on start of line,"
						" old_pos=%d new_pos=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n",
						LineTimerBPos_old, LineTimerBPos,
						FrameCycles, LineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles);
			}

			/* 1 -> 0, timer B is now counting end of line events (cycle 376+28) */
			else if ( ( AER_old & ( 1 << 3 ) ) != 0 )
			{
				LineTimerBPos = Video_TimerB_GetPos ( HblCounterVideo );

				LOG_TRACE((TRACE_VIDEO_HBL | TRACE_MFP_WRITE),
						"mfp/video aer bit 3 1->0, timer B triggers on end of line,"
						" old_pos=%d new_pos=%d video_cyc=%d %d@%d pc=%x instr_cyc=%d\n",
						LineTimerBPos_old, LineTimerBPos,
						FrameCycles, LineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles);
			}

			/* Timer B position changed, update the next interrupt */
			if ( LineTimerBPos_old != LineTimerBPos )
				Video_AddInterruptTimerB ( HblCounterVideo , LineCycles , LineTimerBPos );
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle write to Data Direction Register DDR (0xfffa05 or 0xfffa85)
 */
void	MFP_DataDirection_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;
	uint8_t		DDR_old;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa05 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write ddr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	DDR_old = pMFP->DDR;
	pMFP->DDR = IoMem[IoAccessCurrentAddress];

	/* Update possible interrupts after changing DDR */
	MFP_GPIP_Update_Interrupt ( pMFP , pMFP->GPIP , pMFP->GPIP , pMFP->AER , pMFP->AER , DDR_old , pMFP->DDR );
}


/*-----------------------------------------------------------------------*/
/**
 * Handle write to Interrupt Enable Register A IERA (0xfffa07 or 0xfffa87)
 */
void	MFP_EnableA_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa07 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write iera %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->IERA = IoMem[IoAccessCurrentAddress];
	pMFP->IPRA &= pMFP->IERA;
	MFP_UpdateIRQ ( pMFP , Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Interrupt Enable Register B IERB (0xfffa09 or 0xfffa89)
 */
void	MFP_EnableB_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa09 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write ierb %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->IERB = IoMem[IoAccessCurrentAddress];
	pMFP->IPRB &= pMFP->IERB;
	MFP_UpdateIRQ ( pMFP , Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Interrupt Pending Register A IPRA (0xfffa0b or 0xfffa8b)
 */
void	MFP_PendingA_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa0b )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write ipra %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->IPRA &= IoMem[IoAccessCurrentAddress];		/* Cannot set pending bits - only clear via software */
	MFP_UpdateIRQ ( pMFP , Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Interrupt Pending Register B IPRB (0xfffa0d or 0xfffa8d)
 */
void	MFP_PendingB_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa0d )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write iprb %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->IPRB &= IoMem[IoAccessCurrentAddress];		/* Cannot set pending bits - only clear via software */
	MFP_UpdateIRQ ( pMFP , Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Interrupt In Service Register A ISRA (0xfffa0f or 0xfffa8f)
 */
void	MFP_InServiceA_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa0f )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write isra %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->ISRA &= IoMem[IoAccessCurrentAddress];		/* Cannot set in-service bits - only clear via software */
	MFP_UpdateIRQ ( pMFP , Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Interrupt In Service Register B ISRB (0xfffa11 or 0xfffa91).
 */
void	MFP_InServiceB_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa11 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write isrb %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->ISRB &= IoMem[IoAccessCurrentAddress];		/* Cannot set in-service bits - only clear via software */
	MFP_UpdateIRQ ( pMFP , Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Interrupt Mask Register A IMRA (0xfffa13 or 0xfffa93)
 */
void	MFP_MaskA_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa13 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write imra %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->IMRA = IoMem[IoAccessCurrentAddress];
	MFP_UpdateIRQ ( pMFP , Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Interrupt Mask Register B IMRB (0xfffa15 or 0xfffa95)
 */
void	MFP_MaskB_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa15 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write imrb %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->IMRB = IoMem[IoAccessCurrentAddress];
	MFP_UpdateIRQ ( pMFP , Cycles_GetClockCounterOnWriteAccess() );
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to MFP Vector Register (0xfffa17 or 0xfffa97)
 */
void	MFP_VectorReg_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;
	uint8_t		old_vr;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa17 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write vr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	old_vr = pMFP->VR;				/* Copy for checking if set mode */
	pMFP->VR = IoMem[IoAccessCurrentAddress];

	if ((pMFP->VR^old_vr) & 0x08)			/* Test change in end-of-interrupt mode */
	{
		/* Mode did change but was it to automatic mode ? (ie bit is a zero) */
		if (!(pMFP->VR & 0x08))
		{
			/* We are now in automatic mode, so clear all in-service bits */
			pMFP->ISRA = 0;
			pMFP->ISRB = 0;
			MFP_UpdateIRQ ( pMFP , Cycles_GetClockCounterOnWriteAccess() );
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Timer A Control Register TACR (0xfffa19 or 0xfffa99)
 */
void	MFP_TimerACtrl_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;
	uint8_t		new_tacr;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa19 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write tacr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	new_tacr = IoMem[IoAccessCurrentAddress] & 0x0f;  /* FIXME : ignore bit 4 (reset) ? */

	if ( pMFP->TACR != new_tacr )			/* Timer control changed */
	{
		/* If we stop a timer which was in delay mode, we need to store
		 * the current value of the counter to be able to read it or to
		 * continue from where we left if the timer is restarted later
		 * without writing to the data register. */
		if ((new_tacr == 0) && (pMFP->TACR >=1) && (pMFP->TACR <= 7))
			MFP_ReadTimerA ( pMFP , true);	/* Store result in 'TA_MAINCOUNTER' */

		pMFP->TACR = new_tacr;			/* set to new value before calling MFP_StartTimer */
		MFP_StartTimerA ( pMFP );		/* start/stop timer depending on control reg */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Timer B Control Register TBCR (0xfffa1b or 0xfffa9b)
 */
void MFP_TimerBCtrl_WriteByte(void)
{
	MFP_STRUCT	*pMFP;
	uint8_t		new_tbcr;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa1b )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write tbcr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	new_tbcr = IoMem[IoAccessCurrentAddress] & 0x0f;  /* FIXME : ignore bit 4 (reset) ? */

	if ( pMFP->TBCR != new_tbcr )			/* Timer control changed */
	{
		/* If we stop a timer which was in delay mode, we need to store
		 * the current value of the counter to be able to read it or to
		 * continue from where we left if the timer is restarted later
		 * without writing to the data register. */
		if ((new_tbcr == 0) && (pMFP->TBCR >=1) && (pMFP->TBCR <= 7))
			MFP_ReadTimerB ( pMFP , true);	/* Store result in 'TA_MAINCOUNTER' */

		pMFP->TBCR = new_tbcr;			/* set to new value before calling MFP_StartTimer */
		MFP_StartTimerB ( pMFP );		/* start/stop timer depending on control reg */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to timer C/D control register (0xfffa1d).
 */
void	MFP_TimerCDCtrl_WriteByte(void)
{
	MFP_STRUCT	*pMFP;
	uint8_t		new_tcdcr;
	uint8_t		old_tcdcr;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa1d )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write tcdcr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	new_tcdcr = IoMem[IoAccessCurrentAddress];
	old_tcdcr = pMFP->TCDCR;
//fprintf ( stderr , "write fa1d new %x old %x\n" , IoMem[IoAccessCurrentAddress] , pMFP->TCDCR );

	if ((old_tcdcr & 0x70) != (new_tcdcr & 0x70))	/* Timer C control changed */
	{
		/* If we stop a timer which was in delay mode, we need to store
		 * the current value of the counter to be able to read it or to
		 * continue from where we left if the timer is restarted later
		 * without writing to the data register. */
		if ((new_tcdcr & 0x70) == 0)
			MFP_ReadTimerC ( pMFP , true );	/* Store result in 'TC_MAINCOUNTER' */

		pMFP->TCDCR = ( new_tcdcr & 0x70 ) | ( old_tcdcr & 0x07 );	/* we set TCCR and keep old TDCR in case we need to read it below */
		MFP_StartTimerC ( pMFP );		/* start/stop timer depending on control reg */
	}

	if ((old_tcdcr & 0x07) != (new_tcdcr & 0x07))	/* Timer D control changed */
	{
		uint32_t pc = M68000_GetPC();

		/* Special case for main MFP and TT MFP when bPatchTimerD is used */
		if (ConfigureParams.System.bPatchTimerD && !pMFP->PatchTimerD_Done
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
				new_tcdcr = IoMem[IoAccessCurrentAddress] = (IoMem[IoAccessCurrentAddress] & 0xf0) | 7;
				pMFP->PatchTimerD_Done = 1;
			}
		}

		/* Special case for the main MFP when RS232 is enabled */
		if ( IoAccessCurrentAddress == 0xfffa1d )
		{
			/* Need to change baud rate of RS232 emulation? */
			if (ConfigureParams.RS232.bEnableRS232)
			{
				RS232_SetBaudRateFromTimerD();
			}
		}

		/* If we stop a timer which was in delay mode, we need to store
		 * the current value of the counter to be able to read it or to
		 * continue from where we left if the timer is restarted later
		 * without writing to the data register. */
		if ((new_tcdcr & 0x07) == 0)
			MFP_ReadTimerD ( pMFP , true );	/* Store result in 'TD_MAINCOUNTER' */

		pMFP->TCDCR = new_tcdcr;		/* set to new value before calling MFP_StartTimer */
		MFP_StartTimerD ( pMFP );		/* start/stop timer depending on control reg */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Timer A Data Register TADR (0xfffa1f or 0xfffa9f)
 */
void	MFP_TimerAData_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa1f )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write tadr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->TADR = IoMem[IoAccessCurrentAddress];	/* Store into data register */

	if ( pMFP->TACR == 0 )				/* Now check if timer is running - if so do not set */
	{
		pMFP->TA_MAINCOUNTER = pMFP->TADR;	/* Timer is off, store to main counter */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Timer B Data Register TBDR (0xfffa21 or 0xfffaa1)
 */
void	MFP_TimerBData_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa21 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write tbdr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->TBDR = IoMem[IoAccessCurrentAddress];	/* Store into data register */

	if ( pMFP->TBCR == 0 )				/* Now check if timer is running - if so do not set */
	{
		pMFP->TB_MAINCOUNTER = pMFP->TBDR;	/* Timer is off, store to main counter */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Timer C Data Register TCDR (0xfffa23 or 0xfffaa3)
 */
void	MFP_TimerCData_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa23 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write tcdr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	pMFP->TCDR = IoMem[IoAccessCurrentAddress];	/* Store into data register */

	if ( (pMFP->TCDCR & 0x70) == 0 )		/* Now check if timer is running - if so do not set */
	{
		pMFP->TC_MAINCOUNTER = pMFP->TCDR;	/* Timer is off, store to main counter */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Handle write to Timer D Data Register TDDR (0xfffa25 or 0xfffaa5)
 */
void	MFP_TimerDData_WriteByte ( void )
{
	MFP_STRUCT	*pMFP;
	uint32_t		pc = M68000_GetPC();

	M68000_WaitState(4);

	if ( IoAccessCurrentAddress == 0xfffa25 )
		pMFP = pMFP_Main;
	else
		pMFP = pMFP_TT;

	/* Update timers' state before writing to register */
	MFP_UpdateTimers ( pMFP , Cycles_GetClockCounterImmediate() );

	if ( LOG_TRACE_LEVEL( TRACE_MFP_WRITE ) )
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("mfp%s write tddr %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
			pMFP->NameSuffix , IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
			FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles );
	}

	/* Patch Timer D for better performance ? */
	/* NOTE : in TT mode TOS also starts useless timer D on the TT MFP, so we should patch */
	/* Main MFP and TT MFP when bPatchTimerD is enabled */
	if ( ConfigureParams.System.bPatchTimerD && pc >= TosAddress && pc <= TosAddress + TosSize )
	{
		pMFP->PatchTimerD_TDDR_old = IoMem[IoAccessCurrentAddress];
		IoMem[IoAccessCurrentAddress] = PATCH_TIMER_TDDR_FAKE;	/* Slow down the useless Timer D setup from the bios */
	}

	if ( IoAccessCurrentAddress == 0xfffa25 )
	{
		/* Need to change baud rate of RS232 emulation ? */
		if ( ConfigureParams.RS232.bEnableRS232 && ( IoMem[0xfffa1d] & 0x07 ) )
		{
			RS232_SetBaudRateFromTimerD();
		}
	}

	pMFP->TDDR = IoMem[IoAccessCurrentAddress];	/* Store into data register */

	if ( (pMFP->TCDCR & 0x07) == 0 )		/* Now check if timer is running - if so do not set */
	{
		pMFP->TD_MAINCOUNTER = pMFP->TDDR;	/* Timer is off, store to main counter */
	}
}


static void MFP_Show(FILE *fp, MFP_STRUCT *mfp)
{
	fprintf(fp, "General Purpose Pins:    0x%02x\n", mfp->GPIP);
	fprintf(fp, "Active Edge:             0x%02x\n", mfp->AER);
	fprintf(fp, "Data Direction:          0x%02x\n", mfp->DDR);
	fprintf(fp, "Interrupt A Enable:      0x%02x\n", mfp->IERA);
	fprintf(fp, "Interrupt B Enable:      0x%02x\n", mfp->IERB);
	fprintf(fp, "Interrupt A Pending:     0x%02x\n", mfp->IPRA);
	fprintf(fp, "Interrupt B Pending:     0x%02x\n", mfp->IPRB);
	fprintf(fp, "Interrupt A In-Service:  0x%02x\n", mfp->ISRA);
	fprintf(fp, "Interrupt B In-Service:  0x%02x\n", mfp->ISRB);
	fprintf(fp, "Interrupt A Mask:        0x%02x\n", mfp->IMRA);
	fprintf(fp, "Interrupt B Mask:        0x%02x\n", mfp->IMRB);
	fprintf(fp, "Vector:                  0x%02x\n", mfp->VR);
	fprintf(fp, "Timer A Control:         0x%02x\n", mfp->TACR);
	fprintf(fp, "Timer B Control:         0x%02x\n", mfp->TBCR);
	fprintf(fp, "Timer C/D Control:       0x%02x\n", mfp->TCDCR);
	fprintf(fp, "Timer A Data:            0x%02x\n", mfp->TADR);
	fprintf(fp, "Timer B Data:            0x%02x\n", mfp->TBDR);
	fprintf(fp, "Timer C Data:            0x%02x\n", mfp->TCDR);
	fprintf(fp, "Timer D Data:            0x%02x\n", mfp->TDDR);
	fprintf(fp, "Synchronous Data:        0x%02x\n", mfp->SCR);
	fprintf(fp, "USART Control:           0x%02x\n", mfp->UCR);
	fprintf(fp, "Receiver Status:         0x%02x\n", mfp->RSR);
	fprintf(fp, "Transmitter Status:      0x%02x\n", mfp->TSR);
	fprintf(fp, "USART Data:              0x%02x\n", mfp->UDR);
	fprintf(fp, "IRQ signal:              0x%02x\n", mfp->IRQ);
	fprintf(fp, "Input signal on Timer A: 0x%02x\n", mfp->TAI);
	fprintf(fp, "Input signal on Timer B: 0x%02x\n", mfp->TBI);
}

void MFP_Info(FILE *fp, uint32_t dummy)
{
	MFP_Show(fp, pMFP_Main);
	if (Config_IsMachineTT())
	{
		fprintf(fp, "TT-MFP:\n");
		MFP_Show(fp, pMFP_TT);
	}
}
