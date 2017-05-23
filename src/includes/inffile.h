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

extern bool TOS_AutoStartSet(const char *prgname);
extern const char *TOS_AutoStartInvalidDrive(void);
extern void TOS_CreateAutoInf(void);
extern bool TOS_AutoStarting(autostart_t t);
extern FILE *TOS_AutoStartOpen(const char *filename);
extern bool TOS_AutoStartClose(FILE *fp);

#endif
