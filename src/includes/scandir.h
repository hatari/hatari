/*
  Hatari - scandir.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_SCANDIR_H
#define HATARI_SCANDIR_H

#include "config.h"
#include <dirent.h>

#ifdef QNX
#include <sys/types.h>
#include <sys/dir.h>
#define dirent direct
#endif

#if !HAVE_ALPHASORT
extern int alphasort(const struct dirent **d1, const struct dirent **d2);
#endif

#if !HAVE_SCANDIR
extern int scandir(const char *dirname, struct dirent ***namelist,
                   int (*sdfilter)(const struct dirent *),
                   int (*comp)(const struct dirent **, const struct dirent **));
#endif

#endif /* HATARI_SCANDIR_H */
