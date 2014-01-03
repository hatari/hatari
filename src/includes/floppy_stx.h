/*
  Hatari - floppy_stx.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern void	STX_MemorySnapShot_Capture(bool bSave);
extern bool	STX_FileNameIsSTX(const char *pszFileName, bool bAllowGZ);
extern Uint8	*STX_ReadDisk(const char *pszFileName, long *pImageSize, int *pImageType);
extern bool	STX_WriteDisk(const char *pszFileName, Uint8 *pBuffer, int ImageSize);

extern bool	STX_Init ( void );
extern bool	STX_Insert ( int Drive , Uint8 *pImageBuffer , long ImageSize );
extern bool	STX_Eject ( int Drive );

