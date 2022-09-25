/*
  Hatari - msa.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

extern bool MSA_FileNameIsMSA(const char *pszFileName, bool bAllowGZ);
extern uint8_t *MSA_UnCompress(uint8_t *pMSAFile, long *pImageSize, long nBytesLeft);
extern uint8_t *MSA_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType);
extern bool MSA_WriteDisk(int Drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize);
