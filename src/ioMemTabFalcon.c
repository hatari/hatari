/*
  Hatari - ioMemTabFalcon.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Table with hardware IO handlers for the Falcon.
*/
const char IoMemTabFalc_rcsid[] = "Hatari $Id: ioMemTabFalcon.c,v 1.13 2007-04-02 22:35:15 thothy Exp $";

#include "main.h"
#include "dmaSnd.h"
#include "fdc.h"
#include "ikbd.h"
#include "ioMem.h"
#include "ioMemTables.h"
#include "joy.h"
#include "mfp.h"
#include "midi.h"
#include "nvram.h"
#include "psg.h"
#include "rs232.h"
#include "rtc.h"
#include "video.h"
#include "blitter.h"
#include "falcon/videl.h"
#if ENABLE_DSP_EMU
#include "falcon/dsp.h"
#endif

/**
 * no DSP
 */
void IoMemTabFalcon_DSPnone(void (**readtab)(void), void (**writetab)(void))
{
	int i, offset;
	offset = 0xffa200 - 0xff8000;
	for (i = 0; i < 8; i++) {
		readtab[offset+i] = IoMem_ReadWithoutInterception;
	}
	readtab[offset+2] = IoMem_VoidRead;	/* TODO: why this is needed */
	for (i = 0; i < 8; i++) {
		writetab[offset+i] = IoMem_WriteWithoutInterception;
	}
}

/**
 *  Just a temporary hack - some programs are polling on this register and
 * are expecting the handshake bit (#7) to change after a while...
 */
static void DSP_DummyHostCommand_ReadByte(void)
{
	IoMem[0xffa201] ^= 0x80;
}

/**
 *  Just a temporary hack - some programs are polling on this register and
 * are expecting some bits to change after a while...
 */
static void DSP_DummyInterruptStatus_ReadByte(void)
{
	IoMem[0xffa202] ^= 0xff;
}

/**
 * dummy IO when DSP emulation is not enabled
 */
void IoMemTabFalcon_DSPdummy(void (**readtab)(void), void (**writetab)(void))
{
	int i, offset;
	offset = 0xffa200 - 0xff8000;
	readtab[offset+0] = IoMem_ReadWithoutInterception;
	readtab[offset+1] = DSP_DummyHostCommand_ReadByte;
	readtab[offset+2] = DSP_DummyInterruptStatus_ReadByte;
	for (i = 3; i < 8; i++) {
		readtab[offset+i] = IoMem_ReadWithoutInterception;
	}
	for (i = 0; i < 8; i++) {
		writetab[offset+i] = IoMem_WriteWithoutInterception;
	}
}

#if ENABLE_DSP_EMU
/**
 * enable DSP emulation
 */
void IoMemTabFalcon_DSPemulation(void (**readtab)(void), void (**writetab)(void))
{
	int i, offset;
	offset = 0xffa200 - 0xff8000;
	for (i = 0; i < 8; i++) {
		readtab[offset+i]  = DSP_HandleReadAccess;
		writetab[offset+i] = DSP_HandleWriteAccess;
	}
}
#endif


/*-----------------------------------------------------------------------*/
/*
  List of functions to handle read/write hardware interceptions for a Falcon.
  Note: This is not working at all yet!
*/
const INTERCEPT_ACCESS_FUNC IoMemTable_Falcon[] =
{
	{ 0xff8000, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8001, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Memory configuration */
	{ 0xff8006, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Falcon configuration */
	{ 0xff800C, SIZE_WORD, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8060, SIZE_LONG, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */

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
	{ 0xff820e, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Falcon line offset */
	{ 0xff820f, SIZE_BYTE, Video_LineWidth_ReadByte, Video_LineWidth_WriteByte },
	{ 0xff8210, SIZE_WORD, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Falcon line width */
	{ 0xff8212, 46, IoMem_VoidRead, IoMem_VoidWrite },                                      /* No bus error here */
	{ 0xff8240, 32, IoMem_ReadWithoutInterception, VIDEL_ColorRegsWrite },                  /* ST color regs */
	{ 0xff8260, SIZE_BYTE, Video_ShifterMode_ReadByte, Video_ShifterMode_WriteByte },
	{ 0xff8261, 3,         IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */
	{ 0xff8264, SIZE_BYTE, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* Falcon horizontal fine scrolling high ? */
	{ 0xff8265, SIZE_BYTE, Video_HorScroll_Read, Video_HorScroll_Write },                   /* horizontal fine scrolling */
	{ 0xff8266, SIZE_WORD, IoMem_ReadWithoutInterception, VIDEL_ShiftModeWriteWord },       /* Falcon shift mode */
	{ 0xff8268, 24,        IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xff8280, 68, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception }, /* TODO: Falcon video */

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
	{ 0xff8920, SIZE_WORD, DmaSnd_SoundMode_ReadWord, DmaSnd_SoundMode_WriteWord },         /* DMA sound mode control */
	{ 0xff8922, SIZE_WORD, DmaSnd_MicrowireData_ReadWord, DmaSnd_MicrowireData_WriteWord }, /* Microwire data */
	{ 0xff8924, SIZE_WORD, DmaSnd_MicrowireMask_ReadWord, DmaSnd_MicrowireMask_WriteWord }, /* Microwire mask */

	{ 0xff8930, 0x14, IoMem_ReadWithoutInterception, IoMem_WriteWithoutInterception },      /* Falcon DMA sound / DSP */

	{ 0xff8960, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8961, SIZE_BYTE, NvRam_Select_ReadByte, NvRam_Select_WriteByte },                 /* NVRAM/RTC chip */
	{ 0xff8962, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff8963, SIZE_BYTE, NvRam_Data_ReadByte, NvRam_Data_WriteByte },                     /* NVRAM/RTC chip */

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
	{ 0xff8a3c, SIZE_BYTE, Blitter_Control_ReadByte, Blitter_Control_WriteByte },
	{ 0xff8a3d, SIZE_BYTE, Blitter_Skew_ReadByte, Blitter_Skew_WriteByte },
	{ 0xff8a3e, SIZE_WORD, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */

	{ 0xff8c80, 8, IoMem_VoidRead, IoMem_WriteWithoutInterception },                        /* TODO: SCC */

	{ 0xff9200, SIZE_WORD, Joy_StePadButtons_ReadWord, IoMem_WriteWithoutInterception },    /* Joypad fire buttons */
	{ 0xff9202, SIZE_WORD, Joy_StePadMulti_ReadWord, Joy_StePadMulti_WriteWord },     /* Joypad directions/buttons/selection */
	{ 0xff9210, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff9211, SIZE_BYTE, IoMem_VoidRead, IoMem_WriteWithoutInterception },          /* Joypad 0 X position (?) */
	{ 0xff9212, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff9213, SIZE_BYTE, IoMem_VoidRead, IoMem_WriteWithoutInterception },          /* Joypad 0 Y position (?) */
	{ 0xff9214, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff9215, SIZE_BYTE, IoMem_VoidRead, IoMem_WriteWithoutInterception },          /* Joypad 1 X position (?) */
	{ 0xff9216, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xff9217, SIZE_BYTE, IoMem_VoidRead, IoMem_WriteWithoutInterception },          /* Joypad 1 Y position (?) */
	{ 0xff9220, SIZE_WORD, IoMem_VoidRead, IoMem_WriteWithoutInterception },              /* Lightpen X position */
	{ 0xff9222, SIZE_WORD, IoMem_VoidRead, IoMem_WriteWithoutInterception },              /* Lightpen Y position */

	{ 0xff9800, 0x400, IoMem_ReadWithoutInterception, VIDEL_ColorRegsWrite },            /* Falcon Videl palette */

	{ 0xfffa00, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa01, SIZE_BYTE, MFP_GPIP_ReadByte, MFP_GPIP_WriteByte },
	{ 0xfffa02, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa03, SIZE_BYTE, MFP_ActiveEdge_ReadByte, MFP_ActiveEdge_WriteByte },
	{ 0xfffa04, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa05, SIZE_BYTE, MFP_DataDirection_ReadByte, MFP_DataDirection_WriteByte },
	{ 0xfffa06, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa07, SIZE_BYTE, MFP_EnableA_ReadByte, MFP_EnableA_WriteByte },
	{ 0xfffa08, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa09, SIZE_BYTE, MFP_EnableB_ReadByte, MFP_EnableB_WriteByte },
	{ 0xfffa0a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa0b, SIZE_BYTE, MFP_PendingA_ReadByte, MFP_PendingA_WriteByte },
	{ 0xfffa0c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa0d, SIZE_BYTE, MFP_PendingB_ReadByte, MFP_PendingB_WriteByte },
	{ 0xfffa0e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa0f, SIZE_BYTE, MFP_InServiceA_ReadByte, MFP_InServiceA_WriteByte },
	{ 0xfffa10, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa11, SIZE_BYTE, MFP_InServiceB_ReadByte, MFP_InServiceB_WriteByte },
	{ 0xfffa12, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa13, SIZE_BYTE, MFP_MaskA_ReadByte, MFP_MaskA_WriteByte },
	{ 0xfffa14, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa15, SIZE_BYTE, MFP_MaskB_ReadByte, MFP_MaskB_WriteByte },
	{ 0xfffa16, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa17, SIZE_BYTE, MFP_VectorReg_ReadByte, MFP_VectorReg_WriteByte },
	{ 0xfffa18, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa19, SIZE_BYTE, MFP_TimerACtrl_ReadByte, MFP_TimerACtrl_WriteByte },
	{ 0xfffa1a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa1b, SIZE_BYTE, MFP_TimerBCtrl_ReadByte, MFP_TimerBCtrl_WriteByte },
	{ 0xfffa1c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa1d, SIZE_BYTE, MFP_TimerCDCtrl_ReadByte, MFP_TimerCDCtrl_WriteByte },
	{ 0xfffa1e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa1f, SIZE_BYTE, MFP_TimerAData_ReadByte, MFP_TimerAData_WriteByte },
	{ 0xfffa20, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa21, SIZE_BYTE, MFP_TimerBData_ReadByte, MFP_TimerBData_WriteByte },
	{ 0xfffa22, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa23, SIZE_BYTE, MFP_TimerCData_ReadByte, MFP_TimerCData_WriteByte },
	{ 0xfffa24, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa25, SIZE_BYTE, MFP_TimerDData_ReadByte, MFP_TimerDData_WriteByte },

	{ 0xfffa26, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa27, SIZE_BYTE, RS232_SCR_ReadByte, RS232_SCR_WriteByte },                 /* Sync character register */
	{ 0xfffa28, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa29, SIZE_BYTE, RS232_UCR_ReadByte, RS232_UCR_WriteByte },                  /* USART control register */
	{ 0xfffa2a, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa2b, SIZE_BYTE, RS232_RSR_ReadByte, RS232_RSR_WriteByte },                /* Receiver status register */
	{ 0xfffa2c, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa2d, SIZE_BYTE, RS232_TSR_ReadByte, RS232_TSR_WriteByte },             /* Transmitter status register */
	{ 0xfffa2e, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffa2f, SIZE_BYTE, RS232_UDR_ReadByte, RS232_UDR_WriteByte },                     /* USART data register */

	{ 0xfffc00, SIZE_BYTE, IKBD_KeyboardControl_ReadByte, IKBD_KeyboardControl_WriteByte },
	{ 0xfffc01, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc02, SIZE_BYTE, IKBD_KeyboardData_ReadByte, IKBD_KeyboardData_WriteByte },
	{ 0xfffc03, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc04, SIZE_BYTE, Midi_Control_ReadByte, Midi_Control_WriteByte },
	{ 0xfffc05, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus error here */
	{ 0xfffc06, SIZE_BYTE, Midi_Data_ReadByte, Midi_Data_WriteByte },
	{ 0xfffc07, SIZE_BYTE, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0xffff82, SIZE_WORD, IoMem_VoidRead, IoMem_VoidWrite },                               /* No bus errors here */

	{ 0, 0, NULL, NULL }
};
