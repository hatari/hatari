/*
  Hatari
*/

// List of MFP interrupts (GPIP is General Purpose I/O Interrupt Port)
#define  MFP_EXCEPT_GPIP7           15  // Highest Priority
#define  MFP_EXCEPT_GPIP6           14
#define  MFP_EXCEPT_TIMERA          13
#define  MFP_EXCEPT_RECBUFFULL      12
#define  MFP_EXCEPT_RECERR          11
#define  MFP_EXCEPT_TRANSBUFFEMPTY  10
#define  MFP_EXCEPT_TRANSERR        9
#define  MFP_EXCEPT_TIMERB          8

#define  MFP_EXCEPT_GPIP5      7
#define  MFP_EXCEPT_KEYBOARD   6
#define  MFP_EXCEPT_TIMERC     5
#define  MFP_EXCEPT_TIMERD     4
#define  MFP_EXCEPT_GPIP3      3
#define  MFP_EXCEPT_GPIP2      2
#define  MFP_EXCEPT_GPIP1      1
#define  MFP_EXCEPT_GPIP0      0  // Lowest Priority

// MFP register defines
#define  MFP_TIMER_GPIP7_BIT  0x80
#define  MFP_TIMER_A_BIT      0x20
#define  MFP_TIMER_B_BIT      0x01
#define  MFP_FDCHDC_BIT       0x80
#define  MFP_KEYBOARD_BIT     0x40
#define MFP_TIMER_C_BIT       0x20
#define MFP_TIMER_D_BIT       0x10

// MFP Registers
extern unsigned char MFP_GPIP;
extern unsigned char MFP_AER,MFP_DDR;
extern unsigned char MFP_IERA,MFP_IERB;
extern unsigned char MFP_IPRA,MFP_IPRB;
extern unsigned char MFP_ISRA,MFP_ISRB;
extern unsigned char MFP_IMRA,MFP_IMRB;
extern unsigned char MFP_VR;
extern unsigned char MFP_TACR,MFP_TBCR,MFP_TCDCR;
extern unsigned char MFP_TADR,MFP_TBDR;
extern unsigned char MFP_TCDR,MFP_TDDR;
extern unsigned char MFP_TA_MAINCOUNTER;
extern unsigned char MFP_TB_MAINCOUNTER;
extern unsigned char MFP_TC_MAINCOUNTER;
extern unsigned char MFP_TD_MAINCOUNTER;

extern void MFP_Reset(void);
extern void MFP_MemorySnapShot_Capture(BOOL bSave);
extern void MFP_CheckPendingInterrupts(void);
extern void MFP_UpdateFlags(void);
extern void MFP_InputOnChannel(unsigned char Bit,unsigned char EnableBit,unsigned char *pPendingReg);
extern void MFP_TimerA_EventCount_Interrupt(void);
extern void MFP_TimerB_EventCount_Interrupt(void);
extern void MFP_StartTimerA(void);
extern void MFP_ReadTimerA(void);
extern void MFP_StartTimerB(void);
extern void MFP_ReadTimerB(void);
extern void MFP_StartTimerC(void);
extern void MFP_ReadTimerC(void);
extern void MFP_StartTimerD(void);
extern void MFP_ReadTimerD(void);
extern void MFP_InterruptHandler_TimerA(void);
extern void MFP_InterruptHandler_TimerB(void);
extern void MFP_InterruptHandler_TimerC(void);
extern void MFP_InterruptHandler_TimerD(void);
