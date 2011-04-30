/*
  Hatari - scandir.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
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
extern int alphasort(const void *d1, const void *d2);
#endif

#if !HAVE_SCANDIR
extern int scandir(const char *dirname, struct dirent ***namelist, int (*sdfilter)(struct dirent *), int (*dcomp)(const void *, const void *));
#endif

#endif /* HATARI_SCANDIR_H */
