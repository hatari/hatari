/*
  Hatari - log.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_LOG_H
#define HATARI_LOG_H

typedef enum
{
	LOG_FATAL,
	LOG_ERROR,
	LOG_WARN,
	LOG_INFO,
	LOG_DEBUG
} LOGTYPE;

extern void Log_Init(void);
extern void Log_UnInit(void);
extern void Log_Printf(LOGTYPE nType, const char *psFormat, ...);
extern void Log_AlertDlg(LOGTYPE nType, const char *psFormat, ...);

#endif  /* HATARI_LOG_H */
