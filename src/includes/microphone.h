/*
  Hatari - microphone.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MICROPHONE_H
#define HATARI_MICROPHONE_H

#if HAVE_PORTAUDIO
extern void Microphone_Run(void);
extern int Microphone_Start(int sampleRate);
extern int Microphone_Stop(void);
#else
/* replace function calls with NOPs (could also be empty static inlines) */
# define Microphone_Run()
#define Microphone_Start(rate) 0
#define Microphone_Stop() 0
#endif


#endif /* HATARI_MICROPHONE_H */
