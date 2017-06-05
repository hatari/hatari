/*
  Hatari - inffile.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_INFFILE_H
#define HATARI_INFFILE_H

typedef enum {
	AUTOSTART_INTERCEPT,
	AUTOSTART_FOPEN
} autostart_t;

extern bool INF_SetAutoStart(const char *prgname, int opt_id);
extern bool INF_SetResolution(const char *resolution, int opt_id);
extern void INF_SetVdiMode(int vdi_res);
extern int INF_ValidateAutoStart(const char **val, const char **err);
extern void INF_CreateOverride(void);
extern bool INF_Overriding(autostart_t t);
extern FILE *INF_OpenOverride(const char *filename);
extern bool INF_CloseOverride(FILE *fp);

#endif
