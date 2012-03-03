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
 * Convert given BIOS CON: device character output to ASCII.
 * Accepts one character at the time, parses VT52 escape codes
 * and maps Atari characters to their closest ASCII equivalents.
 * 
 * On host, TOS cursor forwards movement is done with spaces,
 * backwards movement is delayed until next non-white character
 * at which point output switches to next line.  Other VT52
 * escape sequences than cursor movement are ignored.
 */
static void Bios_VT52(Uint8 value)
{
	
	static const Uint8 map_0_31[32] = {
		'.', '.', '.', '.', '.', '.', '.', '.',	/* 0x00 */
		/* white space */
		'\b','\t','\n','.','.','\r', '.', '.',	/* 0x08 */
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

	/* state machine to handle/ignore VT52 escape sequence */
	static int escape_index;
	static int escape_target;
	static int hpos_host, hpos_tos;
	static bool need_nl;
	static enum {
		ESCAPE_NONE, ESCAPE_POSITION
	} escape_type;

	if (escape_target) {
		if (++escape_index == 1) {
			/* VT52 escape sequences */
			switch(value) {
			case 'E':	/* clear screen+home -> newline */
				fputs("\n", stderr);
				hpos_host = 0;
				break;
			/* sequences with arguments */
			case 'b':	/* foreground color */
			case 'c':	/* background color */
				escape_target = 2;
				return;
			case 'Y':	/* cursor position */
				escape_type = ESCAPE_POSITION;
				escape_target = 3;
				return;
			}
		} else if (escape_index < escape_target) {
			return;
		}
		if (escape_type == ESCAPE_POSITION) {
			/* last item gives horizontal position */
			hpos_tos = value - ' ';
			if (hpos_tos > 79) {
				hpos_tos = 79;
			} else if (hpos_tos < 0) {
				hpos_tos = 0;
			}
			if (hpos_tos > hpos_host) {
				fprintf(stderr, "%*s", hpos_tos - hpos_host, "");
				hpos_host = hpos_tos;
			} else if (hpos_tos < hpos_host) {
				need_nl = true;
			}
		}
		/* escape sequence end */
		escape_target = 0;
		return;
	}
	if (value == 27) {
		/* escape sequence start */
		escape_type = ESCAPE_NONE;
		escape_target = 1;
		escape_index = 0;
		return;
	}

	/* do newline & indent for backwards movement only when necessary */
	if (need_nl) {
		/* TOS cursor horizontal movement until host output */
		switch (value) {
		case ' ':
			hpos_tos++;
			return;
		case '\b':
			hpos_tos--;
			return;
		case '\t':
			hpos_tos = (hpos_tos + 8) & 0xfff0;
			return;
		case '\r':
		case '\n':
			hpos_tos = 0;
			break;
		}
		fputs("\n", stderr);
		if (hpos_tos > 0 && hpos_tos < 80) {
			fprintf(stderr, "%*s", hpos_tos, "");
			hpos_host = hpos_tos;
		} else {
			hpos_host = 0;
		}
		need_nl = false;
	}

	/* host cursor horizontal movement */
	switch (value) {
	case '\b':
		hpos_host--;
		break;
	case '\t':
		hpos_host = (hpos_host + 8) & 0xfff0;
		break;
	case '\r':
	case '\n':
		hpos_host = 0;
		break;
	default:
		hpos_host++;
		break;
	}

	/* map normal characters to host console */
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

	LOG_TRACE(TRACE_OS_BIOS, "BIOS 3 Bconout(%i, 0x%02x)\n", Dev, Char);

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

	LOG_TRACE(TRACE_OS_BIOS, "BIOS 4 Rwabs(%d,0x%lX,%d,%d,%i)\n",
	          RWFlag, STRAM_ADDR(pBuffer), Number, RecNo, Dev);

	return false;
}


/*-----------------------------------------------------------------------*/

#if ENABLE_TRACING
/**
 * Map BIOS call opcode to BIOS function name
 */
static const char* Bios_Call2Name(Uint16 opcode)
{
	/* GCC uses substrings from above trace statements
	 * where they match, so having them again here
	 * wastes only a pointer & simplifies things
	 */
	static const char* names[] = {
		"Getmpb", "Bconstat","Bconin", "Bconout",
		"Rwabs",  "Setexc",  "Tickcal","Getbpb",
		"Bcostat","Mediach", "Drvmap", "Kbshift"
	};
	if (opcode < ARRAYSIZE(names) && names[opcode]) {
		return names[opcode];
	}
	return "???";
}
#endif


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
	case 0x3:
		return Bios_Bconout(Params);
	case 0x4:
		return Bios_RWabs(Params);

	case 0x1:
	case 0x2:
	case 0x7:
	case 0x8:
	case 0x9:
	case 0xB:
		/* print arg for calls taking single word */
		LOG_TRACE(TRACE_OS_BIOS, "BIOS %hd %s(0x%02x)\n",
			  BiosCall, Bios_Call2Name(BiosCall),
			  STMemory_ReadWord(Params+SIZE_WORD));
		return false;

	default:
		LOG_TRACE(TRACE_OS_BIOS, "BIOS %hd (%s)\n",
			  BiosCall, Bios_Call2Name(BiosCall));
		return false;
	}
}
