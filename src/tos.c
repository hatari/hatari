/*
  Hatari - tos.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Load TOS image file into ST memory, fix/setup for emulator.

  The Atari ST TOS needs to be patched to help with emulation. Eg, it references
  the MMU chip to set memory size. This is patched to the sizes we need without
  the complicated emulation of hardware which is not needed (as yet). We also
  patch DMA devices and Hard Drives.
  NOTE: TOS versions 1.06 and 1.62 were not designed for use on a real STfm.
  These were for the STe machine ONLY. They access the DMA/Microwire addresses
  on boot-up which (correctly) cause a bus-error on Hatari as they would in a
  real STfm. If a user tries to select any of these images we bring up an error.
*/
static char rcsid[] = "Hatari $Id: tos.c,v 1.15 2003-04-01 16:11:36 thothy Exp $";

#include <SDL_types.h>

#include "main.h"
#include "cart.h"
#include "debug.h"
#include "decode.h"
#include "dialog.h"
#include "errlog.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"


/* Settings for differnt memory sizes */
static MEMORY_INFO MemoryInfo[] =
{
  { 0x80000,  0x01, 0x00080000 },    /* MEMORYSIZE_512 */
  { 0x100000, 0x05, 0x00100000 },    /* MEMORYSIZE_1024 */
  { 0x200000, 0x02, 0x00200000 },    /* MEMORYSIZE_2MB */
  { 0x400000, 0x0A, 0x00400000 }     /* MEMORYSIZE_4MB */
};

/* Bit masks of connected drives(we support up to C,D,E,F,G,H) */
unsigned int ConnectedDriveMaskList[] =
{
  0x03,  /* DRIVELIST_NONE  A,B         */
  0x07,  /* DRIVELIST_C    A,B,C       */
  0x0F,  /* DRIVELIST_CD    A,B,C,D     */
  0x1F,  /* DRIVELIST_CDE  A,B,C,D,E   */
  0x3F,  /* DRIVELIST_CDEF  A,B,C,D,E,F */
  0x7F,  /* DRIVELIST_CDEFG  A,B,C,D,E,F,G */
  0xFF,  /* DRIVELIST_CDEFGH  A,B,C,D,E,F,G,H */
};

unsigned short int TosVersion;          /* eg, 0x0100, 0x0102 */
unsigned long TosAddress, TosSize;      /* Address in ST memory and size of TOS image */
BOOL bTosImageLoaded = FALSE;           /* Successfully loaded a TOS image? */
unsigned int ConnectedDriveMask=0x03;   /* Bit mask of connected drives, eg 0x7 is A,B,C */

/* Possible TOS file extensions to scan for */
char *pszTosNameExts[] =
{
  ".img",
  ".rom",
  ".tos",
  NULL
};

unsigned long STRamEnd;                 /* End of ST Ram, above this address is no-mans-land and hardware vectors */
unsigned long STRamEnd_BusErr;          /* as above, but start of BUS error exception */


/* Flags that define if a TOS patch should be applied */
enum
{
  TP_ALWAYS,            /* Patch should alway be applied */
  TP_HD_ON,             /* Apply patch only if HD emulation is on */
  TP_HD_OFF             /* Apply patch only if HD emulation is off */
};

/* This structure is used for patching the TOS ROMs */
typedef struct
{
  Uint16 Version;       /* TOS version number */
  Sint16 Country;       /* TOS country code: -1 if it does not matter, 0=US, 1=Germany, 2=France, etc. */
  char *pszName;        /* Name of the patch */
  int Flags;            /* When should the patch be applied? (see enum above) */
  Uint32 Address;       /* Where the patch should be applied */
  Uint32 OldData;       /* Expected first 4 old bytes */
  Uint32 Size;          /* Length of the patch */
  void *pNewData;       /* Pointer to the new bytes */
} TOS_PATCH;

static char pszHdvInit[] = "hdv_init - initialize drives";
static char pszHdvBoot[] = "hdv_boot - load boot sector";
static char pszDmaBoot[] = "boot from DMA bus";
static char pszSetConDrv[] = "set connected drives mask";
static char pszClrConDrv[] = "clear connected drives mask";
static char pszTimerD[] = "timer-D init";  /* sets value before calling the set-timer-routine */
static char pszMouse[] = "working mouse in big screen resolutions";
static char pszRomCheck[] = "ROM checksum";
static char pszNoSteHw[] = "disable STE hardware access";

static Uint8 pRtsOpcode[] = { 0x4E, 0x75 };  /* 0x4E75 = RTS */
static Uint8 pNopOpcodes[] = { 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71 };  /* 0x4E71 = NOP */
static Uint8 pConDrvOpcode[] = { 0x00, 0x0A };  /* 0x000A = Hatari's CONDRV_OPCODE */
static Uint8 pTimerDOpcode[] = { 0x00, 0x0B };  /* 0x000B = Hatari's TIMERD_OPCODE */
static Uint8 pMouseOpcode[] = { 0xD3, 0xC1 };  /* "ADDA.L D1,A1" (instead of "ADDA.W D1,A1") */
static Uint8 pRomCheckOpcode[] = { 0x60, 0x00, 0x00, 0x98 };  /* BRA $e00894 */
static Uint8 pBraOpcode[] = { 0x60 };  /* 0x60XX = BRA */

/* The patches for the TOS: */
static TOS_PATCH TosPatches[] =
{
  { 0x100, -1, pszHdvInit, TP_ALWAYS, 0xFC0D60, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x100, -1, pszHdvBoot, TP_ALWAYS, 0xFC1384, 0x4EB900FC, 6, pNopOpcodes }, /* JSR $FC0AF8 */
  { 0x100, -1, pszSetConDrv, TP_HD_ON, 0xFC04d4, 0x4E754DF9, 2, pConDrvOpcode },
  { 0x100, -1, pszDmaBoot, TP_HD_OFF, 0xFC03D6, 0x610000D0, 4, pNopOpcodes }, /* BSR $FC04A8 */
  { 0x100, -1, pszTimerD, TP_ALWAYS, 0xFC21F6, 0x74026100, 2, pTimerDOpcode }, /* (MFP init 0xFC21B4) */

  { 0x102, -1, pszHdvInit, TP_ALWAYS, 0xFC0F44, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x102, -1, pszHdvBoot, TP_ALWAYS, 0xFC1568, 0x4EB900FC, 6, pNopOpcodes }, /* JSR $FC0C2E */
  { 0x102, -1, pszSetConDrv, TP_HD_ON, 0xFC0584, 0x4E754DF9, 2, pConDrvOpcode },
  { 0x102, -1, pszClrConDrv, TP_HD_OFF, 0xFC0302, 0x42B90000, 6, pNopOpcodes }, /* CLR.L $4C2 */
  { 0x102, -1, pszDmaBoot, TP_HD_OFF, 0xFC0472, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $FC0558 */
  /* FIXME: Timer D patch seems to be language dependent for Swedish and Swiss TOS! */
  { 0x102, -1, pszTimerD, TP_ALWAYS, 0xFC2450, 0x74026100, 2, pTimerDOpcode }, /* (MFP init 0xFC2408) */
  { 0x102, 0, pszMouse, TP_ALWAYS, 0xFD0030, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 1, pszMouse, TP_ALWAYS, 0xFD008A, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 2, pszMouse, TP_ALWAYS, 0xFD00A8, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 3, pszMouse, TP_ALWAYS, 0xFD0030, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 6, pszMouse, TP_ALWAYS, 0xFCFEF0, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 8, pszMouse, TP_ALWAYS, 0xFCFEFE, 0xD2C147F9, 2, pMouseOpcode },

  { 0x104, -1, pszHdvInit, TP_ALWAYS, 0xFC16BA, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x104, -1, pszHdvBoot, TP_ALWAYS, 0xFC1CCE, 0x4EB900FC, 6, pNopOpcodes }, /* JSR $FC0BD8 */
  { 0x104, -1, pszSetConDrv, TP_HD_ON, 0xFC0576, 0x4E757A01, 2, pConDrvOpcode },
  { 0x104, -1, pszClrConDrv, TP_HD_OFF, 0xFC02E6, 0x42AD04C2, 4, pNopOpcodes }, /* CLR.L $4C2(A5) */
  { 0x104, -1, pszDmaBoot, TP_HD_OFF, 0xFC0466, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $FC054C */
  { 0x104, -1, pszTimerD, TP_ALWAYS, 0xFC3544, 0x74026100, 2, pTimerDOpcode }, /* (MFP init 0xFC34FC) */

  { 0x205, -1, pszSetConDrv, TP_HD_ON, 0xE0081A, 0x4E752078, 2, pConDrvOpcode }, /* when no bootable DMA devices */
  { 0x205, -1, pszSetConDrv, TP_HD_ON, 0xE00842, 0x4E7541f9, 2, pConDrvOpcode }, /* used if we have DMA devices */
  { 0x205, -1, pszClrConDrv, TP_HD_OFF, 0xE002FC, 0x42B804C2, 4, pNopOpcodes }, /* CLR.L $4C2 */
  { 0x205, -1, pszDmaBoot, TP_HD_OFF, 0xE006AE, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $E00794 */
  { 0x205, -1, pszTimerD, TP_ALWAYS, 0xE01972, 0x74026100, 2, pTimerDOpcode }, /* (MFP init 0xE01928) */
  { 0x205, 0, pszHdvInit, TP_ALWAYS, 0xE0468C, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x205, 1, pszHdvInit, TP_ALWAYS, 0xE046E6, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x205, 2, pszHdvInit, TP_ALWAYS, 0xE04704, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x205, 4, pszHdvInit, TP_ALWAYS, 0xE04712, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x205, 5, pszHdvInit, TP_ALWAYS, 0xE046F4, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x205, 6, pszHdvInit, TP_ALWAYS, 0xE04704, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x205, 0, pszHdvBoot, TP_ALWAYS, 0xE04CA0, 0x4EB900E0, 6, pNopOpcodes }, /* JSR $E00E8E */
  { 0x205, 1, pszHdvBoot, TP_ALWAYS, 0xE04CFA, 0x4EB900E0, 6, pNopOpcodes },
  { 0x205, 2, pszHdvBoot, TP_ALWAYS, 0xE04D18, 0x4EB900E0, 6, pNopOpcodes },
  { 0x205, 4, pszHdvBoot, TP_ALWAYS, 0xE04D26, 0x4EB900E0, 6, pNopOpcodes },
  { 0x205, 5, pszHdvBoot, TP_ALWAYS, 0xE04D08, 0x4EB900E0, 6, pNopOpcodes },
  { 0x205, 6, pszHdvBoot, TP_ALWAYS, 0xE04D18, 0x4EB900E0, 6, pNopOpcodes },
  /* An unpatched TOS 2.05 only works on STEs, so apply some anti-STE patches... */
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE00096, 0x42788900, 4, pNopOpcodes }, /* CLR.W $FFFF8900 */
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE0009E, 0x31D88924, 4, pNopOpcodes }, /* MOVE.W (A0)+,$FFFF8924 */
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE000A6, 0x09D10AA9, 28, pNopOpcodes },
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE003A0, 0x30389200, 4, pNopOpcodes }, /* MOVE.W $ffff9200,D0 */
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE004EA, 0x61000CBC, 4, pNopOpcodes },
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE00508, 0x61000C9E, 4, pNopOpcodes },
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE007A0, 0x631E2F3C, 1, pBraOpcode },
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE00928, 0x10388901, 4, pNopOpcodes }, /* MOVE.B $FFFF8901,D0 */
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE00944, 0xB0388901, 4, pNopOpcodes }, /* CMP.B $FFFF8901,D0 */
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE00950, 0x67024601, 1, pBraOpcode },
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE00968, 0x61000722, 4, pNopOpcodes },
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE00CF2, 0x1038820D, 4, pNopOpcodes }, /* MOVE.B $FFFF820D,D0 */
  { 0x205, -1, pszNoSteHw, TP_ALWAYS, 0xE00E00, 0x1038820D, 4, pNopOpcodes }, /* MOVE.B $FFFF820D,D0 */
  { 0x205, 0, pszNoSteHw, TP_ALWAYS, 0xE03038, 0x31C0860E, 4, pNopOpcodes },
  { 0x205, 0, pszNoSteHw, TP_ALWAYS, 0xE034A8, 0x31C0860E, 4, pNopOpcodes },
  { 0x205, 0, pszNoSteHw, TP_ALWAYS, 0xE034F6, 0x31E90002, 6, pNopOpcodes },

  /* E007FA  MOVE.L  #$1FFFE,D7  Run checksums on 2xROMs (skip) */
  /* Checksum is total of TOS ROM image, but get incorrect results */
  /* as we've changed bytes in the ROM! So, just skip anyway! */
  { 0x206, -1, pszRomCheck, TP_ALWAYS, 0xE007FA, 0x2E3C0001, 4, pRomCheckOpcode },
  { 0x206, -1, pszSetConDrv, TP_HD_ON, 0xE00B3E, 0x4E752078, 2, pConDrvOpcode }, /* when no bootable DMA devices */
  { 0x206, -1, pszSetConDrv, TP_HD_ON, 0xE00B66, 0x4E7541f9, 2, pConDrvOpcode }, /* used if we have DMA devices */
  { 0x206, -1, pszClrConDrv, TP_HD_OFF, 0xE00362, 0x42B804C2, 4, pNopOpcodes }, /* CLR.L $4C2 */
  { 0x206, -1, pszDmaBoot, TP_HD_OFF, 0xE00898, 0x610000E0, 4, pNopOpcodes }, /* BSR.W $E0097A */
  { 0x206, -1, pszTimerD, TP_ALWAYS, 0xE02250, 0x74026100, 2, pTimerDOpcode }, /* (MFP init 0xE02206) */
  { 0x206, 0, pszHdvInit, TP_ALWAYS, 0xE0518E, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x206, 1, pszHdvInit, TP_ALWAYS, 0xE051E8, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x206, 2, pszHdvInit, TP_ALWAYS, 0xE05206, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x206, 3, pszHdvInit, TP_ALWAYS, 0xE0518E, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x206, 6, pszHdvInit, TP_ALWAYS, 0xE05206, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x206, 8, pszHdvInit, TP_ALWAYS, 0xE05214, 0x4E56FFF0, 2, pRtsOpcode },
  { 0x206, 0, pszHdvBoot, TP_ALWAYS, 0xE05944, 0x4EB900E0, 6, pNopOpcodes }, /* JSR  $E011DC */
  { 0x206, 1, pszHdvBoot, TP_ALWAYS, 0xE0599E, 0x4EB900E0, 6, pNopOpcodes },
  { 0x206, 2, pszHdvBoot, TP_ALWAYS, 0xE059BC, 0x4EB900E0, 6, pNopOpcodes },
  { 0x206, 3, pszHdvBoot, TP_ALWAYS, 0xE05944, 0x4EB900E0, 6, pNopOpcodes },
  { 0x206, 6, pszHdvBoot, TP_ALWAYS, 0xE059BC, 0x4EB900E0, 6, pNopOpcodes },
  { 0x206, 8, pszHdvBoot, TP_ALWAYS, 0xE059CA, 0x4EB900E0, 6, pNopOpcodes },

  { 0, 0, NULL, 0, 0, 0, 0, NULL }
};



/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
*/
void TOS_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  MemorySnapShot_Store(&TosVersion, sizeof(TosVersion));
  MemorySnapShot_Store(&TosAddress, sizeof(TosAddress));
  MemorySnapShot_Store(&TosSize, sizeof(TosSize));
  MemorySnapShot_Store(&ConnectedDriveMask, sizeof(ConnectedDriveMask));
}


/*-----------------------------------------------------------------------*/
/*
  Patch TOS to skip some TOS setup code which we don't support/need.

  So, how do we find these addresses when we have no commented source code?
  - Hdv_init: Scan start of TOS for table of move.l <addr>,$46A(a5), around 0x224 bytes in
    and look at the first entry - that's the hdv_init address.
  - Hdv_boot: Scan start of TOS for table of move.l <addr>,$47A(a5), and look for 5th entry,
    that's the hdv_boot address. The function starts with link,movem,jsr.
  - Boot from DMA bus: again scan at start of rom for tst.w $482, boot call will be just above it.
  - Clear connected drives: search for 'clr.w' and '$4c2' to find, may use (a5) in which case op-code
    is only 4 bytes and also note this is only do on TOS > 1.00

  If we use hard disk emulation, we also need to force set condrv ($4c2),
  because the ACSI driver (if any) will reset it. This is done after the DMA
  bus boot (when the driver loads), replacing the RTS with our own routine which
  sets condrv and then RTSes.
*/
static void TOS_FixRom(void)
{
  int nGoodPatches, nBadPatches;
  short TosCountry;
  BOOL bHdIsOn;
  TOS_PATCH *pPatch;

  /* Check for EmuTOS first since we can not patch it */
  if(STMemory_ReadLong(TosAddress+0x2c) == 0x45544F53)      /* 0x45544F53 = 'ETOS' */
  {
    fprintf(stderr, "Detected EmuTOS, skipping TOS patches.\n");
    return;
  }

  nGoodPatches = nBadPatches = 0;
  TosCountry = STMemory_ReadWord(TosAddress+28)>>1;   /* TOS country code */
  bHdIsOn = (ACSI_EMU_ON || GEMDOS_EMU_ON);
  pPatch = TosPatches;

  /* Apply TOS patches: */
  while(pPatch->Version)
  {
    /* Only apply patches that suit to the actual TOS  version: */
    if(pPatch->Version == TosVersion
       && (pPatch->Country == TosCountry || pPatch->Country == -1))
    {
      /* Make sure that we really patch the right place by comparing data: */
      int Address = (TosAddress < 0xe00000 ? // tos in ram ?
		     pPatch->Address - (TosVersion < 0x200 ? 0xfc0000 :
					0xe00000)  + TosAddress :
		     pPatch->Address);
      if(STMemory_ReadLong(Address) == pPatch->OldData)
      {
        /* Only apply the patch if it is really needed: */
        if(pPatch->Flags == TP_ALWAYS || (pPatch->Flags == TP_HD_ON && bHdIsOn)
           || (pPatch->Flags == TP_HD_OFF && !bHdIsOn))
        {
          /* Now we can really apply the patch! */
          /*fprintf(stderr, "Applying TOS patch '%s'.\n", pPatch->pszName);*/
          memcpy(&STRam[Address], pPatch->pNewData, pPatch->Size);
        }
        else
        {
          /*fprintf(stderr, "Skipped patch '%s'.\n", pPatch->pszName);*/
        }
        nGoodPatches += 1;
      }
      else
      {
        fprintf(stderr, "Failed to apply TOS patch '%s' at %x (expected %x found %x).\n", pPatch->pszName,Address,pPatch->OldData,STMemory_ReadLong(Address));
        nBadPatches += 1;
      }
    }
    pPatch += 1;
  }

  fprintf(stderr, "Applied %i TOS patches, %i patches failed.\n",
          nGoodPatches, nBadPatches);

  /* Modify assembler loaded into cartridge area */
  switch(TosVersion)
  {
    case 0x0100:  Cart_WriteHdvAddress(0x167A);  break;
    case 0x0102:  Cart_WriteHdvAddress(0x16DA);  break;
    case 0x0104:  Cart_WriteHdvAddress(0x181C);  break;
    case 0x0205:  Cart_WriteHdvAddress(0x1410);  break;
    case 0x0206:  Cart_WriteHdvAddress(0x1644);  break;
  }

}


/*-----------------------------------------------------------------------*/
/*
  Set default memory configuration, connected floppies and memory size
*/
static void TOS_SetDefaultMemoryConfig(void)
{
  /* As TOS checks hardware for memory size + connected devices on boot-up */
  /* we set these values ourselves and fill in the magic numbers so TOS */
  /* skips these tests which would crash the emulator as the reference the MMU */

  /* Fill in magic numbers, so TOS does not try to reference MMU */
  STMemory_WriteLong(0x420, 0x752019f3);        /* memvalid - configuration is valid */
  STMemory_WriteLong(0x43a, 0x237698aa);        /* another magic # */
  STMemory_WriteLong(0x51a, 0x5555aaaa);        /* and another */

  /* Set memory size, adjust for extra VDI screens if needed */
  if (bUseVDIRes)
  {
    /* This is enough for 1024x768x16colour (0x60000) */
    STMemory_WriteLong(0x436, MemoryInfo[ConfigureParams.Memory.nMemorySize].PhysTop-0x60000);  /* mem top - upper end of user memory (before 32k screen) */
    STMemory_WriteLong(0x42e, MemoryInfo[ConfigureParams.Memory.nMemorySize].PhysTop-0x58000);  /* phys top */
  }
  else
  {
    STMemory_WriteLong(0x436, MemoryInfo[ConfigureParams.Memory.nMemorySize].PhysTop-0x8000);   /* mem top - upper end of user memory(before 32k screen) */
    STMemory_WriteLong(0x42e, MemoryInfo[ConfigureParams.Memory.nMemorySize].PhysTop);          /* phys top */
  }
  STMemory_WriteByte(0x424, MemoryInfo[ConfigureParams.Memory.nMemorySize].MemoryConfig);
  STMemory_WriteByte(0xff8001, MemoryInfo[ConfigureParams.Memory.nMemorySize].MemoryConfig);

  /* Set memory range */
  STRamEnd = MemoryInfo[ConfigureParams.Memory.nMemorySize].MemoryEnd;  /* Set end of RAM */

  /* Set TOS floppies */
  STMemory_WriteWord(0x446, nBootDrive);          /* Boot up on A(0) or C(2) */
  STMemory_WriteWord(0x4a6, 0x2);                 /* Connected floppies A,B (0 or 2) */

  ConnectedDriveMask = ConnectedDriveMaskList[ConfigureParams.HardDisc.nDriveList];

  STMemory_WriteLong(0x4c2, ConnectedDriveMask);  /* Drives A,B and C - NOTE some TOS images overwrite value, see 'TOS_ConnectedDrive_OpCode' */

  /* Mirror ROM boot vectors */
  STMemory_WriteLong(0x00, STMemory_ReadLong(TosAddress));
  STMemory_WriteLong(0x04, STMemory_ReadLong(TosAddress+4));

  /* Initialize the memory banks: */
  memory_uninit();
  memory_init(STRamEnd, 0, TosAddress);
}


/*-----------------------------------------------------------------------*/
/*
  Load TOS Rom image file into ST memory space and fix image so can emulate correctly
  Pre TOS 1.06 are loaded at 0xFC0000 with later ones at 0xE00000
  If we cannot find the TOS image, or we detect an error we default to the built-in
  TOS 1.00 image. This works great for new users who do not understand the idea of a TOS
  Rom and are confused when presented with a 'select TOS image' dialog.
*/
int TOS_LoadImage(void)
{
  void *pTosFile = NULL;

  bTosImageLoaded = FALSE;

  /* Load TOS image into memory so we can check it's vesion */
  TosVersion = 0;
  pTosFile = File_Read(ConfigureParams.TOSGEM.szTOSImageFileName, NULL, NULL, pszTosNameExts);
  TosSize = File_Length(ConfigureParams.TOSGEM.szTOSImageFileName);

  if(pTosFile && TosSize>0)
  {
    /* Now, look at start of image to find Version number and address */
    TosVersion = STMemory_Swap68000Int(*(Uint16 *)((Uint32)pTosFile+2));
    TosAddress = STMemory_Swap68000Long(*(Uint32 *)((Uint32)pTosFile+8));

    if(TosVersion<0x100 || TosVersion>0x500) {
      TosSize-=0x100;
      memmove(pTosFile,pTosFile+0x100,TosSize);
      TosVersion = STMemory_Swap68000Int(*(Uint16 *)((Uint32)pTosFile+2));
      TosAddress = STMemory_Swap68000Long(*(Uint32 *)((Uint32)pTosFile+8));
    }

    /* Check for reasonable TOS version: */
    if(TosVersion<0x100 || TosVersion>0x500 || TosSize>1024*1024L
       || (TosAddress!=0xe00000 && TosAddress!=0xfc0000 && TosAddress!=0xad00))
    {
      Main_Message("Your TOS seems not to be a valid TOS ROM file!\n", PROG_NAME);
      fprintf(stderr,"(Version %x Adress %x)\n",TosVersion,TosAddress);
      return -2;
    }

    /* TOSes 1.06 and 1.62 are for the STe ONLY and so don't run on a real STfm. */
    /* They access illegal memory addresses which don't exist on a real machine and cause the OS */
    /* to lock up. So, if user selects one of these, show an error */
    if(TosVersion==0x0106 || TosVersion==0x0162)
    {
      Main_Message("TOS versions 1.06 and 1.62 are NOT valid STfm images.\n\n"
                   "These were only designed for use on the STe range of machines.\n", PROG_NAME /*,MB_OK|MB_ICONINFORMATION*/);
      return -3;
    }

    /* Copy loaded image into ST memory */
    memcpy((void *)((unsigned long)STRam+TosAddress), pTosFile, TosSize);
  }
  else
  {
    char err_txt[256];
    strcpy(err_txt, "Can not load TOS file:\n ");
    strncat(err_txt, ConfigureParams.TOSGEM.szTOSImageFileName, 256-32);
    strcat(err_txt, "\n");
    Main_Message(err_txt, PROG_NAME);
    return -1;
  }

  fprintf(stderr, "Loaded TOS version %i.%c%c, starting at $%lx, "
          "country code = %i, %s\n", TosVersion>>8, '0'+((TosVersion>>4)&0x0f),
          '0'+(TosVersion&0x0f), TosAddress, STMemory_ReadWord(TosAddress+28)>>1,
          (STMemory_ReadWord(TosAddress+28)&1)?"PAL":"NTSC");

  /* Are we allowed VDI under this TOS? */
  if(TosVersion == 0x0100 && bUseVDIRes)
  {
    /* Warn user (exit if need to) */
    Main_Message("To use GEM extended resolutions, you must select a TOS >= 1.02.",
                 PROG_NAME /*,MB_OK|MB_ICONINFORMATION*/);
    /* And select non VDI */
    bUseVDIRes = ConfigureParams.TOSGEM.bUseExtGEMResolutions = FALSE;
  }

  /* Fix TOS image, modify code for emulation */
  TOS_FixRom();

  /* Set connected devices, memory configuration */
  TOS_SetDefaultMemoryConfig();

  /* and free loaded image */
  Memory_Free(pTosFile);

  bTosImageLoaded = TRUE;
  return 0;
}
