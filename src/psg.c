/*
  Hatari - psg.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Programmable Sound Generator (YM-2149) - PSG

  Also used for the printer (centronics) port emulation (PSG Port B, Register 15)
*/


/* 2007/04/14	[NP]	First approximation to get cycle accurate accesses to ff8800/02	*/
/*			by cumulating wait state of 1 cycle and rounding the final	*/
/*			result to 4.							*/
/* 2007/04/29	[NP]	Functions PSG_Void_WriteByte and PSG_Void_ReadByte to handle	*/
/*			accesses to $ff8801/03. These adresses have no effect, but they	*/
/*			must give a 1 cycle wait state (e.g. move.l d0,ff8800).		*/
/* 2007/09/29	[NP]	Replace printf by calls to HATARI_TRACE.			*/
/* 2007/10/23	[NP]	In PSG_Void_WriteByte, add a wait state only if no wait state	*/
/*			were added so far (hack, but gives good result).		*/
/* 2007/11/18	[NP]	In PSG_DataRegister_WriteByte, set unused bit to 0, in case	*/
/*			the data reg is read later (fix Mindbomb Demo / BBC).		*/
/* 2008/04/20	[NP]	In PSG_DataRegister_WriteByte, set unused bit to 0 for register	*/
/*			6 too (noise period).						*/


/* Emulating wait states when accessing $ff8800/01/02/03 with different 'move' variants	*/
/* is a complex task. So far, adding 1 cycle wait state to each access and rounding the	*/
/* final number to 4 gives some good results, but this is certainly not the way it's	*/
/* working for real in the ST.								*/
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
/* This gives the following "model" :							*/
/*	- each access to $ff8800 or $ff8802 add 1 cycle wait state			*/
/*	- access to $ff8801 or $ff8803 give 0 wait state, except if this is the only	*/
/*	  register accessed (move.b for example).					*/



const char PSG_rcsid[] = "Hatari $Id: psg.c,v 1.23 2008-04-20 13:11:09 npomarede Exp $";

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


Uint8 PSGRegisterSelect;        /* 0xff8800 (read/write) */
Uint8 PSGRegisters[16];         /* Register in PSG, see PSG_REG_xxxx */

static BOOL bLastWriteToIOB;    /* boolean flag: did the last write to the PSG go to IOB? */


/*-----------------------------------------------------------------------*/
/**
 * Reset variables used in PSG
 */
void PSG_Reset(void)
{
	PSGRegisterSelect = 0;
	memset(PSGRegisters, 0, sizeof(PSGRegisters));
	bLastWriteToIOB = FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void PSG_MemorySnapShot_Capture(BOOL bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&PSGRegisterSelect, sizeof(PSGRegisterSelect));
	MemorySnapShot_Store(PSGRegisters, sizeof(PSGRegisters));
	MemorySnapShot_Store(&bLastWriteToIOB, sizeof(bLastWriteToIOB));
}


/*-----------------------------------------------------------------------*/
/**
 * Write byte to 0xff8800, this is used as a selector for when we read/write
 * to address 0xff8802
 */
void PSG_SelectRegister_WriteByte(void)
{
//	M68000_WaitState(4);
	M68000_WaitState(1);				/* [NP] FIXME not 100% accurate, but gives good results */

	/* Store register to select (value in bits 0-3). Use IoAccessCurrentAddress
	 * to be able to handle the PSG mirror registers, too. */
	PSGRegisterSelect = IoMem[IoAccessCurrentAddress] & 0x0f;

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_PSG_WRITE_REG ) )
	  {
	    int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
	    int nLineCycles = nFrameCycles % nCyclesPerLine;
	    HATARI_TRACE_PRINT ( "write ym sel reg=%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
		PSGRegisterSelect, nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	  }
}


/*-----------------------------------------------------------------------*/
/**
 * Read byte from 0xff8800, returns PSG data
 */
void PSG_SelectRegister_ReadByte(void)
{
	M68000_WaitState(4);

	if (PSGRegisterSelect == 14)
	{
		/* Second parallel port joystick uses centronics strobe bit as fire button: */
		if (ConfigureParams.Joysticks.Joy[JOYID_PARPORT2].nJoystickMode != JOYSTICK_DISABLED)
		{
			if (Joy_GetStickData(JOYID_PARPORT2) & 0x80)
				PSGRegisters[14] &= ~32;
			else
				PSGRegisters[14] |= 32;
		}
	}
	else if (PSGRegisterSelect == 15)
	{
		/* PSG register 15 is parallel port data register - used by parallel port joysticks: */
		if (ConfigureParams.Joysticks.Joy[JOYID_PARPORT1].nJoystickMode != JOYSTICK_DISABLED)
		{
			PSGRegisters[15] &= 0x0f;
			PSGRegisters[15] |= ~Joy_GetStickData(JOYID_PARPORT1) << 4;
		}
		if (ConfigureParams.Joysticks.Joy[JOYID_PARPORT2].nJoystickMode != JOYSTICK_DISABLED)
		{
			PSGRegisters[15] &= 0xf0;
			PSGRegisters[15] |= ~Joy_GetStickData(JOYID_PARPORT2) & 0x0f;
		}
	}

	/* Read data last selected by register */
	IoMem[IoAccessCurrentAddress] = PSGRegisters[PSGRegisterSelect];
}


/*-----------------------------------------------------------------------*/
/**
 * Write byte to 0xff8802, stores according to PSG select register (write 0xff8800)
 */
void PSG_DataRegister_WriteByte(void)
{
//	M68000_WaitState(4);
	M68000_WaitState(1);				/* [NP] FIXME not 100% accurate, but gives good results */

	/* Create samples up until this point with current values */
	Sound_Update();

	/* Copy value to PSGRegisters[]. Use IoAccessCurrentAddress to be able
	 * to handle the PSG mirror registers, too. */
	PSGRegisters[PSGRegisterSelect] = IoMem[IoAccessCurrentAddress];

	/* [NP] Clear unused bits for some regs */
	if ( ( PSGRegisterSelect == PSG_REG_CHANNEL_A_COARSE ) || ( PSGRegisterSelect == PSG_REG_CHANNEL_B_COARSE )
		|| ( PSGRegisterSelect == PSG_REG_CHANNEL_C_COARSE ) || ( PSGRegisterSelect == PSG_REG_ENV_SHAPE ) )
	  PSGRegisters[PSGRegisterSelect] &= 0x0f;	/* only keep bits 0 - 3 */

	else if ( ( PSGRegisterSelect == PSG_REG_CHANNEL_A_AMP ) || ( PSGRegisterSelect == PSG_REG_CHANNEL_B_AMP )
		|| ( PSGRegisterSelect == PSG_REG_CHANNEL_C_AMP ) )
	  PSGRegisters[PSGRegisterSelect] &= 0x1f;	/* only keep bits 0 - 4 */

	else if ( PSGRegisterSelect == PSG_REG_NOISE_GENERATOR )
	  PSGRegisters[PSGRegisterSelect] &= 0x3f;	/* only keep bits 0 - 5 */


	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_PSG_WRITE_DATA ) )
	  {
	    int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
	    int nLineCycles = nFrameCycles % nCyclesPerLine;
	    HATARI_TRACE_PRINT ( "write ym data reg=%x val=%x video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
		PSGRegisterSelect, PSGRegisters[PSGRegisterSelect], nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	  }


	switch (PSGRegisterSelect)
	{

	 /* Check registers 8,9 and 10 which are 'amplitude' for each channel and
	  * store if wrote to (to check for sample playback) */
	 case PSG_REG_CHANNEL_A_AMP:
		bWriteChannelAAmp = TRUE;
		break;
	 case PSG_REG_CHANNEL_B_AMP:
		bWriteChannelBAmp = TRUE;
		break;
	 case PSG_REG_CHANNEL_C_AMP:
		bWriteChannelCAmp = TRUE;
		break;

	 case PSG_REG_ENV_SHAPE:            /* Whenever 'write' to register 13, cause envelope to reset */
		bEnvelopeFreqFlag = TRUE;
		bWriteEnvelopeFreq = TRUE;
		break;

	 /*
	  * FIXME: This is only a prelimary dirty hack!
	  * Port B (Printer port) - writing here needs to be dispatched to the printer
	  * STROBE (Port A bit5) does a short LOW and back to HIGH when the char is valid
	  * To print you need to write the character byte to IOB and you need to toggle STROBE
	  * (like EmuTOS does)....therefor we print when STROBE gets low and last write access to
	  * the PSG was to IOB
	  */
	 case PSG_REG_IO_PORTA:
		/* Printer dispatching only when printing is activated */
		if (ConfigureParams.Printer.bEnablePrinting)
		{
			/* Bit 5 - Centronics strobe? If STROBE is low and the last write did go to IOB then
			 * there is data in PORTB to print/transfer to the emulated Centronics port
			 */
			if ((((PSGRegisters[PSG_REG_IO_PORTA]&(1<<5))==0) && bLastWriteToIOB))
			{
				/* Seems like we want to print something... */
				Printer_TransferByteTo((unsigned char) PSGRegisters[PSG_REG_IO_PORTB]);
			}
		}
		/* Bit 3 - Centronics as input */
		if(PSGRegisters[PSG_REG_IO_PORTA]&(1<<3))
		{
			/* FIXME: might be needed if we want to emulate sound sampling hardware */
		}
		
		/* handle Falcon specific bits in PORTA of the PSG */
		if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
		{
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
		break;
	}

	/* Remember if we wrote to IO Port B */
	bLastWriteToIOB = (PSGRegisterSelect == PSG_REG_IO_PORTB);
}


/*-----------------------------------------------------------------------*/
/**
 * Read byte from 0xff8802, returns 0xff
 */
void PSG_DataRegister_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[IoAccessCurrentAddress] = 0xff;
}



/*-----------------------------------------------------------------------*/
/**
 * Write byte to 0xff8801/03. Do nothing, but add some wait states if needed.
 */
void PSG_Void_WriteByte(void)
{
	/* [NP] FIXME If no wait states were added so far, it's possible we're accessing */
	/* 8801/8803 through a .B instruction, so we need to add a wait state */
	/* Else, the wait states will be added when writing to 8800/8802 */
	/* This works so far, but this model is certainly not 100% accurate */
	if ( nWaitStateCycles == 0 )
	  M68000_WaitState(1);

	if ( HATARI_TRACE_LEVEL ( HATARI_TRACE_PSG_WRITE_DATA ) )
	  {
	    int nFrameCycles = Cycles_GetCounter(CYCLES_COUNTER_VIDEO);;
	    int nLineCycles = nFrameCycles % nCyclesPerLine;
	    HATARI_TRACE_PRINT ( "write ym 8801/03 video_cyc=%d %d@%d pc=%x instr_cycle %d\n" ,
		nFrameCycles, nLineCycles, nHBL, M68000_GetPC(), CurrentInstrCycles );
	  }
}



/*-----------------------------------------------------------------------*/
/**
 * Read byte from 0xff8801/03. Do nothing, but add some wait states.
 */
void PSG_Void_ReadByte(void)
{
	M68000_WaitState(1);
}

