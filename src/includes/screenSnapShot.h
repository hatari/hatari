/*
  Hatari - screenSnapShot.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREENSNAPSHOT_H
#define HATARI_SCREENSNAPSHOT_H

#include <SDL_video.h>

#define	SCREEN_SNAPSHOT_BMP	1
#define	SCREEN_SNAPSHOT_PNG	2
#define	SCREEN_SNAPSHOT_NEO	3
#define SCREEN_SNAPSHOT_XIMG	4


extern int ScreenSnapShot_SavePNG_ToFile(SDL_Surface *surface, int destw,
		int desth, FILE *fp, int png_compression_level, int png_filter,
		int CropLeft , int CropRight , int CropTop , int CropBottom );
extern void ScreenSnapShot_SaveScreen(void);
extern void ScreenSnapShot_SaveToFile(const char *filename);

#endif /* ifndef HATARI_SCREENSNAPSHOT_H */
