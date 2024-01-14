/*
  Hatari - ioMemTabSTE.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Table with hardware IO handlers for the STE.
*/
const char IoMemTabSTE_fileid[] = "Hatari ioMemTabSTE.c";

#include "main.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "fdc.h"
#include "acia.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"
#include "joy.h"
#include "mfp.h"
#include "midi.h"
#include "psg.h"
#include "rs232.h"
#include "rtc.h"
#include "video.h"
#include "blitter.h"
#include "statusbar.h"
#include "stMemory.h"


/**
 * Take into account Mega STe Cache/Processor Control register $ff8e21.b
	$FFFF8E21 Mega STe Cache/Processor Control
		BIT 0 : Cache (0 - disabled, 1 - enabled)
		BIT 1 : CPU Speed (0 - 8mhz, 1 - 16mhz)

   We handle only bit 1, bit 0 is ignored (cache is not emulated)
*/
void IoMemTabMegaSTE_CacheCpuCtrl_WriteByte(void)
{
	uint8_t busCtrl = IoMem_ReadByte(0xff8e21);

	/* 68000 Frequency changed ? We change freq only in 68000 mode for a
	 * normal MegaSTE, if the user did not request a faster one manually */
	if (ConfigureParams.System.nCpuLevel == 0 && ConfigureParams.System.nCpuFreq <= 16)
	{
		if ((busCtrl & 0x2) != 0) {
			/* 16 Mhz bus for 68000 */
			Configuration_ChangeCpuFreq ( 16 );
		}
		else {
			/* 8 Mhz bus for 68000 */
			Configuration_ChangeCpuFreq ( 8 );
		}
	}
	Statusbar_UpdateInfo();			/* Update clock speed in the status bar */
}


/**
 * The register at $FF9200.b represents the DIP switches from the
 * MegaSTE motherboard. The meaning of the switches is as follows:
 *
 *   1 - 6  off
 *   7      on = 1.4mb HD floppy drive fitted
 *   8      off (on = disable the DMA sound hardware)
 *
 * Switch 1 is represented by the lowest bit in the $FF9200 register,
 * and switch 8 is represented by the highest bit. Logic is inverted,
 * i.e. when the switch is "on", the bit is 0.
 *
 * We set the value to 0xBF to enable an HD floppy drive by default
 * (earliest MegaSTE produced had a DD floppy drive, but later
 * it was replaced by an HD drive)
 */
uint8_t IoMemTabMegaSTE_DIPSwitches_Read(void)
{
	return 0xbf;
}


/*-----------------------------------------------------------------------*/
/*
  List of functions to handle read/write hardware interceptions for a STE.
*/
const INTERCEPT_ACCESS_FUNC IoMemTable_STE[] =
{
	{ 0xff8000, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8001, SIZE_BYTE, STMemory_MMU_Config_ReadByte, STMemory_MMU_Config_WriteByte },	/* Memory configuration */
	{ 0xff8002, 14,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xff8200, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8201, SIZE_BYTE, IoMem_ReadWithoutInterception, Video_ScreenBase_WriteByte },     /* Video base high byte */
	{ 0xff8202, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8203, SIZE_BYTE, IoMem_ReadWithoutInterception, Video_ScreenBase_WriteByte },     /* Video base med byte */
	{ 0xff8204, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8205, SIZE_BYTE, Video_ScreenCounter_ReadByte, Video_ScreenCounter_WriteByte },
	{ 0xff8206, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8207, SIZE_BYTE, Video_ScreenCounter_ReadByte, Video_ScreenCounter_WriteByte },
	{ 0xff8208, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8209, SIZE_BYTE, Video_ScreenCounter_ReadByte, Video_ScreenCounter_WriteByte },
	{ 0xff820a, SIZE_BYTE, Video_Sync_ReadByte, Video_Sync_WriteByte },
	{ 0xff820b, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here : return 0 not ff */
	{ 0xff820c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff820d, SIZE_BYTE, Video_BaseLow_ReadByte, Video_ScreenBase_WriteByte },
	{ 0xff820e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff820f, SIZE_BYTE, Video_LineWidth_ReadByte, Video_LineWidth_WriteByte },
	{ 0xff8240, SIZE_WORD, Video_Color0_ReadWord, Video_Color0_WriteWord },			/* COLOR 0 */
	{ 0xff8242, SIZE_WORD, Video_Color1_ReadWord, Video_Color1_WriteWord },			/* COLOR 1 */
	{ 0xff8244, SIZE_WORD, Video_Color2_ReadWord, Video_Color2_WriteWord },			/* COLOR 2 */
	{ 0xff8246, SIZE_WORD, Video_Color3_ReadWord, Video_Color3_WriteWord },			/* COLOR 3 */
	{ 0xff8248, SIZE_WORD, Video_Color4_ReadWord, Video_Color4_WriteWord },			/* COLOR 4 */
	{ 0xff824a, SIZE_WORD, Video_Color5_ReadWord, Video_Color5_WriteWord },			/* COLOR 5 */
	{ 0xff824c, SIZE_WORD, Video_Color6_ReadWord, Video_Color6_WriteWord },			/* COLOR 6 */
	{ 0xff824e, SIZE_WORD, Video_Color7_ReadWord, Video_Color7_WriteWord },			/* COLOR 7 */
	{ 0xff8250, SIZE_WORD, Video_Color8_ReadWord, Video_Color8_WriteWord },			/* COLOR 8 */
	{ 0xff8252, SIZE_WORD, Video_Color9_ReadWord, Video_Color9_WriteWord },			/* COLOR 9 */
	{ 0xff8254, SIZE_WORD, Video_Color10_ReadWord, Video_Color10_WriteWord },		/* COLOR 10 */
	{ 0xff8256, SIZE_WORD, Video_Color11_ReadWord, Video_Color11_WriteWord },		/* COLOR 11 */
	{ 0xff8258, SIZE_WORD, Video_Color12_ReadWord, Video_Color12_WriteWord },		/* COLOR 12 */
	{ 0xff825a, SIZE_WORD, Video_Color13_ReadWord, Video_Color13_WriteWord },		/* COLOR 13 */
	{ 0xff825c, SIZE_WORD, Video_Color14_ReadWord, Video_Color14_WriteWord },		/* COLOR 14 */
	{ 0xff825e, SIZE_WORD, Video_Color15_ReadWord, Video_Color15_WriteWord },		/* COLOR 15 */
	{ 0xff8260, SIZE_BYTE, Video_Res_ReadByte, Video_Res_WriteByte },
	{ 0xff8261, 3,         IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus errors here : return 0 not ff */
	{ 0xff8264, SIZE_BYTE, Video_HorScroll_Read_8264, Video_HorScroll_Write_8264 },		/* STE horizontal fine scrolling (no prefetch) */
	{ 0xff8265, SIZE_BYTE, Video_HorScroll_Read_8265, Video_HorScroll_Write_8265 },		/* STE horizontal fine scrolling */
	{ 0xff8266, 26,        IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus errors here : return 0 not ff */

	{ 0xff8604, SIZE_WORD, FDC_DiskControllerStatus_ReadWord, FDC_DiskController_WriteWord },
	{ 0xff8606, SIZE_WORD, FDC_DmaStatus_ReadWord, FDC_DmaModeControl_WriteWord },
	{ 0xff8608, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8609, SIZE_BYTE, FDC_DmaAddress_ReadByte, FDC_DmaAddress_WriteByte },		/* DMA base and counter high byte */
	{ 0xff860a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff860b, SIZE_BYTE, FDC_DmaAddress_ReadByte, FDC_DmaAddress_WriteByte },		/* DMA base and counter med byte  */
	{ 0xff860c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff860d, SIZE_BYTE, FDC_DmaAddress_ReadByte, FDC_DmaAddress_WriteByte },		/* DMA base and counter low byte  */
	{ 0xff860e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff860f, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */

	{ 0xff8800, SIZE_BYTE, PSG_ff8800_ReadByte, PSG_ff8800_WriteByte },
	{ 0xff8801, SIZE_BYTE, PSG_ff880x_ReadByte, PSG_ff8801_WriteByte },
	{ 0xff8802, SIZE_BYTE, PSG_ff880x_ReadByte, PSG_ff8802_WriteByte },
	{ 0xff8803, SIZE_BYTE, PSG_ff880x_ReadByte, PSG_ff8803_WriteByte },

	{ 0xff8900, SIZE_WORD, DmaSnd_SoundControl_ReadWord, DmaSnd_SoundControl_WriteWord },   /* DMA sound control */
	{ 0xff8902, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8903, SIZE_BYTE, IoMem_ReadWithoutInterception, DmaSnd_FrameStartHigh_WriteByte },/* DMA sound frame start high */
	{ 0xff8904, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8905, SIZE_BYTE, IoMem_ReadWithoutInterception, DmaSnd_FrameStartMed_WriteByte }, /* DMA sound frame start med */
	{ 0xff8906, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8907, SIZE_BYTE, IoMem_ReadWithoutInterception, DmaSnd_FrameStartLow_WriteByte }, /* DMA sound frame start low */
	{ 0xff8908, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8909, SIZE_BYTE, DmaSnd_FrameCountHigh_ReadByte, DmaSnd_FrameCountHigh_WriteByte },/* DMA sound frame count high */
	{ 0xff890a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff890b, SIZE_BYTE, DmaSnd_FrameCountMed_ReadByte, DmaSnd_FrameCountMed_WriteByte }, /* DMA sound frame count med */
	{ 0xff890c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff890d, SIZE_BYTE, DmaSnd_FrameCountLow_ReadByte, DmaSnd_FrameCountLow_WriteByte }, /* DMA sound frame count low */
	{ 0xff890e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff890f, SIZE_BYTE, IoMem_ReadWithoutInterception, DmaSnd_FrameEndHigh_WriteByte },  /* DMA sound frame end high */
	{ 0xff8910, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8911, SIZE_BYTE, IoMem_ReadWithoutInterception, DmaSnd_FrameEndMed_WriteByte },   /* DMA sound frame end med */
	{ 0xff8912, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8913, SIZE_BYTE, IoMem_ReadWithoutInterception, DmaSnd_FrameEndLow_WriteByte },   /* DMA sound frame end low */
	{ 0xff8914, 12,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */
	{ 0xff8920, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA sound mode control (contains 0) */
	{ 0xff8921, SIZE_BYTE, DmaSnd_SoundModeCtrl_ReadByte, DmaSnd_SoundModeCtrl_WriteByte }, /* DMA sound mode control */
	{ 0xff8922, SIZE_WORD, DmaSnd_MicrowireData_ReadWord, DmaSnd_MicrowireData_WriteWord }, /* Microwire data */
	{ 0xff8924, SIZE_WORD, DmaSnd_MicrowireMask_ReadWord, DmaSnd_MicrowireMask_WriteWord }, /* Microwire mask */
	{ 0xff8926, 26,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xff8a00, SIZE_WORD, Blitter_Halftone00_ReadWord, Blitter_Halftone00_WriteWord }, /* Blitter halftone RAM 0 */
	{ 0xff8a02, SIZE_WORD, Blitter_Halftone01_ReadWord, Blitter_Halftone01_WriteWord }, /* Blitter halftone RAM 1 */
	{ 0xff8a04, SIZE_WORD, Blitter_Halftone02_ReadWord, Blitter_Halftone02_WriteWord }, /* Blitter halftone RAM 2 */
	{ 0xff8a06, SIZE_WORD, Blitter_Halftone03_ReadWord, Blitter_Halftone03_WriteWord }, /* Blitter halftone RAM 3 */
	{ 0xff8a08, SIZE_WORD, Blitter_Halftone04_ReadWord, Blitter_Halftone04_WriteWord }, /* Blitter halftone RAM 4 */
	{ 0xff8a0a, SIZE_WORD, Blitter_Halftone05_ReadWord, Blitter_Halftone05_WriteWord }, /* Blitter halftone RAM 5 */
	{ 0xff8a0c, SIZE_WORD, Blitter_Halftone06_ReadWord, Blitter_Halftone06_WriteWord }, /* Blitter halftone RAM 6 */
	{ 0xff8a0e, SIZE_WORD, Blitter_Halftone07_ReadWord, Blitter_Halftone07_WriteWord }, /* Blitter halftone RAM 7 */
	{ 0xff8a10, SIZE_WORD, Blitter_Halftone08_ReadWord, Blitter_Halftone08_WriteWord }, /* Blitter halftone RAM 8 */
	{ 0xff8a12, SIZE_WORD, Blitter_Halftone09_ReadWord, Blitter_Halftone09_WriteWord }, /* Blitter halftone RAM 9 */
	{ 0xff8a14, SIZE_WORD, Blitter_Halftone10_ReadWord, Blitter_Halftone10_WriteWord }, /* Blitter halftone RAM 10 */
	{ 0xff8a16, SIZE_WORD, Blitter_Halftone11_ReadWord, Blitter_Halftone11_WriteWord }, /* Blitter halftone RAM 11 */
	{ 0xff8a18, SIZE_WORD, Blitter_Halftone12_ReadWord, Blitter_Halftone12_WriteWord }, /* Blitter halftone RAM 12 */
	{ 0xff8a1a, SIZE_WORD, Blitter_Halftone13_ReadWord, Blitter_Halftone13_WriteWord }, /* Blitter halftone RAM 13 */
	{ 0xff8a1c, SIZE_WORD, Blitter_Halftone14_ReadWord, Blitter_Halftone14_WriteWord }, /* Blitter halftone RAM 14 */
	{ 0xff8a1e, SIZE_WORD, Blitter_Halftone15_ReadWord, Blitter_Halftone15_WriteWord }, /* Blitter halftone RAM 15 */
	{ 0xff8a20, SIZE_WORD, Blitter_SourceXInc_ReadWord, Blitter_SourceXInc_WriteWord }, /* Blitter source x increment */
	{ 0xff8a22, SIZE_WORD, Blitter_SourceYInc_ReadWord, Blitter_SourceYInc_WriteWord }, /* Blitter source y increment */
	{ 0xff8a24, SIZE_LONG, Blitter_SourceAddr_ReadLong, Blitter_SourceAddr_WriteLong },     /* Blitter source address */
	{ 0xff8a28, SIZE_WORD, Blitter_Endmask1_ReadWord, Blitter_Endmask1_WriteWord },
	{ 0xff8a2a, SIZE_WORD, Blitter_Endmask2_ReadWord, Blitter_Endmask2_WriteWord },
	{ 0xff8a2c, SIZE_WORD, Blitter_Endmask3_ReadWord, Blitter_Endmask3_WriteWord },
	{ 0xff8a2e, SIZE_WORD, Blitter_DestXInc_ReadWord, Blitter_DestXInc_WriteWord }, /* Blitter dest. x increment */
	{ 0xff8a30, SIZE_WORD, Blitter_DestYInc_ReadWord, Blitter_DestYInc_WriteWord }, /* Blitter dest. y increment */
	{ 0xff8a32, SIZE_LONG, Blitter_DestAddr_ReadLong, Blitter_DestAddr_WriteLong },
	{ 0xff8a36, SIZE_WORD, Blitter_WordsPerLine_ReadWord, Blitter_WordsPerLine_WriteWord },
	{ 0xff8a38, SIZE_WORD, Blitter_LinesPerBitblock_ReadWord, Blitter_LinesPerBitblock_WriteWord },
	{ 0xff8a3a, SIZE_BYTE, Blitter_HalftoneOp_ReadByte, Blitter_HalftoneOp_WriteByte },
	{ 0xff8a3b, SIZE_BYTE, Blitter_LogOp_ReadByte, Blitter_LogOp_WriteByte },
	{ 0xff8a3c, SIZE_BYTE, Blitter_Control_ReadByte, Blitter_Control_WriteByte },
	{ 0xff8a3d, SIZE_BYTE, Blitter_Skew_ReadByte, Blitter_Skew_WriteByte },
	{ 0xff8a3e, SIZE_WORD, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */

	{ 0xff9000, SIZE_WORD, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff9200, SIZE_WORD, Joy_StePadButtons_DIPSwitches_ReadWord, Joy_StePadButtons_DIPSwitches_WriteWord },    /* Joypad fire buttons + MegaSTE DIP Switches */
	{ 0xff9202, SIZE_WORD, Joy_StePadMulti_ReadWord, Joy_StePadMulti_WriteWord },           /* Joypad directions/buttons/selection */
	{ 0xff9211, SIZE_BYTE, Joy_StePadAnalog0X_ReadByte, IoMem_WriteWithoutInterception }, /* Joypad 0 Analog/Paddle X position */
	{ 0xff9213, SIZE_BYTE, Joy_StePadAnalog0Y_ReadByte, IoMem_WriteWithoutInterception }, /* Joypad 0 Analog/Paddle Y position */
	{ 0xff9215, SIZE_BYTE, Joy_StePadAnalog1X_ReadByte, IoMem_WriteWithoutInterception }, /* Joypad 1 Analog/Paddle X position */
	{ 0xff9217, SIZE_BYTE, Joy_StePadAnalog1Y_ReadByte, IoMem_WriteWithoutInterception }, /* Joypad 1 Analog/Paddle Y position */
	{ 0xff9220, SIZE_WORD, Joy_SteLightpenX_ReadWord, IoMem_WriteWithoutInterception },     /* Lightpen X position */
	{ 0xff9222, SIZE_WORD, Joy_SteLightpenY_ReadWord, IoMem_WriteWithoutInterception },     /* Lightpen Y position */

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

	{ 0xfffa27, SIZE_BYTE, RS232_SCR_ReadByte, RS232_SCR_WriteByte },   /* Sync character register */
	{ 0xfffa29, SIZE_BYTE, RS232_UCR_ReadByte, RS232_UCR_WriteByte },   /* USART control register */
	{ 0xfffa2b, SIZE_BYTE, RS232_RSR_ReadByte, RS232_RSR_WriteByte },   /* Receiver status register */
	{ 0xfffa2d, SIZE_BYTE, RS232_TSR_ReadByte, RS232_TSR_WriteByte },   /* Transmitter status register */
	{ 0xfffa2f, SIZE_BYTE, RS232_UDR_ReadByte, RS232_UDR_WriteByte },   /* USART data register */

	{ 0xfffa31, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffa33, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffa35, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffa37, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffa39, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffa3b, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffa3d, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffa3f, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */

	{ 0xfffc00, SIZE_BYTE, ACIA_IKBD_Read_SR, ACIA_IKBD_Write_CR },
	{ 0xfffc01, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc02, SIZE_BYTE, ACIA_IKBD_Read_RDR, ACIA_IKBD_Write_TDR },
	{ 0xfffc03, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc04, SIZE_BYTE, Midi_Control_ReadByte, Midi_Control_WriteByte },
	{ 0xfffc05, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc06, SIZE_BYTE, Midi_Data_ReadByte, Midi_Data_WriteByte },
	{ 0xfffc07, 26,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xfffc21, SIZE_BYTE, Rtc_SecondsUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc22, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc23, SIZE_BYTE, Rtc_SecondsTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc24, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc25, SIZE_BYTE, Rtc_MinutesUnits_ReadByte, Rtc_MinutesUnits_WriteByte },
	{ 0xfffc26, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc27, SIZE_BYTE, Rtc_MinutesTens_ReadByte, Rtc_MinutesTens_WriteByte },
	{ 0xfffc28, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc29, SIZE_BYTE, Rtc_HoursUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc2a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc2b, SIZE_BYTE, Rtc_HoursTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc2c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc2d, SIZE_BYTE, Rtc_Weekday_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc2e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc2f, SIZE_BYTE, Rtc_DayUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc30, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc31, SIZE_BYTE, Rtc_DayTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc32, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc33, SIZE_BYTE, Rtc_MonthUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc34, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc35, SIZE_BYTE, Rtc_MonthTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc36, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc37, SIZE_BYTE, Rtc_YearUnits_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc38, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc39, SIZE_BYTE, Rtc_YearTens_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xfffc3a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc3b, SIZE_BYTE, Rtc_ClockMod_ReadByte, Rtc_ClockMod_WriteByte },
	{ 0xfffc3c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc3d, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Clock test */
	{ 0xfffc3e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc3f, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Clock reset */
	{ 0xfffc40, 448,       IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0, 0, NULL, NULL }
};
