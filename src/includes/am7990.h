/*
  Hatari - am7990.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  The AMD Am7990 LANCE Ethernet chip: its 4 registers (CSR0-3 through the
  address and data ports), the initialization block, and the receive and
  transmit descriptor rings in the memory it shares with the CPU. The card
  wiring (which bus, which memory, the interrupt) is in the card's file
  (riebl.c).
*/

#ifndef HATARI_AM7990_H
#define HATARI_AM7990_H

#include <stdint.h>
#include <stdbool.h>

#define AM7990_FRAME_MAX	1518		/* without the CRC */

/* The memory the chip sees: a big-endian byte image (the 68000's order),
 * addressed with the chip's 24 address bits masked by mask. send gets the
 * frames the chip transmits. */
typedef struct
{
	uint8_t *ram;
	uint32_t mask;
	void (*send)(const uint8_t *frame, int len);
} am7990_bus_t;

typedef struct
{
	uint16_t csr0, csr1, csr2, csr3;
	uint16_t rap;
	/* from the initialization block */
	uint16_t mode;
	uint8_t padr[6];
	uint16_t ladrf[4];
	uint32_t rdra, tdra;		/* ring addresses */
	uint32_t nrx, ntx;		/* ring lengths */
	uint32_t rxi, txi;		/* the next descriptor of each ring */
	uint8_t tx_frame[AM7990_FRAME_MAX + 4];
} am7990_t;

/* Hardware reset: the chip stops. */
extern void Am7990_Reset(am7990_t *s);

/* The register address port (RAP) and the register data port (RDP). */
extern uint16_t Am7990_ReadRAP(am7990_t *s);
extern void Am7990_WriteRAP(am7990_t *s, uint16_t val);
extern uint16_t Am7990_ReadRDP(am7990_t *s);
extern void Am7990_WriteRDP(am7990_t *s, const am7990_bus_t *bus, uint16_t val);

/* The interrupt line: INTR and INEA in CSR0. */
extern bool Am7990_IRQ(const am7990_t *s);

/* A frame from the network (without CRC). Returns false when the receive
 * ring has no free buffer for it (keep it and try again later), true when
 * the chip took it or dropped it (receiver off, address filter). */
extern bool Am7990_Receive(am7990_t *s, const am7990_bus_t *bus,
			   const uint8_t *frame, int len);

/* Look for frames to send in the transmit ring (the chip polls it). */
extern void Am7990_Poll(am7990_t *s, const am7990_bus_t *bus);

#endif /* HATARI_AM7990_H */
