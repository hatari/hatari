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

/* Aliases for some GPIP lines */
#define	MFP_GPIP_LINE_FDC_HDC		MFP_GPIP_LINE5
#define	MFP_GPIP_LINE_ACIA		MFP_GPIP_LINE4
#define	MFP_GPIP_LINE_GPU_DONE		MFP_GPIP_LINE3

#define	MFP_TT_GPIP_LINE_SCSI_NCR	MFP_GPIP_LINE7
#define	MFP_TT_GPIP_LINE_RTC		MFP_GPIP_LINE6
#define	MFP_TT_GPIP_LINE_SCSI_DMAC	MFP_GPIP_LINE5
#define	MFP_TT_GPIP_LINE_DC		MFP_GPIP_LINE4
#define	MFP_TT_GPIP_LINE_SCC_B		MFP_GPIP_LINE3
#define	MFP_TT_GPIP_LINE_SCC_DMAC	MFP_GPIP_LINE2


#define	MFP_GPIP_STATE_LOW		0
#define	MFP_GPIP_STATE_HIGH		1


typedef struct {
	/* MFP 68901 internal registers */
	uint8_t	GPIP;					/* General Purpose Pins / GPDR 0x01 */
	uint8_t	AER;					/* Active Edge Register 0x03*/
	uint8_t	DDR;					/* Data Direction Register */
	uint8_t	IERA;					/* Interrupt Enable Register A 0x07 */
	uint8_t	IERB;					/* Interrupt Enable Register B 0x09 */
	uint8_t	IPRA;					/* Interrupt Pending Register A 0x0B */
	uint8_t	IPRB;					/* Interrupt Pending Register B 0x0D */
	uint8_t	ISRA;					/* Interrupt In-Service Register A 0x0F */
	uint8_t	ISRB;					/* Interrupt In-Service Register B 0x11 */
	uint8_t	IMRA;					/* Interrupt Mask Register A 0x13 */
	uint8_t	IMRB;					/* Interrupt Mask Register B 0x15 */
	uint8_t	VR;					/* Vector Register 0x17 */
	uint8_t	TACR;					/* Timer A Control Register 0x19 */
	uint8_t	TBCR;					/* Timer B Control Register 0x1B */
	uint8_t	TCDCR;					/* Timer C/D Control Register 0x1D */
	uint8_t	TADR;					/* Timer A Data Register 0x1F */
	uint8_t	TBDR;					/* Timer B Data Register 0x21 */
	uint8_t	TCDR;					/* Timer C Data Register 0x23 */
	uint8_t	TDDR;					/* Timer D Data Register 0x25 */
	uint8_t	SCR;					/* Synchronous Data Register 0x27 */
	uint8_t	UCR;					/* USART Control Register 0x29 */
	uint8_t	RSR;					/* Receiver Status Register 0x2B */
	uint8_t	TSR;					/* Transmitter Status Register 0x2D */
	uint8_t	UDR;					/* USART Data Register 0x2F */

	uint8_t	IRQ;					/* IRQ signal (output) 1=IRQ requested*/
	uint8_t	TAI;					/* Input signal on Timer A (for event count mode) */
	uint8_t	TBI;					/* Input signal on Timer B (for event count mode) */

	/* Emulation variables */
	uint8_t TA_MAINCOUNTER;
	uint8_t TB_MAINCOUNTER;
	uint8_t TC_MAINCOUNTER;
	uint8_t TD_MAINCOUNTER;

	// TODO drop those 4 variables, as they are not really used in MFP_ReadTimer_xx
	uint32_t TimerAClockCycles;
	uint32_t TimerBClockCycles;
	uint32_t TimerCClockCycles;
	uint32_t TimerDClockCycles;

	uint8_t	PatchTimerD_Done;			/* 0=false 1=true */
	uint8_t	PatchTimerD_TDDR_old;			/* Value of TDDR before forcing it to PATCH_TIMER_TDDR_FAKE */

	int16_t	Current_Interrupt;
	uint64_t	IRQ_Time;				/* Time when IRQ was set to 1 */
	uint8_t	IRQ_CPU;				/* Value of IRQ as seen by the CPU. There's a 4 cycle delay */
							/* between a change of IRQ and its visibility at the CPU side */
	uint64_t	Pending_Time_Min;			/* Clock value of the oldest pending int since last MFP_UpdateIRQ() */
	uint64_t	Pending_Time[ MFP_INT_MAX+1 ];		/* Clock value when pending is set to 1 for each non-masked int */

	/* Other variables */
	char		NameSuffix[ 10 ];		/* "" or "_tt" */
} MFP_STRUCT;


#define		MFP_MAX_NB		2		/* 1 MFP in all machines, except TT with 2 MFP */

extern MFP_STRUCT		MFP_Array[ MFP_MAX_NB ];
extern MFP_STRUCT		*pMFP_Main;
extern MFP_STRUCT		*pMFP_TT;


/* MFP Registers */
extern bool	MFP_UpdateNeeded;

extern int	MFP_ConvertCycle_CPU_MFP_TIMER ( int CPU_Cycles );
extern int	MFP_ConvertCycle_MFP_TIMER_CPU ( int MFP_Cycles );

extern void	MFP_Init ( MFP_STRUCT *pAllMFP );
extern void	MFP_Reset_All ( void );
extern void	MFP_MemorySnapShot_Capture ( bool bSave );

extern uint8_t	MFP_GetIRQ_CPU ( void );
extern void	MFP_DelayIRQ ( void );
extern int	MFP_ProcessIACK ( int OldVecNr );
extern bool	MFP_ProcessIRQ_All ( void );
extern void	MFP_UpdateIRQ_All ( uint64_t Event_Time );
extern void	MFP_InputOnChannel ( MFP_STRUCT *pMFP , int Interrupt , int Interrupt_Delayed_Cycles );
extern void	MFP_GPIP_Set_Line_Input ( MFP_STRUCT *pMFP , uint8_t LineNr , uint8_t Bit );

extern void	MFP_TimerA_Set_Line_Input ( MFP_STRUCT *pMFP , uint8_t Bit );
extern void	MFP_TimerA_EventCount( MFP_STRUCT *pMFP );
extern void	MFP_TimerB_EventCount( MFP_STRUCT *pMFP , int Delayed_Cycles );

extern void	MFP_Main_InterruptHandler_TimerA(void);
extern void	MFP_Main_InterruptHandler_TimerB(void);
extern void	MFP_Main_InterruptHandler_TimerC(void);
extern void	MFP_Main_InterruptHandler_TimerD(void);
extern void	MFP_TT_InterruptHandler_TimerA(void);
extern void	MFP_TT_InterruptHandler_TimerB(void);
extern void	MFP_TT_InterruptHandler_TimerC(void);
extern void	MFP_TT_InterruptHandler_TimerD(void);

extern void	MFP_GPIP_ReadByte ( void );
extern void	MFP_ActiveEdge_ReadByte ( void );
extern void	MFP_DataDirection_ReadByte ( void );
extern void	MFP_EnableA_ReadByte ( void );
extern void	MFP_EnableB_ReadByte ( void );
extern void	MFP_PendingA_ReadByte ( void );
extern void	MFP_PendingB_ReadByte ( void );
extern void	MFP_InServiceA_ReadByte ( void );
extern void	MFP_InServiceB_ReadByte ( void );
extern void	MFP_MaskA_ReadByte ( void );
extern void	MFP_MaskB_ReadByte ( void );
extern void	MFP_VectorReg_ReadByte ( void );
extern void	MFP_TimerACtrl_ReadByte ( void );
extern void	MFP_TimerBCtrl_ReadByte ( void );
extern void	MFP_TimerCDCtrl_ReadByte ( void );
extern void	MFP_TimerAData_ReadByte ( void );
extern void	MFP_TimerBData_ReadByte ( void );
extern void	MFP_TimerCData_ReadByte ( void );
extern void	MFP_TimerDData_ReadByte ( void );

extern void	MFP_GPIP_WriteByte ( void );
extern void	MFP_ActiveEdge_WriteByte ( void );
extern void	MFP_DataDirection_WriteByte ( void );
extern void	MFP_EnableA_WriteByte ( void );
extern void	MFP_EnableB_WriteByte ( void );
extern void	MFP_PendingA_WriteByte ( void );
extern void	MFP_PendingB_WriteByte ( void );
extern void	MFP_InServiceA_WriteByte ( void );
extern void	MFP_InServiceB_WriteByte ( void );
extern void	MFP_MaskA_WriteByte ( void );
extern void	MFP_MaskB_WriteByte ( void );
extern void	MFP_VectorReg_WriteByte ( void );
extern void	MFP_TimerACtrl_WriteByte ( void );
extern void	MFP_TimerBCtrl_WriteByte ( void );
extern void	MFP_TimerCDCtrl_WriteByte ( void );
extern void	MFP_TimerAData_WriteByte ( void );
extern void	MFP_TimerBData_WriteByte ( void );
extern void	MFP_TimerCData_WriteByte ( void );
extern void	MFP_TimerDData_WriteByte ( void );

extern void	MFP_Info(FILE *fp, uint32_t dummy);

#endif
