/*
  Hatari - ne2000.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
  (ne2000.c itself is under the MIT license, see there.)

  The NE2000 network chip (a DP8390 with the RTL8019AS additions) of the
  EtherNEC and the NetUSBee: its 32 registers, its 32 KB buffer memory and
  the address PROM. The card wiring (which bus, 8 bits) is in netusbee.c.
*/

#ifndef HATARI_NE2000_H
#define HATARI_NE2000_H

#include <stdint.h>
#include <stdbool.h>

#define NE2000_MEM_SIZE	0x8000		/* NIC address space with memory behind it */
#define NE2000_FRAME_MAX	1514		/* without the CRC */

typedef struct
{
	uint8_t cr, isr, imr, dcr, rcr, tcr;
	uint8_t tsr, rsr;
	uint8_t pstart, pstop, bnry, curr, tpsr;
	uint16_t tbcr, rbcr, rsar, clda;
	uint8_t par[6], mar[8];
	uint8_t cntr[3];
	uint8_t cr9346, config[4];
	uint8_t mac[6];				/* the address in the PROM */
	uint8_t mem[NE2000_MEM_SIZE];		/* PROM at 0-31, then memory */
} ne2000_t;

/* Power on: the PROM gets mac, the chip is reset. */
extern void NE2000_PowerOn(ne2000_t *s, const uint8_t mac[6]);
/* The reset port: the chip stops, as after a hardware reset. */
extern void NE2000_Reset(ne2000_t *s);

/* reg: 0-15 the DP8390 registers of the page CR selects, 16-23 the data
 * port (remote DMA), 24-31 the reset port. NE2000_Write returns true when
 * the write started a transmission: NE2000_TxFrame gives the frame. */
extern uint8_t NE2000_Read(ne2000_t *s, int reg);
extern bool NE2000_Write(ne2000_t *s, int reg, uint8_t val);
extern const uint8_t *NE2000_TxFrame(ne2000_t *s, int *len);

/* A frame from the network. Returns false when the receive ring has no
 * room for it (keep it and try again later), true when the chip took it
 * or its address filter dropped it. */
extern bool NE2000_Receive(ne2000_t *s, const uint8_t *frame, int len);

#endif /* HATARI_NE2000_H */
