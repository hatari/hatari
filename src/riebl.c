/*
  Hatari - riebl.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  The Riebl Ethernet card for the VME bus of the TT and the Mega STE: an
  Am7990 LANCE (am7990.c) with 64 KB of memory that the CPU and the chip
  share, and its network through hostnet.c (libslirp), as netusbee.c.

  In the VME A24 space (the TT sees it at 0xFE000000 + A24):
    base - 16  word  the chip's data port (RDP)
    base - 14  word  the chip's address port (RAP)
    base       64 KB the memory. The chip sees it at its own address 0.
  base is 0x010000 for NetBSD (0xFE010000 on a TT); the stock Atari
  System V driver wants 0xC10000 (--riebl-addr).

  The memory has a battery. In it the card keeps its setup:
    0xEE8A  long  0x09051990, the battery is good
    0xEE8E  6     the Ethernet address
  Drivers keep away from 0xEE70-0xEEC0. The card interrupts on VME IRQ 5;
  its interrupt vector is the byte at 0xFFFF, which the driver writes.

  References :
    - NetBSD sys/arch/atari/vme/if_le_vme.c and if_levar.h (no code taken)
*/
const char Riebl_fileid[] = "Hatari riebl.c";

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "configuration.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "hostnet.h"
#include "netusbee.h"
#include "am7990.h"
#include "riebl.h"

#define MEM_SIZE	0x10000
#define REG_SIZE	16
#define REG_OFFSET	MEM_SIZE	/* Riebl_Decode's offsets of the registers */
#define LER_RDP		0
#define LER_RAP		2

#define RIEBL_MAGIC_ADDR	0xee8a
#define RIEBL_MAC_ADDR		0xee8e
#define RIEBL_IVEC_ADDR		0xffff

/* The card's address: 00:00:36 is Atari's, 04 the Riebl cards' */
static const uint8_t card_mac[6] = { 0x00, 0x00, 0x36, 0x04, 0x00, 0x01 };

static struct
{
	am7990_t lance;
	uint8_t mem[MEM_SIZE];
	bool irq;
} card;

static bool powered;
static uint32_t base;			/* A24 address of the memory */
static hostnet_t *net;
static char net_spec[1024];
static char net_desc[128];
static uint8_t rx_frame[HOSTNET_FRAME_MAX + 4];
static int rx_len;

static void send_frame(const uint8_t *frame, int len);

static const am7990_bus_t bus = { card.mem, MEM_SIZE - 1, send_frame };


bool Riebl_IsEnabled(void)
{
	return (Config_IsMachineTT() || Config_IsMachineMegaSTE())
	       && ConfigureParams.Net.bRiebl;
}

static void power_on(void)
{
	memset(card.mem, 0, sizeof(card.mem));
	card.mem[RIEBL_MAGIC_ADDR] = 0x09;
	card.mem[RIEBL_MAGIC_ADDR + 1] = 0x05;
	card.mem[RIEBL_MAGIC_ADDR + 2] = 0x19;
	card.mem[RIEBL_MAGIC_ADDR + 3] = 0x90;
	memcpy(&card.mem[RIEBL_MAC_ADDR], card_mac, 6);
	Am7990_Reset(&card.lance);
	card.irq = false;
	powered = true;
}

static void update_irq(void)
{
	bool on = Am7990_IRQ(&card.lance);

	if (on != card.irq)
	{
		card.irq = on;
		M68000_Update_intlev();
	}
}

bool Riebl_GetIRQ(void)
{
	return powered && card.irq;
}

int Riebl_Process_IACK(void)
{
	return card.mem[RIEBL_IVEC_ADDR];
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

void Riebl_Apply(void)
{
	char spec[sizeof(net_spec)];

	if (!Riebl_IsEnabled())
	{
		close_net();
		powered = false;
		return;
	}
	base = ConfigureParams.Net.nRieblAddr & 0xff0000;
	if (base < REG_SIZE)
		base = RIEBL_ADDR_DEFAULT;
	if (!powered)
		power_on();

	/* the cartridge port card has the host ports, when it is there too */
	if (!NetUSBee_NatSpec(spec, sizeof(spec), !NetUSBee_Configured()))
		spec[0] = 0;
	if (net && strcmp(spec, net_spec) == 0)
		return;
	close_net();
	if (!spec[0])
		return;
	net = HostNet_Open(spec, card_mac, NULL, net_desc, sizeof(net_desc));
	if (!net)
	{
		Log_AlertDlg(LOG_ERROR, "Riebl card: the NAT does not start (%s): "
			     "the card is not connected.", spec);
		return;
	}
	strcpy(net_spec, spec);
	Log_Printf(LOG_INFO, "Riebl card: %s\n", net_desc);
}

void Riebl_UnInit(void)
{
	close_net();
	powered = false;
}

/* The VME reset stops the chip; the memory keeps its content (battery). */
void Riebl_Reset(bool bCold)
{
	Riebl_Apply();
	if (!powered)
		return;
	if (bCold)
		power_on();
	Am7990_Reset(&card.lance);
	card.irq = false;
	rx_len = 0;
	M68000_Update_intlev();
}

bool Riebl_Decode(uaecptr addr, uint32_t *pOffset)
{
	uint32_t a24 = addr & 0x00ffffff;

	if (!powered)
		return false;
	if (Config_IsMachineTT())
	{
		if (addr < 0xFE000000 || addr >= 0xFEFF0000)
			return false;	/* not the A24 window */
	}
	else if (a24 < 0xA00000 || a24 >= 0xDF0000)
		return false;
	if (a24 >= base && a24 < base + MEM_SIZE)
	{
		*pOffset = a24 - base;
		return true;
	}
	if (a24 >= base - REG_SIZE && a24 < base)
	{
		*pOffset = REG_OFFSET + a24 - (base - REG_SIZE);
		return true;
	}
	return false;
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
				break;
			}
		}
		if (!Am7990_Receive(&card.lance, &bus, rx_frame, rx_len))
			break;		/* no free buffer: keep the frame */
		rx_len = 0;
	}
	update_irq();
}

static void send_frame(const uint8_t *frame, int len)
{
	if (net)
		HostNet_Send(net, frame, len);
}

uint16_t Riebl_ReadWord(uint32_t offset)
{
	if (offset < MEM_SIZE)
		return (card.mem[offset] << 8) | card.mem[(offset + 1) & (MEM_SIZE - 1)];
	switch (offset - REG_OFFSET)
	{
	 case LER_RDP:
		return Am7990_ReadRDP(&card.lance);
	 case LER_RAP:
		return Am7990_ReadRAP(&card.lance);
	 default:
		return 0xffff;
	}
}

void Riebl_WriteWord(uint32_t offset, uint16_t val)
{
	if (offset < MEM_SIZE)
	{
		card.mem[offset] = val >> 8;
		card.mem[(offset + 1) & (MEM_SIZE - 1)] = val;
		return;
	}
	switch (offset - REG_OFFSET)
	{
	 case LER_RDP:
		Am7990_WriteRDP(&card.lance, &bus, val);
		if (Am7990_ReadRAP(&card.lance) == 0)
			pump();		/* a buffer may be free now; this updates the IRQ */
		break;
	 case LER_RAP:
		Am7990_WriteRAP(&card.lance, val);
		break;
	 default:
		break;
	}
}

uint8_t Riebl_ReadByte(uint32_t offset)
{
	uint16_t w;

	if (offset < MEM_SIZE)
		return card.mem[offset];
	w = Riebl_ReadWord(offset & ~1u);
	return (offset & 1) ? w : w >> 8;
}

/* A byte to a register reaches the chip as a word, the other byte 0. */
void Riebl_WriteByte(uint32_t offset, uint8_t val)
{
	if (offset < MEM_SIZE)
	{
		card.mem[offset] = val;
		return;
	}
	Riebl_WriteWord(offset & ~1u, (offset & 1) ? val : val << 8);
}

void Riebl_Vbl(void)
{
	if (!powered)
		return;
	Am7990_Poll(&card.lance, &bus);
	pump();
}

void Riebl_MemorySnapShot_Capture(bool bSave)
{
	/* the chip's registers and the memory; not the network connections */
	bool on = powered;

	MemorySnapShot_Store(&on, sizeof(on));
	if (!bSave)
		Riebl_Apply();
	if (!on)
		return;
	MemorySnapShot_Store(&card, sizeof(card));
	if (!bSave)
	{
		rx_len = 0;
		if (!powered)
			Log_Printf(LOG_WARN, "The snapshot has a Riebl card, the configuration not.\n");
	}
}
