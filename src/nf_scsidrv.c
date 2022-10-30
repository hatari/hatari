/*
 * Hatari - nf_scsidrv.c
 *
 * Copyright (C) 2015-2016, 2018 by Uwe Seimet
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 *
 * nf_scsidrv.c - Implementation of the host system part of a SCSI Driver
 * (Linux only), based on the Linux SG driver version 3. The corresponding
 * TOS binary and its source code can be downloaded from
 * http://hddriver.seimet.de/en/downloads.html, where you can also find
 * information on the open SCSI Driver standard.
 */
const char NfScsiDrv_fileid[] = "Hatari nf_scsidrv.c";

#if defined(__linux__)

#include "config.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#if HAVE_UDEV
#include <libudev.h>
#endif
#include <sys/ioctl.h>
#include <scsi/sg.h>
#include "stMemory.h"
#include "log.h"
#include "gemdos_defines.h"
#include "m68000.h"
#include "nf_scsidrv.h"

// The driver interface version, 1.02
#define INTERFACE_VERSION 0x0102
// Maximum is 20 characters
// (newer TOS side SCSI driver versions will ignore the name)
#define BUS_NAME "Linux Generic SCSI"
// The SG driver supports cAllCmds
#define BUS_FEATURES 0x02
// The transfer length may depend on the device, 65536 should always be safe
#define BUS_TRANSFER_LEN 65536
// The maximum number of SCSI Driver handles, must be the same as in the stub
#define SCSI_MAX_HANDLES 32


typedef struct
{
	int fd;
	int id_lo;
	int error;
} HANDLE_META_DATA;

static HANDLE_META_DATA handle_meta_data[SCSI_MAX_HANDLES];

#if HAVE_UDEV
static struct udev *udev;
static struct udev_monitor *mon;
static int udev_mon_fd;
static struct timeval tv;
#endif

static uint32_t read_stack_long(uint32_t *stack)
{
	uint32_t value = STMemory_ReadLong(*stack);

	*stack += SIZE_LONG;

	return value;
}

static void *read_stack_pointer(uint32_t *stack)
{
	uint32_t ptr = read_stack_long(stack);
	return ptr ? STMemory_STAddrToPointer(ptr) : 0;
}

static void write_long(uint32_t addr, uint32_t value)
{
	STMemory_WriteLong(addr, value);
}

static void write_word(uint32_t addr, uint16_t value)
{
	STMemory_WriteWord(addr, value);
}

// Sets the error status
static void set_error(uint32_t handle, int errbit)
{
	uint32_t i;
	for (i = 0; i < SCSI_MAX_HANDLES; i++)
	{
		if (handle != i && handle_meta_data[i].fd &&
		    handle_meta_data[i].id_lo == handle_meta_data[handle].id_lo)
		{
			handle_meta_data[i].error |= errbit;
		}
	}
}

// udev-based check for media change. When udev is active media change messages
// are handled globally by udev, i.e. that media changes cannot directly be
// detected by the SCSI Driver. The SCSI Driver has to query udev instead.
static bool check_mchg_udev(void)
{
	bool changed = false;

#if HAVE_UDEV
	fd_set udevFds;
	int ret;

	FD_ZERO(&udevFds);
	FD_SET(udev_mon_fd, &udevFds);

	ret = select(udev_mon_fd + 1, &udevFds, 0, 0, &tv);
	if (ret > 0 && FD_ISSET(udev_mon_fd, &udevFds))
	{
		struct udev_device *dev = udev_monitor_receive_device(mon);
		while (dev)
		{
			if (!changed)
			{
				const char *dev_type = udev_device_get_devtype(dev);
				const char *action = udev_device_get_action(dev);
				if (!strcmp("disk", dev_type) && !strcmp("change", action))
				{
					LOG_TRACE(TRACE_SCSIDRV, ": %s has been changed",
					          udev_device_get_devnode(dev));

					// TODO Determine sg device name from block device name and
					// only report media change for the actually affected device

					changed = true;
				}
			}

			// Process all pending events
			dev = udev_monitor_receive_device(mon);
		}
	}
#endif

	return changed;
}

// Checks whether a device exists by checking for the device file name
static int check_device_file(uint32_t id)
{
	char device_file[16];
	sprintf(device_file, "/dev/sg%d", id);

	if (!access(device_file, R_OK | W_OK))
	{
		LOG_TRACE(TRACE_SCSIDRV, ", device file %s is accessible",
		          device_file);

		return 0;
	}
	else
	{
		LOG_TRACE(TRACE_SCSIDRV, ", device file %s is inaccessible",
		          device_file);

		return -1;
	}
}

static int scsidrv_interface_version(uint32_t stack)
{
	LOG_TRACE(TRACE_SCSIDRV, "scsidrv_interface_version: version=$%04x", INTERFACE_VERSION);

	return INTERFACE_VERSION;
}

static int scsidrv_interface_features(uint32_t stack)
{
	uint32_t st_bus_name = STMemory_ReadLong(stack);
	char *busName = read_stack_pointer(&stack);
	uint32_t features = read_stack_long(&stack);
	uint32_t transferLen = read_stack_long(&stack);

	LOG_TRACE(TRACE_SCSIDRV, "scsidrv_interface_features: busName=%s, features=$%04x, transferLen=%d", BUS_NAME, BUS_FEATURES, BUS_TRANSFER_LEN);

	if ( !STMemory_CheckAreaType ( st_bus_name, 20, ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "scsidrv_interface_features: Invalid RAM range 0x%x+%i\n", st_bus_name, 20);
		return -1;
	}

	strncpy(busName, BUS_NAME, 20);
	M68000_Flush_Data_Cache(st_bus_name, 20);
	write_word(features, BUS_FEATURES);
	write_long(transferLen, BUS_TRANSFER_LEN);

	return 0;
}

// SCSI Driver: InquireBus()
static int scsidrv_inquire_bus(uint32_t stack)
{
	uint32_t id = read_stack_long(&stack);
	char device_file[16];

	LOG_TRACE(TRACE_SCSIDRV, "scsidrv_inquire_bus: id=%d", id);

	sprintf(device_file, "/dev/sg%d", id);

	while (!access(device_file, F_OK))
	{
		if (!check_device_file(id))
		{
			return id;
		}

		sprintf(device_file, "/dev/sg%d", ++id);
	}

	return -1;
}

// SCSI Driver: Open()
static int scsidrv_open(uint32_t stack)
{
	char device_file[16];
	uint32_t handle;
	uint32_t id;
	int fd;

#if HAVE_UDEV
	if (!udev)
	{
		udev = udev_new();
		if (!udev)
		{
			return -1;
		}

		mon = udev_monitor_new_from_netlink(udev, "udev");
		udev_monitor_filter_add_match_subsystem_devtype(mon, "block", NULL);
		udev_monitor_enable_receiving(mon);
		udev_mon_fd = udev_monitor_get_fd(mon);

		tv.tv_sec = 0;
		tv.tv_usec = 0;
	}
#endif

	handle = read_stack_long(&stack);
	id = read_stack_long(&stack);

	LOG_TRACE(TRACE_SCSIDRV, "scsidrv_open: handle=%d, id=%d", handle, id);

	if (handle >= SCSI_MAX_HANDLES || handle_meta_data[handle].fd ||
	    check_device_file(id))
	{
		return GEMDOS_ENHNDL;
	}

	sprintf(device_file, "/dev/sg%d", id);

	fd = open(device_file, O_RDWR | O_NONBLOCK);
	if (fd < 0)
	{
		return fd;
	}

	handle_meta_data[handle].fd = fd;
	handle_meta_data[handle].id_lo = id;
	handle_meta_data[handle].error = 0;

	return 0;
}

// SCSI Driver: Close()
static int scsidrv_close(uint32_t stack)
{
	uint32_t handle = read_stack_long(&stack);

	LOG_TRACE(TRACE_SCSIDRV, "scsidrv_close: handle=%d", handle);

	if (handle >= SCSI_MAX_HANDLES || !handle_meta_data[handle].fd)
	{
		return GEMDOS_ENHNDL;
	}

	close(handle_meta_data[handle].fd);

	handle_meta_data[handle].fd = 0;

	return 0;
}

// SCSI Driver: In() and Out()
static int scsidrv_inout(uint32_t stack)
{
	uint32_t handle = read_stack_long(&stack);
	uint32_t dir = read_stack_long(&stack);
	unsigned char *cmd = read_stack_pointer(&stack);
	uint32_t cmd_len = read_stack_long(&stack);
	uint32_t st_buffer = STMemory_ReadLong(stack);
	unsigned char *buffer = read_stack_pointer(&stack);
	uint32_t transfer_len = read_stack_long(&stack);
	uint32_t st_sense_buffer = STMemory_ReadLong(stack);
	unsigned char *sense_buffer = read_stack_pointer(&stack);
	uint32_t timeout = read_stack_long(&stack);
	int status;

	if (LOG_TRACE_LEVEL(TRACE_SCSIDRV))
	{
		LOG_TRACE_DIRECT_INIT();
		LOG_TRACE_DIRECT(
		    "scsidrv_inout: handle=%d, dir=%d, cmd_len=%d, buffer=%p,\n"
		    "               transfer_len=%d, sense_buffer=%p, timeout=%d,\n"
		    "               cmd=",
		    handle, dir, cmd_len, buffer, transfer_len, sense_buffer,
		    timeout);

		uint32_t i;
		for (i = 0; i < cmd_len; i++)
		{
			LOG_TRACE_DIRECT("%s$%02X", i?":":"", cmd[i]);
		}
		LOG_TRACE_DIRECT_FLUSH();
	}

	// Writing is allowed with a RAM or ROM address,
	// reading requires a RAM address
	if ( !STMemory_CheckAreaType ( st_buffer, transfer_len, dir ? ABFLAG_RAM | ABFLAG_ROM : ABFLAG_RAM ) )
	{
		Log_Printf(LOG_WARN, "scsidrv_inout: Invalid RAM range 0x%x+%i\n", st_buffer, transfer_len);
		return -1;
	}

	if (handle >= SCSI_MAX_HANDLES || !handle_meta_data[handle].fd)
	{
		return GEMDOS_ENHNDL;
	}

	if (sense_buffer)
	{
		memset(sense_buffer, 0, 18);
	}

	// No explicit LUN support, the SG driver maps LUNs to device files
	if (cmd[1] & 0xe0)
	{
		if (sense_buffer)
		{
			// Sense Key and ASC
			sense_buffer[2] = 0x05;
			sense_buffer[12] = 0x25;
			M68000_Flush_Data_Cache(st_sense_buffer, 18);

			LOG_TRACE(TRACE_SCSIDRV,
			          "\n               Sense Key=$%02X, ASC=$%02X, ASCQ=$00",
			          sense_buffer[2], sense_buffer[12]);
		}

		return 2;
	}

	if (check_mchg_udev())
	{
		// cErrMediach for all open handles
		uint32_t i;
		for (i = 0; i < SCSI_MAX_HANDLES; i++)
		{
			if (handle_meta_data[i].fd)
			{
				handle_meta_data[i].error |= 1;
			}
		}

		if (sense_buffer)
		{
			// Sense Key and ASC
			sense_buffer[2] = 0x06;
			sense_buffer[12] = 0x28;
		}

		status = 2;
	}
	else
	{
		struct sg_io_hdr io_hdr;
		memset(&io_hdr, 0, sizeof(struct sg_io_hdr));

		io_hdr.interface_id = 'S';

		io_hdr.dxfer_direction = dir ? SG_DXFER_TO_DEV : SG_DXFER_FROM_DEV;
		if (!transfer_len)
		{
			io_hdr.dxfer_direction = SG_DXFER_NONE;
		}

		io_hdr.dxferp = buffer;
		io_hdr.dxfer_len = transfer_len;

		io_hdr.sbp = sense_buffer;
		io_hdr.mx_sb_len = 18;

		io_hdr.cmdp = cmd;
		io_hdr.cmd_len = cmd_len;

		io_hdr.timeout = timeout;

		status = ioctl(handle_meta_data[handle].fd,
		               SG_IO, &io_hdr) < 0 ? -1 : io_hdr.status;
	}

	if (status > 0 && sense_buffer)
	{
		LOG_TRACE(TRACE_SCSIDRV,
		          "\n               Sense Key=$%02X, ASC=$%02X, ASCQ=$%02X",
		          sense_buffer[2], sense_buffer[12], sense_buffer[13]);

		if (status == 2)
		{
			// Automatic media change and reset handling for
			// SCSI Driver version 1.0.1
			if ((sense_buffer[2] & 0x0f) && !sense_buffer[13])
			{
				if (sense_buffer[12] == 0x28)
				{
					// cErrMediach
					set_error(handle, 1);
				}
				else if (sense_buffer[12] == 0x29)
				{
					// cErrReset
					set_error(handle, 2);
				}
			}
		}
	}

	M68000_Flush_Data_Cache(st_sense_buffer, 18);
	if (!dir)
	{
		M68000_Flush_All_Caches(st_buffer, transfer_len);
	}

	return status;
}

// SCSI Driver: Error()
static int scsidrv_error(uint32_t stack)
{
	uint32_t handle = read_stack_long(&stack);
	uint32_t rwflag = read_stack_long(&stack);
	uint32_t errnum = read_stack_long(&stack);
	int errbit;

	LOG_TRACE(TRACE_SCSIDRV, "scsidrv_error: handle=%d, rwflag=%d, errno=%d",
	          handle, rwflag, errnum);

	if (handle >= SCSI_MAX_HANDLES || !handle_meta_data[handle].fd)
	{
		return GEMDOS_ENHNDL;
	}

	errbit = 1 << errnum;

	if (rwflag)
	{
		set_error(handle, errbit);

		return 0;
	}
	else
	{
		int status = handle_meta_data[handle].error & errbit;
		handle_meta_data[handle].error &= ~errbit;

		return status;
	}
}

// SCSI Driver: CheckDev()
static int scsidrv_check_dev(uint32_t stack)
{
	uint32_t id = read_stack_long(&stack);

	LOG_TRACE(TRACE_SCSIDRV, "scsidrv_check_dev: id=%d", id);

	return check_device_file(id);
}

static const struct
{
	int (*cb)(uint32_t stack);
} operations[] =
{
	{ scsidrv_interface_version },
	{ scsidrv_interface_features },
	{ scsidrv_inquire_bus },
	{ scsidrv_open },
	{ scsidrv_close },
	{ scsidrv_inout },
	{ scsidrv_error },
	{ scsidrv_check_dev }
};

bool nf_scsidrv(uint32_t stack, uint32_t subid, uint32_t *retval)
{
	if (subid >= ARRAY_SIZE(operations))
	{
		*retval = -1;

		LOG_TRACE(TRACE_SCSIDRV,
		          "ERROR: Invalid SCSI Driver operation %d requested\n", subid);
	}
	else
	{
		*retval = operations[subid].cb(stack);

		LOG_TRACE(TRACE_SCSIDRV, " -> %d\n", *retval);
	}

	return true;
}

void nf_scsidrv_reset(void)
{
	int i;
	for (i = 0; i < SCSI_MAX_HANDLES; i++)
	{
		if (handle_meta_data[i].fd)
		{
			close(handle_meta_data[i].fd);

			handle_meta_data[i].fd = 0;
		}
	}
}

#endif
