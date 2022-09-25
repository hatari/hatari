/*
  Hatari - debugInfo.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Public atari components debugging infos header.
*/

#ifndef HATARI_DEBUGINFO_H
#define HATARI_DEBUGINFO_H

/* for breakcond.c & profile.c */
extern uint32_t DebugInfo_GetTEXT(void);
extern uint32_t DebugInfo_GetTEXTEnd(void);
extern uint32_t DebugInfo_GetDATA(void);
extern uint32_t DebugInfo_GetBSS(void);
extern uint32_t DebugInfo_GetBASEPAGE(void);

/* for debugui.c */
extern void DebugInfo_ShowSessionInfo(void);
extern char *DebugInfo_MatchInfo(const char *text, int state);
extern char *DebugInfo_MatchLock(const char *text, int state);
extern int DebugInfo_Command(int nArgc, char *psArgs[]);

/* for breakpoint ":info" callbacks */
typedef void (*info_func_t)(FILE *fp, uint32_t arg);
extern info_func_t DebugInfo_GetInfoFunc(const char *name);

#endif /* HATARI_DEBUGINFO_H */
