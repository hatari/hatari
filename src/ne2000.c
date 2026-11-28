/*
 * Hatari - ne2000.c
 *
 * The NE2000 network chip of the EtherNEC and the NetUSBee: a DP8390 with
 * the RTL8019AS additions. Based on QEMU's NE2000 emulation (hw/net/ne2000.c),
 * which carries this notice:
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Changes for Hatari, under the same license:
 * - no host code: the caller gives the received frames and sends the
 *   transmitted ones (netusbee.c), and saves the state as one block
 * - the data port is 8 bits wide (the EtherNEC and NetUSBee wire it so)
 * - memory from NIC address 32 to 32 KB, the PROM (0-31) read-only
 * - the RTL8019AS ID bytes, the page 2 read-back and page 3 registers
 * - the address filter uses the PAR registers, not the PROM
 * - frames shorter than 60 bytes are padded, as a wire would give them
 * - a full receive ring keeps the frame for later instead of dropping it
 * - STP sets the RST status bit; the tally counters clear when read
 */
const char NE2000_fileid[] = "Hatari ne2000.c";

#include <string.h>
#include "ne2000.h"

/* CR */
#define CR_STP		0x01
#define CR_STA		0x02
#define CR_TXP		0x04
#define CR_RD0		0x08
#define CR_RD1		0x10
#define CR_RD2		0x20

/* ISR */
#define ISR_PRX		0x01
#define ISR_PTX		0x02
#define ISR_RDC		0x40
#define ISR_RST		0x80

/* RCR */
#define RCR_AB		0x04
#define RCR_AM		0x08
#define RCR_PRO		0x10
#define RCR_MON		0x20

/* RSR, TSR */
#define RSR_PRX		0x01
#define RSR_PHY		0x20
#define RSR_DIS		0x40
#define TSR_PTX		0x01

#define PROM_SIZE	32
#define FRAME_MIN	60		/* without the CRC */
#define TX_MAX		1518

static uint8_t mem_read(ne2000_t *s, uint32_t addr)
{
	return addr < NE2000_MEM_SIZE ? s->mem[addr] : 0xff;
}

static void mem_write(ne2000_t *s, uint32_t addr, uint8_t val)
{
	if (addr >= PROM_SIZE && addr < NE2000_MEM_SIZE)
		s->mem[addr] = val;
}

void NE2000_PowerOn(ne2000_t *s, const uint8_t mac[6])
{
	int i;

	memset(s, 0, sizeof(*s));
	memcpy(s->mac, mac, 6);
	/* the PROM: the address, then 0x57 0x57 ("WW": an NE2000) at 14 and
	 * 15, each byte twice */
	for (i = 0; i < PROM_SIZE / 2; i++)
	{
		uint8_t v = i < 6 ? mac[i] : (i >= 14 ? 0x57 : 0);
		s->mem[2 * i] = s->mem[2 * i + 1] = v;
	}
	s->pstart = 0x46;
	s->pstop = 0x80;
	s->bnry = 0x46;
	s->curr = 0x47;
	NE2000_Reset(s);
}

void NE2000_Reset(ne2000_t *s)
{
	s->cr = CR_RD2 | CR_STP;
	s->isr = ISR_RST;
	s->imr = 0;
	s->rbcr = 0;
}

/* One byte of remote DMA done: the address wraps at the end of the
 * receive ring, the count reaching 0 sets RDC. */
static void dma_step(ne2000_t *s)
{
	s->rsar++;
	if (s->pstop > s->pstart && s->rsar == (uint16_t)(s->pstop << 8))
		s->rsar = s->pstart << 8;
	if (s->rbcr && --s->rbcr == 0)
		s->isr |= ISR_RDC;
}

static uint8_t dma_read(ne2000_t *s)
{
	uint8_t v = mem_read(s, s->rsar);

	if (s->rbcr)
		dma_step(s);
	return v;
}

static void dma_write(ne2000_t *s, uint8_t val)
{
	if (!s->rbcr)
		return;
	mem_write(s, s->rsar, val);
	dma_step(s);
}

uint8_t NE2000_Read(ne2000_t *s, int reg)
{
	int page;

	reg &= 31;
	if (reg >= 0x18)
		return 0xff;		/* the reset port: a write resets */
	if (reg >= 0x10)
		return dma_read(s);
	if (reg == 0)
		return s->cr;

	page = s->cr >> 6;
	switch ((page << 4) | reg)
	{
	/* page 0 */
	case 0x01: return s->clda & 0xff;
	case 0x02: return s->clda >> 8;
	case 0x03: return s->bnry;
	case 0x04: return s->tsr;
	case 0x05: return 0;			/* NCR: no collisions */
	case 0x06: return 0;			/* FIFO */
	case 0x07: return s->isr;
	case 0x08: return s->rsar & 0xff;	/* CRDA */
	case 0x09: return s->rsar >> 8;
	case 0x0a: return 0x50;			/* RTL8019AS ID: 'P' */
	case 0x0b: return 0x70;			/* 'p' */
	case 0x0c: return s->rsr | ((s->rcr & RCR_MON) ? RSR_DIS : 0);
	case 0x0d: case 0x0e: case 0x0f:	/* tally counters */
	{
		uint8_t v = s->cntr[reg - 0x0d];
		s->cntr[reg - 0x0d] = 0;
		return v;
	}
	/* page 1 */
	case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16:
		return s->par[reg - 1];
	case 0x17: return s->curr;
	case 0x18: case 0x19: case 0x1a: case 0x1b:
	case 0x1c: case 0x1d: case 0x1e: case 0x1f:
		return s->mar[reg - 8];
	/* page 2: what pages 0 writes, the bits that are always 1 read 1 */
	case 0x21: return s->pstart;
	case 0x22: return s->pstop;
	case 0x23: return s->bnry;		/* remote next packet pointer */
	case 0x24: return s->tpsr;
	case 0x25: return s->curr;		/* local next packet pointer */
	case 0x26: return s->clda >> 8;
	case 0x27: return s->clda & 0xff;
	case 0x2c: return s->rcr | 0xc0;
	case 0x2d: return s->tcr | 0xe0;
	case 0x2e: return s->dcr | 0x80;
	case 0x2f: return s->imr | 0x80;
	/* page 3: RTL8019AS configuration */
	case 0x31: return s->cr9346;
	case 0x33: return s->config[0];		/* UTP, link good */
	case 0x34: return s->config[1];
	case 0x35: return s->config[2];
	case 0x36: return s->config[3];
	default:
		return 0;
	}
}

/* CR written: start, stop, remote DMA, transmit. */
static bool write_cr(ne2000_t *s, uint8_t val)
{
	s->cr = val & ~CR_TXP;
	if (val & CR_STP)
	{
		s->isr |= ISR_RST;
		return false;
	}
	s->isr &= ~ISR_RST;
	if ((val & (CR_RD0 | CR_RD1)) && !(val & CR_RD2) && s->rbcr == 0)
		s->isr |= ISR_RDC;	/* a remote DMA of 0 bytes is done at once */
	if ((val & CR_TXP) && (val & CR_STA))
	{
		s->tsr = TSR_PTX;
		s->isr |= ISR_PTX;
		s->clda = (s->tpsr << 8) + s->tbcr;
		return true;
	}
	return false;
}

bool NE2000_Write(ne2000_t *s, int reg, uint8_t val)
{
	int page;

	reg &= 31;
	if (reg >= 0x18)
	{
		NE2000_Reset(s);
		return false;
	}
	if (reg >= 0x10)
	{
		dma_write(s, val);
		return false;
	}
	if (reg == 0)
		return write_cr(s, val);

	page = s->cr >> 6;
	switch ((page << 4) | reg)
	{
	/* page 0 */
	case 0x01: s->pstart = val; break;
	case 0x02: s->pstop = val; break;
	case 0x03: s->bnry = val; break;
	case 0x04: s->tpsr = val; break;
	case 0x05: s->tbcr = (s->tbcr & 0xff00) | val; break;
	case 0x06: s->tbcr = (s->tbcr & 0x00ff) | (val << 8); break;
	case 0x07: s->isr &= ~(val & 0x7f); break;	/* a 1 clears */
	case 0x08: s->rsar = (s->rsar & 0xff00) | val; break;
	case 0x09: s->rsar = (s->rsar & 0x00ff) | (val << 8); break;
	case 0x0a: s->rbcr = (s->rbcr & 0xff00) | val; break;
	case 0x0b: s->rbcr = (s->rbcr & 0x00ff) | (val << 8); break;
	case 0x0c: s->rcr = val & 0x3f; break;
	case 0x0d: s->tcr = val & 0x1f; break;
	case 0x0e: s->dcr = val & 0x7f; break;
	case 0x0f: s->imr = val & 0x7f; break;
	/* page 1 */
	case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16:
		s->par[reg - 1] = val;
		break;
	case 0x17: s->curr = val; break;
	case 0x18: case 0x19: case 0x1a: case 0x1b:
	case 0x1c: case 0x1d: case 0x1e: case 0x1f:
		s->mar[reg - 8] = val;
		break;
	/* page 3: RTL8019AS configuration, CONFIG1-3 when 9346CR allows */
	case 0x31: s->cr9346 = val & 0xcf; break;
	case 0x34: case 0x35: case 0x36:
		if ((s->cr9346 & 0xc0) == 0xc0)
			s->config[reg - 3] = val;
		break;
	default:
		break;
	}
	return false;
}

const uint8_t *NE2000_TxFrame(ne2000_t *s, int *len)
{
	uint32_t start = s->tpsr << 8;
	int n = s->tbcr;

	if (start >= NE2000_MEM_SIZE)
	{
		*len = 0;
		return s->mem;
	}
	if (n > TX_MAX)
		n = TX_MAX;
	if (start + n > NE2000_MEM_SIZE)
		n = NE2000_MEM_SIZE - start;
	*len = n;
	return s->mem + start;
}

/* The DP8390's multicast hash: the top 6 bits of the address's CRC */
static unsigned int mcast_index(const uint8_t *addr)
{
	uint32_t crc = 0xffffffff;
	int i, b;

	for (i = 0; i < 6; i++)
	{
		uint8_t c = addr[i];
		for (b = 0; b < 8; b++)
		{
			uint32_t carry = ((crc >> 31) ^ c) & 1;
			crc <<= 1;
			c >>= 1;
			if (carry)
				crc = (crc ^ 0x04c11db6) | carry;
		}
	}
	return crc >> 26;
}

bool NE2000_Receive(ne2000_t *s, const uint8_t *frame, int len)
{
	static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint8_t pad[FRAME_MIN];
	uint32_t start = s->pstart << 8, stop = s->pstop << 8;
	uint32_t index, boundary, avail, need, next, p;
	int total, i;

	if ((s->cr & CR_STP) || len < 14 || stop <= start || stop > NE2000_MEM_SIZE)
		return true;	/* stopped, or no ring: the frame is lost */

	if (!(s->rcr & RCR_PRO))
	{
		if (memcmp(frame, bcast, 6) == 0)
		{
			if (!(s->rcr & RCR_AB))
				return true;
		}
		else if (frame[0] & 1)
		{
			unsigned int idx;
			if (!(s->rcr & RCR_AM))
				return true;
			idx = mcast_index(frame);
			if (!(s->mar[idx >> 3] & (1 << (idx & 7))))
				return true;
		}
		else if (memcmp(frame, s->par, 6) != 0)
			return true;
	}
	if (s->rcr & RCR_MON)
		return true;	/* monitor mode: checked, not stored */

	if (len < FRAME_MIN)
	{
		memcpy(pad, frame, len);
		memset(pad + len, 0, FRAME_MIN - len);
		frame = pad;
		len = FRAME_MIN;
	}
	if (len > NE2000_FRAME_MAX)
		len = NE2000_FRAME_MAX;

	/* room up to the boundary page, whole pages with the 4-byte header
	 * and the CRC */
	index = s->curr << 8;
	if (index < start || index >= stop)
		index = start;
	boundary = s->bnry << 8;
	if (index < boundary)
		avail = boundary - index;
	else
		avail = (stop - start) - (index - boundary);
	total = len + 4;
	need = (total + 4 + 255) & ~255u;
	if (need >= avail)
		return false;
	next = index + need;
	if (next >= stop)
		next -= stop - start;

	s->rsr = RSR_PRX | ((frame[0] & 1) ? RSR_PHY : 0);
	s->mem[index] = s->rsr;
	s->mem[index + 1] = next >> 8;
	s->mem[index + 2] = total & 0xff;
	s->mem[index + 3] = total >> 8;
	p = index + 4;
	for (i = 0; i < len; i++)
	{
		s->mem[p++] = frame[i];
		if (p == stop)
			p = start;
	}
	s->clda = p;
	s->curr = next >> 8;
	s->isr |= ISR_PRX;
	return true;
}
