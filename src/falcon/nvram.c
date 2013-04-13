/*
  Hatari - nvram.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file is partly based on GPL code taken from the Aranym project.
  - Copyright (c) 2001-2004 Petr Stehlik of ARAnyM dev team
  - Adaption to Hatari (c) 2006 by Thomas Huth

  Atari TT and Falcon NVRAM/RTC emulation code.
  This is a MC146818A or compatible chip with a non-volatile RAM area.

  These are the important bytes in the nvram array:

  Byte:    Description:

  14-15    Preferred operating system (TOS, Unix)
   20      Language
   21      Keyboard layout
   22      Format of date/time
   23      Separator for date
   24      Boot delay
  28-29    Video mode
   30      SCSI-ID in bits 0-2, bus arbitration flag in bit 7 (1=off, 0=on)
  62-63    Checksum

  All other cells are reserved / unused.
*/
const char NvRam_fileid[] = "Hatari nvram.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "log.h"
#include "nvram.h"
#include "paths.h"
#include "vdi.h"


// Defs for checksum
#define CKS_RANGE_START	14
#define CKS_RANGE_END	(14+47)
#define CKS_RANGE_LEN	(CKS_RANGE_END-CKS_RANGE_START+1)
#define CKS_LOC			(14+48)

#define NVRAM_START  14
#define NVRAM_LEN    50

static Uint8 nvram[64] = { 48,255,21,255,23,255,1,25,3,33,42,14,112,128,
	0,0,0,0,0,0,0,0,17,46,32,1,255,0,1,10,135,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };


static Uint8 nvram_index;
static char nvram_filename[FILENAME_MAX];


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
		Uint8 fnvram[NVRAM_LEN];
		if (fread(fnvram, 1, NVRAM_LEN, f) == NVRAM_LEN)
		{
			memcpy(nvram+NVRAM_START, fnvram, NVRAM_LEN);
			ret = true;
		}
		fclose(f);
		Log_Printf(LOG_DEBUG, "NVRAM loaded from '%s'\n", nvram_filename);
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
			ret = true;
		}
		fclose(f);
	}
	else
	{
		Log_Printf(LOG_WARN, "ERROR: cannot store NVRAM to '%s'\n", nvram_filename);
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
			 * which is an invalide video mode. This does not matter,
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
		NvRam_SetChecksum();
	}

	NvRam_Reset();
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
	Uint8 value = IoMem_ReadByte(0xff8961);

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
/**
 * Read from RTC/NVRAM data register ($ff8963)
 */
void NvRam_Data_ReadByte(void)
{
	Uint8 value = 0;

	if (nvram_index == NVRAM_SECONDS || nvram_index == NVRAM_MINUTES || nvram_index == NVRAM_HOURS
	    || (nvram_index >=NVRAM_DAY && nvram_index <=NVRAM_YEAR) )
	{
		/* access to RTC?  - then read host clock and return its values */
		time_t tim = time(NULL);
		struct tm *curtim = localtime(&tim);	/* current time */
		switch(nvram_index)
		{
			case NVRAM_SECONDS: value = curtim->tm_sec; break;
			case NVRAM_MINUTES: value = curtim->tm_min; break;
			case NVRAM_HOURS: value = curtim->tm_hour; break;
			case NVRAM_DAY: value = curtim->tm_mday; break;
			case NVRAM_MONTH: value = curtim->tm_mon+1; break;
			case NVRAM_YEAR: value = curtim->tm_year - 68; break;
		}
	}
	else if (nvram_index == 10)
	{
		static bool rtc_uip = true;
		value = rtc_uip ? 0x80 : 0;
		rtc_uip = !rtc_uip;
	}
	else if (nvram_index == 13)
	{
		value = 0x80;   // Valid RAM and Time bit
	}
	else if (nvram_index < 14)
	{
		Log_Printf(LOG_DEBUG, "Read from unsupported RTC/NVRAM register 0x%x.\n", nvram_index);
		value = nvram[nvram_index];
	}
	else
	{
		value = nvram[nvram_index];
	}

	LOG_TRACE(TRACE_NVRAM, "NVRAM : read data at %d = %d ($%02x)\n", nvram_index, value, value);
	IoMem_WriteByte(0xff8963, value);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to RTC/NVRAM data register ($ff8963)
 */

void NvRam_Data_WriteByte(void)
{
	Uint8 value = IoMem_ReadByte(0xff8963);
	LOG_TRACE(TRACE_NVRAM, "NVRAM : write data at %d = %d ($%02x)\n", nvram_index, value, value);
	nvram[nvram_index] = value;
}
