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

  The Atari TT uses the MC146818A chip
  The Atari Falcon uses the DS1287 chip, which is pin compatible with the MC146818A

  The MC146818A address space is made of 2 parts :
   - 14 bytes used by the RTC and to control alarm, timer, irq, ...
   - 50 bytes of non volatile RAM that can be used by the OS to store
     various settings

  These are the important bytes in the nvram array:

  MC146818/RTC specific bytes :

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

  OS specific bytes :

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

  See: https://www.nxp.com/docs/en/data-sheet/MC146818.pdf


  Pins :
    - CKOUT is not connected
    - SQW Out is not connected
    - IRQ :
       - on TT, IRQ is connected to the 2nd MFP on GPIP6 using the XRTCIRQ line
       - on Falcon, IRQ is not connected

  Input Clock :
    - On TT, the MC146818A is connected to an external freq on OSC1 at 32.768 kHz
    - On Falcon, the DS1287 is not connected to any external freq on OSC1, it uses
      its own internal oscillator

  The clock is kept updated using a small battery cell connected to the chip (even
  when the TT / Falcon is powered OFF)
  When booting, the TOS (Atari one or EmuTOS) will test the battery state using
  bit 7 VRT in register D. If this bit is set, the TOS assumes the date/time
  are correct and will copy them to the gemdos variables.
  If VRT is not set, TOS will init the RTC using the release date stored in the TOS image
  (for example TOS 3.06 will use 1989/06/08 00:00:00)

  On Falcon, the DS1287 is not connected to an external battery, the chip includes
  itw own battery to keep the RTC and the RAM updated even when power is OFF


  Not implemented (as no known use-case):

  - SQW

  - DSE
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
#include "m68000.h"
#include "cycInt.h"
#include "video.h"
#include "mfp.h"
#include "clocks_timings.h"

// Defs for NVRAM control register A (10) bits (read/write, except UIP)
#define REG_BIT_UIP  0x80	/* update-in-progress */
#define REG_DV_MASK 0x70	/* divider control, bits 4,5,6 */
#define REG_RS_MASK 0x0f	/* rate select, bits 0,1,2,3 */

// Defs for NVRAM control register B (11) bits (read/write)
#define REG_BIT_DSE  0x01	/* daylight saving enable (ignored) */
#define REG_BIT_24H  0x02	/* 24/12h clock, 1=24h */
#define REG_BIT_DM   0x04	/* data mode: 1=BIN, 0=BCD */
#define REG_BIT_SQWE 0x08	/* square wave enable, signal to SQW pin */
#define REG_BIT_UIE  0x10	/* update-ended interrupt enable */
#define REG_BIT_AIE  0x20	/* alarm interrupt enable */
#define REG_BIT_PIE  0x40	/* periodic interrupt enable */
#define REG_BIT_SET  0x80	/* suspend RTC updates to set clock values */

// Defs for NVRAM status register C (12) bits (read only)
// bits 0-3 are always 0
#define REG_BIT_UF   0x10	/* update-ended interrupt flag */
#define REG_BIT_AF   0x20	/* alarm interrupt flag */
#define REG_BIT_PF   0x40	/* periodic interrupt flag */
#define REG_BIT_IRQF 0x80	/* interrupt request flag */

// Defs for NVRAM status register D (13) bits (read only)
// bits 0-6 are always 0
#define REG_BIT_VRT  0x80	/* valid RAM and time */


#define		MC146818_IRQ_ON				0	/* O/low sets IRQ line */
#define		MC146818_IRQ_OFF			1	/* 1/high clears IRQ line */

#define		MC146818_UPDATE_DURATION_US		1984	/* 1984 us to update the clock on every second */

static uint8_t	MC146818_IRQ_Line;


// Defs for checksum
#define CKS_RANGE_START	14
#define CKS_RANGE_END	(14+47)
#define CKS_POS_BYTE1	62
#define CKS_POS_BYTE2	63

#define NVRAM_START  14
#define NVRAM_LEN    50

static uint8_t nvram[64] = {
	48, 255, 21, 255, 23, 255, 1, 25, 3, 33, /* clock/alarm registers */
	42, REG_BIT_DM|REG_BIT_24H, 0, REG_BIT_VRT, /* regs A-D */
	0,0,0,0,0,0,0,0,17,46,32,1,255,0,1,10,135,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};





static char		nvram_filename[FILENAME_MAX];

static uint8_t		nvram_index;
static int		year_offset;
static uint8_t		dse_done;


static int64_t		Clock_micro;				/* Incremented every VBL to update the RTC every second */


static struct tm*	getFrozenTime ( void );
static uint8_t		convert_24h_ampm ( uint8_t hour , uint8_t *pPM_flag );
static uint8_t		convert_ampm_24h ( uint8_t hour , uint8_t pm_flag );
static uint8_t		bin2BCD ( uint8_t value );
static uint8_t		BCD2bin ( uint8_t value );

static void		NvRam_Clock_Init ( void );
static void		NvRam_Clock_Check_Alarm ( void );



/*-----------------------------------------------------------------------*/
/**
 * Load NVRAM data from file
 * This restores bytes 0x0E - 0x3F and leaves the RTC unchanged (bytes 0x00 - 0x0D)
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
 * This saves bytes 0x0E - 0x3F, not the RTC (bytes 0x00 - 0x0D)
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
 * Create NVRAM checksum. The checksum is over all bytes from 0x0E to 0x3D except the
 * checksum bytes themselves at 0x3E and 0x3F
 */
static void NvRam_SetChecksum(void)
{
	int i;
	unsigned char sum = 0;
	
	for(i = CKS_RANGE_START; i <= CKS_RANGE_END; ++i)
		sum += nvram[i];
	nvram[CKS_POS_BYTE1] = ~sum;
	nvram[CKS_POS_BYTE2] = sum;
}



/*-----------------------------------------------------------------------*/
/**
 * Set or reset the MC146818's IRQ signal
 * IRQ signal is inverted (0/low sets irq, 1/high clears irq)
 *  - On TT, IRQ pin is connected to the 2nd MFP GPIP6
 *  - On Falcon, IRQ pin is not connected
 */
static void     MC146818_Set_Line_IRQ ( uint8_t bit )
{
        LOG_TRACE ( TRACE_NVRAM, "nvram set irq line val=%d %s VBL=%d HBL=%d\n" , bit , bit?"off":"on" , nVBLs , nHBL );

	if (!Config_IsMachineTT())
		return;

	MC146818_IRQ_Line = bit;

	if ( bit == MC146818_IRQ_ON )			// 0
	{
		MFP_GPIP_Set_Line_Input ( pMFP_TT , MFP_TT_GPIP_LINE_RTC , MFP_GPIP_STATE_LOW );
	}
	else						// 1
	{
		MFP_GPIP_Set_Line_Input ( pMFP_TT , MFP_TT_GPIP_LINE_RTC , MFP_GPIP_STATE_HIGH );
	}
}



static void	MC146818_Update_IRQ ( void )
{
	uint8_t		interrupt_enable;
	uint8_t		interrupt_flag;
	uint8_t		IRQ_new;

//fprintf ( stderr , "scc update irq wr9=$%02x ius=$%02x rr3=$%02x irq_in=%d pc=%x\n" , SCC.Chn[0].WR[9] , SCC.IUS , SCC.Chn[0].RR[3] , SCC.IRQ_Line , M68000_GetPC() );

	/* which interrupts are enabled to raise IRQ ? */
	interrupt_enable = nvram[0x0b] & ( REG_BIT_UIE | REG_BIT_AIE  | REG_BIT_PIE );
	/* which interrupt conditions are set ? */
        interrupt_flag = nvram[0x0c] & ( REG_BIT_UF | REG_BIT_AF  | REG_BIT_PF );

	if ( interrupt_enable & interrupt_flag )	/* bits position are the same in _enable and _flag */
	{
		nvram[0x0c] |= REG_BIT_IRQF;
		IRQ_new = MC146818_IRQ_ON;
	}
	else
	{
		nvram[0x0c] &= ~REG_BIT_IRQF;
		IRQ_new = MC146818_IRQ_OFF;
	}

	LOG_TRACE ( TRACE_NVRAM, "nvram update irq_new=%d VBL=%d HBL=%d\n" , IRQ_new , nVBLs , nHBL );

	/* Update IRQ line if needed */
	if ( IRQ_new != MC146818_IRQ_Line )
		MC146818_Set_Line_IRQ ( IRQ_new );
}




/*-----------------------------------------------------------------------*/
/**
 * NvRam_Reset: Called during init and reset, used for resetting the
 * emulated chip.
 *
 * This can also force some values in RAM depending on the current video mode
 */
void NvRam_Reset( bool bCold)
{
	/*
	 * Reset the chip
	 */

	/* On power up, we clear control regs (this is not documented in the */
	/* datasheet but we do it to avoid random behaviour) */
	if ( bCold )
	{
	      nvram[0x0a] = nvram[0x0b] = nvram[0x0c] = nvram[0x0d] = 0x00;
	}

	/* clear SWQE + interrupt enable bits */
	nvram[0x0b] &= ~(REG_BIT_SQWE|REG_BIT_UIE|REG_BIT_AIE|REG_BIT_PIE);

	/* clear all interrupt flags */
	nvram[0x0c] &= ~(REG_BIT_UF|REG_BIT_AF|REG_BIT_PF|REG_BIT_IRQF);

	MC146818_Set_Line_IRQ ( MC146818_IRQ_OFF );	/* IRQ line goes high */

	nvram_index = 0;


	/* Set some default values in RAM, depending on the current video mode */
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
	NvRam_Reset( true );

	/* Set suitable tm->tm_year offset and init the RTC
	 * (tm->tm_year starts from 1900, NVRAM year from 1968)
	 */
	year_offset = 68;
	if (ConfigureParams.System.nRtcYear)
	{
		time_t ticks = time(NULL);
		int year = 1900 + localtime(&ticks)->tm_year;
		year_offset += year - ConfigureParams.System.nRtcYear;
	}

	NvRam_Clock_Init();
	Clock_micro = 0;
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
/**
 * Init the RTC content with the host date/time
 * Date/time will then be increased every second by NvRam_Clock_Update
 *
 * We set REG_BIT_VRT in reg D to tell the TOS that the RTC is valid
 */
static void NvRam_Clock_Init ( void )
{
	uint8_t		hour;
	uint8_t		pm_flag;

	nvram[0] = bin2BCD(getFrozenTime()->tm_sec);
	nvram[2] = bin2BCD(getFrozenTime()->tm_min);

	hour = getFrozenTime()->tm_hour;
	hour = convert_24h_ampm ( hour , &pm_flag);	/* take into account 24H or AM/PM for hour */
	nvram[4] = bin2BCD(hour) | pm_flag;

	nvram[6] = bin2BCD(getFrozenTime()->tm_wday + 1);
	nvram[7] = bin2BCD(getFrozenTime()->tm_mday);
	nvram[8] = bin2BCD(getFrozenTime()->tm_mon + 1);
	nvram[9] = bin2BCD(getFrozenTime()->tm_year - year_offset);

	nvram[0x0d] |= REG_BIT_VRT;

fprintf ( stderr , "nvram clock init : %02d-%02d-%02d %d %02d:%02d:%02d\n" ,BCD2bin(nvram[9]),BCD2bin(nvram[8]),BCD2bin(nvram[7]),BCD2bin(nvram[6]),BCD2bin(nvram[4]&0x7f),BCD2bin(nvram[2]),BCD2bin(nvram[0]) );
}




/*
 * Update the RTC values on every second
 *
 * Year is 0-99 and is supposed to be relative to 1900 (although that's not
 * mentioned in the datasheet). So "68" means "1968" and this is a leap year.
 * Later TOS and EmuTOS are using "68" as the "year_offset" for the RTC,
 * because it allowed to represent dates from 1968 to 2068 and it was compatible
 * with the way the RTC handles leap year
 */

void NvRam_Clock_Update ( void )
{
	int64_t	FrameDuration_micro;
	uint8_t sec, min, hour, wday, day, month, year;
	uint8_t pm_flag;
	/* Max number of days per month */
	uint8_t day_max[ 12 ] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31  };
	uint8_t max;

	/* Check if more than 1 second passed since last increment of date/time */
	FrameDuration_micro = ClocksTimings_GetVBLDuration_micro ( ConfigureParams.System.nMachineType , nScreenRefreshRate );
	Clock_micro += FrameDuration_micro;

	/* Simulate clearing the UIP bit after a small delay of ~2 ms */
	/* NOTE : here we use the VBL duration, which is a much bigger delay of ~20 ms*/
	/* but this is enough to set / clear UIP regularly, in case a program */
	/* is monitoring the UIP bit before reading the RTC content */
	if ( ( Clock_micro >= MC146818_UPDATE_DURATION_US )	/* More than 1984 us since end of previous update */
	  && ( nvram[0x0a] & REG_BIT_UIP ) )			/* UIP not cleared yet */
	{
		nvram[0x0a] &= ~REG_BIT_UIP;
		nvram[0x0c] |=  REG_BIT_UF;			/* Set update-ended IRQ flag */
		MC146818_Update_IRQ();
	}

	if ( Clock_micro < 1000000 )
		return;						/* Less than 1 second, don't increment date/time yet */
	Clock_micro -= 1000000;

	/* Don't update RTC when SET bit is set in reg B */
	if ( nvram[0x0b] & REG_BIT_SET )
		return;

	/* Set UIP bit in reg A */
	nvram[0x0a] |= REG_BIT_UIP;

	sec =	BCD2bin( nvram[0] );
	min =	BCD2bin( nvram[2] );

	if ( nvram[0x0b] & REG_BIT_24H )
		hour =	BCD2bin( nvram[4] );			/* 24H mode */
	else
	{
		pm_flag = nvram[4] & 0x80;			/* 0=AM else PM */
		hour =	BCD2bin( nvram[4] & 0x7f );
		hour = convert_ampm_24h ( hour , pm_flag );
	}

	wday =	BCD2bin( nvram[6] );
	day =	BCD2bin( nvram[7] );
	month =	BCD2bin( nvram[8] );
	year =	BCD2bin( nvram[9] );

	if ( month > 12 )					/* ensure month is correct to access day_max[] */
		month = 12;

fprintf ( stderr , "nvram clock in : %02d-%02d-%02d %d %02d:%02d:%02d\n" , year, month, day, wday, hour, min, sec );


	/* Seconds */
	sec++;
	if ( sec <= 59 )
		goto done;
	sec = 0;

	/* Minutes */
	min++;
	if ( min <= 59 )
		goto done;
	min = 0;

	/* Hours */
	hour++;
	if ( hour <= 23 )
		goto done;
	hour = 0;

	/* Day of week 1..7 */
	wday++ ;
	if ( wday > 7 )
		wday = 1;

	/* Day of month 1..31 */
	day++;
	max = day_max[ month-1 ];

	if ( ( month == 2 ) && ( ( year % 4 ) == 0 ) )		/* february and leap year (eg 68) */
		max = 29;

	if ( day <= max )
		goto done;
	day = 1;

	/* Month 1..12 */
	month++;
	if ( month <= 12 )
		goto done;
	month = 1;

	/* Year 0..99 */
	year++;
	if ( year <= 99 )
		goto done;
	year = 0;

done:

	/*
	 * Special case if daylight saving is enabled
	 *  - on the last sunday of april 02:00:00 AM becomes 03:00:00
	 *  - on the last sunday of october 02:00:00 AM becomes 01:00:00
	 */
	if ( nvram[0x0b] & REG_BIT_DSE )
	{
		if ( ( month == 4 ) && ( wday == 1 ) && ( day >= 24 )		/* last sunday of april */
		  && ( hour == 2 ) && ( min == 0 ) && ( sec == 0 ) )		/* 02:00:00 */
			hour = 3;

		else if ( ( month == 10 ) && ( wday == 1 ) && ( day >= 25 )	/* last sunday of october */
		  && ( hour == 2 ) && ( min == 0 ) && ( sec == 0 )		/* 02:00:00 */
		  && ( dse_done == 0 ) )					/* ensure we don't loop on 02:00:00 -> 01:00:00 */
			{ hour = 1; dse_done = 1; }

		else if ( ( hour >= 2 ) && ( dse_done == 1 ) )			/* >= 02:00:00 */
			dse_done = 0;						/* Any hour after 02:00:00 reset dse_done */
	}


fprintf ( stderr , "nvram clock out : %02d-%02d-%02d %d %02d:%02d:%02d\n" , year, month, day, wday, hour, min, sec );

	nvram[0] = bin2BCD( sec );
	nvram[2] = bin2BCD( min );

	hour = convert_24h_ampm ( hour , &pm_flag);	/* take into account 24H or AM/PM for hour */
	nvram[4] = bin2BCD(hour) | pm_flag;

	nvram[6] = bin2BCD( wday );
	nvram[7] = bin2BCD( day );
	nvram[8] = bin2BCD( month );
	nvram[9] = bin2BCD( year );

	NvRam_Clock_Check_Alarm();
}



/*
 * Check if the alarm hour/min/sec is matching the current time in the RTC
 * If so, set the AF bit in reg C. Else clear AF bit.
 *
 * If an alarm field has bit 6-7 set (ie >= 0xc0) then it's considered as "don't care"
 * and match any corresponding value
 */

void NvRam_Clock_Check_Alarm ( void )
{
	if ( ( ( nvram[0] == nvram[1] ) || ( nvram[1] >= 0xc0 ) )	/* sec */
	  && ( ( nvram[2] == nvram[3] ) || ( nvram[3] >= 0xc0 ) )	/* min */
	  && ( ( nvram[4] == nvram[5] ) || ( nvram[5] >= 0xc0 ) ) )	/* hour */
		nvram[0x0c] |= REG_BIT_AF;
	else
		nvram[0x0c] &= ~REG_BIT_AF;

	MC146818_Update_IRQ();
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
	if (nvram[0x0b] & REG_BIT_SET)
		return refreshFrozenTime(false);
	else
		return refreshFrozenTime(true);
}




/*
 * Convert 'hour' between 24h mode and AM/PM mode, depending on REG_BIT_24H bit in reg B
 *
 * - in 24H mode, hour is 0 .. 23
 * - in AM/PM mode, hour is 1 .. 12
 *
 * 12:00 AM is 00:00 24h
 * 12:00 PM is 12:00 24h
 */

/* input : hour 0..23
 * output : hour 0..23 or 1..12 with am/pm flag
 */
static uint8_t convert_24h_ampm ( uint8_t hour , uint8_t *pPM_flag )
{
	if ( (nvram[0x0b] & REG_BIT_24H) == 0 )		/* AM/PM mode, hour = 1 ... 12 */
	{
		*pPM_flag = (hour == 0 || hour >= 13) ? 0x80 : 0;
		hour = hour % 12;
		if (hour == 0)
			hour = 12;
	}
	else						/* 24H mode, hour = 0 ... 23 */
		*pPM_flag = 0;

	return hour;
}


/* input : hour 0..23 or 1..12 with am/pm flag
 * output : hour 0..23
 */
static uint8_t convert_ampm_24h ( uint8_t hour , uint8_t pm_flag )
{
	if ( (nvram[0x0b] & REG_BIT_24H) == 0 )		/* AM/PM mode, hour = 1 ... 12 */
	{
		if ( pm_flag == 0 )
		{
			if ( hour == 12 )		/* 12 AM -> 00 ; 1..11 AM -> no change */
				hour = 0;
		}
		else
		{
			if ( hour != 12 )		/* 12 PM -> 12 ; 1..11 PM -> 13..23 */
				hour += 12;
		}
	}

	return hour;
}



/**
 * If NVRAM data mode bit is set, returns given value as binary
 * otherwise returns it as BCD.
 */
static uint8_t bin2BCD(uint8_t value)
{
	if ((nvram[0x0b] & REG_BIT_DM))
		return value;
	return ((value / 10) << 4) | (value % 10);
}


static uint8_t BCD2bin(uint8_t value)
{
	if ((nvram[0x0b] & REG_BIT_DM))
		return value;
	return ( value >> 4 ) * 10 + (value & 0x0f);
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
	case 0:					/* second */
	case 2:					/* minute */
	case 4:					/* hour */
	case 6:					/* wday */
	case 7:					/* day */
	case 8:					/* month */
	case 9:					/* year */
		value = nvram[nvram_index];
		break;

	case 1:					/* alarm second */
	case 3:					/* alarm minute */
	case 5:					/* alarm hour */
		value = nvram[nvram_index];
		break;

	case 0x0a:
		/* control reg A
		 * read-only UIP bit + clock dividers & rate selectors
		 */
		value = nvram[nvram_index];
		break;
	case 0x0b:
		/* control reg B
		 * set, interrupt enable, sqw enable, clock mode, daylight savings bits
		 */
		value = nvram[nvram_index];
		break;
	case 0x0c:
		/* status reg C, read-only
		 * bits 4-7 interrupt status bits, bits 0-3 unused/zero
		 * register is cleared after read and irq is updated
		 */
		value = nvram[nvram_index];
		nvram[0x0c] = 0;
		MC146818_Update_IRQ();
		break;
	case 0x0d:
		/* status reg D, read-only
		 * Valid RAM and Time bit, rest of bits are zero/unused
		 */
		value = nvram[nvram_index];
		break;

	default:				/* RAM 0x0E - 0x3F */
		value = nvram[nvram_index];
		break;
	}

	LOG_TRACE(TRACE_NVRAM, "NVRAM: read data at %02x = %d ($%02x) pc=%x\n", nvram_index, value, value, M68000_GetPC());
	IoMem_WriteByte(0xff8963, value);
}


/*-----------------------------------------------------------------------*/
/**
 * Write to RTC/NVRAM data register ($ff8963)
 */

void NvRam_Data_WriteByte(void)
{
	uint8_t value = IoMem_ReadByte(0xff8963);

	switch (nvram_index)
	{
	case 0x00:
	case 0x02:
	case 0x04:
	case 0x06:
	case 0x07:
	case 0x08:
	case 0x09:
		/* Change RTC values */
		/* Don't do anything here, it will be done during next call to NvRam_Clock_Update() */
		break;

	case 0x01:
	case 0x03:
	case 0x05:
		/* Change alarm hour/min/sec */
		/* Don't check alarm here, it will be done during next call to NvRam_Clock_Update() */
		break;

	case 0x0a:
		/* UIP bit is read-only, we keep its value from reg A */
		value = (value & ~REG_BIT_UIP) | (nvram[10] & REG_BIT_UIP);
		break;
	case 0x0b:
		/* Hatari specific code to automatically re-init RTC with default host date/time */
		/* each time DM or 12/24 modes are changed */
		uint8_t old_dm_24 = nvram[0x0b] & ( REG_BIT_24H | REG_BIT_DM );
		uint8_t new_dm_24 = value & ( REG_BIT_24H | REG_BIT_DM );
		if ( old_dm_24 != new_dm_24 )
			NvRam_Clock_Init();

		/* If Update is suspended, then UIP bit is cleared */
		if (value & REG_BIT_SET)
			nvram[0x0a] &= ~REG_BIT_UIP;
		break;
	case 0x0c:
	case 0x0d:
		IoMem_WriteByte(0xff8963, nvram[nvram_index]);
		Log_Printf(LOG_WARN, "Ignored write %d ($%02x) to read-only RTC/NVRAM status register %02x!\n",
			   value, value, nvram_index);
		return;
	}
	LOG_TRACE(TRACE_NVRAM, "NVRAM: write data at %02x = %d ($%02x) pc=%x\n", nvram_index, value, value, M68000_GetPC());
	nvram[nvram_index] = value;
}


void NvRam_Info(FILE *fp, uint32_t dummy)
{
	fprintf(fp, "- File: '%s'\n", nvram_filename);
	fprintf(fp, "- Time: from host (regs: 0, 2, 4, 6-9)\n");
	fprintf(fp, "- Alarm: %02d:%02d:%02d (1, 3, 5)\n",
		bin2BCD(nvram[5]), bin2BCD(nvram[3]), bin2BCD(nvram[1]));
	fprintf(fp, "- Control reg A: 0x%02x (10)\n", nvram[0x0a]);
	fprintf(fp, "- Control reg B: 0x%02x (11)\n", nvram[0x0b]);
	fprintf(fp, "- Status reg A:  0x%02x (12)\n", nvram[0x0c]);
	fprintf(fp, "- Status reg B:  0x%02x (13)\n", nvram[0x0d]);

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

int NvRam_GetKbdLayoutCode(void)
{
	return nvram[NVRAM_KEYBOARDLAYOUT];
}
