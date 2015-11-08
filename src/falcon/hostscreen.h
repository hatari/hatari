/*
  Hatari - hostscreen.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_HOSTSCREEN_H
#define HATARI_HOSTSCREEN_H

extern void HostScreen_toggleFullScreen(void);
extern void HostScreen_update1(SDL_Rect* extra, bool forced);
extern Uint32 HostScreen_getWidth(void);
extern Uint32 HostScreen_getHeight(void);
extern void HostScreen_updatePalette(int colorCount);
extern void HostScreen_setWindowSize(int width, int height, int bpp, bool bForceChange);

#endif
