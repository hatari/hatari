/*
  Hatari - breakcond.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_BREAKCOND_H
#define HATARI_BREAKCOND_H

extern bool BreakCond_MatchCpu(void);
extern bool BreakCond_MatchDsp(void);
extern int BreakCond_BreakPointCount(bool bForDsp);
extern bool BreakCond_Command(const char *expression, bool bForDsp);

#endif
