/*
  Hatari - compat.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains all the includes and defines specific to windows (such as
  TCHAR) needed by WinUAE CPU core.
  The aim is to have minimum changes in WinUae CPU core for next updates.
*/

#ifndef HATARI_COMPAT_H
#define HATARI_COMPAT_H

#include <ctype.h>
#include "uae/string.h"

#define strnicmp strncasecmp

#define console_out printf
//#define console_out_f printf
#define console_out_f(...)	{ if ( console_out_FILE ) fprintf ( console_out_FILE , __VA_ARGS__ ); else printf ( __VA_ARGS__ ); }
#define gui_message console_out_f

#define uae_log printf


static inline void to_upper (TCHAR *s, int len) {
	int i;
	for (i = 0; i < len; i++) {
		s[i] = toupper(s[i]);
	}
}


static inline void my_trim (TCHAR *s)
{
	int len;
	while (_tcslen (s) > 0 && _tcscspn (s, _T("\t \r\n")) == 0)
		memmove (s, s + 1, (_tcslen (s + 1) + 1) * sizeof (TCHAR));
	len = _tcslen (s);
	while (len > 0 && _tcscspn (s + len - 1, _T("\t \r\n")) == 0)
		s[--len] = '\0';
}


#endif

