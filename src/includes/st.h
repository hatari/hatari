/*
  Hatari - st.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern BOOL ST_FileNameIsST(char *pszFileName, BOOL bAllowGZ);
extern Uint8 *ST_ReadDisc(char *pszFileName, long *pImageSize);
extern BOOL ST_WriteDisc(char *pszFileName, Uint8 *pBuffer, int ImageSize);
