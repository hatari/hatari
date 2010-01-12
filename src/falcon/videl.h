/*
  Hatari - videl.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VIDEL_H
#define HATARI_VIDEL_H

extern bool VIDEL_renderScreen(void);
extern void VIDEL_reset(void);
extern void VIDEL_ColorRegsWrite(void);
extern void VIDEL_ShiftModeWriteWord(void);
extern void VIDEL_ZoomModeChanged(void);
extern void VIDEL_ConvertScreenNoZoom(int vw, int vh, int bpp, int nextline);
extern void VIDEL_ConvertScreenZoom(int vw, int vh, int bpp, int nextline);

#endif /* _VIDEL_H */
