/*
  Hatari - ioMem.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This is where we intercept read/writes to/from the hardware. The ST's memory
  is nicely split into four main parts - the bottom area of RAM is for user
  programs. This is followed by a large area which causes a Bus Error. After
  this is the ROM addresses for TOS and finally an area for hardware mapping.
  To gain speed any address in the user area can simply read/write, but anything
  above this range needs to be checked for validity and sent to the various
  handlers.
  A big problem for ST emulation is the use of the hardware registers. These
  often consist of an 'odd' byte in memory and is usually addressed as a single
  byte. A number of applications, however, write to the address using a word or
  even long word. So we have a list of handlers that take care of each address
  that has to be intercepted. Eg, a long write to a PSG register (which access
  two registers) will write the long into IO memory space and then call the two
  handlers which read off the bytes for each register.
  This means that any access to any hardware register in such a way will work
  correctly - it certainly fixes a lot of bugs and means writing just one
  routine for each hardware register we mean to intercept! Phew!
  You have also to take into consideration that some hardware registers are
  bigger than 1 byte (there are also word and longword registers) and that
  a lot of addresses in between can cause a bus error - so it's not so easy
  to cope with all type of handlers in a straight forward way.
  Also note the 'mirror' (or shadow) registers of the PSG - this is used by most
  games.
*/
char IoMem_rcsid[] = "Hatari $Id: ioMem.c,v 1.2 2005-01-29 22:42:00 thothy Exp $";

#include <SDL_types.h>

#include "main.h"
#include "debug.h"
#include "fdc.h"
#include "int.h"
#include "ikbd.h"
#include "ioMem.h"
#include "m68000.h"
#include "mfp.h"
#include "midi.h"
#include "psg.h"
#include "rtc.h"
#include "rs232.h"
#include "screen.h"
#include "video.h"
#include "blitter.h"
#include "uae-cpu/sysdeps.h"


#define IOMEM_DEBUG 0

#if IOMEM_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif


/* Hardware address details */
typedef struct
{
	Uint32 Address;              /* ST hardware address */
	int SpanInBytes;             /* E.g. SIZE_BYTE, SIZE_WORD or SIZE_LONG */
	void *ReadFunc;              /* Read function */
	void *WriteFunc;             /* Write function */
} INTERCEPT_ACCESS_FUNC;



/*-----------------------------------------------------------------------*/
/* List of functions to handle read/write hardware intercepts. */
INTERCEPT_ACCESS_FUNC InterceptAccessFuncs[] =
{
 	{ 0xff8001, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Memory configuration */

 	{ 0xff8201, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Video base high byte */
 	{ 0xff8203, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Video base med byte */
 	{ 0xff8205, SIZE_BYTE, Video_ScreenCounterHigh_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8207, SIZE_BYTE, Video_ScreenCounterMed_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8209, SIZE_BYTE, Video_ScreenCounterLow_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff820a, SIZE_BYTE, Video_Sync_ReadByte, Video_Sync_WriteByte },
	{ 0xff820d, SIZE_BYTE, Video_BaseLow_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff820f, SIZE_BYTE, Video_LineWidth_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8240, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color0_WriteWord },         /* COLOR 0 */
	{ 0xff8242, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color1_WriteWord },         /* COLOR 1 */
	{ 0xff8244, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color2_WriteWord },         /* COLOR 2 */
	{ 0xff8246, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color3_WriteWord },         /* COLOR 3 */
	{ 0xff8248, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color4_WriteWord },         /* COLOR 4 */
	{ 0xff824a, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color5_WriteWord },         /* COLOR 5 */
	{ 0xff824c, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color6_WriteWord },         /* COLOR 6 */
	{ 0xff824e, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color7_WriteWord },         /* COLOR 7 */
	{ 0xff8250, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color8_WriteWord },         /* COLOR 8 */
	{ 0xff8252, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color9_WriteWord },         /* COLOR 9 */
	{ 0xff8254, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color10_WriteWord },        /* COLOR 10 */
	{ 0xff8256, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color11_WriteWord },        /* COLOR 11 */
	{ 0xff8258, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color12_WriteWord },        /* COLOR 12 */
	{ 0xff825a, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color13_WriteWord },        /* COLOR 13 */
	{ 0xff825c, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color14_WriteWord },        /* COLOR 14 */
	{ 0xff825e, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color15_WriteWord },        /* COLOR 15 */
 	{ 0xff8260, SIZE_BYTE, Video_ShifterMode_ReadByte, Video_ShifterMode_WriteByte },

	{ 0xff8604, SIZE_WORD, FDC_DiscControllerStatus_ReadWord, FDC_DiscController_WriteWord },
	{ 0xff8606, SIZE_WORD, FDC_DmaStatus_ReadWord, FDC_DmaModeControl_WriteWord },
 	{ 0xff8609, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA base and counter high byte */
 	{ 0xff860B, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA base and counter med byte  */
 	{ 0xff860D, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA base and counter low byte  */

	{ 0xff8800, SIZE_BYTE, PSG_SelectRegister_ReadByte, PSG_SelectRegister_WriteByte },
	{ 0xff8801, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception },
	{ 0xff8802, SIZE_BYTE, PSG_DataRegister_ReadByte, PSG_DataRegister_WriteByte },
	{ 0xff8803, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception },

	{ 0xff8a00, 32,        IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter halftone RAM */
	{ 0xff8a20, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter source x increment */
	{ 0xff8a22, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter source y increment */
	{ 0xff8a24, SIZE_LONG, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter source address */
	{ 0xff8a28, SIZE_WORD, Blitter_Endmask1_ReadWord, Blitter_Endmask1_WriteWord },
	{ 0xff8a2a, SIZE_WORD, Blitter_Endmask2_ReadWord, Blitter_Endmask2_WriteWord },
	{ 0xff8a2c, SIZE_WORD, Blitter_Endmask3_ReadWord, Blitter_Endmask3_WriteWord },
	{ 0xff8a2e, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter dest. x increment */
	{ 0xff8a30, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter dest. y increment */
	{ 0xff8a32, SIZE_LONG, Blitter_DestAddr_ReadLong, Blitter_DestAddr_WriteLong },
	{ 0xff8a36, SIZE_WORD, Blitter_WordsPerLine_ReadWord, Blitter_WordsPerLine_WriteWord },
	{ 0xff8a38, SIZE_WORD, Blitter_LinesPerBitblock_ReadWord, Blitter_LinesPerBitblock_WriteWord },
	{ 0xff8a3a, SIZE_BYTE, Blitter_HalftoneOp_ReadByte, Blitter_HalftoneOp_WriteByte },
	{ 0xff8a3b, SIZE_BYTE, Blitter_LogOp_ReadByte, Blitter_LogOp_WriteByte },
	{ 0xff8a3c, SIZE_BYTE, Blitter_LineNum_ReadByte, Blitter_LineNum_WriteByte },
	{ 0xff8a3d, SIZE_BYTE, Blitter_Skew_ReadByte, Blitter_Skew_WriteByte },

	{ 0xfffa01, SIZE_BYTE, MFP_GPIP_ReadByte, MFP_GPIP_WriteByte },
	{ 0xfffa03, SIZE_BYTE, MFP_ActiveEdge_ReadByte, MFP_ActiveEdge_WriteByte },
	{ 0xfffa05, SIZE_BYTE, MFP_DataDirection_ReadByte, MFP_DataDirection_WriteByte },
	{ 0xfffa07, SIZE_BYTE, MFP_EnableA_ReadByte, MFP_EnableA_WriteByte },
	{ 0xfffa09, SIZE_BYTE, MFP_EnableB_ReadByte, MFP_EnableB_WriteByte },
	{ 0xfffa0b, SIZE_BYTE, MFP_PendingA_ReadByte, MFP_PendingA_WriteByte },
	{ 0xfffa0d, SIZE_BYTE, MFP_PendingB_ReadByte, MFP_PendingB_WriteByte },
	{ 0xfffa0f, SIZE_BYTE, MFP_InServiceA_ReadByte, MFP_InServiceA_WriteByte },
	{ 0xfffa11, SIZE_BYTE, MFP_InServiceB_ReadByte, MFP_InServiceB_WriteByte },
	{ 0xfffa13, SIZE_BYTE, MFP_MaskA_ReadByte, MFP_MaskA_WriteByte },
	{ 0xfffa15, SIZE_BYTE, MFP_MaskB_ReadByte, MFP_MaskB_WriteByte },
	{ 0xfffa17, SIZE_BYTE, MFP_VectorReg_ReadByte, MFP_VectorReg_WriteByte },
	{ 0xfffa19, SIZE_BYTE, MFP_TimerACtrl_ReadByte, MFP_TimerACtrl_WriteByte },
	{ 0xfffa1b, SIZE_BYTE, MFP_TimerBCtrl_ReadByte, MFP_TimerBCtrl_WriteByte },
	{ 0xfffa1d, SIZE_BYTE, MFP_TimerCDCtrl_ReadByte, MFP_TimerCDCtrl_WriteByte },
	{ 0xfffa1f, SIZE_BYTE, MFP_TimerAData_ReadByte, MFP_TimerAData_WriteByte },
	{ 0xfffa21, SIZE_BYTE, MFP_TimerBData_ReadByte, MFP_TimerBData_WriteByte },
	{ 0xfffa23, SIZE_BYTE, MFP_TimerCData_ReadByte, MFP_TimerCData_WriteByte },
	{ 0xfffa25, SIZE_BYTE, MFP_TimerDData_ReadByte, MFP_TimerDData_WriteByte },

	{ 0xfffa27, SIZE_BYTE, RS232_SCR_ReadByte, RS232_SCR_WriteByte },    /* Sync character register */
	{ 0xfffa29, SIZE_BYTE, RS232_UCR_ReadByte, RS232_UCR_WriteByte },    /* USART control register */
	{ 0xfffa2b, SIZE_BYTE, RS232_RSR_ReadByte, RS232_RSR_WriteByte },    /* Receiver status register */
	{ 0xfffa2d, SIZE_BYTE, RS232_TSR_ReadByte, RS232_TSR_WriteByte },    /* Transmitter status register */
	{ 0xfffa2f, SIZE_BYTE, RS232_UDR_ReadByte, RS232_UDR_WriteByte },    /* USART data register */

	{ 0xfffc00, SIZE_BYTE, IKBD_KeyboardControl_ReadByte, IKBD_KeyboardControl_WriteByte },
	{ 0xfffc02, SIZE_BYTE, IKBD_KeyboardData_ReadByte, IKBD_KeyboardData_WriteByte },
	{ 0xfffc04, SIZE_BYTE, Midi_Control_ReadByte, Midi_Control_WriteByte },
	{ 0xfffc06, SIZE_BYTE, Midi_Data_ReadByte, Midi_Data_WriteByte },

	{ 0xfffc21, SIZE_BYTE, Rtc_SecondsUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc23, SIZE_BYTE, Rtc_SecondsTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc25, SIZE_BYTE, Rtc_MinutesUnits_ReadByte, Rtc_MinutesUnits_WriteByte },
	{ 0xfffc27, SIZE_BYTE, Rtc_MinutesTens_ReadByte, Rtc_MinutesTens_WriteByte },
	{ 0xfffc29, SIZE_BYTE, Rtc_HoursUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc2b, SIZE_BYTE, Rtc_HoursTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc2d, SIZE_BYTE, Rtc_Weekday_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc2f, SIZE_BYTE, Rtc_DayUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc31, SIZE_BYTE, Rtc_DayTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc33, SIZE_BYTE, Rtc_MonthUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc35, SIZE_BYTE, Rtc_MonthTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc37, SIZE_BYTE, Rtc_YearUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc39, SIZE_BYTE, Rtc_YearTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc3b, SIZE_BYTE, Rtc_ClockMod_ReadByte, Rtc_ClockMod_WriteByte },
	{ 0xfffc3d, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Clock test */
	{ 0xfffc3f, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Clock reset */
};


void *pInterceptReadTable[0x8000];
void *pInterceptWriteTable[0x8000];

BOOL bEnableBlitter = FALSE;                  /* TRUE if blitter is enabled */

static int nBusErrorAccesses;                 /* Needed to count bus error accesses */


/*-----------------------------------------------------------------------*/
/*
  Create 'intercept' tables for hardware address access. Each 'intercept
  table is a list of 0x8000 pointers to a list of functions to call when
  that location in the ST's memory is accessed. 
*/
void IoMem_Init(void)
{
	Uint32 addr;
	int i;

	/* Set default IO access handler (-> bus error) */
	for (addr = 0xff8000; addr <= 0xffffff; addr++)
	{
		if (addr & 1)
		{
    		pInterceptReadTable[addr - 0xff8000] = IoMem_BusErrorOddReadAccess;     /* For 'read' */
	    	pInterceptWriteTable[addr - 0xff8000] = IoMem_BusErrorOddWriteAccess;   /* and 'write' */
		}
		else
		{
    		pInterceptReadTable[addr - 0xff8000] = IoMem_BusErrorEvenReadAccess;    /* For 'read' */
	    	pInterceptWriteTable[addr - 0xff8000] = IoMem_BusErrorEvenWriteAccess;  /* and 'write' */
		}
    }

	/* Now set the correct handlers */
	for (addr=0xff8000; addr <= 0xffffff; addr++)
	{
		/* Handle blitter */
		if (!bEnableBlitter && addr>=0xff8a00 && addr<0xff8a40)
			continue;    /* Ignore blitter area if blitter is disabled */

		/* Does this hardware location/span appear in our list of possible intercepted functions? */
		for (i=0; i<(sizeof(InterceptAccessFuncs)/sizeof(INTERCEPT_ACCESS_FUNC)); i++)
		{
			if (addr >= InterceptAccessFuncs[i].Address
			    && addr < InterceptAccessFuncs[i].Address+InterceptAccessFuncs[i].SpanInBytes)
			{
				/* Security checks... */
				if (pInterceptReadTable[addr-0xff8000] != IoMem_BusErrorEvenReadAccess && pInterceptReadTable[addr-0xff8000] != IoMem_BusErrorOddReadAccess)
					fprintf(stderr, "IoMem_Init: Warning: $%x (R) already defined\n", addr);
				if (pInterceptWriteTable[addr-0xff8000] != IoMem_BusErrorEvenWriteAccess && pInterceptWriteTable[addr-0xff8000] != IoMem_BusErrorOddWriteAccess)
					fprintf(stderr, "IoMem_Init: Warning: $%x (W) already defined\n", addr);

				/* This location needs to be intercepted, so add entry to list */
				pInterceptReadTable[addr-0xff8000] = InterceptAccessFuncs[i].ReadFunc;
				pInterceptWriteTable[addr-0xff8000] = InterceptAccessFuncs[i].WriteFunc;
			}
		}
    }

}


/*-----------------------------------------------------------------------*/
/*
  Uninitialize the IoMem code (currently unused).
*/
void IoMem_UnInit(void)
{
}


/*-----------------------------------------------------------------------*/
/*
  Enable/disable blitter emulation
*/
void Intercept_EnableBlitter(BOOL enableFlag)
{
	if(bEnableBlitter != enableFlag)
	{
		bEnableBlitter = enableFlag;
		/* Ugly hack: Enable/disable the blitter emulation by
		   re-init the interception tables... */
		IoMem_UnInit();
		IoMem_Init();
	}
}


/*-----------------------------------------------------------------------*/
/*
  Check if need to change our address as maybe a mirror register.
  Currently we only have a PSG mirror area.
*/
static unsigned long IoMem_CheckMirrorAddresses(Uint32 addr)
{
	if (addr>=0xff8800 && addr<0xff8900)    /* Is a PSG mirror registers? */
		addr = 0xff8800 + (addr & 3);       /* Bring into 0xff8800-0xff8804 range */

	return addr;
}



/*-----------------------------------------------------------------------*/
/*
  Handle byte read access from IO memory.
*/
uae_u32 IoMem_bget(uaecptr addr)
{
	Dprintf(("IoMem_bget($%x)\n", addr));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		return 0xff;
	}

	BusAddressLocation = addr;                    /* Store access location */
	nBusErrorAccesses = 0;
	addr = IoMem_CheckMirrorAddresses(addr);

	CALL_VAR(pInterceptReadTable[addr-0xff8000]); /* Call handler */

	/* Check if we read from a bus-error region */
	if (nBusErrorAccesses == 1)
	{
		M68000_BusError(addr, 1);
		return 0xff;
	}

	return IoMem[addr];
}


/*-----------------------------------------------------------------------*/
/*
  Handle word read access from IO memory.
*/
uae_u32 IoMem_wget(uaecptr addr)
{
	Uint32 idx;

	Dprintf(("IoMem_wget($%x)\n", addr));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		return 0xff;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	nBusErrorAccesses = 0;
	addr = IoMem_CheckMirrorAddresses(addr);

	idx = addr - 0xff8000;
	CALL_VAR(pInterceptReadTable[idx]);           /* Call 1st handler */
	if (pInterceptReadTable[idx+1] != pInterceptReadTable[idx])
		CALL_VAR(pInterceptReadTable[idx+1]);     /* Call 2nd handler */

	/* Check if we completely read from a bus-error region */
	if (nBusErrorAccesses == 2)
	{
		M68000_BusError(addr, 1);
		return 0xff;
	}

	return IoMem_ReadWord(addr);
}


/*-----------------------------------------------------------------------*/
/*
  Handle long-word read access from IO memory.
*/
uae_u32 IoMem_lget(uaecptr addr)
{
	Uint32 idx;

	Dprintf(("IoMem_lget($%x)\n", addr));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		return 0;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	nBusErrorAccesses = 0;
	addr = IoMem_CheckMirrorAddresses(addr);

	idx = addr - 0xff8000;
	CALL_VAR(pInterceptReadTable[idx]);           /* Call 1st handler */
	if (pInterceptReadTable[idx+1] != pInterceptReadTable[idx])
		CALL_VAR(pInterceptReadTable[idx+1]);     /* Call 2nd handler */
	if (pInterceptReadTable[idx+2] != pInterceptReadTable[idx+1])
		CALL_VAR(pInterceptReadTable[idx+2]);     /* Call 3rd handler */
	if (pInterceptReadTable[idx+3] != pInterceptReadTable[idx+2])
		CALL_VAR(pInterceptReadTable[idx+3]);     /* Call 4th handler */

	/* Check if we completely read from a bus-error region */
	if (nBusErrorAccesses == 4)
	{
		M68000_BusError(addr, 1);
		return 0xff;
	}

	return IoMem_ReadLong(addr);
}


/*-----------------------------------------------------------------------*/
/*
  Handle byte write access to IO memory.
*/
void IoMem_bput(uaecptr addr, uae_u32 val)
{
	Dprintf(("IoMem_bput($%x, $%x)\n", addr, val));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
		return;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	nBusErrorAccesses = 0;
	addr = IoMem_CheckMirrorAddresses(addr);
	IoMem[addr] = val;

	CALL_VAR(pInterceptWriteTable[addr-0xff8000]); /* Call handler */

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == 1)
	{
		M68000_BusError(addr, 0);
	}
}


/*-----------------------------------------------------------------------*/
/*
  Handle word write access to IO memory.
*/
void IoMem_wput(uaecptr addr, uae_u32 val)
{
	Uint32 idx;

	Dprintf(("IoMem_wput($%x, $%x)\n", addr, val));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
		return;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	nBusErrorAccesses = 0;
	addr = IoMem_CheckMirrorAddresses(addr);
	IoMem_WriteWord(addr, val);

	idx = addr - 0xff8000;
	CALL_VAR(pInterceptWriteTable[idx]);          /* Call handler */
	if (pInterceptWriteTable[idx+1] != pInterceptWriteTable[idx])
		CALL_VAR(pInterceptWriteTable[idx+1]);    /* Call 2nd handler */

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == 2)
	{
		M68000_BusError(addr, 0);
	}
}


/*-----------------------------------------------------------------------*/
/*
  Handle long-word write access to IO memory.
*/
void IoMem_lput(uaecptr addr, uae_u32 val)
{
	Uint32 idx;

	Dprintf(("IoMem_lput($%x, $%x)\n", addr, val));

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
		return;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	nBusErrorAccesses = 0;
	addr = IoMem_CheckMirrorAddresses(addr);
	IoMem_WriteLong(addr, val);

	idx = addr - 0xff8000;
	CALL_VAR(pInterceptWriteTable[idx]);          /* Call handler */
	if (pInterceptWriteTable[idx+1] != pInterceptWriteTable[idx])
		CALL_VAR(pInterceptWriteTable[idx+1]);    /* Call 2nd handler */
	if (pInterceptWriteTable[idx+2] != pInterceptWriteTable[idx+1])
		CALL_VAR(pInterceptWriteTable[idx+2]);    /* Call 3rd handler */
	if (pInterceptWriteTable[idx+3] != pInterceptWriteTable[idx+2])
		CALL_VAR(pInterceptWriteTable[idx+3]);    /* Call 4th handler */

	/* Check if we wrote to a bus-error region */
	if (nBusErrorAccesses == 4)
	{
		M68000_BusError(addr, 0);
	}
}


/*-------------------------------------------------------------------------*/
/*
  This handler will be called if a ST program tries to read from an address
  that causes a bus error on a real ST. However, we can't call M68000_BusError()
  directly: For example, a "move.b $ff8204,d0" triggers a bus error on a real ST,
  while a "move.w $ff8204,d0" works! So we have to count the accesses to bus error
  addresses and we only trigger a bus error later if the count matches the complete
  access size (e.g. nBusErrorAccesses==4 for a long word access).
*/
void IoMem_BusErrorEvenReadAccess(void)
{
	nBusErrorAccesses += 1;
}

/*
  We need two handler so that the IoMem_*get functions can distinguish
  consecutive addresses.
*/
void IoMem_BusErrorOddReadAccess(void)
{
	nBusErrorAccesses += 1;
}

/*-------------------------------------------------------------------------*/
/*
  Same as IoMem_BusErrorReadAccess() but for write access this time.
*/
void IoMem_BusErrorEvenWriteAccess(void)
{
	nBusErrorAccesses += 1;
}

/*
  We need two handler so that the IoMem_*put functions can distinguish
  consecutive addresses.
*/
void IoMem_BusErrorOddWriteAccess(void)
{
	nBusErrorAccesses += 1;
}


/*-------------------------------------------------------------------------*/
/*
  A dummy function that does nothing at all - for memory regions that don't
  need a special handler for read access.
*/
void IoMem_ReadWithoutInterception(void)
{
	/* Nothing... */
}

/*-------------------------------------------------------------------------*/
/*
  A dummy function that does nothing at all - for memory regions that don't
  need a special handler for write access.
*/
void IoMem_WriteWithoutInterception(void)
{
	/* Nothing... */
}
