/*
  Hatari

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_PATHS_H
#define HATARI_PATHS_H

extern void Paths_Init(const char *argv0);
extern void Paths_UnInit(void);
extern const char *Paths_GetWorkingDir(void);
extern const char *Paths_GetDataDir(void);
extern const char *Paths_GetUserHome(void);
extern const char *Paths_GetHatariHome(void);
extern const char *Paths_GetScreenShotDir(void);
#if defined(__APPLE__)
extern char *Paths_GetMacScreenShotDir(void);
#endif

#endif
