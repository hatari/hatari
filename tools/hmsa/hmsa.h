/*
  Hatari tool: Magic Shadow Archiver - hmsa.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HMSA_H
#define HMSA_H

#include <SDL_types.h>

typedef int BOOL;

#define DRIVE_BUFFER_BYTES    (4*1024*1024)  /* 4MiB area for a disk image */

#ifndef FALSE
#define FALSE 0
#define TRUE (!0)
#endif

#ifndef NULL
#define NULL 0L
#endif

#endif /* HMSA_H */
