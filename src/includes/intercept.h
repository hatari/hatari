/*
  Hatari - intercept.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_INTERCEPT_H
#define HATARI_INTERCEPT_H


#define  BUS_ERROR_ADDR  0xE00000           /* Address below here causes bus error exception */

#define INTERCEPT_WORKSPACE_SIZE  (8*1024)  /* 8k, size of intercept lists */

/* Hardware address details */
typedef struct
{
  unsigned int Address;        /* ST hardware address */
  int SpanInBytes;             /* SIZE_BYTE, SIZE_WORD or SIZE_LONG */
  void *ReadFunc;              /* Read function */
  void *WriteFunc;             /* Write function */
} INTERCEPT_ACCESS_FUNC;

/* List of hardware address which are not documented, ie STe, TT, Falcon locations - should be unconnected on STfm */
typedef struct
{
  unsigned int Start_Address;
  unsigned int End_Address;
} INTERCEPT_ADDRESSRANGE;


extern BOOL bEnableBlitter;


uae_u32 Intercept_ReadByte(uaecptr addr);
uae_u32 Intercept_ReadWord(uaecptr addr);
uae_u32 Intercept_ReadLong(uaecptr addr);

void Intercept_WriteByte(uaecptr addr, uae_u32 val);
void Intercept_WriteWord(uaecptr addr, uae_u32 val);
void Intercept_WriteLong(uaecptr addr, uae_u32 val);

uae_u32 Intercept_IDEReadByte(uaecptr addr);
uae_u32 Intercept_IDEReadWord(uaecptr addr);
uae_u32 Intercept_IDEReadLong(uaecptr addr);

void Intercept_IDEWriteByte(uaecptr addr, uae_u32 val);
void Intercept_IDEWriteWord(uaecptr addr, uae_u32 val);
void Intercept_IDEWriteLong(uaecptr addr, uae_u32 val);

/* Read intercept functions */
extern void Intercept_VideoHigh_ReadByte(void);
extern void Intercept_VideoMed_ReadByte(void);
extern void Intercept_VideoLow_ReadByte(void);
extern void Intercept_VideoSync_ReadByte(void);
extern void Intercept_VideoBaseLow_ReadByte(void);
extern void Intercept_LineWidth_ReadByte(void);
extern void Intercept_Colour0_ReadWord(void);
extern void Intercept_Colour1_ReadWord(void);
extern void Intercept_Colour2_ReadWord(void);
extern void Intercept_Colour3_ReadWord(void);
extern void Intercept_Colour4_ReadWord(void);
extern void Intercept_Colour5_ReadWord(void);
extern void Intercept_Colour6_ReadWord(void);
extern void Intercept_Colour7_ReadWord(void);
extern void Intercept_Colour8_ReadWord(void);
extern void Intercept_Colour9_ReadWord(void);
extern void Intercept_Colour10_ReadWord(void);
extern void Intercept_Colour11_ReadWord(void);
extern void Intercept_Colour12_ReadWord(void);
extern void Intercept_Colour13_ReadWord(void);
extern void Intercept_Colour14_ReadWord(void);
extern void Intercept_Colour15_ReadWord(void);
extern void Intercept_ShifterMode_ReadByte(void);
extern void Intercept_DiskControl_ReadWord(void);
extern void Intercept_DmaStatus_ReadWord(void);
extern void Intercept_PSGRegister_ReadByte(void);
extern void Intercept_PSGData_ReadByte(void);
extern void Intercept_MicrowireData_ReadWord(void);
extern void Intercept_Monitor_ReadByte(void);
extern void Intercept_ActiveEdge_ReadByte(void);
extern void Intercept_DataDirection_ReadByte(void);
extern void Intercept_EnableA_ReadByte(void);
extern void Intercept_EnableB_ReadByte(void);
extern void Intercept_PendingA_ReadByte(void);
extern void Intercept_PendingB_ReadByte(void);
extern void Intercept_InServiceA_ReadByte(void);
extern void Intercept_InServiceB_ReadByte(void);
extern void Intercept_MaskA_ReadByte(void);
extern void Intercept_MaskB_ReadByte(void);
extern void Intercept_VectorReg_ReadByte(void);
extern void Intercept_TimerACtrl_ReadByte(void);
extern void Intercept_TimerBCtrl_ReadByte(void);
extern void Intercept_TimerCDCtrl_ReadByte(void);
extern void Intercept_TimerAData_ReadByte(void);
extern void Intercept_TimerBData_ReadByte(void);
extern void Intercept_TimerCData_ReadByte(void);
extern void Intercept_TimerDData_ReadByte(void);
extern void Intercept_KeyboardControl_ReadByte(void);
extern void Intercept_KeyboardData_ReadByte(void);
extern void Intercept_MidiControl_ReadByte(void);
extern void Intercept_MidiData_ReadByte(void);
extern void Intercept_BlitterEndmask1_ReadWord(void);
extern void Intercept_BlitterEndmask2_ReadWord(void);
extern void Intercept_BlitterEndmask3_ReadWord(void);
extern void Intercept_BlitterDst_ReadLong(void);
extern void Intercept_BlitterWPL_ReadWord(void);
extern void Intercept_BlitterLPB_ReadWord(void);
extern void Intercept_BlitterHalftoneOp_ReadByte(void);
extern void Intercept_BlitterLogOp_ReadByte(void);
extern void Intercept_BlitterLineNum_ReadByte(void);
extern void Intercept_BlitterSkew_ReadByte(void);


/* Write intercept functions */
extern void Intercept_VideoHigh_WriteByte(void);
extern void Intercept_VideoMed_WriteByte(void);
extern void Intercept_VideoLow_WriteByte(void);
extern void Intercept_VideoSync_WriteByte(void);
extern void Intercept_VideoBaseLow_WriteByte(void);
extern void Intercept_LineWidth_WriteByte(void);
extern void Intercept_Colour0_WriteWord(void);
extern void Intercept_Colour1_WriteWord(void);
extern void Intercept_Colour2_WriteWord(void);
extern void Intercept_Colour3_WriteWord(void);
extern void Intercept_Colour4_WriteWord(void);
extern void Intercept_Colour5_WriteWord(void);
extern void Intercept_Colour6_WriteWord(void);
extern void Intercept_Colour7_WriteWord(void);
extern void Intercept_Colour8_WriteWord(void);
extern void Intercept_Colour9_WriteWord(void);
extern void Intercept_Colour10_WriteWord(void);
extern void Intercept_Colour11_WriteWord(void);
extern void Intercept_Colour12_WriteWord(void);
extern void Intercept_Colour13_WriteWord(void);
extern void Intercept_Colour14_WriteWord(void);
extern void Intercept_Colour15_WriteWord(void);
extern void Intercept_ShifterMode_WriteByte(void);
extern void Intercept_DiskControl_WriteWord(void);
extern void Intercept_DmaStatus_WriteWord(void);
extern void Intercept_PSGRegister_WriteByte(void);
extern void Intercept_PSGData_WriteByte(void);
extern void Intercept_MicrowireData_WriteWord(void);
extern void Intercept_Monitor_WriteByte(void);
extern void Intercept_ActiveEdge_WriteByte(void);
extern void Intercept_DataDirection_WriteByte(void);
extern void Intercept_EnableA_WriteByte(void);
extern void Intercept_EnableB_WriteByte(void);
extern void Intercept_PendingA_WriteByte(void);
extern void Intercept_PendingB_WriteByte(void);
extern void Intercept_InServiceA_WriteByte(void);
extern void Intercept_InServiceB_WriteByte(void);
extern void Intercept_MaskA_WriteByte(void);
extern void Intercept_MaskB_WriteByte(void);
extern void Intercept_VectorReg_WriteByte(void);
extern void Intercept_TimerACtrl_WriteByte(void);
extern void Intercept_TimerBCtrl_WriteByte(void);
extern void Intercept_TimerCDCtrl_WriteByte(void);
extern void Intercept_TimerAData_WriteByte(void);
extern void Intercept_TimerBData_WriteByte(void);
extern void Intercept_TimerCData_WriteByte(void);
extern void Intercept_TimerDData_WriteByte(void);
extern void Intercept_KeyboardControl_WriteByte(void);
extern void Intercept_KeyboardData_WriteByte(void);
extern void Intercept_MidiControl_WriteByte(void);
extern void Intercept_MidiData_WriteByte(void);
extern void Intercept_BlitterEndmask1_WriteWord(void);
extern void Intercept_BlitterEndmask2_WriteWord(void);
extern void Intercept_BlitterEndmask3_WriteWord(void);
extern void Intercept_BlitterDst_WriteLong(void);
extern void Intercept_BlitterWPL_WriteWord(void);
extern void Intercept_BlitterLPB_WriteWord(void);
extern void Intercept_BlitterHalftoneOp_WriteByte(void);
extern void Intercept_BlitterLogOp_WriteByte(void);
extern void Intercept_BlitterLineNum_WriteByte(void);
extern void Intercept_BlitterSkew_WriteByte(void);


extern void Intercept_Init(void);
extern void Intercept_UnInit(void);
extern void Intercept_CreateTable(unsigned long *pInterceptTable[],int Span,int ReadWrite);
extern void Intercept_EnableBlitter(BOOL enableFlag);
extern void Intercept_ModifyTablesForBusErrors(void);
extern void Intercept_ModifyTablesForNoMansLand(void);

#endif
