/*
  Hatari - videl.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

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
	$FFFF8264 (byte) : Horizontal scroll register shadow register
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

const char VIDEL_fileid[] = "Hatari videl.c";

#include "main.h"
#include "configuration.h"
#include "memorySnapShot.h"
#include "ioMem.h"
#include "log.h"
#include "screen.h"
#include "screenConvert.h"
#include "avi_record.h"
#include "statusbar.h"
#include "stMemory.h"
#include "tos.h"
#include "videl.h"
#include "video.h"				/* for bUseHighRes variable */
#include "vdi.h"				/* for bUseVDIRes variable */

#define VIDEL_COLOR_REGS_BEGIN	0xff9800


struct videl_s {
	Uint8  reg_ffff8006_save;		/* save reg_ffff8006 as it's a read only register */
	Uint8  monitor_type;			/* 00 Monochrome (SM124) / 01 Color (SC1224) / 10 VGA Color / 11 Television ($FFFF8006) */

	Uint16 vertFreqCounter;			/* Counter for VFC register $ff82a0, restarted on each VBL */
	Uint32 videoRaster;			/* Video raster offset, restarted on each VBL */

	Sint16 leftBorderSize;			/* Size of the left border */
	Sint16 rightBorderSize;			/* Size of the right border */
	Sint16 upperBorderSize;			/* Size of the upper border */
	Sint16 lowerBorderSize;			/* Size of the lower border */
	Uint16 XSize;				/* X size of the graphical area */
	Uint16 YSize;				/* Y size of the graphical area */

	Uint16 save_scrWidth;			/* save screen width to detect a change of X resolution */
	Uint16 save_scrHeight;			/* save screen height to detect a change of Y resolution */
	Uint16 save_scrBpp;			/* save screen Bpp to detect a change of bitplan mode */

	bool hostColorsSync;			/* Sync palette with host's */
	bool bUseSTShifter;			/* whether to use ST or Falcon palette */
};

static struct videl_s videl;

static void Videl_SetDefaultSavedRes(void)
{
	/* Default resolution to boot with */
	videl.save_scrWidth = 640;
	videl.save_scrHeight = 480;
	videl.save_scrBpp = 4;
}

/**
 * Called upon startup (and via VIDEL_reset())
 */
void Videl_Init(void)
{
	videl.hostColorsSync = false;
	Videl_SetDefaultSavedRes();
}

/**
 *  Called when CPU encounters a RESET instruction.
 */
void VIDEL_reset(void)
{
	Videl_Init();
	Screen_SetGenConvSize(videl.save_scrWidth, videl.save_scrHeight, false);

	videl.bUseSTShifter = false;				/* Use Falcon color palette by default */
	videl.reg_ffff8006_save = IoMem_ReadByte(0xff8006);
	videl.monitor_type = videl.reg_ffff8006_save & 0xc0;

	VIDEL_RestartVideoCounter();

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

	/* Make sure that the save_scr* variables match the ones during reset,
	 * so that resolution changes get evaluated properly (e.g. to set the
	 * right zooming variables */
	if (!bSave)
		Videl_SetDefaultSavedRes();
}


/**
 * Return the vertical refresh rate for the current video mode
 * We use the following formula :
 *   VFreq = ( HFreq / (VFT+1) ) * 2
 * HFreq is 15625 Hz in RGB/TV mode or 31250 Hz in VGA mode (in VGA mode HFreq can take other values in the same range)
 *
 * Some VFT values set by TOS :
 *  - 320x200 16 colors, RGB : VFT = 625	-> 50 Hz
 *  - 320x200 16 colors, VGA : VFT = 1049	-> 60 Hz
 */
int VIDEL_Get_VFreq(void)
{
	int	HFreq;
	int	VFT;
	int	VFreq;

	if ( IoMem_ReadWord(0xff82c0) & 4 )		/* VC0 : bit2=0 32 MHz   bit2=1 25 MHz */
		HFreq = 31250;				/* 25 MHz, VGA */
	else
		HFreq = 15625;				/* 32 MHz, RGB */

	VFT = IoMem_ReadWord(0xff82a2);


	VFreq = round ( ( (double)HFreq / ( VFT+1 ) ) * 2 );

	return VFreq;
}


/**
 * Return the content of videl.bUseSTShifter.
 * This tells if the current video mode is compatible with ST/STE
 * video mode or not
 */
bool VIDEL_Use_STShifter(void)
{
	return videl.bUseSTShifter;
}


/**
 * Monitor write access to Falcon color palette registers
 */
void VIDEL_FalconColorRegsWrite(void)
{
	uint32_t color = IoMem_ReadLong(IoAccessBaseAddress & ~3);
	color &= 0xfcfc00fc;	/* Unused bits have to be set to 0 */
	IoMem_WriteLong(IoAccessBaseAddress & ~3, color);
	videl.hostColorsSync = false;
}

/**
 * VIDEL_Monitor_WriteByte : Contains memory and monitor configuration.
 *                           This register is read only.
 */
void VIDEL_Monitor_WriteByte(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8006 Monitor and memory conf write (Read only)\n");
	/* Restore hardware value */

	IoMem_WriteByte(0xff8006, videl.reg_ffff8006_save);
}

/**
 * VIDEL_SyncMode_WriteByte:
 * Videl synchronization mode. Bit 1 is used by TOS 4.04 to set either 50 Hz
 * (bit set) or 60 Hz (bit cleared).
 * Note: There are documentation files out there that claim that bit 1 is
 * used to distinguish between monochrome or color monitor, but these are
 * definitely wrong.
 */
void VIDEL_SyncMode_WriteByte(void)
{
	Uint8 syncMode = IoMem_ReadByte(0xff820a);
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff820a Sync Mode write: 0x%02x\n", syncMode);

	syncMode &= 0x03;	/* Upper bits are hard-wired to 0 */

	IoMem_WriteByte(0xff820a, syncMode);
}

/**
 * Read video address counter and update ff8205/07/09
 */
void VIDEL_ScreenCounter_ReadByte(void)
{
	Uint32 addr = videl.videoRaster;
	IoMem[0xff8205] = ( addr >> 16 ) & 0xff;
	IoMem[0xff8207] = ( addr >> 8 ) & 0xff;
	IoMem[0xff8209] = addr & 0xff;

	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8205/07/09 Sync Mode read: 0x%08x\n", addr);
}

/**
 * Write video address counter
 */
void VIDEL_ScreenCounter_WriteByte(void)
{
	Uint32 addr_new = videl.videoRaster;
	Uint8 AddrByte = IoMem[ IoAccessCurrentAddress ];

	/* Compute the new video address with one modified byte */
	if ( IoAccessCurrentAddress == 0xff8205 )
		addr_new = ( addr_new & 0x00ffff ) | ( AddrByte << 16 );
	else if ( IoAccessCurrentAddress == 0xff8207 )
		addr_new = ( addr_new & 0xff00ff ) | ( AddrByte << 8 );
	else if ( IoAccessCurrentAddress == 0xff8209 )
		addr_new = ( addr_new & 0xffff00 ) | ( AddrByte );

	videl.videoRaster = addr_new;
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8205/07/09 Sync Mode write: 0x%08x\n", addr_new);
}

/**
 * VIDEL_LineOffset_ReadWord: $FFFF820E [R/W] W _______876543210  Line Offset
 * How many words are added to the end of display line, i.e. how many words are
 * 'behind' the display.
 */
void VIDEL_LineOffset_ReadWord(void)
{
	/* Unused bits in the first byte are read as zero, so mask them */
	IoMem[0xff820e] &= 0x01;
}

/**
 * VIDEL_LineOffset_WriteWord: $FFFF820E [R/W] W _______876543210  Line Offset
 * How many words are added to the end of display line, i.e. how many words are
 * 'behind' the display.
 */
void VIDEL_LineOffset_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff820e Line Offset write: 0x%04x\n",
	          IoMem_ReadWord(0xff820e));
}

/**
 * VIDEL_Line_Width_WriteWord: $FFFF8210 [R/W] W ______9876543210 Line Width (VWRAP)
 * Length of display line in words.Or, how many words should be added to
 * vram counter after every display line.
 */
void VIDEL_Line_Width_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8210 Line Width write: 0x%04x\n",
	          IoMem_ReadWord(0xff8210));
}

/**
 * Write to video address base high, med and low register (0xff8201/03/0d).
 * On Falcon, when a program writes to high or med registers, base low register
 * is reset to zero.
 */
void VIDEL_ScreenBase_WriteByte(void)
{
	if ((IoAccessCurrentAddress == 0xff8201) || (IoAccessCurrentAddress == 0xff8203)) {
		/* Reset screen base low register */
		IoMem[0xff820d] = 0;
	}

	LOG_TRACE(TRACE_VIDEL, "Videl : $%04x Screen base write: 0x%02x\t (screen: 0x%04x)\n",
	          IoAccessCurrentAddress, IoMem[IoAccessCurrentAddress],
	          (IoMem[0xff8201]<<16) + (IoMem[0xff8203]<<8) + IoMem[0xff820d]);
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
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8260 ST Shift Mode (STSHIFT) write: 0x%02x\n", st_shiftMode);

	/* Bits 2-7 are set to 0 */
	IoMem_WriteByte(0xff8260, st_shiftMode & 3);

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

	/* Hack for Sparrow-TOS (which does not know about Videl registers): */
	if (TosVersion == 0x207)
	{
		if (st_shiftMode == 2)  /* Mono? */
		{
			IoMem_WriteWord(0xff82a4, 0);
			IoMem_WriteWord(0xff82a6, 0);
			IoMem_WriteWord(0xff82a8, 0x43);
			IoMem_WriteWord(0xff82aa, 0x363);
		}
		else if (ConfigureParams.Screen.nMonitorType == MONITOR_TYPE_VGA)
		{
			IoMem_WriteWord(0xff82a4, 0x3af);
			IoMem_WriteWord(0xff82a6, 0x8f);
			IoMem_WriteWord(0xff82a8, 0x8f);
			IoMem_WriteWord(0xff82aa, 0x3af);
		}
		else
		{
			IoMem_WriteWord(0xff82a4, 0x20e);
			IoMem_WriteWord(0xff82a6, 0x7e);
			IoMem_WriteWord(0xff82a8, 0x7e);
			IoMem_WriteWord(0xff82aa, 0x20e);
		}
	}
}

/**
    VIDEL_HorScroll64_WriteByte : Horizontal scroll register (0-15)
		$FFFF8264 [R/W] ________  ................................ H-SCROLL HI
				    ||||  [ Shadow register for $FFFF8265 ]
				    ++++--Pixel shift [ 0:normal / 1..15:Left shift ]
					[ Change in line-width NOT required ]
 */
void VIDEL_HorScroll64_WriteByte(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8264 Horizontal scroll 64 write: 0x%02x\n",
	          IoMem_ReadByte(0xff8264));
}

/**
    VIDEL_HorScroll65_WriteByte : Horizontal scroll register (0-15)
		$FFFF8265 [R/W] ____3210  .................................H-SCROLL LO
				    ||||
				    ++++--Pixel [ 0:normal / 1..15:Left shift ]
					[ Change in line-width NOT required ]
 */
void VIDEL_HorScroll65_WriteByte(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8265 Horizontal scroll 65 write: 0x%02x\n",
	          IoMem_ReadByte(0xff8265));
}

/**
    VIDEL_Falcon_ShiftMode_WriteWord :
	$FFFF8266 [R/W] W  _____A98_6543210  Falcon Shift Mode (SPSHIFT)
	                        ||| |||||||
	                        ||| |||++++- 0..15: Colourbank choice from 256-colour table in 16 colour multiples
	                        ||| ||+----- 8 Bitplanes mode (256 Colors) [0:off / 1:on]
	                        ||| |+------ Vertical Sync [0: internal / 1: external]
	                        ||| +------- Horizontal Sync [0: internal / 1: external]
	                        ||+--------- True-Color-Mode [0:off / 1:on]
	                        |+---------- Overlay-Mode [0:off / 1:on]
	                        +----------- 0: 2-Color-Mode [0:off / 1:on]

	Writing to this register does the following things:
		- activate Falcon palette
		- if you set Bits A/8/4 == 0, it selects 16-Color-Falcon-Mode (NOT the
		  same as ST LOW since Falcon palette is used!)
		- $8260 register is ignored, you don't need to write here anything

	Note: 4-Color-Mode isn't realisable with Falcon palette.
 */
void VIDEL_Falcon_ShiftMode_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8266 Falcon Shift Mode (SPSHIFT) write: 0x%04x\n",
	          IoMem_ReadWord(0xff8266));

	videl.bUseSTShifter = false;
}

/**
 *  Write Horizontal Hold Counter (HHC)
 */
void VIDEL_HHC_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8280 Horizontal Hold Counter (HHC) write: 0x%04x\n",
	          IoMem_ReadWord(0xff8280));
}

/**
 *  Write Horizontal Hold Timer (HHT)
 */
void VIDEL_HHT_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8282 Horizontal Hold Timer (HHT) write: 0x%04x\n",
	          IoMem_ReadWord(0xff8282));
}

/**
 *  Write Horizontal Border Begin (HBB)
 */
void VIDEL_HBB_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8284 Horizontal Border Begin (HBB) write: 0x%04x\n",
	          IoMem_ReadWord(0xff8284));
}

/**
 *  Write Horizontal Border End (HBE)
 */
void VIDEL_HBE_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8286 Horizontal Border End (HBE) write: 0x%04x\n",
	          IoMem_ReadWord(0xff8286));
}

/**
 *  Write Horizontal Display Begin (HDB)
	$FFFF8288 [R/W] W ______9876543210  Horizontal Display Begin (HDB)
				|
				+---------- Display will start in [0: 1st halfline / 1: 2nd halfline]
 */
void VIDEL_HDB_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8288 Horizontal Display Begin (HDB) write: 0x%04x\n",
	          IoMem_ReadWord(0xff8288));
}

/**
 *  Write Horizontal Display End (HDE)
 */
void VIDEL_HDE_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff828a Horizontal Display End (HDE) write: 0x%04x\n",
	          IoMem_ReadWord(0xff828a));
}

/**
 *  Write Horizontal SS (HSS)
 */
void VIDEL_HSS_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff828c Horizontal SS (HSS) write: 0x%04x\n",
	          IoMem_ReadWord(0xff828c));
}

/**
 *  Write Horizontal FS (HFS)
 */
void VIDEL_HFS_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff828e Horizontal FS (HFS) write: 0x%04x\n",
	          IoMem_ReadWord(0xff828e));
}

/**
 *  Write Horizontal EE (HEE)
 */
void VIDEL_HEE_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff8290 Horizontal EE (HEE) write: 0x%04x\n",
	          IoMem_ReadWord(0xff8290));
}

/**
 *  Write Vertical Frequency Counter (VFC)
 */
void VIDEL_VFC_ReadWord(void)
{
	IoMem_WriteWord(0xff82a0, videl.vertFreqCounter);
	videl.vertFreqCounter++;
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff82a0 Vertical Frequency Counter (VFC) read: 0x%04x\n", videl.vertFreqCounter);
}

/**
 *  Write Vertical Frequency Timer (VFT)
 */
void VIDEL_VFT_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff82a2 Vertical Frequency Timer (VFT) write: 0x%04x\n",
	          IoMem_ReadWord(0xff82a2));
}

/**
 *  Write Vertical Border Begin (VBB)
 */
void VIDEL_VBB_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff82a4 Vertical Border Begin (VBB) write: 0x%04x\n",
	          IoMem_ReadWord(0xff82a4));
}

/**
 *  Write Vertical Border End (VBE)
 */
void VIDEL_VBE_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff82a6 Vertical Border End (VBE) write: 0x%04x\n",
	          IoMem_ReadWord(0xff82a6));
}

/**
 *  Write Vertical Display Begin (VDB)
 */
void VIDEL_VDB_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff82a8 Vertical Display Begin (VDB) write: 0x%04x\n",
	          IoMem_ReadWord(0xff82a8));
}

/**
 *  Write Vertical Display End (VDE)
 */
void VIDEL_VDE_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff82aa Vertical Display End (VDE) write: 0x%04x\n",
	          IoMem_ReadWord(0xff82aa));
}

/**
 *  Write Vertical SS (VSS)
 */
void VIDEL_VSS_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff82ac Vertical SS (VSS) write: 0x%04x\n",
	          IoMem_ReadWord(0xff82ac));
}

/**
 *  Write Video Control (VCO)
 */
void VIDEL_VCO_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff82c0 Video control (VCO) write: 0x%04x\n",
	          IoMem_ReadWord(0xff82c0));
}

/**
 *  Write Video Mode (VDM)
 */
void VIDEL_VMD_WriteWord(void)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : $ff82c2 Video Mode (VDM) write: 0x%04x\n",
	          IoMem_ReadWord(0xff82c2));
}


/**
 * Reset appropriate registers on VBL etc
 */
void VIDEL_RestartVideoCounter(void)
{
	videl.videoRaster = Video_GetScreenBaseAddr();
	/* counter for VFC register $ff82a0 */
	videl.vertFreqCounter = 0;
}

/**
 * Increment appropriate registers on HBL
 */
void VIDEL_VideoRasterHBL(void)
{
	int lineoffset = IoMem_ReadWord(0xff820e) & 0x01ff; /* 9 bits */
	int linewidth = IoMem_ReadWord(0xff8210) & 0x03ff;  /* 10 bits */

	videl.videoRaster += linewidth + lineoffset;

	/* TODO: VFC is incremented every half line, here, we increment it every line */
	videl.vertFreqCounter++;
}


static Uint16 VIDEL_getScreenBpp(void)
{
	Uint16 f_shift = IoMem_ReadWord(0xff8266);
	Uint16 bits_per_pixel;
	Uint8  st_shift = IoMem_ReadByte(0xff8260);

	/* to get bpp, we must examine f_shift and st_shift.
	 * f_shift is valid if any of bits no. 10, 8 or 4 is set.
	 * Priority in f_shift is: 10 ">" 8 ">" 4, i.e.
	 * if bit 10 set then bit 8 and bit 4 don't care...
	 * If all these bits are 0 and ST shifter is written
	 * after Falcon one, get display depth from st_shift
	 * (as for ST and STE)
	 */
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
 *	left border  : hdb - hbe-offset
 *	right border : hbb - hde-offset
 *	Graphics display : starts at cycle HDB and ends at cycle HDE.
 */
static int VIDEL_getScreenWidth(void)
{
	Uint16 hbb, hbe, hdb, hde, vdm, hht;
	Uint16 cycPerPixel, divider;
	Sint16 hdb_offset, hde_offset;
	Sint16 leftBorder, rightBorder;
	Uint16 bpp = VIDEL_getScreenBpp();

	/* X Size of the Display area */
	videl.XSize = (IoMem_ReadWord(0xff8210) & 0x03ff) * 16 / bpp;

	/* Sanity check - don't use unsupported texture sizes for SDL2:
	 *   http://answers.unity3d.com/questions/563094/mobile-max-texture-size.html
	 * (largest currently known real Videl width is ~1600)
	 */
	while (videl.XSize > 2048)
		videl.XSize /= 2;

	/* If the user disabled the borders display from the gui, we suppress them */
	if (ConfigureParams.Screen.bAllowOverscan == 0) {
		videl.leftBorderSize = 0;
		videl.rightBorderSize = 0;
		return videl.XSize;
	}

	/* According to Aura and Animal Mine doc about Videl, if a monochrome monitor is connected,
	 * HDB and HDE have no significance and no border is displayed.
	 */
	if (videl.monitor_type == FALCON_MONITOR_MONO) {
		videl.leftBorderSize = 0;
		videl.rightBorderSize = 0;
		return videl.XSize;
	}

	hbb = IoMem_ReadWord(0xff8284) & 0x01ff;
	hbe = IoMem_ReadWord(0xff8286) & 0x01ff;
	hdb = IoMem_ReadWord(0xff8288) & 0x01ff;
	hde = IoMem_ReadWord(0xff828a) & 0x01ff;
	vdm = IoMem_ReadWord(0xff82c2) & 0xc;
	hht = IoMem_ReadWord(0xff8282) & 0x1ff;

	/* Compute cycles per pixel */
	if (vdm == 0)
		cycPerPixel = 4;
	else if (vdm == 4)
		cycPerPixel = 2;
	else
		cycPerPixel = 1;

	/* Compute the divider */
	if (videl.monitor_type == FALCON_MONITOR_VGA) {
		if (cycPerPixel == 4)
			divider = 4;
		else
			divider = 2;
	}
	else if (videl.bUseSTShifter == true) {
		divider = 16;
	}
	else {
		divider = cycPerPixel;
	}

	/* Compute hdb_offset and hde_offset */
	if (videl.bUseSTShifter == false) {
		if (bpp < 16) {
			/* falcon mode bpp */
			hdb_offset = ((64+(128/bpp + 16 + 2) * cycPerPixel) / divider ) + 1;
			hde_offset = ((128/bpp + 2) * cycPerPixel) / divider;
		}
		else {
			/* falcon mode true color */
			hdb_offset = ((64 + 16 * cycPerPixel) / divider ) + 1;
			hde_offset = 0;
		}
	}
	else {
		/* ST bitplan mode */
		hdb_offset = ((128+(128/bpp + 2) * cycPerPixel) / divider ) + 1;
		hde_offset = ((128/bpp + 2) * cycPerPixel) / divider;
	}

	LOG_TRACE(TRACE_VIDEL, "hdb_offset=%04x,    hde_offset=%04x\n", hdb_offset, hde_offset);

	/* Compute left border size in cycles */
	if (IoMem_ReadWord(0xff8288) & 0x0200)
		leftBorder = hdb - hbe + hdb_offset - hht - 2;
	else
		leftBorder = hdb - hbe + hdb_offset;

	/* Compute right border size in cycles */
	rightBorder = hbb - hde_offset - hde;

	videl.leftBorderSize = leftBorder / cycPerPixel;
	videl.rightBorderSize = rightBorder / cycPerPixel;
	LOG_TRACE(TRACE_VIDEL, "left border size=%04x,    right border size=%04x\n", videl.leftBorderSize, videl.rightBorderSize);

	if (videl.leftBorderSize < 0) {
//		fprintf(stderr, "BORDER LEFT < 0   %d\n", videl.leftBorderSize);
		videl.leftBorderSize = 0;
	}
	if (videl.rightBorderSize < 0) {
//		fprintf(stderr, "BORDER RIGHT < 0   %d\n", videl.rightBorderSize);
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
	Uint16 vbb = IoMem_ReadWord(0xff82a4) & 0x07ff;
	Uint16 vbe = IoMem_ReadWord(0xff82a6) & 0x07ff;
	Uint16 vdb = IoMem_ReadWord(0xff82a8) & 0x07ff;
	Uint16 vde = IoMem_ReadWord(0xff82aa) & 0x07ff;
	Uint16 vmode = IoMem_ReadWord(0xff82c2);

	/* According to Aura and Animal Mine doc about Videl, if a monochrome monitor is connected,
	 * VDB and VDE have no significance and no border is displayed.
	 */
	if (videl.monitor_type == FALCON_MONITOR_MONO) {
		videl.upperBorderSize = 0;
		videl.lowerBorderSize = 0;
	}
	else {
		/* We must take the positive value only, as a program like AceTracker starts the */
		/* graphical area 1 line before the end of the upper border */
		videl.upperBorderSize = vdb - vbe > 0 ? vdb - vbe : 0;
		videl.lowerBorderSize = vbb - vde > 0 ? vbb - vde : 0;
	}

	/* Y Size of the Display area */
	if (vde >= vdb) {
		videl.YSize = vde - vdb;
	}
	else {
		LOG_TRACE(TRACE_VIDEL, "WARNING: vde=0x%x is less than vdb=0x%x\n",
		          vde, vdb);
	}

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


/**
 * Map the correct colortable into the correct pixel format
 */
void VIDEL_UpdateColors(void)
{
	int i, r, g, b, colors = 1 << videl.save_scrBpp;

	if (videl.hostColorsSync)
		return;

#define F_COLORS(i) IoMem_ReadByte(VIDEL_COLOR_REGS_BEGIN + (i))
#define STE_COLORS(i)	IoMem_ReadByte(0xff8240 + (i))

	/* True color mode ? */
	if (videl.save_scrBpp > 8) {
		/* Videl color 0 ($ffff9800) must be taken into account as it is the border color in true color mode */
		r = F_COLORS(0) & 0xfc;
		r |= r>>6;
		g = F_COLORS(0 + 1) & 0xfc;
		g |= g>>6;
		b = F_COLORS(0 + 3) & 0xfc;
		b |= b>>6;
		Screen_SetPaletteColor(0, r,g,b);
		return;
	}

	if (!videl.bUseSTShifter) {
		for (i = 0; i < colors; i++) {
			int offset = i << 2;
			r = F_COLORS(offset) & 0xfc;
			r |= r>>6;
			g = F_COLORS(offset + 1) & 0xfc;
			g |= g>>6;
			b = F_COLORS(offset + 3) & 0xfc;
			b |= b>>6;
			Screen_SetPaletteColor(i, r,g,b);
		}
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
			Screen_SetPaletteColor(i, r,g,b);
		}
	}

	videl.hostColorsSync = true;
}


void Videl_ScreenModeChanged(bool bForceChange)
{
	LOG_TRACE(TRACE_VIDEL, "Videl : video mode change to %dx%d@%d\n",
	          videl.save_scrWidth, videl.save_scrHeight, videl.save_scrBpp);

	Screen_SetGenConvSize(videl.save_scrWidth, videl.save_scrHeight, bForceChange);
}


bool VIDEL_renderScreen(void)
{
	/* Atari screen infos */
	int vw	 = VIDEL_getScreenWidth();
	int vh	 = VIDEL_getScreenHeight();
	int vbpp = VIDEL_getScreenBpp();

	int lineoffset = IoMem_ReadWord(0xff820e) & 0x01ff; /* 9 bits */
	int linewidth = IoMem_ReadWord(0xff8210) & 0x03ff;  /* 10 bits */
	int hscrolloffset = IoMem_ReadByte(0xff8265) & 0x0f;
	int nextline;

	bool change = false;

	Uint32 videoBase = Video_GetScreenBaseAddr();

	if (vw > 0 && vw != videl.save_scrWidth) {
		LOG_TRACE(TRACE_VIDEL, "Videl : width change from %d to %d\n", videl.save_scrWidth, vw);
		videl.save_scrWidth = vw;
		change = true;
	}
	if (vh > 0 && vh != videl.save_scrHeight) {
		LOG_TRACE(TRACE_VIDEL, "Videl : height change from %d to %d\n", videl.save_scrHeight, vh);
		videl.save_scrHeight = vh;
		change = true;
	}
	if (vbpp != videl.save_scrBpp) {
		LOG_TRACE(TRACE_VIDEL, "Videl : bpp change from %d to %d\n", videl.save_scrBpp, vbpp);
		videl.save_scrBpp = vbpp;
		change = true;
	}
	if (change) {
		Videl_ScreenModeChanged(false);
	}

	if (vw < 32 || vh < 32) {
		LOG_TRACE(TRACE_VIDEL, "Videl : %dx%d screen size, not drawing\n", vw, vh);
		return false;
	}

	if (!Screen_Lock())
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
	   int bitoffset = IoMem_ReadWord(0xff8264) & 0x000f;
	   The meaning of this register in True Color mode is not clear
	   for me at the moment (and my experiments on the Falcon don't help
	   me).
	*/
	nextline = linewidth + lineoffset;

	VIDEL_UpdateColors();

	Screen_GenConvert(videoBase, &STRam[videoBase], videl.XSize, videl.YSize,
	                  videl.save_scrBpp, nextline, hscrolloffset,
	                  videl.leftBorderSize, videl.rightBorderSize,
	                  videl.upperBorderSize, videl.lowerBorderSize);

	Screen_UnLock();
	Screen_GenConvUpdate(Statusbar_Update(sdlscrn, false), false);

	return true;
}


/**
 * Write to videl ST palette registers (0xff8240-0xff825e)
 *
 * Note that there's a special "strange" case when writing only to the upper byte
 * of the color reg (instead of writing 16 bits at once with .W/.L).
 * In that case, the byte written to address x is automatically written
 * to address x+1 too (but we shouldn't copy x in x+1 after masking x ; we apply the mask at the end)
 * Similarly, when writing a byte to address x+1, it's also written to address x
 * So :	move.w #0,$ff8240	-> color 0 is now $000
 *	move.b #7,$ff8240	-> color 0 is now $707 !
 *	move.b #$55,$ff8241	-> color 0 is now $555 !
 *	move.b #$71,$ff8240	-> color 0 is now $171 (bytes are first copied, then masked)
 */
static void Videl_ColorReg_WriteWord(void)
{
	Uint16 col;
	Uint32 addr = IoAccessCurrentAddress;

	videl.hostColorsSync = false;

	if (bUseHighRes || bUseVDIRes)               /* Don't store if hi-res or VDI resolution */
		return;

	/* Handle special case when writing only to the lower or
	 * upper byte of the color reg: copy written byte also
	 * to the other byte before masking the color value.
	 */
	if (nIoMemAccessSize == SIZE_BYTE)
		col = (IoMem_ReadByte(addr) << 8) + IoMem_ReadByte(addr);
	/* Usual case, writing a word or a long (2 words) */
	else
		col = IoMem_ReadWord(addr);

	col &= 0xfff;				/* Mask off to 4096 palette */

	addr &= 0xfffffffe;			/* Ensure addr is even to store the 16 bit color */

	IoMem_WriteWord(addr, col);
}

/*
 * [NP] TODO : due to how .L accesses are handled in ioMem.c, we can't call directly
 * Video_ColorReg_WriteWord from ioMemTabFalcon.c, we must use an intermediate
 * function, else .L accesses will not change 2 .W color regs, but only one.
 * This should be changed in ioMem.c to do 2 separate .W accesses, as would do a real 68000
 */

void Videl_Color0_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color1_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color2_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color3_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color4_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color5_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color6_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color7_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color8_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color9_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color10_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color11_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color12_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color13_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color14_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

void Videl_Color15_WriteWord(void)
{
	Videl_ColorReg_WriteWord();
}

/**
 * display Videl registers values (for debugger info command)
 */
void Videl_Info(FILE *fp, uint32_t dummy)
{
	if (ConfigureParams.System.nMachineType != MACHINE_FALCON) {
		fprintf(fp, "Not Falcon - no Videl!\n");
		return;
	}

	fprintf(fp, "$FF8006.b : monitor type                     : %02x\n", IoMem_ReadByte(0xff8006));
	fprintf(fp, "$FF8201.b : Video Base Hi                    : %02x\n", IoMem_ReadByte(0xff8201));
	fprintf(fp, "$FF8203.b : Video Base Mi                    : %02x\n", IoMem_ReadByte(0xff8203));
	fprintf(fp, "$FF8205.b : Video Count Hi                   : %02x\n", IoMem_ReadByte(0xff8205));
	fprintf(fp, "$FF8207.b : Video Count Mi                   : %02x\n", IoMem_ReadByte(0xff8207));
	fprintf(fp, "$FF8209.b : Video Count Lo                   : %02x\n", IoMem_ReadByte(0xff8209));
	fprintf(fp, "$FF820A.b : Sync mode                        : %02x\n", IoMem_ReadByte(0xff820a));
	fprintf(fp, "$FF820D.b : Video Base Lo                    : %02x\n", IoMem_ReadByte(0xff820d));
	fprintf(fp, "$FF820E.w : offset to next line              : %04x\n", IoMem_ReadWord(0xff820e));
	fprintf(fp, "$FF8210.w : VWRAP - line width               : %04x\n", IoMem_ReadWord(0xff8210));
	fprintf(fp, "$FF8260.b : ST shift mode                    : %02x\n", IoMem_ReadByte(0xff8260));
	fprintf(fp, "$FF8264.w : Horizontal scroll register       : %04x\n", IoMem_ReadWord(0xff8264));
	fprintf(fp, "$FF8266.w : Falcon shift mode                : %04x\n", IoMem_ReadWord(0xff8266));
	fprintf(fp, "\n");
	fprintf(fp, "$FF8280.w : HHC - Horizontal Hold Counter    : %04x\n", IoMem_ReadWord(0xff8280));
	fprintf(fp, "$FF8282.w : HHT - Horizontal Hold Timer      : %04x\n", IoMem_ReadWord(0xff8282));
	fprintf(fp, "$FF8284.w : HBB - Horizontal Border Begin    : %04x\n", IoMem_ReadWord(0xff8284));
	fprintf(fp, "$FF8286.w : HBE - Horizontal Border End      : %04x\n", IoMem_ReadWord(0xff8286));
	fprintf(fp, "$FF8288.w : HDB - Horizontal Display Begin   : %04x\n", IoMem_ReadWord(0xff8288));
	fprintf(fp, "$FF828A.w : HDE - Horizontal Display End     : %04x\n", IoMem_ReadWord(0xff828a));
	fprintf(fp, "$FF828C.w : HSS - Horizontal SS              : %04x\n", IoMem_ReadWord(0xff828c));
	fprintf(fp, "$FF828E.w : HFS - Horizontal FS              : %04x\n", IoMem_ReadWord(0xff828e));
	fprintf(fp, "$FF8290.w : HEE - Horizontal EE              : %04x\n", IoMem_ReadWord(0xff8290));
	fprintf(fp, "\n");
	fprintf(fp, "$FF82A0.w : VFC - Vertical Frequency Counter : %04x\n", IoMem_ReadWord(0xff82a0));
	fprintf(fp, "$FF82A2.w : VFT - Vertical Frequency Timer   : %04x\n", IoMem_ReadWord(0xff82a2));
	fprintf(fp, "$FF82A4.w : VBB - Vertical Border Begin      : %04x\n", IoMem_ReadWord(0xff82a4));
	fprintf(fp, "$FF82A6.w : VBE - Vertical Border End        : %04x\n", IoMem_ReadWord(0xff82a6));
	fprintf(fp, "$FF82A8.w : VDB - Vertical Display Begin     : %04x\n", IoMem_ReadWord(0xff82a8));
	fprintf(fp, "$FF82AA.w : VDE - Vertical Display End       : %04x\n", IoMem_ReadWord(0xff82aa));
	fprintf(fp, "$FF82AC.w : VSS - Vertical SS                : %04x\n", IoMem_ReadWord(0xff82ac));
	fprintf(fp, "\n");
	fprintf(fp, "$FF82C0.w : VCO - Video control              : %04x\n", IoMem_ReadWord(0xff82c0));
	fprintf(fp, "$FF82C2.w : VMD - Video mode                 : %04x\n", IoMem_ReadWord(0xff82c2));
	fprintf(fp, "\n-------------------------\n");

	fprintf(fp, "Video base  : %08x\n",
		(IoMem_ReadByte(0xff8201)<<16) +
		(IoMem_ReadByte(0xff8203)<<8)  +
		 IoMem_ReadByte(0xff820d));
	fprintf(fp, "Video count : %08x\n",
		(IoMem_ReadByte(0xff8205)<<16) +
		(IoMem_ReadByte(0xff8207)<<8)  +
		 IoMem_ReadByte(0xff8209));

	fprintf(fp, "Palette type: %s\n", videl.bUseSTShifter ?
		"ST/STE compat ($FF8240)" :
		"Falcon ($FF9800)");
}
