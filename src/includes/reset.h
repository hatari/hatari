/*
  Hatari - reset.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_RESET_H
#define HATARI_RESET_H

extern int Reset_Cold(void);
extern int Reset_Warm(void);
extern int Reset_ST(BOOL bCold);

#endif
