/*
  Hatari - dim.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern BOOL DIM_FileNameIsDIM(char *pszFileName, BOOL bAllowGZ);
extern Uint8 *DIM_ReadDisc(char *pszFileName, long *pImageSize);
extern BOOL DIM_WriteDisc(char *pszFileName, unsigned char *pBuffer, int ImageSize);
