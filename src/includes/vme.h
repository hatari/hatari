/*
  Hatari - vme.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VME_H
#define HATARI_VME_H

extern void VME_SetAccess(void (**readtab)(void), void (**writetab)(void));
extern void VME_Info(FILE *fp, uint32_t arg);
extern void VME_Reset(void);

#endif
