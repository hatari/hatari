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
const char TOS_fileid[] = "Hatari tos.c";

#include <SDL_endian.h>

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "gemdos.h"
#include "hdc.h"
#include "ide.h"
#include "ioMem.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "stMemory.h"
#include "str.h"
#include "tos.h"
#include "lilo.h"
#include "vdi.h"
#include "falcon/dsp.h"
#include "clocks_timings.h"
#include "screen.h"
#include "video.h"

#include "faketosData.c"

#define TEST_PRG_BASEPAGE 0x1000
#define TEST_PRG_START (TEST_PRG_BASEPAGE + 0x100)

bool bIsEmuTOS;
Uint32 EmuTosVersion;
Uint16 TosVersion;                      /* eg. 0x0100, 0x0102 */
Uint32 TosAddress, TosSize;             /* Address in ST memory and size of TOS image */
bool bTosImageLoaded = false;           /* Successfully loaded a TOS image? */
bool bRamTosImage;                      /* true if we loaded a RAM TOS image */
bool bUseTos = true;                    /* false if we run in TOS-less test mode */
const char *psTestPrg;                  /* Points to the name of the PRG that should be used for testing */
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

/* Flags that define if a TOS patch should be applied */
enum
{
	TP_ALWAYS,            /* Patch should always be applied */
	TP_HDIMAGE_OFF,       /* Apply patch only if HD emulation is off */
	TP_ANTI_STE,          /* Apply patch only if running on plain ST */
	TP_ANTI_PMMU,         /* Apply patch only if no PMMU is available */
	TP_FIX_040,           /* Apply patch only if CPU is 68040 */
	TP_FIX_060,           /* Apply patch only if CPU is 68060 */
	TP_VDIRES,            /* Apply patch only if VDI is used */
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
static const char pszMouse[] = "big VDI resolutions mouse driver";
static const char pszRomCheck[] = "ROM checksum";
static const char pszNoSteHw[] = "disable STE hardware access";
static const char pszNoPmmu[] = "disable PMMU access";
static const char pszFix060[] = "replace code for 68060";
static const char pszFix040[] = "replace code for 68040";
static const char pszFalconExtraRAM[] = "enable extra TT RAM on Falcon";
static const char pszAtariLogo[] = "draw Atari Logo";
static const char pszSTbook[] = "disable MCU access on ST-Book";
static const char pszNoSparrowHw[] = "disable Sparrow hardware access";

//static Uint8 pRtsOpcode[] = { 0x4E, 0x75 };  /* 0x4E75 = RTS */
static const Uint8 pNopOpcodes[] = { 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71 };  /* 0x4E71 = NOP */
static const Uint8 pMouseOpcode[] = { 0xD3, 0xC1 };  /* "ADDA.L D1,A1" (instead of "ADDA.W D1,A1") */
static const Uint8 pRomCheckOpcode206[] = { 0x60, 0x00, 0x00, 0x98 };  /* BRA $e00894 */
static const Uint8 pRomCheckOpcode207[] = { 0x60, 0x00, 0x00, 0x98 };  /* BRA $e00892 */
static const Uint8 pRomCheckOpcode306[] = { 0x60, 0x00, 0x00, 0xB0 };  /* BRA $e00886 */
static const Uint8 pRomCheckOpcode404[] = { 0x60, 0x00, 0x00, 0x94 };  /* BRA $e00746 */
static const Uint8 pBraOpcode[] = { 0x60 };  /* 0x60XX = BRA */

/*
 * Routine for drawing the Atari logo.
 * When this function is called, A0 contains a pointer to a 96x86x1 image.
 * We cannot use the vdi yet (the screen workstation has not yet been opened),
 * but we can take into account extended VDI modes.
 */
static const Uint8 pAtariLogo[] = {
	0x3e, 0x3c, 0x00, 0x01,     /* move.w    #planes, d7; number will be patched below */
	0x2c, 0x3c, 0, 0, 1, 64,    /* move.l    #linewidth, d6; number will be patched below */
	0x22, 0x78, 0x04, 0x4e,     /* movea.l   (_v_bas_ad).w,a1 */
	0xd3, 0xc6,                 /* adda.l    d6,a1 ; start drawing at 5th line */
	0xd3, 0xc6,                 /* adda.l    d6,a1 */
	0xd3, 0xc6,                 /* adda.l    d6,a1 */
	0xd3, 0xc6,                 /* adda.l    d6,a1 */
	0xd3, 0xc6,                 /* adda.l    d6,a1 */
	0x30, 0x3c, 0x00, 0x55,     /* move.w    #$0055,d0 ; 86 lines of data */
/* logocol1: */
	0x72, 0x05,                 /* moveq.l   #5,d1 ; 6 words of data per line */
	0x24, 0x49,                 /* movea.l   a1,a2 */
/* logocol2: */
	0x34, 0x18,                 /* move.w    (a0)+,d2 */
	0x36, 0x07,                 /* move.w    d7,d3 */
	0x53, 0x43,                 /* subq.w    #1,d3 */
/* logocol3: */
	0x34, 0xc2,                 /* move.w    d2,(a2)+ */
	0x51, 0xcb, 0xff, 0xfc,     /* dbf       d3,logocol3 */
	0x51, 0xc9, 0xff, 0xf2,     /* dbf       d1,logocol2 */
	0xd3, 0xc6,                 /* adda.l    d6,a1 */
	0x51, 0xc8, 0xff, 0xe8,     /* dbf       d0,logocol1 */
	0x4e, 0x71,                 /* nops to end of original routine */
	0x4e, 0x71,
	0x4e, 0x71,
	0x4e, 0x71,
	0x4e, 0x71,
	0x4e, 0x71,
	0x4e, 0x71,
	0x4e, 0x71
};

static const Uint8 p060movep1[] = {	/* replace MOVEP */
	0x70, 0x0c,			/* moveq #12,d0 */
	0x42, 0x30, 0x08, 0x00,		/* loop: clr.b 0,(d0,a0) */
	0x55, 0x40,			/* subq  #2,d0 */
	0x4a, 0x40,			/* tst.w d0 */
	0x66, 0xf6,			/* bne.s loop */
};
static const Uint8 p060movep2[] = {		/* replace MOVEP */
	0x41, 0xf8, 0xfa, 0x26,			/* lea    0xfffffa26.w,a0 */
	0x20, 0xfc, 0x00, 0x00, 0x00, 0x88,	/* move.l #$00000088,(a0)+ */
	0x20, 0xbc, 0x00, 0x01, 0x00, 0x05,	/* move.l #$00010005,(a0) */
	0x4a, 0x38, 0x0a, 0x87			/* tst.b  $a87.w */
};
static const Uint8 p060movep3_1[] = {		/* replace MOVEP */
	0x4e, 0xb9, 0x00, 0xe7, 0xf0, 0x00,	/* jsr     $e7f000 */
	0x4e, 0x71				/* nop */
};
static const Uint8 p060movep3_2[] = {		/* replace MOVEP $28(a2),d7 */

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

/* patch for cpu type detection  */
static unsigned char const pCputype040[58] = {
	0x20, 0xfc, 0x00, 0x00, 0x00, 0x28,	/* move.l #40,(a0)+ */
	0x4e, 0x71,				/* nop */
	0xf5, 0x18,                             /* pflusha */
	0x4e, 0x71,                             /* nop */
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71
};
static unsigned char const pCputype060[58] = {
	0x20, 0xfc, 0x00, 0x00, 0x00, 0x3c,	/* move.l #60,(a0)+ */
	0x4e, 0x71,				/* nop */
	0xf5, 0x18,                             /* pflusha */
	0x4e, 0x71,                             /* nop */
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71, 0x4e, 0x71,
	0x4e, 0x71
};

static unsigned char const pCacheflush[10] = {
	0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71,	/* nops */
	0xf4, 0xf8,				/* cpusha bc */
	0x4e, 0x71				/* nop */
};
static unsigned char const pCacheflush2[14] = {
	0x4e, 0x71,				/* nop */
	0xf4, 0xf8, 				/* cpusha bc */
	0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71,	/* nops */
	0x4e, 0x71, 0x4e, 0x71			/* nops */
};
static unsigned char const pCacheflush3[12] = {
	0x4e, 0x71,				/* nop */
	0xf4, 0xf8,				/* cpusha bc */
	0x4e, 0x71, 0x4e, 0x71,			/* nops */
	0x4e, 0x71, 0x4e, 0x71			/* nops */
};
static unsigned char const pCacheflush4[12] = {
	0x4e, 0x71,				/* nop */
	0xf4, 0x98,				/* cinva ic */
	0x4e, 0x71, 0x4e, 0x71,			/* nops */
	0x4e, 0x71, 0x4e, 0x71			/* nops */
};
static unsigned char const pColdboot1[10] = {
	0x4d, 0xfa, 0x00, 0x08,			/* lea.l $00E00064(pc),a6 */
	0x4e, 0xf9, 0x00, 0xe7, 0xb0, 0x00	/* jmp $00E7B000 */
};
static unsigned char const pColdboot2[6] = {
	0x4e, 0xf9, 0x00, 0xe7, 0xb0, 0xf4	/* jmp $00E7B0F4 */
};
static unsigned char const pColdboot3[] = {
	0x40, 0xc7,				/* move.w    sr,d7 */
	0x00, 0x7c, 0x07, 0x00,			/* ori.w     #$0700,sr */
	0x4e, 0x7a, 0x10, 0x06,			/* movec     dtt0,d1 */
	0x70, 0x00,				/* moveq.l   #0,d0 */
	0x4e, 0x7b, 0x00, 0x02,			/* movec     d0,cacr */
	0x4e, 0x71,				/* nop */
	0xf4, 0xd8,				/* cinva     bc */
	0x4e, 0x71,				/* nop */
	0x20, 0x3c, 0x00, 0xff, 0xe0, 0x40,	/* move.l    #$00FFE040,d0 */
	0x4e, 0x7b, 0x00, 0x06,			/* movec     d0,dtt0 */
	0x4e, 0x71,				/* nop */
	0xf5, 0x18,				/* pflusha */
	0x4e, 0x71,				/* nop */
	0x7c, 0x00,				/* moveq.l   #0,d6 */
	0x4e, 0x7a, 0x08, 0x07,			/* movec     srp,d0 */
	0x4a, 0x80,				/* tst.l     d0 */
	0x67, 0x32,				/* beq.s     $00E7B062 */
	0xb0, 0xbc, 0x05, 0x00, 0x00, 0x00,	/* cmp.l     #$05000000,d0 */
	0x64, 0x2a,				/* bcc.s     $00E7B062 */
	0xb0, 0xbc, 0x00, 0xe0, 0x00, 0x00,	/* cmp.l     #$00E00000,d0 */
	0x65, 0x08,				/* bcs.s     $00E7B048 */
	0xb0, 0xbc, 0x01, 0x00, 0x00, 0x00,	/* cmp.l     #$01000000,d0 */
	0x65, 0x1a,				/* bcs.s     $00E7B062 */
	                                      /* e7b048 : */
	0x20, 0x40,				/* movea.l   d0,a0 */
	0x0c, 0xa8, 0x54, 0x52, 0x45, 0x45, 0xff, 0xe8,	/* cmpi.l    #$54524545,-24(a0) */
	0x66, 0x00, 0x00, 0x0e,			/* bne.w     $00E7B062 */
	0x0c, 0xa8, 0x4b, 0x45, 0x45, 0x50, 0xff, 0xec,	/* cmpi.l    #$4B454550,-20(a0) */
	0x66, 0x02,				/* bne.s     $00E7B062 */
	0x7c, 0x01,				/* moveq.l   #1,d6 */
	                                      /* e7b062 : */
	0x4e, 0x71,				/* nop */
	0x4e, 0x7b, 0x10, 0x06,			/* movec     d1,dtt0 */
	0x4e, 0x71,				/* nop */
	0xf5, 0x18,				/* pflusha */
	0x4e, 0x71,				/* nop */
	0x4a, 0x86,				/* tst.l     d6 */
	0x67, 0x2e,				/* beq.s     $00E7B0A0 */
	0x20, 0x08,				/* move.l    a0,d0 */
	0x4e, 0x7b, 0x08, 0x07,			/* movec     d0,srp */
	0x4e, 0x7b, 0x08, 0x06,			/* movec     d0,urp */
	0x20, 0x3c, 0x00, 0x00, 0xc0, 0x00,	/* move.l    #$0000C000,d0 */
	0x4e, 0x7b, 0x00, 0x03,			/* movec     d0,tc */
	0x70, 0x00,				/* moveq.l   #0,d0 */
	0x4e, 0x7b, 0x00, 0x04,			/* movec     d0,itt0 */
	0x4e, 0x7b, 0x00, 0x05,			/* movec     d0,itt1 */
	0x4e, 0x7b, 0x00, 0x06,			/* movec     d0,dtt0 */
	0x4e, 0x7b, 0x00, 0x07,			/* movec     d0,dtt1 */
	0x4e, 0x71,				/* nop */
	0xf5, 0x18,				/* pflusha */
	0x4e, 0x71,				/* nop */
	0x60, 0x32,				/* bra.s     $00E7B0D2 */
	                                      /* e7b0a0 : */
	0x20, 0x3c, 0x00, 0xff, 0xe0, 0x00,	/* move.l    #$00FFE000,d0 */
	0x4e, 0x7b, 0x00, 0x04,			/* movec     d0,itt0 */
	0x20, 0x3c,	0x00, 0xff, 0xe0, 0x40,	/* move.l    #$00FFE040,d0 */
	0x4e, 0x7b, 0x00, 0x06,			/* movec     d0,dtt0 */
	0x70, 0x00,				/* moveq.l   #0,d0 */
	0x4e, 0x7b, 0x00, 0x05,			/* movec     d0,itt1 */
	0x4e, 0x7b, 0x00, 0x07,			/* movec     d0,dtt1 */
	0x70, 0x00,				/* moveq.l   #0,d0 */
	0x4e, 0x7b, 0x00, 0x03,			/* movec     d0,tc */
	0x4e, 0x7b, 0x08, 0x07,			/* movec     d0,srp */
	0x4e, 0x7b, 0x08, 0x06,			/* movec     d0,urp */
	0x4e, 0x71,				/* nop */
	0xf5, 0x18,				/* pflusha */
	0x4e, 0x71,				/* nop */
	                                      /* e7b0d2 : */
	0x20, 0x3c, 0x00, 0x00, 0x80, 0x00,	/* move.l    #$00008000,d0 */
	0x4e, 0x7b, 0x00, 0x02,			/* movec     d0,cacr */
	0x46, 0xc7,				/* move.w    d7,sr */
	0x4e, 0xd6,				/* jmp       (a6) */
	                                      /* e7b0e0 : */
	0x08, 0xb8, 0x00, 0x05, 0x82, 0x66,	/* bclr      #5,($FFFF8266).w */
	0x08, 0xb8, 0x00, 0x06, 0x82, 0x66,	/* bclr      #6,($FFFF8266).w */
	0x08, 0xb8, 0x00, 0x00, 0x82, 0x0a,	/* bclr      #0,($FFFF820A).w */
	0x4e, 0xd0,				/* jmp       (a0) */
	                                      /* e7b0f4 : */
	0x00, 0x7c, 0x07, 0x00,			/* ori.w     #$0700,sr */
	0x72, 0x00,				/* moveq.l   #0,d1 */
	0x41, 0xf8, 0x98, 0x00,			/* lea.l     ($FFFF9800).w,a0 */
	0x30, 0x3c, 0x00, 0xff,			/* move.w    #$00FF,d0 */
	0x20, 0xc1,				/* move.l    d1,(a0)+ */
	0x51, 0xc8, 0xff, 0xfc,			/* dbf       d0,$00E7B102 */
	0x41, 0xf8, 0x82, 0x40,			/* lea.l     ($FFFF8240).w,a0 */
	0x70, 0x07,				/* moveq.l   #7,d0 */
	0x20, 0xc1,				/* move.l    d1,(a0)+ */
	0x51, 0xc8, 0xff, 0xfc,			/* dbf       d0,$00E7B10E */
	0x70, 0x00,				/* moveq.l   #0,d0 */
	0x4e, 0x7b, 0x00, 0x02,			/* movec     d0,cacr */
	0x4e, 0x71,				/* nop */
	0x4e, 0x71,				/* nop */
	0xf4, 0xd8,				/* cinva     bc */
	0x4e, 0x71,				/* nop */
	0x41, 0xf8, 0x00, 0x08,			/* lea.l     ($00000008).w,a0 */
	0x20, 0x3c, 0x00, 0x00, 0x06, 0x00,	/* move.l    #$00000600,d0 */
	0x90, 0x88,				/* sub.l     a0,d0 */
	0xe4, 0x88,				/* lsr.l     #2,d0 */
	0x42, 0x81,				/* clr.l     d1 */
	                                      /* e7b132 : */
	0x20, 0xc1,				/* move.l    d1,(a0)+ */
	0x53, 0x80,				/* subq.l    #1,d0 */
	0x66, 0xfa,				/* bne.s     $00E7B132 */
	0x4e, 0x71,				/* nop */
	0xf5, 0x18,				/* pflusha */
	0x4e, 0x71,				/* nop */
	0x20, 0x3c, 0x00, 0xff, 0xe0, 0x00,	/* move.l    #$00FFE000,d0 */
	0x4e, 0x7b, 0x00, 0x04,			/* movec     d0,itt0 */
	0x20, 0x3c, 0x00, 0xff, 0xe0, 0x40,	/* move.l    #$00FFE040,d0 */
	0x4e, 0x7b, 0x00, 0x06,			/* movec     d0,dtt0 */
	0x70, 0x00,				/* moveq.l   #0,d0 */
	0x4e, 0x7b, 0x00, 0x05,			/* movec     d0,itt1 */
	0x4e, 0x7b, 0x00, 0x07,			/* movec     d0,dtt1 */
	0x4e, 0x71,				/* nop */
	0x70, 0x00,				/* moveq.l   #0,d0 */
	0x4e, 0x7b, 0x00, 0x03,			/* movec     d0,tc */
	0x4e, 0x71,				/* nop */
	0x4e, 0x7b, 0x08, 0x07,			/* movec     d0,srp */
	0x4e, 0x7b, 0x08, 0x06,			/* movec     d0,urp */
	0x4e, 0x71,				/* nop */
	0x4e, 0xf9, 0x00, 0xe0, 0x00, 0x30,	/* jmp       $00E00030 */
	                                      /* e7b176 : */
	0x4e, 0x7a, 0x00, 0x02,			/* movec     cacr,d0 */
	0x08, 0x80, 0x00, 0x0f,			/* bclr      #15,d0 */
	0x67, 0x0a,				/* beq.s     $00E7B18A */
	0x4e, 0x7b, 0x00, 0x02,			/* movec     d0,cacr */
	0x4e, 0x71,				/* nop */
	0xf4, 0x98,				/* cinva     ic */
	0x4e, 0x71,				/* nop */
	0x4e, 0xf9, 0xde, 0xad, 0xfa, 0xce, 	/* jmp       $DEADFACE */
	                                      /* e7b190 : */
	0x20, 0x02, 0x10, 0x21,			/* dc.l 0x20021021 */
	0x00, 0xe7, 0xb1, 0x9c,			/* dc.l 0x00e7b19c */
	0x00, 0xe7, 0xb1, 0xa0,			/* dc.l 0x00e7b1a0 */
	                                      /* e7b19c : */
	0x73, 0x00,				/* dc.w 0x7300 (nf_id) */
	0x4e, 0x75,				/* rts */
	                                      /* e7b1a0 : */
	0x73, 0x01,				/* dc.w 0x7301 (nf_call) */
	0x4e, 0x75				/* rts */
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
  { 0x206, -1, pszAtariLogo, TP_VDIRES, 0xE0076C, 0x1038044c, sizeof( pAtariLogo ), pAtariLogo },

  { 0x207, -1, pszNoSparrowHw, TP_ALWAYS, 0xE02D90, 0x08F80005, 6, pNopOpcodes },  /* BSET #5,$ffff8e0d.w */
  { 0x207, -1, pszRomCheck, TP_ALWAYS, 0xE007F8, 0x2E3C0001, 4, pRomCheckOpcode207 },
  { 0x207, -1, pszDmaBoot, TP_HDIMAGE_OFF, 0xE008DC, 0x610000E0, 4, pNopOpcodes }, /* BSR.W $E009BE */
  { 0x207, -1, pszAtariLogo, TP_VDIRES, 0xE0076A, 0x1038044c, sizeof(pAtariLogo), pAtariLogo },

  { 0x208, -1, pszDmaBoot, TP_HDIMAGE_OFF, 0xE00806, 0x610000E8, 4, pNopOpcodes }, /* BSR.W $E008F0 */
  { 0x208, -1, pszAtariLogo, TP_VDIRES, 0xE006B4, 0x1038044c, sizeof( pAtariLogo ), pAtariLogo },
  { 0x208, -1, pszSTbook, TP_ALWAYS, 0xE00066, 0x303900d0, 18, pNopOpcodes },
  { 0x208, -1, pszSTbook, TP_ALWAYS, 0xE000D6, 0x4a7900d0, 6, pNopOpcodes },
  { 0x208, -1, pszSTbook, TP_ALWAYS, 0xE009FE, 0x303900d0, 14, pNopOpcodes },

  { 0x306, -1, pszRomCheck, TP_ALWAYS, 0xE007D4, 0x2E3C0001, 4, pRomCheckOpcode306 },
  { 0x306, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE00068, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x306, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE01702, 0xF0394C00, 32, pNopOpcodes }, /* pmove : CRP=80000002 00000700 TC=80f04445 TT0=017e8107 TT1=807e8507 -> */
  { 0x306, -1, pszFix060, TP_FIX_060, 0xe024dc, 0x01C80000, 12, p060movep1 },
  { 0x306, -1, pszFix060, TP_FIX_060, 0xe024fa, 0x01C80000, 12, p060movep1 },
  { 0x306, -1, pszAtariLogo, TP_VDIRES, 0xE00754, 0x1038044c, sizeof( pAtariLogo ), pAtariLogo },

  { 0x400, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE00064, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x400, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE0148A, 0xF0394C00, 32, pNopOpcodes },
  { 0x400, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE03948, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x400, -1, pszRomCheck, TP_ALWAYS, 0xE00686, 0x2E3C0007, 4, pRomCheckOpcode404 },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE003BE, 0x7200347C, sizeof(pCputype040), pCputype040 },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE00614, 0x4E7B0002, 4, pNopOpcodes },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE0072C, 0x203C0000, sizeof(pCacheflush), pCacheflush },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE00836, 0x4E7A0002, sizeof(pCacheflush2), pCacheflush2 },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE01894, 0x4E7A1002, sizeof(pCacheflush3), pCacheflush3 },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE01916, 0x4E7B0002, 4, pNopOpcodes },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE00054, 0x203c0000, sizeof(pColdboot1), pColdboot1 },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE03934, 0x46FC2700, sizeof(pColdboot2), pColdboot2 },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE7B000, 0xFFFFFFFF, sizeof(pColdboot3), pColdboot3 },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE098AC, 0x4E7A2002, sizeof(pCacheflush4), pCacheflush4 },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE23636, 0x4E7B7002, 4, pNopOpcodes },
  { 0x400, -1, pszFix040, TP_FIX_040, 0xE41634, 0x4E7B7002, 4, pNopOpcodes },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE003BE, 0x7200347C, sizeof(pCputype060), pCputype060 },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE00614, 0x4E7B0002, 4, pNopOpcodes },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE0072C, 0x203C0000, sizeof(pCacheflush), pCacheflush },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE00836, 0x4E7A0002, sizeof(pCacheflush2), pCacheflush2 },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE01894, 0x4E7A1002, sizeof(pCacheflush3), pCacheflush3 },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE01916, 0x4E7B0002, 4, pNopOpcodes },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE00054, 0x203c0000, sizeof(pColdboot1), pColdboot1 },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE03934, 0x46FC2700, sizeof(pColdboot2), pColdboot2 },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE7B000, 0xFFFFFFFF, sizeof(pColdboot3), pColdboot3 },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE098AC, 0x4E7A2002, sizeof(pCacheflush4), pCacheflush4 },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE23636, 0x4E7B7002, 4, pNopOpcodes },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE41634, 0x4E7B7002, 4, pNopOpcodes },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE0258A, 0x01C80000, 12, p060movep1 },
  { 0x400, -1, pszFix060, TP_FIX_060, 0xE025DA, 0x41F8FA01, 20, p060movep2 },

  { 0x401, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE0006A, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x401, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE014A8, 0xF0394C00, 32, pNopOpcodes },
  { 0x401, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE03946, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x401, -1, pszRomCheck, TP_ALWAYS, 0xE006A6, 0x2E3C0007, 4, pRomCheckOpcode404 },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE003C4, 0x7200347C, sizeof(pCputype040), pCputype040 },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE00634, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE0074C, 0x203C0000, sizeof(pCacheflush), pCacheflush },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE00856, 0x4E7A0002, sizeof(pCacheflush2), pCacheflush2 },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE01892, 0x4E7A1002, sizeof(pCacheflush3), pCacheflush3 },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE01914, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE0005A, 0x203c0000, sizeof(pColdboot1), pColdboot1 },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE03932, 0x46FC2700, sizeof(pColdboot2), pColdboot2 },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE7B000, 0xFFFFFFFF, sizeof(pColdboot3), pColdboot3 },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE098A2, 0x4E7A2002, sizeof(pCacheflush4), pCacheflush4 },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE11B28, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE11BB0, 0x4E7B7002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE11CAC, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE12512, 0x4E7B5002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE12888, 0x4E7B6002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE128D4, 0x4E7B6002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE12938, 0x4E7B6002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE12B50, 0x4E7B5002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE12BD0, 0x4E7B5002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE12C48, 0x4E7B2002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE12CC6, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE12D2E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE12DF0, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE17AA6, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE17B3E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE23840, 0x4E7B7002, 4, pNopOpcodes },
  { 0x401, -1, pszFix040, TP_FIX_040, 0xE42670, 0x4E7B7002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE003C4, 0x7200347C, sizeof(pCputype060), pCputype060 },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE00634, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE0074C, 0x203C0000, sizeof(pCacheflush), pCacheflush },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE00856, 0x4E7A0002, sizeof(pCacheflush2), pCacheflush2 },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE01892, 0x4E7A1002, sizeof(pCacheflush3), pCacheflush3 },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE01914, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE0005A, 0x203c0000, sizeof(pColdboot1), pColdboot1 },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE03932, 0x46FC2700, sizeof(pColdboot2), pColdboot2 },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE7B000, 0xFFFFFFFF, sizeof(pColdboot3), pColdboot3 },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE098A2, 0x4E7A2002, sizeof(pCacheflush4), pCacheflush4 },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE11B28, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE11BB0, 0x4E7B7002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE11CAC, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE12512, 0x4E7B5002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE12888, 0x4E7B6002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE128D4, 0x4E7B6002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE12938, 0x4E7B6002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE12B50, 0x4E7B5002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE12BD0, 0x4E7B5002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE12C48, 0x4E7B2002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE12CC6, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE12D2E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE12DF0, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE17AA6, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE17B3E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE23840, 0x4E7B7002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE42670, 0x4E7B7002, 4, pNopOpcodes },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE02588, 0x01C80000, 12, p060movep1 },
  { 0x401, -1, pszFix060, TP_FIX_060, 0xE025D8, 0x41F8FA01, 20, p060movep2 },

  { 0x402, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE0006A, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x402, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE014A8, 0xF0394C00, 32, pNopOpcodes },
  { 0x402, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE03946, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x402, -1, pszRomCheck, TP_ALWAYS, 0xE006A6, 0x2E3C0007, 4, pRomCheckOpcode404 },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE003C4, 0x7200347C, sizeof(pCputype040), pCputype040 },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE00634, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE0074C, 0x203C0000, sizeof(pCacheflush), pCacheflush },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE00856, 0x4E7A0002, sizeof(pCacheflush2), pCacheflush2 },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE01892, 0x4E7A1002, sizeof(pCacheflush3), pCacheflush3 },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE01914, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE0005A, 0x203c0000, sizeof(pColdboot1), pColdboot1 },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE03932, 0x46FC2700, sizeof(pColdboot2), pColdboot2 },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE7B000, 0xFFFFFFFF, sizeof(pColdboot3), pColdboot3 },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE098AC, 0x4E7A2002, sizeof(pCacheflush4), pCacheflush4 },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE11B76, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE11BFE, 0x4E7B7002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE11CFA, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE12560, 0x4E7B5002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE128D6, 0x4E7B6002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE12922, 0x4E7B6002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE12986, 0x4E7B6002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE12B9E, 0x4E7B5002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE12C1E, 0x4E7B5002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE12C96, 0x4E7B2002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE12D14, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE12D7C, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE12E3E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE17AF4, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE17B8C, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE25078, 0x4E7B7002, 4, pNopOpcodes },
  { 0x402, -1, pszFix040, TP_FIX_040, 0xE444B0, 0x4E7B7002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE003C4, 0x7200347C, sizeof(pCputype060), pCputype060 },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE00634, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE0074C, 0x203C0000, sizeof(pCacheflush), pCacheflush },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE00856, 0x4E7A0002, sizeof(pCacheflush2), pCacheflush2 },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE01892, 0x4E7A1002, sizeof(pCacheflush3), pCacheflush3 },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE01914, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE0005A, 0x203c0000, sizeof(pColdboot1), pColdboot1 },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE03932, 0x46FC2700, sizeof(pColdboot2), pColdboot2 },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE7B000, 0xFFFFFFFF, sizeof(pColdboot3), pColdboot3 },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE098AC, 0x4E7A2002, sizeof(pCacheflush4), pCacheflush4 },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE11B76, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE11BFE, 0x4E7B7002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE11CFA, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE12560, 0x4E7B5002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE128D6, 0x4E7B6002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE12986, 0x4E7B6002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE12922, 0x4E7B6002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE12B9E, 0x4E7B5002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE12C1E, 0x4E7B5002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE12C96, 0x4E7B2002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE12D14, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE12D7C, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE12E3E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE17AF4, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE17B8C, 0x4E7B0002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE25078, 0x4E7B7002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE444B0, 0x4E7B7002, 4, pNopOpcodes },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE02588, 0x01C80000, 12, p060movep1 },
  { 0x402, -1, pszFix060, TP_FIX_060, 0xE025D8, 0x41F8FA01, 20, p060movep2 },

  { 0x404, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE0006A, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x404, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE014E6, 0xF0394C00, 32, pNopOpcodes }, /* pmove : CRP=80000002 00000700 TC=80f04445 TT0=017e8107 TT1=807e8507 -> */
  { 0x404, -1, pszNoPmmu, TP_ANTI_PMMU, 0xE039A0, 0xF0394000, 24, pNopOpcodes }, /* pmove : TC=0 TT0=0 TT1=0 -> disable MMU */
  { 0x404, -1, pszRomCheck, TP_ALWAYS, 0xE006B0, 0x2E3C0007, 4, pRomCheckOpcode404 },
  { 0x404, -1, pszDmaBoot, TP_ALWAYS, 0xE01C9E, 0x62FC31FC, 2, pNopOpcodes },  /* Just a delay */
  { 0x404, -1, pszDmaBoot, TP_ALWAYS, 0xE01CB2, 0x62FC31FC, 2, pNopOpcodes },  /* Just a delay */
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE003C4, 0x7200347C, sizeof(pCputype040), pCputype040 },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE0063E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE00756, 0x203C0000, sizeof(pCacheflush), pCacheflush },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE00860, 0x4E7A0002, sizeof(pCacheflush2), pCacheflush2 },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE018D0, 0x4E7A1002, sizeof(pCacheflush3), pCacheflush3 },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE01952, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE0005A, 0x203c0000, sizeof(pColdboot1), pColdboot1 },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE0398C, 0x46FC2700, sizeof(pColdboot2), pColdboot2 },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE7B000, 0xFFFFFFFF, sizeof(pColdboot3), pColdboot3 },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE0990C, 0x4E7A2002, sizeof(pCacheflush4), pCacheflush4 },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE11BD6, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE11C5E, 0x4E7B7002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE11D5A, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE125C0, 0x4E7B5002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE12936, 0x4E7B6002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE12982, 0x4E7B6002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE129E6, 0x4E7B6002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE12BFE, 0x4E7B5002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE12C7E, 0x4E7B5002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE12CF6, 0x4E7B2002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE12D74, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE12DDC, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE12E9E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE17B54, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE17BEC, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE250D8, 0x4E7B7002, 4, pNopOpcodes },
  { 0x404, -1, pszFix040, TP_FIX_040, 0xE44510, 0x4E7B7002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE003C4, 0x7200347C, sizeof(pCputype060), pCputype060 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE0063E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE00756, 0x203C0000, sizeof(pCacheflush), pCacheflush },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE00860, 0x4E7A0002, sizeof(pCacheflush2), pCacheflush2 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE018D0, 0x4E7A1002, sizeof(pCacheflush3), pCacheflush3 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE01952, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE0005A, 0x203c0000, sizeof(pColdboot1), pColdboot1 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE0398C, 0x46FC2700, sizeof(pColdboot2), pColdboot2 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE7B000, 0xFFFFFFFF, sizeof(pColdboot3), pColdboot3 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE0990C, 0x4E7A2002, sizeof(pCacheflush4), pCacheflush4 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE11BD6, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE11C5E, 0x4E7B7002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE11D5A, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE125C0, 0x4E7B5002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE12936, 0x4E7B6002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE12982, 0x4E7B6002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE129E6, 0x4E7B6002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE12BFE, 0x4E7B5002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE12C7E, 0x4E7B5002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE12CF6, 0x4E7B2002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE12D74, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE12DDC, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE12E9E, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE17B54, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE17BEC, 0x4E7B0002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE250D8, 0x4E7B7002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE44510, 0x4E7B7002, 4, pNopOpcodes },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE025E2, 0x01C80000, 12, p060movep1 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE02632, 0x41F8FA01, 20, p060movep2 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE02B1E, 0x007c0700, 8, p060movep3_1 },
  { 0x404, -1, pszFix060, TP_FIX_060, 0xE7F000, 0xFFFFFFFF, sizeof( p060movep3_2 ), p060movep3_2 },
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
 *
 * Set logpatch_addr if patch for that is needed.
 */
static void TOS_FixRom(Uint32 *logopatch_addr)
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
			               ConfigureParams.System.nCpuLevel == 3;
#else
			bool use_mmu = false;
#endif
			/* Only apply the patch if it is really needed: */
			if (pPatch->Flags == TP_ALWAYS
			    || (pPatch->Flags == TP_HDIMAGE_OFF && !bAcsiEmuOn
			        && !ConfigureParams.Ide[0].bUseDevice
			        && ConfigureParams.System.bFastBoot)
			    || (pPatch->Flags == TP_ANTI_STE && Config_IsMachineST())
			    || (pPatch->Flags == TP_ANTI_PMMU && !use_mmu)
			    || (pPatch->Flags == TP_VDIRES && bUseVDIRes)
			    || (pPatch->Flags == TP_FIX_060 && ConfigureParams.System.nCpuLevel > 4)
			    || (pPatch->Flags == TP_FIX_040 && ConfigureParams.System.nCpuLevel == 4)
			   )
			{
				/* Make sure that we really patch the right place by comparing data: */
				if(STMemory_ReadLong(pPatch->Address) == pPatch->OldData)
				{
					/* Now we can really apply the patch! */
					Log_Printf(LOG_DEBUG, "Applying TOS patch '%s'.\n", pPatch->pszName);
					memcpy(&RomMem[pPatch->Address], pPatch->pNewData, pPatch->Size);
					nGoodPatches += 1;
					if (strcmp(pPatch->pszName, pszAtariLogo) == 0)
						*logopatch_addr = pPatch->Address;
				}
				else
				{
					Log_Printf(LOG_DEBUG, "Failed to apply TOS patch '%s' at %x (expected %x, found %x).\n",
					           pPatch->pszName, pPatch->Address, pPatch->OldData, STMemory_ReadLong(pPatch->Address));
					nBadPatches += 1;
				}
			}
			else
			{
				Log_Printf(LOG_DEBUG, "Skipped patch '%s'.\n", pPatch->pszName);
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
	int oldCpuLevel = ConfigureParams.System.nCpuLevel;
	MACHINETYPE oldMachineType = ConfigureParams.System.nMachineType;
#if ENABLE_WINUAE_CPU
	FPUTYPE oldFpuType = ConfigureParams.System.n_FPUType;
#endif

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
		Configuration_ChangeCpuFreq ( 8 );
		ConfigureParams.System.nCpuLevel = 0;
	}
	else if ((TosVersion & 0x0f00) == 0x0300 && !Config_IsMachineTT())
	{
		Log_AlertDlg(LOG_ERROR, "TOS versions 3.0x are for Atari TT only.\n"
		             " ==> Switching to TT mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_TT;
		ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
		Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );
		IoMem_Init();
		Configuration_ChangeCpuFreq ( 32 );
		ConfigureParams.System.nCpuLevel = 3;
	}
	else if (((TosVersion & 0x0f00) == 0x0400 || TosVersion == 0x0207)
	         && !Config_IsMachineFalcon())
	{
		Log_AlertDlg(LOG_ERROR, "TOS version %x.%02x is for Atari Falcon only.\n"
		             " ==> Switching to Falcon mode now.\n",
		             TosVersion >> 8, TosVersion & 0xff);
		Ide_UnInit();
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_FALCON;
		ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
		Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );
#if ENABLE_DSP_EMU
		ConfigureParams.System.nDSPType = DSP_TYPE_EMU;
		DSP_Enable();
#endif
		IoMem_Init();
		Ide_Init();
		Configuration_ChangeCpuFreq ( 16 );
		ConfigureParams.System.nCpuLevel = 3;
	}
	else if (TosVersion <= 0x0104 &&
	         (ConfigureParams.System.nCpuLevel > 0 || !Config_IsMachineST()))
	{
		Log_AlertDlg(LOG_ERROR, "TOS versions <= 1.4 work only in\n"
		             "ST mode and with a 68000 CPU.\n"
		             " ==> Switching to ST mode with 68000 now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_ST;
		ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
		Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );
		IoMem_Init();
		Configuration_ChangeCpuFreq ( 8 );
		ConfigureParams.System.nCpuLevel = 0;
	}
	else if (TosVersion < 0x0300 &&
		 (Config_IsMachineTT() ||
		  (Config_IsMachineFalcon() && TosVersion != 0x0207)))
	{
		Log_AlertDlg(LOG_ERROR, "This TOS version does not work in TT/Falcon mode.\n"
		             " ==> Switching to STE mode now.\n");
		IoMem_UnInit();
		ConfigureParams.System.nMachineType = MACHINE_STE;
		ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
		Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );
		IoMem_Init();
		Configuration_ChangeCpuFreq ( 8 );
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
		if (Config_IsMachineTT())
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
	if (Config_IsMachineFalcon() && bUseVDIRes && !bIsEmuTOS)
	{
		Log_AlertDlg(LOG_ERROR, "Please use 512k EmuTOS instead of TOS v4 "
			     "for proper extended VDI resolutions support on Falcon.");
	}
}


/**
 * Load TOS Rom image file and do some basic sanity checks.
 * Returns pointer to allocated memory with TOS data, or NULL for error.
 */
static uint8_t *TOS_LoadImage(void)
{
	uint8_t *pTosFile = NULL;
	long nFileSize;

	/* Load TOS image into memory so that we can check its version */
	TosVersion = 0;
	pTosFile = File_Read(ConfigureParams.Rom.szTosImageFileName, &nFileSize, pszTosNameExts);

	if (!pTosFile || nFileSize < 0x40)
	{
		Log_AlertDlg(LOG_FATAL, "Can not load TOS file:\n'%s'", ConfigureParams.Rom.szTosImageFileName);
		free(pTosFile);
		return NULL;
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
	if (bIsEmuTOS)
	{
		/* The magic value 'OSXH' indicates an extended header */
		if (SDL_SwapBE32(*(Uint32 *)&pTosFile[0x34]) == 0x4F535848)
			EmuTosVersion = SDL_SwapBE32(*(Uint32 *)&pTosFile[0x3c]);
		else
			EmuTosVersion = 0;	/* Older than 1.0 */
	}

	/* Now, look at start of image to find Version number and address */
	TosVersion = SDL_SwapBE16(*(Uint16 *)&pTosFile[2]);
	TosAddress = SDL_SwapBE32(*(Uint32 *)&pTosFile[8]);
	if (TosVersion == 0x206 && SDL_SwapBE16(*(Uint16 *)&pTosFile[30]) == 0x186A)
		TosVersion = 0x208;

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
		return NULL;
	}

	/* Assert that machine type matches the TOS version.
	 * 512k EmuTOS declares itself as TOS 2.x, but it can handle
	 * all machine types, so it can & needs to be skipped here
	 */
	if (!(bIsEmuTOS && TosSize == 512*1024))
		TOS_CheckSysConfig();

#if ENABLE_WINUAE_CPU
	/* 32-bit addressing is supported only by CPU >= 68020, TOS v3, TOS v4 and EmuTOS */
	if (ConfigureParams.System.nCpuLevel < 2 || (TosVersion < 0x0300 && !bIsEmuTOS))
	{
		ConfigureParams.System.bAddressSpace24 = true;
		M68000_CheckCpuSettings();
	}
	else if (ConfigureParams.Memory.TTRamSize_KB)
	{
		switch (ConfigureParams.System.nMachineType)
		{
		case MACHINE_TT:
			if (ConfigureParams.System.bAddressSpace24)
			{
				/* Print a message and force 32 bit addressing (keeping 24 bit with TT RAM would crash TOS) */
				Log_AlertDlg(LOG_ERROR, "Enabling 32-bit addressing for TT-RAM access.\nThis can cause issues in some programs!\n");
				ConfigureParams.System.bAddressSpace24 = false;
				M68000_CheckCpuSettings();
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

	return pTosFile;
}


/**
 * Set the name of the program that should be tested (without TOS)
 */
void TOS_SetTestPrgName(const char *testprg)
{
	psTestPrg = testprg;
}


/**
 * Create a fake TOS ROM that just jumps to test code in memory
 */
static uint8_t *TOS_FakeRomForTesting(void)
{
	uint8_t *pFakeTosMem;

	/* We don't have a proper memory detection code in above init code,
	 * so we have to disable the MMU emulation in this TOS-less mode */
	ConfigureParams.System.bFastBoot = true;

	TosVersion = 0;
	TosAddress = 0xe00000;
	TosSize = sizeof(FakeTos_data);

	pFakeTosMem = malloc(TosSize);
	if (!pFakeTosMem)
		return NULL;

	memcpy(pFakeTosMem, FakeTos_data, TosSize);

	return pFakeTosMem;
}

/**
 * Load TOS Rom image file into ST memory space and fix image so it can be
 * emulated correctly.  Pre TOS 1.06 are loaded at 0xFC0000 and later ones
 * at 0xE00000.
 *
 * Return zero if all OK, non-zero value for error.
 */
int TOS_InitImage(void)
{
	uint8_t *pTosFile = NULL;
	Uint32 logopatch_addr = 0;

	bTosImageLoaded = false;

	/* Calculate initial end of RAM */
	STRamEnd = ConfigureParams.Memory.STRamSize_KB * 1024;

	if (bUseTos)
	{
		pTosFile = TOS_LoadImage();
		if (!pTosFile)
			return -1;
	}
	else
	{
		pTosFile = TOS_FakeRomForTesting();
		if (!pTosFile)
			return -1;
	}

	/* After TOS is loaded, and machine configuration adapted
	 * accordingly, memory amount can be corrected to a valid
	 * machine specific value
	 */
	STRamEnd = STMemory_CorrectSTRamSize();

	/* (Re-)Initialize the memory banks: */
	memory_uninit();
	memory_init(STRamEnd, ConfigureParams.Memory.TTRamSize_KB*1024, TosAddress);

	/* Clear Upper memory (ROM and IO memory) */
	memset(&RomMem[0xe00000], 0, 0x200000);

	/* Copy loaded image into memory */
	if (bRamTosImage)
		memcpy(&STRam[TosAddress], pTosFile, TosSize);
	else
		memcpy(&RomMem[TosAddress], pTosFile, TosSize);
	free(pTosFile);
	pTosFile = NULL;

	Log_Printf(LOG_DEBUG, "Loaded TOS version %i.%c%c, starting at $%x, "
	           "country code = %i, %s\n", TosVersion>>8, '0'+((TosVersion>>4)&0x0f),
	           '0'+(TosVersion&0x0f), TosAddress, STMemory_ReadWord(TosAddress+28)>>1,
	           (STMemory_ReadWord(TosAddress+28)&1)?"PAL":"NTSC");

	/* Are we allowed VDI under this TOS? */
	if (bUseVDIRes)
	{
		if (TosVersion == 0x0100)
		{
			/* Warn user */
			Log_AlertDlg(LOG_ERROR, "To use extended VDI resolutions, you must select a TOS >= 1.02.");
			/* And select non VDI */
			bUseVDIRes = ConfigureParams.Screen.bUseExtVdiResolutions = false;
		}
		else
		{
			/* needs to be called after TosVersion is set, but
			 * before STMemory_SetDefaultConfig() is called
			 */
			VDI_SetResolution(ConfigureParams.Screen.nVdiColors,
					  ConfigureParams.Screen.nVdiWidth,
					  ConfigureParams.Screen.nVdiHeight);
		}
	}

	/* Fix TOS image, modify code for emulation */
	if (ConfigureParams.Rom.bPatchTos && !bIsEmuTOS && bUseTos)
	{
		TOS_FixRom(&logopatch_addr);
	}
	else
	{
		Log_Printf(LOG_DEBUG, "Skipped TOS patches.\n");
	}

	/*
	 * patch some values into the "Draw logo" patch.
	 * Needs to be called after final VDI resolution has been determined.
	 */
	if (logopatch_addr != 0)
	{
		STMemory_WriteWord(logopatch_addr + 2, VDIPlanes);
		STMemory_WriteLong(logopatch_addr + 6, VDIWidth * VDIPlanes / 8);
	}

	/* Set rest of machine memory configuration, connected devices, etc. */
	STMemory_SetDefaultConfig();

	if (bUseLilo)
	{
		TosSize = 0;
		/* load linux */
		if (!lilo_init())
			return -1;
	}
	else if (!bUseTos)
	{
		/* Load test program (has to be done after memory has been cleared) */
		if (psTestPrg)
		{
			Log_Printf(LOG_DEBUG, "Loading '%s' to 0x%x.\n",
			           psTestPrg, TEST_PRG_START);
			if (GemDOS_LoadAndReloc(psTestPrg, TEST_PRG_BASEPAGE, true))
			{
				fprintf(stderr, "Failed to load '%s'\n", psTestPrg);
				exit(1);
			}
		}
		else
		{
			/* Jump to same address again */
			STMemory_WriteLong(TEST_PRG_START, 0x4ef80000 | TEST_PRG_START);
		}
	}

	bTosImageLoaded = true;
	return 0;
}
