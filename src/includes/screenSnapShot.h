/*
  Hatari - screenSnapShot.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREENSNAPSHOT_H
#define HATARI_SCREENSNAPSHOT_H

#include <SDL_video.h>

extern int ScreenSnapShot_SavePNG_ToFile(SDL_Surface *surface, int destw,
		int desth, FILE *fp, int png_compression_level, int png_filter,
		int CropLeft , int CropRight , int CropTop , int CropBottom );
extern void ScreenSnapShot_SaveScreen(void);
extern void ScreenSnapShot_SaveToFile(const char *filename);

#endif /* ifndef HATARI_SCREENSNAPSHOT_H */
