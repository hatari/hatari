/*
  Hatari - change.h
  
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_CONTROL_H
#define HATARI_CONTROL_H

#include "config.h"
#include "main.h"

/* supported only on BSD compatible / POSIX compliant systems */
#if HAVE_UNIX_DOMAIN_SOCKETS
extern void Control_CheckUpdates(void);
extern const char* Control_SetSocket(const char *socketpath);
#else
#define Control_CheckUpdates()
#define Control_SetSocket(path) "Control socket is not supported on this platform."
#endif /* HAVE_UNIX_DOMAIN_SOCKETS */

#endif /* HATARI_CONTROL_H */
