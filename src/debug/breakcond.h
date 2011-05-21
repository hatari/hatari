/*
  Hatari - breakcond.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_BREAKCOND_H
#define HATARI_BREAKCOND_H

/* for debugui.c */
extern bool BreakCond_Save(const char *filename);
extern char *BreakCond_MatchCpuVariable(const char *text, int state);
extern char *BreakCond_MatchDspVariable(const char *text, int state);

/* for evaluate.c */
extern bool BreakCond_GetHatariVariable(const char *name, Uint32 *value);

/* for debugcpu.c & debugdsp.c */
extern const char BreakCond_Description[];
extern const char BreakAddr_Description[];

extern int BreakCond_MatchCpu(void);
extern int BreakCond_MatchDsp(void);
extern int BreakCond_BreakPointCount(bool bForDsp);
extern bool BreakCond_Command(const char *expression, bool bForDsp);
extern bool BreakAddr_Command(char *expression, bool bforDsp);

/* extra functions exported for the test code */
extern int BreakCond_MatchCpuExpression(int position, const char *expression);

#endif
