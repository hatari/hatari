/*
  Hatari - mfp.h

  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MFP_H
#define HATARI_MFP_H

/* List of MFP interrupts (GPIP is General Purpose I/O Interrupt Port) */
#define  MFP_EXCEPT_GPIP7           15  /* Highest Priority */
#define  MFP_EXCEPT_GPIP6           14
#define  MFP_EXCEPT_TIMERA          13
#define  MFP_EXCEPT_RECBUFFULL      12
#define  MFP_EXCEPT_RECERR          11
#define  MFP_EXCEPT_TRANSBUFFEMPTY  10
#define  MFP_EXCEPT_TRANSERR        9
#define  MFP_EXCEPT_TIMERB          8

#define  MFP_EXCEPT_GPIP5      7
#define  MFP_EXCEPT_ACIA       6
#define  MFP_EXCEPT_TIMERC     5
#define  MFP_EXCEPT_TIMERD     4
#define  MFP_EXCEPT_GPIP3      3
#define  MFP_EXCEPT_GPIP2      2
#define  MFP_EXCEPT_GPIP1      1
#define  MFP_EXCEPT_GPIP0      0  /* Lowest Priority */

/* MFP register defines */
#define  MFP_TIMER_GPIP7_BIT  0x80
#define  MFP_TIMER_A_BIT      0x20
#define  MFP_RCVBUFFULL_BIT   0x10
#define  MFP_TRNBUFEMPTY_BIT  0x04
#define  MFP_TIMER_B_BIT      0x01

#define  MFP_FDCHDC_BIT       0x80
#define  MFP_ACIA_BIT         0x40
#define  MFP_TIMER_C_BIT      0x20
#define  MFP_TIMER_D_BIT      0x10
#define  MFP_GPU_DONE_BIT     0x08
#define  MFP_GPIP_1_BIT       0x02
#define  MFP_GPIP_0_BIT       0x01

/* MFP Registers */
extern Uint8 MFP_GPIP;
extern Uint8 MFP_IERA,MFP_IERB;
extern Uint8 MFP_IPRA,MFP_IPRB;
extern Uint8 MFP_TACR,MFP_TBCR;
extern Uint8 MFP_VR;

extern void MFP_Reset(void);
extern void MFP_MemorySnapShot_Capture(bool bSave);
extern bool MFP_CheckPendingInterrupts(void);
extern void MFP_InputOnChannel(Uint8 Bit, Uint8 EnableBit, Uint8 *pPendingReg);
extern void MFP_TimerA_EventCount_Interrupt(void);
extern void MFP_TimerB_EventCount_Interrupt(void);
extern void MFP_InterruptHandler_TimerA(void);
extern void MFP_InterruptHandler_TimerB(void);
extern void MFP_InterruptHandler_TimerC(void);
extern void MFP_InterruptHandler_TimerD(void);

extern void MFP_GPIP_ReadByte(void);
extern void MFP_ActiveEdge_ReadByte(void);
extern void MFP_DataDirection_ReadByte(void);
extern void MFP_EnableA_ReadByte(void);
extern void MFP_EnableB_ReadByte(void);
extern void MFP_PendingA_ReadByte(void);
extern void MFP_PendingB_ReadByte(void);
extern void MFP_InServiceA_ReadByte(void);
extern void MFP_InServiceB_ReadByte(void);
extern void MFP_MaskA_ReadByte(void);
extern void MFP_MaskB_ReadByte(void);
extern void MFP_VectorReg_ReadByte(void);
extern void MFP_TimerACtrl_ReadByte(void);
extern void MFP_TimerBCtrl_ReadByte(void);
extern void MFP_TimerCDCtrl_ReadByte(void);
extern void MFP_TimerAData_ReadByte(void);
extern void MFP_TimerBData_ReadByte(void);
extern void MFP_TimerCData_ReadByte(void);
extern void MFP_TimerDData_ReadByte(void);

extern void MFP_GPIP_WriteByte(void);
extern void MFP_ActiveEdge_WriteByte(void);
extern void MFP_DataDirection_WriteByte(void);
extern void MFP_EnableA_WriteByte(void);
extern void MFP_EnableB_WriteByte(void);
extern void MFP_PendingA_WriteByte(void);
extern void MFP_PendingB_WriteByte(void);
extern void MFP_InServiceA_WriteByte(void);
extern void MFP_InServiceB_WriteByte(void);
extern void MFP_MaskA_WriteByte(void);
extern void MFP_MaskB_WriteByte(void);
extern void MFP_VectorReg_WriteByte(void);
extern void MFP_TimerACtrl_WriteByte(void);
extern void MFP_TimerBCtrl_WriteByte(void);
extern void MFP_TimerCDCtrl_WriteByte(void);
extern void MFP_TimerAData_WriteByte(void);
extern void MFP_TimerBData_WriteByte(void);
extern void MFP_TimerCData_WriteByte(void);
extern void MFP_TimerDData_WriteByte(void);

#endif
