/*
  Hatari - dmaSnd.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_DMASND_H
#define HATARI_DMASND_H

#define DMASNDCTRL_PLAY      0x01
#define DMASNDCTRL_PLAYLOOP  0x02

#define DMASNDMODE_MONO      0x80

extern Uint16 nDmaSoundControl;

void DmaSnd_GenerateSamples(int nMixBufIdx, int nSamplesToGenerate);
void DmaSnd_InterruptHandler(void);
void DmaSnd_SoundControl_ReadWord(void);
void DmaSnd_SoundControl_WriteWord(void);
void DmaSnd_FrameCountHigh_ReadByte(void);
void DmaSnd_FrameCountMed_ReadByte(void);
void DmaSnd_FrameCountLow_ReadByte(void);
void DmaSnd_SoundMode_ReadWord(void);
void DmaSnd_SoundMode_WriteWord(void);
void DmaSnd_MicrowireData_ReadWord(void);

#endif /* HATARI_DMASND_H */
