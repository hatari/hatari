/*
  Hatari - microphone.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MICROPHONE_H
#define HATARI_MICROPHONE_H

#if HAVE_PORTAUDIO
#include "portaudio.h"
#endif

extern void Microphone_Run(void);
extern int Microphone_Start(int sampleRate);
extern int Microphone_Stop(void);

#endif /* HATARI_MICROPHONE_H */
