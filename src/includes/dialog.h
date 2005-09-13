/*
  Hatari - dialog.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_DIALOG_H
#define HATARI_DIALOG_H

#include "configuration.h"

extern CNF_PARAMS  DialogParams;

extern BOOL Dialog_DoProperty(void);

/* prototypes for gui-sdl/dlg*.c functions: */
extern int Dialog_MainDlg(BOOL *bReset);
extern void Dialog_AboutDlg(void);
extern int DlgAlert_Notice(const char *text);
extern int DlgAlert_Query(const char *text);
extern void Dialog_DeviceDlg(void);
extern void Dialog_DiskDlg(void);
extern void Dialog_JoyDlg(void);
extern void Dialog_KeyboardDlg(void);
extern void Dialog_MemDlg(void);
extern void DlgNewDisk_Main(void);
extern void Dialog_ScreenDlg(void);
extern void Dialog_SoundDlg(void);
extern void Dialog_SystemDlg(void);
extern void DlgRom_Main(void);

#endif
