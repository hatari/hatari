/*
  Hatari - msa.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern BOOL MSA_FileNameIsMSA(char *pszFileName, BOOL bAllowGZ);
extern Uint8 *MSA_UnCompress(Uint8 *pMSAFile, long *pImageSize);
extern Uint8 *MSA_ReadDisc(char *pszFileName, long *pImageSize);
extern BOOL MSA_WriteDisc(char *pszFileName,unsigned char *pBuffer,int ImageSize);
