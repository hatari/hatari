/*
  Hatari - filename.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FILENAME_H
#define HATARI_FILENAME_H

void FileName_RemoveSlashes(char *pszFileName);
void FileName_AddSlashToEnd(char *pszFileName);
bool FileName_DoesExtensionMatch(const char *pszFileName, const char *pszExtension);
bool FileName_ChangeExtension(const char *Filename_old, const char *Extension_old,
                              char *Filename_new , const char *Extension_new);
const char *FileName_RemoveDrive(const char *pszFileName);
bool FileName_DoesEndWithSlash(char *pszFileName);
char* FileName_FindPossibleExt(const char *pszFileName,const char * const ppszExts[]);
const char *FileName_Basename(const char *path);
void FileName_SplitPath(const char *pSrcFileName, char *pDir, char *pName, char *Ext);
char* FileName_MakePath(const char *pDir, const char *pName, const char *pExt);
int FileName_MakePathBuf(char *buf, size_t buflen, const char *pDir,
                         const char *pName, const char *pExt);
void FileName_Shrink(char *pDestFileName, const char *pSrcFileName, int maxlen);
void FileName_MakeAbsoluteSpecial(char *pszFileName);
void FileName_MakeAbsolute(char *pszFileName);
void FileName_ShortenPath(char *path, int dirs);
void FileName_HandleDotDirs(char *path);

#endif /* HATARI_FILENAME_H */
