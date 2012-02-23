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


#define Atari2HostAddr(a) (&STRam[a])

#define HW	0xff8200
#define VIDEL_COLOR_REGS_BEGIN	0xff9800

#define MAX_SCREEN_WIDTH 2000	/* max width of the display */


struct videl_s {
	bool   bUseSTShifter;			/* whether to use ST or Falcon palette */
	Uint8  reg_ffff8006_save;		/* save reg_ffff8006 as it's a read only register */
	Uint8  monitor_type;			/* 00 Monochrome (SM124) / 01 Color (SC1224) / 10 VGA Color / 11 Television ($FFFF8006) */

	Uint16 leftBorderSize;			/* Size of the left border */
	Uint16 rightBorderSize;			/* Size of the right border */
	Uint16 upperBorderSize;			/* Size of the upper border */
	Uint16 lowerBorderSize;			/* Size of the lower border */
	Uint16 XSize;				/* X size of the graphical area */
	Uint16 YSize;				/* Y size of the graphical area */

	Uint16 save_scrWidth;				/* save screen width to detect a change of X resolution */
	Uint16 save_scrHeight;				/* save screen height to detect a change of Y resolution */
	Uint16 save_scrBpp;				/* save screen Bpp to detect a change of bitplan mode */

	bool hostColorsSync;				/* Sync palette with host's */
};

struct videl_zoom_s {
	Uint16 zoomwidth;
	Uint16 prev_scrwidth;
	Uint16 zoomheight;
	Uint16 prev_scrheight;
	int *zoomxtable;
	int *zoomytable;
};

static struct videl_s videl;
static struct videl_zoom_s videl_zoom;

Uint16 vfc_counter;			/* counter for VFC register $ff82a0 (to be internalized when VIDEL emulation is complete) */

static void VIDEL_memset_uint32(Uint32 *addr, Uint32 color, int count);
static void VIDEL_memset_uint16(Uint16 *addr, Uint16 color, int count);
static void VIDEL_memset_uint8(Uint8 *addr, Uint8 color, int count);
       
/**
 *  Called upon startup and when CPU encounters a RESET instruction.
 */
void VIDEL_reset(void)
{
	videl.bUseSTShifter = false;				/* Use Falcon color palette by default */
	videl.reg_ffff8006_save = IoMem_ReadByte(0xff8006);
	videl.monitor_type  = videl.reg_ffff8006_save & 0xc0;
	
	videl.hostColorsSync = false; 

	vfc_counter = 0;
	
	/* Autozoom */
	videl_zoom.zoomwidth = 0;
	videl_zoom.prev_scrwidth = 0;
	videl_zoom.zoomheight = 0;
	videl_zoom.prev_scrheight = 0;
	videl_zoom.zoomxtable = NULL;
	videl_zoom.zoomytable = NULL;

	/* Default resolution to boot with */
	videl.save_scrWidth = 640;
	videl.save_scrHeight = 480;
	videl.save_scrBpp = ConfigureParams.Screen.nForceBpp;
	HostScreen_setWindowSize(videl.save_scrWidth, videl.save_scrHeight, videl.save_scrBpp);

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
	MemorySnapShot_Store(&videl_zoom, sizeof(videl_zoom));
	MemorySnapShot_Store(&vfc_counter, sizeof(vfc_counter));
}

/**
 * Monitor write access to ST/E color palette registers
 */
void VIDEL_ColorRegsWrite(void)
{
	videl.hostColorsSync = false;
}

/**
 * VIDEL_Monitor_WriteByte : Contains memory and monitor configuration. 
 *                           This register is read only.
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
		default:
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
	return (IoMem_ReadByte(HW + 1) << 16) | (IoMem_ReadByte(HW + 3) << 8) | IoMem_ReadByte(HW + 0x0d);
}

static int VIDEL_getScreenBpp(void)
{
	int f_shift = IoMem_ReadWord(HW + 0x66);
	int st_shift = IoMem_ReadByte(HW + 0x60);
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

/*	LOG_TRACE(TRACE_VIDEL, "Videl works in %d bpp, f_shift=%04x, st_shift=%d", bits_per_pixel, f_shift, st_shift); */

	return bits_per_pixel;
}

/**
 *  VIDEL_getScreenWidth : returns the visible X resolution
 *	left border + graphic area + right border
 *	left border  : hdb - hbe ???
 *	right border : hbb - hde ???
 *	Graphics display : starts at cycle HDB and ends at cycle HDE. 
 */
static int VIDEL_getScreenWidth(void)
{
	Uint16 hbb = IoMem_ReadWord(HW + 0x84) & 0x03ff;
	Uint16 hbe = IoMem_ReadWord(HW + 0x86) & 0x03ff;  
	Uint16 hdb = IoMem_ReadWord(HW + 0x88) & 0x01ff;
	Uint16 hde = IoMem_ReadWord(HW + 0x8a) & 0x03ff;

	videl.leftBorderSize = hdb - hbe > 0 ? hdb - hbe : 0;
	videl.rightBorderSize = hbb - hde > 0 ? hbb - hde : 0;
	videl.XSize = (IoMem_ReadWord(HW + 0x10) & 0x03ff) * 16 / VIDEL_getScreenBpp();

	/* If the user disabled the borders display from the gui, we suppress them */
	if (ConfigureParams.Screen.bAllowOverscan == 0) {
		videl.leftBorderSize = 0;
		videl.rightBorderSize = 0;
	}

	return videl.leftBorderSize + videl.XSize + videl.rightBorderSize;
}

/**
 *  VIDEL_getScreenHeight : returns the visible Y resolution
 *	upper border + graphic area + lower border
 *	upper border : vdb - vbe
 *	lower border : vbb - vde
 *	Graphics display : starts at line VDB and ends at line VDE. 
 *	If interlace mode off unit of VC-registers is half lines, else lines.
 */
static int VIDEL_getScreenHeight(void)
{
	Uint16 vbb = IoMem_ReadWord(HW + 0xa4) & 0x07ff;
	Uint16 vbe = IoMem_ReadWord(HW + 0xa6) & 0x07ff;  
	Uint16 vdb = IoMem_ReadWord(HW + 0xa8) & 0x07ff;
	Uint16 vde = IoMem_ReadWord(HW + 0xaa) & 0x07ff;
	Uint16 vmode = IoMem_ReadWord(HW + 0xc2);

	/* We must take the positive value only, as a program like AceTracker starts the */
	/* graphical area 1 line before the end of the upper border */
	videl.upperBorderSize = vdb - vbe > 0 ? vdb - vbe : 0;
	videl.lowerBorderSize = vbb - vde > 0 ? vbb - vde : 0;
	videl.YSize = vde - vdb;

	/* If the user disabled the borders display from the gui, we suppress them */
	if (ConfigureParams.Screen.bAllowOverscan == 0) {
		videl.upperBorderSize = 0;
		videl.lowerBorderSize = 0;
	}

	if (!(vmode & 0x02)){		/* interlace */
		videl.YSize >>= 1;
		videl.upperBorderSize >>= 1;
		videl.lowerBorderSize >>= 1;
	}
	
	if (vmode & 0x01) {		/* double */
		videl.YSize >>= 1;
		videl.upperBorderSize >>= 1;
		videl.lowerBorderSize >>= 1;
	}
	
	return videl.upperBorderSize + videl.YSize + videl.lowerBorderSize;
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
	int vmode = IoMem_ReadWord(HW + 0xc2);

	/* half pixel seems to have opposite meaning on
	 * VGA and RGB monitor, so they need to handled separately
	 */
	if (videl.monitor_type) == FALCON_MONITOR_VGA) {
		if (vmode & 0x08) {  /* quarter pixel */
			*sx = 1;
		} else {
			*sx = 2;
		}
		if (vmode & 0x01) {  /* double line */
			*sy = 2;
		} else {
			*sy = 1;
		}
	} else {
		if (vmode & 0x04) {  /* half pixel */
			*sx = 1;
		} else {
			*sx = 2;
		}
		if (vmode & 0x02) {  /* interlace used only on RGB ? */
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
	int i, r, g, b, colors = 1 << videl.save_scrBpp;

#define F_COLORS(i) IoMem_ReadByte(VIDEL_COLOR_REGS_BEGIN + (i))
#define STE_COLORS(i)	IoMem_ReadByte(0xff8240 + (i))

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

	videl.hostColorsSync = true;
}


void VIDEL_ZoomModeChanged(void)
{
	/* User selected another zoom mode, so set a new screen resolution now */
	HostScreen_setWindowSize(videl.save_scrWidth, videl.save_scrHeight, videl.save_scrBpp == 16 ? 16 : ConfigureParams.Screen.nForceBpp);
}


bool VIDEL_renderScreen(void)
{
	/* Atari screen infos */
	int vw	 = VIDEL_getScreenWidth();
	int vh	 = VIDEL_getScreenHeight();
	int vbpp = VIDEL_getScreenBpp();

	int lineoffset = IoMem_ReadWord(HW + 0x0e) & 0x01ff; /* 9 bits */
	int linewidth = IoMem_ReadWord(HW + 0x10) & 0x03ff;  /* 10 bits */

	bool change = false;

	if (vw > 0 && vw != videl.save_scrWidth) {
		LOG_TRACE(TRACE_VIDEL, "Videl width change from %d to %d\n", videl.save_scrWidth, vw);
		videl.save_scrWidth = vw;
		change = true;
	}
	if (vh > 0 && vh != videl.save_scrHeight) {
		LOG_TRACE(TRACE_VIDEL, "Videl height change from %d to %d\n", videl.save_scrHeight, vh);
		videl.save_scrHeight = vh;
		change = true;
	}
	if (vbpp != videl.save_scrBpp) {
		LOG_TRACE(TRACE_VIDEL, "Videl bpp change from %d to %d\n", videl.save_scrBpp, vbpp);
		videl.save_scrBpp = vbpp;
		change = true;
	}
	if (change) {
		LOG_TRACE(TRACE_VIDEL, "Videl : video mode change to %dx%d@%d\n", videl.save_scrWidth, videl.save_scrHeight, videl.save_scrBpp);
		HostScreen_setWindowSize(videl.save_scrWidth, videl.save_scrHeight, videl.save_scrBpp == 16 ? 16 : ConfigureParams.Screen.nForceBpp);
	}

	if (!HostScreen_renderBegin())
		return false;

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
	   int bitoffset = IoMem_ReadWord(HW + 0x64) & 0x000f;
	   The meaning of this register in True Color mode is not clear
	   for me at the moment (and my experiments on the Falcon don't help
	   me).
	*/
	int nextline = linewidth + lineoffset;

	if ((vw<32) || (vh<32))
		return false;

	if (videl.save_scrBpp < 16 && videl.hostColorsSync == 0)
		VIDEL_updateColors();

	if (nScreenZoomX * nScreenZoomY != 1) {
		VIDEL_ConvertScreenZoom(vw, vh, videl.save_scrBpp, nextline);
	} else {
		VIDEL_ConvertScreenNoZoom(vw, vh, videl.save_scrBpp, nextline);
	}


	HostScreen_renderEnd();
	HostScreen_update1(false);

	return true;
}


/**
 * Performs conversion from the TOS's bitplane word order (big endian) data
 * into the native chunky color index.
 */
static void VIDEL_bitplaneToChunky(Uint16 *atariBitplaneData, Uint16 bpp,
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

void VIDEL_ConvertScreenNoZoom(int vw, int vh, int vbpp, int nextline)
{
	int scrpitch = HostScreen_getPitch();
	int h, w, j;

	Uint16 *fvram = (Uint16 *) Atari2HostAddr(VIDEL_getVideoramAddress());
	Uint8 *hvram = HostScreen_getVideoramAddress();
	SDL_PixelFormat *scrfmt = HostScreen_getFormat();

	Uint16 lowBorderSize, rightBorderSize;

	int hscrolloffset = (IoMem_ReadByte(HW + 0x65) & 0x0f);

	/* Horizontal scroll register set? */
	if (hscrolloffset) {
		/* Yes, so we need to adjust offset to next line: */
		nextline += vbpp;
	}

	/* If emulated computer is the TT, we use the same rendering for display, but without the borders */
	if (ConfigureParams.System.nMachineType == MACHINE_TT) {
		videl.leftBorderSize = 0;
		videl.rightBorderSize = 0;
		videl.upperBorderSize = 0;
		videl.lowerBorderSize = 0;
	}

	/* Clip to SDL_Surface dimensions */
	int scrwidth = HostScreen_getWidth();
	int scrheight = HostScreen_getHeight();
	int vw_clip = vw;
	int vh_clip = vh;
	if (vw>scrwidth) vw_clip = scrwidth;
	if (vh>scrheight) vh_clip = scrheight;	

	/* If emulated computer is the FALCON, we must take :
	 * vw = X area display size and not all the X sreen with the borders into account
	 * vh = Y area display size and not all the Y sreen with the borders into account
	 */
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON) {
		vw = videl.XSize;
		vh = videl.YSize;
	}

	/* If there's not enough space to display the left border, just return */
	if (vw_clip < videl.leftBorderSize)
		return;
	/* If there's not enough space for the left border + the graphic area, we clip */ 
	if (vw_clip < vw + videl.leftBorderSize) {
		vw = vw_clip - videl.leftBorderSize;
		rightBorderSize = 0;
	}
	/* if there's not enough space for the left border + the graphic area + the right border, we clip the border */
	else if (vw_clip < vw + videl.leftBorderSize + videl.rightBorderSize)
		rightBorderSize = vw_clip - videl.leftBorderSize - vw;
	else
		rightBorderSize = videl.rightBorderSize;

	/* If there's not enough space to display the upper border, just return */
	if (vh_clip < videl.upperBorderSize)
		return;

	/* If there's not enough space for the upper border + the graphic area, we clip */ 
	if (vh_clip < vh + videl.upperBorderSize) {
		vh = vh_clip - videl.upperBorderSize;
		lowBorderSize = 0;
	}
	/* if there's not enough space for the upper border + the graphic area + the lower border, we clip the border */
	else if (vh_clip < vh + videl.upperBorderSize + videl.lowerBorderSize)
		lowBorderSize = vh_clip - videl.upperBorderSize - vh;
	else
		lowBorderSize = videl.lowerBorderSize;

	/* Center screen */
	hvram += ((scrheight-vh_clip)>>1)*scrpitch;
	hvram += ((scrwidth-vw_clip)>>1)*HostScreen_getBpp();

	Uint16 *fvram_line = fvram;

	/* render the graphic area */
	if (vbpp < 16) {
		/* Bitplanes modes */

		/* The SDL colors blitting... */
		Uint8 color[16];

		/* FIXME: The byte swap could be done here by enrolling the loop into 2 each by 8 pixels */
		switch ( HostScreen_getBpp() ) {
			case 1:
			{
				Uint8 *hvram_line = hvram;

				/* Render the upper border */
				VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), videl.upperBorderSize * scrpitch);
				hvram_line += videl.upperBorderSize * scrpitch;

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint8 *hvram_column = hvram_line;

					/* Left border first */
					VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), videl.leftBorderSize);
					hvram_column += videl.leftBorderSize;
				
					/* First 16 pixels */
					VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
					memcpy(hvram_column, color+hscrolloffset, 16-hscrolloffset);
					hvram_column += 16-hscrolloffset;
					fvram_column += vbpp;
					/* Now the main part of the line */
					for (w = 1; w < (vw+15)>>4; w++) {
						VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
						memcpy(hvram_column, color, 16);
						hvram_column += 16;
						fvram_column += vbpp;
					}
					/* Last pixels of the line for fine scrolling */
					if (hscrolloffset) {
						VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
						memcpy(hvram_column, color, hscrolloffset);
					}
					/* Right border */
					VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch;
				}

				/* Render the lower border */
				VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), lowBorderSize * scrpitch);
			}
			break;
			case 2:
			{
				Uint16 *hvram_line = (Uint16 *)hvram;

				/* Render the upper border */
				VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), videl.upperBorderSize * (scrpitch>>1));
				hvram_line += videl.upperBorderSize * (scrpitch>>1);

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint16 *hvram_column = hvram_line;

					/* Left border first */
					VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), videl.leftBorderSize);
					hvram_column += videl.leftBorderSize;
				
					/* First 16 pixels */
					VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
					for (j = 0; j < 16 - hscrolloffset; j++) {
						*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
					}
					fvram_column += vbpp;
					/* Now the main part of the line */
					for (w = 1; w < (vw+15)>>4; w++) {
						VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
						for (j=0; j<16; j++) {
							*hvram_column++ = HostScreen_getPaletteColor( color[j] );
						}
						fvram_column += vbpp;
					}
					/* Last pixels of the line for fine scrolling */
					if (hscrolloffset) {
						VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
						for (j = 0; j < hscrolloffset; j++) {
							*hvram_column++ = HostScreen_getPaletteColor(color[j]);
						}
					}
					/* Right border */
					VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch>>1;
				}

				/* Render the lower border */
				VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), lowBorderSize * (scrpitch>>1));
			}
			break;
			case 4:
			{
				Uint32 *hvram_line = (Uint32 *)hvram;

				/* Render the upper border */
				VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), videl.upperBorderSize * (scrpitch>>2));
				hvram_line += videl.upperBorderSize * (scrpitch>>2);

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint32 *hvram_column = hvram_line;

					/* Left border first */
					VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), videl.leftBorderSize);
					hvram_column += videl.leftBorderSize;
				
					/* First 16 pixels */
					VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
					for (j = 0; j < 16 - hscrolloffset; j++) {
						*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
					}
					fvram_column += vbpp;
					/* Now the main part of the line */
					for (w = 1; w < (vw+15)>>4; w++) {
						VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
						for (j=0; j<16; j++) {
							*hvram_column++ = HostScreen_getPaletteColor( color[j] );
						}
						fvram_column += vbpp;
					}
					/* Last pixels of the line for fine scrolling */
					if (hscrolloffset) {
						VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
						for (j = 0; j < hscrolloffset; j++) {
							*hvram_column++ = HostScreen_getPaletteColor(color[j]);
						}
					}
					/* Right border */
					VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch>>2;
				}

				/* Render the lower border */
				VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), lowBorderSize * (scrpitch>>2));
			}
			break;
		}

	} else {

		/* Falcon TC (High Color) */
		switch ( HostScreen_getBpp() )  {
			case 1:
			{
				/* FIXME: when Videl switches to 16bpp, set the palette to 3:3:2 */
				Uint8 *hvram_line = hvram;

				/* Render the upper border */
				VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), videl.upperBorderSize * scrpitch);
				hvram_line += videl.upperBorderSize * scrpitch;

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint8 *hvram_column = hvram_line;
					int tmp;

					/* Left border first */
					VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), videl.leftBorderSize);
					hvram_column += videl.leftBorderSize;

					/* Graphical area */
					for (w = 0; w < vw; w++) {
						tmp = SDL_SwapBE16(*fvram_column++);
						*hvram_column++ = (((tmp>>13) & 7) << 5) + (((tmp>>8) & 7) << 2) + (((tmp>>2) & 3));
					}

					/* Right border */
					VIDEL_memset_uint8 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch;
				}
				/* Render the bottom border */
				VIDEL_memset_uint8 (hvram_line, HostScreen_getPaletteColor(0), videl.lowerBorderSize * scrpitch);
			}
			break;
			case 2:
			{
				Uint16 *hvram_line = (Uint16 *)hvram;

				/* Render the upper border */
				VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), videl.upperBorderSize * (scrpitch>>1));
				hvram_line += videl.upperBorderSize * (scrpitch>>1);

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *hvram_column = hvram_line;

					/* Left border first */
					VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), videl.leftBorderSize);
					hvram_column += videl.leftBorderSize;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
					/* FIXME: here might be a runtime little/big video endian switch like:
						if ( " videocard memory in Motorola endian format " false)
					*/
					memcpy(hvram_column, fvram_line, vw<<1);
					hvram_column += vw<<1;
#else
					Uint16 *fvram_column = fvram_line;
					/* Graphical area */
					for (w = 0; w < vw; w++)
						*hvram_column ++ = SDL_SwapBE16(*fvram_column++);
#endif /* SDL_BYTEORDER == SDL_BIG_ENDIAN */

					/* Right border */
					VIDEL_memset_uint16 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);

					fvram_line += nextline;
					hvram_line += scrpitch>>1;
				}

				/* Render the bottom border */
				VIDEL_memset_uint16 (hvram_line, HostScreen_getPaletteColor(0), videl.lowerBorderSize * (scrpitch>>1));
			}
			break;
			case 4:
			{
				Uint32 *hvram_line = (Uint32 *)hvram;

				/* Render the upper border */
				VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), videl.upperBorderSize * (scrpitch>>2));
				hvram_line += videl.upperBorderSize * (scrpitch>>2);

				/* Render the graphical area */
				for (h = 0; h < vh; h++) {
					Uint16 *fvram_column = fvram_line;
					Uint32 *hvram_column = hvram_line;

					/* Left border first */
					VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), videl.leftBorderSize);
					hvram_column += videl.leftBorderSize;

					/* Graphical area */
					for (w = 0; w < vw; w++) {
						Uint16 srcword = *fvram_column++;
						*hvram_column ++ = SDL_MapRGB(scrfmt, (srcword & 0xf8), (((srcword & 0x07) << 5) | ((srcword >> 11) & 0x3c)), ((srcword >> 5) & 0xf8));
					}

					/* Right border */
					VIDEL_memset_uint32 (hvram_column, HostScreen_getPaletteColor(0), rightBorderSize);
				}

				fvram_line += nextline;
				hvram_line += scrpitch>>2;

				/* Render the bottom border */
				VIDEL_memset_uint32 (hvram_line, HostScreen_getPaletteColor(0), videl.lowerBorderSize * (scrpitch>>2));
			}
			break;
		}
	}
}


void VIDEL_ConvertScreenZoom(int vw, int vh, int vbpp, int nextline)
{
	int i, j, w, h, cursrcline;

	Uint16 *fvram = (Uint16 *) Atari2HostAddr(VIDEL_getVideoramAddress());
	Uint16 *fvram_line;
	Uint16 scrIdx = 0;

	int coefx = 1;
	int coefy = 1;

	/* If emulated computer is the TT, we use the same rendering for display, but without the borders */
	if (ConfigureParams.System.nMachineType == MACHINE_TT) {
		videl.leftBorderSize = 0;
		videl.rightBorderSize = 0;
		videl.upperBorderSize = 0;
		videl.lowerBorderSize = 0;
	}

	/* Host screen infos */
	int scrpitch = HostScreen_getPitch();
	int scrwidth = HostScreen_getWidth();
	int scrheight = HostScreen_getHeight();
	int scrbpp = HostScreen_getBpp();
	SDL_PixelFormat *scrfmt = HostScreen_getFormat();
	Uint8 *hvram = (Uint8 *) HostScreen_getVideoramAddress();

	int hscrolloffset = (IoMem_ReadByte(HW + 0x65) & 0x0f);

	/* Horizontal scroll register set? */
	if (hscrolloffset) {
		/* Yes, so we need to adjust offset to next line: */
		nextline += vbpp;
	}

	/* Integer zoom coef ? */
	if (/*(bx_options.autozoom.integercoefs) &&*/ (scrwidth>=vw) && (scrheight>=vh)) {
		coefx = scrwidth/vw;
		coefy = scrheight/vh;

		scrwidth = vw * coefx;
		scrheight = vh * coefy;

		/* Center screen */
		hvram += ((HostScreen_getHeight()-scrheight)>>1)*scrpitch;
		hvram += ((HostScreen_getWidth()-scrwidth)>>1)*scrbpp;
	}

	/* New zoom ? */
	if ((videl_zoom.zoomwidth != vw) || (scrwidth != videl_zoom.prev_scrwidth)) {
		if (videl_zoom.zoomxtable) {
			free(videl_zoom.zoomxtable);
		}
		videl_zoom.zoomxtable = malloc(sizeof(int)*scrwidth);
		for (i=0; i<scrwidth; i++) {
			videl_zoom.zoomxtable[i] = (vw*i)/scrwidth;
		}
		videl_zoom.zoomwidth = vw;
		videl_zoom.prev_scrwidth = scrwidth;
	}
	if ((videl_zoom.zoomheight != vh) || (scrheight != videl_zoom.prev_scrheight)) {
		if (videl_zoom.zoomytable) {
			free(videl_zoom.zoomytable);
		}
		videl_zoom.zoomytable = malloc(sizeof(int)*scrheight);
		for (i=0; i<scrheight; i++) {
			videl_zoom.zoomytable[i] = (vh*i)/scrheight;
		}
		videl_zoom.zoomheight = vh;
		videl_zoom.prev_scrheight = scrheight;
	}

	cursrcline = -1;

	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
		vw = videl.XSize;

	if (vbpp<16) {
		Uint8 color[16];

		/* Bitplanes modes */
		switch(scrbpp) {
			case 1:
			{
				/* One complete 16-pixel aligned planar 2 chunky line */
				Uint8 *p2cline = malloc(sizeof(Uint8) * ((vw+15) & ~15));
				Uint8 *hvram_line = hvram;
				Uint8 *hvram_column = p2cline;

				for (h = 0; h < scrheight; h++) {
					/* Render the upper border */
					if (h < videl.upperBorderSize * coefy) {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the graphical area */
					else if (h < (videl.upperBorderSize + videl.YSize) * coefy) {
						fvram_line = fvram + (videl_zoom.zoomytable[scrIdx] * nextline);
						scrIdx ++;

						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-scrpitch, scrwidth*scrbpp);
						} else {
							Uint16 *fvram_column = fvram_line;
							hvram_column = p2cline;

							/* First 16 pixels of a new line */
							VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
							memcpy(hvram_column, color+hscrolloffset, 16-hscrolloffset);
							hvram_column += 16-hscrolloffset;
							fvram_column += vbpp;
							/* Convert main part of the new line */
							for (w=1; w < (vw+15)>>4; w++) {
								VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
								memcpy(hvram_column, color, 16);
								hvram_column += 16;
								fvram_column += vbpp;
							}
							/* Last pixels of the line for fine scrolling: */
							if (hscrolloffset) {
								VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
								memcpy(hvram_column, color, hscrolloffset);
							}

							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								/* Left border first */
								if (w < videl.leftBorderSize)
									hvram_line[w] = HostScreen_getPaletteColor(0);
								/* Graphical area */
								else if (w - videl.leftBorderSize < videl.XSize * coefx)
									hvram_line[w] = p2cline[videl_zoom.zoomxtable[w - videl.leftBorderSize]];
								/* Right border now */
								else
									hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the bottom border */
					else {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}

					hvram_line += scrpitch;
					cursrcline = videl_zoom.zoomytable[h];
				}
				free(p2cline);
			}
			break;
			case 2:
			{
				/* One complete 16-pixel aligned planar 2 chunky line */
				Uint16 *p2cline = malloc(sizeof(Uint16) * ((vw+15) & ~15));
				Uint16 *hvram_line = (Uint16 *)hvram;
				Uint16 *hvram_column = p2cline;

				for (h = 0; h < scrheight; h++) {
					/* Render the upper border */
					if (h < videl.upperBorderSize * coefy) {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the graphical area */
					else if (h < (videl.upperBorderSize + videl.YSize) * coefy) {
						fvram_line = fvram + (videl_zoom.zoomytable[scrIdx] * nextline);
						scrIdx ++;

						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>1), scrwidth*scrbpp);
						} else {
							Uint16 *fvram_column = fvram_line;
							hvram_column = p2cline;

							/* First 16 pixels of a new line: */
							VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
							for (j = 0; j < 16 - hscrolloffset; j++) {
								*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
							}
							fvram_column += vbpp;
							/* Convert the main part of the new line: */
							for (w = 1; w < (vw+15)>>4; w++) {
								VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
								for (j=0; j<16; j++) {
									*hvram_column++ = HostScreen_getPaletteColor( color[j] );
								}
								fvram_column += vbpp;
							}
							/* Last pixels of the new line for fine scrolling: */
							if (hscrolloffset) {
								VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
								for (j = 0; j < hscrolloffset; j++) {
									*hvram_column++ = HostScreen_getPaletteColor(color[j]);
								}
							}

							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								/* Left border first */
								if (w < videl.leftBorderSize)
									hvram_line[w] = HostScreen_getPaletteColor(0);
								/* Graphical area */
								else if (w - videl.leftBorderSize < videl.XSize * coefx)
									hvram_line[w] = p2cline[videl_zoom.zoomxtable[w - videl.leftBorderSize]];
								/* Right border now */
								else
									hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the bottom border */
					else {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}

					hvram_line += scrpitch>>1;
					cursrcline = videl_zoom.zoomytable[h];
				}

				free(p2cline);
			}
			break;
			case 4:
			{
				/* One complete 16-pixel aligned planar 2 chunky line */
				Uint32 *p2cline = malloc(sizeof(Uint32) * ((vw+15) & ~15));
				Uint32 *hvram_line = (Uint32 *)hvram;
				Uint32 *hvram_column = p2cline;

				for (h = 0; h < scrheight; h++) {

					/* Render the upper border */
					if (h < videl.upperBorderSize * coefy) {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the graphical area */
					else if (h < (videl.upperBorderSize + videl.YSize) * coefy) {
						fvram_line = fvram + (videl_zoom.zoomytable[scrIdx] * nextline);
						scrIdx ++;
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							Uint16 *fvram_column = fvram_line;
							hvram_column = p2cline;

							/* First 16 pixels of a new line: */
							VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
							for (j = 0; j < 16 - hscrolloffset; j++) {
								*hvram_column++ = HostScreen_getPaletteColor(color[j+hscrolloffset]);
							}
							fvram_column += vbpp;
							/* Convert the main part of the new line: */
							for (w = 1; w < (vw+15)>>4; w++) {
								VIDEL_bitplaneToChunky( fvram_column, vbpp, color );
								for (j=0; j<16; j++) {
									*hvram_column++ = HostScreen_getPaletteColor( color[j] );
								}
								fvram_column += vbpp;
							}
							/* Last pixels of the new line for fine scrolling: */
							if (hscrolloffset) {
								VIDEL_bitplaneToChunky(fvram_column, vbpp, color);
								for (j = 0; j < hscrolloffset; j++) {
									*hvram_column++ = HostScreen_getPaletteColor(color[j]);
								}
							}

							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								/* Left border first */
								if (w < videl.leftBorderSize)
									hvram_line[w] = HostScreen_getPaletteColor(0);
								/* Graphical area */
								else if (w - videl.leftBorderSize < videl.XSize * coefx)
									hvram_line[w] = p2cline[videl_zoom.zoomxtable[w - videl.leftBorderSize]];
								/* Right border now */
								else
									hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the bottom border */
					else {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}

					cursrcline = videl_zoom.zoomytable[h];
					hvram_line += scrpitch>>2;
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
				Uint8 *hvram_line = hvram;
				Uint8 *hvram_column = hvram_line;

				for (h = 0; h < scrheight; h++) {
					/* Render the upper border */
					if (h < videl.upperBorderSize * coefy) {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the graphical area */
					else if (h < (videl.upperBorderSize + videl.YSize) * coefy) {
						Uint16 *fvram_column;

						fvram_line = fvram + (videl_zoom.zoomytable[scrIdx] * nextline);
						scrIdx ++;

						fvram_column = fvram_line;
						hvram_column = hvram_line;

						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-scrpitch, scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w = 0; w < scrwidth; w++) {
								Uint16 srcword;
								Uint8 dstbyte;
							
								/* Left border first */
								if (w < videl.leftBorderSize)
									*hvram_column++ = HostScreen_getPaletteColor(0);
								/* Graphical area */
								else if (w - videl.leftBorderSize < videl.XSize * coefx) {
									srcword = SDL_SwapBE16(fvram_column[videl_zoom.zoomxtable[w - videl.leftBorderSize]]);

									dstbyte = ((srcword>>13) & 7) << 5;
									dstbyte |= ((srcword>>8) & 7) << 2;
									dstbyte |= ((srcword>>2) & 3);
									*hvram_column++ = dstbyte;
								}
								/* Right border now */
								else
									*hvram_column++ = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the bottom border */
					else {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}

					cursrcline = videl_zoom.zoomytable[h];
					hvram_line += scrpitch;
				}
			}
			break;
			case 2:
			{
				Uint16 *hvram_line = (Uint16 *)hvram;
				Uint16 *hvram_column = hvram_line;

				for (h = 0; h < scrheight; h++) {
					/* Render the upper border */
					if (h < videl.upperBorderSize * coefy) {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the graphical area */
					else if (h < (videl.upperBorderSize + videl.YSize) * coefy) {
						Uint16 *fvram_column;

						fvram_line = fvram + (videl_zoom.zoomytable[scrIdx] * nextline);
						scrIdx ++;

						fvram_column = fvram_line;
						hvram_column = hvram_line;

						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>1), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w = 0; w < scrwidth; w++) {
								Uint16 srcword;
							
								/* Left border first */
								if (w < videl.leftBorderSize)
									*hvram_column++ = HostScreen_getPaletteColor(0);
								/* Graphical area */
								else if (w - videl.leftBorderSize < videl.XSize * coefx) {
									srcword = SDL_SwapBE16(fvram_column[videl_zoom.zoomxtable[w - videl.leftBorderSize]]);
									*hvram_column++ = srcword;
								}
								/* Right border now */
								else
									*hvram_column++ = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the bottom border */
					else {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}

					cursrcline = videl_zoom.zoomytable[h];
					hvram_line += scrpitch>>1;
				}
			}
			break;
			case 4:
			{
				Uint32 *hvram_line = (Uint32 *)hvram;
				Uint32 *hvram_column = hvram_line;

				for (h = 0; h < scrheight; h++) {
					/* Render the upper border */
					if (h < videl.upperBorderSize * coefy) {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the graphical area */
					else if (h < (videl.upperBorderSize + videl.YSize) * coefy) {
						Uint16 *fvram_column;

						fvram_line = fvram + (videl_zoom.zoomytable[scrIdx] * nextline);
						scrIdx ++;
						fvram_column = fvram_line;
						hvram_column = hvram_line;

						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w = 0; w < scrwidth; w++) {
								Uint16 srcword;
							
								/* Left border first */
								if (w < videl.leftBorderSize)
									*hvram_column++ = HostScreen_getPaletteColor(0);
								/* Graphical area */
								else if (w - videl.leftBorderSize < videl.XSize * coefx) {
									srcword = fvram_column[videl_zoom.zoomxtable[w - videl.leftBorderSize]];

									*hvram_column++ = SDL_MapRGB(scrfmt, (srcword & 0xf8), (((srcword & 0x07) << 5) | ((srcword >> 11) & 0x3c)), ((srcword >> 5) & 0xf8));
								}
								/* Right border now */
								else
									*hvram_column++ = HostScreen_getPaletteColor(0);
							}
						}
					}
					/* Render the bottom border */
					else {
						/* Recopy the same line ? */
						if (videl_zoom.zoomytable[h] == cursrcline) {
							memcpy(hvram_line, hvram_line-(scrpitch>>2), scrwidth*scrbpp);
						} else {
							/* Zoom a new line */
							for (w=0; w<scrwidth; w++) {
								hvram_line[w] = HostScreen_getPaletteColor(0);
							}
						}
					}

					cursrcline = videl_zoom.zoomytable[h];
					hvram_line += scrpitch>>2;
				}
			}
			break;
		}
	}
}

static void VIDEL_memset_uint32(Uint32 *addr, Uint32 color, int count)
{
	while (count-- > 0) {
		*addr++ = color;
	}
}

static void VIDEL_memset_uint16(Uint16 *addr, Uint16 color, int count)
{
	while (count-- > 0) {
		*addr++ = color;
	}
}

static void VIDEL_memset_uint8(Uint8 *addr, Uint8 color, int count)
{
	memset(addr, color, count);
}
