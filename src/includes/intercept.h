/*
  Hatari
*/

#define  BUS_ERROR_ADDR  0xE00000           /* Address below here causes bus error exception */

#define INTERCEPT_WORKSPACE_SIZE  (8*1024)  /* 8k, size of intercept lists */

/* List of hardware addresses to be intercepted */
enum {
  INTERCEPT_NULL,
  INTERCEPT_VIDEOHIGH,       /* 0xff8205 byte */
  INTERCEPT_VIDEOMED,        /* 0xff8207 byte */
  INTERCEPT_VIDEOLOW,        /* 0xff8209 byte */
  INTERCEPT_VIDEOSYNC,       /* 0xff820a byte */
  INTERCEPT_VIDEOBASELOW,    /* 0xff820d byte */
  INTERCEPT_LINEWIDTH,       /* 0xff820e byte */
  INTERCEPT_COLOUR0,         /* 0xff8240 word */
  INTERCEPT_COLOUR1,         /* 0xff8242 word */
  INTERCEPT_COLOUR2,         /* 0xff8244 word */
  INTERCEPT_COLOUR3,         /* 0xff8246 word */
  INTERCEPT_COLOUR4,         /* 0xff8248 word */
  INTERCEPT_COLOUR5,         /* 0xff824a word */
  INTERCEPT_COLOUR6,         /* 0xff824c word */
  INTERCEPT_COLOUR7,         /* 0xff824e word */
  INTERCEPT_COLOUR8,         /* 0xff8250 word */
  INTERCEPT_COLOUR9,         /* 0xff8252 word */
  INTERCEPT_COLOUR10,        /* 0xff8254 word */
  INTERCEPT_COLOUR11,        /* 0xff8256 word */
  INTERCEPT_COLOUR12,        /* 0xff8258 word */
  INTERCEPT_COLOUR13,        /* 0xff825a word */
  INTERCEPT_COLOUR14,        /* 0xff825c word */
  INTERCEPT_COLOUR15,        /* 0xff825e word */
  INTERCEPT_SHIFTERMODE,     /* 0xff8260 byte */
  INTERCEPT_DISKCONTROL,     /* 0xff8604 word */
  INTERCEPT_DMASTATUS,       /* 0xff8606 word */
  INTERCEPT_PSG_REGISTER,    /* 0xff8800 byte */
  INTERCEPT_PSG_DATA,        /* 0xff8802 byte */
  INTERCEPT_MICROWIREDATA,   /* 0xff8922 word */
  INTERCEPT_MONITOR,         /* 0xfffa01 byte */
  INTERCEPT_ACTIVE_EDGE,     /* 0xfffa03 byte */
  INTERCEPT_DATA_DIRECTION,  /* 0xfffa05 byte */
  INTERCEPT_ENABLE_A,        /* 0xfffa07 byte */
  INTERCEPT_ENABLE_B,        /* 0xfffa09 byte */
  INTERCEPT_PENDING_A,       /* 0xfffa0b byte */
  INTERCEPT_PENDING_B,       /* 0xfffa0d byte */
  INTERCEPT_INSERVICE_A,     /* 0xfffa0f byte */
  INTERCEPT_INSERVICE_B,     /* 0xfffa11 byte */
  INTERCEPT_MASK_A,          /* 0xfffa13 byte */
  INTERCEPT_MASK_B,          /* 0xfffa15 byte */
  INTERCEPT_VECTOR_REG,      /* 0xfffa17 byte */
  INTERCEPT_TIMERA_CTRL,     /* 0xfffa19 byte */
  INTERCEPT_TIMERB_CTRL,     /* 0xfffa1b byte */
  INTERCEPT_TIMERCD_CTRL,    /* 0xfffa1d byte */
  INTERCEPT_TIMERA_DATA,     /* 0xfffa1f byte */
  INTERCEPT_TIMERB_DATA,     /* 0xfffa21 byte */
  INTERCEPT_TIMERC_DATA,     /* 0xfffa23 byte */
  INTERCEPT_TIMERD_DATA,     /* 0xfffa25 byte */
  INTERCEPT_KEYBOARDCONTROL, /* 0xfffc00 byte */
  INTERCEPT_KEYBOARDDATA,    /* 0xfffc02 byte */
  INTERCEPT_MIDICONTROL,     /* 0xfffc04 byte */
  INTERCEPT_MIDIDATA,        /* 0xfffc06 byte */

  INTERCEPT_COUNT
};

/* Hardware address details */
typedef struct {
  unsigned int Address;        /* ST hardware address */
  int SpanInBytes;             /* SIZE_BYTE, SIZE_WORD or SIZE_LONG */
  void *ReadFunc;              /* Read function */
  void *WriteFunc;             /* Write function */
} INTERCEPT_ACCESS_FUNC;

/* List of hardware address which are not documented, ie STe, TT, Falcon locations - should be unconnected on STfm */
typedef struct {
  unsigned int Start_Address;
  unsigned int End_Address;
} INTERCEPT_ADDRESSRANGE;



uae_u32 Intercept_ReadByte(uaecptr addr);
uae_u32 Intercept_ReadWord(uaecptr addr);
uae_u32 Intercept_ReadLong(uaecptr addr);

void Intercept_WriteByte(uaecptr addr, uae_u32 val);
void Intercept_WriteWord(uaecptr addr, uae_u32 val);
void Intercept_WriteLong(uaecptr addr, uae_u32 val);



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

extern void Intercept_Init(void);
extern void Intercept_UnInit(void);
extern void Intercept_CreateTable(unsigned long *pInterceptTable[],int Span,int ReadWrite);
extern void Intercept_ModifyTablesForBusErrors(void);
extern void Intercept_ModifyTablesForNoMansLand(void);
