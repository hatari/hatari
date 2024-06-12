/*
  Hatari - vme.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VME_H
#define HATARI_VME_H

extern void SCU_SetAccess(void (**readtab)(void), void (**writetab)(void));
extern void SCU_SetEnabled ( bool on_off );
extern void	SCU_Reset ( bool bCold );

extern void SCU_MemorySnapShot_Capture ( bool bSave );
extern void SCU_Info ( FILE *fp, uint32_t arg ) ;

#endif
