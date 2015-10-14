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
#include "gemdos_defines.h"
#include "nf_scsidrv.h"

// The driver interface version, 1.02
#define INTERFACE_VERSION 0x0102
// Maximum is 20 characters
#define BUS_NAME "Linux Generic SCSI"
// The SG driver supports cAllCmds
#define BUS_FEATURES 0x02
// The transfer length may depend on the device, 65536 should always be safe
#define BUS_TRANSFER_LEN 65536
// The maximum number of SCSI Driver handles, must be the same as in stub
#define SCSI_MAX_HANDLES 32


typedef struct
{
    int fd;
    int id_lo;
    int error;
} HANDLE_META_DATA;

static HANDLE_META_DATA handle_meta_data[SCSI_MAX_HANDLES];


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

static void set_error(Uint32 handle, Uint32 errnum)
{
    Uint32 i;
    for(i = 0; i < SCSI_MAX_HANDLES; i++)
    {
        if(handle != i && handle_meta_data[i].fd &&
           handle_meta_data[i].id_lo == handle_meta_data[handle].id_lo)
        {
            handle_meta_data[i].error = errnum;
        }
    }
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
    Uint32 id = read_stack_long(&stack);

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
    Uint32 handle = read_stack_long(&stack);
    Uint32 id = read_stack_long(&stack);

    LOG_TRACE(TRACE_SCSIDRV, "scsidrv_open: handle=%d, id=%d", handle, id);
    
    if(handle >= SCSI_MAX_HANDLES || handle_meta_data[handle].fd ||
       check_device_file(id))
    {
        return GEMDOS_ENHNDL;
    }

    char device_file[16];
    sprintf(device_file, "/dev/sg%d", id);

    int fd = open(device_file, O_RDWR | O_NONBLOCK);
    if(fd < 0)
    {
        return fd;
    }

    handle_meta_data[handle].fd = fd;
    handle_meta_data[handle].id_lo = id;
    handle_meta_data[handle].error = 0;

    return 0;
}

static int scsidrv_close(Uint32 stack)
{
    Uint32 handle = read_stack_long(&stack);

    LOG_TRACE(TRACE_SCSIDRV, "scsidrv_close: handle=%d", handle);

    if(handle >= SCSI_MAX_HANDLES || !handle_meta_data[handle].fd)
    {
        return GEMDOS_ENHNDL;
    }

    close(handle_meta_data[handle].fd);

    handle_meta_data[handle].fd = 0;

    return 0;
}

static int scsidrv_inout(Uint32 stack)
{
    Uint32 handle = read_stack_long(&stack);
    Uint32 dir = read_stack_long(&stack);
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
            "scsidrv_inout: handle=%d, dir=%d, cmd_len=%d, buffer=%p,\n"
            "               transfer_len=%d, sense_buffer=%p, timeout=%d,\n"
            "               cmd=",
            handle, dir, cmd_len, buffer, transfer_len, sense_buffer,
            timeout);

        Uint32 i;
        for(i = 0; i < cmd_len; i++)
        {
            char str[8];
            sprintf(str, i ? ":$%02X" : "$%02X", cmd[i]);
            LOG_TRACE_PRINT("%s", str);
        }
    }
    
    if(handle >= SCSI_MAX_HANDLES || !handle_meta_data[handle].fd)
    {
        return GEMDOS_ENHNDL;
    }

    // No explicit LUN support, the SG driver maps LUNs to device files
    if(cmd[1] & 0xe0)
    {
        if(sense_buffer)
        {
            // Sense Key, ASC
            sense_buffer[2] = 0x05;
            sense_buffer[12] = 0x25;

            LOG_TRACE(TRACE_SCSIDRV,
                      "\n               Sense Key=$%02X, ASC=$%02X, ASCQ=$00",
                      sense_buffer[2], sense_buffer[12]);
        }

        return 2;
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

    int status = ioctl(handle_meta_data[handle].fd,
                       SG_IO, &io_hdr) < 0 ? -1 : io_hdr.status;

    if(status > 0 && sense_buffer)
    {
        LOG_TRACE(TRACE_SCSIDRV,
                  "\n               Sense Key=$%02X, ASC=$%02X, ASCQ=$%02X",
                  sense_buffer[2], sense_buffer[12], sense_buffer[13]);

        if(status == 2)
        {
            // Automatic media change and reset handling for
            // SCSI Driver version 1.0.1
            if((sense_buffer[2] & 0x0f) && !sense_buffer[13])
            {
                if(sense_buffer[12] == 0x28)
                {
                    // cErrMediach
                    set_error(handle, 1);
                }
                else if(sense_buffer[12] == 0x29)
                {
                    // cErrReset
                    set_error(handle, 2);
                }
            }
        }
    }

    return status;
}

static int scsidrv_error(Uint32 stack)
{
    Uint32 handle = read_stack_long(&stack);
    Uint32 rwflag = read_stack_long(&stack);
    Uint32 errnum = read_stack_long(&stack);

    LOG_TRACE(TRACE_SCSIDRV, "scsidrv_error: handle=%d, rwflag=%d, errno=%d",
              handle, rwflag, errnum);

    if(handle >= SCSI_MAX_HANDLES || !handle_meta_data[handle].fd)
    {
        return GEMDOS_ENHNDL;
    }

    if(rwflag)
    {
        set_error(handle, errnum);

        return errnum;
    }
    else
    {
        int status = handle_meta_data[handle].error;
        handle_meta_data[handle].error = 0;

        return status;
    }
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
    { scsidrv_close },
    { scsidrv_inout },
    { scsidrv_error },
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

void nf_scsidrv_reset()
{
    int i;
    for(i = 0; i < SCSI_MAX_HANDLES; i++)
    {
        if(handle_meta_data[i].fd)
        {
            close(handle_meta_data[i].fd);

            handle_meta_data[i].fd = 0;
        }
    }
}

#endif
