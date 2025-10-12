/*
  Hatari - change.h
  
  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_CONTROL_H
#define HATARI_CONTROL_H

#include "main.h"

extern void Control_ProcessBuffer(const char *buffer);

/* supported only on BSD compatible / POSIX compliant systems */
#if HAVE_UNIX_DOMAIN_SOCKETS
extern void Control_SendEmbedSize(int width, int height);
extern bool Control_CheckUpdates(void);
extern void Control_RemoveFifo(void);
extern const char* Control_SetFifo(const char *fifopath);
extern const char* Control_SetSocket(const char *socketpath);
#else
#define Control_CheckUpdates() false
#define Control_RemoveFifo() false
#define Control_SetFifo(path) "Command FIFO is not supported on this platform."
#define Control_SetSocket(path) "Control socket is not supported on this platform."
#endif /* HAVE_UNIX_DOMAIN_SOCKETS */

#endif /* HATARI_CONTROL_H */
