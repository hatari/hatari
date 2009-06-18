/*
  Hatari - bios.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Bios Handler (Trap #13)

  We intercept some Bios calls for debugging
*/
const char Bios__fileid[] = "Hatari bios.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "floppy.h"
#include "log.h"
#include "m68000.h"
#include "printer.h"
#include "rs232.h"
#include "stMemory.h"
#include "bios.h"


/*-----------------------------------------------------------------------*/
/**
 * BIOS Return input device status
 * Call 1
 */
static bool Bios_Bconstat(Uint32 Params)
{
	Uint16 Dev;

	Dev = STMemory_ReadWord(Params+SIZE_WORD);

	LOG_TRACE(TRACE_OS_BIOS, "BIOS Bconstat(%i)\n", Dev);

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * BIOS Read character from device
 * Call 2
 */
static bool Bios_Bconin(Uint32 Params)
{
	Uint16 Dev;

	Dev = STMemory_ReadWord(Params+SIZE_WORD);

	LOG_TRACE(TRACE_OS_BIOS, "BIOS Bconin(%i)\n", Dev);

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * BIOS Write character to device
 * Call 3
 */
static bool Bios_Bconout(Uint32 Params)
{
	Uint16 Dev;
	unsigned char Char;

	Dev = STMemory_ReadWord(Params+SIZE_WORD);
	Char = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD);

	LOG_TRACE(TRACE_OS_BIOS, "BIOS Bconout(%i, 0x%02x)\n", Dev, Char);

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * BIOS Read/Write disk sector
 * Call 4
 */
static bool Bios_RWabs(Uint32 Params)
{
	Uint32 pBuffer;
	Uint16 RWFlag, Number, RecNo, Dev;

	/* Read details from stack */
	RWFlag = STMemory_ReadWord(Params+SIZE_WORD);
	pBuffer = STMemory_ReadLong(Params+SIZE_WORD+SIZE_WORD);
	Number = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG);
	RecNo = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG+SIZE_WORD);
	Dev = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_LONG+SIZE_WORD+SIZE_WORD);

	LOG_TRACE(TRACE_OS_BIOS, "BIOS RWabs %i,%d,0x%lX,%d,%d\n",
	          Dev, RWFlag, STRAM_ADDR(pBuffer), RecNo, Number);

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * BIOS Return output device status
 * Call 8
 */
static bool Bios_Bcostat(Uint32 Params)
{
	Uint16 Dev;

	Dev = STMemory_ReadWord(Params+SIZE_WORD);

	LOG_TRACE(TRACE_OS_BIOS, "BIOS Bcostat(%i)\n", Dev);

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Check Bios call and see if we need to re-direct to our own routines
 * Return true if we've handled the exception, else return false to let TOS attempt it
 */
bool Bios(void)
{
	Uint32 Params;
	Uint16 BiosCall;

	/* Get call */
	Params = Regs[REG_A7];
	BiosCall = STMemory_ReadWord(Params);

	/* Intercept? */
	switch(BiosCall)
	{
	 case 0x1:
		return Bios_Bconstat(Params);
	 case 0x2:
		return Bios_Bconin(Params);
	 case 0x3:
		return Bios_Bconout(Params);
	 case 0x4:
		return Bios_RWabs(Params);
	 case 0x8:
		return Bios_Bcostat(Params);
	 default:           /* Call as normal! */
		LOG_TRACE(TRACE_OS_BIOS, "BIOS %d\n", BiosCall);
		return false;
	}
}
