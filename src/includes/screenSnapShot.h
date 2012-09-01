/*
  Hatari - screenSnapShot.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREENSNAPSHOT_H
#define HATARI_SCREENSNAPSHOT_H

#include <stdio.h>
#include <SDL.h>

extern int ScreenSnapShot_SavePNG_ToFile(SDL_Surface *surface, FILE *fp, int png_compression_level, int png_filter ,
		int CropLeft , int CropRight , int CropTop , int CropBottom );
extern void ScreenSnapShot_SaveScreen(void);

#endif /* ifndef HATARI_SCREENSNAPSHOT_H */

