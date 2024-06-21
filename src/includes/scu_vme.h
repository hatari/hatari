/*
  Hatari - scu_vme.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCU_VME_H
#define HATARI_SCU_VME_H

extern bool	SCU_IsEnabled ( void );

extern void	SCU_Reset ( bool bCold );

extern void	SCU_UpdatePendingInts_CPU ( void );
extern void	SCU_SetIRQ_CPU ( int IntNr );
extern void	SCU_ClearIRQ_CPU ( int IntNr );

extern void	SCU_SysIntMask_ReadByte ( void );
extern void	SCU_SysIntMask_WriteByte ( void );
extern void	SCU_SysIntState_ReadByte ( void );
extern void	SCU_SysIntState_WriteByte ( void );
extern void	SCU_SysInterrupter_ReadByte ( void );
extern void	SCU_SysInterrupter_WriteByte ( void );
extern void	SCU_VmeInterrupter_ReadByte ( void );
extern void	SCU_VmeInterrupter_WriteByte ( void );
extern void	SCU_GPR1_ReadByte ( void );
extern void	SCU_GPR1_WriteByte ( void );
extern void	SCU_GPR2_ReadByte ( void );
extern void	SCU_GPR2_WriteByte ( void );
extern void	SCU_VmeIntMask_Readyte ( void );
extern void	SCU_VmeIntMask_WriteByte ( void );
extern void	SCU_VmeIntState_ReadByte ( void );
extern void	SCU_VmeIntState_WriteByte ( void );

extern void	SCU_MemorySnapShot_Capture ( bool bSave );
extern void	SCU_Info ( FILE *fp, uint32_t arg ) ;

#endif
