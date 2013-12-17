/*
  Hatari - fdc.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FDC_H
#define HATARI_FDC_H


extern void	FDC_MemorySnapShot_Capture ( bool bSave );
extern void	FDC_Init ( void );
extern void	FDC_Reset ( bool bCold );
extern void	FDC_SetDMAStatus ( bool bError );

extern void	FDC_SetIRQ ( void );
extern void	FDC_ClearIRQ ( void );
extern void	FDC_InterruptHandler_Update ( void );

extern void	FDC_SetDriveLedBusy ( Uint8 SR );
extern void	FDC_EnableDrive ( int Drive , bool value );
extern void	FDC_InsertFloppy ( int Drive );
extern void	FDC_EjectFloppy ( int Drive );
extern void	FDC_SetDriveSide ( Uint8 io_porta_old , Uint8 io_porta_new );

extern void	FDC_DiskController_WriteWord ( void );
extern void	FDC_DiskControllerStatus_ReadWord ( void );
extern void	FDC_DmaModeControl_WriteWord ( void );
extern void	FDC_DmaStatus_ReadWord ( void );
extern int	FDC_DMA_GetModeControl_R_WR ( void );
extern void	FDC_DMA_FIFO_Push ( Uint8 Byte );
extern Uint8	FDC_DMA_FIFO_Pull ( void );

extern void	FDC_DmaAddress_ReadByte ( void );
extern void	FDC_DmaAddress_WriteByte ( void );
extern Uint32	FDC_GetDMAAddress ( void );
extern void	FDC_WriteDMAAddress ( Uint32 Address );

extern void	FDC_FloppyMode_ReadByte ( void );
extern void	FDC_FloppyMode_WriteByte ( void );

#endif /* ifndef HATARI_FDC_H */
