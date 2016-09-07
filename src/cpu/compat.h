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

#include "uae/string.h"

#define strnicmp strncasecmp


#define f_out fprintf
#define console_out printf
//#define console_out_f printf
#define console_out_f(...)	{ if ( console_out_FILE ) fprintf ( console_out_FILE , __VA_ARGS__ ); else printf ( __VA_ARGS__ ); }
#define error_log printf
#define gui_message console_out_f

#define uae_log printf

#endif

