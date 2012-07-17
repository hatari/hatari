/*
  Hatari - floppy_ipf.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern bool	IPF_FileNameIsIPF(const char *pszFileName, bool bAllowGZ);
extern Uint8	*IPF_ReadDisk(const char *pszFileName, long *pImageSize, int *pImageType);
extern bool	IPF_WriteDisk(const char *pszFileName, Uint8 *pBuffer, int ImageSize);

extern bool	IPF_Insert ( Uint8 *pImageBuffer , long ImageSize );
