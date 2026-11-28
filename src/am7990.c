/*
  Hatari - am7990.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  The AMD Am7990 LANCE (Local Area Network Controller for Ethernet), as on
  the Riebl, PAM and BVME410 VME cards. See am7990.h.

  The chip has two ports: RAP selects one of the 4 registers, RDP reads and
  writes it. CSR0 is the control and status register, CSR1 and CSR2 hold
  the address of the initialization block, CSR3 the bus mode (BSWP: the
  data buffers are in big-endian byte order). CSR1-3 can only be written
  while the chip is stopped.

  INIT reads the initialization block: the mode, the station address, the
  multicast filter and the two descriptor rings. Each descriptor is 4 words:
  the buffer address, the OWN bit and flags, the negative buffer size, and
  (for received frames) the frame length. The CPU gives a descriptor to the
  chip with OWN; the chip gives it back when the frame is in it (receive)
  or sent (transmit), and sets RINT or TINT.

  Not emulated: collisions, babble, the chip's timing (frames go at once),
  memory errors, the transmit CRC options other than DTCR.

  References :
    - AMD Am7990 Local Area Network Controller for Ethernet, data sheet
    - NetBSD sys/dev/ic/lance.c, am7990.c and lancereg.h (no code taken)
*/
const char Am7990_fileid[] = "Hatari am7990.c";

#include <string.h>
#include <stdio.h>
#include "main.h"
#include "log.h"
#include "am7990.h"

/* CSR0 */
#define C0_ERR		0x8000
#define C0_BABL		0x4000
#define C0_CERR		0x2000
#define C0_MISS		0x1000
#define C0_MERR		0x0800
#define C0_RINT		0x0400
#define C0_TINT		0x0200
#define C0_IDON		0x0100
#define C0_INTR		0x0080
#define C0_INEA		0x0040
#define C0_RXON		0x0020
#define C0_TXON		0x0010
#define C0_TDMD		0x0008
#define C0_STOP		0x0004
#define C0_STRT		0x0002
#define C0_INIT		0x0001
#define C0_W1C		(C0_BABL | C0_CERR | C0_MISS | C0_MERR | C0_RINT | C0_TINT | C0_IDON)

/* CSR3 */
#define C3_BSWP		0x0004

/* the mode word of the initialization block */
#define M_PROM		0x8000
#define M_DTCR		0x0008
#define M_LOOP		0x0004
#define M_DTX		0x0002
#define M_DRX		0x0001

/* descriptor word 1 */
#define D1_OWN		0x8000
#define D1_ERR		0x4000
#define D1_STP		0x0200
#define D1_ENP		0x0100
#define D1_HADR		0x00ff
#define TMD1_MORE	0x1000
#define TMD1_ONE	0x0800
#define TMD1_DEF	0x0400

#define ETH_MIN		60		/* the shortest frame, without CRC */


static uint16_t rd16(const am7990_bus_t *b, uint32_t a)
{
	a &= b->mask & ~1u;
	return (b->ram[a] << 8) | b->ram[a + 1];
}

static void wr16(const am7990_bus_t *b, uint32_t a, uint16_t v)
{
	a &= b->mask & ~1u;
	b->ram[a] = v >> 8;
	b->ram[a + 1] = v;
}

/* Data buffer bytes: without BSWP the chip puts the first byte of each
 * word on bits 7-0, the odd address on a big-endian bus. */
static uint8_t rd8(const am7990_t *s, const am7990_bus_t *b, uint32_t a)
{
	if (!(s->csr3 & C3_BSWP))
		a ^= 1;
	return b->ram[a & b->mask];
}

static void wr8(const am7990_t *s, const am7990_bus_t *b, uint32_t a, uint8_t v)
{
	if (!(s->csr3 & C3_BSWP))
		a ^= 1;
	b->ram[a & b->mask] = v;
}

/* the Ethernet CRC, bit-reversed, without the final inversion */
static uint32_t crc32_le(const uint8_t *p, int n)
{
	uint32_t c = 0xffffffff;
	int k;

	while (n--)
	{
		c ^= *p++;
		for (k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320 & -(c & 1));
	}
	return c;
}

/* the buffer size of a descriptor: word 2 holds it negative, in 12 bits */
static int buf_size(uint16_t w2)
{
	int n = (0x10000 - (w2 | 0xf000)) & 0xffff;
	return n ? n : 0x1000;
}

static uint32_t buf_addr(uint16_t w0, uint16_t w1)
{
	return ((uint32_t)(w1 & D1_HADR) << 16) | w0;
}

static uint16_t csr0_value(const am7990_t *s)
{
	uint16_t v = s->csr0 & ~(C0_ERR | C0_INTR);

	if (v & (C0_BABL | C0_CERR | C0_MISS | C0_MERR))
		v |= C0_ERR;
	if (v & (C0_BABL | C0_MISS | C0_MERR | C0_RINT | C0_TINT | C0_IDON))
		v |= C0_INTR;
	return v;
}

bool Am7990_IRQ(const am7990_t *s)
{
	uint16_t v = csr0_value(s);
	return (v & C0_INTR) && (v & C0_INEA);
}

void Am7990_Reset(am7990_t *s)
{
	memset(s, 0, sizeof(*s));
	s->csr0 = C0_STOP;
	s->nrx = s->ntx = 1;
}

/* INIT: read the initialization block at CSR2:CSR1 */
static void do_init(am7990_t *s, const am7990_bus_t *b)
{
	uint32_t ia = ((uint32_t)(s->csr2 & 0xff) << 16) | (s->csr1 & 0xfffe);
	uint16_t w;
	int i;

	s->mode = rd16(b, ia);
	for (i = 0; i < 3; i++)
	{
		w = rd16(b, ia + 2 + 2 * i);
		s->padr[2 * i] = w;
		s->padr[2 * i + 1] = w >> 8;
	}
	for (i = 0; i < 4; i++)
		s->ladrf[i] = rd16(b, ia + 8 + 2 * i);
	w = rd16(b, ia + 18);
	s->rdra = buf_addr(rd16(b, ia + 16), w) & ~7u;
	s->nrx = 1u << ((w >> 13) & 7);
	w = rd16(b, ia + 22);
	s->tdra = buf_addr(rd16(b, ia + 20), w) & ~7u;
	s->ntx = 1u << ((w >> 13) & 7);
	s->rxi = s->txi = 0;

	s->csr0 &= ~C0_STOP;
	s->csr0 |= C0_INIT | C0_IDON;
}

static void do_start(am7990_t *s, const am7990_bus_t *b)
{
	s->csr0 &= ~C0_STOP;
	s->csr0 |= C0_STRT;
	if (!(s->mode & M_DRX))
		s->csr0 |= C0_RXON;
	if (!(s->mode & M_DTX))
		s->csr0 |= C0_TXON;
	Am7990_Poll(s, b);
}

static void write_csr0(am7990_t *s, const am7990_bus_t *b, uint16_t val)
{
	if (val & C0_STOP)
	{
		/* STOP wins over everything else, and clears the rest */
		s->csr0 = C0_STOP;
		return;
	}
	s->csr0 &= ~(val & C0_W1C);
	s->csr0 = (s->csr0 & ~C0_INEA) | (val & C0_INEA);
	/* INIT and STRT only act once: drivers write back what they read */
	if ((val & C0_INIT) && !(s->csr0 & C0_INIT))
		do_init(s, b);
	if ((val & C0_STRT) && !(s->csr0 & C0_STRT))
		do_start(s, b);
	if (val & C0_TDMD)
		Am7990_Poll(s, b);
}

uint16_t Am7990_ReadRAP(am7990_t *s)
{
	return s->rap;
}

void Am7990_WriteRAP(am7990_t *s, uint16_t val)
{
	s->rap = val & 3;
}

uint16_t Am7990_ReadRDP(am7990_t *s)
{
	switch (s->rap)
	{
	 case 0:
		return csr0_value(s);
	 case 1:
		return s->csr1;
	 case 2:
		return s->csr2;
	 default:
		return s->csr3;
	}
}

void Am7990_WriteRDP(am7990_t *s, const am7990_bus_t *b, uint16_t val)
{
	if (s->rap == 0)
	{
		write_csr0(s, b, val);
		return;
	}
	if (!(s->csr0 & C0_STOP))
		return;		/* CSR1-3: only while stopped */
	switch (s->rap)
	{
	 case 1:
		s->csr1 = val & 0xfffe;
		break;
	 case 2:
		s->csr2 = val & 0x00ff;
		break;
	 default:
		s->csr3 = val & 0x0007;
		break;
	}
}

/* the address filter: ours, broadcast, a multicast in the filter, or all */
static bool accept(const am7990_t *s, const uint8_t *f)
{
	static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint32_t h;

	if (s->mode & M_PROM)
		return true;
	if (memcmp(f, s->padr, 6) == 0)
		return true;
	if (!(f[0] & 1))
		return false;
	if (memcmp(f, bcast, 6) == 0)
		return true;
	h = crc32_le(f, 6) >> 26;
	return s->ladrf[h >> 4] & (1 << (h & 15));
}

bool Am7990_Receive(am7990_t *s, const am7990_bus_t *b, const uint8_t *frame, int len)
{
	uint8_t buf[AM7990_FRAME_MAX + 4];
	uint32_t fcs, d, i;
	int total, room, off, n;

	if (!(s->csr0 & C0_RXON) || len < 14 || len > AM7990_FRAME_MAX)
		return true;
	if (!accept(s, frame))
		return true;

	/* the frame as it was on the wire: padded, with its CRC */
	n = len < ETH_MIN ? ETH_MIN : len;
	memcpy(buf, frame, len);
	memset(buf + len, 0, n - len);
	fcs = ~crc32_le(buf, n);
	buf[n] = fcs;
	buf[n + 1] = fcs >> 8;
	buf[n + 2] = fcs >> 16;
	buf[n + 3] = fcs >> 24;
	total = n + 4;

	/* enough buffers that the CPU gave to the chip? */
	room = 0;
	for (i = 0; i < s->nrx && room < total; i++)
	{
		d = s->rdra + 8 * ((s->rxi + i) % s->nrx);
		if (!(rd16(b, d + 2) & D1_OWN))
			return false;
		room += buf_size(rd16(b, d + 4));
	}
	if (room < total)
	{
		/* the whole ring is too small for it: lost */
		s->csr0 |= C0_MISS;
		return true;
	}

	off = 0;
	i = s->rxi;
	while (off < total)
	{
		uint16_t w1, flags;
		uint32_t a;
		int k, chunk;

		d = s->rdra + 8 * i;
		w1 = rd16(b, d + 2);
		a = buf_addr(rd16(b, d), w1);
		chunk = buf_size(rd16(b, d + 4));
		if (chunk > total - off)
			chunk = total - off;
		for (k = 0; k < chunk; k++)
			wr8(s, b, a + k, buf[off + k]);
		flags = off == 0 ? D1_STP : 0;
		off += chunk;
		if (off == total)
		{
			flags |= D1_ENP;
			wr16(b, d + 6, total & 0x0fff);
		}
		wr16(b, d + 2, (w1 & D1_HADR) | flags);	/* OWN goes back to the CPU last */
		i = (i + 1) % s->nrx;
	}
	s->rxi = i;
	s->csr0 |= C0_RINT;
	return true;
}

void Am7990_Poll(am7990_t *s, const am7990_bus_t *b)
{
	uint32_t sent = 0;

	s->csr0 &= ~C0_TDMD;
	if (!(s->csr0 & C0_TXON))
		return;
	while (sent < s->ntx)
	{
		uint32_t i, j, cnt = 0;
		int n = 0;
		bool end = false;

		/* the frame: from the descriptor with STP to the one with ENP */
		for (j = s->txi; cnt < s->ntx; j = (j + 1) % s->ntx)
		{
			uint32_t d = s->tdra + 8 * j;
			uint16_t w1 = rd16(b, d + 2);
			uint32_t a;
			int k, size;

			if (!(w1 & D1_OWN))
				break;
			a = buf_addr(rd16(b, d), w1);
			size = buf_size(rd16(b, d + 4));
			for (k = 0; k < size && n < (int)sizeof(s->tx_frame); k++)
				s->tx_frame[n++] = rd8(s, b, a + k);
			cnt++;
			if (w1 & D1_ENP)
			{
				end = true;
				break;
			}
		}
		if (!end)
			return;		/* nothing, or the rest is not ready yet */

		if ((s->mode & M_DTCR) && n >= 4)
			n -= 4;		/* the CPU wrote the CRC itself */
		if (n > AM7990_FRAME_MAX)
			n = AM7990_FRAME_MAX;
		if (s->mode & M_LOOP)
			Am7990_Receive(s, b, s->tx_frame, n);
		else if (b->send && n >= 14)
			b->send(s->tx_frame, n);

		for (i = 0; i < cnt; i++)
		{
			uint32_t d = s->tdra + 8 * ((s->txi + i) % s->ntx);
			uint16_t w1 = rd16(b, d + 2);

			wr16(b, d + 6, 0);
			wr16(b, d + 2, w1 & ~(D1_OWN | D1_ERR | TMD1_MORE | TMD1_ONE | TMD1_DEF));
		}
		s->txi = (s->txi + cnt) % s->ntx;
		s->csr0 |= C0_TINT;
		sent += cnt;
	}
}
