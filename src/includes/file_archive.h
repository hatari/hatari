/*
  Hatari - file_archive.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FILE_ARCHIVE_H
#define HATARI_FILE_ARCHIVE_H


#include <dirent.h>

typedef struct
{
	char **names;
	int nfiles;
} archive_dir;

extern bool		Archive_FileNameIsSupported ( const char *FileName );
extern struct dirent	**Archive_GetFilesDir ( const archive_dir *pArcDir, const char *dir, int *pEntries );
extern void		Archive_FreeArcDir ( archive_dir *pArcDir );
extern archive_dir	*Archive_GetFiles ( const char *FileName );
extern uint8_t		*Archive_ReadDisk ( int Drive, const char *FileName, const char *ArchivePath, long *pImageSize, int *pImageType );
extern bool		Archive_WriteDisk ( int Drive, const char *FileName, unsigned char *pBuffer, int ImageSize);
extern uint8_t		*Archive_ReadFirstFile ( const char *FileName, long *pImageSize, const char * const Exts[] );


#endif  /* HATARI_FILE_ARCHIVE_H */
