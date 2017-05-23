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

extern bool INF_AutoStartSet(const char *prgname);
extern const char *INF_AutoStartValidate(void);
extern void INF_AutoStartCreate(void);
extern bool INF_AutoStarting(autostart_t t);
extern FILE *INF_AutoStartOpen(const char *filename);
extern bool INF_AutoStartClose(FILE *fp);

#endif
