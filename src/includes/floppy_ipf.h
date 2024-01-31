/*
  Hatari - floppy_ipf.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

extern void	IPF_MemorySnapShot_Capture(bool bSave);
extern bool	IPF_FileNameIsIPF(const char *pszFileName, bool bAllowGZ);
extern uint8_t	*IPF_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType);
extern bool	IPF_WriteDisk(int Drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize);

extern bool	IPF_Init ( void );
extern void	IPF_Exit ( void );
extern bool	IPF_Insert ( int Drive , uint8_t *pImageBuffer , long ImageSize );
extern bool	IPF_Eject ( int Drive );

extern void	IPF_Reset ( bool bCold );
extern void	IPF_Drive_Set_Enable ( int Drive , bool value );
extern void	IPF_Drive_Set_DoubleSided ( int Drive , bool value );
extern void	IPF_SetDriveSide ( uint8_t io_porta_old , uint8_t io_porta_new );
extern void	IPF_FDC_WriteReg ( uint8_t Reg , uint8_t Byte );
extern uint8_t	IPF_FDC_ReadReg ( uint8_t Reg );
extern void	IPF_FDC_StatusBar ( uint8_t *pCommand , uint8_t *pHead , uint8_t *pTrack , uint8_t *pSector , uint8_t *pSide );
extern void	IPF_Emulate ( void );

