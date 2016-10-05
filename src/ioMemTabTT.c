/*
  Hatari - ioMemTabTT.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Table with hardware IO handlers for the TT.

  NOTE [NP] : contrary to some unofficial documentations, the TT doesn't have
  hardware scrolling similar to the STE. As such, registers FF820E,
  FF820F, FF8264 and FF8265 are not available and seem to return undefined values
  based on the data last seen on the bus (this would need more tests on a TT)
	move.b $ff820e,d0  -> FF
	move.b $ff820f,d0  -> 01
	move.b $ff8264,d0  -> 82
	move.b $ff8265,d0  -> 65
*/
const char IoMemTabTT_fileid[] = "Hatari ioMemTabTT.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dmaSnd.h"
#include "fdc.h"
#include "acia.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "joy.h"
#include "mfp.h"
#include "midi.h"
#include "nvram.h"
#include "psg.h"
#include "rs232.h"
#include "rtc.h"
#include "screen.h"
#include "video.h"
#include "blitter.h"

/**
 * The register at $FF9200.b represents the DIP switches from the
 * TT motherboard. The meaning of the switches is as follows:
 *
 *   1      off (on = CaTTamaran installed, not an official setting)
 *   2 - 6  off
 *   7      on = 1.4mb HD floppy drive fitted
 *   8      off (on = Disables the DMA sound hardware)
 *
 * Switch 1 is represented by the lowest bit in the $FF9200 register,
 * and switch 8 is represented by the highest bit. Logic is inverted,
 * i.e. when the switch is "on", the bit is 0.
 */
static void IoMemTabTT_ReadDIPSwitches(void)
{
	IoMem_WriteWord(0xff9200, 0xbf00);
}

/**
 * The value in the upper 7 bits of VME GPIO register 1 is used by TOS 3.06 as
 * some kind of boot partition selector when jumping into the boot sector of an
 * ACSI disk. These bits should be 0 so that AHDI detects the partitions right.
 * The lowest bit in this register seem to trigger something in the memory
 * detection algorithm of TOS ... when it is 0, TOS crashes when Hatari is
 * running with bAddressSpace24 == true.
 */
static void IoMemTabTT_ReadVmeGP1(void)
{
	IoMem_WriteByte(0xff8e09, 0x01);
}

/**
 * List of functions to handle read/write hardware interceptions for a TT.
 */
const INTERCEPT_ACCESS_FUNC IoMemTable_TT[] =
{
	{ 0xff8000, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8001, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Memory configuration */

	{ 0xff8200, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8201, SIZE_BYTE, IoMem_ReadWithoutInterception, Video_ScreenBase_WriteByte },	/* Video base high byte */
	{ 0xff8202, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8203, SIZE_BYTE, IoMem_ReadWithoutInterception, Video_ScreenBase_WriteByte },	/* Video base med byte */
	{ 0xff8204, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8205, SIZE_BYTE, Video_ScreenCounter_ReadByte, Video_ScreenCounter_WriteByte },
	{ 0xff8206, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8207, SIZE_BYTE, Video_ScreenCounter_ReadByte, Video_ScreenCounter_WriteByte },
	{ 0xff8208, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8209, SIZE_BYTE, Video_ScreenCounter_ReadByte, Video_ScreenCounter_WriteByte },
	{ 0xff820a, SIZE_BYTE, Video_Sync_ReadByte, Video_Sync_WriteByte },
	{ 0xff820b, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here : return 0 not ff */
	{ 0xff820c, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here : return 0 not ff */
	{ 0xff820d, SIZE_BYTE, Video_BaseLow_ReadByte, Video_ScreenBase_WriteByte },
	{ 0xff820e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff820f, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8240, 16*SIZE_WORD, IoMem_ReadWithoutInterception, Video_TTColorRegs_STRegWrite },        /* 16 TT ST-palette entries */
	{ 0xff8260, SIZE_BYTE, Video_Res_ReadByte, Video_Res_WriteByte },
	{ 0xff8261, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus errors here : return 0 not ff */
	{ 0xff8262, SIZE_WORD, IoMem_ReadWithoutInterception, Video_TTShiftMode_WriteWord },    /* TT screen mode */
	{ 0xff8264, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception },
	{ 0xff8265, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Horizontal fine scrolling */
	{ 0xff8266, 26,        IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus errors here : return 0 not ff */

	{ 0xff8400, 512,       IoMem_ReadWithoutInterception, Video_TTColorRegs_Write },        /* 256 TT palette entries */

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

	{ 0xff8780, 16, IoMem_VoidRead_00, IoMem_WriteWithoutInterception },                    /* TT SCSI controller */

	{ 0xff8800, SIZE_BYTE, PSG_ff8800_ReadByte, PSG_ff8800_WriteByte },
	{ 0xff8802, SIZE_BYTE, PSG_ff880x_ReadByte, PSG_ff8802_WriteByte },

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
	{ 0xff8920, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* DMA sound mode control (contains 0) */
	{ 0xff8921, SIZE_BYTE, DmaSnd_SoundModeCtrl_ReadByte, DmaSnd_SoundModeCtrl_WriteByte }, /* DMA sound mode control */
	{ 0xff8922, SIZE_WORD, DmaSnd_MicrowireData_ReadWord, DmaSnd_MicrowireData_WriteWord }, /* Microwire data */
	{ 0xff8924, SIZE_WORD, DmaSnd_MicrowireMask_ReadWord, DmaSnd_MicrowireMask_WriteWord }, /* Microwire mask */
	{ 0xff8926, 26,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xff8961, SIZE_BYTE, NvRam_Select_ReadByte, NvRam_Select_WriteByte },                 /* NVRAM/RTC chip */
	{ 0xff8963, SIZE_BYTE, NvRam_Data_ReadByte, NvRam_Data_WriteByte },                     /* NVRAM/RTC chip */

	/* Note: The TT does not have a blitter (0xff8a00 - 0xff8a3e) */

	//{ 0xff8c80, 8, IoMem_VoidRead, IoMem_WriteWithoutInterception },         /* SCC */

	{ 0xff8e01, 1, IoMem_VoidRead, IoMem_WriteWithoutInterception },         /* VME bus sys int mask */
	{ 0xff8e03, 1, IoMem_VoidRead, IoMem_WriteWithoutInterception },         /* VME bus sys int state */
	{ 0xff8e05, 1, IoMem_VoidRead, IoMem_WriteWithoutInterception },         /* VME bus sys interrupter */
	{ 0xff8e07, 1, IoMem_VoidRead, IoMem_WriteWithoutInterception },         /* VME bus vme interrupter */
	{ 0xff8e09, 1, IoMemTabTT_ReadVmeGP1, IoMem_WriteWithoutInterception },  /* VME bus GPIO 1 */
	{ 0xff8e0b, 1, IoMem_VoidRead_00, IoMem_WriteWithoutInterception },      /* VME bus GPIO 2 */
	{ 0xff8e0d, 1, IoMem_VoidRead, IoMem_WriteWithoutInterception },         /* VME bus vme int mask */
	{ 0xff8e0f, 1, IoMem_VoidRead, IoMem_WriteWithoutInterception },         /* VME bus vme int state */

	{ 0xff9000, SIZE_WORD, IoMem_VoidRead, IoMem_VoidWrite },                /* No bus error here */
	{ 0xff9200, SIZE_WORD, IoMemTabTT_ReadDIPSwitches, IoMem_VoidWrite },    /* DIP switches */

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

	{ 0xfffa80, 48, IoMem_VoidRead, IoMem_WriteWithoutInterception },  /* 2nd TT MFP */

	{ 0xfffc00, SIZE_BYTE, ACIA_IKBD_Read_SR, ACIA_IKBD_Write_CR },
	{ 0xfffc02, SIZE_BYTE, ACIA_IKBD_Read_RDR, ACIA_IKBD_Write_TDR },
	{ 0xfffc04, SIZE_BYTE, Midi_Control_ReadByte, Midi_Control_WriteByte },
	{ 0xfffc06, SIZE_BYTE, Midi_Data_ReadByte, Midi_Data_WriteByte },
	{ 0xfffc08, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc0a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc0c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc0e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */

	{ 0, 0, NULL, NULL }
};
