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
char TOS_rcsid[] = "Hatari $Id: tos.c,v 1.32 2005-10-04 13:13:37 thothy Exp $";

#include <SDL_endian.h>

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "ioMem.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"


Uint16 TosVersion;                      /* eg, 0x0100, 0x0102 */
Uint32 TosAddress, TosSize;             /* Address in ST memory and size of TOS image */
BOOL bTosImageLoaded = FALSE;           /* Successfully loaded a TOS image? */
BOOL bRamTosImage;                      /* TRUE if we loaded a RAM TOS image */
unsigned int ConnectedDriveMask = 0x03; /* Bit mask of connected drives, eg 0x7 is A,B,C */


/* Bit masks of connected drives(we support up to C,D,E,F,G,H) */
static const unsigned int ConnectedDriveMaskList[] =
{
  0x03,  /* DRIVELIST_NONE  A,B         */
  0x07,  /* DRIVELIST_C    A,B,C       */
  0x0F,  /* DRIVELIST_CD    A,B,C,D     */
  0x1F,  /* DRIVELIST_CDE  A,B,C,D,E   */
  0x3F,  /* DRIVELIST_CDEF  A,B,C,D,E,F */
  0x7F,  /* DRIVELIST_CDEFG  A,B,C,D,E,F,G */
  0xFF,  /* DRIVELIST_CDEFGH  A,B,C,D,E,F,G,H */
};

/* Possible TOS file extensions to scan for */
static const char *pszTosNameExts[] =
{
  ".img",
  ".rom",
  ".tos",
  NULL
};


/* Flags that define if a TOS patch should be applied */
enum
{
  TP_ALWAYS,            /* Patch should alway be applied */
  TP_ACSI_OFF,          /* Apply patch only if ACSI HD emulation is off */
  TP_ANTI_STE           /* Apply patch only if running on plain ST */
};

/* This structure is used for patching the TOS ROMs */
typedef struct
{
  Uint16 Version;       /* TOS version number */
  Sint16 Country;       /* TOS country code: -1 if it does not matter, 0=US, 1=Germany, 2=France, etc. */
  const char *pszName;  /* Name of the patch */
  int Flags;            /* When should the patch be applied? (see enum above) */
  Uint32 Address;       /* Where the patch should be applied */
  Uint32 OldData;       /* Expected first 4 old bytes */
  Uint32 Size;          /* Length of the patch */
  const void *pNewData; /* Pointer to the new bytes */
} TOS_PATCH;

static const char pszDmaBoot[] = "boot from DMA bus";
static const char pszMouse[] = "working mouse in big screen resolutions";
static const char pszRomCheck[] = "ROM checksum";
static const char pszNoSteHw[] = "disable STE hardware access";

//static Uint8 pRtsOpcode[] = { 0x4E, 0x75 };  /* 0x4E75 = RTS */
static const Uint8 pNopOpcodes[] = { 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71 };  /* 0x4E71 = NOP */
static const Uint8 pMouseOpcode[] = { 0xD3, 0xC1 };  /* "ADDA.L D1,A1" (instead of "ADDA.W D1,A1") */
static const Uint8 pRomCheckOpcode[] = { 0x60, 0x00, 0x00, 0x98 };  /* BRA $e00894 */
static const Uint8 pBraOpcode[] = { 0x60 };  /* 0x60XX = BRA */

/* The patches for the TOS: */
static const TOS_PATCH TosPatches[] =
{
  { 0x100, -1, pszDmaBoot, TP_ACSI_OFF, 0xFC03D6, 0x610000D0, 4, pNopOpcodes }, /* BSR $FC04A8 */

  { 0x102, -1, pszDmaBoot, TP_ACSI_OFF, 0xFC0472, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $FC0558 */
  { 0x102, 0, pszMouse, TP_ALWAYS, 0xFD0030, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 1, pszMouse, TP_ALWAYS, 0xFD008A, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 2, pszMouse, TP_ALWAYS, 0xFD00A8, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 3, pszMouse, TP_ALWAYS, 0xFD0030, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 6, pszMouse, TP_ALWAYS, 0xFCFEF0, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 8, pszMouse, TP_ALWAYS, 0xFCFEFE, 0xD2C147F9, 2, pMouseOpcode },

  { 0x104, -1, pszDmaBoot, TP_ACSI_OFF, 0xFC0466, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $FC054C */

  { 0x106, -1, pszDmaBoot, TP_ACSI_OFF, 0xE00576, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $E0065C */

  { 0x162, -1, pszDmaBoot, TP_ACSI_OFF, 0xE00576, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $E0065C */

  { 0x205, -1, pszDmaBoot, TP_ACSI_OFF, 0xE006AE, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $E00794 */
  /* An unpatched TOS 2.05 only works on STEs, so apply some anti-STE patches... */
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE00096, 0x42788900, 4, pNopOpcodes }, /* CLR.W $FFFF8900 */
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE0009E, 0x31D88924, 4, pNopOpcodes }, /* MOVE.W (A0)+,$FFFF8924 */
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE000A6, 0x09D10AA9, 28, pNopOpcodes },
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE003A0, 0x30389200, 4, pNopOpcodes }, /* MOVE.W $ffff9200,D0 */
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE004EA, 0x61000CBC, 4, pNopOpcodes },
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE00508, 0x61000C9E, 4, pNopOpcodes },
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE007A0, 0x631E2F3C, 1, pBraOpcode },
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE00928, 0x10388901, 4, pNopOpcodes }, /* MOVE.B $FFFF8901,D0 */
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE00944, 0xB0388901, 4, pNopOpcodes }, /* CMP.B $FFFF8901,D0 */
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE00950, 0x67024601, 1, pBraOpcode },
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE00968, 0x61000722, 4, pNopOpcodes },
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE00CF2, 0x1038820D, 4, pNopOpcodes }, /* MOVE.B $FFFF820D,D0 */
  { 0x205, -1, pszNoSteHw, TP_ANTI_STE, 0xE00E00, 0x1038820D, 4, pNopOpcodes }, /* MOVE.B $FFFF820D,D0 */
  { 0x205, 0, pszNoSteHw, TP_ANTI_STE, 0xE03038, 0x31C0860E, 4, pNopOpcodes },
  { 0x205, 0, pszNoSteHw, TP_ANTI_STE, 0xE034A8, 0x31C0860E, 4, pNopOpcodes },
  { 0x205, 0, pszNoSteHw, TP_ANTI_STE, 0xE034F6, 0x31E90002, 6, pNopOpcodes },

  /* E007FA  MOVE.L  #$1FFFE,D7  Run checksums on 2xROMs (skip) */
  /* Checksum is total of TOS ROM image, but get incorrect results */
  /* as we've changed bytes in the ROM! So, just skip anyway! */
  { 0x206, -1, pszRomCheck, TP_ALWAYS, 0xE007FA, 0x2E3C0001, 4, pRomCheckOpcode },
  { 0x206, -1, pszDmaBoot, TP_ACSI_OFF, 0xE00898, 0x610000E0, 4, pNopOpcodes }, /* BSR.W $E0097A */

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
  - For the "Boot from DMA bus" patch:
    Scan at start of rom for tst.w $482, boot call will be just above it.
*/
static void TOS_FixRom(void)
{
  int nGoodPatches, nBadPatches;
  short TosCountry;
  const TOS_PATCH *pPatch;

  /* Check for EmuTOS first since we can not patch it */
  if(STMemory_ReadLong(TosAddress+0x2c) == 0x45544F53)      /* 0x45544F53 = 'ETOS' */
  {
    Log_Printf(LOG_DEBUG, "Detected EmuTOS, skipping TOS patches.\n");
    return;
  }

  /* We also can't patch RAM TOS images (yet) */
  if(bRamTosImage)
  {
    Log_Printf(LOG_DEBUG, "Detected RAM TOS image, skipping TOS patches.\n");
    return;
  }

  nGoodPatches = nBadPatches = 0;
  TosCountry = STMemory_ReadWord(TosAddress+28)>>1;   /* TOS country code */
  pPatch = TosPatches;

  /* Apply TOS patches: */
  while(pPatch->Version)
  {
    /* Only apply patches that suit to the actual TOS  version: */
    if(pPatch->Version == TosVersion
       && (pPatch->Country == TosCountry || pPatch->Country == -1))
    {
      /* Make sure that we really patch the right place by comparing data: */
      if(STMemory_ReadLong(pPatch->Address) == pPatch->OldData)
      {
        /* Only apply the patch if it is really needed: */
        if (pPatch->Flags == TP_ALWAYS || (pPatch->Flags == TP_ACSI_OFF && !ACSI_EMU_ON)
            || (pPatch->Flags == TP_ANTI_STE && ConfigureParams.System.nMachineType == MACHINE_ST))
        {
          /* Now we can really apply the patch! */
          /*fprintf(stderr, "Applying TOS patch '%s'.\n", pPatch->pszName);*/
          memcpy(&STRam[pPatch->Address], pPatch->pNewData, pPatch->Size);
          nGoodPatches += 1;
        }
        else
        {
          /*fprintf(stderr, "Skipped patch '%s'.\n", pPatch->pszName);*/
        }
      }
      else
      {
        Log_Printf(LOG_DEBUG, "Failed to apply TOS patch '%s' at %x (expected %x, found %x).\n",
                   pPatch->pszName, pPatch->Address, pPatch->OldData, STMemory_ReadLong(pPatch->Address));
        nBadPatches += 1;
      }
    }
    pPatch += 1;
  }

  Log_Printf(LOG_DEBUG, "Applied %i TOS patches, %i patches failed.\n",
             nGoodPatches, nBadPatches);
}


/*-----------------------------------------------------------------------*/
/*
  Set default memory configuration, connected floppies and memory size
*/
static void TOS_SetDefaultMemoryConfig(void)
{
  Uint8 nMemControllerByte;
  static const int MemControllerTable[] =
  {
    0x01,   /* 512 KiB */
    0x05,   /* 1 MiB */
    0x02,   /* 2 MiB */
    0x06,   /* 2.5 MiB */
    0x0A    /* 4 MiB */
  };

  /* As TOS checks hardware for memory size + connected devices on boot-up */
  /* we set these values ourselves and fill in the magic numbers so TOS */
  /* skips these tests which would crash the emulator as the reference the MMU */

  /* Fill in magic numbers, so TOS does not try to reference MMU */
  STMemory_WriteLong(0x420, 0x752019f3);          /* memvalid - configuration is valid */
  STMemory_WriteLong(0x43a, 0x237698aa);          /* another magic # */
  STMemory_WriteLong(0x51a, 0x5555aaaa);          /* and another */

  /* Calculate end of RAM */
  if (ConfigureParams.Memory.nMemorySize > 0 && ConfigureParams.Memory.nMemorySize <= 14)
    STRamEnd = ConfigureParams.Memory.nMemorySize * 0x100000;
  else
    STRamEnd = 0x80000;   /* 512 KiB */

  /* Set memory size, adjust for extra VDI screens if needed */
  if (bUseVDIRes)
  {
    /* This is enough for 1024x768x16colors (0x60000) */
    STMemory_WriteLong(0x436, STRamEnd-0x60000);  /* mem top - upper end of user memory (before 32k screen) */
    STMemory_WriteLong(0x42e, STRamEnd-0x58000);  /* phys top */
  }
  else
  {
    STMemory_WriteLong(0x436, STRamEnd-0x8000);   /* mem top - upper end of user memory (before 32k screen) */
    STMemory_WriteLong(0x42e, STRamEnd);          /* phys top */
  }

  /* Set memory controller byte according to different memory sizes */
  /* Setting per bank: %00=128k %01=512k %10=2Mb %11=reserved. - e.g. %1010 means 4Mb */
  if (ConfigureParams.Memory.nMemorySize <= 4)
    nMemControllerByte = MemControllerTable[ConfigureParams.Memory.nMemorySize];
  else
    nMemControllerByte = 0x0f;
  STMemory_WriteByte(0x424, nMemControllerByte);
  STMemory_WriteByte(0xff8001, nMemControllerByte);

  /* Set TOS floppies */
  STMemory_WriteWord(0x446, nBootDrive);          /* Boot up on A(0) or C(2) */
  //STMemory_WriteWord(0x4a6, 0x2);                 /* Connected floppies A,B (0 or 2) */

  ConnectedDriveMask = ConnectedDriveMaskList[ConfigureParams.HardDisk.nDriveList];
  STMemory_WriteLong(0x4c2, ConnectedDriveMask);  /* Drives A,B and C - NOTE: some TOS images overwrite value, see 'OpCode_SysInit' */

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
*/
int TOS_LoadImage(void)
{
  Uint8 *pTosFile = NULL;

  bTosImageLoaded = FALSE;

  /* Load TOS image into memory so we can check it's vesion */
  TosVersion = 0;
  pTosFile = File_Read(ConfigureParams.Rom.szTosImageFileName, NULL, NULL, pszTosNameExts);
  TosSize = File_Length(ConfigureParams.Rom.szTosImageFileName);

  if(pTosFile && TosSize>0)
  {
    /* Check for RAM TOS images first: */
    if(SDL_SwapBE32(*(Uint32 *)pTosFile) == 0x46FC2700)
    {
      Log_Printf(LOG_WARN, "Detected a RAM TOS - this will probably not work very well!\n");
      /* RAM TOS images have a 256 bytes loader function before the real image
       * starts, so we simply skip the first 256 bytes here: */
      TosSize -= 0x100;
      memmove(pTosFile, pTosFile + 0x100, TosSize);
      bRamTosImage = TRUE;
    }
    else
    {
      bRamTosImage = FALSE;
    }

    /* Now, look at start of image to find Version number and address */
    TosVersion = SDL_SwapBE16(*(Uint16 *)&pTosFile[2]);
    TosAddress = SDL_SwapBE32(*(Uint32 *)&pTosFile[8]);

    /* Check for reasonable TOS version: */
    if(TosVersion<0x100 || TosVersion>0x500 || TosSize>1024*1024L
       || (!bRamTosImage && TosAddress!=0xe00000 && TosAddress!=0xfc0000))
    {
      Log_AlertDlg(LOG_FATAL, "Your TOS image seems not to be a valid TOS ROM file!\n"
                              "(TOS version %x, address $%x)", TosVersion, TosAddress);
      return -2;
    }

    /* TOSes 1.06 and 1.62 are for the STE ONLY and so don't run on a real ST. */
    /* They access illegal memory addresses which don't exist on a real machine and cause the OS */
    /* to lock up. So, if user selects one of these, switch to STE mode. */
    if ((TosVersion == 0x0106 || TosVersion == 0x0162) && ConfigureParams.System.nMachineType != MACHINE_STE)
    {
      Log_AlertDlg(LOG_INFO, "TOS versions 1.06 and 1.62\nare NOT valid ST ROM images.\n"
                             " => Switching to STE mode now.\n");
      IoMem_UnInit();
      ConfigureParams.System.nMachineType = MACHINE_STE;
      IoMem_Init();
    }

    /* Copy loaded image into ST memory */
    memcpy(STRam+TosAddress, pTosFile, TosSize);
  }
  else
  {
    Log_AlertDlg(LOG_FATAL, "Can not load TOS file:\n'%s'", ConfigureParams.Rom.szTosImageFileName);
    return -1;
  }

  Log_Printf(LOG_DEBUG, "Loaded TOS version %i.%c%c, starting at $%x, "
          "country code = %i, %s\n", TosVersion>>8, '0'+((TosVersion>>4)&0x0f),
          '0'+(TosVersion&0x0f), TosAddress, STMemory_ReadWord(TosAddress+28)>>1,
          (STMemory_ReadWord(TosAddress+28)&1)?"PAL":"NTSC");

  /* Are we allowed VDI under this TOS? */
  if (TosVersion == 0x0100 && bUseVDIRes)
  {
    /* Warn user */
    Log_AlertDlg(LOG_WARN, "To use GEM extended resolutions, you must select a TOS >= 1.02.");
    /* And select non VDI */
    bUseVDIRes = ConfigureParams.Screen.bUseExtVdiResolutions = FALSE;
  }

  /* Fix TOS image, modify code for emulation */
  TOS_FixRom();

  /* Set connected devices, memory configuration */
  TOS_SetDefaultMemoryConfig();

  /* and free loaded image */
  free(pTosFile);

  bTosImageLoaded = TRUE;
  return 0;
}
