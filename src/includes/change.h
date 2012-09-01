/*
  Hatari - change.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_CHANGE_H
#define HATARI_CHANGE_H

#include "configuration.h"

extern bool Change_DoNeedReset(CNF_PARAMS *current, CNF_PARAMS *changed);
extern void Change_CopyChangedParamsToConfiguration(CNF_PARAMS *current, CNF_PARAMS *changed, bool bForceReset);
extern bool Change_ApplyCommandline(char *cmdline);

#endif /* HATARI_CHANGE_H */
