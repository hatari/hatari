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
  even long word - which may span over two hardware registers! Rather than check
  for any and all combinations we use a tables for byte/word/long and for
  read/write. These are lists of functions which access the IO memory area any
  bytes which maybe affected by the operation. Eg, a long write to a PSG
  register (which access two registers) will write the long into IO memory space
  and then call the two handlers which read off the bytes for each register.
  This means that any access to any hardware register in such a way will work
  correctly - it certainly fixes a lot of bugs and means writing just one
  routine for each hardware register we mean to intercept! Phew!
  Note the 'mirror'(or shadow) registers of the PSG - this is used by most games.
*/
char IoMem_rcsid[] = "Hatari $Id: ioMem.c,v 1.1 2005-01-18 23:33:20 thothy Exp $";

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


/*#define CHECK_FOR_NO_MANS_LAND*/            /* Check for read/write from unknown hardware addresses */


#define INTERCEPT_WORKSPACE_SIZE  (10*1024)  /* 10k, size of intercept lists */

/* Hardware address details */
typedef struct
{
	unsigned int Address;        /* ST hardware address */
	int SpanInBytes;             /* SIZE_BYTE, SIZE_WORD or SIZE_LONG */
	void *ReadFunc;              /* Read function */
	void *WriteFunc;             /* Write function */
} INTERCEPT_ACCESS_FUNC;

/* List of hardware address which are not documented, ie STe, TT, Falcon locations - should be unconnected on STfm */
typedef struct
{
	unsigned int Start_Address;
	unsigned int End_Address;
} INTERCEPT_ADDRESSRANGE;



/* A dummy function that does nothing at all... */
static void IoMem_WriteWithoutInterception(void)
{
	/* Nothing... */
}

/* A dummy function that does nothing at all... */
static void IoMem_ReadWithoutInterception(void)
{
	/* Nothing... */
}


/*-----------------------------------------------------------------------*/
/* List of functions to handle read/write hardware intercepts. */
INTERCEPT_ACCESS_FUNC InterceptAccessFuncs[] =
{
 	{ 0x0,SIZE_BYTE,NULL,NULL },
 	{ 0xff8205, SIZE_BYTE, Video_ScreenCounterHigh_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8207, SIZE_BYTE, Video_ScreenCounterMed_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8209, SIZE_BYTE, Video_ScreenCounterLow_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff820a, SIZE_BYTE, Video_Sync_ReadByte, Video_Sync_WriteByte },
	{ 0xff820d, SIZE_BYTE, Video_BaseLow_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff820f, SIZE_BYTE, Video_LineWidth_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8240, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color0_WriteWord },        /* COLOR 0 */
	{ 0xff8242, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color1_WriteWord },        /* COLOR 1 */
	{ 0xff8244, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color2_WriteWord },        /* COLOR 2 */
	{ 0xff8246, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color3_WriteWord },        /* COLOR 3 */
	{ 0xff8248, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color4_WriteWord },        /* COLOR 4 */
	{ 0xff824a, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color5_WriteWord },        /* COLOR 5 */
	{ 0xff824c, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color6_WriteWord },        /* COLOR 6 */
	{ 0xff824e, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color7_WriteWord },        /* COLOR 7 */
	{ 0xff8250, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color8_WriteWord },        /* COLOR 8 */
	{ 0xff8252, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color9_WriteWord },        /* COLOR 9 */
	{ 0xff8254, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color10_WriteWord },       /* COLOR 10 */
	{ 0xff8256, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color11_WriteWord },       /* COLOR 11 */
	{ 0xff8258, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color12_WriteWord },       /* COLOR 12 */
	{ 0xff825a, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color13_WriteWord },       /* COLOR 13 */
	{ 0xff825c, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color14_WriteWord },       /* COLOR 14 */
	{ 0xff825e, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color15_WriteWord },       /* COLOR 15 */
 	{ 0xff8260, SIZE_BYTE, Video_ShifterMode_ReadByte, Video_ShifterMode_WriteByte },

	{ 0xff8604, SIZE_WORD, FDC_DiscControllerStatus_ReadWord, FDC_DiscController_WriteWord },
	{ 0xff8606, SIZE_WORD, FDC_DmaStatus_ReadWord, FDC_DmaModeControl_WriteWord },

	{ 0xff8800, SIZE_BYTE, PSG_SelectRegister_ReadByte, PSG_SelectRegister_WriteByte },
	{ 0xff8802, SIZE_BYTE, PSG_DataRegister_ReadByte, PSG_DataRegister_WriteByte },

	{ 0xff8a28, SIZE_WORD, Blitter_Endmask1_ReadWord, Blitter_Endmask1_WriteWord },
	{ 0xff8a2a, SIZE_WORD, Blitter_Endmask2_ReadWord, Blitter_Endmask2_WriteWord },
	{ 0xff8a2c, SIZE_WORD, Blitter_Endmask3_ReadWord, Blitter_Endmask3_WriteWord },
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
};


unsigned long *pInterceptWorkspace;           /* Memory used to store all read/write NULL terminated function call tables */
unsigned long *pCurrentInterceptWorkspace;    /* Index into above */
unsigned long *pInterceptReadByteTable[0x8000],*pInterceptReadWordTable[0x8000],*pInterceptReadLongTable[0x8000];
unsigned long *pInterceptWriteByteTable[0x8000],*pInterceptWriteWordTable[0x8000],*pInterceptWriteLongTable[0x8000];
BOOL bEnableBlitter = FALSE;                  /* TRUE if blitter is enabled */

#ifdef CHECK_FOR_NO_MANS_LAND
/* We use a well-known address for the no-mans-land workspace so we can test for it in Intercept_CreateTable() */
unsigned long noMansLandWorkspace[2] = { (unsigned long)Intercept_NoMansLand_ReadWrite, 0L };
#else
unsigned long noMansLandWorkspace[1] = { 0L };
#endif



/*-----------------------------------------------------------------------*/
/*
  Set Intercept hardware address table index's

  Each 'intercept table' is a list of 0x8000 pointers to a list of functions to call when that
  location in the ST's memory is accessed. Each entry is terminated by a NULL
  Eg, if we write a long word to address '0xff8800', we
  need to call the functions 'InterceptPSGRegister_WriteByte' and then 'InterceptPSGData_WriteByte'.
*/

static void IoMem_CreateTable(unsigned long *pInterceptTable[], int Span, int ReadWrite)
{
  unsigned int Address, LowAddress, HiAddress, i;

  /* Scan each hardware address */
  for(Address=0xff8000; Address<=0xffffff; Address++)
  {
    /* Does this hardware location/span appear in our list of possible intercepted functions? */
    for (i=0; i<(sizeof(InterceptAccessFuncs)/sizeof(INTERCEPT_ACCESS_FUNC)); i++)
    {
      LowAddress = InterceptAccessFuncs[i].Address;
      HiAddress = InterceptAccessFuncs[i].Address+InterceptAccessFuncs[i].SpanInBytes;

      if ( (Address+Span) <= LowAddress )
        continue;
      if ( Address >= HiAddress )
        continue;

      /* This location needs to be intercepted, so add entry to list */
      if(pInterceptTable[Address-0xff8000] == NULL
         || pInterceptTable[Address-0xff8000] == noMansLandWorkspace)
      {
        pInterceptTable[Address-0xff8000] = pCurrentInterceptWorkspace;
      }

      if(ReadWrite==0)
        *pCurrentInterceptWorkspace++ = (unsigned long)InterceptAccessFuncs[i].ReadFunc;
      else
        *pCurrentInterceptWorkspace++ = (unsigned long)InterceptAccessFuncs[i].WriteFunc;
    }

    /* Terminate table? */
    if (pInterceptTable[Address-0xff8000] && pInterceptTable[Address-0xff8000] != noMansLandWorkspace)
      *pCurrentInterceptWorkspace++ = 0L;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Create 'intercept' tables for hardware address access
*/
void IoMem_Init(void)
{
	/* Allocate memory for intercept tables */
	pCurrentInterceptWorkspace = pInterceptWorkspace = (unsigned long *)malloc(INTERCEPT_WORKSPACE_SIZE);
	if (!pInterceptWorkspace)
	{
		perror("malloc failed in IoMem_Init");
		exit(-1);
	}

	/* Clear intercept tables (NULL signifies no entries for that location) */
	memset(pInterceptReadByteTable, 0, sizeof(pInterceptReadByteTable));
	memset(pInterceptReadWordTable, 0, sizeof(pInterceptReadWordTable));
	memset(pInterceptReadLongTable, 0, sizeof(pInterceptReadLongTable));
	memset(pInterceptWriteByteTable, 0, sizeof(pInterceptWriteByteTable));
	memset(pInterceptWriteWordTable, 0, sizeof(pInterceptWriteWordTable));
	memset(pInterceptWriteLongTable, 0, sizeof(pInterceptWriteLongTable));

#ifdef CHECK_FOR_NO_MANS_LAND
	/* This causes a error when an application tries to access illegal hardware registers(maybe mirror'd) */
	Intercept_ModifyTablesForNoMansLand();
#endif  /*CHECK_FOR_NO_MANS_LAND*/

	/* Create 'read' tables */
	IoMem_CreateTable(pInterceptReadByteTable, SIZE_BYTE, 0);
	IoMem_CreateTable(pInterceptReadWordTable, SIZE_WORD, 0);
	IoMem_CreateTable(pInterceptReadLongTable, SIZE_LONG, 0);
	/* And 'write' tables */
	IoMem_CreateTable(pInterceptWriteByteTable, SIZE_BYTE, 1);
	IoMem_CreateTable(pInterceptWriteWordTable, SIZE_WORD, 1);
	IoMem_CreateTable(pInterceptWriteLongTable, SIZE_LONG, 1);

	/* And modify for bus-error in hardware space */
	Intercept_ModifyTablesForBusErrors();
}


/*-----------------------------------------------------------------------*/
/*
  Free 'intercept' hardware lists
*/
void IoMem_UnInit(void)
{
	free(pInterceptWorkspace);
	pInterceptWorkspace = NULL;
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
  Check list of handlers to see if address needs to be intercepted and call
   routines.
*/
static void IoMem_ScanHandlers(unsigned long *the_func)
{
	if (the_func)
	{
		while (*the_func)           /* Do we have any routines to run for this address? */
		{
			CALL_VAR(*the_func);    /* Call routine */
			the_func+=1;
		}
	}
}


/*-----------------------------------------------------------------------*/
/*
  Check if need to change our address as maybe a mirror register.
  Currently we only have a PSG mirror area.
*/
static unsigned long IoMem_CheckMirrorAddresses(unsigned long addr)
{
	if (addr>=0xff8800 && addr<0xff8900)    /* Is a PSG mirror registers? */
		addr = ( addr & 3) + 0xff8800;      /* Bring into 0xff8800-0xff8804 range */

	return addr;
}



/*-----------------------------------------------------------------------*/
/*
  Handle byte read access from IO memory.
*/
uae_u32 IoMem_bget(uaecptr addr)
{
	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		return 0;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	addr = IoMem_CheckMirrorAddresses(addr);
	IoMem_ScanHandlers(pInterceptReadByteTable[addr - 0x00ff8000]);

	return IoMem[addr];
}


/*-----------------------------------------------------------------------*/
/*
  Handle word read access from IO memory.
*/
uae_u32 IoMem_wget(uaecptr addr)
{
	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		return 0;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	addr = IoMem_CheckMirrorAddresses(addr);
	IoMem_ScanHandlers(pInterceptReadWordTable[addr - 0x00ff8000]);

	return IoMem_ReadWord(addr);
}


/*-----------------------------------------------------------------------*/
/*
  Handle long-word read access from IO memory.
*/
uae_u32 IoMem_lget(uaecptr addr)
{
	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 1);
		return 0;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	addr = IoMem_CheckMirrorAddresses(addr);
	IoMem_ScanHandlers(pInterceptReadLongTable[addr - 0x00ff8000]);

	return IoMem_ReadLong(addr);
}


/*-----------------------------------------------------------------------*/
/*
  Handle byte write access to IO memory.
*/
void IoMem_bput(uaecptr addr, uae_u32 val)
{
	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
	return;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	addr = IoMem_CheckMirrorAddresses(addr);
	IoMem[addr] = val;
	IoMem_ScanHandlers(pInterceptWriteByteTable[addr - 0x00ff8000]);
}


/*-----------------------------------------------------------------------*/
/*
  Handle word write access to IO memory.
*/
void IoMem_wput(uaecptr addr, uae_u32 val)
{
	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
		return;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	addr = IoMem_CheckMirrorAddresses(addr);
	IoMem_WriteWord(addr, val);
	IoMem_ScanHandlers(pInterceptWriteWordTable[addr - 0x00ff8000]);
}


/*-----------------------------------------------------------------------*/
/*
  Handle long-word write access to IO memory.
*/
void IoMem_lput(uaecptr addr, uae_u32 val)
{
	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr < 0x00ff8000)
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr, 0);
		return;
	}

	BusAddressLocation = addr;                    /* Store for exception frame, just in case */
	addr = IoMem_CheckMirrorAddresses(addr);
	IoMem_WriteLong(addr, val);
	IoMem_ScanHandlers(pInterceptWriteLongTable[addr - 0x00ff8000]);
}





/* Address space for Bus Error in hardware mapping */
INTERCEPT_ADDRESSRANGE InterceptBusErrors[] =
{
  { 0xff8002,0xff8200 },
  { 0xff8210,0xff823e },
  { 0xff8280,0xff8600 },        /* Falcon VIDEL, TT Palette */
  { 0xff8900,0xff89fe },        /* DMA Sound/MicroWire */
  { 0xff8a00,0xff8a3e },        /* Blitter (now supported, but disabled by default) */
  { 0xff8a40,0xff8e00 },
  { 0xff8e10,0xfff9fe },
  { 0xfffa40,0xfffbfe },        /* Mega-STE FPU and 2nd (TT) MFP */
  { 0xfffe00,0xffffff },

  { 0,0 }  /* term */
};


/*-------------------------------------------------------------------------*/
/*
  Jump to the BusError handler with the correct bus address
*/
static void Intercept_BusErrorReadAccess(void)
{
  M68000_BusError(BusAddressLocation, 1);
}

static void Intercept_BusErrorWriteAccess(void)
{
  M68000_BusError(BusAddressLocation, 0);
}


/*-------------------------------------------------------------------------*/
/*
  Modify 'intercept' tables to cause Bus Errors on addres to un-mapped
  hardware space (Wing Of Death addresses Blitter space which causes
  BusError on STfm)
*/
void Intercept_ModifyTablesForBusErrors(void)
{
  unsigned long *pInterceptListRead, *pInterceptListWrite;
  unsigned int Address;
  int i;

  /* Set routine list */
  pInterceptListRead = pCurrentInterceptWorkspace;
  *pCurrentInterceptWorkspace++ = (unsigned long)Intercept_BusErrorReadAccess;
  *pCurrentInterceptWorkspace++ = 0L;

  pInterceptListWrite = pCurrentInterceptWorkspace;
  *pCurrentInterceptWorkspace++ = (unsigned long)Intercept_BusErrorWriteAccess;
  *pCurrentInterceptWorkspace++ = 0L;

  /* Set all bus-error entries */
  for(i=0; InterceptBusErrors[i].Start_Address!=0; i++)
  {
    if(bEnableBlitter && InterceptBusErrors[i].Start_Address==0xff8a00)
      continue;    /* Ignore blitter area if blitter is enabled */
    /* Set bus-error table */
    for(Address=InterceptBusErrors[i].Start_Address; Address<InterceptBusErrors[i].End_Address; Address++)
    {
      /* For 'read' */
      pInterceptReadByteTable[Address-0xff8000] = pInterceptListRead;
      pInterceptReadWordTable[Address-0xff8000] = pInterceptListRead;
      pInterceptReadLongTable[Address-0xff8000] = pInterceptListRead;
      /* and 'write' */
      pInterceptWriteByteTable[Address-0xff8000] = pInterceptListWrite;
      pInterceptWriteWordTable[Address-0xff8000] = pInterceptListWrite;
      pInterceptWriteLongTable[Address-0xff8000] = pInterceptListWrite;
    }

  }
}



#ifdef CHECK_FOR_NO_MANS_LAND

/*-----------------------------------------------------------------------*/
/*
  Intercept function used on all non-documented hardware registers.
  Used to help debugging
*/
void Intercept_NoMansLand_ReadWrite(void)
{
  fprintf(stderr,"NoMansLand_ReadWrite at address $%lx , PC=$%lx\n",
          (long)BusAddressLocation, (long)m68k_getpc());
}

/*-----------------------------------------------------------------------*/
/*
  Modify 'intercept' tables to check for access into 'no-mans-land',
  i.e. unknown hardware locations.
  We fill the whole IO memory address space first with the no-mans-land handler
  and overwrite it later in Intercept_Init with the real handlers.
*/
void Intercept_ModifyTablesForNoMansLand(void)
{
  unsigned int Address;

  /* Set all 'no-mans-land' entries */
  for(Address = 0xff8000; Address < 0xffffff; Address++)
  {
    /* For 'read' */
    pInterceptReadByteTable[Address-0xff8000] = noMansLandWorkspace;
    pInterceptReadWordTable[Address-0xff8000] = noMansLandWorkspace;
    pInterceptReadLongTable[Address-0xff8000] = noMansLandWorkspace;
    /* and 'write' */
    pInterceptWriteByteTable[Address-0xff8000] = noMansLandWorkspace;
    pInterceptWriteWordTable[Address-0xff8000] = noMansLandWorkspace;
    pInterceptWriteLongTable[Address-0xff8000] = noMansLandWorkspace;
  }
}

#endif  /*CHECK_FOR_NO_MANS_LAND*/

