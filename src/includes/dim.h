/*
  Hatari - dim.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern BOOL DIM_FileNameIsDIM(char *pszFileName, BOOL bAllowGZ);
extern Uint8 *DIM_ReadDisk(char *pszFileName, long *pImageSize);
extern BOOL DIM_WriteDisk(char *pszFileName, Uint8 *pBuffer, int ImageSize);
