/*
  Hatari - hostnet.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Ethernet frames between an emulated network adapter and the host, with
  three backends (see hostnet.h for the spec strings):

  - TAP (Linux): a host interface the user creates; full layer 2.
  - SLIRP (libslirp, any OS): user-mode NAT like QEMU's "-netdev user".
    Nothing to set up and no privileges needed: the guest gets DHCP and
    DNS, reaches the host's network through the host's own sockets, and
    host ports can be forwarded into it (hostfwd=). ICMP (ping) to the
    outside may not work, as the host OS may not allow it unprivileged.
  - PCAP (libpcap; Npcap on Windows): the frames go straight onto a real
    host interface, so the guest is a machine on the LAN. Needs capture
    rights (root or CAP_NET_RAW on Linux, access to /dev/bpf* on macOS,
    Npcap installed on Windows). Only frames arriving on the interface are
    seen, so the host itself can't talk to the guest over it (other LAN
    machines can); most Wi-Fi access points drop frames from the guest's
    MAC. On a veth or bridge fed by the host's own stack, turn TX checksum
    offload off (ethtool -K <peer> tx off) or TCP from the host arrives
    with unfinished checksums and the guest drops it.

  The emulated adapters poll for frames (DaynaPORT's receive command), so
  every backend works without threads: SLIRP's sockets and timers are
  serviced whenever the guest asks for a frame or sends one.
*/
const char HostNet_fileid[] = "Hatari hostnet.c";

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "main.h"
#include "log.h"
#include "hostnet.h"


#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#define HAVE_TAP 1
#endif

#ifdef HAVE_SLIRP
#include <slirp/libslirp.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <poll.h>
#include <time.h>
#include <arpa/inet.h>
#endif
#endif

#ifdef HAVE_PCAP
#include <pcap/pcap.h>
#endif

enum { NET_TAP, NET_SLIRP, NET_PCAP };

#define QUEUE_LEN   64               /* frames waiting for the guest */
#define MAX_TIMERS  16
#define MAX_POLLFDS 64

#ifdef HAVE_SLIRP
struct hn_timer {
	SlirpTimerCb cb;
	void *cb_opaque;
	int64_t expire_ms;               /* -1: not armed */
	bool used;
};
#endif

struct hostnet {
	int kind;
	/* TAP */
	int fd;
	/* SLIRP */
#ifdef HAVE_SLIRP
	Slirp *slirp;
	struct hn_timer timers[MAX_TIMERS];
#ifdef _WIN32
	WSAPOLLFD pollfds[MAX_POLLFDS];
#else
	struct pollfd pollfds[MAX_POLLFDS];
#endif
	int npollfds;
#endif
	/* PCAP */
#ifdef HAVE_PCAP
	pcap_t *pcap;
#endif
	/* frames for the guest (SLIRP) */
	uint8_t queue[QUEUE_LEN][HOSTNET_FRAME_MAX];
	int queue_len[QUEUE_LEN];
	int q_head, q_count;
};

static void queue_put(hostnet_t *net, const uint8_t *frame, int len)
{
	int slot;

	if (len <= 0 || len > HOSTNET_FRAME_MAX)
		return;
	if (net->q_count == QUEUE_LEN)
	{
		/* full: the oldest frame goes, as a real adapter's would */
		net->q_head = (net->q_head + 1) % QUEUE_LEN;
		net->q_count--;
	}
	slot = (net->q_head + net->q_count) % QUEUE_LEN;
	memcpy(net->queue[slot], frame, len);
	net->queue_len[slot] = len;
	net->q_count++;
}

static int queue_get(hostnet_t *net, uint8_t *frame, int max)
{
	int len;

	if (net->q_count == 0)
		return 0;
	len = net->queue_len[net->q_head];
	if (len > max)
		len = max;
	memcpy(frame, net->queue[net->q_head], len);
	net->q_head = (net->q_head + 1) % QUEUE_LEN;
	net->q_count--;
	return len;
}


/* ---------------------------------------------------------------- TAP */

static bool tap_open(hostnet_t *net, const char *ifname, char *desc, int desclen)
{
#ifdef HAVE_TAP
	struct ifreq ifr;

	net->fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
	if (net->fd < 0)
	{
		Log_Printf(LOG_ERROR, "Network: cannot open /dev/net/tun: %s\n", strerror(errno));
		return false;
	}
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	snprintf(ifr.ifr_name, IFNAMSIZ, "%.*s", IFNAMSIZ - 1, ifname);
	if (ioctl(net->fd, TUNSETIFF, &ifr) < 0)
	{
		Log_Printf(LOG_ERROR, "Network: TAP interface '%s': %s "
		           "(create it with: ip tuntap add dev %s mode tap user $USER)\n",
		           ifname, strerror(errno), ifname);
		close(net->fd);
		net->fd = -1;
		return false;
	}
	snprintf(desc, desclen, "TAP interface '%s'", ifr.ifr_name);
	return true;
#else
	Log_Printf(LOG_ERROR, "Network: TAP interfaces are only available on "
	           "Linux: use slirp (or pcap:<interface>) instead\n");
	return false;
#endif
}


/* -------------------------------------------------------------- SLIRP */

#ifdef HAVE_SLIRP

/* slirp's clock: monotonic nanoseconds */
static int64_t now_ns(void)
{
#ifdef _WIN32
	static LARGE_INTEGER freq;
	LARGE_INTEGER t;

	if (!freq.QuadPart)
		QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&t);
	return (int64_t)(t.QuadPart / freq.QuadPart * 1000000000LL
	                 + t.QuadPart % freq.QuadPart * 1000000000LL / freq.QuadPart);
#else
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
}

static slirp_ssize_t slirp_cb_send(const void *buf, size_t len, void *opaque)
{
	queue_put(opaque, buf, (int)len);
	return (slirp_ssize_t)len;
}

static void slirp_cb_error(const char *msg, void *opaque)
{
	Log_Printf(LOG_WARN, "Network (slirp): %s\n", msg);
}

static int64_t slirp_cb_clock(void *opaque)
{
	return now_ns();
}

static void *slirp_cb_timer_new(SlirpTimerCb cb, void *cb_opaque, void *opaque)
{
	hostnet_t *net = opaque;
	int i;

	for (i = 0; i < MAX_TIMERS; i++)
		if (!net->timers[i].used)
		{
			net->timers[i].used = true;
			net->timers[i].cb = cb;
			net->timers[i].cb_opaque = cb_opaque;
			net->timers[i].expire_ms = -1;
			return &net->timers[i];
		}
	Log_Printf(LOG_ERROR, "Network (slirp): out of timers\n");
	return NULL;
}

static void slirp_cb_timer_free(void *timer, void *opaque)
{
	if (timer)
		memset(timer, 0, sizeof(struct hn_timer));
}

static void slirp_cb_timer_mod(void *timer, int64_t expire_ms, void *opaque)
{
	if (timer)
		((struct hn_timer *)timer)->expire_ms = expire_ms;
}

static void slirp_cb_register_fd(int fd, void *opaque) { }
static void slirp_cb_unregister_fd(int fd, void *opaque) { }
static void slirp_cb_notify(void *opaque) { }

static int slirp_cb_add_poll(int fd, int events, void *opaque)
{
	hostnet_t *net = opaque;
	short ev = 0;
	int i = net->npollfds;

	if (i == MAX_POLLFDS)
		return -1;
	if (events & SLIRP_POLL_IN)
		ev |= POLLIN;
	if (events & SLIRP_POLL_OUT)
		ev |= POLLOUT;
#ifndef _WIN32
	if (events & SLIRP_POLL_PRI)
		ev |= POLLPRI;
#endif
	net->pollfds[i].fd = fd;
	net->pollfds[i].events = ev;
	net->pollfds[i].revents = 0;
	net->npollfds++;
	return i;
}

static int slirp_cb_get_revents(int idx, void *opaque)
{
	hostnet_t *net = opaque;
	short r;
	int ev = 0;

	if (idx < 0 || idx >= net->npollfds)
		return 0;
	r = net->pollfds[idx].revents;
	if (r & POLLIN)
		ev |= SLIRP_POLL_IN;
	if (r & POLLOUT)
		ev |= SLIRP_POLL_OUT;
#ifndef _WIN32
	if (r & POLLPRI)
		ev |= SLIRP_POLL_PRI;
#endif
	if (r & POLLERR)
		ev |= SLIRP_POLL_ERR;
	if (r & POLLHUP)
		ev |= SLIRP_POLL_HUP;
	return ev;
}

/* service slirp's sockets and timers once, without waiting */
static void slirp_pump(hostnet_t *net)
{
	uint32_t timeout = 0;
	int64_t now_ms = now_ns() / 1000000;
	int i, rc;

	net->npollfds = 0;
	slirp_pollfds_fill(net->slirp, &timeout, slirp_cb_add_poll, net);
#ifdef _WIN32
	rc = net->npollfds ? WSAPoll(net->pollfds, net->npollfds, 0) : 0;
#else
	rc = net->npollfds ? poll(net->pollfds, net->npollfds, 0) : 0;
#endif
	slirp_pollfds_poll(net->slirp, rc < 0, slirp_cb_get_revents, net);
	for (i = 0; i < MAX_TIMERS; i++)
		if (net->timers[i].used && net->timers[i].expire_ms >= 0
		    && net->timers[i].expire_ms <= now_ms)
		{
			net->timers[i].expire_ms = -1;
			net->timers[i].cb(net->timers[i].cb_opaque);
		}
}

static bool parse_addr(const char *s, struct in_addr *a)
{
	char buf[32];
	size_t n = strcspn(s, ",/-:");

	if (n == 0 || n >= sizeof(buf))
		return false;
	memcpy(buf, s, n);
	buf[n] = 0;
	return inet_pton(AF_INET, buf, a) == 1;
}

/* hostfwd=tcp|udp:[hostaddr:]hostport-[guestaddr]:guestport */
static bool slirp_hostfwd(hostnet_t *net, const char *s, struct in_addr guest)
{
	struct in_addr host_addr, guest_addr = guest;
	int is_udp, host_port, guest_port;
	const char *p, *dash;

	if (strncmp(s, "tcp:", 4) == 0)
		is_udp = 0;
	else if (strncmp(s, "udp:", 4) == 0)
		is_udp = 1;
	else
		return false;
	s += 4;
	dash = strchr(s, '-');
	if (!dash)
		return false;
	/* host side: [addr:]port - by default only this machine can connect */
	inet_pton(AF_INET, "127.0.0.1", &host_addr);
	p = memchr(s, ':', dash - s);
	if (p)
	{
		if (p > s && !parse_addr(s, &host_addr))
			return false;
		s = p + 1;
	}
	host_port = atoi(s);
	/* guest side: [addr]:port */
	s = dash + 1;
	p = strchr(s, ':');
	if (!p)
		return false;
	if (p > s && !parse_addr(s, &guest_addr))
		return false;
	guest_port = atoi(p + 1);
	if (host_port <= 0 || host_port > 65535 || guest_port <= 0 || guest_port > 65535)
		return false;
	if (slirp_add_hostfwd(net->slirp, is_udp, host_addr, host_port,
	                      guest_addr, guest_port) < 0)
	{
		Log_Printf(LOG_ERROR, "Network (slirp): can't forward host port %d: %s\n",
		           host_port, strerror(errno));
		return true;             /* understood, just not possible */
	}
	Log_Printf(LOG_INFO, "Network (slirp): host %s port %d -> guest port %d\n",
	           is_udp ? "udp" : "tcp", host_port, guest_port);
	return true;
}

static const SlirpCb slirp_callbacks = {
	.send_packet = slirp_cb_send,
	.guest_error = slirp_cb_error,
	.clock_get_ns = slirp_cb_clock,
	.timer_new = slirp_cb_timer_new,
	.timer_free = slirp_cb_timer_free,
	.timer_mod = slirp_cb_timer_mod,
	.register_poll_fd = slirp_cb_register_fd,
	.unregister_poll_fd = slirp_cb_unregister_fd,
	.notify = slirp_cb_notify,
};

static bool slirp_open(hostnet_t *net, const char *opts,
                       bool (*unknown)(const char *opt), char *desc, int desclen)
{
	SlirpConfig cfg;
	char nets[20] = "10.0.2.0", hosts[16], dnss[16], guests[16];
	struct in_addr guest;
	int prefix = 24;
	const char *o;
	char fwd[8][96];
	int nfwd = 0;

	memset(&cfg, 0, sizeof(cfg));
	cfg.version = 1;
	cfg.in_enabled = true;
	inet_pton(AF_INET, "10.0.2.0", &cfg.vnetwork);
	cfg.vhost.s_addr = 0;
	cfg.vnameserver.s_addr = 0;
	cfg.vdhcp_start.s_addr = 0;
	cfg.vhostname = "hatari";
	cfg.if_mtu = 1500;
	cfg.if_mru = 1500;

	/* options: net= host= dns= guest= hostfwd= (the rest to the caller) */
	for (o = opts; o && *o; o = strchr(o, ',') ? strchr(o, ',') + 1 : NULL)
	{
		if (*o == ',')
			continue;
		if (strncmp(o, "net=", 4) == 0)
		{
			const char *slash = strchr(o + 4, '/');

			if (!parse_addr(o + 4, &cfg.vnetwork))
				goto bad;
			if (slash)
				prefix = atoi(slash + 1);
			if (prefix < 8 || prefix > 30)
				goto bad;
		}
		else if (strncmp(o, "host=", 5) == 0)
		{
			if (!parse_addr(o + 5, &cfg.vhost))
				goto bad;
		}
		else if (strncmp(o, "dns=", 4) == 0)
		{
			if (!parse_addr(o + 4, &cfg.vnameserver))
				goto bad;
		}
		else if (strncmp(o, "guest=", 6) == 0)
		{
			if (!parse_addr(o + 6, &cfg.vdhcp_start))
				goto bad;
		}
		else if (strncmp(o, "hostfwd=", 8) == 0)
		{
			if (nfwd < 8)
			{
				snprintf(fwd[nfwd], sizeof(fwd[0]), "%.*s",
				         (int)strcspn(o + 8, ","), o + 8);
				nfwd++;
			}
		}
		else if (!unknown || !unknown(o))
		{
			Log_Printf(LOG_WARN, "Network (slirp): unknown option '%.*s'\n",
			           (int)strcspn(o, ","), o);
		}
	}

	/* the addresses not given follow QEMU's layout: .2 the host, .3 the
	 * DNS server, .15 the first DHCP address */
	{
		uint32_t base = ntohl(cfg.vnetwork.s_addr), mask = 0xffffffffu << (32 - prefix);

		base &= mask;
		cfg.vnetwork.s_addr = htonl(base);
		cfg.vnetmask.s_addr = htonl(mask);
		if (!cfg.vhost.s_addr)
			cfg.vhost.s_addr = htonl(base | 2);
		if (!cfg.vnameserver.s_addr)
			cfg.vnameserver.s_addr = htonl(base | 3);
		if (!cfg.vdhcp_start.s_addr)
			cfg.vdhcp_start.s_addr = htonl(base | 15);
	}
	guest = cfg.vdhcp_start;

	net->slirp = slirp_new(&cfg, &slirp_callbacks, net);
	if (!net->slirp)
	{
		Log_Printf(LOG_ERROR, "Network (slirp): initialisation failed\n");
		return false;
	}
	{
		int i;

		for (i = 0; i < nfwd; i++)
			if (!slirp_hostfwd(net, fwd[i], guest))
				Log_Printf(LOG_ERROR, "Network (slirp): bad hostfwd '%s' "
				           "(tcp|udp:[hostaddr:]hostport-[guestaddr]:guestport)\n", fwd[i]);
	}
	inet_ntop(AF_INET, &cfg.vnetwork, nets, sizeof(nets));
	inet_ntop(AF_INET, &cfg.vhost, hosts, sizeof(hosts));
	inet_ntop(AF_INET, &cfg.vnameserver, dnss, sizeof(dnss));
	inet_ntop(AF_INET, &guest, guests, sizeof(guests));
	snprintf(desc, desclen, "slirp NAT (network %s/%d, gateway %s, DNS %s, "
	         "guest %s)", nets, prefix, hosts, dnss, guests);
	return true;
bad:
	Log_Printf(LOG_ERROR, "Network (slirp): bad option '%.*s'\n", (int)strcspn(o, ","), o);
	return false;
}

#endif /* HAVE_SLIRP */


/* --------------------------------------------------------------- PCAP */

#ifdef HAVE_PCAP

static void pcap_list(void)
{
	char err[PCAP_ERRBUF_SIZE];
	pcap_if_t *all, *d;

	if (pcap_findalldevs(&all, err) < 0)
	{
		Log_Printf(LOG_ERROR, "Network (pcap): %s\n", err);
		return;
	}
	fprintf(stderr, "Interfaces for pcap:<name> (as pcap can open them):\n");
	for (d = all; d; d = d->next)
		fprintf(stderr, "  %s%s%s%s\n", d->name, d->description ? "  (" : "",
		        d->description ? d->description : "", d->description ? ")" : "");
	if (!all)
		fprintf(stderr, "  none (capture rights missing?)\n");
	pcap_freealldevs(all);
}

static bool pcap_open_if(hostnet_t *net, const char *ifname, char *desc, int desclen)
{
	char err[PCAP_ERRBUF_SIZE];

	if (strcmp(ifname, "list") == 0)
	{
		pcap_list();
		return false;
	}
	net->pcap = pcap_create(ifname, err);
	if (!net->pcap)
	{
		Log_Printf(LOG_ERROR, "Network (pcap): %s\n", err);
		return false;
	}
	pcap_set_snaplen(net->pcap, HOSTNET_FRAME_MAX);
	pcap_set_promisc(net->pcap, 1);      /* frames for the guest's own MAC */
	pcap_set_immediate_mode(net->pcap, 1);
	pcap_set_timeout(net->pcap, 1);
	if (pcap_activate(net->pcap) < 0)
	{
		Log_Printf(LOG_ERROR, "Network (pcap): interface '%s': %s "
		           "(capture rights needed; 'pcap:list' shows the names)\n",
		           ifname, pcap_geterr(net->pcap));
		pcap_close(net->pcap);
		net->pcap = NULL;
		return false;
	}
	if (pcap_datalink(net->pcap) != DLT_EN10MB)
	{
		Log_Printf(LOG_ERROR, "Network (pcap): '%s' is not an ethernet interface\n", ifname);
		pcap_close(net->pcap);
		net->pcap = NULL;
		return false;
	}
	/* our own frames coming back would look like traffic for the guest */
	pcap_setdirection(net->pcap, PCAP_D_IN);
	if (pcap_setnonblock(net->pcap, 1, err) < 0)
		Log_Printf(LOG_WARN, "Network (pcap): %s\n", err);
	snprintf(desc, desclen, "pcap on '%s'", ifname);
	return true;
}

#endif /* HAVE_PCAP */


/* ------------------------------------------------------------ the API */

hostnet_t *HostNet_Open(const char *spec, const uint8_t mac[6],
                        bool (*unknown)(const char *opt), char *desc, int desclen)
{
	hostnet_t *net = calloc(1, sizeof(*net));
	char name[64];
	const char *opts = strchr(spec, ',');
	int n = opts ? (int)(opts - spec) : (int)strlen(spec);
	bool ok = false;

	if (!net)
		return NULL;
	net->fd = -1;
	snprintf(name, sizeof(name), "%.*s", n, spec);
	if (opts)
		opts++;

	if (strcmp(name, "slirp") == 0)
	{
		net->kind = NET_SLIRP;
#ifdef HAVE_SLIRP
		ok = slirp_open(net, opts, unknown, desc, desclen);
		opts = NULL;             /* slirp_open saw the options */
#else
		Log_Printf(LOG_ERROR, "Network: this Hatari was built without libslirp\n");
#endif
	}
	else if (strncmp(name, "pcap:", 5) == 0)
	{
		net->kind = NET_PCAP;
#ifdef HAVE_PCAP
		ok = pcap_open_if(net, name + 5, desc, desclen);
#else
		Log_Printf(LOG_ERROR, "Network: this Hatari was built without libpcap\n");
#endif
	}
	else
	{
		net->kind = NET_TAP;
		ok = tap_open(net, strncmp(name, "tap:", 4) == 0 ? name + 4 : name,
		              desc, desclen);
	}
	/* options for the caller (the adapter's own) */
	while (ok && opts && *opts)
	{
		if (!unknown || !unknown(opts))
			Log_Printf(LOG_WARN, "Network: unknown option '%.*s'\n",
			           (int)strcspn(opts, ","), opts);
		opts = strchr(opts, ',');
		if (opts)
			opts++;
	}
	if (!ok)
	{
		free(net);
		return NULL;
	}
	(void)mac;
	return net;
}

int HostNet_Recv(hostnet_t *net, uint8_t *frame, int max)
{
	switch (net->kind)
	{
	case NET_TAP:
	{
		int n = net->fd >= 0 ? (int)read(net->fd, frame, max) : -1;

		return n > 0 ? n : 0;
	}
#ifdef HAVE_SLIRP
	case NET_SLIRP:
		if (net->q_count == 0)
			slirp_pump(net);
		return queue_get(net, frame, max);
#endif
#ifdef HAVE_PCAP
	case NET_PCAP:
	{
		struct pcap_pkthdr *hdr;
		const u_char *data;
		int n;

		if (pcap_next_ex(net->pcap, &hdr, &data) != 1)
			return 0;
		n = (int)hdr->caplen < max ? (int)hdr->caplen : max;
		memcpy(frame, data, n);
		return n;
	}
#endif
	}
	return 0;
}

bool HostNet_Send(hostnet_t *net, const uint8_t *frame, int len)
{
	switch (net->kind)
	{
	case NET_TAP:
		return net->fd >= 0 && write(net->fd, frame, len) == len;
#ifdef HAVE_SLIRP
	case NET_SLIRP:
		slirp_input(net->slirp, frame, len);
		slirp_pump(net);
		return true;
#endif
#ifdef HAVE_PCAP
	case NET_PCAP:
		return pcap_sendpacket(net->pcap, frame, len) == 0;
#endif
	}
	return false;
}

void HostNet_Flush(hostnet_t *net)
{
	uint8_t frame[HOSTNET_FRAME_MAX];

	net->q_head = net->q_count = 0;
	while (HostNet_Recv(net, frame, sizeof(frame)) > 0)
		;
}

void HostNet_Close(hostnet_t *net)
{
	if (!net)
		return;
#ifdef HAVE_TAP
	if (net->fd >= 0)
		close(net->fd);
#endif
#ifdef HAVE_SLIRP
	if (net->slirp)
		slirp_cleanup(net->slirp);
#endif
#ifdef HAVE_PCAP
	if (net->pcap)
		pcap_close(net->pcap);
#endif
	free(net);
}
