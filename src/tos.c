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
const char TOS_rcsid[] = "Hatari $Id: tos.c,v 1.52 2007-10-31 21:31:50 eerot Exp $";

#include <SDL_endian.h>

#include "main.h"
#include "configuration.h"
#include "file.h"
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
int nNumDrives = 2;                     /* Number of drives, default is 2 for A: and B: */


/* Possible TOS file extensions to scan for */
static const char * const pszTosNameExts[] =
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
	TP_HDIMAGE_OFF,       /* Apply patch only if HD emulation is off */
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
static const char pszMouse[] = "big resolutions mouse driver";
static const char pszRomCheck[] = "ROM checksum";
static const char pszNoSteHw[] = "disable STE hardware access";
static const char pszNoPmmu[] = "disable PMMU access";
static const char pszHwDisable[] = "disable hardware access";

//static Uint8 pRtsOpcode[] = { 0x4E, 0x75 };  /* 0x4E75 = RTS */
static const Uint8 pNopOpcodes[] = { 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71 };  /* 0x4E71 = NOP */
static const Uint8 pMouseOpcode[] = { 0xD3, 0xC1 };  /* "ADDA.L D1,A1" (instead of "ADDA.W D1,A1") */
static const Uint8 pRomCheckOpcode206[] = { 0x60, 0x00, 0x00, 0x98 };  /* BRA $e00894 */
static const Uint8 pRomCheckOpcode306[] = { 0x60, 0x00, 0x00, 0xB0 };  /* BRA $e00886 */
static const Uint8 pRomCheckOpcode404[] = { 0x60, 0x00, 0x00, 0x94 };  /* BRA $e00746 */
static const Uint8 pBraOpcode[] = { 0x60 };  /* 0x60XX = BRA */

/* The patches for the TOS: */
static const TOS_PATCH TosPatches[] =
{
  { 0x100, -1, pszDmaBoot, TP_HDIMAGE_OFF, 0xFC03D6, 0x610000D0, 4, pNopOpcodes }, /* BSR $FC04A8 */

  { 0x102, -1, pszDmaBoot, TP_HDIMAGE_OFF, 0xFC0472, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $FC0558 */
  { 0x102, 0, pszMouse, TP_ALWAYS, 0xFD0030, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 1, pszMouse, TP_ALWAYS, 0xFD008A, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 2, pszMouse, TP_ALWAYS, 0xFD00A8, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 3, pszMouse, TP_ALWAYS, 0xFD0030, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 6, pszMouse, TP_ALWAYS, 0xFCFEF0, 0xD2C147F9, 2, pMouseOpcode },
  { 0x102, 8, pszMouse, TP_ALWAYS, 0xFCFEFE, 0xD2C147F9, 2, pMouseOpcode },

  { 0x104, -1, pszDmaBoot, TP_HDIMAGE_OFF, 0xFC0466, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $FC054C */

  { 0x106, -1, pszDmaBoot, TP_HDIMAGE_OFF, 0xE00576, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $E0065C */

  { 0x162, -1, pszDmaBoot, TP_HDIMAGE_OFF, 0xE00576, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $E0065C */

  { 0x205, -1, pszDmaBoot, TP_HDIMAGE_OFF, 0xE006AE, 0x610000E4, 4, pNopOpcodes }, /* BSR.W $E00794 */
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
  { 0x206, -1, pszRomCheck, TP_ALWAYS, 0xE007FA, 0x2E3C0001, 4, pRomCheckOpcode206 },
  { 0x206, -1, pszDmaBoot, TP_HDIMAGE_OFF, 0xE00898, 0x610000E0, 4, pNopOpcodes }, /* BSR.W $E0097A */

  { 0x306, -1, pszRomCheck, TP_ALWAYS, 0xE007D4, 0x2E3C0001, 4, pRomCheckOpcode306 },
  { 0x306, -1, pszNoPmmu, TP_ALWAYS, 0xE00068, 0xF0394000, 24, pNopOpcodes },
  { 0x306, -1, pszNoPmmu, TP_ALWAYS, 0xE01702, 0xF0394C00, 32, pNopOpcodes },

  { 0x400, -1, pszNoPmmu, TP_ALWAYS, 0xE00064, 0xF0394000, 24, pNopOpcodes },
  { 0x400, -1, pszNoPmmu, TP_ALWAYS, 0xE0148A, 0xF0394C00, 32, pNopOpcodes },
  { 0x400, -1, pszNoPmmu, TP_ALWAYS, 0xE03948, 0xF0394000, 24, pNopOpcodes },
  { 0x400, -1, pszRomCheck, TP_ALWAYS, 0xE00686, 0x2E3C0007, 4, pRomCheckOpcode404 },

  { 0x401, -1, pszNoPmmu, TP_ALWAYS, 0xE0006A, 0xF0394000, 24, pNopOpcodes },
  { 0x401, -1, pszNoPmmu, TP_ALWAYS, 0xE014A8, 0xF0394C00, 32, pNopOpcodes },
  { 0x401, -1, pszNoPmmu, TP_ALWAYS, 0xE03946, 0xF0394000, 24, pNopOpcodes },
  { 0x401, -1, pszRomCheck, TP_ALWAYS, 0xE006A6, 0x2E3C0007, 4, pRomCheckOpcode404 },

  { 0x402, -1, pszNoPmmu, TP_ALWAYS, 0xE0006A, 0xF0394000, 24, pNopOpcodes },
  { 0x402, -1, pszNoPmmu, TP_ALWAYS, 0xE014A8, 0xF0394C00, 32, pNopOpcodes },
  { 0x402, -1, pszNoPmmu, TP_ALWAYS, 0xE03946, 0xF0394000, 24, pNopOpcodes },
  { 0x402, -1, pszRomCheck, TP_ALWAYS, 0xE006A6, 0x2E3C0007, 4, pRomCheckOpcode404 },

  { 0x404, -1, pszNoPmmu, TP_ALWAYS, 0xE0006A, 0xF0394000, 24, pNopOpcodes },
  { 0x404, -1, pszNoPmmu, TP_ALWAYS, 0xE014E6, 0xF0394C00, 32, pNopOpcodes },
  { 0x404, -1, pszNoPmmu, TP_ALWAYS, 0xE039A0, 0xF0394000, 24, pNopOpcodes },
  { 0x404, -1, pszRomCheck, TP_ALWAYS, 0xE006B0, 0x2E3C0007, 4, pRomCheckOpcode404 },

  { 0x492, -1, pszNoPmmu, TP_ALWAYS, 0x00F946, 0xF0394000, 24, pNopOpcodes },
  { 0x492, -1, pszNoPmmu, TP_ALWAYS, 0x01097A, 0xF0394C00, 32, pNopOpcodes },
  { 0x492, -1, pszNoPmmu, TP_ALWAYS, 0x012E04, 0xF0394000, 24, pNopOpcodes },

  { 0, 0, NULL, 0, 0, 0, 0, NULL }
};



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void TOS_MemorySnapShot_Capture(BOOL bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&TosVersion, sizeof(TosVersion));
	MemorySnapShot_Store(&TosAddress, sizeof(TosAddress));
	MemorySnapShot_Store(&TosSize, sizeof(TosSize));
	MemorySnapShot_Store(&ConnectedDriveMask, sizeof(ConnectedDriveMask));
	MemorySnapShot_Store(&nNumDrives, sizeof(nNumDrives));
}


/*-----------------------------------------------------------------------*/
/**
 * Patch TOS to skip some TOS setup code which we don't support/need.
 *
 * So, how do we find these addresses when we have no commented source code?
 * - For the "Boot from DMA bus" patch:
 *   Scan at start of rom for tst.w $482, boot call will be just above it.
 */
static void TOS_FixRom(void)
{
	int nGoodPatches, nBadPatches;
	short TosCountry;
	const TOS_PATCH *pPatch;

	/* We can't patch RAM TOS images (yet) */
	if (bRamTosImage && TosVersion != 0x0492)
	{
		Log_Printf(LOG_DEBUG, "Detected RAM TOS image, skipping TOS patches.\n");
		return;
	}

	nGoodPatches = nBadPatches = 0;
	TosCountry = STMemory_ReadWord(TosAddress+28)>>1;   /* TOS country code */
	pPatch = TosPatches;

	/* Apply TOS patches: */
	while (pPatch->Version)
	{
		/* Only apply patches that suit to the actual TOS  version: */
		if (pPatch->Version == TosVersion
		    && (pPatch->Country == TosCountry || pPatch->Country == -1))
		{
			/* Make sure that we really patch the right place by comparing data: */
			if(STMemory_ReadLong(pPatch->Address) == pPatch->OldData)
			{
				/* Only apply the patch if it is really needed: */
				if (pPatch->Flags == TP_ALWAYS
				    || (pPatch->Flags == TP_HDIMAGE_OFF && !ACSI_EMU_ON && !ConfigureParams.HardDisk.bUseIdeHardDiskImage)
				    || (pPatch->Flags == TP_ANTI_STE && ConfigureParams.System.nMachineType == MACHINE_ST))
				{
					/* Now we can really apply the patch! */
					Log_Printf(LOG_DEBUG, "Applying TOS patch '%s'.\n", pPatch->pszName);
					memcpy(&STRam[pPatch->Address], pPatch->pNewData, pPatch->Size);
					nGoodPatches += 1;
				}
				else
				{
					Log_Printf(LOG_DEBUG, "Skipped patch '%s'.\n", pPatch->pszName);
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
/**
 * Assert that TOS version matches the machine type and change the system
 * configuration if necessary.
 * For example TOSes 1.06 and 1.62 are for the STE ONLY and so don't run
 * on a real ST, TOS 3.0x is TT only and TOS 4.x is Falcon only.
 * These TOS version access illegal memory addresses on machine they were
 * not designed for and so cause the OS to lock up. So, if user selects one
 * of these, switch to the appropriate machine type.
 */
static void TOS_CheckSysConfig(void)
{
	if ((TosVersion == 0x0106 || TosVersion == 0x0162) && ConfigureParams.System.nMachineType != MACHINE_STE)
	{
		Log_AlertDlg(LOG_INFO, "TOS versions 1.06 and 1.62 are for Atari STE only.\n"
		             " ==> Switching to STE mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_STE;
		IoMem_Init();
	}
	else if ((TosVersion & 0x0f00) == 0x0300 && ConfigureParams.System.nMachineType != MACHINE_TT)
	{
		Log_AlertDlg(LOG_INFO, "TOS versions 3.0x are for Atari TT only.\n"
		             " ==> Switching to TT mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_TT;
		IoMem_Init();
		ConfigureParams.System.nCpuLevel = 3;
		M68000_CheckCpuLevel();
	}
	else if ((TosVersion & 0x0f00) == 0x0400 && ConfigureParams.System.nMachineType != MACHINE_FALCON)
	{
		Log_AlertDlg(LOG_INFO, "TOS versions 4.x are for Atari Falcon only.\n"
		             " ==> Switching to Falcon mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_FALCON;
		IoMem_Init();
		ConfigureParams.System.nCpuLevel = 3;
		M68000_CheckCpuLevel();
	}
	else if ((TosVersion < 0x0300 && ConfigureParams.System.nMachineType == MACHINE_FALCON)
	         || (TosVersion < 0x0200 && ConfigureParams.System.nMachineType == MACHINE_TT))
	{
		Log_AlertDlg(LOG_INFO, "This TOS versions does not work in TT/Falcon mode.\n"
		             " ==> Switching to STE mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_STE;
		IoMem_Init();
		if (TosVersion <= 0x0104)
		{
			ConfigureParams.System.nCpuLevel = 0;
			M68000_CheckCpuLevel();
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Load TOS Rom image file into ST memory space and fix image so can emulate correctly
 * Pre TOS 1.06 are loaded at 0xFC0000 with later ones at 0xE00000
 */
int TOS_LoadImage(void)
{
	Uint8 *pTosFile = NULL;
	long nFileSize;
	BOOL bIsEmuTOS;

	bTosImageLoaded = FALSE;

	/* Load TOS image into memory so we can check it's vesion */
	TosVersion = 0;
	pTosFile = File_Read(ConfigureParams.Rom.szTosImageFileName, &nFileSize, pszTosNameExts);

	if (!pTosFile || nFileSize <= 0)
	{
		Log_AlertDlg(LOG_FATAL, "Can not load TOS file:\n'%s'", ConfigureParams.Rom.szTosImageFileName);
		return -1;
	}

	TosSize = nFileSize;

	/* Check for RAM TOS images first: */
	if (SDL_SwapBE32(*(Uint32 *)pTosFile) == 0x46FC2700)
	{
		int nRamTosLoaderSize;
		Log_Printf(LOG_WARN, "Detected a RAM TOS - this will probably not work very well!\n");
		/* RAM TOS images have a 256 bytes loader function before the real image
		 * starts (34 bytes for TOS 4.92). Since we directly copy the image to the right
		 * location later, we simply skip this additional header here: */
		if (SDL_SwapBE32(*(Uint32 *)(pTosFile+34)) == 0x602E0492)
			nRamTosLoaderSize = 0x22;
		else
			nRamTosLoaderSize = 0x100;
		TosSize -= nRamTosLoaderSize;
		memmove(pTosFile, pTosFile + nRamTosLoaderSize, TosSize);
		bRamTosImage = TRUE;
	}
	else
	{
		bRamTosImage = FALSE;
	}

	/* Check for EmuTOS ... (0x45544F53 = 'ETOS') */
	bIsEmuTOS = (SDL_SwapBE32(*(Uint32 *)&pTosFile[0x2c]) == 0x45544F53);

	/* Now, look at start of image to find Version number and address */
	TosVersion = SDL_SwapBE16(*(Uint16 *)&pTosFile[2]);
	TosAddress = SDL_SwapBE32(*(Uint32 *)&pTosFile[8]);

	/* Check for reasonable TOS version: */
	if (TosVersion<0x100 || TosVersion>=0x500 || TosSize>1024*1024L
	    || (!bRamTosImage && TosAddress!=0xe00000 && TosAddress!=0xfc0000))
	{
		Log_AlertDlg(LOG_FATAL, "Your TOS image seems not to be a valid TOS ROM file!\n"
		             "(TOS version %x, address $%x)", TosVersion, TosAddress);
		return -2;
	}

	/* Assert that machine type matches the TOS version. Note that EmuTOS can
	 * handle all machine types, so we don't do the system check there: */
	if (!bIsEmuTOS)
		TOS_CheckSysConfig();

	/* Copy loaded image into ST memory */
	memcpy(STRam+TosAddress, pTosFile, TosSize);

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
	if (!bIsEmuTOS)
		TOS_FixRom();

	/* Set connected devices, memory configuration, etc. */
	STMemory_SetDefaultConfig();

	/* and free loaded image */
	free(pTosFile);

	bTosImageLoaded = TRUE;

	return 0;
}
