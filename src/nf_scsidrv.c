/*
 * Hatari - nf_scsidrv.c
 * 
 * Copyright (C) 2015 by Uwe Seimet
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
const char NfScsiDrv_fileid[] = "Hatari nf_scsidrv.c : " __DATE__ " " __TIME__;

#if defined(__linux__)

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>
#include "stMemory.h"
#include "log.h"
#include "nf_scsidrv.h"

// The driver interface version, 1.00
#define INTERFACE_VERSION 0x0100
// Maximum is 20 characters
#define BUS_NAME "Linux Generic SCSI"
// The SG driver supports cAllCmds
#define BUS_FEATURES 0x02
// The transfer length may depend on the device, 65536 should always be safe
#define BUS_TRANSFER_LEN 65536


static Uint32 read_stack_long(Uint32 *stack)
{
    Uint32 value = STMemory_ReadLong(*stack);

    *stack += SIZE_LONG;
 
    return value;
}

static void *read_stack_pointer(Uint32 *stack)
{
    Uint32 ptr = read_stack_long(stack);
    return ptr ? STMemory_STAddrToPointer(ptr) : 0;
}

static void write_long(Uint32 addr, Uint32 value)
{
    STMemory_WriteLong(addr, value);
}

static void write_word(Uint32 addr, Uint16 value)
{
    STMemory_WriteWord(addr, value);
}

static int check_device_file(Uint32 id)
{
    char device_file[16];
    sprintf(device_file, "/dev/sg%d", id);

    if(!access(device_file, R_OK | W_OK))
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

static int scsidrv_interface_version(Uint32 stack)
{
    return INTERFACE_VERSION;
}

static int scsidrv_interface_features(Uint32 stack)
{
    char *busName = read_stack_pointer(&stack);
    Uint32 features = read_stack_long(&stack);
    Uint32 transferLen = read_stack_long(&stack);

    strncpy(busName, BUS_NAME, 20);
    write_word(features, BUS_FEATURES);
    write_long(transferLen, BUS_TRANSFER_LEN);

    return 0;
}

static int scsidrv_inquire_bus(Uint32 stack)
{
    Uint16 id = read_stack_long(&stack);

    LOG_TRACE(TRACE_SCSIDRV, "scsidrv_inquire_bus: id=%d", id);

    char device_file[16];
    sprintf(device_file, "/dev/sg%d", id);

    while(!access(device_file, F_OK))
    {
        if(!check_device_file(id))
        {
            return id;
        }

        sprintf(device_file, "/dev/sg%d", ++id);
    }

    return -1;
}

static int scsidrv_open(Uint32 stack)
{
    Uint32 id = read_stack_long(&stack);

    LOG_TRACE(TRACE_SCSIDRV, "scsidrv_open: id=%d", id);
    
    return check_device_file(id);
}

static int scsidrv_inout(Uint32 stack)
{
    Uint32 dir = read_stack_long(&stack);
    Uint32 id = read_stack_long(&stack);
    unsigned char *cmd = read_stack_pointer(&stack);
    Uint32 cmd_len = read_stack_long(&stack);
    unsigned char *buffer = read_stack_pointer(&stack);
    Uint32 transfer_len = read_stack_long(&stack);
    unsigned char *sense_buffer = read_stack_pointer(&stack);
    if(sense_buffer)
    {
        memset(sense_buffer, 0, 18);
    }
    Uint32 timeout = read_stack_long(&stack);

    if(LOG_TRACE_LEVEL(TRACE_SCSIDRV))
    {
        LOG_TRACE_PRINT(
            "scsidrv_inout: dir=%d, id=%d, cmd_len=%d, buffer=%p,\n"
            "               transfer_len=%d, sense_buffer=%p, timeout=%d,\n"
            "               cmd=",
            dir, id, cmd_len, buffer, transfer_len, sense_buffer,
            timeout);

        Uint32 i;
        for(i = 0; i < cmd_len; i++)
        {
            char str[8];
            sprintf(str, i ? ":$%02X" : "$%02X", cmd[i]);
            LOG_TRACE_PRINT("%s", str);
        }
    }

    // No explicit LUN support, the SG driver maps LUNs to device files
    if(cmd[1] & 0xe0)
    {
        if(sense_buffer)
        {
            // Sense Key and ASC
            sense_buffer[2] = 0x05;
            sense_buffer[12] = 0x25;

            LOG_TRACE(TRACE_SCSIDRV,
                      "\n               Sense Key=$%02X, ASC=$%02X, ASCQ=$00",
                      sense_buffer[2], sense_buffer[12]);
        }

        return 2;
    }

    char device_file[16];
    sprintf(device_file, "/dev/sg%d", id);

    int fd = open(device_file, O_RDWR | O_NONBLOCK | O_EXCL);
    if(fd < 0) {
        LOG_TRACE(TRACE_SCSIDRV, "               Cannot open device file %s",
                  device_file);

        return -1;
    }
    
    struct sg_io_hdr io_hdr;
    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));

    io_hdr.interface_id = 'S';

    io_hdr.dxfer_direction = dir ? SG_DXFER_TO_DEV : SG_DXFER_FROM_DEV;
    if(!transfer_len) {
        io_hdr.dxfer_direction = SG_DXFER_NONE;
    }
    
    io_hdr.dxferp = buffer;
    io_hdr.dxfer_len = transfer_len;

    io_hdr.sbp = sense_buffer;
    io_hdr.mx_sb_len = 18;

    io_hdr.cmdp = cmd;
    io_hdr.cmd_len = cmd_len;

    io_hdr.timeout = timeout;

    int status = ioctl(fd, SG_IO, &io_hdr) < 0 ? -1 : io_hdr.status;

    close(fd);

    if(status > 0 && sense_buffer)
    {
        LOG_TRACE(TRACE_SCSIDRV,
                  "\n               Sense Key=$%02X, ASC=$%02X, ASCQ=$%02X",
                  sense_buffer[2], sense_buffer[12], sense_buffer[13]);
    }

    return status;
}

static int scsidrv_check_dev(Uint32 stack)
{
    Uint32 id = read_stack_long(&stack);

    LOG_TRACE(TRACE_SCSIDRV, "scsidrv_check_dev: id=%d", id);

    return check_device_file(id);
}

static const struct {
    int (*cb)(Uint32 stack);
} operations[] = {
    { scsidrv_interface_version },
    { scsidrv_interface_features },
    { scsidrv_inquire_bus },
    { scsidrv_open },
    { scsidrv_inout },
    { scsidrv_check_dev }
};

bool nf_scsidrv(Uint32 stack, Uint32 subid, Uint32 *retval)
{
    if (subid >= ARRAYSIZE(operations)) {
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

#endif
