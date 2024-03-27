/*
 * Hatari - console.c
 * 
 * Copyright (C) 2012-2015 by Eero Tamminen
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * console.c - catching of emulated console output with minimal VT52 emulation.
 */
const char Console_fileid[] = "Hatari console.c";

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "m68000.h"
#include "stMemory.h"
#include "hatari-glue.h"
#include "console.h"
#include "options.h"

/* number of xconout devices to track */
int ConOutDevices;

#define CONOUT_DEVICE_NONE 127 /* valid ones are 0-7 */

/* device number for xconout devices to track */
static int con_dev = CONOUT_DEVICE_NONE;
static bool con_trace;

/**
 * Set which Atari xconout device output goes to host console.
 * Returns true for valid device values (0-7), false otherwise
 */
bool Console_SetDevice(int dev)
{
	if (dev < 0 || dev > 7) {
		return false;
	}
	if (con_dev == CONOUT_DEVICE_NONE) {
		ConOutDevices++;
	}
	con_dev = dev;
	return true;
}

/**
 * Enable / disable xconout 2 host output for tracing.
 * Overrides Console_SetDevice() device while enabled
 */
void Console_SetTrace(bool enable)
{
	if (enable && !con_trace) {
		ConOutDevices++;
	}
	if (con_trace && !enable) {
		ConOutDevices--;
	}
	con_trace = enable;
}

/**
 * Maps Atari characters to their closest ASCII equivalents.
 */
static void map_character(uint8_t value)
{
	static const uint8_t map_0_31[32] = {
		'.', '.', '.', '.', '.', '.', '.', '.',	/* 0x00 */
		/* white space */
		'\b','\t','\n','.','.','\r', '.', '.',	/* 0x08 */
		/* LED numbers */
		'0', '1', '2', '3', '4', '5', '6', '7',	/* 0x10 */
		'8', '9', '.', '.', '.', '.', '.', '.' 	/* 0x18 */
	};
	static const uint8_t map_128_255[128] = {
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
	/* map normal characters to host console */
	if (value < 32) {
		fputc(map_0_31[value], stdout);
	} else if (value > 127) {
		fputc(map_128_255[value-128], stdout);
	} else {
		fputc(value, stdout);
	}
}


/**
 * Convert given console character output to ASCII.
 * Accepts one character at the time, parses VT52 escape codes
 * and outputs them on console.
 * 
 * On host, TOS cursor forwards movement is done with spaces,
 * backwards movement is delayed until next non-white character
 * at which point output switches to next line.  Other VT52
 * escape sequences than cursor movement are ignored.
 */
static void vt52_emu(uint8_t value)
{
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
				fputs("\n", stdout);
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
				fprintf(stdout, "%*s", hpos_tos - hpos_host, "");
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
		fputs("\n", stdout);
		if (hpos_tos > 0 && hpos_tos < 80) {
			fprintf(stdout, "%*s", hpos_tos, "");
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
	map_character(value);
}

/**
 * Catch requested xconout vector calls and show their output on console
 */
void Console_Check(void)
{
	uint32_t pc, xconout, stack, stackbeg, stackend;
	int increment, dev;
	uint16_t chr;

	if (con_trace) {
		dev = 2;
	} else {
		dev = con_dev;
	}
	/* xconout vector for requested device? */
	xconout = STMemory_ReadLong(0x57e + dev * SIZE_LONG);
	pc = M68000_GetPC();
	if (pc != xconout) {
		return;
	}

	/* assumptions about xconout function:
	 * - c declaration: leftmost item on top of stackframe
	 * - args: WORD device, WORD character to output
	 * - can find the correct stackframe arguments by skipping
	 *   wrong looking stack content from intermediate functions
	 *   (bsr/jsr return addresses are > 0xff, local stack args
	 *   could be an issue but hopefully don't match device number
	 *   in any of the TOSes nor in MiNT or its conout devices)
	 */
	stackbeg = stack = Regs[REG_A7];
	stackend = stack + 16;
	increment = SIZE_LONG;
	while (STMemory_ReadWord(stack) != dev) {
		stack += increment;
		if (stack > stackend) {
			if (increment == SIZE_LONG) {
				/* skipping return addresses not enough,
				 * try skipping potential local args too
				 */
				Log_Printf(LOG_WARN, "xconout stack args not found by skipping return addresses, trying short skipping.\n");
				increment = SIZE_WORD;
				stack = stackbeg;
				continue;
			}
			/* failed */
			Log_Printf(LOG_WARN, "xconout args not found from stack.\n");
			return;
		}
	}
	chr = STMemory_ReadWord(stack + SIZE_WORD);
	if (chr & 0xff00) {
		/* allow 0xff high byte (sign extension?) */
		if ((chr & 0xff00) != 0xff00) {
			Log_Printf(LOG_WARN, "xconout character '%c' has unknown high byte bits: 0x%x.\n", chr&0xff, chr&0xff00);
			/* higher bits, assume not correct arg */
			return;
		}
		chr &= 0xff;
	}
	switch(dev) {
	case 2:	/* EmuTOS/TOS/MiNT/etc console, VT-52 terminal */
		vt52_emu(chr);
		break;
	case 0: /* Printer/Parallel port */
	case 1: /* Aux device, the RS-232 port */
	case 3: /* MIDI port */
	case 4: /* Keyboard port */
	case 5: /* Raw screen device (no escape sequence / control char processing) */
	case 6: /* ST compatible RS-232 port (Modem 1) */
	case 7: /* SCC channel B (Modem 2) */
		map_character(chr);
		break;
	}
	fflush(stdout);
}
