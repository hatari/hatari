/*
  Hatari - netusbee.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  The EtherNEC and the NetUSBee on the cartridge port (netusbee.c).

  - EtherNEC: an NE2000 (RTL8019AS) network chip on an 8-bit bus. The
    cartridge port can only be read, so a register read is a read at
    $FA0000 + reg * 512, and a register write is a read at
    $FB0000 + reg * 512 + data * 2.
  - NetUSBee: the same NE2000, and an ISP1160 USB host (not emulated yet).
  - The card reaches the host's network through libslirp (hostnet.c): the
    guest is 10.0.2.15, the gateway 10.0.2.2, DNS 10.0.2.3.
*/

#ifndef HATARI_NETUSBEE_H
#define HATARI_NETUSBEE_H

#include <stdint.h>
#include <stdbool.h>

/* Set the card up as the configuration says: at start, and at each reset
 * (only what changed changes). */
extern void NetUSBee_Apply(void);
extern void NetUSBee_UnInit(void);
/* Whether a card holds the cartridge port now. */
extern bool NetUSBee_OnCartPort(void);
/* Whether the configuration puts a card on the cartridge port. */
extern bool NetUSBee_Configured(void);
/* A reset: a cold one powers the card again (the cartridge port has no
 * reset line). */
extern void NetUSBee_Reset(bool bCold);
/* A CPU read in $FA0000-$FBFFFF. first: the first byte of the bus cycle
 * (a byte access, or the even byte of a word). */
extern uint8_t NetUSBee_ReadByte(uint32_t addr, bool first);
/* Each VBL: frames from the network into the card. */
extern void NetUSBee_Vbl(void);
extern void NetUSBee_MemorySnapShot_Capture(bool bSave);

#endif /* HATARI_NETUSBEE_H */
