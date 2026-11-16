/*
  Hatari - daynaport.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Dayna SCSI/Link ethernet adapter on the SCSI bus, as the ZuluSCSI and
  BlueSCSI firmwares present it: a processor-type target with vendor
  "Dayna", product "SCSI/Link", driven by five vendor commands:

    0x09  read MAC address + 3 counters (18 bytes in)
    0x0E  enable (cdb[5] & 0x80) / disable the interface
    0x0A  send one frame (cdb[5] == 0: raw, length in cdb[3..4])
    0x08  receive: 6-byte header [len16][0][0][0][flags] then the frame
          and a 4-byte CRC; an empty queue answers six zero bytes
    0x0D  add a multicast address (6 bytes out, ignored here)

  Frames go to a host TAP interface (Linux only). The interface has to
  exist and be owned by the user running Hatari:

    ip tuntap add dev tap0 mode tap user $USER
    ip addr add 192.168.30.1/24 dev tap0 && ip link set tap0 up

  then `--scsi-net 4=tap0` puts the adapter at SCSI id 4 with a
  00:80:19 (Dayna) MAC.

  By default the adapter behaves like the ZuluSCSI/BlueSCSI emulations:
  every command is accepted at any time. `--scsi-net 4=tap0,rom` instead
  follows the real Dayna ROM (v2.0) as the SCSI/Link implementor's guide
  describes it, so a driver can be tested against the hardware it will
  meet:

    - while disabled, only TEST UNIT READY, REQUEST SENSE, INQUIRY and
      ENABLE are accepted; data commands answer CHECK CONDITION, sense
      key 5 (illegal request)
    - for 500 ms (emulated time) after ENABLE, data commands answer
      CHECK CONDITION (sense key 2 here; the ROM's key for this window
      is not documented), while TEST UNIT READY keeps answering GOOD
    - ENABLE discards the frames queued for reception
    - 0x09 answers 22 bytes (MAC + four counters), cut to the allocation
    - REQUEST SENSE reports the last key, at most nine bytes

  `,wedge=<n>` (with or without `rom`) also reproduces the ROM's
  dropped-packet state: after <n> received frames, every receive answers
  a header with the four bytes after the length all 0xFF and a nonsense
  length, until the driver disables and re-enables the interface.
*/
const char DaynaPort_fileid[] = "Hatari daynaport.c";

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#endif

#include "main.h"
#include "configuration.h"
#include "hdc.h"
#include "log.h"
#include "cycles.h"
#include "clocks_timings.h"
#include "daynaport.h"

#define DP_FRAME_MAX   1518          /* ethernet frame without CRC */
#define DP_HEADER      6

static int tap_fd = -1;
static bool dp_enabled;
static bool dp_rom;                  /* real-ROM command gating */
static int dp_wedge_after;           /* 0 = never wedge */
static int dp_rx_count;              /* frames received since enable */
static bool dp_wedged;
static uint8_t dp_sense;             /* key for the next REQUEST SENSE */
static uint64_t dp_settle_until;     /* CyclesGlobalClockCounter */
static int dp_refused;               /* refusals since the last enable */
static char dp_ifname[IFNAMSIZ];
static const uint8_t dp_mac[6] = { 0x00, 0x80, 0x19, 0x1a, 0x7a, 0x01 };

static const uint8_t dp_inquiry[36] =
{
	0x03, 0x00, 0x01, 0x00, 0x1f, 0x00, 0x00, 0x00,    /* processor device */
	'D','a','y','n','a',' ',' ',' ',
	'S','C','S','I','/','L','i','n','k',' ',' ',' ',' ',' ',' ',' ',
	'2','.','0','f'
};

/* CRC-32 (IEEE 802.3), as the adapter appends to received frames */
static uint32_t dp_crc32(const uint8_t *p, int n)
{
	uint32_t crc = 0xffffffff;
	while (n--)
	{
		crc ^= *p++;
		for (int i = 0; i < 8; i++)
			crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
	}
	return ~crc;
}


/**
 * Open the TAP interface for a SCSI slot configured as a network adapter.
 * Returns true on success; the device is then enabled on the bus.
 */
bool DaynaPort_Init(SCSI_DEV *dev, const char *ifname)
{
#ifdef __linux__
	struct ifreq ifr;
	char name[IFNAMSIZ];
	const char *opt;

	/* "tap0[,rom][,wedge=<n>]" */
	opt = strchr(ifname, ',');
	snprintf(name, sizeof(name), "%.*s",
	         opt ? (int)(opt - ifname) : (int)strlen(ifname), ifname);
	dp_rom = false;
	dp_wedge_after = 0;
	while (opt)
	{
		opt++;
		if (strncmp(opt, "rom", 3) == 0 && (opt[3] == ',' || opt[3] == 0))
			dp_rom = true;
		else if (strncmp(opt, "wedge=", 6) == 0)
			dp_wedge_after = atoi(opt + 6);
		else
			Log_Printf(LOG_WARN, "DaynaPORT: unknown option '%s'\n", opt);
		opt = strchr(opt, ',');
	}
	ifname = name;

	if (tap_fd >= 0)
		close(tap_fd);
	tap_fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
	if (tap_fd < 0)
	{
		Log_Printf(LOG_ERROR, "DaynaPORT: cannot open /dev/net/tun: %s\n", strerror(errno));
		return false;
	}
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(tap_fd, TUNSETIFF, &ifr) < 0)
	{
		Log_Printf(LOG_ERROR, "DaynaPORT: TAP interface '%s': %s "
		           "(create it with: ip tuntap add dev %s mode tap user $USER)\n",
		           ifname, strerror(errno), ifname);
		close(tap_fd);
		tap_fd = -1;
		return false;
	}
	memcpy(dp_ifname, ifr.ifr_name, sizeof(dp_ifname));
	dp_ifname[sizeof(dp_ifname) - 1] = 0;
	memset(dev, 0, sizeof(*dev));
	dev->enabled = true;
	dev->network = true;
	dev->scsi_version = 2;
	dp_enabled = false;
	dp_wedged = false;
	dp_sense = 0;
	Log_Printf(LOG_INFO, "DaynaPORT: SCSI/Link on TAP interface '%s', MAC %02x:%02x:%02x:%02x:%02x:%02x%s%s\n",
	           dp_ifname, dp_mac[0], dp_mac[1], dp_mac[2], dp_mac[3], dp_mac[4], dp_mac[5],
	           dp_rom ? ", ROM behaviour" : "", dp_wedge_after ? ", wedging" : "");
	return true;
#else
	Log_Printf(LOG_ERROR, "DaynaPORT: TAP networking is only available on Linux\n");
	return false;
#endif
}

void DaynaPort_UnInit(void)
{
	if (tap_fd >= 0)
		close(tap_fd);
	tap_fd = -1;
}


/**
 * Is this frame for us? Our MAC, broadcast, or any multicast (the driver's
 * multicast list is not honoured; the IP stack drops what it does not want).
 */
static bool dp_wanted(const uint8_t *frame)
{
	return (frame[0] & 1) || memcmp(frame, dp_mac, 6) == 0;
}


static uint8_t *dp_buf(SCSI_CTRLR *ctr, int size)
{
	ctr->data_len = size;
	ctr->offset = 0;
	if (size > ctr->buffer_size)
	{
		ctr->buffer_size = size;
		ctr->buffer = realloc(ctr->buffer, size);
	}
	return ctr->buffer;
}


/** Answer CHECK CONDITION; the next REQUEST SENSE reports `key`. */
static void dp_check(SCSI_CTRLR *ctr, uint8_t key)
{
	ctr->status = HD_STATUS_ERROR;
	ctr->data_len = 0;
	dp_sense = key;
	dp_refused++;
}

/**
 * ROM mode: may this data command run now? Refuses it (and returns false)
 * while the interface is disabled or still settling after ENABLE.
 */
static bool dp_rom_ready(SCSI_CTRLR *ctr)
{
	if (!dp_rom)
		return true;
	if (!dp_enabled)
	{
		LOG_TRACE(TRACE_SCSI_CMD, "DaynaPORT: 0x%02x refused, interface disabled\n", ctr->command[0]);
		dp_check(ctr, 5);
		return false;
	}
	if (CyclesGlobalClockCounter < dp_settle_until)
	{
		LOG_TRACE(TRACE_SCSI_CMD, "DaynaPORT: 0x%02x refused, settling after enable\n", ctr->command[0]);
		dp_check(ctr, 2);
		return false;
	}
	if (dp_settle_until)
	{
		Log_Printf(LOG_INFO, "DaynaPORT: first data command (0x%02x) accepted after enable; %d refused before it\n",
		           ctr->command[0], dp_refused);
		dp_settle_until = 0;
	}
	return true;
}

/**
 * A command packet for the adapter is complete. IN commands fill the
 * response here; OUT commands only size the buffer, DaynaPort_DataOut()
 * runs when the data has arrived.
 */
void DaynaPort_EmulateCommand(SCSI_CTRLR *ctr)
{
	const uint8_t *cdb = ctr->command;
	int size = (cdb[3] << 8) | cdb[4];
	uint8_t *buf;

	ctr->status = HD_STATUS_OK;
	ctr->data_len = 0;

	switch (cdb[0])
	{
	case 0x00:                           /* TEST UNIT READY */
		break;

	case 0x12:                           /* INQUIRY */
		size = cdb[4];
		buf = dp_buf(ctr, size);
		memset(buf, 0, size);
		memcpy(buf, dp_inquiry, size < (int)sizeof(dp_inquiry) ? size : (int)sizeof(dp_inquiry));
		if ((cdb[1] >> 5) != 0)
			buf[0] = 0x7f;               /* no such LUN */
		break;

	case 0x03:                           /* REQUEST SENSE: the last key */
		size = cdb[4];
		if (dp_rom)                      /* the ROM: 0 means 4, at most 9 */
			size = size == 0 ? 4 : size > 9 ? 9 : size;
		buf = dp_buf(ctr, size);
		memset(buf, 0, size);
		if (size > 0) buf[0] = 0x70;
		if (size > 2) buf[2] = dp_sense;
		if (size > 7 && !dp_rom) buf[7] = 10;
		dp_sense = 0;
		break;

	case 0x09:                           /* MAC address + counters */
		if (!dp_rom_ready(ctr))
			break;
		if (dp_rom)                      /* ROM: four counters, 22 bytes */
		{
			int n = size < 22 ? size : 22;
			buf = dp_buf(ctr, n);
			memset(buf, 0, n);
			memcpy(buf, dp_mac, n < 6 ? n : 6);
			break;
		}
		buf = dp_buf(ctr, 18);
		memset(buf, 0, 18);
		memcpy(buf, dp_mac, 6);
		break;

	case 0x0e:                           /* enable / disable */
		dp_enabled = (cdb[5] & 0x80) != 0;
		LOG_TRACE(TRACE_SCSI_CMD, "DaynaPORT: interface %s\n", dp_enabled ? "enabled" : "disabled");
		if (dp_enabled)
		{
			if (dp_wedged)
				Log_Printf(LOG_INFO, "DaynaPORT: re-enabled, dropped-packet state cleared\n");
			dp_wedged = false;
			dp_rx_count = 0;
		}
		if (dp_enabled && dp_rom)
		{
			uint8_t frame[DP_FRAME_MAX];
			/* the controller resets: queued frames are lost, and data
			 * commands are refused for the next 500 ms */
			while (tap_fd >= 0 && read(tap_fd, frame, sizeof(frame)) > 0)
				;
			dp_settle_until = CyclesGlobalClockCounter + MachineClocks.CPU_Freq_Emul / 2;
			dp_refused = 0;
		}
		break;

	case 0x08:                           /* receive one frame */
	{
		uint8_t frame[DP_FRAME_MAX];
		int n = -1;

		if (!dp_rom_ready(ctr))
			break;
		if (size < DP_HEADER + 64)
		{
			ctr->status = HD_STATUS_ERROR;
			break;
		}
		if (dp_wedged)                   /* dropped-packet state */
		{
			buf = dp_buf(ctr, DP_HEADER);
			buf[0] = 0x5a;
			buf[1] = 0x3c;                   /* nonsense length */
			buf[2] = buf[3] = buf[4] = buf[5] = 0xff;
			LOG_TRACE(TRACE_SCSI_CMD, "DaynaPORT: receive while wedged\n");
			break;
		}
		if (dp_enabled && tap_fd >= 0)
		{
			do {
				n = read(tap_fd, frame, sizeof(frame));
			} while (n > 0 && !dp_wanted(frame));
		}
		if (n > 0 && dp_wedge_after && ++dp_rx_count > dp_wedge_after)
		{
			/* the adapter's buffer overflowed: this frame is lost and
			 * the receiver stays wedged until disable/enable */
			Log_Printf(LOG_INFO, "DaynaPORT: wedged after %d frames (dropped-packet state)\n", dp_wedge_after);
			dp_wedged = true;
			buf = dp_buf(ctr, DP_HEADER);
			buf[0] = 0x5a;
			buf[1] = 0x3c;
			buf[2] = buf[3] = buf[4] = buf[5] = 0xff;
			break;
		}
		if (n <= 0)
		{
			buf = dp_buf(ctr, DP_HEADER);
			memset(buf, 0, DP_HEADER);
			break;
		}
		if (n < 60)                      /* pad runts as the adapter does */
		{
			memset(frame + n, 0, 60 - n);
			n = 60;
		}
		if (DP_HEADER + n + 4 > size)
			n = size - DP_HEADER - 4;
		buf = dp_buf(ctr, DP_HEADER + n + 4);
		buf[0] = (n + 4) >> 8;
		buf[1] = (n + 4) & 0xff;
		buf[2] = buf[3] = buf[4] = 0;
		buf[5] = 0;                      /* no more frames queued */
		memcpy(buf + DP_HEADER, frame, n);
		uint32_t crc = dp_crc32(frame, n);
		buf[DP_HEADER + n + 0] = crc & 0xff;
		buf[DP_HEADER + n + 1] = (crc >> 8) & 0xff;
		buf[DP_HEADER + n + 2] = (crc >> 16) & 0xff;
		buf[DP_HEADER + n + 3] = (crc >> 24) & 0xff;
		LOG_TRACE(TRACE_SCSI_CMD, "DaynaPORT: receive %d bytes\n", n);
		break;
	}

	case 0x0a:                           /* send: data follows */
		if (!dp_rom_ready(ctr))
			break;
		if (size <= 0 || size > DP_FRAME_MAX + 4)
		{
			ctr->status = HD_STATUS_ERROR;
			break;
		}
		dp_buf(ctr, size);
		break;

	case 0x0d:                           /* add multicast address: data follows */
		if (!dp_rom_ready(ctr))
			break;
		dp_buf(ctr, cdb[4] ? cdb[4] : 6);
		break;

	default:
		LOG_TRACE(TRACE_SCSI_CMD, "DaynaPORT: unsupported command 0x%02x\n", cdb[0]);
		ctr->status = HD_STATUS_ERROR;
		break;
	}
}


/**
 * The DATA OUT phase of a send / multicast command has completed.
 */
void DaynaPort_DataOut(SCSI_CTRLR *ctr)
{
	if (ctr->command[0] != 0x0a)
		return;
	if (!dp_enabled || tap_fd < 0)
		return;
	int len = ctr->data_len;
	if (ctr->command[5] != 0 && len >= 4)
	{
		/* framed write: 2-byte length, 2 bytes padding, then the frame */
		int n = (ctr->buffer[0] << 8) | ctr->buffer[1];
		if (n > 0 && n + 4 <= len)
		{
			memmove(ctr->buffer, ctr->buffer + 4, n);
			len = n;
		}
	}
	if (write(tap_fd, ctr->buffer, len) != len)
		Log_Printf(LOG_WARN, "DaynaPORT: send of %d bytes failed: %s\n", len, strerror(errno));
	else
		LOG_TRACE(TRACE_SCSI_CMD, "DaynaPORT: sent %d bytes\n", len);
}
