/*
  Hatari - midi.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MIDI_H
#define HATARI_MIDI_H

void Midi_Init(void);
void Midi_UnInit(void);
Uint8 Midi_ReadControl(void);
Uint8 Midi_ReadData(void);
void Midi_WriteControl(Uint8 controlByte);
void Midi_WriteData(Uint8 dataByte);

#endif
