/*
  Hatari - breakcond.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_BREAKCOND_H
#define HATARI_BREAKCOND_H

extern const char BreakCond_Description[];

/* for debugui.c */
extern void BreakCond_MemorySnapShot_Capture(bool bSave);
extern int BreakCond_MatchCpu(void);
extern int BreakCond_MatchDsp(void);
extern int BreakCond_BreakPointCount(bool bForDsp);
extern char *BreakCond_MatchVariable(const char *text, int state);
extern bool BreakCond_Command(char *expression, bool bForDsp);

/* extra functions exported for the test code */
extern void BreakCond_List(bool bForDsp);
extern void BreakCond_RemoveAll(bool bForDsp);
extern bool BreakCond_Remove(int position, bool bForDsp);
extern int BreakCond_MatchCpuExpression(int position, const char *expression);

#endif
