/*
  Hatari - floppy_ipf.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern void	IPF_MemorySnapShot_Capture(bool bSave);
extern bool	IPF_FileNameIsIPF(const char *pszFileName, bool bAllowGZ);
extern Uint8	*IPF_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType);
extern bool	IPF_WriteDisk(int Drive, const char *pszFileName, Uint8 *pBuffer, int ImageSize);

extern bool	IPF_Init ( void );
extern void	IPF_Exit ( void );
extern bool	IPF_Insert ( int Drive , Uint8 *pImageBuffer , long ImageSize );
extern bool	IPF_Eject ( int Drive );

extern void	IPF_Reset ( void );
extern void	IPF_Drive_Set_Enable ( int Drive , bool value );
extern void	IPF_Drive_Set_DoubleSided ( int Drive , bool value );
extern void	IPF_SetDriveSide ( Uint8 io_porta_old , Uint8 io_porta_new );
extern void	IPF_FDC_WriteReg ( Uint8 Reg , Uint8 Byte );
extern Uint8	IPF_FDC_ReadReg ( Uint8 Reg );
extern void	IPF_FDC_StatusBar ( Uint8 *pCommand , Uint8 *pHead , Uint8 *pTrack , Uint8 *pSector , Uint8 *pSide );
extern void	IPF_Emulate ( void );

