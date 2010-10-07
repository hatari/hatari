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
 * Converts given BIOS CON: device character output to ASCII.
 * Accepts one character at the time, ignores VT52 escape codes
 * and maps Atari characters to their closest ASCII equivalents.
 *
 * TODO: Keep track of current position on line & white space usage and
 * convert 1,1 position to \r + forward position movements to spaces?
 */
static void Bios_VT52(Uint8 value)
{
	
	static const Uint8 map_0_31[32] = {
		'.', '.', '.', '.', '.', '.', '.', '.',	/* 0x00 */
		/* white space */
		'.','\t','\n','\v','\f','\r', '.', '.',	/* 0x08 */
		/* LED numbers */
		'0', '1', '2', '3', '4', '5', '6', '7',	/* 0x10 */
		'8', '9', '.', '.', '.', '.', '.', '.' 	/* 0x18 */
	};
	static const Uint8 map_128_255[128] = {
		/* accented characters */
		'C', 'U', 'e', 'a', 'a', 'a', 'a', 'c',	/* 0x80 */
		'e', 'e', 'e', 'i', 'i', 'i', 'A', 'A',	/* 0x88 */
		'E', 'a', 'A', 'o', 'o', 'o', 'u', 'u',	/* 0x90 */
		'y', 'o', 'u', 'c', '.', 'Y', 'B', 'f',	/* 0x98 */
		'a', 'i', 'o', 'u', 'n', 'N', 'a', 'o',	/* 0xA0 */
		'?', '.', '.', '.', '.', 'i', '<', '>',	/* 0xA8 */
		'a', 'o', 'O', 'o', 'o', 'O', 'A', 'A',	/* 0xB0 */
		'O', '"','\'', '.', '.', 'C', 'R', '.',	/* 0xB8 */
		'j', 'J', '.', '.', '.', '.', '.', '.',	/* 0xC0 */
		'.', '.', '.', '.', '.', '.', '.', '.',	/* 0xC8 */
		'.', '.', '.', '.', '.', '.', '.', '.',	/* 0xD0 */
		'.', '.', '.', '.', '.', '.', '^', '.',	/* 0xD8 */
		'.', '.', '.', '.', '.', '.', '.', '.',	/* 0xE0 */
		'.', '.', '.', '.', '.', '.', '.', '.',	/* 0xE8 */
		'.', '.', '.', '.', '.', '.', '.', '.',	/* 0xF0 */
		'.', '.', '.', '.', '.', '.', '.', '.'	/* 0xF8 */
	};

	/* state machine to ignore VT52 escape sequence */
	static int escape_index;
	static int escape_target;
	
	if (escape_target) {
		if (++escape_index == 1) {
			/* VT52 escape sequences with arguments? */
			switch(value) {
			case 'b':	/* foreground color */
			case 'c':	/* background color */
				escape_target = 2;
				return;
			case 'Y':	/* cursor position */
				escape_target = 3;
				return;
			}
		} else if (escape_index < escape_target) {
			return;
		}
		/* escape sequence end */
		escape_target = 0;
		return;
	}
	if (value == 27) {
		/* escape sequence start */
		escape_target = 1;
		escape_index = 0;
		return;
	}

	/* map normal characters */
	if (value < 32) {
		fputc(map_0_31[value], stderr);
	} else if (value > 127) {
		fputc(map_128_255[value-128], stderr);
	} else {
		fputc(value, stderr);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * BIOS Write character to device
 * Call 3
 */
static bool Bios_Bconout(Uint32 Params)
{
	Uint16 Dev;
	Uint8 Char;

	Dev = STMemory_ReadWord(Params+SIZE_WORD);
	Char = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD);

	LOG_TRACE(TRACE_OS_BIOS, "BIOS Bconout(%i, 0x%02x)\n", Dev, Char);

	if (Dev == 2) {
		Bios_VT52(Char);
	}
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

	LOG_TRACE(TRACE_OS_BIOS, "BIOS Rwabs %i,%d,0x%lX,%d,%d\n",
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
 * Print BIOS call name when BIOS tracing enabled.
 */
static bool Bios_Trace(Uint16 BiosCall)
{
#if ENABLE_TRACING
	/* GCC uses substrings from above trace statements
	 * where they match, so having them again here
	 * wastes only a pointer & simplifies things
	 */
	static const char* names[] = {
		"Getmpb", "Bconstat","Bconin", "Bconout",
		"Rwabs",  "Setexc",  "Tickcal","Getbpb",
		"Bcostat","Mediach", "Drvmap", "Kbshift"
	};
	if (BiosCall < ARRAYSIZE(names)) {
		LOG_TRACE(TRACE_OS_BIOS, "BIOS %s()\n", names[BiosCall]);
	} else {
		LOG_TRACE(TRACE_OS_BIOS, "BIOS %d?\n", BiosCall);
	}
#endif
	/* let TOS handle it */
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Check Bios call and see if we need to re-direct to our own routines.
 * Return true if we've handled the exception, else return false to let
 * TOS attempt it
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
	 default:
		return Bios_Trace(BiosCall);
	}
}
