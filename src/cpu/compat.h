/*
  Hatari - compat.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains all the includes and defines specific to windows (such as TCHAR)
  needed by WinUae CPU core. 
  The aim is to have minimum changes in WinUae CPU core for next updates
*/

#ifndef HATARI_COMPAT_H
#define HATARI_COMPAT_H

#include "sysconfig.h"

#ifndef REGPARAM
#define REGPARAM
#endif

#ifndef REGPARAM2
#define REGPARAM2
#endif

#ifndef REGPARAM3
#define REGPARAM3
#endif

#ifndef TCHAR
#define TCHAR char
#endif

#ifndef STATIC_INLINE
#define STATIC_INLINE static inline
#endif

#define false 0
#define true 1

#ifndef bool
#define bool int
#endif

/*
#ifndef SPCFLAG_MODE_CHANGE
#define SPCFLAG_MODE_CHANGE 8192
#endif
*/

#define _vsnprintf vsnprintf
#define _tcsncmp strncmp
#define _istspace isspace
#define _tcscmp strcmp
#define _tcslen strlen

#endif
