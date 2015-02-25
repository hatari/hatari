/*
  Hatari - mfp.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MFP_H
#define HATARI_MFP_H

/* List of MFP interrupts (GPIP is General Purpose I/O Interrupt Port) */
#define	MFP_INT_MAX			15		/* We have 16 ints from 0 to 15 */

#define	MFP_INT_GPIP7			15		/* Highest Priority */
#define	MFP_INT_GPIP6			14
#define	MFP_INT_TIMER_A			13
#define	MFP_INT_RCV_BUF_FULL		12
#define	MFP_INT_RCV_ERR			11
#define	MFP_INT_TRN_BUF_EMPTY		10
#define	MFP_INT_TRN_ERR			9
#define	MFP_INT_TIMER_B			8

#define	MFP_INT_GPIP5			7
#define	MFP_INT_GPIP4			6
#define	MFP_INT_TIMER_C			5
#define	MFP_INT_TIMER_D			4
#define	MFP_INT_GPIP3			3
#define	MFP_INT_GPIP2			2
#define	MFP_INT_GPIP1			1
#define	MFP_INT_GPIP0			0		/* Lowest Priority */


/* MFP register defines ( 1 << Int ) */
#define	MFP_GPIP7_BIT			0x80
#define	MFP_GPIP6_BIT			0x40
#define	MFP_TIMER_A_BIT			0x20
#define	MFP_RCV_BUF_FULL_BIT		0x10
#define	MFP_RCV_ERR_BIT			0x08
#define	MFP_TRN_BUF_EMPTY_BIT		0x04
#define	MFP_TRN_ERR_BIT			0x02
#define	MFP_TIMER_B_BIT			0x01

#define	MFP_GPIP5_BIT			0x80
#define	MFP_GPIP4_BIT			0x40
#define	MFP_TIMER_C_BIT			0x20
#define	MFP_TIMER_D_BIT			0x10
#define	MFP_GPIP3_BIT			0x08
#define	MFP_GPIP2_BIT			0x04
#define	MFP_GPIP1_BIT			0x02
#define	MFP_GPIP0_BIT			0x01


/* List of the GPIP lines */
#define	MFP_GPIP_LINE7			7			
#define	MFP_GPIP_LINE6			6
#define	MFP_GPIP_LINE5			5
#define	MFP_GPIP_LINE4			4
#define	MFP_GPIP_LINE3			3
#define	MFP_GPIP_LINE2			2
#define	MFP_GPIP_LINE1			1
#define	MFP_GPIP_LINE0			0

#define	MFP_GPIP_LINE_FDC_HDC		MFP_GPIP_LINE5
#define	MFP_GPIP_LINE_ACIA		MFP_GPIP_LINE4
#define	MFP_GPIP_LINE_GPU_DONE		MFP_GPIP_LINE3

#define	MFP_GPIP_STATE_LOW		0
#define	MFP_GPIP_STATE_HIGH		1


/* MFP Registers */
extern Uint8 MFP_GPIP;
extern Uint8 MFP_IERA,MFP_IERB;
extern Uint8 MFP_IPRA,MFP_IPRB;
extern Uint8 MFP_TACR,MFP_TBCR;
extern Uint8 MFP_VR;
extern bool  MFP_UpdateNeeded;

extern void MFP_Reset(void);
extern void MFP_MemorySnapShot_Capture(bool bSave);

extern Uint8 MFP_GetIRQ_CPU ( void );
extern void MFP_DelayIRQ ( void );
extern int  MFP_ProcessIACK ( int OldVecNr );
extern bool MFP_ProcessIRQ ( void );
extern void MFP_UpdateIRQ ( Uint64 Event_Time );
extern void MFP_InputOnChannel ( int Interrupt , int Interrupt_Delayed_Cycles );
extern void MFP_GPIP_Set_Line_Input ( Uint8 LineNr , Uint8 Bit );

extern void MFP_TimerA_EventCount_Interrupt(void);
extern void MFP_TimerB_EventCount_Interrupt( int Delayed_Cycles );
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
