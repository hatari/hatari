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
const char IoMemTabTT_fileid[] = "Hatari ioMemTabTT.c";

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
#include "ncr5380.h"
#include "nvram.h"
#include "psg.h"
#include "rs232.h"
#include "rtc.h"
#include "scc.h"
#include "video.h"
#include "stMemory.h"


/**
 * The register at $FF9200.b represents the DIP switches from the
 * TT motherboard. The meaning of the switches is as follows:
 *
 *   1      off (on = CaTTamaran installed, not an official setting)
 *   2 - 6  off
 *   7      on = 1.4mb HD floppy drive fitted
 *   8      off (on = disable the DMA sound hardware)
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
 * List of functions to handle read/write hardware interceptions for a TT.
 */
const INTERCEPT_ACCESS_FUNC IoMemTable_TT[] =
{
	{ 0xff8000, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8001, SIZE_BYTE, STMemory_MMU_Config_ReadByte, STMemory_MMU_Config_WriteByte },	/* Memory configuration */

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
	{ 0xff860e, SIZE_WORD, FDC_DensityMode_ReadWord , FDC_DensityMode_WriteWord },		/* Choose DD/HD mode */

	{ 0xff8700, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8701, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI DMA Address Pointer (Highest byte) */
	{ 0xff8702, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8703, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI DMA Address Pointer (High byte)    */
	{ 0xff8704, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8705, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI DMA Address Pointer (Low byte)     */
	{ 0xff8706, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8707, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI DMA Address Pointer (Lowest byte)  */
	{ 0xff8708, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8709, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI DMA Byte Count (Highest byte)      */
	{ 0xff870a, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff870b, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI DMA Byte Count (High byte)         */
	{ 0xff870c, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff870d, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI DMA Byte Count (Low byte)          */
	{ 0xff870e, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff870f, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI DMA Byte Count (Lowest byte)       */
	{ 0xff8710, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI Residue Data Register (High Word)  */
	{ 0xff8712, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCSI Residue Data Register (Low Word)   */
	{ 0xff8714, SIZE_WORD, IoMem_ReadWithoutInterception, Ncr5380_TT_DMA_Ctrl_WriteWord },  /* SCSI Control register                   */
	{ 0xff8716, 10, IoMem_VoidRead_00, IoMem_VoidWrite },                                   /* No bus error here */

	{ 0xff8780, 16, Ncr5380_IoMemTT_ReadByte, Ncr5380_IoMemTT_WriteByte },                  /* TT SCSI controller */

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

	{ 0xff8c00, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8c01, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC DMA Address Pointer (Highest byte) */
	{ 0xff8c02, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8c03, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC DMA Address Pointer (High byte)    */
	{ 0xff8c04, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8c05, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC DMA Address Pointer (Low byte)     */
	{ 0xff8c06, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8c07, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC DMA Address Pointer (Lowest byte)  */
	{ 0xff8c08, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8c09, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC DMA Byte Count (Highest byte)      */
	{ 0xff8c0a, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8c0b, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC DMA Byte Count (High byte)         */
	{ 0xff8c0c, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8c0d, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC DMA Byte Count (Low byte)          */
	{ 0xff8c0e, SIZE_BYTE, IoMem_VoidRead_00, IoMem_VoidWrite },                            /* No bus error here */
	{ 0xff8c0f, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC DMA Byte Count (Lowest byte)       */
	{ 0xff8c10, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC Residue Data Register (High Word)  */
	{ 0xff8c12, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* SCC Residue Data Register (Low Word)   */
	{ 0xff8c14, SIZE_WORD, IoMem_VoidRead_00, IoMem_WriteWithoutInterception },             /* SCC Control register                   */
	{ 0xff8c16, 10, IoMem_VoidRead_00, IoMem_VoidWrite },                                   /* No bus error here */

	{ 0xff8c80, 8, SCC_IoMem_ReadByte, SCC_IoMem_WriteByte },                               /* SCC */
	{ 0xff8c88, 8, IoMem_VoidRead_00, IoMem_VoidWrite },                                    /* No bus error here */

	/* VME/SCU 0xff8e01-0xff8e0f registers set at run-time in ioMem.c/vme.c for TT */

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

	{ 0xfffa81, SIZE_BYTE, MFP_GPIP_ReadByte, MFP_GPIP_WriteByte },			/* TT MFP GPIP */
	{ 0xfffa83, SIZE_BYTE, MFP_ActiveEdge_ReadByte, MFP_ActiveEdge_WriteByte },	/* TT MFP AER */
	{ 0xfffa85, SIZE_BYTE, MFP_DataDirection_ReadByte, MFP_DataDirection_WriteByte	/* TT MFP DDR */},
	{ 0xfffa87, SIZE_BYTE, MFP_EnableA_ReadByte, MFP_EnableA_WriteByte },		/* TT MFP IERA */
	{ 0xfffa89, SIZE_BYTE, MFP_EnableB_ReadByte, MFP_EnableB_WriteByte },		/* TT MFP IERB */
	{ 0xfffa8b, SIZE_BYTE, MFP_PendingA_ReadByte, MFP_PendingA_WriteByte },		/* TT MFP IPRA */
	{ 0xfffa8d, SIZE_BYTE, MFP_PendingB_ReadByte, MFP_PendingB_WriteByte },		/* TT MFP IPRB */
	{ 0xfffa8f, SIZE_BYTE, MFP_InServiceA_ReadByte, MFP_InServiceA_WriteByte },	/* TT MFP ISRA */
	{ 0xfffa91, SIZE_BYTE, MFP_InServiceB_ReadByte, MFP_InServiceB_WriteByte },	/* TT MFP ISRB */
	{ 0xfffa93, SIZE_BYTE, MFP_MaskA_ReadByte, MFP_MaskA_WriteByte },               /* TT MFP IMRA */
	{ 0xfffa95, SIZE_BYTE, MFP_MaskB_ReadByte, MFP_MaskB_WriteByte },		/* TT MFP IMRB */
	{ 0xfffa97, SIZE_BYTE, MFP_VectorReg_ReadByte, MFP_VectorReg_WriteByte },	/* TT MFP VR */
	{ 0xfffa99, SIZE_BYTE, MFP_TimerACtrl_ReadByte, MFP_TimerACtrl_WriteByte },	/* TT MFP TACR */
	{ 0xfffa9b, SIZE_BYTE, MFP_TimerBCtrl_ReadByte, MFP_TimerBCtrl_WriteByte },	/* TT MFP TBCR */
	{ 0xfffa9d, SIZE_BYTE, MFP_TimerCDCtrl_ReadByte, MFP_TimerCDCtrl_WriteByte },	/* TT MFP TCDCR */
	{ 0xfffa9f, SIZE_BYTE, MFP_TimerAData_ReadByte, MFP_TimerAData_WriteByte },	/* TT MFP TADR */
	{ 0xfffaa1, SIZE_BYTE, MFP_TimerBData_ReadByte, MFP_TimerBData_WriteByte },	/* TT MFP TBDR */
	{ 0xfffaa3, SIZE_BYTE, MFP_TimerCData_ReadByte, MFP_TimerCData_WriteByte },	/* TT MFP TCDR */
	{ 0xfffaa5, SIZE_BYTE, MFP_TimerDData_ReadByte, MFP_TimerDData_WriteByte },	/* TT MFP TDDR */
	{ 0xfffaa7, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },			/* TT MFP SCR */
	{ 0xfffaa9, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },			/* TT MFP UCR */
	{ 0xfffaab, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },			/* TT MFP RSR */
	{ 0xfffaad, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },			/* TT MFP TSR */
	{ 0xfffaaf, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },			/* TT MFP UDR */

	{ 0xfffab1, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffab3, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffab5, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffab7, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffab9, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffabb, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffabd, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */
	{ 0xfffabf, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },           /* No bus error here */

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
