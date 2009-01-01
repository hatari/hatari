/*
  Hatari - midi.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MIDI_H
#define HATARI_MIDI_H

void Midi_Init(void);
void Midi_UnInit(void);
void Midi_Reset(void);
void Midi_Control_ReadByte(void);
void Midi_Data_ReadByte(void);
void Midi_Control_WriteByte(void);
void Midi_Data_WriteByte(void);
void Midi_InterruptHandler_Update(void);

#endif
