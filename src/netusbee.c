/*
  Hatari - netusbee.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  The EtherNEC and the NetUSBee on the cartridge port: the card's wiring to
  the NE2000 chip (ne2000.c), and its network through hostnet.c (libslirp).
  See netusbee.h.

  The cartridge port has no write line and no interrupt: the drivers poll
  the chip, and a register write is a read whose address holds the data
  (A1-A8). The chip's data lines are on D8-D15, so only the even byte of a
  read gives its value. A word read is one bus cycle: the chip sees it once.
*/
const char NetUSBee_fileid[] = "Hatari netusbee.c";

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "configuration.h"
#include "log.h"
#include "memorySnapShot.h"
#include "hostnet.h"
#include "ne2000.h"
#include "netusbee.h"

#define NE_READ		0xfa0000	/* /ROM4: register reads */
#define NE_WRITE	0xfb0000	/* /ROM3: register writes, data in A1-A8 */
#define NE_SPAN		(32 * 512)	/* 32 registers, one each 512 bytes */

/* The card's address: 02 (locally administered) 4E 53 54 00 01 */
static const uint8_t card_mac[6] = { 0x02, 0x4e, 0x53, 0x54, 0x00, 0x01 };

static int cart = CNF_NET_CART_NONE;	/* the card on the port now */
static ne2000_t ne;
static hostnet_t *net;
static char net_spec[1024];		/* what net was opened with, "" for none */
static char net_desc[128];
static uint8_t rx_frame[HOSTNET_FRAME_MAX + 4];
static int rx_len;			/* a frame waiting for room in the ring */

bool NetUSBee_Configured(void)
{
	return ConfigureParams.Net.nCartDevice == CNF_NET_CART_ETHERNEC ||
	       ConfigureParams.Net.nCartDevice == CNF_NET_CART_NETUSBEE;
}

bool NetUSBee_OnCartPort(void)
{
	return cart != CNF_NET_CART_NONE;
}

/*
 * The backend spec for hostnet.c from the configuration: "" without the NAT.
 * --nat-forwards is "tcp|udp:bind-ip:host-port:guest-port,..."; each becomes
 * a hostfwd= option. Returns false (and an empty spec) when it is bad.
 */
static bool make_spec(char *spec, size_t len)
{
	const CNF_NET *n = &ConfigureParams.Net;
	const char *p = n->szNatForwards;

	spec[0] = 0;
	if (!n->bNat)
		return true;
	snprintf(spec, len, "slirp%s", n->bNatLoopback ? "" : ",loopback=off");
	while (*p)
	{
		char item[96], proto[4], bind[64];
		unsigned int hport, gport;
		size_t l = strcspn(p, ", ");
		int used = 0;

		if (l == 0)
		{
			p++;
			continue;
		}
		if (l >= sizeof(item))
			goto bad;
		memcpy(item, p, l);
		item[l] = 0;
		p += l;
		if (sscanf(item, "%3[a-z]:%63[^:]:%u:%u%n", proto, bind, &hport, &gport, &used) != 4
		    || item[used] || (strcmp(proto, "tcp") && strcmp(proto, "udp"))
		    || !hport || hport > 65535 || !gport || gport > 65535)
			goto bad;
		l = strlen(spec);
		snprintf(spec + l, len - l, ",hostfwd=%s:%s:%u-:%u", proto, bind, hport, gport);
	}
	return true;
bad:
	Log_AlertDlg(LOG_ERROR, "Network card: bad --nat-forwards '%s', expected "
		     "tcp|udp:bind-ip:host-port:guest-port,...", n->szNatForwards);
	spec[0] = 0;
	return false;
}

static void close_net(void)
{
	if (net)
		HostNet_Close(net);
	net = NULL;
	net_spec[0] = 0;
	net_desc[0] = 0;
	rx_len = 0;
}

static void apply_net(void)
{
	char spec[sizeof(net_spec)];

	if (!make_spec(spec, sizeof(spec)))
		spec[0] = 0;
	if (net && strcmp(spec, net_spec) == 0)
		return;
	close_net();
	if (!spec[0])
		return;
	net = HostNet_Open(spec, card_mac, NULL, net_desc, sizeof(net_desc));
	if (!net)
	{
		Log_AlertDlg(LOG_ERROR, "Network card: the NAT does not start (%s): "
			     "the card is not connected.", spec);
		return;
	}
	strcpy(net_spec, spec);
	Log_Printf(LOG_INFO, "Network card: %s\n", net_desc);
}

void NetUSBee_Apply(void)
{
	int want = NetUSBee_Configured() ? ConfigureParams.Net.nCartDevice : CNF_NET_CART_NONE;

	if (want == CNF_NET_CART_NONE)
	{
		close_net();
		cart = CNF_NET_CART_NONE;
		return;
	}
	if (cart == CNF_NET_CART_NONE)
		NE2000_PowerOn(&ne, card_mac);	/* the card is plugged in */
	cart = want;
	apply_net();
}

void NetUSBee_UnInit(void)
{
	close_net();
	cart = CNF_NET_CART_NONE;
}

void NetUSBee_Reset(bool bCold)
{
	NetUSBee_Apply();
	if (bCold && cart != CNF_NET_CART_NONE)
	{
		NE2000_PowerOn(&ne, card_mac);
		rx_len = 0;
	}
}

/* Frames from the network into the receive ring, while it has room. */
static void pump(void)
{
	int n;

	if (!net)
		return;
	for (n = 0; n < 64; n++)
	{
		if (!rx_len)
		{
			rx_len = HostNet_Recv(net, rx_frame, sizeof(rx_frame));
			if (rx_len <= 0)
			{
				rx_len = 0;
				return;
			}
		}
		if (!NE2000_Receive(&ne, rx_frame, rx_len))
			return;		/* the ring is full: keep the frame */
		rx_len = 0;
	}
}

static void transmit(void)
{
	int len;
	const uint8_t *frame = NE2000_TxFrame(&ne, &len);

	if (net && len >= 14)
	{
		HostNet_Send(net, frame, len);
		pump();		/* answers the NAT has at once (ARP, DNS cache) */
	}
}

uint8_t NetUSBee_ReadByte(uint32_t addr, bool first)
{
	if (cart == CNF_NET_CART_NONE)
		return 0xff;
	if (addr >= NE_READ && addr < NE_READ + NE_SPAN)
	{
		uint8_t v;
		if (!first)
			return 0xff;	/* the same bus cycle as the even byte */
		v = NE2000_Read(&ne, (addr - NE_READ) >> 9);
		return (addr & 1) ? 0xff : v;
	}
	if (addr >= NE_WRITE && addr < NE_WRITE + NE_SPAN)
	{
		if (first)
		{
			uint32_t off = addr - NE_WRITE;
			if (NE2000_Write(&ne, off >> 9, (off >> 1) & 0xff))
				transmit();
		}
		return 0xff;
	}
	return 0xff;		/* the empty rest of the cartridge port */
}

void NetUSBee_Vbl(void)
{
	if (cart != CNF_NET_CART_NONE)
		pump();
}

void NetUSBee_MemorySnapShot_Capture(bool bSave)
{
	/* the chip's registers and memory; not the network connections */
	int32_t kind = cart;

	MemorySnapShot_Store(&kind, sizeof(kind));
	if (!bSave)
		NetUSBee_Apply();	/* the card of the restored configuration */
	if (kind == CNF_NET_CART_NONE)
		return;
	MemorySnapShot_Store(&ne, sizeof(ne));
	if (!bSave)
	{
		rx_len = 0;
		if (kind != cart)
			Log_Printf(LOG_WARN, "The snapshot has another network card than the configuration.\n");
	}
}
