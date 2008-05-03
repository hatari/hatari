/*
  Hatari - st.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern bool ST_FileNameIsST(char *pszFileName, bool bAllowGZ);
extern Uint8 *ST_ReadDisk(char *pszFileName, long *pImageSize);
extern bool ST_WriteDisk(char *pszFileName, Uint8 *pBuffer, int ImageSize);
