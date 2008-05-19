/*
  Hatari - dim.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern bool DIM_FileNameIsDIM(char *pszFileName, bool bAllowGZ);
extern Uint8 *DIM_ReadDisk(char *pszFileName, long *pImageSize);
extern bool DIM_WriteDisk(char *pszFileName, Uint8 *pBuffer, int ImageSize);
