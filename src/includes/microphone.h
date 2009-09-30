/*
  Hatari - microphone.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MICROPHONE_H
#define HATARI_MICROPHONE_H

#include "portaudio.h"

extern void Microphone_Run(void);
extern int Microphone_Start(int sampleRate);
extern int Microphone_Stop(void);

PaStreamParameters micro_inputParameters;
PaStream *micro_stream;
PaError  micro_err;
int  micro_totalFrames;
int  micro_numSamples;
int  micro_numBytes;
int  micro_sampleRate;

#endif /* HATARI_MICROPHONE_H */
