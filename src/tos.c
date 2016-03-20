/*
  Hatari - tos.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Load TOS image file into ST memory, fix/setup for emulator.

  The Atari ST TOS needs to be patched to help with emulation. Eg, it
  references the MMU chip to set memory size. This is patched to the
  sizes we need without the complicated emulation of hardware which
  is not needed (as yet). We also patch DMA devices and Hard Drives.

  NOTE: TOS versions 1.06 and 1.62 were not designed for use on a
  real STfm. These were for the STe machine ONLY. They access the
  DMA/Microwire addresses on boot-up which (correctly) cause a
  bus-error on Hatari as they would in a real STfm. If a user tries
  to select any of these images we bring up an error. */
const char TOS_fileid[] = "Hatari tos.c : " __DATE__ " " __TIME__;

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
#include "str.h"
#include "tos.h"
#include "vdi.h"
#include "falcon/dsp.h"
#include "clocks_timings.h"
#include "screen.h"
#include "video.h"

bool bIsEmuTOS;
Uint16 TosVersion;                      /* eg. 0x0100, 0x0102 */
Uint32 TosAddress, TosSize;             /* Address in ST memory and size of TOS image */
bool bTosImageLoaded = false;           /* Successfully loaded a TOS image? */
bool bRamTosImage;                      /* true if we loaded a RAM TOS image */
unsigned int ConnectedDriveMask = 0x00; /* Bit mask of connected drives, eg 0x7 is A,B,C */
int nNumDrives = 2;                     /* Number of drives, default is 2 for A: and B: - Strictly, this is the highest mapped drive letter, in-between drives may not be allocated */

/* Possible TOS file extensions to scan for */
static const char * const pszTosNameExts[] =
{
	".img",
	".rom",
	".tos",
	NULL
};

static struct {
	FILE *file;          /* file pointer to contents of INF file */
	char prgname[16];    /* TOS name of the program to auto start */
	const char *infname; /* name of the INF file TOS will try to match */
	int match_count;     /* how many times INF was matched after boot */
	int match_max;       /* how many times TOS needs it to be matched */
} TosAutoStart;

/* autostarted program name will be added after first '\' character */
static const char emudesk_inf[] =
"#E 9A 07\r\n"
"#Z 01 C:\\@\r\n"
"#W 00 00 02 06 26 0C 08 C:\\*.*@\r\n"
"#W 00 00 02 08 26 0C 00 @\r\n"
"#W 00 00 02 0A 26 0C 00 @\r\n"
"#W 00 00 02 0D 26 0C 00 @\r\n"
"#M 00 00 01 FF A DISK A@ @\r\n"
"#M 01 00 01 FF B DISK B@ @\r\n"
"#M 02 00 01 FF C DISK C@ @\r\n"
"#F FF 28 @ *.*@\r\n"
"#D FF 02 @ *.*@\r\n"
"#G 08 FF *.APP@ @\r\n"
"#G 08 FF *.PRG@ @\r\n"
"#P 08 FF *.TTP@ @\r\n"
"#F 08 FF *.TOS@ @\r\n"
"#T 00 03 03 FF   TRASH@ @\r\n";

static const char desktop_inf[] =
"#a000000\r\n"
"#b001000\r\n"
"#c7770007000600070055200505552220770557075055507703111302\r\n"
"#d\r\n"
"#Z 01 C:\\@\r\n"
"#E D8 11\r\n"
"#W 00 00 10 01 17 17 13 C:\\*.*@\r\n"
"#W 00 00 08 0B 1D 0D 00 @\r\n"
"#W 00 00 0A 0F 1A 09 00 @\r\n"
"#W 00 00 0E 01 1A 09 00 @\r\n"
"#M 00 00 05 FF A DISK A@ @\r\n"
"#M 00 01 05 FF B DISK B@ @\r\n"
"#M 00 02 05 FF C DISK C@ @\r\n"
"#T 00 03 02 FF   TRASH@ @\r\n"
"#F FF 04   @ *.*@\r\n"
"#D FF 01   @ *.*@\r\n"
"#P 03 04   @ *.*@\r\n"
"#G 03 FF   *.APP@ @\r\n"
"#G 03 FF   *.PRG@ @\r\n"
"#P 03 FF   *.TTP@ @\r\n"
"#F 03 04   *.TOS@ @\r\n";

/* Flags that define if a TOS patch should be applied */
enum
{
	TP_ALWAYS,            /* Patch should alway be applied */
	TP_HDIMAGE_OFF,       /* Apply patch only if HD emulation is off */
	TP_ANTI_STE,          /* Apply patch only if running on plain ST */
	TP_ANTI_PMMU,         /* Apply patch only if no PMMU is available */
	TP_FIX_060,           /* Apply patch only if CPU is 68060 */
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
static const char pszFix060[] = "replace code for 68060";
static const char pszFalconExtraRAM[] = "enable extra TT RAM on Falcon";

//static Uint8 pRtsOpcode[] = { 0x4E, 0x75 };  /* 0x4E75 = RTS */
static const Uint8 pNopOpcodes[] = { 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71 };  /* 0x4E71 = NOP */
static const Uint8 pMouseOpcode[] = { 0xD3, 0xC1 };  /* "ADDA.L D1,A1" (instead of "ADDA.W D1,A1") */
static const Uint8 pRomCheckOpcode206[] = { 0x60, 0x00, 0x00, 0x98 };  /* BRA $e00894 */
static const Uint8 pRomCheckOpcode306[] = { 0x60, 0x00, 0x00, 0xB0 };  /* BRA $e00886 */
static const Uint8 pRomCheckOpcode404[] = { 0x60, 0x00, 0x00, 0x94 };  /* BRA $e00746 */
static const Uint8 pBraOpcode[] = { 0x60 };  /* 0x60XX = BRA */

static const Uint8 p060Pmove1[] = {	/* replace PMOVE */
	0x70, 0x0c,			/* moveq #12,d0 */
	0x42, 0x30, 0x08, 0x00,		/* loop: clr.b 0,(d0,a0) */
	0x55, 0x40,			/* subq  #2,d0 */
	0x4a, 0x40,			/* tst.w d0 */
	0x66, 0xf6,			/* bne.s loop */
};
static const Uint8 p060Pmove2[] = {		/* replace PMOVE */
	0x41, 0xf8, 0xfa, 0x26,			/* lea    0xfffffa26.w,a0 */
	0x20, 0xfc, 0x00, 0x00, 0x00, 0x88,	/* move.l #$00000088,(a0)+ */
	0x20, 0xbc, 0x00, 0x01, 0x00, 0x05,	/* move.l #$00010005,(a0) */
	0x4a, 0x38, 0x0a, 0x87			/* tst.b  $a87.w */
};
static const Uint8 p060Pmove3_1[] = {		/* replace PMOVE */
	0x4e, 0xb9, 0x00, 0xe7, 0xf0, 0x00,	/* jsr     $e7f000 */
	0x4e, 0x71				/* nop */
};
static const Uint8 p060Pmove3_2[] = {		/* replace PMOVE $28(a2),d7 */

	0x00, 0x7c, 0x07, 0x00,			/* ori       #$700,sr */
	0x1e, 0x2a, 0x00, 0x28,			/* move.b    $28(a2),d7 */
	0xe1, 0x4f,				/* lsl.w     #8,d7 */
	0x1e, 0x2a, 0x00, 0x2a,			/* move.b    $2a(a2),d7 */
	0x48, 0x47,				/* swap      d7 */
	0x1e, 0x2a, 0x00, 0x2c,			/* move.b    $2c(a2),d7 */
	0xe1, 0x4f,				/* lsl.w     #8,d7 */
	0x1e, 0x2a, 0x00, 0x2e,			/* move.b    $2e(a2),d7 */
	0x4e, 0x75				/* rts */
};

static const Uint8 pFalconExtraRAM_1[] = {
	0x4e, 0xb9, 0x00, 0xe7, 0xf1, 0x00	/* jsr       $e7f100 */
};
static const Uint8 pFalconExtraRAM_2[] = {	/* call maddalt() to declare the extra RAM */
	0x20, 0x38, 0x05, 0xa4,			/* move.l    $05a4.w,d0 */
	0x67, 0x18,				/* beq.s     $ba2d2 */
	0x04, 0x80, 0x01, 0x00, 0x00, 0x00,	/* subi.l    #$1000000,d0 */
	0x2f, 0x00,				/* move.l    d0,-(sp) */
	0x2f, 0x3c, 0x01, 0x00, 0x00, 0x00,	/* move.l    #$1000000,-(sp) */
	0x3f, 0x3c, 0x00, 0x14,			/* move.w    #$14,-(sp) */
	0x4e, 0x41,				/* trap      #1 */
	0x4f, 0xef, 0x00, 0x0a,			/* lea       $a(sp),sp */
	0x70, 0x03,				/* moveq     #3,d0 */
	0x4e, 0xf9, 0x00, 0xe0, 0x0b, 0xd2	/* jmp       $e00bd2 */
};

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
  { 0x306, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE00068, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x306, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE01702, 0xF0394C00, 32, pNopOpcodes }, /* pmove : CRP=80000002 00000700 TC=80f04445 TT0=017e8107 TT1=807e8507 -> */

  { 0x400, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE00064, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x400, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE0148A, 0xF0394C00, 32, pNopOpcodes },
  { 0x400, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE03948, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x400, -1, pszRomCheck, TP_ALWAYS, 0xE00686, 0x2E3C0007, 4, pRomCheckOpcode404 },

  { 0x401, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE0006A, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x401, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE014A8, 0xF0394C00, 32, pNopOpcodes },
  { 0x401, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE03946, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x401, -1, pszRomCheck, TP_ALWAYS, 0xE006A6, 0x2E3C0007, 4, pRomCheckOpcode404 },

  { 0x402, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE0006A, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x402, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE014A8, 0xF0394C00, 32, pNopOpcodes },
  { 0x402, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE03946, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x402, -1, pszRomCheck, TP_ALWAYS, 0xE006A6, 0x2E3C0007, 4, pRomCheckOpcode404 },

  { 0x404, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE0006A, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x404, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE014E6, 0xF0394C00, 32, pNopOpcodes }, /* pmove : CRP=80000002 00000700 TC=80f04445 TT0=017e8107 TT1=807e8507 -> */
  { 0x404, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE039A0, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x404, -1, pszRomCheck, TP_ALWAYS, 0xE006B0, 0x2E3C0007, 4, pRomCheckOpcode404 },
  { 0x404, -1, pszDmaBoot, TP_ALWAYS, 0xE01C9E, 0x62FC31FC, 2, pNopOpcodes },  /* Just a delay */
  { 0x404, -1, pszDmaBoot, TP_ALWAYS, 0xE01CB2, 0x62FC31FC, 2, pNopOpcodes },  /* Just a delay */
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE025E2, 0x01C80000, 12, p060Pmove1 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE02632, 0x41F8FA01, 20, p060Pmove2 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE02B1E, 0x007c0700, 8, p060Pmove3_1 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE7F000, 0xFFFFFFFF, sizeof( p060Pmove3_2 ), p060Pmove3_2 },
  { 0x404, -1, pszFalconExtraRAM, TP_ALWAYS, 0xE0096E, 0x70036100, 6, pFalconExtraRAM_1 },
  { 0x404, -1, pszFalconExtraRAM, TP_ALWAYS, 0xE7F100, 0xFFFFFFFF, sizeof( pFalconExtraRAM_2 ), pFalconExtraRAM_2 },

  { 0x492, -1, pszNoPmmu, TP_ANTI_PMMU, 0x00F946, 0xF0394000, 24, pNopOpcodes },
  { 0x492, -1, pszNoPmmu, TP_ANTI_PMMU, 0x01097A, 0xF0394C00, 32, pNopOpcodes },
  { 0x492, -1, pszNoPmmu, TP_ANTI_PMMU, 0x012E04, 0xF0394000, 24, pNopOpcodes },

  { 0, 0, NULL, 0, 0, 0, 0, NULL }
};



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void TOS_MemorySnapShot_Capture(bool bSave)
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
#if ENABLE_WINUAE_CPU
			bool use_mmu = ConfigureParams.System.bMMU &&
			               ConfigureParams.System.nCpuLevel >= 3;
#else
			bool use_mmu = false;
#endif
			/* Make sure that we really patch the right place by comparing data: */
			if(STMemory_ReadLong(pPatch->Address) == pPatch->OldData)
			{
				/* Only apply the patch if it is really needed: */
				if (pPatch->Flags == TP_ALWAYS
				    || (pPatch->Flags == TP_HDIMAGE_OFF && !ACSI_EMU_ON
				        && !ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage
				        && ConfigureParams.System.bFastBoot)
				    || (pPatch->Flags == TP_ANTI_STE
				        && ConfigureParams.System.nMachineType == MACHINE_ST)
				    || (pPatch->Flags == TP_ANTI_PMMU && !use_mmu)
				    || (pPatch->Flags == TP_FIX_060 && ConfigureParams.System.nCpuLevel > 4)
				   )
				{
					/* Now we can really apply the patch! */
					Log_Printf(LOG_DEBUG, "Applying TOS patch '%s'.\n", pPatch->pszName);
					memcpy(&RomMem[pPatch->Address], pPatch->pNewData, pPatch->Size);
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
 * Set name of program that will be auto started after TOS boots.
 * Supported only from TOS 1.04 forward.
 */
void TOS_AutoStart(const char *prgname)
{
	Str_Filename2TOSname(prgname, TosAutoStart.prgname);
}

/*-----------------------------------------------------------------------*/
/**
 * Create a temporary desktop.inf file which will start autostart program.
 * This needs to be re-created on each boot in case user changed TOS version.
 */
static void TOS_CreateAutoInf(void)
{
	const char *contents, *infname, *prgname;
	int offset, size, max;
	FILE *fp;

	/* in case TOS didn't for some reason close it on previous boot */
	TOS_AutoStartClose(TosAutoStart.file);

	prgname = TosAutoStart.prgname;
	/* autostart not enabled? */
	if (!*prgname)
		return;

	/* autostart not supported? */
	if (TosVersion < 0x0104)
	{
		Log_Printf(LOG_WARN, "Only TOS versions >= 1.04 support autostarting!\n");
		return;
	}

	if (bIsEmuTOS)
	{
		infname = "C:\\EMUDESK.INF";
		size = sizeof(emudesk_inf);
		contents = emudesk_inf;
		max = 1;
	} else {
		/* need to match file TOS searches first */
		if (TosVersion >= 0x0200)
			infname = "NEWDESK.INF";
		else
			infname = "DESKTOP.INF";
		size = sizeof(desktop_inf);
		contents = desktop_inf;
		max = 1;
	}
	/* infname needs to be exactly the same string that given
	 * TOS version gives for GEMDOS to find.
	 */
	TosAutoStart.infname = infname;
	TosAutoStart.match_max = max;
	TosAutoStart.match_count = 0;

	/* find where to insert the program name */
	for (offset = 0; offset < size; )
	{
		if (contents[offset++] == '\\')
			break;
	}
	assert(offset < size);

	/* create the autostart file */
	fp = tmpfile();
	if (!(fp
	      && fwrite(contents, offset, 1, fp) == 1
	      && fwrite(prgname, strlen(prgname), 1, fp) == 1
	      && fwrite(contents+offset, size-offset-1, 1, fp) == 1
	      && fseek(fp, 0, SEEK_SET) == 0))
	{
		if (fp)
			fclose(fp);
		Log_Printf(LOG_ERROR, "Failed to create autostart file for '%s'!\n", TosAutoStart.prgname);
		return;
	}
	TosAutoStart.file = fp;
	Log_Printf(LOG_WARN, "Virtual autostart file '%s' created for '%s'.\n", infname, prgname);
}

/*-----------------------------------------------------------------------*/
/**
 * If given name matches autostart file, return its handle, NULL otherwise
 */
FILE *TOS_AutoStartOpen(const char *filename)
{
	if (TosAutoStart.file && strcmp(filename, TosAutoStart.infname) == 0)
	{
		/* whether to "autostart" also exception debugging? */
		if (ConfigureParams.Log.nExceptionDebugMask & EXCEPT_AUTOSTART)
		{
			ExceptionDebugMask = ConfigureParams.Log.nExceptionDebugMask & ~EXCEPT_AUTOSTART;
			fprintf(stderr, "Exception debugging enabled (0x%x).\n", ExceptionDebugMask);
		}
		Log_Printf(LOG_WARN, "Autostart file '%s' for '%s' matched.\n", filename, TosAutoStart.prgname);
		return TosAutoStart.file;
	}
	return NULL;
}

/*-----------------------------------------------------------------------*/
/**
 * If given handle matches autostart file, close it and return true,
 * false otherwise.
 */
bool TOS_AutoStartClose(FILE *fp)
{
	if (fp && fp == TosAutoStart.file)
	{
		if (++TosAutoStart.match_count >= TosAutoStart.match_max)
		{
			/* Remove autostart INF file after TOS has
			 * read it enough times to do autostarting.
			 * Otherwise user may try change desktop settings
			 * and save them, but they would be lost.
			 */
			fclose(TosAutoStart.file);
			TosAutoStart.file = NULL;
			Log_Printf(LOG_WARN, "Autostart file removed.\n");
		}
		return true;
	}
	return false;
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
	int oldCpuLevel = ConfigureParams.System.nCpuLevel;
	FPUTYPE oldFpuType = ConfigureParams.System.n_FPUType;
	MACHINETYPE oldMachineType = ConfigureParams.System.nMachineType;

	if (((TosVersion == 0x0106 || TosVersion == 0x0162) && ConfigureParams.System.nMachineType != MACHINE_STE)
	    || (TosVersion == 0x0162 && ConfigureParams.System.nCpuLevel != 0))
	{
		Log_AlertDlg(LOG_ERROR, "TOS versions 1.06 and 1.62 are for Atari STE only.\n"
		             " ==> Switching to STE mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_STE;
		ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
		Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );
		IoMem_Init();
		ConfigureParams.System.nCpuFreq = 8;
		ConfigureParams.System.nCpuLevel = 0;
	}
	else if ((TosVersion & 0x0f00) == 0x0300 && ConfigureParams.System.nMachineType != MACHINE_TT)
	{
		Log_AlertDlg(LOG_ERROR, "TOS versions 3.0x are for Atari TT only.\n"
		             " ==> Switching to TT mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_TT;
		ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
		Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );
		IoMem_Init();
		ConfigureParams.System.nCpuFreq = 32;
		ConfigureParams.System.nCpuLevel = 3;
	}
	else if ((TosVersion & 0x0f00) == 0x0400 && ConfigureParams.System.nMachineType != MACHINE_FALCON)
	{
		Log_AlertDlg(LOG_ERROR, "TOS versions 4.x are for Atari Falcon only.\n"
		             " ==> Switching to Falcon mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_FALCON;
		ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
		Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );
#if ENABLE_DSP_EMU
		ConfigureParams.System.nDSPType = DSP_TYPE_EMU;
		DSP_Enable();
#endif
		IoMem_Init();
		ConfigureParams.System.nCpuFreq = 16;
		ConfigureParams.System.nCpuLevel = 3;
	}
	else if (TosVersion <= 0x0104 &&
	         (ConfigureParams.System.nCpuLevel > 0 || ConfigureParams.System.nMachineType != MACHINE_ST))
	{
		Log_AlertDlg(LOG_ERROR, "TOS versions <= 1.4 work only in\n"
		             "ST mode and with a 68000 CPU.\n"
		             " ==> Switching to ST mode with 68000 now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_ST;
		ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
		Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );
		IoMem_Init();
		ConfigureParams.System.nCpuFreq = 8;
		ConfigureParams.System.nCpuLevel = 0;
	}
	else if ((TosVersion < 0x0300 && ConfigureParams.System.nMachineType == MACHINE_FALCON)
	         || (TosVersion < 0x0200 && ConfigureParams.System.nMachineType == MACHINE_TT))
	{
		Log_AlertDlg(LOG_ERROR, "This TOS version does not work in TT/Falcon mode.\n"
		             " ==> Switching to STE mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_STE;
		ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
		Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );
		IoMem_Init();
		ConfigureParams.System.nCpuFreq = 8;
		ConfigureParams.System.nCpuLevel = 0;
	}
	else if ((TosVersion & 0x0f00) == 0x0400 && ConfigureParams.System.nCpuLevel < 2)
	{
		Log_AlertDlg(LOG_ERROR, "TOS versions 4.x require a CPU >= 68020.\n"
		             " ==> Switching to 68020 mode now.\n");
		ConfigureParams.System.nCpuLevel = 2;
	}
#if ENABLE_WINUAE_CPU
	else if ((TosVersion & 0x0f00) == 0x0300 &&
	         (ConfigureParams.System.nCpuLevel < 2 || ConfigureParams.System.n_FPUType == FPU_NONE))
	{
		Log_AlertDlg(LOG_ERROR, "TOS versions 3.0x require a CPU >= 68020 with FPU.\n"
		             " ==> Switching to 68030 mode with FPU now.\n");
		ConfigureParams.System.nCpuLevel = 3;
		ConfigureParams.System.n_FPUType = FPU_68882;
	}
#else
	else if ((TosVersion & 0x0f00) == 0x0300 && ConfigureParams.System.nCpuLevel < 3)
	{
		Log_AlertDlg(LOG_ERROR, "TOS versions 3.0x require a CPU >= 68020 with FPU.\n"
		             " ==> Switching to 68030 mode with FPU now.\n");
		ConfigureParams.System.nCpuLevel = 3;
	}
#endif

	/* TOS version triggered changes? */
	if (ConfigureParams.System.nMachineType != oldMachineType)
	{
#if ENABLE_WINUAE_CPU
		if (ConfigureParams.System.nMachineType == MACHINE_TT)
		{
			ConfigureParams.System.bCompatibleFPU = true;
			ConfigureParams.System.n_FPUType = FPU_68882;
		} else {
			ConfigureParams.System.n_FPUType = FPU_NONE;	/* TODO: or leave it as-is? */
		}
		if (TosVersion < 0x200)
		{
			ConfigureParams.System.bAddressSpace24 = true;
			ConfigureParams.System.bMMU = false;
		}
#endif
		M68000_CheckCpuSettings();
	}
	else if (ConfigureParams.System.nCpuLevel != oldCpuLevel
#if ENABLE_WINUAE_CPU
		 || ConfigureParams.System.n_FPUType != oldFpuType
#endif
		)
	{
		M68000_CheckCpuSettings();
	}
	if (TosVersion < 0x0104 && ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
		Log_AlertDlg(LOG_ERROR, "Please use at least TOS v1.04 for the HD directory emulation "
			     "(all required GEMDOS functionality isn't completely emulated for this TOS version).");
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Load TOS Rom image file into ST memory space and fix image so it can be
 * emulated correctly.  Pre TOS 1.06 are loaded at 0xFC0000 and later ones
 * at 0xE00000.
 */
int TOS_LoadImage(void)
{
	Uint8 *pTosFile = NULL;
	long nFileSize;

	bTosImageLoaded = false;

	/* Calculate end of RAM */
	if (ConfigureParams.Memory.nMemorySize > 0
	    && ConfigureParams.Memory.nMemorySize <= 14)
		STRamEnd = ConfigureParams.Memory.nMemorySize * 0x100000;
	else
		STRamEnd = 0x80000;   /* 512 KiB */

	/* Load TOS image into memory so that we can check its version */
	TosVersion = 0;
	pTosFile = File_Read(ConfigureParams.Rom.szTosImageFileName, &nFileSize, pszTosNameExts);

	if (!pTosFile || nFileSize <= 0)
	{
		Log_AlertDlg(LOG_FATAL, "Can not load TOS file:\n'%s'", ConfigureParams.Rom.szTosImageFileName);
		free(pTosFile);
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
		bRamTosImage = true;
	}
	else
	{
		bRamTosImage = false;
	}

	/* Check for EmuTOS ... (0x45544F53 = 'ETOS') */
	bIsEmuTOS = (SDL_SwapBE32(*(Uint32 *)&pTosFile[0x2c]) == 0x45544F53);

	/* Now, look at start of image to find Version number and address */
	TosVersion = SDL_SwapBE16(*(Uint16 *)&pTosFile[2]);
	TosAddress = SDL_SwapBE32(*(Uint32 *)&pTosFile[8]);

	/* Check for reasonable TOS version: */
	if (TosVersion == 0x000 && TosSize == 16384)
	{
		/* TOS 0.00 was a very early boot loader ROM which could only
		 * execute a boot sector from floppy disk, which was used in
		 * the very early STs before a full TOS was available in ROM.
		 * It's not very useful nowadays, but we support it here, too,
		 * just for fun. */
		TosAddress = 0xfc0000;
	}
	else if (TosVersion < 0x100 || TosVersion >= 0x500 || TosSize > 1024*1024L
	         || (TosAddress == 0xfc0000 && TosSize > 224*1024L)
	         || (bRamTosImage && TosAddress + TosSize > STRamEnd)
	         || (!bRamTosImage && TosAddress != 0xe00000 && TosAddress != 0xfc0000))
	{
		Log_AlertDlg(LOG_FATAL, "Your TOS image seems not to be a valid TOS ROM file!\n"
		             "(TOS version %x, address $%x)", TosVersion, TosAddress);
		free(pTosFile);
		return -2;
	}

	/* Assert that machine type matches the TOS version. Note that EmuTOS can
	 * handle all machine types, so we don't do the system check there: */
	if (!bIsEmuTOS)
		TOS_CheckSysConfig();

#if ENABLE_WINUAE_CPU
	/* 32-bit addressing is supported only by 680x0, TOS v3, TOS v4 and EmuTOS */
	if (ConfigureParams.System.nCpuLevel == 0 || (TosVersion < 0x0300 && !bIsEmuTOS))
	{
		ConfigureParams.System.bAddressSpace24 = true;
		M68000_CheckCpuSettings();
	}

	else if (ConfigureParams.Memory.nTTRamSize)
	{
		switch (ConfigureParams.System.nMachineType)
		{
		case MACHINE_TT:
			if (ConfigureParams.System.bAddressSpace24)
			{
				/* Print a message and force 32 bit addressing (keeping 24 bit with TT RAM would crash TOS) */
				Log_AlertDlg(LOG_ERROR, "Enabling 32-bit addressing for TT-RAM access.\nThis can cause issues in some programs!\n");
				ConfigureParams.System.bAddressSpace24 = false;
			}
			break;
		case MACHINE_FALCON:
			if (ConfigureParams.System.bAddressSpace24)
			{
				/* Print a message, but don't force 32 bit addressing as 24 bit addressing is also possible under Falcon */
				/* So, if Falcon is in 24 bit mode, we just don't add TT RAM */
				Log_AlertDlg(LOG_ERROR, "You need to disable 24-bit addressing to use TT-RAM in Falcon mode.\n");
			}
			break;
		default:
			break;
		}
	}
#endif

	/* (Re-)Initialize the memory banks: */
	memory_uninit();
	memory_init(STRamEnd, ConfigureParams.Memory.nTTRamSize*1024*1024, TosAddress);

	/* Clear Upper memory (ROM and IO memory) */
	memset(&RomMem[0xe00000], 0, 0x200000);

	/* Copy loaded image into memory */
	if (bRamTosImage)
		memcpy(&STRam[TosAddress], pTosFile, TosSize);
	else
		memcpy(&RomMem[TosAddress], pTosFile, TosSize);

	Log_Printf(LOG_DEBUG, "Loaded TOS version %i.%c%c, starting at $%x, "
	           "country code = %i, %s\n", TosVersion>>8, '0'+((TosVersion>>4)&0x0f),
	           '0'+(TosVersion&0x0f), TosAddress, STMemory_ReadWord(TosAddress+28)>>1,
	           (STMemory_ReadWord(TosAddress+28)&1)?"PAL":"NTSC");

	/* Are we allowed VDI under this TOS? */
	if (TosVersion == 0x0100 && bUseVDIRes)
	{
		/* Warn user */
		Log_AlertDlg(LOG_ERROR, "To use extended VDI resolutions, you must select a TOS >= 1.02.");
		/* And select non VDI */
		bUseVDIRes = ConfigureParams.Screen.bUseExtVdiResolutions = false;
	}

	/* Fix TOS image, modify code for emulation */
	if (ConfigureParams.Rom.bPatchTos && !bIsEmuTOS)
	{
		TOS_FixRom();
	}
	else
	{
		Log_Printf(LOG_DEBUG, "Skipped TOS patches.\n");
	}

	/* Set connected devices, memory configuration, etc. */
	STMemory_SetDefaultConfig();

	/* and free loaded image */
	free(pTosFile);

	bTosImageLoaded = true;
	TOS_CreateAutoInf();

	return 0;
}
