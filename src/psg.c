/*
  Hatari - psg.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Programmable Sound Generator (YM-2149) - PSG

  Also used for the printer (centronics) port emulation (PSG Port B, Register 15)
*/
const char PSG_rcsid[] = "Hatari $Id: psg.c,v 1.12 2006-11-17 18:08:03 simonsunnyboy Exp $";

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "joy.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "sound.h"
#include "printer.h"            /* because Printer I/O goes through PSG Register 15 */
#include "psg.h"
#if ENABLE_FALCON
#include "falcon/dsp.h"
#endif

Uint8 PSGRegisterSelect;        /* 0xff8800 (read/write) */
Uint8 PSGRegisters[16];         /* Register in PSG, see PSG_REG_xxxx */

static BOOL bLastWriteToIOB;    /* boolean flag: did the last write to the PSG go to IOB? */


/*-----------------------------------------------------------------------*/
/*
  Reset variables used in PSG
*/
void PSG_Reset(void)
{
	PSGRegisterSelect = 0;
	memset(PSGRegisters, 0, sizeof(PSGRegisters));
	bLastWriteToIOB = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
*/
void PSG_MemorySnapShot_Capture(BOOL bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&PSGRegisterSelect, sizeof(PSGRegisterSelect));
	MemorySnapShot_Store(PSGRegisters, sizeof(PSGRegisters));
	MemorySnapShot_Store(&bLastWriteToIOB, sizeof(bLastWriteToIOB));
}


/*-----------------------------------------------------------------------*/
/*
  Write byte to 0xff88000, this is used as a selector for when we read/write
  to address 0xff8802
*/
void PSG_SelectRegister_WriteByte(void)
{
	M68000_WaitState(4);

	PSGRegisterSelect = IoMem[0xff8800] & 0x0f;     /* Store register to select (value in bits 0-3) */
}


/*-----------------------------------------------------------------------*/
/*
  Read byte from 0xff8800, returns PSG data
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
	IoMem[0xff8800] = PSGRegisters[PSGRegisterSelect];
}


/*-----------------------------------------------------------------------*/
/*
  Write byte to 0xff8802, stores according to PSG select register (write 0xff8800)
*/
void PSG_DataRegister_WriteByte(void)
{
	M68000_WaitState(4);

	Sound_Update();                            /* Create samples up until this point with current values */
	PSGRegisters[PSGRegisterSelect] = IoMem[0xff8802];        /* Write value to PSGRegisters[] */

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
		
#if ENABLE_FALCON
		/* handle Falcon specific bits in PORTA of the PSG */
		if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
		{
			/* Bit 4 - DSP reset? */
			if(PSGRegisters[PSG_REG_IO_PORTA]&(1<<4))
			{
				fprintf(stderr, "Calling DSP_Reset?\n");
				//DSP_Reset();
			}
			/* Bit 6 - Internal Speaker control */
			if(PSGRegisters[PSG_REG_IO_PORTA]&(1<<6))
			{
				fprintf(stderr, "Falcon: Internal Speaker state\n");
				/* FIXME: add code to handle? (if we want to emulate the speaker at all? */
			}
			/* Bit 7 - Reset IDE? */
			if(PSGRegisters[PSG_REG_IO_PORTA]&(1<<7))
			{
				fprintf(stderr, "Falcon: Reset IDE subsystem\n");
				/* FIXME: add code to handle IDE reset */
			}
		}
#endif
		break;
	}

	/* Remember if we wrote to IO Port B */
	bLastWriteToIOB = (PSGRegisterSelect == PSG_REG_IO_PORTB);
}


/*-----------------------------------------------------------------------*/
/*
  Read byte from 0xff8802, returns 0xff
*/
void PSG_DataRegister_ReadByte(void)
{
	M68000_WaitState(4);

	IoMem[0xff8802] = 0xff;
}
