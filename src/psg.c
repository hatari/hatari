/*
  Hatari - psg.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Programmable Sound Generator (YM-2149) - PSG

  Also used for the printer (centronics) port emulation (PSG Port B, Register 15)
*/
char PSG_rcsid[] = "Hatari $Id: psg.c,v 1.7 2005-01-18 23:33:24 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "sound.h"
#include "printer.h"            /* because Printer I/O goes through PSG Register 15 */
#include "psg.h"


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
	PSGRegisterSelect = IoMem[0xff8800] & 0x0f;     /* Store register to select (value in bits 0-3) */

	M68000_WaitState();
}


/*-----------------------------------------------------------------------*/
/*
  Read byte from 0xff8800, returns to PSG data
*/
void PSG_SelectRegister_ReadByte(void)
{
	/* Read data last selected by register */
	IoMem[0xff8800] = PSGRegisters[PSGRegisterSelect];

	M68000_WaitState();
}


/*-----------------------------------------------------------------------*/
/*
  Write byte to 0xff8802, stores according to PSG select register (write 0xff8800)
*/
void PSG_DataRegister_WriteByte(void)
{
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
			/* Is STROBE low and did the last write go to IOB? */
			if ((PSGRegisters[PSG_REG_IO_PORTA]&32)==0 && bLastWriteToIOB)
			{
				/* Seems like we want to print something... */
				Printer_TransferByteTo((unsigned char) PSGRegisters[PSG_REG_IO_PORTB]);
			}
		}
		break;
	}

	/* Remember if we wrote to IO Port B */
	bLastWriteToIOB = (PSGRegisterSelect == PSG_REG_IO_PORTB);

	M68000_WaitState();
}


/*-----------------------------------------------------------------------*/
/*
  Read byte from 0xff8802, returns 0xff
*/
void PSG_DataRegister_ReadByte(void)
{
	IoMem[0xff8802] = 0xff;

	M68000_WaitState();
}
