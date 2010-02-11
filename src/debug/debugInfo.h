/*
  Hatari - debugInfo.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Public atari components debugging infos header.
*/

#ifndef HATARI_DEBUGINFO_H
#define HATARI_DEBUGINFO_H

extern void DebugInfo_ShowInfo(void);
extern char *DebugInfo_MatchInfo(const char *text, int state);
extern char *DebugInfo_MatchLock(const char *text, int state);
extern int DebugInfo_Command(int nArgc, char *psArgs[]);

#endif /* HATARI_DEBUGINFO_H */
