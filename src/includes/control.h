/*
  Hatari - change.h
  
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_CONTROL_H
#define HATARI_CONTROL_H

#include "main.h"

extern void Control_ProcessBuffer(char *buffer);

/* supported only on BSD compatible / POSIX compliant systems */
#if HAVE_UNIX_DOMAIN_SOCKETS
extern bool Control_CheckUpdates(void);
extern const char* Control_SetSocket(const char *socketpath);
extern void Control_ReparentWindow(int width, int height, bool noembed);
#else
#define Control_CheckUpdates() false
#define Control_SetSocket(path) "Control socket is not supported on this platform."
#define Control_ReparentWindow(width, height, noembed);
#endif /* HAVE_UNIX_DOMAIN_SOCKETS */

#endif /* HATARI_CONTROL_H */
