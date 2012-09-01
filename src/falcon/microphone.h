/*
  Hatari - microphone.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_MICROPHONE_H
#define HATARI_MICROPHONE_H

#if HAVE_PORTAUDIO
extern bool Microphone_Start (int sampleRate);
extern bool Microphone_Stop (void);
#else
/* replace function calls with NOPs (could also be empty static inlines) */
#define Microphone_Start(rate) false
#define Microphone_Stop() false
#endif


#endif /* HATARI_MICROPHONE_H */
