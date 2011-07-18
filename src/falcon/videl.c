/*
  Hatari - videl.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Falcon Videl emulation. The Videl is the graphics shifter chip of the Falcon.
  It supports free programmable resolutions with 1, 2, 4, 8 or 16 bits per
  pixel.

  This file originally came from the Aranym project and has been heavily
  modified to work for Hatari (but the kudos for the great Videl emulation
  code goes to the people from the Aranym project of course).

  Videl can run at 2 frequencies : 25.175 Mhz or 32 MHz
  
  Hardware I/O registers:

	$FFFF8006 (byte) : monitor type

	$FFFF8201 (byte) : VDL_VBH - Video Base Hi
	$FFFF8203 (byte) : VDL_VBM - Video Base Mi
	$FFFF8205 (byte) : VDL_VCH - Video Count Hi
	$FFFF8207 (byte) : VDL_VCM - Video Count Mi
	$FFFF8209 (byte) : VDL_VCL - Video Count Lo
	$FFFF820A (byte) : VDL_SYM - Sync mode
	$FFFF820D (byte) : VDL_VBL - Video Base Lo
	$FFFF820E (word) : VDL_LOF - Offset to next line
	$FFFF8210 (word) : VDL_LWD - Line Wide in Words
	
	$FFFF8240 (word) : VDL_STC - ST Palette Register 00
	.........
	$FFFF825E (word) : VDL_STC - ST Palette Register 15

	$FFFF8260 (byte) : ST shift mode
	$FFFF8265 (byte) : Horizontal scroll register
	$FFFF8266 (word) : Falcon shift mode
	
	$FFFF8280 (word) : HHC - Horizontal Hold Counter
	$FFFF8282 (word) : HHT - Horizontal Hold Timer
	$FFFF8284 (word) : HBB - Horizontal Border Begin
	$FFFF8286 (word) : HBE - Horizontal Border End
	$FFFF8288 (word) : HDB - Horizontal Display Begin
	$FFFF828A (word) : HDE - Horizontal Display End
	$FFFF828C (word) : HSS - Horizontal SS
	$FFFF828E (word) : HFS - Horizontal FS
	$FFFF8290 (word) : HEE - Horizontal EE
	
	$FFFF82A0 (word) : VFC - Vertical Frequency Counter
	$FFFF82A2 (word) : VFT - Vertical Frequency Timer
	$FFFF82A4 (word) : VBB - Vertical Border Begin
	$FFFF82A6 (word) : VBE - Vertical Border End
	$FFFF82A8 (word) : VDB - Vertical Display Begin
	$FFFF82AA (word) : VDE - Vertical Display End
	$FFFF82AC (word) : VSS - Vertical SS
	
	$FFFF82C0 (word) : VCO - Video control
	$FFFF82C2 (word) : VMD - Video mode

	$FFFF9800 (long) : VDL_PAL - Videl palette Register 000
	...........
	$FFFF98FC (long) : VDL_PAL - Videl palette Register 255
*/

const char VIDEL_fileid[] = "Hatari videl.c : " __DATE__ " " __TIME__;

#include <SDL_endian.h>
#include <SDL.h>
#include "main.h"
#include "configuration.h"
#include "memorySnapShot.h"
#include "ioMem.h"
#include "log.h"
#include "hostscreen.h"
#include "screen.h"
#include "stMemory.h"
#include "video.h"
#include "videl.h"


#define handleRead(a) IoMem_ReadByte(a)
#define handleReadW(a) IoMem_ReadWord(a)
#define Atari2HostAddr(a) (&STRam[a])

#define VIDEL_DEBUG 0

#if VIDEL_DEBUG
#define Dprintf(a) printf a
#else
#define Dprintf(a)
#endif

#define HW	0xff8200
#define VIDEL_COLOR_REGS_BEGIN	0xff9800

struct videl_s {
	bool   bUseSTShifter;			/* whether to use ST or Falcon palette */
	Uint8  reg_ffff8006_save;		/* save reg_ffff8006 as it's a read only register */
	Uint8  monitor_type;			/* 00 Monochrome (SM124) / 01 Color (SC1224) / 10 VGA Color / 11 Television ($FFFF8006) */
};

static struct videl_s videl;

/* TODO: put these to some struct so that it's easier to see
 * they're VIDEL global
 */
static int width, height, bpp;
static bool hostColorsSync;

Uint16 vfc_counter;			/* counter for VFC register $ff82a0 (to be internalized when VIDEL emulation is complete) */

/* Autozoom */
static int zoomwidth, prev_scrwidth;
static int zoomheight, prev_scrheight;
static int *zoomxtable;
static int *zoomytable;



static void VIDEL_renderScreenNoZoom(void);
static void VIDEL_renderScreenZoom(void);

       
// Called upon startup and when CPU encounters a RESET instruction.
void VIDEL_reset(void)
{
	videl.bUseSTShifter = false;				/* Use Falcon color palette by default */
	videl.reg_ffff8006_save = IoMem_ReadByte(0xff8006);
	videl.monitor_type  = videl.reg_ffff8006_save & 0xc0;
	
	hostColorsSync = false; 

	vfc_counter = 0;
	
	/* Autozoom */
	zoomwidth=prev_scrwidth=0;
	zoomheight=prev_scrheight=0;
	zoomxtable=NULL;
	zoomytable=NULL;

	/* Default resolution to boot with */
	width = 640;
	height = 480;
	HostScreen_setWindowSize(width, height, ConfigureParams.Screen.nForceBpp);

	/* Reset IO register (some are not initialized by TOS) */
	IoMem_WriteWord(0xff820e, 0);    /* Line offset */
	IoMem_WriteWord(0xff8264, 0);    /* Horizontal scroll */
}

/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void VIDEL_MemorySnapShot_Capture(bool bSave)
{
	/* Save/Restore details */
	MemorySnapShot_Store(&videl, sizeof(videl));
	MemorySnapShot_Store(&vfc_counter, sizeof(vfc_counter));
}

/**
 * Monitor write access to ST/E color palette registers
 */
void VIDEL_ColorRegsWrite(void)
{
	hostColorsSync = false;
}

/**
 * VIDEL_Monitor_WriteByte : Contains memory and monitor configuration. 
 *                           This register is  read only.
 */
void VIDEL_Monitor_WriteByte(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff8006 Monitor and memory conf write (Read only)\n");
	/* Restore hardware value */
	IoMem_WriteByte(0xff8006, videl.reg_ffff8006_save);
}

/**
 * Write to video address base high, med and low register (0xff8201/03/0d).
 * On Falcon, when a program writes to high or med registers, base low register
 * is reset to zero.
 */
void VIDEL_ScreenBase_WriteByte(void)
{
	Uint32 screenBase;
	
	if ((IoAccessCurrentAddress == 0xff8201) || (IoAccessCurrentAddress == 0xff8203)) {
		/* Reset screen base low register */
		IoMem[0xff820d] = 0;
	}
	
	screenBase = (IoMem[0xff8201]<<16)+(IoMem[0xff8203]<<8)+IoMem[0xff820d];

	LOG_TRACE(TRACE_VIDEL, "Videl : $%04x Screen base write: 0x%01x\t (screen: 0x%04x)\n", 
						IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress], screenBase);
}

/**
    VIDEL_ST_ShiftModeWriteByte : 
	$FFFF8260 [R/W] B  ______10  ST Shift Mode
	                         ||
	                         ||                           others   vga
	                         ||                  $FF8210 $FF82C2 $FF82C2
	                         00--4BP/320 Pixels=> $0050   $0000   $0005
	                         01--2BP/640 Pixels=> $0050   $0004   $0009
	                         10--1BP/640 Pixels=> $0028   $0006   $0008
	                         11--???/320 Pixels=> $0050   $0000   $0000

	Writing to this register does the following things:
		- activate STE palette
		- sets line width ($ffff8210)
		- sets video mode in $ffff82c2 (double lines/interlace & cycles/pixel)
 */
void VIDEL_ST_ShiftModeWriteByte(void)
{
	Uint16 line_width, video_mode;
	Uint8 st_shiftMode;

	st_shiftMode = IoMem_ReadByte(0xff8260);
	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff8260 ST Shift Mode (STSHIFT) write: 0x%x\n", st_shiftMode);

	/* Activate STE palette */
	videl.bUseSTShifter = true;

	/*  Compute line width and video mode */
	switch (st_shiftMode & 0x3) {
		case 0:	/* 4BP/320 Pixels */
			line_width = 0x50;
			/* half pixels + double lines vs. no scaling */
			video_mode = videl.monitor_type == FALCON_MONITOR_VGA ? 0x5 : 0x0;
			break;
		case 1:	/* 2BP/640 Pixels */
			line_width = 0x50;
			/* quarter pixels + double lines vs. half pixels */
			video_mode = videl.monitor_type == FALCON_MONITOR_VGA ? 0x9 : 0x4;
			break;
		case 2:	/* 1BP/640 Pixels */
			line_width = 0x28;
			if (videl.monitor_type == FALCON_MONITOR_MONO) {
				video_mode = 0x0;
				break;
			}
			/* quarter pixels vs. half pixels + interlace */
			video_mode = videl.monitor_type == FALCON_MONITOR_VGA ? 0x8 : 0x6;
			break;
		case 3:	/* ???/320 Pixels */
			line_width = 0x50;
			video_mode = 0x0;
			break;
	}

	/* Set line width ($FFFF8210) */
	IoMem_WriteWord(0xff8210, line_width); 
	
	/* Set video mode ($FFFF82C2) */
	IoMem_WriteWord(0xff82c2, video_mode); 
}

/**
    VIDEL_FALCON_ShiftModeWriteWord :
	$FFFF8266 [R/W] W  _____A98_6543210  Falcon Shift Mode (SPSHIFT)
	                        ||| |||||||
	                        ||| |||++++- 0..15: Colourbank setting in 8BP
	                        ||| ||+----- 0: 8 Bitplanes (256 Colors) off
	                        ||| ||+----- 1: 8 Bitplanes (256 Colors) on
	                        ||| |+------ 0: internal Vertical Sync
	                        ||| |        1: external Vertical Sync
	                        ||| +------- 0: internal Horizontal Sync
	                        |||          1: external Horizontal Sync
	                        ||+--------- 0: True-Color-Mode off
	                        ||           1: True-Color-Mode on
	                        |+---------- 0: Overlay-Mode off
	                        |            1: Overlay-Mode on
	                        +----------- 0: 2-Color-Mode off
	                                     1: 2-Color-Mode on

	Writing to this register does the following things:
		- activate Falcon palette
		- if you set Bits A/8/4 == 0, it selects 16-Color-Falcon-Mode (NOT the
		  same as ST LOW since Falcon palette is used!)
		- $8260 register is ignored, you don't need to write here anything

	Note: 4-Color-Mode isn't realisable with Falcon palette.
 */
void VIDEL_FALC_ShiftModeWriteWord(void)
{
	Uint16 falc_shiftMode = IoMem_ReadWord(0xff8266);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff8266 Falcon Shift Mode (SPSHIFT) write: 0x%02x\n", falc_shiftMode);

	videl.bUseSTShifter = false;
}

/**
 *  Write Horizontal Hold Counter (HHC)
 */
void VIDEL_HHC_WriteWord(void)
{
	Uint16 hhc = IoMem_ReadWord(0xff8280);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff8280 Horizontal Hold Counter (HHC) write: 0x%02x\n", hhc);
}

/**
 *  Write Horizontal Hold Timer (HHT)
 */
void VIDEL_HHT_WriteWord(void)
{
	Uint16 hht = IoMem_ReadWord(0xff8282);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff8282 Horizontal Hold Timer (HHT) write: 0x%02x\n", hht);
}

/**
 *  Write Horizontal Border Begin (HBB)
 */
void VIDEL_HBB_WriteWord(void)
{
	Uint16 hbb = IoMem_ReadWord(0xff8284);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff8284 Horizontal Border Begin (HBB) write: 0x%02x\n", hbb);
}

/**
 *  Write Horizontal Border End (HBE)
 */
void VIDEL_HBE_WriteWord(void)
{
	Uint16 hbe = IoMem_ReadWord(0xff8286);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff8286 Horizontal Border End (HBE) write: 0x%02x\n", hbe);
}

/**
 *  Write Horizontal Display Begin (HDB)
 */
void VIDEL_HDB_WriteWord(void)
{
	Uint16 hdb = IoMem_ReadWord(0xff8288);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff8288 Horizontal Display Begin (HDB) write: 0x%02x\n", hdb);
}

/**
 *  Write Horizontal Display End (HDE)
 */
void VIDEL_HDE_WriteWord(void)
{
	Uint16 hde = IoMem_ReadWord(0xff828a);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff828a Horizontal Display End (HDE) write: 0x%02x\n", hde);
}

/**
 *  Write Horizontal SS (HSS)
 */
void VIDEL_HSS_WriteWord(void)
{
	Uint16 hss = IoMem_ReadWord(0xff828c);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff828c Horizontal SS (HSS) write: 0x%02x\n", hss);
}

/**
 *  Write Horizontal FS (HFS)
 */
void VIDEL_HFS_WriteWord(void)
{
	Uint16 hfs = IoMem_ReadWord(0xff828e);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff828e Horizontal FS (HFS) write: 0x%02x\n", hfs);
}

/**
 *  Write Horizontal EE (HEE)
 */
void VIDEL_HEE_WriteWord(void)
{
	Uint16 hee = IoMem_ReadWord(0xff8290);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff8290 Horizontal EE (HEE) write: 0x%02x\n", hee);
}

/**
 *  Write Vertical Frequency Counter (VFC)
 */
void VIDEL_VFC_ReadWord(void)
{
	IoMem_WriteWord(0xff82a0, vfc_counter);
	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff82a0 Vertical Frequency Counter (VFC) read: 0x%02x\n", vfc_counter);
}

/**
 *  Write Vertical Frequency Timer (VFT)
 */
void VIDEL_VFT_WriteWord(void)
{
	Uint16 vft = IoMem_ReadWord(0xff82a2);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff82a2 Vertical Frequency Timer (VFT) write: 0x%02x\n", vft);
}

/**
 *  Write Vertical Border Begin (VBB)
 */
void VIDEL_VBB_WriteWord(void)
{
	Uint16 vbb = IoMem_ReadWord(0xff82a4);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff82a4 Vertical Border Begin (VBB) write: 0x%02x\n", vbb);
}

/**
 *  Write Vertical Border End (VBE)
 */
void VIDEL_VBE_WriteWord(void)
{
	Uint16 vbe = IoMem_ReadWord(0xff82a6);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff82a6 Vertical Border End (VBE) write: 0x%02x\n", vbe);
}

/**
 *  Write Vertical Display Begin (VDB)
 */
void VIDEL_VDB_WriteWord(void)
{
	Uint16 vdb = IoMem_ReadWord(0xff82a8);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff82a8 Vertical Display Begin (VDB) write: 0x%02x\n", vdb);
}

/**
 *  Write Vertical Display End (VDE)
 */
void VIDEL_VDE_WriteWord(void)
{
	Uint16 vde = IoMem_ReadWord(0xff82aa);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff82aa Vertical Display End (VDE) write: 0x%02x\n", vde);
}

/**
 *  Write Vertical SS (VSS)
 */
void VIDEL_VSS_WriteWord(void)
{
	Uint16 vss = IoMem_ReadWord(0xff82ac);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff82ac Vertical SS (VSS) write: 0x%02x\n", vss);
}

/**
 *  Write Video Control (VCO)
 */
void VIDEL_VCO_WriteWord(void)
{
	Uint16 vco = IoMem_ReadWord(0xff82c0);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff82c0 Video control (VCO) write: 0x%02x\n", vco);
}

/**
 *  Write Video Mode (VDM)
 */
void VIDEL_VMD_WriteWord(void)
{
	Uint16 vdm = IoMem_ReadWord(0xff82c2);

	LOG_TRACE(TRACE_VIDEL, "Videl : $ffff82c2 Video Mode (VDM) write: 0x%02x\n", vdm);
}


static long VIDEL_getVideoramAddress(void)
{
	return (handleRead(HW + 1) << 16) | (handleRead(HW + 3) << 8) | handleRead(HW + 0x0d);
}

static int VIDEL_getScreenBpp(void)
{
	int f_shift = handleReadW(HW + 0x66);
	int st_shift = handleRead(HW + 0x60);
	/* to get bpp, we must examine f_shift and st_shift.
	 * f_shift is valid if any of bits no. 10, 8 or 4
	 * is set. Priority in f_shift is: 10 ">" 8 ">" 4, i.e.
	 * if bit 10 set then bit 8 and bit 4 don't care...
	 * If all these bits are 0 and ST shifter is written
	 * after Falcon one, get display depth from st_shift
	 * (as for ST and STE)
	 */
	int bits_per_pixel;
	if (f_shift & 0x400)		/* Falcon: 2 colors */
		bits_per_pixel = 1;
	else if (f_shift & 0x100)	/* Falcon: hicolor */
		bits_per_pixel = 16;
	else if (f_shift & 0x010)	/* Falcon: 8 bitplanes */
		bits_per_pixel = 8;
	else if (!videl.bUseSTShifter)	/* Falcon: 4 bitplanes */
		bits_per_pixel = 4;
	else if (st_shift == 0)
		bits_per_pixel = 4;
	else if (st_shift == 0x01)
		bits_per_pixel = 2;
	else /* if (st_shift == 0x02) */
		bits_per_pixel = 1;

	// Dprintf(("Videl works in %d bpp, f_shift=%04x, st_shift=%d", bits_per_pixel, f_shift, st_shift));

	return bits_per_pixel;
}

static int VIDEL_getScreenWidth(void)
{
	return (handleReadW(HW + 0x10) & 0x03ff) * 16 / VIDEL_getScreenBpp();
}

static int VIDEL_getScreenHeight(void)
{
	int vdb = handleReadW(HW + 0xa8) & 0x07ff;
	int vde = handleReadW(HW + 0xaa) & 0x07ff;
	int vmode = handleReadW(HW + 0xc2);

	/* visible y resolution:
	 * Graphics display starts at line VDB and ends at line
	 * VDE. If interlace mode off unit of VC-registers is
	 * half lines, else lines.
	 */
	int yres = vde - vdb;
	if (!(vmode & 0x02))		// interlace
		yres >>= 1;
	if (vmode & 0x01)		// double
		yres >>= 1;

	return yres;
}

#if 0
/* this is easier & more robustly done in hostscreen.c just by
 * comparing requested screen width & height to each other.
 */
static void VIDEL_getMonitorScale(int *sx, int *sy)
{
	/* Videl video mode register bits and resulting desktop resolution:
	 * 
	 * quarter, half, interlace, double:   pixels: -> zoom:
	 * rgb:
	 *    0       0       0         0      320x200 -> 2 x 2
	 *    0       0       1         0      320x400 -> 2 x 1
	 *    0       1       0         0      640x200 -> 1 x 2 !
	 *    0       1       1         0      640x400 -> 1 x 1
	 * vga:
	 *    0       0       0         1      (just double ?)
	 *    0       0       1         1      (double & interlace ???)
	 *    0       1       0         0      320x480 -> 2 x 1 !
	 *    0       1       0         1      320x240 -> 2 x 2
	 *    0       1       1         1      (double + interlace ???)
	 *    1       0       0         0      640x480 -> 1 x 1
	 *    1       0       0         1      640x240 -> 1 x 2
	 */
	int vmode = handleReadW(HW + 0xc2);

	/* half pixel seems to have opposite meaning on
	 * VGA and RGB monitor, so they need to handled separately
	 */
	if (videl.monitor_type) == FALCON_MONITOR_VGA) {
		if (vmode & 0x08) {  // quarter pixel
			*sx = 1;
		} else {
			*sx = 2;
		}
		if (vmode & 0x01) {  // double line
			*sy = 2;
		} else {
			*sy = 1;
		}
	} else {
		if (vmode & 0x04) {  // half pixel
			*sx = 1;
		} else {
			*sx = 2;
		}
		if (vmode & 0x02) {  // interlace used only on RGB?
			*sy = 1;
		} else {
			*sy = 2;
		}
	}
}
#endif


/** map the correct colortable into the correct pixel format
 */
static void VIDEL_updateColors(void)
{
	//Dprintf(("ColorUpdate in progress\n"));

	int i, r, g, b, colors = 1 << bpp;

#define F_COLORS(i) handleRead(VIDEL_COLOR_REGS_BEGIN + (i))
#define STE_COLORS(i)	handleRead(0xff8240 + (i))

	if (!videl.bUseSTShifter) {
		for (i = 0; i < colors; i++) {
			int offset = i << 2;
			r = F_COLORS(offset) & 0xfc;
			r |= r>>6;
			g = F_COLORS(offset + 1) & 0xfc;
			g |= g>>6;
			b = F_COLORS(offset + 3) & 0xfc;
			b |= b>>6;
			HostScreen_setPaletteColor(i, r,g,b);
		}
		HostScreen_updatePalette(colors);
	} else {
		for (i = 0; i < colors; i++) {
			int offset = i << 1;
			r = STE_COLORS(offset) & 0x0f;
			r = ((r & 7)<<1)|(r>>3);
			r |= r<<4;
			g = (STE_COLORS(offset + 1)>>4) & 0x0f;
			g = ((g & 7)<<1)|(g>>3);
			g |= g<<4;
			b = STE_COLORS(offset + 1) & 0x0f;
			b = ((b & 7)<<1)|(b>>3);
			b |= b<<4;
			HostScreen_setPaletteColor(i, r,g,b);
		}
		HostScreen_updatePalette(colors);
	}

	hostColorsSync = true;
}


void VIDEL_ZoomModeChanged(void)
{
	/* User selected another zoom mode, so set a new screen resolution now */
	HostScreen_setWindowSize(width, height, bpp == 16 ? 16 : ConfigureParams.Screen.nForceBpp);
}


bool VIDEL_renderScreen(void)
{
	int vw	 = VIDEL_getScreenWidth();
	int vh	 = VIDEL_getScreenHeight();
	int vbpp = VIDEL_getScreenBpp();
	bool change = false;

	if (vw > 0 && vw != width) {
		Dprintf(("CH width %d\n", width));
		width = vw;
		change = true;
	}
	if (vh > 0 && vh != height) {
		Dprintf(("CH height %d\n", width));
		height = vh;
		change = true;
	}
	if (vbpp != bpp) {
		Dprintf(("CH bpp %d\n", vbpp));
		bpp = vbpp;
		change = true;
	}
	if (change) {
		LOG_TRACE(TRACE_VIDEL, "Videl : video mode change to %dx%d@%d\n", width, height, bpp);
		HostScreen_setWindowSize(width, height, bpp == 16 ? 16 : ConfigureParams.Screen.nForceBpp);
	}

	if (!HostScreen_renderBegin())
		return false;

	if (nScreenZoomX * nScreenZoomY != 1) {
		VIDEL_renderScreenZoom();
	} else {
		VIDEL_renderScreenNoZoom();
	}

	HostScreen_renderEnd();

	HostScreen_update1(false);

	return true;
}


/**
 * Performs conversion from the TOS's bitplane word order (big endian) data
 * into the native chunky color index.
 */
static void Videl_bitplaneToChunky(Uint16 *atariBitplaneData, Uint16 bpp,
                                   Uint8 colorValues[16])
{
	Uint32 a, b, c, d, x;

	/* Obviously the different cases can be broken out in various
	 * ways to lessen the amount of work needed for <8 bit modes.
	 * It's doubtful if the usage of those modes warrants it, though.
	 * The branches below should be ~100% correctly predicted and
	 * thus be more or less for free.
	 * Getting the palette values inline does not seem to help
	 * enough to worry about. The palette lookup is much slower than
	 * this code, though, so it would be nice to do something about it.
	 */
	if (bpp >= 4) {
		d = *(Uint32 *)&atariBitplaneData[0];
		c = *(Uint32 *)&atariBitplaneData[2];
		if (bpp == 4) {
			a = b = 0;
		} else {
			b = *(Uint32 *)&atariBitplaneData[4];
			a = *(Uint32 *)&atariBitplaneData[6];
		}
	} else {
		a = b = c = 0;
		if (bpp == 2) {
			d = *(Uint32 *)&atariBitplaneData[0];
		} else {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			d = atariBitplaneData[0]<<16;
#else
			d = atariBitplaneData[0];
#endif
		}
	}

	x = a;
	a =  (a & 0xf0f0f0f0)       | ((c & 0xf0f0f0f0) >> 4);
	c = ((x & 0x0f0f0f0f) << 4) |  (c & 0x0f0f0f0f);
	x = b;
	b =  (b & 0xf0f0f0f0)       | ((d & 0xf0f0f0f0) >> 4);
	d = ((x & 0x0f0f0f0f) << 4) |  (d & 0x0f0f0f0f);

	x = a;
	a =  (a & 0xcccccccc)       | ((b & 0xcccccccc) >> 2);
	b = ((x & 0x33333333) << 2) |  (b & 0x33333333);
	x = c;
	c =  (c & 0xcccccccc)       | ((d & 0xcccccccc) >> 2);
	d = ((x & 0x33333333) << 2) |  (d & 0x33333333);

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	a = (a & 0x5555aaaa) | ((a & 0x00005555) << 17) | ((a & 0xaaaa0000) >> 17);
	b = (b & 0x5555aaaa) | ((b & 0x00005555) << 17) | ((b & 0xaaaa0000) >> 17);
	c = (c & 0x5555aaaa) | ((c & 0x00005555) << 17) | ((c & 0xaaaa0000) >> 17);
	d = (d & 0x5555aaaa) | ((d & 0x00005555) << 17) | ((d & 0xaaaa0000) >> 17);

	colorValues[ 8] = a;
	a >>= 8;
	colorValues[ 0] = a;
	a >>= 8;
	colorValues[ 9] = a;
	a >>= 8;
	colorValues[ 1] = a;

	colorValues[10] = b;
	b >>= 8;
	colorValues[ 2] = b;
	b >>= 8;
	colorValues[11] = b;
	b >>= 8;
	colorValues[ 3] = b;

	colorValues[12] = c;
	c >>= 8;
	colorValues[ 4] = c;
	c >>= 8;
	colorValues[13] = c;
	c >>= 8;
	colorValues[ 5] = c;

	colorValues[14] = d;
	d >>= 8;
	colorValues[ 6] = d;
	d >>= 8;
	colorValues[15] = d;
	d >>= 8;
	colorValues[ 7] = d;
#else
	a = (a & 0xaaaa5555) | ((a & 0x0000aaaa) << 15) | ((a & 0x55550000) >> 15);
	b = (b & 0xaaaa5555) | ((b & 0x0000aaaa) << 15) | ((b & 0x55550000) >> 15);
	c = (c & 0xaaaa5555) | ((c & 0x0000aaaa) << 15) | ((c & 0x55550000) >> 15);
	d = (d & 0xaaaa5555) | ((d & 0x0000aaaa) << 15) | ((d & 0x55550000) >> 15);

	colorValues[ 1] = a;
	a >>= 8;
	colorValues[ 9] = a;
	a >>= 8;
	colorValues[ 0] = a;
	a >>= 8;
	colorValues[ 8] = a;

	colorValues[ 3] = b;
	b >>= 8;
	colorValues[11] = b;
	b >>= 8;
	colorValues[ 2] = b;
	b >>= 8;
	colorValues[10] = b;

	colorValues[ 5] = c;
	c >>= 8;
	colorValues[13] = c;
	c >>= 8;
	colorValues[ 4] = c;
	c >>= 8;
	colorValues[12] = c;

	colorValues[ 7] = d;
	d >>= 8;
	colorValues[15] = d;
	d >>= 8;
	colorValues[ 6] = d;
	d >>= 8;
	colorValues[14] = d;
#endif
}


static void VIDEL_renderScreenNoZoom(void)
{
	int vw	 = VIDEL_getScreenWidth();
	int vh	 = VIDEL_getScreenHeight();

	int lineoffset = handleReadW(HW + 0x0e) & 0x01ff; // 9 bits
	int linewidth = handleReadW(HW + 0x10) & 0x03ff; // 10 bits
	/* 
	   I think this implementation is naive: 
	   indeed, I suspect that we should instead skip lineoffset
	   words each time we have read "more" than linewidth words
	   (possibly "more" because of the number of bit planes).
	   Moreover, the 1 bit plane mode is particular;
	   while doing some experiments on my Falcon, it seems to
	   behave like the 4 bit planes mode.
	   At last, we have also to take into account the 4 bits register
	   located at the word $ffff8264 (bit offset). This register makes
	   the semantics of the lineoffset register change a little.
	   int bitoffset = handleReadW(HW + 0x64) & 0x000f;
	   The meaning of this register in True Color mode is not clear
	   for me at the moment (and my experiments on the Falcon don't help
	   me).
	*/
	int nextline = linewidth + lineoffset;

	if (bpp < 16 && !hostColorsSync) {
		VIDEL_updateColors();
	}

	VIDEL_ConvertScreenNoZoom(vw, vh, bpp, nextline);
}


void VIDEL_ConvertScreenNoZoom(int vw, int vh, int vbpp, int nextline)
{
	int scrpitch = HostScreen_getPitch();

	long atariVideoRAM = VIDEL_getVideoramAddress();

	Uint16 *fvram = (Uint16 *) Atari2HostAddr(atariVideoRAM);
	Uint8 *hvram = HostScreen_getVideoramAddress();
	SDL_PixelFormat *scrfmt = HostScreen_getFormat();

	int hscrolloffset = (handleRead(HW + 0x65) & 0x0f);

	/* Clip to SDL_Surface dimensions */
	int scrwidth = HostScreen_getWidth();
	int scrheight = HostScreen_getHeight();
	int vw_clip = vw;
	int vh_clip = vh;
	if (vw>scrwidth) vw_clip = scrwidth;
	if (vh>scrheight) vh_clip = scrheight;	

	/* Horizontal scroll register set? */
	if (hscrolloffset) {
		/* Yes, so we need to adjust offset to next line: */
		nextline += vbpp;
	}

	/* Center screen */
	hvram += ((scrheight-vh_clip)>>1)*scrpitch;
	hvram += ((scrwidth-vw_clip)>>1)*HostScreen_getBpp();

	/* Render */
	if (vbpp < 16) {
		/* Bitplanes modes */

		// The SDL colors blitting...
		Uint8 color[16];

		// FIXME: The byte swap could be done here by enrolling the loop into 2 each by 8 pixels
		switch ( HostScreen_getBpp() ) {
			case 1:
				{
					Uint16 *fvram_line = fvram;
					Uint8 *hvram_line = hvram;
					int h;

					for (h = 0; h < vh_clip; h++) {
						Uint16 *fvram_column = fvram_line;
						Uint8 *hvram_column = hvram_line;
						int w;

						/* First 16 pixels: */
						Videl_bitplaneToChunky(fvram_column, vbpp, color);
						memcpy(hvram_column, color+hscrolloffset, 16-hscrolloffset);
						hvram_column += 16-hscrolloffset;
						fvram_column += vbpp;
						/* Now the main part of the line: */
						for (w = 1; w < (vw_clip+15)>>4; w++) {
							Videl_bitplaneToChunky( fvram_column, vbpp, color );
							memcpy(hvram_column, color, 16);
							hvram_column += 16;
							fvram_column += vbpp;
						}
						/* Last pixels of the line for fine scrolling: */
						if (hscrolloffset) {
							Videl_bitplaneToChunky(fvram_column, vbpp, color);
							memcpy(hvram_column, color, hscrolloffset);
						}

						hvram_line += scrpitch;
						fvram_line += nextline;
					}
				}
				break;
			case 2:
				{
					Uint16 *fvram_line = fvram;
					Uint16 *hvram_line = (Uint16 *)hvram;
					int h;

					for (h = 0; h < vh_clip; h++) {
						Uint16 *fvram_column = fvram_line;
						Uint16 *hvram_column = hvram_line;
						int w, j;

						/* First 16 pixels: */
						Videl_bitplaneToChunky(fvram_column, vbpp, color);
						for (j = 0; j < 16 - hscrolloffset; j++) {
							*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
						}
						fvram_column += vbpp;
						/* Now the main part of the line: */
						for (w = 1; w < (vw_clip+15)>>4; w++) {
							Videl_bitplaneToChunky( fvram_column, vbpp, color );
							for (j=0; j<16; j++) {
								*hvram_column++ = HostScreen_getPaletteColor( color[j] );
							}
							fvram_column += vbpp;
						}
						/* Last pixels of the line for fine scrolling: */
						if (hscrolloffset) {
							Videl_bitplaneToChunky(fvram_column, vbpp, color);
							for (j = 0; j < hscrolloffset; j++) {
								*hvram_column++ = HostScreen_getPaletteColor(color[j]);
							}
						}

						hvram_line += scrpitch>>1;
						fvram_line += nextline;
					}
				}
				break;
			case 4:
				{
					Uint16 *fvram_line = fvram;
					Uint32 *hvram_line = (Uint32 *)hvram;
					int h;

					for (h = 0; h < vh_clip; h++) {
						Uint16 *fvram_column = fvram_line;
						Uint32 *hvram_column = hvram_line;
						int w, j;

						/* First 16 pixels: */
						Videl_bitplaneToChunky(fvram_column, vbpp, color);
						for (j = 0; j < 16 - hscrolloffset; j++) {
							*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
						}
						fvram_column += vbpp;
						/* Now the main part of the line: */
						for (w = 1; w < (vw_clip+15)>>4; w++) {
							Videl_bitplaneToChunky( fvram_column, vbpp, color );
							for (j=0; j<16; j++) {
								*hvram_column++ = HostScreen_getPaletteColor( color[j] );
							}
							fvram_column += vbpp;
						}
						/* Last pixels of the line for fine scrolling: */
						if (hscrolloffset) {
							Videl_bitplaneToChunky(fvram_column, vbpp, color);
							for (j = 0; j < hscrolloffset; j++) {
								*hvram_column++ = HostScreen_getPaletteColor(color[j]);
							}
						}

						hvram_line += scrpitch>>2;
						fvram_line += nextline;
					}
				}
				break;
		}

	} else {

		// Falcon TC (High Color)
		switch ( HostScreen_getBpp() )  {
			case 1:
				{
					/* FIXME: when Videl switches to 16bpp, set the palette to 3:3:2 */
					Uint16 *fvram_line = fvram;
					Uint8 *hvram_line = hvram;
					int h;

					for (h = 0; h < vh_clip; h++) {
						Uint16 *fvram_column = fvram_line;
						Uint8 *hvram_column = hvram_line;
						int w, tmp;

						for (w = 0; w < vw_clip; w++) {
							
							tmp = SDL_SwapBE16(*fvram_column);

							*hvram_column = ((tmp>>13) & 7) << 5;
							*hvram_column |= ((tmp>>8) & 7) << 2;
							*hvram_column |= ((tmp>>2) & 3);

							hvram_column++;
							fvram_column++;
						}

						hvram_line += scrpitch;
						fvram_line += nextline;
					}
				}
				break;
			case 2:
				{
					Uint16 *fvram_line = fvram;
					Uint16 *hvram_line = (Uint16 *)hvram;
					int h;

					for (h = 0; h < vh_clip; h++) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
						//FIXME: here might be a runtime little/big video endian switch like:
						//      if ( /* videocard memory in Motorola endian format */ false)
						memcpy(hvram_line, fvram_line, vw_clip<<1);
#else
						int w;
						Uint16 *fvram_column = fvram_line;
						Uint16 *hvram_column = hvram_line;

						for (w = 0; w < vw_clip; w++) {
							// byteswap with SDL asm macros
							*hvram_column++ = SDL_SwapBE16(*fvram_column++);
						}
#endif // SDL_BYTEORDER == SDL_BIG_ENDIAN

						hvram_line += scrpitch>>1;
						fvram_line += nextline;
					}
				}
				break;
			case 4:
				{
					Uint16 *fvram_line = fvram;
					Uint32 *hvram_line = (Uint32 *)hvram;
					int h;

					for (h = 0; h < vh_clip; h++) {
						Uint16 *fvram_column = fvram_line;
						Uint32 *hvram_column = hvram_line;
						int w;

						for (w = 0; w < vw_clip; w++) {
							Uint16 srcword = *fvram_column++;

							*hvram_column++ =
								SDL_MapRGB(scrfmt,
									   (srcword & 0xf8),
									   (((srcword & 0x07) << 5) |
										   ((srcword >> 11) & 0x3c)),
									   ((srcword >> 5) & 0xf8));
						}

						hvram_line += scrpitch>>2;
						fvram_line += nextline;
					}
				}
				break;
		}
	}
}


static void VIDEL_renderScreenZoom(void)
{
	/* Atari screen infos */
	int vw	 = VIDEL_getScreenWidth();
	int vh	 = VIDEL_getScreenHeight();

	int lineoffset = handleReadW(HW + 0x0e) & 0x01ff; // 9 bits
	int linewidth = handleReadW(HW + 0x10) & 0x03ff; // 10 bits
	/* same remark as before: too naive */
	int nextline = linewidth + lineoffset;

	if ((vw<32) || (vh<32))  return;

	if (bpp<16 && !hostColorsSync) {
		VIDEL_updateColors();
	}

	VIDEL_ConvertScreenZoom(vw, vh, bpp, nextline);
}


void VIDEL_ConvertScreenZoom(int vw, int vh, int vbpp, int nextline)
{
	int i, j, w, h, cursrcline;

	Uint16 *fvram = (Uint16 *) Atari2HostAddr(VIDEL_getVideoramAddress());

	/* Host screen infos */
	int scrpitch = HostScreen_getPitch();
	int scrwidth = HostScreen_getWidth();
	int scrheight = HostScreen_getHeight();
	int scrbpp = HostScreen_getBpp();
	SDL_PixelFormat *scrfmt = HostScreen_getFormat();
	Uint8 *hvram = (Uint8 *) HostScreen_getVideoramAddress();

	int hscrolloffset = (handleRead(HW + 0x65) & 0x0f);

	/* Horizontal scroll register set? */
	if (hscrolloffset) {
		/* Yes, so we need to adjust offset to next line: */
		nextline += vbpp;
	}

	/* Integer zoom coef ? */
	if (/*(bx_options.autozoom.integercoefs) &&*/ (scrwidth>=vw) && (scrheight>=vh)) {
		int coefx = scrwidth/vw;
		int coefy = scrheight/vh;

		scrwidth = vw * coefx;
		scrheight = vh * coefy;

		/* Center screen */
		hvram += ((HostScreen_getHeight()-scrheight)>>1)*scrpitch;
		hvram += ((HostScreen_getWidth()-scrwidth)>>1)*scrbpp;
	}

	/* New zoom ? */
	if ((zoomwidth != vw) || (scrwidth != prev_scrwidth)) {
		if (zoomxtable) {
			free(zoomxtable);
		}
		zoomxtable = malloc(sizeof(int)*scrwidth);
		for (i=0; i<scrwidth; i++) {
			zoomxtable[i] = (vw*i)/scrwidth;
		}
		zoomwidth = vw;
		prev_scrwidth = scrwidth;
	}
	if ((zoomheight != vh) || (scrheight != prev_scrheight)) {
		if (zoomytable) {
			free(zoomytable);
		}
		zoomytable = malloc(sizeof(int)*scrheight);
		for (i=0; i<scrheight; i++) {
			zoomytable[i] = (vh*i)/scrheight;
		}
		zoomheight = vh;
		prev_scrheight = scrheight;
	}

	cursrcline = -1;

	if (vbpp<16) {
		Uint8 color[16];

		/* Bitplanes modes */
		switch(scrbpp) {
			case 1:
				{
					/* One complete planar 2 chunky line */
					Uint8 *p2cline = malloc(sizeof(Uint8)*vw);

					Uint16 *fvram_line;
					Uint8 *hvram_line = hvram;

					for (h = 0; h < scrheight; h++) {
						fvram_line = fvram + (zoomytable[h] * nextline);

						/* Recopy the same line ? */
						if (zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-scrpitch, scrwidth*scrbpp);
						} else {
							Uint16 *fvram_column = fvram_line;
							Uint8 *hvram_column = p2cline;

							/* First 16 pixels of a new line */
							Videl_bitplaneToChunky(fvram_column, vbpp, color);
							memcpy(hvram_column, color+hscrolloffset, 16-hscrolloffset);
							hvram_column += 16-hscrolloffset;
							fvram_column += vbpp;
							/* Convert main part of the new line */
							for (w=1; w < (vw+15)>>4; w++) {
								Videl_bitplaneToChunky( fvram_column, vbpp, color );
								memcpy(hvram_column, color, 16);
								hvram_column += 16;
								fvram_column += vbpp;
							}
							/* Last pixels of the line for fine scrolling: */
							if (hscrolloffset) {
								Videl_bitplaneToChunky(fvram_column, vbpp, color);
								memcpy(hvram_column, color, hscrolloffset);
							}

							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = p2cline[zoomxtable[w]];
							}
						}

						hvram_line += scrpitch;
						cursrcline = zoomytable[h];
					}

					free(p2cline);
				}
				break;
			case 2:
				{
					/* One complete planar 2 chunky line */
					Uint16 *p2cline = malloc(sizeof(Uint16)*vw);

					Uint16 *fvram_line;
					Uint16 *hvram_line = (Uint16 *)hvram;

					for (h = 0; h < scrheight; h++) {
						fvram_line = fvram + (zoomytable[h] * nextline);

						/* Recopy the same line ? */
						if (zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>1), scrwidth*scrbpp);
						} else {
							Uint16 *fvram_column = fvram_line;
							Uint16 *hvram_column = p2cline;

							/* First 16 pixels of a new line: */
							Videl_bitplaneToChunky(fvram_column, vbpp, color);
							for (j = 0; j < 16 - hscrolloffset; j++) {
								*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
							}
							fvram_column += vbpp;
							/* Convert the main part of the new line: */
							for (w = 1; w < (vw+15)>>4; w++) {
								Videl_bitplaneToChunky( fvram_column, vbpp, color );
								for (j=0; j<16; j++) {
									*hvram_column++ = HostScreen_getPaletteColor( color[j] );
								}
								fvram_column += vbpp;
							}
							/* Last pixels of the new line for fine scrolling: */
							if (hscrolloffset) {
								Videl_bitplaneToChunky(fvram_column, vbpp, color);
								for (j = 0; j < hscrolloffset; j++) {
									*hvram_column++ = HostScreen_getPaletteColor(color[j]);
								}
							}

							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = p2cline[zoomxtable[w]];
							}
						}

						hvram_line += scrpitch>>1;
						cursrcline = zoomytable[h];
					}

					free(p2cline);
				}
				break;
			case 4:
				{
					/* One complete planar 2 chunky line */
					Uint32 *p2cline = malloc(sizeof(Uint32)*vw);

					Uint16 *fvram_line;
					Uint32 *hvram_line = (Uint32 *)hvram;

					for (h = 0; h < scrheight; h++) {
						fvram_line = fvram + (zoomytable[h] * nextline);

						/* Recopy the same line ? */
						if (zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							Uint16 *fvram_column = fvram_line;
							Uint32 *hvram_column = p2cline;

							/* First 16 pixels of a new line: */
							Videl_bitplaneToChunky(fvram_column, vbpp, color);
							for (j = 0; j < 16 - hscrolloffset; j++) {
								*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
							}
							fvram_column += vbpp;
							/* Convert the main part of the new line: */
							for (w = 1; w < (vw+15)>>4; w++) {
								Videl_bitplaneToChunky( fvram_column, vbpp, color );
								for (j=0; j<16; j++) {
									*hvram_column++ = HostScreen_getPaletteColor( color[j] );
								}
								fvram_column += vbpp;
							}
							/* Last pixels of the new line for fine scrolling: */
							if (hscrolloffset) {
								Videl_bitplaneToChunky(fvram_column, vbpp, color);
								for (j = 0; j < hscrolloffset; j++) {
									*hvram_column++ = HostScreen_getPaletteColor(color[j]);
								}
							}

							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = p2cline[zoomxtable[w]];
							}
						}

						hvram_line += scrpitch>>2;
						cursrcline = zoomytable[h];
					}

					free(p2cline);
				}
				break;
		}
	} else {
		/* Falcon high-color (16-bit) mode */

		switch(scrbpp) {
			case 1:
				{
					/* FIXME: when Videl switches to 16bpp, set the palette to 3:3:2 */
					Uint16 *fvram_line;
					Uint8 *hvram_line = hvram;

					for (h = 0; h < scrheight; h++) {
						Uint16 *fvram_column;
						Uint8 *hvram_column;

						fvram_line = fvram + (zoomytable[h] * nextline);
						fvram_column = fvram_line;
						hvram_column = hvram_line;

						/* Recopy the same line ? */
						if (zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-scrpitch, scrwidth*scrbpp);
						} else {
							for (w = 0; w < scrwidth; w++) {
								Uint16 srcword;
								Uint8 dstbyte;
							
								srcword = SDL_SwapBE16(fvram_column[zoomxtable[w]]);

								dstbyte = ((srcword>>13) & 7) << 5;
								dstbyte |= ((srcword>>8) & 7) << 2;
								dstbyte |= ((srcword>>2) & 3);

								*hvram_column++ = dstbyte;
							}
						}

						hvram_line += scrpitch;
						cursrcline = zoomytable[h];
					}
				}
				break;
			case 2:
				{
					Uint16 *fvram_line;
					Uint16 *hvram_line = (Uint16 *)hvram;

					for (h = 0; h < scrheight; h++) {
						Uint16 *fvram_column;
						Uint16 *hvram_column;

						fvram_line = fvram + (zoomytable[h] * nextline);
						fvram_column = fvram_line;
						hvram_column = hvram_line;

						/* Recopy the same line ? */
						if (zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>1), scrwidth*scrbpp);
						} else {
							for (w = 0; w < scrwidth; w++) {
								Uint16 srcword;
							
								srcword = SDL_SwapBE16(fvram_column[zoomxtable[w]]);
								*hvram_column++ = srcword;
							}
						}

						hvram_line += scrpitch>>1;
						cursrcline = zoomytable[h];
					}
				}
				break;
			case 4:
				{
					Uint16 *fvram_line;
					Uint32 *hvram_line = (Uint32 *)hvram;

					for (h = 0; h < scrheight; h++) {
						Uint16 *fvram_column;
						Uint32 *hvram_column;

						fvram_line = fvram + (zoomytable[h] * nextline);
						fvram_column = fvram_line;
						hvram_column = hvram_line;

						/* Recopy the same line ? */
						if (zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							for (w = 0; w < scrwidth; w++) {
								Uint16 srcword;
							
								srcword = fvram_column[zoomxtable[w]];

								*hvram_column++ =
									SDL_MapRGB(scrfmt,
										   (srcword & 0xf8),
										   (((srcword & 0x07) << 5) |
											   ((srcword >> 11) & 0x3c)),
										   ((srcword >> 5) & 0xf8));
							}
						}

						hvram_line += scrpitch>>2;
						cursrcline = zoomytable[h];
					}
				}
				break;
		}
	}
}
