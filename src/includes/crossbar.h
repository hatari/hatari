/*
  Hatari - crossbar.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CROSSBAR_H
#define HATARI_CROSSBAR_H

#define CROSSBAR_SNDCTRL_PLAY         0x01
#define DMASNDCTRL_PLAYLOOP     0x02

#define CROSSBAR_SNDMODE_16BITSTEREO  0x40
#define CROSSBAR_SNDMODE_MONO         0x80

extern Uint16 nCbar_DmaSoundControl;

/* Called by mfp.c */
extern void Crossbar_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate);

extern void Crossbar_Reset(bool bCold);
extern void Crossbar_MemorySnapShot_Capture(bool bSave);

extern void Crossbar_BufferInter_ReadWord(void);
extern void Crossbar_BufferInter_WriteWord(void);
extern void Crossbar_DmaCtrlReg_ReadWord(void);
extern void Crossbar_DmaCtrlReg_WriteWord(void);
extern void Crossbar_DmaTrckCtrl_ReadByte(void);
extern void Crossbar_DmaTrckCtrl_WriteByte(void);
extern void Crossbar_SoundModeCtrl_ReadByte(void);
extern void Crossbar_SoundModeCtrl_WriteByte(void);
extern void Crossbar_SrcControler_ReadWord(void);
extern void Crossbar_SrcControler_WriteWord(void);
extern void Crossbar_DstControler_ReadWord(void);
extern void Crossbar_DstControler_WriteWord(void);
extern void Crossbar_FreqDivExt_ReadByte(void);
extern void Crossbar_FreqDivExt_WriteByte(void);
extern void Crossbar_FreqDivInt_ReadByte(void);
extern void Crossbar_FreqDivInt_WriteByte(void);
extern void Crossbar_TrackRecSelect_ReadByte(void);
extern void Crossbar_TrackRecSelect_WriteByte(void);
extern void Crossbar_CodecInput_ReadByte(void);
extern void Crossbar_CodecInput_WriteByte(void);
extern void Crossbar_AdcInput_ReadByte(void);
extern void Crossbar_AdcInput_WriteByte(void);
extern void Crossbar_InputAmp_ReadByte(void);
extern void Crossbar_InputAmp_WriteByte(void);
extern void Crossbar_OutputReduct_ReadWord(void);
extern void Crossbar_OutputReduct_WriteWord(void);
extern void Crossbar_CodecStatus_ReadWord(void);
extern void Crossbar_CodecStatus_WriteWord(void);

extern void Crossbar_InterruptHandler_DspXmit(void);


static double Crossbar_DetectSampleRate(void);
static void Crossbar_SendDataToDAC(Sint16 value);

#endif /* HATARI_CROSSBAR_H */
