/*
  Hatari - nvram.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file is partly based on GPL code taken from the Aranym project.
  - Copyright (c) 2001-2004 Petr Stehlik of ARAnyM dev team
  - Adaption to Hatari (c) 2006 by Thomas Huth
  - Copyright (c) 2015 Thorsten Otto of ARAnyM dev team
  - Adaption to Hatari (c) 2019 by Eero Tamminen

  Atari TT and Falcon NVRAM/RTC emulation code.
  This is a MC146818A or compatible chip with a non-volatile RAM area.

  These are the important bytes in the nvram array:

  Byte:    Description:

    0      Seconds
    1      Seconds Alarm
    2      Minutes
    3      Minutes Alarm
    4      Hours
    5      Hours Alarm
    6      Day of Week
    7      Date of Month
    8      Month
    9      Year
   10      Control register A
   11      Control register B
   12      Status register C
   13      Status register D
  14-15    Preferred operating system (TOS, Unix)
   20      Language
   21      Keyboard layout
   22      Format of date/time
   23      Separator for date
   24      Boot delay
  28-29    Video mode
   30      SCSI-ID in bits 0-2, bus arbitration flag in bit 7 (1=off, 0=on)
  62-63    Checksum

  See: https://www.nxp.com/docs/en/data-sheet/MC146818.pdf

  Not implemented (as no known use-case):
  - all alarm handling
  - doing clock updates at 1Hz
    (instead of when regs are read)
  - periodic divisor & rate-control bits
  - alarm, update-end and periodic interrupt generation
*/
const char NvRam_fileid[] = "Hatari nvram.c";

#include <time.h>

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "log.h"
#include "nvram.h"
#include "paths.h"
#include "tos.h"
#include "vdi.h"

// Defs for NVRAM control register A (10) bits
#define REG_BIT_UIP  0x80	/* update-in-progress */
// and 3 clock divider & 3 rate-control bits

// Defs for NVRAM control register B (11) bits
#define REG_BIT_DSE  0x01	/* daylight saving enable (ignored) */
#define REG_BIT_24H  0x02	/* 24/12h clock, 1=24h */
#define REG_BIT_DM   0x04	/* data mode: 1=BIN, 0=BCD */
#define REG_BIT_SQWE 0x08	/* square wave enable, signal to SQW pin */
#define REG_BIT_UIE  0x10	/* update-ended interrupt enable */
#define REG_BIT_AIE  0x20	/* alarm interrupt enable */
#define REG_BIT_PIE  0x40	/* periodic interrupt enable */
#define REG_BIT_SET  0x80	/* suspend RTC updates to set clock values */

// Defs for NVRAM status register C (12) bits
#define REG_BIT_UF   0x10	/* update-ended interrupt flag */
#define REG_BIT_AF   0x20	/* alarm interrupt flag */
#define REG_BIT_PF   0x40	/* periodic interrupt flag */
#define REG_BIT_IRQF 0x80	/* interrupt request flag */

// Defs for NVRAM status register D (13) bits
#define REG_BIT_VRM  0x80	/* valid RAM and time */

// Defs for checksum
#define CKS_RANGE_START	14
#define CKS_RANGE_END	(14+47)
#define CKS_RANGE_LEN	(CKS_RANGE_END-CKS_RANGE_START+1)
#define CKS_LOC			(14+48)

#define NVRAM_START  14
#define NVRAM_LEN    50

static uint8_t nvram[64] = {
	48, 255, 21, 255, 23, 255, 1, 25, 3, 33, /* clock/alarm registers */
	42, REG_BIT_DM|REG_BIT_24H, 0, REG_BIT_VRM, /* regs A-D */
	0,0,0,0,0,0,0,0,17,46,32,1,255,0,1,10,135,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};


static uint8_t nvram_index;
static char nvram_filename[FILENAME_MAX];
static int year_offset;


/*-----------------------------------------------------------------------*/
/**
 * Load NVRAM data from file.
 */
static bool NvRam_Load(void)
{
	bool ret = false;
	FILE *f = fopen(nvram_filename, "rb");
	if (f != NULL)
	{
		uint8_t fnvram[NVRAM_LEN];
		if (fread(fnvram, 1, NVRAM_LEN, f) == NVRAM_LEN)
		{
			memcpy(nvram+NVRAM_START, fnvram, NVRAM_LEN);
			LOG_TRACE(TRACE_NVRAM, "NVRAM: loaded from '%s'\n", nvram_filename);
			ret = true;
		}
		else
		{
			Log_Printf(LOG_WARN, "NVRAM loading from '%s' failed\n", nvram_filename);
		}
		fclose(f);
	}
	else
	{
		Log_Printf(LOG_INFO, "NVRAM not found at '%s'\n", nvram_filename);
	}

	return ret;
}


/*-----------------------------------------------------------------------*/
/**
 * Save NVRAM data to file
 */
static bool NvRam_Save(void)
{
	bool ret = false;
	FILE *f = fopen(nvram_filename, "wb");
	if (f != NULL)
	{
		if (fwrite(nvram+NVRAM_START, 1, NVRAM_LEN, f) == NVRAM_LEN)
		{
			LOG_TRACE(TRACE_NVRAM, "NVRAM: saved to '%s'\n", nvram_filename);
			ret = true;
		}
		else
		{
			Log_Printf(LOG_WARN, "Writing NVRAM to '%s' failed\n", nvram_filename);
		}
		fclose(f);
	}
	else
	{
		Log_Printf(LOG_WARN, "Storing NVRAM to '%s' failed\n", nvram_filename);
	}

	return ret;
}


/*-----------------------------------------------------------------------*/
/**
 * Create NVRAM checksum. The checksum is over all bytes except the
 * checksum bytes themselves; these are at the very end.
 */
static void NvRam_SetChecksum(void)
{
	int i;
	unsigned char sum = 0;
	
	for(i = CKS_RANGE_START; i <= CKS_RANGE_END; ++i)
		sum += nvram[i];
	nvram[NVRAM_CHKSUM1] = ~sum;
	nvram[NVRAM_CHKSUM2] = sum;
}


/*-----------------------------------------------------------------------*/
/**
 * Register C interrupt status flags clearing.
 *
 * Flags are cleared both on resets and register C reads.
 * Because rest of bits are always zero, whole register goes to zero.
 *
 * However, because interrupt handling isn't emulated yet
 * (there isn't even a known Atari use-case for them),
 * and most of them would occur quickly:
 * - update-ended interrupt flag is set at 1Hz clock update cycle
 * - periodic interrupt is set based on divider & rate-control clock rate
 * - alarm interrupt flag is set when time matches alarm time
 *   i.e. only once a day
 */
static void clear_reg_c(void)
{
	/* => set flags for fastest 2 interrupts right away */
	nvram[12] = REG_BIT_UF|REG_BIT_PF;
	/* are these interrupts also enable in reg B? */
	if (nvram[11] & nvram[12])
	{
		/* -> set also interrupt request flag */
		nvram[12] |= REG_BIT_IRQF;
		/* TODO: generate interrupt */
	}
}

/*-----------------------------------------------------------------------*/
/**
 * NvRam_Reset: Called during init and reset, used for resetting the
 * emulated chip.
 */
void NvRam_Reset(void)
{
	if (bUseVDIRes)
	{
		/* The objective is to start the TOS with a video mode similar
		 * to the requested one. This is important for the TOS to initialize
		 * the right font height and palette. */
		if (VDIHeight < 400)
		{
			/* This will select the 8x8 system font */
			switch(VDIPlanes)
			{
			/* The case 1 is not handled, because that would result in 0x0000
			 * which is an invalid video mode. This does not matter,
			 * since any color palette is good for monochrome, anyway. */
			case 2:	/* set 320x200x4 colors */
				nvram[NVRAM_VMODE1] = 0x00;
				nvram[NVRAM_VMODE2] = 0x01;
				break;
			case 4:	/* set 320x200x16 colors */
			default:
				nvram[NVRAM_VMODE1] = 0x00;
				nvram[NVRAM_VMODE2] = 0x02;
			}
		}
		else
		{
			/* This will select the 8x16 system font */
			switch(VDIPlanes)
			{
			case 4:	/* set 640x400x16 colors */
				nvram[NVRAM_VMODE1] = 0x01;
				nvram[NVRAM_VMODE2] = 0x0a;
				break;
			case 2:	/* set 640x400x4 colors */
				nvram[NVRAM_VMODE1] = 0x01;
				nvram[NVRAM_VMODE2] = 0x09;
				break;
			case 1:	/* set 640x400x2 colors */
			default:
				nvram[NVRAM_VMODE1] = 0x01;
				nvram[NVRAM_VMODE2] = 0x08;
			}
		}
		NvRam_SetChecksum();
	}
	/* reset clears SWQE + interrupt enable bits */
	nvram[11] &= ~(REG_BIT_SQWE|REG_BIT_UIE|REG_BIT_AIE|REG_BIT_PIE);
	/* and interrupt flag bits */
	clear_reg_c();

	nvram_index = 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Initialization
 */
void NvRam_Init(void)
{
	const char sBaseName[] = "hatari.nvram";
	const char *psHomeDir;

	// set up the nvram filename
	psHomeDir = Paths_GetHatariHome();
	if (strlen(psHomeDir)+sizeof(sBaseName)+1 < sizeof(nvram_filename))
		sprintf(nvram_filename, "%s%c%s", psHomeDir, PATHSEP, sBaseName);
	else
		strcpy(nvram_filename, sBaseName);

	if (!NvRam_Load())		// load NVRAM file automatically
	{
		if (ConfigureParams.Screen.nMonitorType == MONITOR_TYPE_VGA)   // VGA ?
		{
			nvram[NVRAM_VMODE1] &= ~0x01;		// No doublescan
			nvram[NVRAM_VMODE2] |= 0x10;		// VGA mode
			nvram[NVRAM_VMODE2] &= ~0x20;		// 60 Hz
		}
		else
		{
			nvram[NVRAM_VMODE1] |= 0x01;		// Interlaced
			nvram[NVRAM_VMODE2] &= ~0x10;		// TV/RGB mode
			nvram[NVRAM_VMODE2] |= 0x20;		// 50 Hz
		}
	}
	if (ConfigureParams.Keyboard.nLanguage != TOS_LANG_UNKNOWN)
		nvram[NVRAM_LANGUAGE] = ConfigureParams.Keyboard.nLanguage;
	if (ConfigureParams.Keyboard.nKbdLayout != TOS_LANG_UNKNOWN)
		nvram[NVRAM_KEYBOARDLAYOUT] = ConfigureParams.Keyboard.nKbdLayout;

	NvRam_SetChecksum();
	NvRam_Reset();

	/* Set suitable tm->tm_year offset
	 * (tm->tm_year starts from 1900, NVRAM year from 1968)
	 */
	year_offset = 68;
	if (!ConfigureParams.System.nRtcYear)
		return;

	time_t ticks = time(NULL);
	int year = 1900 + localtime(&ticks)->tm_year;
	year_offset += year - ConfigureParams.System.nRtcYear;
}


/*-----------------------------------------------------------------------*/
/**
 * De-Initialization
 */
void NvRam_UnInit(void)
{
	NvRam_Save();		// save NVRAM file upon exit automatically (should be conditionalized)
}


/*-----------------------------------------------------------------------*/
/**
 * Read from RTC/NVRAM offset selection register ($ff8961)
 */
void NvRam_Select_ReadByte(void)
{
	IoMem_WriteByte(0xff8961, nvram_index);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to RTC/NVRAM offset selection register ($ff8961)
 */
void NvRam_Select_WriteByte(void)
{
	uint8_t value = IoMem_ReadByte(0xff8961);

	if (value < sizeof(nvram))
	{
		nvram_index = value;
	}
	else
	{
		Log_Printf(LOG_WARN, "NVRAM: trying to set out-of-bound position (%d)\n", value);
	}
}


/*-----------------------------------------------------------------------*/

static struct tm* refreshFrozenTime(bool refresh)
{
	static struct tm frozen_time;

	if (refresh)
	{
		/* update frozen time */
		time_t tim = time(NULL);
		frozen_time = *localtime(&tim);
	}
	return &frozen_time;
}

/**
 * Returns pointer to "frozen time".  Unless NVRAM SET time bit is set,
 * that's first refreshed from host clock (= doing "RTC update cycle").
 * Correct applications have SET bit enabled while they write clock registers.
 */
static struct tm* getFrozenTime(void)
{
	if (nvram[11] & REG_BIT_SET)
		return refreshFrozenTime(false);
	else
		return refreshFrozenTime(true);
}


/**
 * If NVRAM data mode bit is set, returns given value,
 * otherwise returns it as BCD.
 */
static uint8_t bin2BCD(uint8_t value)
{
	if ((nvram[11] & REG_BIT_DM))
		return value;
	return ((value / 10) << 4) | (value % 10);
}


/*-----------------------------------------------------------------------*/
/**
 * Read from RTC/NVRAM data register ($ff8963)
 */
void NvRam_Data_ReadByte(void)
{
	uint8_t value = 0;

	switch(nvram_index)
	{
	case 1: /* alarm seconds */
	case 3:	/* alarm minutes */
	case 5: /* alarm hour */
		value = bin2BCD(nvram[nvram_index]);
		break;
	case 0:
		value = bin2BCD(getFrozenTime()->tm_sec);
		break;
	case 2:
		value = bin2BCD(getFrozenTime()->tm_min);
		break;
	case 4:
		value = getFrozenTime()->tm_hour;
		if (!(nvram[11] & REG_BIT_24H))
		{
			uint8_t pmflag = (value == 0 || value >= 13) ? 0x80 : 0;
			value = value % 12;
			if (value == 0)
				value = 12;
			value = bin2BCD(value) | pmflag;
		}
		else
			value = bin2BCD(value);
		break;
	case 6:
		value = bin2BCD(getFrozenTime()->tm_wday + 1);
		break;
	case 7:
		value = bin2BCD(getFrozenTime()->tm_mday);
		break;
	case 8:
		value = bin2BCD(getFrozenTime()->tm_mon + 1);
		break;
	case 9:
		value = bin2BCD(getFrozenTime()->tm_year - year_offset);
		break;
	case 10:
		/* control reg A
		 * read-only UIP bit + clock dividers & rate selectors
		 *
		 * UIP is suspended during SET, otherwise
		 * dummy toggling it is enough to fool programs
		 */
		if (nvram[11] & REG_BIT_SET)
			nvram[nvram_index] &= ~REG_BIT_UIP;
		else
			nvram[nvram_index] ^= REG_BIT_UIP;
		value = nvram[nvram_index];
		break;
	case 12:
		/* status reg C, read-only
		 * 0xf0 interrupt status bits, 0x0f unused/zero
		 * register is cleared after read
		 */
		value = nvram[nvram_index];
		clear_reg_c();
		break;
	case 11:
		/* control reg B
		 * set, interrupt enable, sqw enable, clock mode, daylight savings bits
		 * writing SET bit aborts/suspends UIP and clears UIP bit
		 */
		/* fall-through */
	case 13:
		/* status reg D, read-only
		 * Valid RAM and Time bit, rest of bits are zero/unused
		 */
		/* fall-through */
	default:
		value = nvram[nvram_index];
		break;
	}

	LOG_TRACE(TRACE_NVRAM, "NVRAM: read data at %d = %d ($%02x)\n", nvram_index, value, value);
	IoMem_WriteByte(0xff8963, value);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to RTC/NVRAM data register ($ff8963)
 */

void NvRam_Data_WriteByte(void)
{
	/* enable & flag bits in B & C regs match each other -> use same mask for both */
	const uint8_t int_mask = REG_BIT_UF|REG_BIT_AF|REG_BIT_PF;

	uint8_t value = IoMem_ReadByte(0xff8963);
	switch (nvram_index)
	{
	case 0:
		/* high-order bit read-only: don't care as we always read from host */
		break;
	case 10:
		/* UIP bit is read-only */
		value = (value & ~REG_BIT_UIP) | (nvram[10] & REG_BIT_UIP);
		break;
	case 11:
		if (value & int_mask)
		{
			Log_Printf(LOG_WARN, "Write to unimplemented RTC/NVRAM interrupt enable bits 0x%x\n", value & int_mask);
			if (nvram[12] & int_mask)
			{
				/* reg B enabling bits matched reg C flag bits */
				nvram[12] |= REG_BIT_IRQF;
				/* TODO: generate interrupt */
			}
			/* TODO: start updating reg C flag bits & generate interrupts when appropriate */
		}
		if (value & REG_BIT_SET)
		{
			/* refresh clock as its updating is suspended while SET is enabled */
			refreshFrozenTime(true);
		}
		break;
	case 12:
	case 13:
		IoMem_WriteByte(0xff8963, nvram[nvram_index]);
		Log_Printf(LOG_WARN, "Ignored write %d ($%02x) to read-only RTC/NVRAM status register %d!\n",
			   value, value, nvram_index);
		return;
	}
	LOG_TRACE(TRACE_NVRAM, "NVRAM: write data at %d = %d ($%02x)\n", nvram_index, value, value);
	nvram[nvram_index] = value;
}


void NvRam_Info(FILE *fp, uint32_t dummy)
{
	fprintf(fp, "- File: '%s'\n", nvram_filename);
	fprintf(fp, "- Time: from host (regs: 0, 2, 4, 6-9)\n");
	fprintf(fp, "- Alarm: %02d:%02d:%02d (1, 3, 5)\n",
		bin2BCD(nvram[5]), bin2BCD(nvram[3]), bin2BCD(nvram[1]));
	fprintf(fp, "- Control reg A: 0x%02x (10)\n", nvram[10]);
	fprintf(fp, "- Control reg B: 0x%02x (11)\n", nvram[11]);
	fprintf(fp, "- Status reg A:  0x%02x (12)\n", nvram[12]);
	fprintf(fp, "- Status reg B:  0x%02x (13)\n", nvram[13]);
	fprintf(fp, "- Preferred OS:  0x%02x 0x%02x (14, 15)\n",
		nvram[14], nvram[15]);
	fprintf(fp, "- Language:      0x%02x (20)\n", nvram[20]);
	fprintf(fp, "- Keyboard layout:  0x%02x (21)\n", nvram[21]);
	fprintf(fp, "- Date/time format: 0x%02x (22)\n", nvram[22]);
	fprintf(fp, "- Date separator:   0x%02x (23)\n", nvram[23]);
	fprintf(fp, "- Video mode:  0x%02x 0x%02x (28, 19)\n",
		nvram[28], nvram[29]);
	fprintf(fp, "- SCSI ID: %d, bus arbitration: %s (30)\n",
		nvram[30] & 0x7, nvram[30] & 128 ? "off" : "on");
}
