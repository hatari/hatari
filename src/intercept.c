/*
  Hatari - intercept.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This is where we intercept read/writes to/from the hardware. The ST's memory is nicely split into
  four main parts - the bottom area of RAM is for user programs. This is followed by a large area which
  causes a Bus Error. After this is the ROM addresses for TOS and finally an area for hardware mapping.
  To gain speed any address in the user area can simply read/write, but anything above this range needs
  to be checked for validity and sent to the various handlers.
  A big problem for ST emulation is the use of the hardware registers. These often consist of an 'odd' byte
  in memory and is usually addressed as a single byte. A number of applications, however, write to the address
  using a word or even long word - which may span over two hardware registers! Rather than check for any and
  all combinations we use a tables for byte/word/long and for read/write. These are lists of functions which
  access the ST Ram area any bytes which maybe affected by the operation. Eg, a long write to a PSG register
  (which access two registers) will write the long into ST Ram and then call the two handlers which read off
  the bytes for each register. This means that any access to any hardware register in such a way will work
  correctly - it certainly fixes a lot of bugs and means writing just one routine for each hardware register
  we mean to intercept! Phew!
  Note the 'mirror'(or shadow) registers of the PSG - this is used by most games. We also have a means of
  testing for addressing into 'no-mans-land' which are parts of the hardware map which are not valid on a
  standard STfm.
*/
static char rcsid[] = "Hatari $Id: intercept.c,v 1.15 2003-08-15 16:09:49 thothy Exp $";

#include <SDL_types.h>

#include "main.h"
#include "debug.h"
#include "decode.h"
#include "dialog.h"
#include "fdc.h"
#include "int.h"
#include "intercept.h"
#include "ikbd.h"
#include "m68000.h"
#include "memAlloc.h"
#include "mfp.h"
#include "midi.h"
#include "psg.h"
#include "rtc.h"
#include "screen.h"
#include "spec512.h"
#include "stMemory.h"
#include "vdi.h"
#include "video.h"
#include "blitter.h"
#include "uae-cpu/sysdeps.h"
#include "tos.h"

/*#define CHECK_FOR_NO_MANS_LAND*/            /* Check for read/write from unknown hardware addresses */


/* A dummy function that does nothing at all... */
void Intercept_WriteNothing(void)
{
  /* Nothing... */
}


/*-----------------------------------------------------------------------*/
/* List of functions to handle read/write hardware intercepts. */
INTERCEPT_ACCESS_FUNC InterceptAccessFuncs[] =
{
  { 0x0,SIZE_BYTE,NULL,NULL },
  { 0xff8205,SIZE_BYTE,Intercept_VideoHigh_ReadByte,Intercept_VideoHigh_WriteByte },      /* INTERCEPT_VIDEOHIGH */
  { 0xff8207,SIZE_BYTE,Intercept_VideoMed_ReadByte,Intercept_VideoMed_WriteByte },        /* INTERCEPT_VIDEOMED */
  { 0xff8209,SIZE_BYTE,Intercept_VideoLow_ReadByte,Intercept_VideoLow_WriteByte },        /* INTERCEPT_VIDEOLOW */
  { 0xff820a,SIZE_BYTE,Intercept_VideoSync_ReadByte,Intercept_VideoSync_WriteByte },      /* INTERCEPT_VIDEOSYNC */
  { 0xff820d,SIZE_BYTE,Intercept_VideoBaseLow_ReadByte,Intercept_VideoBaseLow_WriteByte },   /* INTERCEPT_VIDEOBASELOW */
  { 0xff820f,SIZE_BYTE,Intercept_LineWidth_ReadByte,Intercept_LineWidth_WriteByte },      /* INTERCEPT_LINEWIDTH */
  { 0xff8240,SIZE_WORD,Intercept_Colour0_ReadWord,Intercept_Colour0_WriteWord },          /* INTERCEPT_COLOUR0 */
  { 0xff8242,SIZE_WORD,Intercept_Colour1_ReadWord,Intercept_Colour1_WriteWord },          /* INTERCEPT_COLOUR1 */
  { 0xff8244,SIZE_WORD,Intercept_Colour2_ReadWord,Intercept_Colour2_WriteWord },          /* INTERCEPT_COLOUR2 */
  { 0xff8246,SIZE_WORD,Intercept_Colour3_ReadWord,Intercept_Colour3_WriteWord },          /* INTERCEPT_COLOUR3 */
  { 0xff8248,SIZE_WORD,Intercept_Colour4_ReadWord,Intercept_Colour4_WriteWord },          /* INTERCEPT_COLOUR4 */
  { 0xff824a,SIZE_WORD,Intercept_Colour5_ReadWord,Intercept_Colour5_WriteWord },          /* INTERCEPT_COLOUR5 */
  { 0xff824c,SIZE_WORD,Intercept_Colour6_ReadWord,Intercept_Colour6_WriteWord },          /* INTERCEPT_COLOUR6 */
  { 0xff824e,SIZE_WORD,Intercept_Colour7_ReadWord,Intercept_Colour7_WriteWord },          /* INTERCEPT_COLOUR7 */
  { 0xff8250,SIZE_WORD,Intercept_Colour8_ReadWord,Intercept_Colour8_WriteWord },          /* INTERCEPT_COLOUR8 */
  { 0xff8252,SIZE_WORD,Intercept_Colour9_ReadWord,Intercept_Colour9_WriteWord },          /* INTERCEPT_COLOUR9 */
  { 0xff8254,SIZE_WORD,Intercept_Colour10_ReadWord,Intercept_Colour10_WriteWord },        /* INTERCEPT_COLOUR10 */
  { 0xff8256,SIZE_WORD,Intercept_Colour11_ReadWord,Intercept_Colour11_WriteWord },        /* INTERCEPT_COLOUR11 */
  { 0xff8258,SIZE_WORD,Intercept_Colour12_ReadWord,Intercept_Colour12_WriteWord },        /* INTERCEPT_COLOUR12 */
  { 0xff825a,SIZE_WORD,Intercept_Colour13_ReadWord,Intercept_Colour13_WriteWord },        /* INTERCEPT_COLOUR13 */
  { 0xff825c,SIZE_WORD,Intercept_Colour14_ReadWord,Intercept_Colour14_WriteWord },        /* INTERCEPT_COLOUR14 */
  { 0xff825e,SIZE_WORD,Intercept_Colour15_ReadWord,Intercept_Colour15_WriteWord },        /* INTERCEPT_COLOUR15 */
  { 0xff8260,SIZE_BYTE,Intercept_ShifterMode_ReadByte,Intercept_ShifterMode_WriteByte },  /* INTERCEPT_SHIFTERMODE */

  { 0xff8604,SIZE_WORD,Intercept_DiskControl_ReadWord,Intercept_DiskControl_WriteWord },  /* INTERCEPT_DISKCONTROL */
  { 0xff8606,SIZE_WORD,Intercept_DmaStatus_ReadWord,Intercept_DmaStatus_WriteWord },      /* INTERCEPT_DMASTATUS */
  { 0xff8800,SIZE_BYTE,Intercept_PSGRegister_ReadByte,Intercept_PSGRegister_WriteByte },  /* INTERCEPT_PSG_REGISTER */
  { 0xff8802,SIZE_BYTE,Intercept_PSGData_ReadByte,Intercept_PSGData_WriteByte },          /* INTERCEPT_PSG_DATA */
  { 0xff8922,SIZE_WORD,Intercept_MicrowireData_ReadWord,Intercept_MicrowireData_WriteWord }, /* INTERCEPT_MICROWIREDATA */

  { 0xff8a28,SIZE_WORD,Intercept_BlitterEndmask1_ReadWord,Intercept_BlitterEndmask1_WriteWord },
  { 0xff8a2a,SIZE_WORD,Intercept_BlitterEndmask2_ReadWord,Intercept_BlitterEndmask2_WriteWord },
  { 0xff8a2c,SIZE_WORD,Intercept_BlitterEndmask3_ReadWord,Intercept_BlitterEndmask3_WriteWord },
  { 0xff8a32,SIZE_LONG,Intercept_BlitterDst_ReadLong,Intercept_BlitterDst_WriteLong },
  { 0xff8a36,SIZE_WORD,Intercept_BlitterWPL_ReadWord,Intercept_BlitterWPL_WriteWord },
  { 0xff8a38,SIZE_WORD,Intercept_BlitterLPB_ReadWord,Intercept_BlitterLPB_WriteWord },
  { 0xff8a3a,SIZE_BYTE,Intercept_BlitterHalftoneOp_ReadByte,Intercept_BlitterHalftoneOp_WriteByte },
  { 0xff8a3b,SIZE_BYTE,Intercept_BlitterLogOp_ReadByte,Intercept_BlitterLogOp_WriteByte },
  { 0xff8a3c,SIZE_BYTE,Intercept_BlitterLineNum_ReadByte,Intercept_BlitterLineNum_WriteByte },
  { 0xff8a3d,SIZE_BYTE,Intercept_BlitterSkew_ReadByte,Intercept_BlitterSkew_WriteByte },

  { 0xfffa01,SIZE_BYTE,Intercept_Monitor_ReadByte,Intercept_Monitor_WriteByte },          /* INTERCEPT_MONITOR */
  { 0xfffa03,SIZE_BYTE,Intercept_ActiveEdge_ReadByte,Intercept_ActiveEdge_WriteByte },    /* INTERCEPT_ACTIVE_EDGE */
  { 0xfffa05,SIZE_BYTE,Intercept_DataDirection_ReadByte,Intercept_DataDirection_WriteByte }, /* INTERCEPT_DATA_DIRECTION */
  { 0xfffa07,SIZE_BYTE,Intercept_EnableA_ReadByte,Intercept_EnableA_WriteByte },          /* INTERCEPT_ENABLE_A */
  { 0xfffa09,SIZE_BYTE,Intercept_EnableB_ReadByte,Intercept_EnableB_WriteByte },          /* INTERCEPT_ENABLE_B */
  { 0xfffa0b,SIZE_BYTE,Intercept_PendingA_ReadByte,Intercept_PendingA_WriteByte },        /* INTERCEPT_PENDING_A */
  { 0xfffa0d,SIZE_BYTE,Intercept_PendingB_ReadByte,Intercept_PendingB_WriteByte },        /* INTERCEPT_PENDING_B */
  { 0xfffa0f,SIZE_BYTE,Intercept_InServiceA_ReadByte,Intercept_InServiceA_WriteByte },    /* INTERCEPT_INSERVICE_A */
  { 0xfffa11,SIZE_BYTE,Intercept_InServiceB_ReadByte,Intercept_InServiceB_WriteByte },    /* INTERCEPT_INSERVICE_B */
  { 0xfffa13,SIZE_BYTE,Intercept_MaskA_ReadByte,Intercept_MaskA_WriteByte },              /* INTERCEPT_MASK_A */
  { 0xfffa15,SIZE_BYTE,Intercept_MaskB_ReadByte,Intercept_MaskB_WriteByte },              /* INTERCEPT_MASK_B */
  { 0xfffa17,SIZE_BYTE,Intercept_VectorReg_ReadByte,Intercept_VectorReg_WriteByte },      /* INTERCEPT_VECTOR_REG */
  { 0xfffa19,SIZE_BYTE,Intercept_TimerACtrl_ReadByte,Intercept_TimerACtrl_WriteByte },    /* INTERCEPT_TIMERA_CTRL */
  { 0xfffa1b,SIZE_BYTE,Intercept_TimerBCtrl_ReadByte,Intercept_TimerBCtrl_WriteByte },    /* INTERCEPT_TIMERB_CTRL */
  { 0xfffa1d,SIZE_BYTE,Intercept_TimerCDCtrl_ReadByte,Intercept_TimerCDCtrl_WriteByte },  /* INTERCEPT_TIMERCD_CTRL */
  { 0xfffa1f,SIZE_BYTE,Intercept_TimerAData_ReadByte,Intercept_TimerAData_WriteByte },    /* INTERCEPT_TIMERA_DATA */
  { 0xfffa21,SIZE_BYTE,Intercept_TimerBData_ReadByte,Intercept_TimerBData_WriteByte },    /* INTERCEPT_TIMERB_DATA */
  { 0xfffa23,SIZE_BYTE,Intercept_TimerCData_ReadByte,Intercept_TimerCData_WriteByte },    /* INTERCEPT_TIMERC_DATA */
  { 0xfffa25,SIZE_BYTE,Intercept_TimerDData_ReadByte,Intercept_TimerDData_WriteByte },    /* INTERCEPT_TIMERD_DATA */

  { 0xfffc00,SIZE_BYTE,Intercept_KeyboardControl_ReadByte,Intercept_KeyboardControl_WriteByte }, /* INTERCEPT_KEYBOARDCONTROL */
  { 0xfffc02,SIZE_BYTE,Intercept_KeyboardData_ReadByte,Intercept_KeyboardData_WriteByte },   /* INTERCEPT_KEYBOARDDATA */
  { 0xfffc04,SIZE_BYTE,Intercept_MidiControl_ReadByte,Intercept_MidiControl_WriteByte },  /* INTERCEPT_MIDICONTROL */
  { 0xfffc06,SIZE_BYTE,Intercept_MidiData_ReadByte,Intercept_MidiData_WriteByte },        /* INTERCEPT_MIDIDATA */

  { 0xfffc21,SIZE_BYTE,Rtc_SecondsUnits_ReadByte,Intercept_WriteNothing },
  { 0xfffc23,SIZE_BYTE,Rtc_SecondsTens_ReadByte,Intercept_WriteNothing },
  { 0xfffc25,SIZE_BYTE,Rtc_MinutesUnits_ReadByte,Rtc_MinutesUnits_WriteByte },
  { 0xfffc27,SIZE_BYTE,Rtc_MinutesTens_ReadByte,Rtc_MinutesTens_WriteByte },
  { 0xfffc29,SIZE_BYTE,Rtc_HoursUnits_ReadByte,Intercept_WriteNothing },
  { 0xfffc2b,SIZE_BYTE,Rtc_HoursTens_ReadByte,Intercept_WriteNothing },
  { 0xfffc2d,SIZE_BYTE,Rtc_Weekday_ReadByte,Intercept_WriteNothing },
  { 0xfffc2f,SIZE_BYTE,Rtc_DayUnits_ReadByte,Intercept_WriteNothing },
  { 0xfffc31,SIZE_BYTE,Rtc_DayTens_ReadByte,Intercept_WriteNothing },
  { 0xfffc33,SIZE_BYTE,Rtc_MonthUnits_ReadByte,Intercept_WriteNothing },
  { 0xfffc35,SIZE_BYTE,Rtc_MonthTens_ReadByte,Intercept_WriteNothing },
  { 0xfffc37,SIZE_BYTE,Rtc_YearUnits_ReadByte,Intercept_WriteNothing },
  { 0xfffc39,SIZE_BYTE,Rtc_YearTens_ReadByte,Intercept_WriteNothing },
  { 0xfffc3b,SIZE_BYTE,Rtc_ClockMod_ReadByte,Rtc_ClockMod_WriteByte },
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
  Create 'intercept' tables for hardware address access
*/
void Intercept_Init(void)
{
  /* Allocate memory for intercept tables */
  pCurrentInterceptWorkspace = pInterceptWorkspace = (unsigned long *)Memory_Alloc(INTERCEPT_WORKSPACE_SIZE);

  /* Clear intercept tables(NULL signifies no entries for that location) */
  Memory_Clear(pInterceptReadByteTable,sizeof(unsigned long *)*0x8000);
  Memory_Clear(pInterceptReadWordTable,sizeof(unsigned long *)*0x8000);
  Memory_Clear(pInterceptReadLongTable,sizeof(unsigned long *)*0x8000);
  Memory_Clear(pInterceptWriteByteTable,sizeof(unsigned long *)*0x8000);
  Memory_Clear(pInterceptWriteWordTable,sizeof(unsigned long *)*0x8000);
  Memory_Clear(pInterceptWriteLongTable,sizeof(unsigned long *)*0x8000);

#ifdef CHECK_FOR_NO_MANS_LAND
  /* This causes a error when an application tries to access illegal hardware registers(maybe mirror'd) */
  Intercept_ModifyTablesForNoMansLand();
#endif  /*CHECK_FOR_NO_MANS_LAND*/

  /* Create 'read' tables */
  Intercept_CreateTable(pInterceptReadByteTable,SIZE_BYTE,0);
  Intercept_CreateTable(pInterceptReadWordTable,SIZE_WORD,0);
  Intercept_CreateTable(pInterceptReadLongTable,SIZE_LONG,0);
  /* And 'write' tables */
  Intercept_CreateTable(pInterceptWriteByteTable,SIZE_BYTE,1);
  Intercept_CreateTable(pInterceptWriteWordTable,SIZE_WORD,1);
  Intercept_CreateTable(pInterceptWriteLongTable,SIZE_LONG,1);

  /* And modify for bus-error in hardware space */
  Intercept_ModifyTablesForBusErrors();
}


/*-----------------------------------------------------------------------*/
/*
  Free 'intercept' hardware lists
*/
void Intercept_UnInit(void)
{
  Memory_Free(pInterceptWorkspace);
}


/*-----------------------------------------------------------------------*/
/*
  Set Intercept hardware address table index's

  Each 'intercept table' is a list of 0x8000 pointers to a list of functions to call when that
  location in the ST's memory is accessed. Each entry is terminated by a NULL
  Eg, if we write a long word to address '0xff8800', we
  need to call the functions 'InterceptPSGRegister_WriteByte' and then 'InterceptPSGData_WriteByte'.
*/

void Intercept_CreateTable(unsigned long *pInterceptTable[],int Span,int ReadWrite)
{
  unsigned int Address, LowAddress, HiAddress;
  int i;

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
  Enable/disable blitter emulation
*/
void Intercept_EnableBlitter(BOOL enableFlag)
{
  if(bEnableBlitter!=enableFlag)
  {
    bEnableBlitter = enableFlag;
    /* Ugly hack: Enable/disable the blitter emulation by
       re-init the interception tables... */
    Intercept_UnInit();
    Intercept_Init();
  }
}


/*-----------------------------------------------------------------------*/
/*
  Check list of handlers to see if address needs to be intercepted and call
   routines.
*/
void Intercept_ScanHandlers(unsigned long *the_func)
{
 if( the_func )
  while( *the_func )      /* Do we have any routines to run for this address? */
   {
    CALL_VAR(*the_func);    /* Call routine */
    the_func+=1;
   }
}


/*-----------------------------------------------------------------------*/
/*
  Check if need to change our address as maybe a mirror register.
  Currently we only have a PSG mirror area.
*/
static unsigned long Intercept_CheckMirrorAddresses(unsigned long addr)
{
  if( addr>=0xff8800 && addr<0xff8900 )   /* Is a PSG mirror registers? */
    addr = ( addr & 3) + 0xff8800;        /* Bring into 0xff8800-0xff8804 range */

  return addr;
}


/*-----------------------------------------------------------------------*/


uae_u32 Intercept_ReadByte(uaecptr addr)
{
  addr &= 0x00ffffff;                           /* Use a 24 bit address */

  if(addr < 0x00ff8000)
  {
    /* invalid memory addressing --> bus error */
    M68000_BusError(addr);
    return 0;
  }

  BusAddressLocation = addr;                    /* Store for exception frame, just in case */
  addr = Intercept_CheckMirrorAddresses(addr);
  Intercept_ScanHandlers(pInterceptReadByteTable[addr - 0x00ff8000]);

  return( STRam[addr] );
}


uae_u32 Intercept_ReadWord(uaecptr addr)
{
  addr &= 0x00ffffff;                           /* Use a 24 bit address */

  if( addr&1 )
  {
    M68000_AddressError(addr);                  /* Is address error? (not correct alignment) */
    return 0;
  }

  if(addr < 0x00ff8000)
  {
    /* invalid memory addressing --> bus error */
    M68000_BusError(addr);
    return 0;
  }

  BusAddressLocation = addr;                    /* Store for exception frame, just in case */
  addr = Intercept_CheckMirrorAddresses(addr);
  Intercept_ScanHandlers(pInterceptReadWordTable[addr - 0x00ff8000]);

  return STMemory_ReadWord(addr);
}


uae_u32 Intercept_ReadLong(uaecptr addr)
{
  addr &= 0x00ffffff;                           /* Use a 24 bit address */

  if( addr&1 )
  {
    M68000_AddressError(addr);                  /* Is address error? (not correct alignment) */
    return 0;
  }

  if(addr < 0x00ff8000)
  {
    /* invalid memory addressing --> bus error */
    M68000_BusError(addr);
    return 0;
  }

  BusAddressLocation = addr;                    /* Store for exception frame, just in case */
  addr = Intercept_CheckMirrorAddresses(addr);
  Intercept_ScanHandlers(pInterceptReadLongTable[addr - 0x00ff8000]);

  return STMemory_ReadLong(addr);
}


/*-----------------------------------------------------------------------*/


void Intercept_WriteByte(uaecptr addr, uae_u32 val)
{
  addr &= 0x00ffffff;                           /* Use a 24 bit address */

  if(addr < 0x00ff8000)
  {
    /* invalid memory addressing --> bus error */
    M68000_BusError(addr);
    return;
  }

  BusAddressLocation = addr;                    /* Store for exception frame, just in case */
  addr = Intercept_CheckMirrorAddresses(addr);
  STRam[addr] = val;
  Intercept_ScanHandlers(pInterceptWriteByteTable[addr - 0x00ff8000]);
}


void Intercept_WriteWord(uaecptr addr, uae_u32 val)
{
  addr &= 0x00ffffff;                           /* Use a 24 bit address */

  if( addr&1 )
  {
    M68000_AddressError(addr);                  /* Is address error? (not correct alignment) */
    return;
  }

  if(addr < 0x00ff8000)
  {
    /* invalid memory addressing --> bus error */
    M68000_BusError(addr);
    return;
  }

  BusAddressLocation = addr;                    /* Store for exception frame, just in case */
  addr = Intercept_CheckMirrorAddresses(addr);
  STMemory_WriteWord(addr, val);
  Intercept_ScanHandlers(pInterceptWriteWordTable[addr - 0x00ff8000]);
}


void Intercept_WriteLong(uaecptr addr, uae_u32 val)
{
  addr &= 0x00ffffff;                           /* Use a 24 bit address */

  if( addr&1 )
  {
    M68000_AddressError(addr);                  /* Is address error? (not correct alignment) */
    return;
  }

  if(addr < 0x00ff8000)
  {
    /* invalid memory addressing --> bus error */
    M68000_BusError(addr);
    return;
  }

  BusAddressLocation = addr;                    /* Store for exception frame, just in case */
  addr = Intercept_CheckMirrorAddresses(addr);
  STMemory_WriteLong(addr, val);
  Intercept_ScanHandlers(pInterceptWriteLongTable[addr - 0x00ff8000]);
}



/*-----------------------------------------------------------------------*/
/*  Read from Hardware(0x00ff8000 to 0xffffff)  */

/* INTERCEPT_VIDEOHIGH(0xff8205 byte) */
void Intercept_VideoHigh_ReadByte(void)
{
 STRam[0xff8205] = Video_ReadAddress() >> 16;   /* Get video address high byte */
}

/* INTERCEPT_VIDEOMED(0xff8207 byte) */
void Intercept_VideoMed_ReadByte(void)
{
 STRam[0xff8207] = Video_ReadAddress() >> 8;    /* Get video address med byte */
}

/* INTERCEPT_VIDEOLOW(0xff8209 byte) */
void Intercept_VideoLow_ReadByte(void)
{
 STRam[0xff8209] = Video_ReadAddress();         /* Get video address med byte */
}

/* INTERCEPT_VIDEOSYNC(0xff820a byte) */
void Intercept_VideoSync_ReadByte(void)
{
  /* Nothing... */
}

/* INTERCEPT_VIDEOBASELOW(0xff820d byte) */
void Intercept_VideoBaseLow_ReadByte(void)
{
 STRam[0xff820d] = 0;          /* ST can only store screen address to 256 bytes(ie no lower byte) */
}

/* INTERCEPT_LINEWIDTH(0xff820f byte) */
void Intercept_LineWidth_ReadByte(void)
{
 STRam[0xff820f]=0;          /* On ST this is always 0 */
}

/* INTERCEPT_COLOUR0(0xff8240 word) */
void Intercept_Colour0_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR1(0xff8242 word) */
void Intercept_Colour1_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR2(0xff8244 word) */
void Intercept_Colour2_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR3(0xff8246 word) */
void Intercept_Colour3_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR4(0xff8248 word) */
void Intercept_Colour4_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR5(0xff824a word) */
void Intercept_Colour5_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR6(0xff824c word) */
void Intercept_Colour6_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR7(0xff824e word) */
void Intercept_Colour7_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR8(0xff8250 word) */
void Intercept_Colour8_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR9(0xff8252 word) */
void Intercept_Colour9_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR10(0xff8254 word) */
void Intercept_Colour10_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR11(0xff8256 word) */
void Intercept_Colour11_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR12(0xff8258 word) */
void Intercept_Colour12_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR13(0xff825a word) */
void Intercept_Colour13_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR14(0xff825c word) */
void Intercept_Colour14_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_COLOUR15(0xff825e word) */
void Intercept_Colour15_ReadWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_SHIFTERMODE(0xff8260 byte) */
void Intercept_ShifterMode_ReadByte(void)
{
 if(bUseHighRes)
   STRam[0xff8260]=2;                     /* If mono monitor, force to high resolution */
  else
   STRam[0xff8260]=VideoShifterByte;      /* Read shifter register */
}

/* INTERCEPT_DISKCONTROL(0xff8604 word) */
void Intercept_DiskControl_ReadWord(void)
{
 STMemory_WriteWord( 0xff8604, FDC_ReadDiscControllerStatus() );
}

/* INTERCEPT_DMASTATUS(0xff8606 word) */
void Intercept_DmaStatus_ReadWord(void)
{
 STMemory_WriteWord( 0xff8606, FDC_ReadDMAStatus() );
}

/* INTERCEPT_PSG_REGISTER(0xff8800 byte) */
void Intercept_PSGRegister_ReadByte(void)
{
 STRam[0xff8800] = PSG_ReadSelectRegister();
}

/* INTERCEPT_PSG_DATA(0xff8802 byte) */
void Intercept_PSGData_ReadByte(void)
{
 STRam[0xff8802] = PSG_ReadDataRegister();
}

/* INTERCEPT_MICROWIREDATA(0xff8922 word) */
void Intercept_MicrowireData_ReadWord(void)
{
 STMemory_WriteWord( 0xff8922, 0 );
}

/* INTERCEPT_MONITOR(0xfffa01 byte) */
void Intercept_Monitor_ReadByte(void)
{
 unsigned short v;
 v=MFP_GPIP & 0x7f;    /* Lower 7-bits are GPIP(Top bit is monitor type) */
 if( !bUseHighRes )  v|=0x80;  /* Colour monitor */
 STRam[0xfffa01]=v;
}

/* INTERCEPT_ACTIVE_EDGE(0xfffa03 byte) */
void Intercept_ActiveEdge_ReadByte(void)
{
 STRam[0xfffa03] = MFP_AER;
}

/* INTERCEPT_DATA_DIRECTION(0xfffa05 byte) */
void Intercept_DataDirection_ReadByte(void)
{
 STRam[0xfffa05] = MFP_DDR;
}

/* INTERCEPT_ENABLE_A(0xfffa07 byte) */
void Intercept_EnableA_ReadByte(void)
{
 STRam[0xfffa07] = MFP_IERA;
}

/* INTERCEPT_ENABLE_B(0xfffa09 byte) */
void Intercept_EnableB_ReadByte(void)
{
 STRam[0xfffa09] = MFP_IERB;
}

/* INTERCEPT_PENDING_A(0xfffa0b byte) */
void Intercept_PendingA_ReadByte(void)
{
 STRam[0xfffa0b] = MFP_IPRA;
}

/* INTERCEPT_PENDING_B(0xfffa0d byte) */
void Intercept_PendingB_ReadByte(void)
{
 STRam[0xfffa0d] = MFP_IPRB;
}

/* INTERCEPT_INSERVICE_A(0xfffa0f byte) */
void Intercept_InServiceA_ReadByte(void)
{
 STRam[0xfffa0f] = MFP_ISRA;
}

/* INTERCEPT_INSERVICE_B(0xfffa11 byte) */
void Intercept_InServiceB_ReadByte(void)
{
 STRam[0xfffa11] = MFP_ISRB;
}

/* INTERCEPT_MASK_A(0xfffa13 byte) */
void Intercept_MaskA_ReadByte(void)
{
 STRam[0xfffa13] = MFP_IMRA;
}

/* INTERCEPT_MASK_B(0xfffa15 byte) */
void Intercept_MaskB_ReadByte(void)
{
 STRam[0xfffa15] = MFP_IMRB;
}

/* INTERCEPT_VECTOR_REG(0xfffa17 byte) */
void Intercept_VectorReg_ReadByte(void)
{
 STRam[0xfffa17] = MFP_VR;
}

/* INTERCEPT_TIMERA_CTRL(0xfffa19 byte) */
void Intercept_TimerACtrl_ReadByte(void)
{
 STRam[0xfffa19] = MFP_TACR;
}

/* INTERCEPT_TIMERB_CTRL(0xfffa1b byte) */
void Intercept_TimerBCtrl_ReadByte(void)
{
 STRam[0xfffa1b] = MFP_TBCR;
}

/* INTERCEPT_TIMERCD_CTRL(0xfffa1d byte) */
void Intercept_TimerCDCtrl_ReadByte(void)
{
 STRam[0xfffa1d] = MFP_TCDCR;
}

/* INTERCEPT_TIMERA_DATA(0xfffa1f byte) */
void Intercept_TimerAData_ReadByte(void)
{
 if( MFP_TACR != 8 )        /* Is event count? Need to re-calculate counter */
   MFP_ReadTimerA();        /* Stores result in 'MFP_TA_MAINCOUNTER' */
 STRam[0xfffa1f] = MFP_TA_MAINCOUNTER;
}

/* INTERCEPT_TIMERB_DATA(0xfffa21 byte) */
void Intercept_TimerBData_ReadByte(void)
{
 if(MFP_TBCR != 8)        /* Is event count? Need to re-calculate counter */
   MFP_ReadTimerB();      /* Stores result in 'MFP_TB_MAINCOUNTER' */
 STRam[0xfffa21] = MFP_TB_MAINCOUNTER;
}

/* INTERCEPT_TIMERC_DATA(0xfffa23 byte) */
void Intercept_TimerCData_ReadByte(void)
{
 MFP_ReadTimerC();        /* Stores result in 'MFP_TC_MAINCOUNTER' */
 STRam[0xfffa23] = MFP_TC_MAINCOUNTER;
}

static int timerd_tos_value;

/* INTERCEPT_TIMERD_DATA(0xfffa25 byte) */
void Intercept_TimerDData_ReadByte(void)
{
 int pc = m68k_getpc();
 if (pc >= TosAddress && pc <= TosAddress + TosSize) {
   STRam[0xfffa25] = timerd_tos_value; // trick the tos to believe it was changed
 } else {
   MFP_ReadTimerD();        /* Stores result in 'MFP_TD_MAINCOUNTER' */
   STRam[0xfffa25] = MFP_TD_MAINCOUNTER;
 }
}

/* INTERCEPT_KEYBOARDCONTROL(0xfffc00 byte) */
void Intercept_KeyboardControl_ReadByte(void)
{
 /* For our emulation send is immediate so acknowledge buffer is empty */
 STRam[0xfffc00] = ACIAStatusRegister | ACIA_STATUS_REGISTER__TX_BUFFER_EMPTY;
}

/* INTERCEPT_KEYBOARDDATA(0xfffc02 byte) */
void Intercept_KeyboardData_ReadByte(void)
{
 STRam[0xfffc02] = IKBD_GetByteFromACIA();  /* Return our byte from keyboard processor */
}

/* INTERCEPT_MIDICONTROL(0xfffc04 byte) */
void Intercept_MidiControl_ReadByte(void)
{
 STRam[0xfffc04] = Midi_ReadControl();
}

/* INTERCEPT_MIDIDATA(0xfffc06 byte) */
void Intercept_MidiData_ReadByte(void)
{
 STRam[0xfffc06] = Midi_ReadData();
}


void Intercept_BlitterEndmask1_ReadWord(void)
{
  STMemory_WriteWord( 0xff8a28, LOAD_W_ff8a28() );
}

void Intercept_BlitterEndmask2_ReadWord(void)
{
  STMemory_WriteWord( 0xff8a2a, LOAD_W_ff8a2a() );
}

void Intercept_BlitterEndmask3_ReadWord(void)
{
  STMemory_WriteWord( 0xff8a2c, LOAD_W_ff8a2c() );
}

void Intercept_BlitterDst_ReadLong(void)
{
  STMemory_WriteLong( 0xff8a32, LOAD_L_ff8a32() );
}

void Intercept_BlitterWPL_ReadWord(void)
{
  STMemory_WriteWord( 0xff8a36, LOAD_W_ff8a36() );
}

void Intercept_BlitterLPB_ReadWord(void)
{
  STMemory_WriteWord( 0xff8a38, LOAD_W_ff8a38() );
}

void Intercept_BlitterHalftoneOp_ReadByte(void)
{
  STMemory_WriteByte( 0xff8a3a, LOAD_B_ff8a3a() );
}

void Intercept_BlitterLogOp_ReadByte(void)
{
  STMemory_WriteByte( 0xff8a3b, LOAD_B_ff8a3b() );
}

void Intercept_BlitterLineNum_ReadByte(void)
{
  STMemory_WriteByte( 0xff8a3c, LOAD_B_ff8a3c() );
}

void Intercept_BlitterSkew_ReadByte(void)
{
  STMemory_WriteByte( 0xff8a3d, LOAD_B_ff8a3d() );
}



/*-----------------------------------------------------------------------*/
/*  Write to Hardware(0x00ff8000 to 0xffffff)  */

/* INTERCEPT_VIDEOHIGH(0xff8205 byte) */
void Intercept_VideoHigh_WriteByte(void)
{
  /* Nothing... */
}

/* INTERCEPT_VIDEOMED(0xff8207 byte) */
void Intercept_VideoMed_WriteByte(void)
{
  /* Nothing... */
}

/* INTERCEPT_VIDEOLOW(0xff8209 byte) */
void Intercept_VideoLow_WriteByte(void)
{
  /* Nothing... */
}

/* INTERCEPT_VIDEOSYNC(0xff820a byte) */
void Intercept_VideoSync_WriteByte(void)
{
 VideoSyncByte = STRam[0xff820a] & 3;      /* We're only interested in lower 2 bits(50/60Hz) */

 if (nHBL >= OVERSCAN_TOP && nHBL <= 39 && nStartHBL > FIRST_VISIBLE_HBL)
 {
   Video_SyncHandler_SetTopBorder();
   pHBLPaletteMasks -= OVERSCAN_TOP;
   pHBLPalettes -= OVERSCAN_TOP;
 }
 else if (nHBL >= SCREEN_START_HBL+SCREEN_HEIGHT_HBL)
 {
   Video_SyncHandler_SetBottomBorder();
 }
/*
 else if (nStartHBL > FIRST_VISIBLE_HBL)
 {
   fprintf(stderr,"hbl %d (%d - %d)\n",nHBL,OVERSCAN_TOP,37);
 }
*/
 Video_WriteToSync();
}

/* INTERCEPT_VIDEOBASELOW(0xff820d byte) */
void Intercept_VideoBaseLow_WriteByte(void)
{
  /* Nothing... */
}

/* INTERCEPT_LINEWIDTH(0xff820f byte) */
void Intercept_LineWidth_WriteByte(void)
{
  /* Nothing... */
}

void Intercept_Colour_WriteWord(unsigned long addr)
{
 if( !bUseHighRes )                                 /* Don't store if hi-res */
  {
   unsigned short col;
   Video_SetHBLPaletteMaskPointers();               /* Set 'pHBLPalettes' etc.. according cycles into frame */
   col = STMemory_ReadWord( addr );
   col &= 0x777;                                    /* Mask off to 512 palette */
   STMemory_WriteWord(addr, col);                   /* (some games write 0xFFFF and read back to see if STe) */
   Spec512_StoreCyclePalette( col, addr );          /* Store colour into CyclePalettes[] */
   pHBLPalettes[(addr-0xff8240)/2]=col;             /* Set colour x */
   *pHBLPaletteMasks |= 1 << ((addr-0xff8240)/2);   /* And mask */
  }
}

/* INTERCEPT_COLOUR0(0xff8240 word) */
void Intercept_Colour0_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8240 );
}

/* INTERCEPT_COLOUR1(0xff8242 word) */
void Intercept_Colour1_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8242 );
}

/* INTERCEPT_COLOUR2(0xff8244 word) */
void Intercept_Colour2_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8244 );
}

/* INTERCEPT_COLOUR3(0xff8246 word) */
void Intercept_Colour3_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8246 );
}

/* INTERCEPT_COLOUR4(0xff8248 word) */
void Intercept_Colour4_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8248 );
}

/* INTERCEPT_COLOUR5(0xff824a word) */
void Intercept_Colour5_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff824a );
}

/* INTERCEPT_COLOUR6(0xff824c word) */
void Intercept_Colour6_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff824c );
}

/* INTERCEPT_COLOUR7(0xff824e word) */
void Intercept_Colour7_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff824e );
}

/* INTERCEPT_COLOUR8(0xff8250 word) */
void Intercept_Colour8_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8250 );
}

/* INTERCEPT_COLOUR9(0xff8252 word) */
void Intercept_Colour9_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8252 );
}

/* INTERCEPT_COLOUR10(0xff8254 word) */
void Intercept_Colour10_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8254 );
}

/* INTERCEPT_COLOUR11(0xff8256 word) */
void Intercept_Colour11_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8256 );
}

/* INTERCEPT_COLOUR12(0xff8258 word) */
void Intercept_Colour12_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff8258 );
}

/* INTERCEPT_COLOUR13(0xff825a word) */
void Intercept_Colour13_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff825a );
}

/* INTERCEPT_COLOUR14(0xff825c word) */
void Intercept_Colour14_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff825c );
}

/* INTERCEPT_COLOUR15(0xff825e word) */
void Intercept_Colour15_WriteWord(void)
{
 Intercept_Colour_WriteWord( 0xff825e );
}

/* INTERCEPT_SHIFTERMODE(0xff8260 byte) */
void Intercept_ShifterMode_WriteByte(void)
{
 if( !bUseHighRes && !bUseVDIRes )                    /* Don't store if hi-res and don't store if VDI resolution */
  {
   VideoShifterByte = STRam[0xff8260] & 3;            /* We only care for lower 2-bits */
   Video_WriteToShifter();
   Video_SetHBLPaletteMaskPointers();
   *pHBLPaletteMasks &= 0xff00ffff;
   /* Store resolution after palette mask and set resolution write bit: */
   *pHBLPaletteMasks |= (((unsigned long)VideoShifterByte|0x04)<<16);
  }
}

/* INTERCEPT_DISKCONTROL(0xff8604 word) */
void Intercept_DiskControl_WriteWord(void)
{
 FDC_WriteDiscController( STMemory_ReadWord(0xff8604) );
}

/* INTERCEPT_DMASTATUS(0xff8606 word) */
void Intercept_DmaStatus_WriteWord(void)
{
 FDC_WriteDMAModeControl( STMemory_ReadWord(0xff8606) );
}

/* INTERCEPT_PSG_REGISTER(0xff8800 byte) */
void Intercept_PSGRegister_WriteByte(void)
{
 PSG_WriteSelectRegister( STRam[0xff8800] );
}

/* INTERCEPT_PSG_DATA(0xff8802 byte) */
void Intercept_PSGData_WriteByte(void)
{
 PSG_WriteDataRegister( STRam[0xff8802] );
}

/* INTERCEPT_MICROWIREDATA(0xff8922 word) */
void Intercept_MicrowireData_WriteWord(void)
{
  /* Nothing... */
}

/* INTERCEPT_MONITOR(0xfffa01 byte) */
void Intercept_Monitor_WriteByte(void)
{
  /* Nothing... */
}

/* INTERCEPT_ACTIVE_EDGE(0xfffa03 byte) */
void Intercept_ActiveEdge_WriteByte(void)
{
 MFP_AER = STRam[0xfffa03];
}

/* INTERCEPT_DATA_DIRECTION(0xfffa05 byte) */
void Intercept_DataDirection_WriteByte(void)
{
 MFP_DDR = STRam[0xfffa05];
}


/* INTERCEPT_ENABLE_A(0xfffa07 byte) */
void Intercept_EnableA_WriteByte(void)
{
 MFP_IERA = STRam[0xfffa07];
 MFP_IPRA &= MFP_IERA;
 MFP_UpdateFlags();
 /* We may have enabled Timer A or B, check */
 MFP_StartTimerA();
 MFP_StartTimerB();
}

/* INTERCEPT_ENABLE_B(0xfffa09 byte) */
void Intercept_EnableB_WriteByte(void)
{
 MFP_IERB = STRam[0xfffa09];
 MFP_IPRB &= MFP_IERB;
 MFP_UpdateFlags();
 /* We may have enabled Timer C or D, check */
 MFP_StartTimerC();
 MFP_StartTimerD();
}

/* INTERCEPT_PENDING_A(0xfffa0b byte) */
void Intercept_PendingA_WriteByte(void)
{
 MFP_IPRA &= STRam[0xfffa0b];         /* Cannot set pending bits - only clear via software */
 MFP_UpdateFlags();                   /* Check if any interrupts pending */
}

/* INTERCEPT_PENDING_B(0xfffa0d byte) */
void Intercept_PendingB_WriteByte(void)
{
 MFP_IPRB &= STRam[0xfffa0d];
 MFP_UpdateFlags();                   /* Check if any interrupts pending */
}

/* INTERCEPT_INSERVICE_A(0xfffa0f byte) */
void Intercept_InServiceA_WriteByte(void)
{
 MFP_ISRA &= STRam[0xfffa0f];         /* Cannot set in-service bits - only clear via software */
}

/* INTERCEPT_INSERVICE_B(0xfffa11 byte) */
void Intercept_InServiceB_WriteByte(void)
{
 MFP_ISRB &= STRam[0xfffa11];         /* Cannot set in-service bits - only clear via software */
}

/* INTERCEPT_MASK_A(0xfffa13 byte) */
 void Intercept_MaskA_WriteByte(void)
{
 MFP_IMRA = STRam[0xfffa13];
}

/* INTERCEPT_MASK_B(0xfffa15 byte) */
void Intercept_MaskB_WriteByte(void)
{
 MFP_IMRB = STRam[0xfffa15];
}

/* INTERCEPT_VECTOR_REG(0xfffa17 byte) */
void Intercept_VectorReg_WriteByte(void)
{
 unsigned short old_vr;
 old_vr = MFP_VR;                 /* Copy for checking if set mode */
 MFP_VR = STRam[0xfffa17];
 if( (MFP_VR^old_vr)&0x08 )       /* Test change in end-of-interrupt mode */
   if( MFP_VR&0x08 )              /* Mode did change but was it to automatic mode? (ie bit is a zero) */
     {                            /* We are now in automatic mode, so clear all in-service bits! */
      MFP_ISRA = 0;
      MFP_ISRB = 0;
     }
}

/* INTERCEPT_TIMERA_CTRL(0xfffa19 byte) */
void Intercept_TimerACtrl_WriteByte(void)
{
 unsigned short old_tacr;
 old_tacr = MFP_TACR;               /* Remember old control state */
 MFP_TACR = STRam[0xfffa19] & 0x0f; /* Mask, Fish(auto160) writes into top nibble! */
 if( (MFP_TACR^old_tacr)&0x0f )     /* Check if Timer A control changed */
   MFP_StartTimerA();               /* Reset timers if need to */
}

/* INTERCEPT_TIMERB_CTRL(0xfffa1b byte) */
void Intercept_TimerBCtrl_WriteByte(void)
{
 unsigned short old_tbcr;
 old_tbcr = MFP_TBCR;               /* Remember old control state */
 MFP_TBCR = STRam[0xfffa1b] & 0x0f; /* Mask, Fish(auto160) writes into top nibble! */
 if( (MFP_TBCR^old_tbcr)&0x0f )     /* Check if Timer B control changed */
   MFP_StartTimerB();               /* Reset timers if need to */
}

/* INTERCEPT_TIMERCD_CTRL(0xfffa1d byte) */
void Intercept_TimerCDCtrl_WriteByte(void)
{
 unsigned short old_tcdcr;
 int pc = m68k_getpc();

 old_tcdcr = MFP_TCDCR;             /* Remember old control state */
 MFP_TCDCR = STRam[0xfffa1d];       /* Store new one */
 if( (MFP_TCDCR^old_tcdcr)&0x70 )   /* Check if Timer C control changed */
   MFP_StartTimerC();               /* Reset timers if need to */
 if( (MFP_TCDCR^old_tcdcr)&0x07 ){   /* Check if Timer D control changed */
   if (pc >= TosAddress && pc <= TosAddress + TosSize) {
     MFP_TCDCR = STRam[0xfffa1d] = (STRam[0xfffa1d] & 0xf0) | 7; // slow down timer d if set from tos
   }
   MFP_StartTimerD();               /* Reset timers if need to */
 }
}

/* INTERCEPT_TIMERA_DATA(0xfffa1f byte) */
void Intercept_TimerAData_WriteByte(void)
{
 MFP_TADR = STRam[0xfffa1f];        /* Store into data register */
 if( MFP_TACR==0 )                  /* Now check if timer is running - if so do not set */
  {
   MFP_TA_MAINCOUNTER = MFP_TADR;   /* Timer is off, store to main counter */
   MFP_StartTimerA();               /* Add our interrupt */
  }
}

/* INTERCEPT_TIMERB_DATA(0xfffa21 byte) */
void Intercept_TimerBData_WriteByte(void)
{
 MFP_TBDR = STRam[0xfffa21];        /* Store into data register */
 if( MFP_TBCR==0 )                  /* Now check if timer is running - if so do not set */
  {
   MFP_TB_MAINCOUNTER = MFP_TBDR;   /* Timer is off, store to main counter */
   MFP_StartTimerB();               /* Add our interrupt */
  }
}

/* INTERCEPT_TIMERC_DATA(0xfffa23 byte) */
void Intercept_TimerCData_WriteByte(void)
{
 MFP_TCDR = STRam[0xfffa23];        /* Store into data register */
 if( (MFP_TCDCR&0x70)==0 )          /* Now check if timer is running - if so do not set */
  {
   MFP_StartTimerC();               /* Add our interrupt */
  }
}

/* INTERCEPT_TIMERD_DATA(0xfffa25 byte) */
void Intercept_TimerDData_WriteByte(void)
{
 int pc = m68k_getpc();
 if (pc >= TosAddress && pc <= TosAddress + TosSize) {
   timerd_tos_value = STRam[0xfffa25];
   STRam[0xfffa25] = 0x64; // slow down the useless interrupt from the bios for timer d
 }

 MFP_TDDR = STRam[0xfffa25];        /* Store into data register */
 if( (MFP_TCDCR&0x07)==0 )          /* Now check if timer is running - if so do not set */
  {
   MFP_StartTimerD();               /* Add our interrupt */
  }
}

/* INTERCEPT_KEYBOARDCONTROL(0xfffc00 byte) */
void Intercept_KeyboardControl_WriteByte(void)
{
  /* Nothing... */
}

/* INTERCEPT_KEYBOARDDATA(0xfffc02 byte) */
void Intercept_KeyboardData_WriteByte(void)
{
 IKBD_SendByteToKeyboardProcessor( STRam[0xfffc02] );  /* Pass our byte to the keyboard processor */
}

/* INTERCEPT_MIDICONTROL(0xfffc04 byte) */
void Intercept_MidiControl_WriteByte(void)
{
  Midi_WriteControl(STRam[0xfffc04]);
}

/* INTERCEPT_MIDIDATA(0xfffc06 byte) */
void Intercept_MidiData_WriteByte(void)
{
  Midi_WriteData(STRam[0xfffc06]);
}


void Intercept_BlitterEndmask1_WriteWord(void)
{
  STORE_W_ff8a28( STMemory_ReadWord(0xff8a28) );
}

void Intercept_BlitterEndmask2_WriteWord(void)
{
  STORE_W_ff8a2a( STMemory_ReadWord(0xff8a2a) );
}

void Intercept_BlitterEndmask3_WriteWord(void)
{
  STORE_W_ff8a2c( STMemory_ReadWord(0xff8a2c) );
}

void Intercept_BlitterDst_WriteLong(void)
{
  STORE_L_ff8a32( STMemory_ReadLong(0xff8a32) );
}

void Intercept_BlitterWPL_WriteWord(void)
{
  STORE_W_ff8a36( STMemory_ReadWord(0xff8a36) );
}

void Intercept_BlitterLPB_WriteWord(void)
{
  STORE_W_ff8a38( STMemory_ReadWord(0xff8a38) );
}

void Intercept_BlitterHalftoneOp_WriteByte(void)
{
  STORE_B_ff8a3a( STMemory_ReadByte(0xff8a3a) );
}

void Intercept_BlitterLogOp_WriteByte(void)
{
  STORE_B_ff8a3b( STMemory_ReadByte(0xff8a3b) );
}

void Intercept_BlitterLineNum_WriteByte(void)
{
  STORE_B_ff8a3c( STMemory_ReadByte(0xff8a3c) );
}

void Intercept_BlitterSkew_WriteByte(void)
{
  STORE_B_ff8a3d( STMemory_ReadByte(0xff8a3d) );
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
void Intercept_BusError(void)
{
  M68000_BusError(BusAddressLocation);
}


/*-------------------------------------------------------------------------*/
/*
  Modify 'intercept' tables to cause Bus Errors on addres to un-mapped
  hardware space (Wing Of Death addresses Blitter space which causes
  BusError on STfm)
*/
void Intercept_ModifyTablesForBusErrors(void)
{
  unsigned long *pInterceptList;
  unsigned int Address;
  int i;

  /* Set routine list */
  pInterceptList = pCurrentInterceptWorkspace;
  *pCurrentInterceptWorkspace++ = (unsigned long)Intercept_BusError;
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
      pInterceptReadByteTable[Address-0xff8000] = pInterceptList;
      pInterceptReadWordTable[Address-0xff8000] = pInterceptList;
      pInterceptReadLongTable[Address-0xff8000] = pInterceptList;
      /* and 'write' */
      pInterceptWriteByteTable[Address-0xff8000] = pInterceptList;
      pInterceptWriteWordTable[Address-0xff8000] = pInterceptList;
      pInterceptWriteLongTable[Address-0xff8000] = pInterceptList;
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

