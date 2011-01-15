/*
  Hatari - resolution.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_RESOLUTION_H
#define HATARI_RESOLUTION_H

extern void Resolution_Init(void);
extern void Resolution_GetDesktopSize(int *width, int *height);
extern void Resolution_GetLimits(int *width, int *height, int *bpp);
extern void Resolution_Search(int *width, int *height, int *bpp);

#endif
