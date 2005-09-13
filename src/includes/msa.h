/*
  Hatari - msa.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern BOOL MSA_FileNameIsMSA(char *pszFileName, BOOL bAllowGZ);
extern Uint8 *MSA_UnCompress(Uint8 *pMSAFile, long *pImageSize);
extern Uint8 *MSA_ReadDisk(char *pszFileName, long *pImageSize);
extern BOOL MSA_WriteDisk(char *pszFileName, Uint8 *pBuffer, int ImageSize);
