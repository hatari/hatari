/*
  Hatari - ioMemTables.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Tables with hardware IO handlers.
*/
char IoMemTables_rcsid[] = "Hatari $Id: ioMemTables.c,v 1.9 2005-09-13 01:10:09 thothy Exp $";

#include "main.h"
#include "dmaSnd.h"
#include "fdc.h"
#include "ikbd.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "mfp.h"
#include "midi.h"
#include "psg.h"
#include "rs232.h"
#include "rtc.h"
#include "video.h"
#include "blitter.h"


/*-----------------------------------------------------------------------*/
/*
  List of functions to handle read/write hardware interceptions for a plain ST.
*/
INTERCEPT_ACCESS_FUNC IoMemTable_ST[] =
{
	{ 0xff8001, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Memory configuration */

	{ 0xff8201, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Video base high byte */
	{ 0xff8203, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Video base med byte */
	{ 0xff8205, SIZE_BYTE, Video_ScreenCounterHigh_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8207, SIZE_BYTE, Video_ScreenCounterMed_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8209, SIZE_BYTE, Video_ScreenCounterLow_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff820a, SIZE_BYTE, Video_Sync_ReadByte, Video_Sync_WriteByte },
	{ 0xff820b, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff820d, SIZE_BYTE, Video_BaseLow_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff820f, SIZE_BYTE, Video_LineWidth_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8240, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color0_WriteWord },         /* COLOR 0 */
	{ 0xff8242, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color1_WriteWord },         /* COLOR 1 */
	{ 0xff8244, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color2_WriteWord },         /* COLOR 2 */
	{ 0xff8246, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color3_WriteWord },         /* COLOR 3 */
	{ 0xff8248, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color4_WriteWord },         /* COLOR 4 */
	{ 0xff824a, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color5_WriteWord },         /* COLOR 5 */
	{ 0xff824c, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color6_WriteWord },         /* COLOR 6 */
	{ 0xff824e, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color7_WriteWord },         /* COLOR 7 */
	{ 0xff8250, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color8_WriteWord },         /* COLOR 8 */
	{ 0xff8252, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color9_WriteWord },         /* COLOR 9 */
	{ 0xff8254, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color10_WriteWord },        /* COLOR 10 */
	{ 0xff8256, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color11_WriteWord },        /* COLOR 11 */
	{ 0xff8258, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color12_WriteWord },        /* COLOR 12 */
	{ 0xff825a, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color13_WriteWord },        /* COLOR 13 */
	{ 0xff825c, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color14_WriteWord },        /* COLOR 14 */
	{ 0xff825e, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color15_WriteWord },        /* COLOR 15 */
	{ 0xff8260, SIZE_BYTE, Video_ShifterMode_ReadByte, Video_ShifterMode_WriteByte },
	{ 0xff8261, 31,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xff8604, SIZE_WORD, FDC_DiskControllerStatus_ReadWord, FDC_DiskController_WriteWord },
	{ 0xff8606, SIZE_WORD, FDC_DmaStatus_ReadWord, FDC_DmaModeControl_WriteWord },
	{ 0xff8609, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA base and counter high byte */
	{ 0xff860b, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA base and counter med byte  */
	{ 0xff860d, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA base and counter low byte  */
	{ 0xff860f, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */

	{ 0xff8800, SIZE_BYTE, PSG_SelectRegister_ReadByte, PSG_SelectRegister_WriteByte },
	{ 0xff8801, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },
	{ 0xff8802, SIZE_BYTE, PSG_DataRegister_ReadByte, PSG_DataRegister_WriteByte },
	{ 0xff8803, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },

	{ 0xff8a00, 32,        IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter halftone RAM */
	{ 0xff8a20, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter source x increment */
	{ 0xff8a22, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter source y increment */
	{ 0xff8a24, SIZE_LONG, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter source address */
	{ 0xff8a28, SIZE_WORD, Blitter_Endmask1_ReadWord, Blitter_Endmask1_WriteWord },
	{ 0xff8a2a, SIZE_WORD, Blitter_Endmask2_ReadWord, Blitter_Endmask2_WriteWord },
	{ 0xff8a2c, SIZE_WORD, Blitter_Endmask3_ReadWord, Blitter_Endmask3_WriteWord },
	{ 0xff8a2e, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter dest. x increment */
	{ 0xff8a30, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter dest. y increment */
	{ 0xff8a32, SIZE_LONG, Blitter_DestAddr_ReadLong, Blitter_DestAddr_WriteLong },
	{ 0xff8a36, SIZE_WORD, Blitter_WordsPerLine_ReadWord, Blitter_WordsPerLine_WriteWord },
	{ 0xff8a38, SIZE_WORD, Blitter_LinesPerBitblock_ReadWord, Blitter_LinesPerBitblock_WriteWord },
	{ 0xff8a3a, SIZE_BYTE, Blitter_HalftoneOp_ReadByte, Blitter_HalftoneOp_WriteByte },
	{ 0xff8a3b, SIZE_BYTE, Blitter_LogOp_ReadByte, Blitter_LogOp_WriteByte },
	{ 0xff8a3c, SIZE_BYTE, Blitter_LineNum_ReadByte, Blitter_LineNum_WriteByte },
	{ 0xff8a3d, SIZE_BYTE, Blitter_Skew_ReadByte, Blitter_Skew_WriteByte },

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

	{ 0xfffc00, SIZE_BYTE, IKBD_KeyboardControl_ReadByte, IKBD_KeyboardControl_WriteByte },
	{ 0xfffc01, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc02, SIZE_BYTE, IKBD_KeyboardData_ReadByte, IKBD_KeyboardData_WriteByte },
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


/*-----------------------------------------------------------------------*/
/*
  List of functions to handle read/write hardware interceptions for a STE.
*/
INTERCEPT_ACCESS_FUNC IoMemTable_STE[] =
{
	{ 0xff8000, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8001, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Memory configuration */
	{ 0xff8002, 14,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xff8200, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8201, SIZE_BYTE, IoMem_ReadWithoutInterception, Video_ScreenBaseSTE_WriteByte },  /* Video base high byte */
	{ 0xff8202, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8203, SIZE_BYTE, IoMem_ReadWithoutInterception, Video_ScreenBaseSTE_WriteByte },  /* Video base med byte */
	{ 0xff8204, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8205, SIZE_BYTE, Video_ScreenCounterHigh_ReadByte, Video_ScreenCounter_WriteByte },
	{ 0xff8206, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8207, SIZE_BYTE, Video_ScreenCounterMed_ReadByte, Video_ScreenCounter_WriteByte },
	{ 0xff8208, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8209, SIZE_BYTE, Video_ScreenCounterLow_ReadByte, Video_ScreenCounter_WriteByte },
	{ 0xff820a, SIZE_BYTE, Video_Sync_ReadByte, Video_Sync_WriteByte },
	{ 0xff820b, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff820c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff820d, SIZE_BYTE, Video_BaseLow_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff820e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff820f, SIZE_BYTE, Video_LineWidth_ReadByte, IoMem_WriteWithoutInterception },
	{ 0xff8240, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color0_WriteWord },         /* COLOR 0 */
	{ 0xff8242, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color1_WriteWord },         /* COLOR 1 */
	{ 0xff8244, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color2_WriteWord },         /* COLOR 2 */
	{ 0xff8246, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color3_WriteWord },         /* COLOR 3 */
	{ 0xff8248, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color4_WriteWord },         /* COLOR 4 */
	{ 0xff824a, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color5_WriteWord },         /* COLOR 5 */
	{ 0xff824c, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color6_WriteWord },         /* COLOR 6 */
	{ 0xff824e, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color7_WriteWord },         /* COLOR 7 */
	{ 0xff8250, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color8_WriteWord },         /* COLOR 8 */
	{ 0xff8252, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color9_WriteWord },         /* COLOR 9 */
	{ 0xff8254, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color10_WriteWord },        /* COLOR 10 */
	{ 0xff8256, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color11_WriteWord },        /* COLOR 11 */
	{ 0xff8258, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color12_WriteWord },        /* COLOR 12 */
	{ 0xff825a, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color13_WriteWord },        /* COLOR 13 */
	{ 0xff825c, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color14_WriteWord },        /* COLOR 14 */
	{ 0xff825e, SIZE_WORD, IoMem_ReadWithoutInterception, Video_Color15_WriteWord },        /* COLOR 15 */
	{ 0xff8260, SIZE_BYTE, Video_ShifterMode_ReadByte, Video_ShifterMode_WriteByte },
	{ 0xff8261, 4,         IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */
	{ 0xff8265, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* STE horizontal fine scrolling */
	{ 0xff8266, 26,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xff8604, SIZE_WORD, FDC_DiskControllerStatus_ReadWord, FDC_DiskController_WriteWord },
	{ 0xff8606, SIZE_WORD, FDC_DmaStatus_ReadWord, FDC_DmaModeControl_WriteWord },
	{ 0xff8608, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8609, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA base and counter high byte */
	{ 0xff860a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff860b, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA base and counter med byte  */
	{ 0xff860c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff860d, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA base and counter low byte  */
	{ 0xff860e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff860f, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */

	{ 0xff8800, SIZE_BYTE, PSG_SelectRegister_ReadByte, PSG_SelectRegister_WriteByte },
	{ 0xff8801, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },
	{ 0xff8802, SIZE_BYTE, PSG_DataRegister_ReadByte, PSG_DataRegister_WriteByte },
	{ 0xff8803, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },

	{ 0xff8900, SIZE_WORD, DmaSnd_SoundControl_ReadWord, DmaSnd_SoundControl_WriteWord },   /* DMA sound control */
	{ 0xff8902, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8903, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA sound frame start high */
	{ 0xff8904, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8905, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA sound frame start med */
	{ 0xff8906, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8907, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA sound frame start low */
	{ 0xff8908, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8909, SIZE_BYTE, DmaSnd_FrameCountHigh_ReadByte, IoMem_VoidWrite },               /* DMA sound frame count high */
	{ 0xff890a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff890b, SIZE_BYTE, DmaSnd_FrameCountMed_ReadByte, IoMem_VoidWrite },                /* DMA sound frame count med */
	{ 0xff890c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff890d, SIZE_BYTE, DmaSnd_FrameCountLow_ReadByte, IoMem_VoidWrite },                /* DMA sound frame count low */
	{ 0xff890e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff890f, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA sound frame end high */
	{ 0xff8910, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8911, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA sound frame end med */
	{ 0xff8912, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8913, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA sound frame end low */
	{ 0xff8914, 12,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */
	{ 0xff8920, SIZE_WORD, DmaSnd_SoundMode_ReadWord, DmaSnd_SoundMode_WriteWord },         /* DMA sound mode control */
	{ 0xff8922, SIZE_WORD, DmaSnd_MicrowireData_ReadWord, IoMem_WriteWithoutInterception }, /* Microwire data */
	{ 0xff8924, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Microwire mask */
	{ 0xff8925, 27,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xff8a00, 32,        IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter halftone RAM */
	{ 0xff8a20, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter source x increment */
	{ 0xff8a22, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter source y increment */
	{ 0xff8a24, SIZE_LONG, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter source address */
	{ 0xff8a28, SIZE_WORD, Blitter_Endmask1_ReadWord, Blitter_Endmask1_WriteWord },
	{ 0xff8a2a, SIZE_WORD, Blitter_Endmask2_ReadWord, Blitter_Endmask2_WriteWord },
	{ 0xff8a2c, SIZE_WORD, Blitter_Endmask3_ReadWord, Blitter_Endmask3_WriteWord },
	{ 0xff8a2e, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter dest. x increment */
	{ 0xff8a30, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Blitter dest. y increment */
	{ 0xff8a32, SIZE_LONG, Blitter_DestAddr_ReadLong, Blitter_DestAddr_WriteLong },
	{ 0xff8a36, SIZE_WORD, Blitter_WordsPerLine_ReadWord, Blitter_WordsPerLine_WriteWord },
	{ 0xff8a38, SIZE_WORD, Blitter_LinesPerBitblock_ReadWord, Blitter_LinesPerBitblock_WriteWord },
	{ 0xff8a3a, SIZE_BYTE, Blitter_HalftoneOp_ReadByte, Blitter_HalftoneOp_WriteByte },
	{ 0xff8a3b, SIZE_BYTE, Blitter_LogOp_ReadByte, Blitter_LogOp_WriteByte },
	{ 0xff8a3c, SIZE_BYTE, Blitter_LineNum_ReadByte, Blitter_LineNum_WriteByte },
	{ 0xff8a3d, SIZE_BYTE, Blitter_Skew_ReadByte, Blitter_Skew_WriteByte },
	{ 0xff8a3e, SIZE_WORD, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */

	{ 0xff9000, SIZE_WORD, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff9201, SIZE_BYTE, IoMem_VoidRead, IoMem_WriteWithoutInterception }, /* Joypad fire buttons */
	{ 0xff9202, SIZE_WORD, IoMem_VoidRead, IoMem_WriteWithoutInterception }, /* Joypad input (?) */
	{ 0xff9211, SIZE_BYTE, IoMem_VoidRead, IoMem_WriteWithoutInterception }, /* Joypad 0 X position (?) */
	{ 0xff9213, SIZE_BYTE, IoMem_VoidRead, IoMem_WriteWithoutInterception }, /* Joypad 0 Y position (?) */
	{ 0xff9215, SIZE_BYTE, IoMem_VoidRead, IoMem_WriteWithoutInterception }, /* Joypad 1 X position (?) */
	{ 0xff9217, SIZE_BYTE, IoMem_VoidRead, IoMem_WriteWithoutInterception }, /* Joypad 1 Y position (?) */
	{ 0xff9220, SIZE_WORD, IoMem_VoidRead, IoMem_WriteWithoutInterception }, /* Lightpen X position */
	{ 0xff9222, SIZE_WORD, IoMem_VoidRead, IoMem_WriteWithoutInterception }, /* Lightpen Y position */

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

	{ 0xfffc00, SIZE_BYTE, IKBD_KeyboardControl_ReadByte, IKBD_KeyboardControl_WriteByte },
	{ 0xfffc01, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc02, SIZE_BYTE, IKBD_KeyboardData_ReadByte, IKBD_KeyboardData_WriteByte },
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
