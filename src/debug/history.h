/*
  Hatari - history.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_HISTORY_H
#define HATARI_HISTORY_H

/* couldn't think of better place for this */
extern const char* History_ReasonStr(debug_reason_t reason);

/* for debugcpu/dsp.c */
extern bool bHistoryEnabled;
extern void History_AddCpu(void);
extern void History_AddDsp(void);

/* for debugInfo.c & breakcond.c */
extern void History_Enable(bool enable);

/* for breakcond.c */
extern Uint32 History_GetLastCpu(void);
extern Uint32 History_GetLastDsp(void);

/* for debugInfo.c */
extern void History_Show(int count);

/* for debugui */
extern void History_Mark(debug_reason_t reason);
extern bool History_Parse(int nArgc, char *psArgv[]);

#endif
