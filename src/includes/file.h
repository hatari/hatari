/*
  Hatari - file.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FILE_H
#define HATARI_FILE_H

#include "config.h"
#include <sys/types.h>		/* Needed for off_t */

#ifndef HAVE_FSEEKO
#define fseeko fseek
#endif
#ifndef HAVE_FTELLO
#define ftello ftell
#endif

extern void File_CleanFileName(char *pszFileName);
extern void File_AddSlashToEndFileName(char *pszFileName);
extern bool File_DoesFileExtensionMatch(const char *pszFileName, const char *pszExtension);
extern bool File_ChangeFileExtension(const char *Filename_old, const char *Extension_old , char *Filename_new , const char *Extension_new);
extern const char *File_RemoveFileNameDrive(const char *pszFileName);
extern bool File_DoesFileNameEndWithSlash(char *pszFileName);
extern uint8_t *File_ZlibRead(const char *pszFileName, long *pFileSize);
extern uint8_t *File_ReadAsIs(const char *pszFileName, long *pFileSize);
extern uint8_t *File_Read(const char *pszFileName, long *pFileSize, const char * const ppszExts[]);
extern bool File_Save(const char *pszFileName, const uint8_t *pAddress, size_t Size, bool bQueryOverwrite);
extern off_t File_Length(const char *pszFileName);
extern bool File_Exists(const char *pszFileName);
extern bool File_DirExists(const char *psDirName);
extern bool File_QueryOverwrite(const char *pszFileName);
extern char* File_FindPossibleExtFileName(const char *pszFileName,const char * const ppszExts[]);
extern void File_SplitPath(const char *pSrcFileName, char *pDir, char *pName, char *Ext);
extern char* File_MakePath(const char *pDir, const char *pName, const char *pExt);
extern int File_MakePathBuf(char *buf, size_t buflen, const char *pDir,
                            const char *pName, const char *pExt);
extern void File_ShrinkName(char *pDestFileName, const char *pSrcFileName, int maxlen);
extern FILE *File_Open(const char *path, const char *mode);
extern FILE *File_Close(FILE *fp);
extern bool File_Lock(FILE *fp);
extern void File_UnLock(FILE *fp);
extern bool File_InputAvailable(FILE *fp);
extern const char *File_Basename(const char *path);
extern void File_MakeAbsoluteSpecialName(char *pszFileName);
extern void File_MakeAbsoluteName(char *pszFileName);
extern void File_MakeValidPathName(char *pPathName);
extern void File_PathShorten(char *path, int dirs);
extern void File_HandleDotDirs(char *path);
extern FILE *File_OpenTempFile(char **name);

#endif /* HATARI_FILE_H */
