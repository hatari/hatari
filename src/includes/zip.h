/*
  Hatari - zip.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_ZIP_H
#define HATARI_ZIP_H


#include <dirent.h>

typedef struct {
  char **names;
  int nfiles;
} zip_dir;

extern BOOL ZIP_FileNameIsZIP(char *pszFileName);
extern struct dirent **ZIP_GetFilesDir(zip_dir *files, char *dir, int *entries);
extern void ZIP_FreeZipDir(zip_dir *zd);
extern zip_dir *ZIP_GetFiles(char *pszFileName);
extern Uint8 *ZIP_ReadDisc(char *pszFileName, char *pszZipPath, long *pImageSize);
extern BOOL ZIP_WriteDisc(char *pszFileName,unsigned char *pBuffer,int ImageSize);


#endif  /* HATARI_ZIP_H */
