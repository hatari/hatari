/*
  Hatari - file.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FILE_H
#define HATARI_FILE_H

/* File types */
enum
{
  FILEFILTER_DISCFILES,
  FILEFILTER_ALLFILES,
  FILEFILTER_TOSROM,
  FILEFILTER_MAPFILE,
  FILEFILTER_YMFILE,
  FILEFILTER_MEMORYFILE,
};

#if defined(__BEOS__) || (defined(__sun) && defined(__SVR4))
#include <dirent.h>
extern int alphasort(const void *d1, const void *d2);
extern int scandir(const char *dirname, struct dirent ***namelist, int (*select)(struct dirent *), int (*dcomp)(const void *, const void *));
#endif  /* __BEOS__ */

extern void File_CleanFileName(char *pszFileName);
extern void File_AddSlashToEndFileName(char *pszFileName);
extern BOOL File_DoesFileExtensionMatch(const char *pszFileName, const char *pszExtension);
extern BOOL File_IsRootFileName(char *pszFileName);
extern const char *File_RemoveFileNameDrive(const char *pszFileName);
extern BOOL File_DoesFileNameEndWithSlash(char *pszFileName);
extern void File_RemoveFileNameTrailingSlashes(char *pszFileName);
extern void *File_Read(char *pszFileName, void *pAddress, long *pFileSize, const char *ppszExts[]);
extern BOOL File_Save(char *pszFileName, void *pAddress, size_t Size, BOOL bQueryOverwrite);
extern int File_Length(const char *pszFileName);
extern BOOL File_Exists(const char *pszFileName);
extern BOOL File_QueryOverwrite(const char *pszFileName);
extern BOOL File_FindPossibleExtFileName(char *pszFileName,const char *ppszExts[]);
extern void File_splitpath(const char *pSrcFileName, char *pDir, char *pName, char *Ext);
extern void File_makepath(char *pDestFileName, const char *pDir, const char *pName, const char *pExt);
extern void File_ShrinkName(char *pDestFileName, char *pSrcFileName, int maxlen);
extern void File_MakeAbsoluteName(char *pszFileName);
extern void File_MakeValidPathName(char *pPathName);

#endif /* HATARI_FILE_H */
