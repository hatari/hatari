/*
  Hatari
*/

typedef struct {
  unsigned int Address;                /* Address we accessed, eg VideoSync(0xff820a) or VideoShifter(0xff8260) */
  unsigned char Byte;                  /* Byte we wrote */
  int FrameCycles;                     /* Clock cycles into frame */
} SYNCSHIFTER_ACCESS;

typedef struct {
  int nCount;                          /* Number of matching entries - when equal 'nChecks' this table is complete */
  int nChecks;                         /* Number of address/byte/cycle checks in match table */
  SYNCSHIFTER_ACCESS *pSyncShifterAccess;   /* Checks to compare with */
  void *pFunc;                         /* Handler function to call when table is found to match */
  int Value;                           /* Value to pass to above function via 'SyncHandler_Value' */
} SYNCSHIFTER_ACCESS_TABLE;

#define BORDERMASK_NONE    0x0000      /* Borders masks */
#define BORDERMASK_TOP     0x0001
#define BORDERMASK_BOTTOM  0x0002
#define BORDERMASK_LEFT    0x0004
#define BORDERMASK_RIGHT   0x0008

extern long VideoAddress;
extern unsigned char VideoSyncByte,VideoShifterByte;
extern BOOL bUseHighRes;
extern int nVBLs,nHBL;
extern int nStartHBL,nEndHBL;
extern int OverscanMode;
extern unsigned short int HBLPalettes[(NUM_VISIBLE_LINES+1)*16];
extern unsigned long HBLPaletteMasks[NUM_VISIBLE_LINES+1];
extern unsigned short int *pHBLPalettes;
extern unsigned long *pHBLPaletteMasks;
extern unsigned long VideoBase;
extern unsigned long VideoRaster;

extern void Video_Reset(void);
extern void Video_MemorySnapShot_Capture(BOOL bSave);
extern void Video_ClearOnVBL(void);
extern void Video_CalculateAddress(void);
extern unsigned long Video_ReadAddress(void);
extern void Video_InterruptHandler_VBL(void);
extern void Video_InterruptHandler_VBL_Pending(void);
extern void Video_InterruptHandler_EndLine(void);
extern void Video_InterruptHandler_HBL(void);
extern void Video_SyncHandler_SetLeftRightBorder(void);
extern void Video_SyncHandler_SetSyncScrollOffset(void);
extern void Video_SyncHandler_SetTopBorder(void);
extern void Video_SyncHandler_SetBottomBorder(void);
extern void Video_WriteToShifter(void);
extern void Video_WriteToSync(void);
extern void Video_StoreSyncShifterAccess(unsigned int Address,unsigned char Byte);
extern void Video_StartHBL(void);
extern void Video_CopyVDIScreen(void);
extern void Video_EndHBL(void);
extern void Video_SetScreenRasters(void);
extern void Video_SetHBLPaletteMaskPointers(void);
