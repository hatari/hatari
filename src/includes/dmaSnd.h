/*
  Hatari - dmaSnd.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_DMASND_H
#define HATARI_DMASND_H

#define DMASNDCTRL_PLAY         0x01
#define DMASNDCTRL_PLAYLOOP     0x02

#define DMASNDMODE_16BITSTEREO  0x40
#define DMASNDMODE_MONO         0x80

extern Uint16 nDmaSoundControl;

extern void DmaSnd_Reset(bool bCold);
extern void DmaSnd_MemorySnapShot_Capture(bool bSave);
extern void DmaSnd_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate);
extern void DmaSnd_InterruptHandler(void);
extern void DmaSnd_InterruptHandler_Microwire(void);
extern void DmaSnd_SoundControl_ReadWord(void);
extern void DmaSnd_SoundControl_WriteWord(void);
extern void DmaSnd_FrameCountHigh_ReadByte(void);
extern void DmaSnd_FrameCountMed_ReadByte(void);
extern void DmaSnd_FrameCountLow_ReadByte(void);
extern void DmaSnd_SoundModeCtrl_ReadByte(void);
extern void DmaSnd_SoundModeCtrl_WriteByte(void);
extern void DmaSnd_MicrowireData_ReadWord(void);
extern void DmaSnd_MicrowireData_WriteWord(void);
extern void DmaSnd_MicrowireMask_ReadWord(void);
extern void DmaSnd_MicrowireMask_WriteWord(void);

extern void DmaSnd_ReceiveSoundFromDAC(Sint16 value);
#endif /* HATARI_DMASND_H */
