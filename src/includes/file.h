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

extern uint8_t *File_ZlibRead(const char *pszFileName, long *pFileSize);
extern uint8_t *File_ReadAsIs(const char *pszFileName, long *pFileSize);
extern uint8_t *File_Read(const char *pszFileName, long *pFileSize, const char * const ppszExts[]);
extern bool File_Save(const char *pszFileName, const uint8_t *pAddress, size_t Size, bool bQueryOverwrite);
extern off_t File_Length(const char *pszFileName);
extern bool File_Exists(const char *pszFileName);
extern bool File_DirExists(const char *psDirName);
extern bool File_QueryOverwrite(const char *pszFileName);
extern FILE *File_Open(const char *path, const char *mode);
extern FILE *File_Close(FILE *fp);
extern bool File_Lock(FILE *fp);
extern void File_UnLock(FILE *fp);
extern bool File_InputAvailable(FILE *fp);
extern void File_MakeValidPathName(char *pPathName);
extern char *File_GetCWD(char *buf, size_t bufsize);
extern FILE *File_OpenTempFile(char **name);

#endif /* HATARI_FILE_H */
