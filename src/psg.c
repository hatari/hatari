/*
  Hatari - psg.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Programmable Sound Generator (YM-2149) - PSG

  Also used for the printer (centronics) port emulation (PSG Port B, Register 15)
*/


/* 2007/04/14	[NP]	First approximation to get cycle accurate accesses to ff8800/02	*/
/*			by cumulating wait state of 1 cycle and rounding the final	*/
/*			result to 4.							*/
/* 2007/04/29	[NP]	Functions PSG_Void_WriteByte and PSG_Void_ReadByte to handle	*/
/*			accesses to $ff8801/03. These addresses have no effect, but	*/
/*			they must give a 1 cycle wait state (e.g. move.l d0,ff8800).	*/
/* 2007/09/29	[NP]	Replace printf by calls to HATARI_TRACE.			*/
/* 2007/10/23	[NP]	In PSG_Void_WriteByte, add a wait state only if no wait state	*/
/*			were added so far (hack, but gives good result).		*/
/* 2007/11/18	[NP]	In PSG_DataRegister_WriteByte, set unused bit to 0, in case	*/
/*			the data reg is read later (fix Mindbomb Demo / BBC).		*/
/* 2008/04/20	[NP]	In PSG_DataRegister_WriteByte, set unused bit to 0 for register	*/
/*			6 too (noise period).						*/
/* 2008/07/27	[NP]	Better separation between accesses to the YM hardware registers	*/
/*			and the sound rendering routines. Use Sound_WriteReg() to pass	*/
/*			all writes to the sound rendering functions. This allows to	*/
/*			have sound.c independent of psg.c (to ease replacement of	*/
/*			sound.c	by another rendering method).				*/
/* 2008/08/11	[NP]	Set drive leds.							*/
/* 2008/10/16	[NP]	When writing to $ff8800, register select should not be masked	*/
/*			with 0xf, it's a real 8 bits register where all bits are	*/
/*			significant. This means only value <16 should be considered as	*/
/*			valid register selection. When reg select is >= 16, all writes	*/
/*			and reads in $ff8802 should be ignored.				*/
/*			(fix European Demo Intro, which sets addr reg to 0x10 when	*/
/*			sample playback is disabled).					*/
/* 2008/12/21	[NP]	After testing different cases on a real STF, rewrite registers	*/
/*			handling. As only pins BC1 and BDIR are used in an Atari to	*/
/*			address the YM2149, this means only 1 bit is necessary to access*/
/*			select/data registers. Other variations of the $ff88xx addresses*/
/*			will point to either $ff8800 or $ff8802. Only bit 1 of $ff88xx	*/
/*			is useful to know which register is accessed in the YM2149.	*/
/*			So, it's possible to access the YM2149 with $ff8801 and $ff8803	*/
/*			but under conditions : the write to a shadow address (bit 0=1)	*/
/*			can't be made by an instruction that writes to the same address	*/
/*			with bit 0=0 at the same time (.W or .L access).		*/
/*			In that case, only the address with bit 0=0 is taken into	*/
/*			account. This means a write to $ff8801/03 will succeed only if	*/
/*			the access size is .B (byte) or the opcode is a movep (because	*/
/*			in that case we won't access the same register with 2 different	*/
/*			addresses) (fix the game X-Out, which uses movep.w to write to	*/
/*			$ff8801/03).							*/
/*			Refactorize some code for cleaner handling of these accesses.	*/
/*			Only reads to $ff8800 will return a data, reads to $ff8801/02/03*/
/*			always return 0xff (tested on STF).				*/
/*			When PSGRegisterSelect > 15, reads to $ff8800 also return 0xff.	*/
/* 2009/01/24	[NP]	Remove redundant test, as movep implies SIZE_BYTE access.	*/
/* 2011/10/30	[NP]	There's a special case when reading a register from $ff8800 :	*/
/*			if the register number was not changed since the last write,	*/
/*			then we must return the value that was written to $ff8802	*/
/*			without masking the unused bit (fix the game Murders In Venice,	*/
/*			which expects to read $10 from reg 3).				*/
/* 2015/10/15	[NP]	Better handling of the wait states when accessing YM2149 regs.	*/
/*			Replace M68000_WaitState(1) by PSG_WaitState() which adds	*/
/*			4 cycles every 4th access. Previous method worked because all	*/
/*			cycles were rounded to 4, but it was not how real HW works and	*/
/*			would not work in cycle exact mode where cycles are not rounded.*/


/*
  YM2149 Software-Controlled Sound Generator / Programmable Sound Generator

  References :
   - YM2149 datasheet by Yamaha (1987)


                                               -----------
                                      VSS/GND -| 1    40 |- VCC
                                           NC -| 2    39 |- TEST1 : not connected
                             ANALOG CHANNEL B -| 3    38 |- ANALOG CHANNEL C
                             ANALOG CHANNEL A -| 4    37 |- DA0
                                           NC -| 5    36 |- DA1
         IOB7 : connected to parallel port D7 -| 6    35 |- DA2
         IOB6 : connected to parallel port D6 -| 7    34 |- DA3
         IOB5 : connected to parallel port D5 -| 8    33 |- DA4
         IOB4 : connected to parallel port D4 -| 9    32 |- DA5
         IOB3 : connected to parallel port D3 -| 10   31 |- DA6
         IOB2 : connected to parallel port D2 -| 11   30 |- DA7
         IOB1 : connected to parallel port D1 -| 12   29 |- BC1
         IOB0 : connected to parallel port D0 -| 13   28 |- BC2 : connected to VCC
                         IOA7 : not connected -| 14   27 |- BDIR
                      IOA6 : connected to GPO -| 15   26 |- SEL(INV) : not connected
     IOA5 : connected to parallel port STROBE -| 16   25 |- A8 : connected to VCC
          IOA4 : connected to RS232C port DTR -| 17   24 |- A9(INV) : connected to VSS/GND
          IOA3 : connected to RS232C port RTS -| 18   23 |- RESET(INV)
  IOA2 : connected to floppy drive 1 'select' -| 19   22 |- CLOCK : connected to 2 MHz
  IOA1 : connected to floppy drive 0 'select' -| 20   21 |- IOA0 : connected to floppy drives 'side select'
                                               -----------

  Registers :
    0xff8800.b	Set address register (write) / Read content of selected data register (read)
    0xff8802.b	Write into selected data register (write) / No action, return 0xFF (read)

    Note that under certain conditions 0xff8801 can be accessed as a shadow version of 0xff8800.
    Similarly, 0xff8803 can be used instead of 0xff8802.
    Also, only bits 0 and 1 of addresses 0xff88xx are used to access the YM2149, which means
    the region 0xff8804 - 0xff88ff can be used to access 0xff8800 - 0xff8803 (with a special
    case for Falcon when not in ST compatible mode)

*/



/* Emulating wait states when accessing $ff8800/01/02/03 with different 'move' variants	*/
/* is a complex task. So far, adding 1 cycle wait state to each access and rounding the	*/
/* final number to 4 gave some good results, but this is certainly not the way it's	*/
/* working for real in the ST.								*/
/* Also in Hatari it only works when the cpu rounds all cycles to the next multiple	*/
/* of 4, but it will not work when running in cycle exact mode. This means we must	*/
/* add 4 cycles at a time, but not on every register access, see below.		*/
/*											*/
/* The following examples show some verified wait states for different accesses :	*/
/*	lea     $ffff8800,a1								*/
/*	lea     $ffff8802,a2								*/
/*	lea     $ffff8801,a3								*/
/*											*/
/*	movep.w d1,(a1)         ; 20 16+4       (ventura loader)			*/
/*	movep.l d1,(a1)         ; 28 24+4       (ventura loader, ulm loader)		*/
/*											*/
/*	movep.l d6,0(a5)        ; 28 24+4       (SNY I, TCB)				*/
/*	movep.w d5,0(a5)        ; 20 16+4       (SNY I, TCB)				*/
/*											*/
/*	move.b d1,(a1)          ; 12 8+4						*/
/*	move.b d1,(a2)          ; 12 8+4						*/
/*	move.b d1,(a3)          ; 12 8+4        (crickey ulm hidden)			*/
/*											*/
/*	move.w d1,(a1)          ; 12 8+4						*/
/*	move.w d1,(a2)          ; 12 8+4						*/
/*	move.l d1,(a1)          ; 16 12+4       (ulm loader)				*/
/*											*/
/*	movem.l d1,(a1)         ; 20 16+4						*/
/*	movem.l d1-d2,(a1)      ; 28 24+4						*/
/*	movem.l d1-d3,(a1)      ; 40 32+4+4						*/
/*	movem.l d1-d4,(a1)      ; 48 40+4+4						*/
/*	movem.l d1-d5,(a1)      ; 60 48+4+4+4						*/
/*	movem.l d1-d6,(a1)      ; 68 56+4+4+4						*/
/*	movem.l d1-d7,(a1)      ; 80 64+4+4+4+4						*/
/*	movem.l d0-d7,(a1)      ; 88 72+4+4+4+4						*/
/*											*/
/*	movep.w	d0,(a3)				(X-Out)					*/
/*											*/
/*	clr.b (a1)		; 20 12+8	(4 for read + 4 for write)		*/
/*	tas (a1)		; 16 		(no waitstate ?)			*/
/*											*/
/*											*/
/* This gives the following "model" :							*/
/*	- each instruction accessing a valid YM2149 register gets an initial 4 cycle	*/
/*	  wait state for the 1st access (whether it accesses just 1 reg (eg move.b)	*/
/*	  or up to 4 regs (movep.l)).							*/
/*	- susbequent accesses made by the same instruction don't add more wait state	*/
/*	  (except if the instruction is a MOVEM).					*/
/*	- MOVEM can access more than 4 regs (up to 15) : in that case we add 4 extra	*/
/*	  cycles each time we access a 4th register (eg : regs 4,8,12, ...)		*/
/*	- accesses to $ff8801 or $ff8803 are considered "valid" only if we don't access	*/
/*	  the corresponding "non shadow" addresses $ff8800/02 at the same time (ie with	*/
/*	  the same instruction).							*/
/*	  This means only .B size (move.b for example) or movep opcode will work.	*/
/*	  If the access is valid, add 4 cycle wait state when necessary, else ignore	*/
/*	  the write and	don't add any cycle.						*/



const char PSG_fileid[] = "Hatari psg.c";

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "joy.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "sound.h"
#include "printer.h"            /* because Printer I/O goes through PSG Register 15 */
#include "psg.h"
#if ENABLE_DSP_EMU
#include "falcon/dsp.h"
#endif
#include "video.h"
#include "statusbar.h"
#include "mfp.h"
#include "fdc.h"
#include "scc.h"


static uint8_t PSGRegisterSelect;		/* Write to 0xff8800 sets the register number used in read/write accesses */
static uint8_t PSGRegisterReadData;	/* Value returned when reading from 0xff8800 */
uint8_t PSGRegisters[MAX_PSG_REGISTERS];	/* Registers in PSG, see PSG_REG_xxxx */

static unsigned int LastStrobe=0; /* Falling edge of Strobe used for printer */


/*-----------------------------------------------------------------------*/
/**
 * Reset variables used in PSG
 */
void PSG_Reset(void)
{
	int i;

	if (LOG_TRACE_LEVEL(TRACE_PSG_WRITE))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("ym reset video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
		                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	PSGRegisterSelect = 0;
	PSGRegisterReadData = 0;
	memset(PSGRegisters, 0, sizeof(PSGRegisters));
	PSGRegisters[PSG_REG_IO_PORTA] = 0xff;			/* no drive selected + side 0 after a reset */

	/* Update sound's emulation registers */
	for (i = 0; i < NUM_PSG_SOUND_REGISTERS; i++)
		Sound_WriteReg ( i , 0 );

	LastStrobe=0;
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void PSG_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&PSGRegisterSelect, sizeof(PSGRegisterSelect));
	MemorySnapShot_Store(&PSGRegisterReadData, sizeof(PSGRegisterReadData));
	MemorySnapShot_Store(PSGRegisters, sizeof(PSGRegisters));
	MemorySnapShot_Store(&LastStrobe, sizeof(LastStrobe));
}


/*-----------------------------------------------------------------------*/
/**
 * Write byte to the YM address register (usually 0xff8800). This is used
 * as a selector for when we read/write the YM data register (0xff8802).
 */
void PSG_Set_SelectRegister(uint8_t val)
{
	/* Store register used to read/write in $ff8802. This register */
	/* is 8 bits on the YM2149, this means it should not be masked */
	/* with 0xf. Instead, we keep the 8 bits, but we must ignore */
	/* read/write to ff8802 when PSGRegisterSelect >= 16 */
	PSGRegisterSelect = val;

	/* When address register is changed, a read from $ff8800 should */
	/* return the masked value of the register. We set the value here */
	/* to be returned in case PSG_Get_DataRegister is called */
	PSGRegisterReadData = PSGRegisters[PSGRegisterSelect];

	if (LOG_TRACE_LEVEL(TRACE_PSG_WRITE))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("ym write reg=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
		                PSGRegisterSelect, FrameCycles, LineCycles, HblCounterVideo,
		                M68000_GetPC(), CurrentInstrCycles);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Read byte from 0xff8800, return PSG data
 */
uint8_t PSG_Get_DataRegister(void)
{
	/* Is a valid PSG register currently selected ? */
	if ( PSGRegisterSelect >= MAX_PSG_REGISTERS )
		return 0xff;				/* not valid, return 0xff */

	if (PSGRegisterSelect == PSG_REG_IO_PORTA)
	{
		/* Second parallel port joystick uses centronics strobe bit as fire button: */
		if (ConfigureParams.Joysticks.Joy[JOYID_PARPORT2].nJoystickMode != JOYSTICK_DISABLED)
		{
			if (Joy_GetStickData(JOYID_PARPORT2) & 0x80)
				PSGRegisters[PSG_REG_IO_PORTA] &= ~32;
			else
				PSGRegisters[PSG_REG_IO_PORTA] |= 32;
		}
	}
	else if (PSGRegisterSelect == PSG_REG_IO_PORTB)
	{
		/* PSG register 15 is parallel port data register - used by parallel port joysticks: */
		if (ConfigureParams.Joysticks.Joy[JOYID_PARPORT1].nJoystickMode != JOYSTICK_DISABLED)
		{
			PSGRegisters[PSG_REG_IO_PORTB] &= 0x0f;
			PSGRegisters[PSG_REG_IO_PORTB] |= ~Joy_GetStickData(JOYID_PARPORT1) << 4;
		}
		if (ConfigureParams.Joysticks.Joy[JOYID_PARPORT2].nJoystickMode != JOYSTICK_DISABLED)
		{
			PSGRegisters[PSG_REG_IO_PORTB] &= 0xf0;
			PSGRegisters[PSG_REG_IO_PORTB] |= ~Joy_GetStickData(JOYID_PARPORT2) & 0x0f;
		}
	}

	/* Read data last selected by register */
	return PSGRegisterReadData;
}


/*-----------------------------------------------------------------------*/
/**
 * Write byte to YM's register (0xff8802), store according to PSG select register (0xff8800)
 */
void PSG_Set_DataRegister(uint8_t val)
{
	uint8_t	val_old;

	if (LOG_TRACE_LEVEL(TRACE_PSG_WRITE))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("ym write data reg=0x%x val=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
		                PSGRegisterSelect, val, FrameCycles, LineCycles,
		                HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	/* Is a valid PSG register currently selected ? */
	if ( PSGRegisterSelect >= MAX_PSG_REGISTERS )
		return;					/* not valid, ignore write and do nothing */

	/* Create samples up until this point with current values */
	Sound_Update ( Cycles_GetClockCounterOnWriteAccess() );

	/* When a read is made from $ff8800 without changing PSGRegisterSelect, we should return */
	/* the non masked value. */
	PSGRegisterReadData = val;			/* store non masked value for PSG_Get_DataRegister */

	/* Read previous content */
	val_old = PSGRegisters[PSGRegisterSelect];

	/* Copy value to PSGRegisters[] */
	PSGRegisters[PSGRegisterSelect] = val;

	/* Clear unused bits for some regs */
	if ( ( PSGRegisterSelect == PSG_REG_CHANNEL_A_COARSE ) || ( PSGRegisterSelect == PSG_REG_CHANNEL_B_COARSE )
		|| ( PSGRegisterSelect == PSG_REG_CHANNEL_C_COARSE ) || ( PSGRegisterSelect == PSG_REG_ENV_SHAPE ) )
	  PSGRegisters[PSGRegisterSelect] &= 0x0f;	/* only keep bits 0 - 3 */

	else if ( ( PSGRegisterSelect == PSG_REG_CHANNEL_A_AMP ) || ( PSGRegisterSelect == PSG_REG_CHANNEL_B_AMP )
		|| ( PSGRegisterSelect == PSG_REG_CHANNEL_C_AMP ) || ( PSGRegisterSelect == PSG_REG_NOISE_GENERATOR ) )
	  PSGRegisters[PSGRegisterSelect] &= 0x1f;	/* only keep bits 0 - 4 */


	if ( PSGRegisterSelect < NUM_PSG_SOUND_REGISTERS )
	{
		/* Copy sound related registers 0..13 to the sound module's internal buffer */
		Sound_WriteReg ( PSGRegisterSelect , PSGRegisters[PSGRegisterSelect] );
	}

	else if ( PSGRegisterSelect == PSG_REG_IO_PORTA )
	{
	/*
	 * FIXME: This is only a prelimary dirty hack!
	 * Port B (Printer port) - writing here needs to be dispatched to the printer
	 * STROBE (Port A bit5) does a short LOW and back to HIGH when the char is valid
	 * To print you need to write the character byte to IOB and you need to toggle STROBE
	 * (like EmuTOS does).
	 */
		/* Printer dispatching only when printing is activated */
		if (ConfigureParams.Printer.bEnablePrinting)
		{
			/* Bit 5 - Centronics strobe? If STROBE is low and the LastStrobe was high,
					then print/transfer to the emulated Centronics port.
			 */
			if (LastStrobe && ( (PSGRegisters[PSG_REG_IO_PORTA]&(1<<5)) == 0 ))
			{
				/* Seems like we want to print something... */
				Printer_TransferByteTo(PSGRegisters[PSG_REG_IO_PORTB]);
				/* Initiate a possible GPIP0 Printer BUSY interrupt */
				MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE0 , MFP_GPIP_STATE_LOW );

				/* Initiate a possible GPIP1 Falcon ACK interrupt */
				if (Config_IsMachineFalcon())
					MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE1 , MFP_GPIP_STATE_LOW );
			}
		}
		LastStrobe = PSGRegisters[PSG_REG_IO_PORTA]&(1<<5);

		/* Bit 0-2 : side and drive select */
		if ( (PSGRegisters[PSG_REG_IO_PORTA]&(1<<1)) == 0 )
		{
			/* floppy drive A is ON */
			Statusbar_SetFloppyLed(DRIVE_LED_A, LED_STATE_ON);
		}
		else
		{
			Statusbar_SetFloppyLed(DRIVE_LED_A, LED_STATE_OFF);
		}
		if ( (PSGRegisters[PSG_REG_IO_PORTA]&(1<<2)) == 0 )
		{
			/* floppy drive B is ON */
			Statusbar_SetFloppyLed(DRIVE_LED_B, LED_STATE_ON);
		}
		else
		{
			Statusbar_SetFloppyLed(DRIVE_LED_B, LED_STATE_OFF);
		}

		/* Report a possible drive/side change */
		FDC_SetDriveSide ( val_old & 7 , PSGRegisters[PSG_REG_IO_PORTA] & 7 );

		/* Handle MegaSTE / TT specific bit 7 in PORTA of the PSG */
		if (Config_IsMachineMegaSTE() || Config_IsMachineTT())
		{
			SCC_Check_Lan_IsEnabled ();
		}

		/* Handle Falcon specific bits in PORTA of the PSG */
		if (Config_IsMachineFalcon())
		{
			/* Bit 3 - centronics port SELIN line (pin 17) */
			/*
			if (PSGRegisters[PSG_REG_IO_PORTA] & (1 << 3))
			{
				// not emulated yet
			}
			*/

			/* Bit 4 - DSP reset? */
			if(PSGRegisters[PSG_REG_IO_PORTA]&(1<<4))
			{
				Log_Printf(LOG_DEBUG, "Calling DSP_Reset?\n");
#if ENABLE_DSP_EMU
				if (ConfigureParams.System.nDSPType == DSP_TYPE_EMU) {
					DSP_Reset();
				}
#endif
			}
			/* Bit 6 - Internal Speaker control */
			if(PSGRegisters[PSG_REG_IO_PORTA]&(1<<6))
			{
				/*Log_Printf(LOG_DEBUG, "Falcon: Internal Speaker state\n");*/
				/* FIXME: add code to handle? (if we want to emulate the speaker at all? */
			}
			/* Bit 7 - Reset IDE? */
			if(PSGRegisters[PSG_REG_IO_PORTA]&(1<<7))
			{
				Log_Printf(LOG_DEBUG, "Falcon: Reset IDE subsystem\n");
				/* FIXME: add code to handle IDE reset */
			}
		}

	}
}


/*-----------------------------------------------------------------------*/
/**
 * Handle wait state when accessing YM2149 registers
 * - each instruction accessing YM2149 gets an initial 4 cycle wait state
 *   for the 1st access (whether it accesses just 1 reg (eg move.b) or up to 4 regs (movep.l))
 * - special case for movem which can access more than 4 regs (up to 15) :
 *   we add 4 extra cycles each time we access a 4th reg (eg : regs 4,8,12, ...)
 *
 * See top of this file for several examples measured on real STF
 */
static void PSG_WaitState(void)
{
#if 0
	M68000_WaitState(1);				/* [NP] FIXME not 100% accurate, but gives good results */
#else
	static uint64_t	PSG_InstrPrevClock;
	static int	NbrAccesses;

	if ( PSG_InstrPrevClock != CyclesGlobalClockCounter )	/* New instruction accessing YM2149 : add 4 cycles */
	{
		M68000_WaitState ( 4 );
		PSG_InstrPrevClock = CyclesGlobalClockCounter;
		NbrAccesses = 0;
	}

	else							/* Same instruction doing several accesses : only movem can add more cycles */
	{
		if ( ( OpcodeFamily == i_MVMEL ) || ( OpcodeFamily == i_MVMLE ) )
		{
			NbrAccesses += 1;
			if ( NbrAccesses % 4 == 0 )		/* Add 4 extra cycles every 4th access */
				M68000_WaitState ( 4 );
		}
	}

#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Read byte from 0xff8800. Return current content of data register
 */
void PSG_ff8800_ReadByte(void)
{
	PSG_WaitState();

	IoMem[IoAccessCurrentAddress] = PSG_Get_DataRegister();

	if (LOG_TRACE_LEVEL(TRACE_PSG_READ))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("ym read data %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
		                IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
		                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Read byte from 0xff8801/02/03. Return 0xff.
 */
void PSG_ff880x_ReadByte(void)
{
	PSG_WaitState();

	IoMem[IoAccessCurrentAddress] = 0xff;

	if (LOG_TRACE_LEVEL(TRACE_PSG_READ))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("ym read void %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
		                IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
		                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}
}



/*-----------------------------------------------------------------------*/
/**
 * Write byte to 0xff8800. Set content of YM's address register.
 */
void PSG_ff8800_WriteByte(void)
{
	PSG_WaitState();

	if (LOG_TRACE_LEVEL(TRACE_PSG_WRITE))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("ym write %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
		                IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
		                FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	PSG_Set_SelectRegister ( IoMem[IoAccessCurrentAddress] );
}


/*-----------------------------------------------------------------------*/
/**
 * Write byte to 0xff8801. Set content of YM's address register under conditions.
 * Address 0xff8801 is a shadow version of 0xff8800, so both addresses can't be written
 * at the same time by the same instruction. This means only a .B access or
 * a movep will have a valid effect, other accesses are ignored.
 */
void PSG_ff8801_WriteByte(void)
{
	if ( nIoMemAccessSize == SIZE_BYTE )		/* byte access or movep */
	{	
		PSG_WaitState();
	
		if (LOG_TRACE_LEVEL(TRACE_PSG_WRITE))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("ym write %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
					IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
					FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
		}
	
		PSG_Set_SelectRegister ( IoMem[IoAccessCurrentAddress] );
	}

	else
	{						/* do nothing, just a trace if needed */
		if (LOG_TRACE_LEVEL(TRACE_PSG_WRITE))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("ym write ignored %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
					IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
					FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Write byte to 0xff8802. Set content of YM's data register.
 */
void PSG_ff8802_WriteByte(void)
{
	PSG_WaitState();

	if (LOG_TRACE_LEVEL(TRACE_PSG_WRITE))
	{
		int FrameCycles, HblCounterVideo, LineCycles;
		Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
		LOG_TRACE_PRINT("ym write %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
				IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
				FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
	}

	PSG_Set_DataRegister ( IoMem[IoAccessCurrentAddress] );
}


/*-----------------------------------------------------------------------*/
/**
 * Write byte to 0xff8803. Set content of YM's data register under conditions.
 * Address 0xff8803 is a shadow version of 0xff8802, so both addresses can't be written
 * at the same time by the same instruction. This means only a .B access or
 * a movep will have a valid effect, other accesses are ignored.
 */
void PSG_ff8803_WriteByte(void)
{
	if ( nIoMemAccessSize == SIZE_BYTE )		/* byte access or movep */
	{	
		PSG_WaitState();
	
		if (LOG_TRACE_LEVEL(TRACE_PSG_WRITE))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("ym write %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
					IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
					FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
		}
		
		PSG_Set_DataRegister ( IoMem[IoAccessCurrentAddress] );
	}

	else
	{						/* do nothing, just a trace if needed */
		if (LOG_TRACE_LEVEL(TRACE_PSG_WRITE))
		{
			int FrameCycles, HblCounterVideo, LineCycles;
			Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );
			LOG_TRACE_PRINT("ym write ignored %x=0x%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n",
					IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
					FrameCycles, LineCycles, HblCounterVideo, M68000_GetPC(), CurrentInstrCycles);
		}
	}
}


/* ------------------------------------------------------------------
 * YM-2149 register content dump (for debugger info command)
 */
void PSG_Info(FILE *fp, uint32_t dummy)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(PSGRegisters); i++)
	{
		fprintf(fp, "Reg $%02X : $%02X\n", i, PSGRegisters[i]);
	}
}
