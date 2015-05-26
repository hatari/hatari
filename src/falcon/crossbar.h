/*
  Hatari - crossbar.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CROSSBAR_H
#define HATARI_CROSSBAR_H

#define CROSSBAR_SNDCTRL_PLAY         0x01
#define CROSSBAR_SNDCTRL_PLAYLOOP     0x02
#define CROSSBAR_SNDCTRL_RECORD       0x10
#define CROSSBAR_SNDCTRL_RECORDLOOP   0x20

#define CROSSBAR_FREQ_25MHZ           0x0
#define CROSSBAR_FREQ_EXTERN          0x1
#define CROSSBAR_FREQ_32MHZ           0x2

#define CROSSBAR_SNDMODE_16BITSTEREO  0x40
#define CROSSBAR_SNDMODE_MONO         0x80

extern Uint16 nCbar_DmaSoundControl;

/* Called by audio.c */
void Crossbar_Compute_Ratio(void);

/* Called by mfp.c */
extern void Crossbar_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate);

extern void Crossbar_Reset(bool bCold);
extern void Crossbar_MemorySnapShot_Capture(bool bSave);

/* Called by ioMemTabFalcon.c */
extern void Crossbar_BufferInter_WriteByte(void);
extern void Crossbar_DmaCtrlReg_WriteByte(void);
extern void Crossbar_FrameStartHigh_ReadByte(void);
extern void Crossbar_FrameStartHigh_WriteByte(void);
extern void Crossbar_FrameStartMed_ReadByte(void);
extern void Crossbar_FrameStartMed_WriteByte(void);
extern void Crossbar_FrameStartLow_ReadByte(void);
extern void Crossbar_FrameStartLow_WriteByte(void);
extern void Crossbar_FrameCountHigh_ReadByte(void);
extern void Crossbar_FrameCountHigh_WriteByte(void);
extern void Crossbar_FrameCountMed_ReadByte(void);
extern void Crossbar_FrameCountMed_WriteByte(void);
extern void Crossbar_FrameCountLow_ReadByte(void);
extern void Crossbar_FrameEndHigh_ReadByte(void);
extern void Crossbar_FrameCountLow_WriteByte(void);
extern void Crossbar_FrameEndHigh_WriteByte(void);
extern void Crossbar_FrameEndMed_ReadByte(void);
extern void Crossbar_FrameEndMed_WriteByte(void);
extern void Crossbar_FrameEndLow_ReadByte(void);
extern void Crossbar_FrameEndLow_WriteByte(void);
extern void Crossbar_DmaTrckCtrl_WriteByte(void);
extern void Crossbar_SoundModeCtrl_WriteByte(void);
extern void Crossbar_SrcControler_WriteWord(void);
extern void Crossbar_DstControler_WriteWord(void);
extern void Crossbar_FreqDivExt_WriteByte(void);
extern void Crossbar_FreqDivInt_WriteByte(void);
extern void Crossbar_TrackRecSelect_WriteByte(void);
extern void Crossbar_CodecInput_WriteByte(void);
extern void Crossbar_AdcInput_WriteByte(void);
extern void Crossbar_InputAmp_WriteByte(void);
extern void Crossbar_OutputReduct_WriteWord(void);
extern void Crossbar_CodecStatus_WriteWord(void);
extern void Crossbar_Microwire_WriteWord(void);

/* Called by cycint.c */
extern void Crossbar_InterruptHandler_25Mhz(void);
extern void Crossbar_InterruptHandler_32Mhz(void);

/* Called by dmaSnd.c */
extern void Crossbar_InterruptHandler_Microwire(void);

/* Called by dsp.c */
void Crossbar_DmaPlayInHandShakeMode(void);
void Crossbar_DmaRecordInHandShakeMode_Frame(Uint32 frame);

/* Called by microphone.c */
void Crossbar_GetMicrophoneDatas(Sint16 *micro_bufferL, Sint16 *micro_bufferR, Uint32 microBuffer_size);

/* called by debugInfo.c */
extern void Crossbar_Info(FILE *fp, Uint32 dummy);

#endif /* HATARI_CROSSBAR_H */
