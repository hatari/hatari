/*
  Hatari - hostnet.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Ethernet frames to and from the host, for emulated network adapters.
*/

#ifndef HATARI_HOSTNET_H
#define HATARI_HOSTNET_H

#include <stdbool.h>
#include <stdint.h>

#define HOSTNET_FRAME_MAX  1518   /* ethernet frame without CRC */

typedef struct hostnet hostnet_t;

/*
 * Open a host network backend. spec is "<backend>[,<option>...]":
 *
 *   tap:<if> (or just <if>)  a TAP interface (Linux)
 *   slirp                    user-mode NAT, no host setup or privileges
 *                            options: net=<a.b.c.0/nn> host=<addr>
 *                            dns=<addr> guest=<addr>
 *                            hostfwd=tcp|udp:[<hostaddr>:]<hostport>-[<guestaddr>]:<guestport>
 *   pcap:<if>                bridge onto a host interface (libpcap, Npcap)
 *   pcap:list                print the interfaces pcap can open
 *
 * Options the backend doesn't know are left to the caller: unknown(opt)
 * is called for each (it may be NULL). mac: the adapter's address, for
 * backends that need it. Returns NULL (with an error logged) on failure;
 * desc receives a short description for log messages.
 */
extern hostnet_t *HostNet_Open(const char *spec, const uint8_t mac[6],
                               bool (*unknown)(const char *opt),
                               char *desc, int desclen);

/* The next frame for the guest: its length, or 0 when none is queued. */
extern int HostNet_Recv(hostnet_t *net, uint8_t *frame, int max);

/* A frame from the guest. */
extern bool HostNet_Send(hostnet_t *net, const uint8_t *frame, int len);

/* Drop every frame queued for the guest. */
extern void HostNet_Flush(hostnet_t *net);

extern void HostNet_Close(hostnet_t *net);

#endif
