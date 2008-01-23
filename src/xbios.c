/*
  Hatari - xbios.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  XBios Handler (Trap #14)

  We intercept and direct some XBios calls to handle the RS-232 etc. and help
  with floppy debugging.
*/
const char XBios_rcsid[] = "Hatari $Id: xbios.c,v 1.14 2008-01-23 19:32:36 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "floppy.h"
#include "log.h"
#include "m68000.h"
#include "misc.h"
#include "rs232.h"
#include "screenSnapShot.h"
#include "stMemory.h"
#include "xbios.h"


#define XBIOS_DEBUG 0


/* List of Atari ST RS-232 baud rates */
static const int BaudRates[] =
{
	19200, /* 0 */
	9600,  /* 1 */
	4800,  /* 2 */
	3600,  /* 3 */
	2400,  /* 4 */
	2000,  /* 5 */
	1800,  /* 6 */
	1200,  /* 7 */
	600,   /* 8 */
	300,   /* 9 */
	200,   /* 10 */
	150,   /* 11 */
	134,   /* 12 */
	110,   /* 13 */
	75,    /* 14 */
	50     /* 15 */
};


/*-----------------------------------------------------------------------*/
/**
 * XBIOS Floppy Read
 * Call 8
 */
static BOOL XBios_Floprd(Uint32 Params)
{
#if XBIOS_DEBUG
	char *pBuffer;
	Uint16 Dev,Sector,Side,Track,Count;

	/* Read details from stack */
	pBuffer = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
	Dev = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG);
	Sector = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD);
	Track = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD);
	Side = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Count = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);

	Log_Printf(LOG_DEBUG, "FLOPRD %s,%d,%d,%d,%d at addr 0x%X\n", EmulationDrives[Dev].szFileName,
	           Side, Track, Sector, Count, M68000_GetPC());
#endif

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * XBIOS Floppy Write
 * Call 9
 */
static BOOL XBios_Flopwr(Uint32 Params)
{
#if XBIOS_DEBUG
	char *pBuffer;
	Uint16 Dev,Sector,Side,Track,Count;

	/* Read details from stack */
	pBuffer = (char *)STRAM_ADDR(STMemory_ReadLong(Params+SIZE_WORD));
	Dev = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG);
	Sector = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD);
	Track = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD);
	Side = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Count = STMemory_ReadWord(Params+SIZE_WORD+SIZE_LONG+SIZE_LONG+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);

	Log_Printf(LOG_DEBUG, "FLOPWR %s,%d,%d,%d,%d at addr 0x%X\n", EmulationDrives[Dev].szFileName,
	           Side, Track, Sector, Count, M68000_GetPC());
#endif

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * XBIOS RsConf
 * Call 15
 */
static BOOL XBios_Rsconf(Uint32 Params)
{
	short int Baud,Ctrl,Ucr,Rsr,Tsr,Scr;

	Baud = STMemory_ReadWord(Params+SIZE_WORD);
	Ctrl = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD);
	Ucr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Rsr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Tsr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);
	Scr = STMemory_ReadWord(Params+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD+SIZE_WORD);

	/* Set baud rate and other configuration, if RS232 emaulation is enabled */
	if (ConfigureParams.RS232.bEnableRS232)
	{
		if (Baud>=0 && Baud<=15)
		{
			/* Convert ST baud rate index to value */
			int BaudRate = BaudRates[Baud];
			/* And set new baud rate: */
			RS232_SetBaudRate(BaudRate);
		}

		if (Ucr != -1)
		{
			RS232_HandleUCR(Ucr);
		}

		if (Ctrl != -1)
		{
			RS232_SetFlowControl(Ctrl);
		}

		return TRUE;
	}

	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * XBIOS Scrdmp
 * Call 20
 */
static BOOL XBios_Scrdmp(Uint32 Params)
{
	fprintf(stderr, "XBIOS screendump!\n");

	ScreenSnapShot_SaveScreen();

	/* Correct return code? */
	Regs[REG_D0] = 0;

	return TRUE;
}


/*----------------------------------------------------------------------- */
/**
 * XBIOS Prtblk
 * Call 36
 */
static BOOL XBios_Prtblk(Uint32 Params)
{
	fprintf(stderr, "Intercepted XBIOS Prtblk()\n");

	/* Correct return code? */
	Regs[REG_D0] = 0;

	return TRUE;
}


/*-----------------------------------------------------------------------*/
/**
 * Check if we need to re-direct XBios call to our own routines
 */
BOOL XBios(void)
{
	Uint32 Params;
	Uint16 XBiosCall;

	/* Find call */
	Params = Regs[REG_A7];
	XBiosCall = STMemory_ReadWord(Params);

	/*Log_Printf(LOG_DEBUG, "XBIOS %d\n",XBiosCall);*/

	switch (XBiosCall)
	{
	 case 8:
		return XBios_Floprd(Params);
	 case 9:
		return XBios_Flopwr(Params);
	 case 15:
		return XBios_Rsconf(Params);
	 case 20:
		return XBios_Scrdmp(Params);
	 case 36:
		return XBios_Prtblk(Params);

	 default:  /* Call as normal! */
		return FALSE;
	}
}
