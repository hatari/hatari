/*
  Hatari - dialog.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_DIALOG_H
#define HATARI_DIALOG_H

#include "configuration.h"

extern CNF_PARAMS  DialogParams;

extern void Dialog_DefaultConfigurationDetails(void);
extern void Dialog_CopyDetailsFromConfiguration(BOOL bReset);
extern BOOL Dialog_DoProperty(void);

#endif
