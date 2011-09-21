/*
  Hatari - fdc.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FDC_H
#define HATARI_FDC_H


extern Uint16 DMAModeControl_ff8606wr;

extern void FDC_AcknowledgeInterrupt(void);
extern void FDC_DiskController_WriteWord(void);
extern void FDC_DiskControllerStatus_ReadWord(void);
extern void FDC_DmaModeControl_WriteWord(void);
extern void FDC_DmaStatus_ReadWord(void);
extern void FDC_DmaAddress_ReadByte(void);
extern void FDC_DmaAddress_WriteByte(void);

extern void FDC_FloppyMode_ReadByte(void);
extern void FDC_FloppyMode_WriteByte(void);
extern void FDC_GpipRead(void);
extern void FDC_InterruptHandler_Update(void);
extern void FDC_MemorySnapShot_Capture(bool bSave);
extern Uint32 FDC_GetDMAAddress(void);
extern void FDC_Reset(void);
extern void FDC_SetDMAStatus(bool bError);
extern void FDC_WriteDMAAddress(Uint32 Address);


#endif /* ifndef HATARI_FDC_H */
