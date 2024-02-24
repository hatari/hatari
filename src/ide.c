/*
  Hatari - ide.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This is where we intercept read/writes to/from the IDE controller hardware.
*/

#include <SDL_endian.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "ide.h"
#include "hdc.h" /* for partition counting */
#include "m68000.h"
#include "mfp.h"
#include "stMemory.h"
#include "sysdeps.h"

int nIDEPartitions = 0;

struct IDEState;
typedef void EndTransferFunc(struct IDEState *);
typedef struct BlockDriverState BlockDriverState;

/* NOTE: IDEState represents in fact one drive */
typedef struct IDEState
{
	/* ide config */
	int is_cdrom;
	int cylinders, heads, sectors;
	int64_t nb_sectors;
	int mult_sectors;
	int identify_set;
	uint16_t identify_data[256];
	int drive_serial;
	/* ide regs */
	uint8_t feature;
	uint8_t error;
	uint32_t nsector;
	uint8_t sector;
	uint8_t lcyl;
	uint8_t hcyl;
	/* other part of tf for lba48 support */
	uint8_t hob_feature;
	uint8_t hob_nsector;
	uint8_t hob_sector;
	uint8_t hob_lcyl;
	uint8_t hob_hcyl;

	uint8_t select;
	uint8_t status;

	/* 0x3f6 command, only meaningful for drive 0 */
	uint8_t cmd;
	/* set for lba48 access */
	uint8_t lba48;
	/* depends on bit 4 in select, only meaningful for drive 0 */
	struct IDEState *cur_drive;
	BlockDriverState *bs;
	/* ATAPI specific */
	uint8_t sense_key;
	uint8_t asc;
	int packet_transfer_size;
	int elementary_transfer_size;
	int io_buffer_index;
	int lba;
	int cd_sector_size;
	/* ATA DMA state */
	int io_buffer_size;
	/* PIO transfer handling */
	int req_nb_sectors; /* number of sectors per interrupt */
	EndTransferFunc *end_transfer_func;
	uint8_t *data_ptr;
	uint8_t *data_end;
	uint8_t *io_buffer;
	int media_changed;
} IDEState;

static IDEState ide_state[2];

static void ide_ioport_write(IDEState *ide_if, uint32_t addr, uint32_t val);
static uint32_t ide_ioport_read(IDEState *ide_if, uint32_t addr1);
static uint32_t ide_status_read(IDEState *ide_if, uint32_t addr);
static void ide_ctrl_write(IDEState *ide_if, uint32_t addr, uint32_t val);
static void ide_data_writew(IDEState *ide_if, uint32_t addr, uint32_t val);
static uint32_t ide_data_readw(IDEState *ide_if, uint32_t addr);
static void ide_data_writel(IDEState *ide_if, uint32_t addr, uint32_t val);
static uint32_t ide_data_readl(IDEState *ide_if, uint32_t addr);

/**
 * Check whether IDE is available: The Falcon always has an IDE controller,
 * and for the other machines it is normally only available on expansion
 * cards - we assume that the users want us to emulate an IDE controller
 * on such an expansion card if one of the IDE drives has been enabled.
 * Note that we also disable IDE on Falcon if bFastBoot is enabled - TOS
 * boots much faster if it does not have to scan for IDE devices.
 */
bool Ide_IsAvailable(void)
{
	return ConfigureParams.Ide[0].bUseDevice ||
	       ConfigureParams.Ide[1].bUseDevice ||
	       (Config_IsMachineFalcon() && !ConfigureParams.System.bFastBoot);
}

/**
 * Convert Falcon IDE registers to "normal" IDE register numbers.
 * (taken from Aranym - cheers!)
 */
static uint32_t fcha2io(uint32_t address)
{
	switch (address)
	{
	case 0xf00000:
		return 0x00;
	case 0xf00005:
		return 0x01;
	case 0xf00009:
		return 0x02;
	case 0xf0000d:
		return 0x03;
	case 0xf00011:
		return 0x04;
	case 0xf00015:
		return 0x05;
	case 0xf00019:
		return 0x06;
	case 0xf0001d:
		return 0x07;
	case 0xf00039:
		return 0x16;
	default:
		return 0xffffffff;
	}
}


/**
 * Handle byte read access from IDE IO memory.
 * Note: Registers are available from usermode, too, so there is no check for
 * the supervisor mode required here.
 */
uae_u32 REGPARAM3 Ide_Mem_bget(uaecptr addr)
{
	int ideport;
	uint8_t retval;
	uaecptr addr_in = addr;

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr >= 0xf00040 || !Ide_IsAvailable())
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr_in, BUS_ERROR_READ, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, 0);
		return -1;
	}

	ideport = fcha2io(addr);

	if (ideport >= 1 && ideport <= 7)
	{
		retval = ide_ioport_read(ide_state, ideport);
	}
	else if (ideport == 8 || ideport == 22)
	{
		retval = ide_status_read(ide_state, 0);
	}
	else
	{
		retval = 0xFF;
	}

	LOG_TRACE(TRACE_IDE, "IDE: bget($%x) = $%02x\n", addr, retval);
	return retval;
}


/**
 * Handle word read access from IDE IO memory.
 */
uae_u32 REGPARAM3 Ide_Mem_wget(uaecptr addr)
{
	uint16_t retval;
	uaecptr addr_in = addr;

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr >= 0xf00040 || !Ide_IsAvailable())
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr_in, BUS_ERROR_READ, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, 0);
		return -1;
	}

	if (addr == 0xf00000 || addr == 0xf00002)
	{
		retval = ide_data_readw(ide_state, 0);
	}
	else
	{
		retval = 0xFFFF;
	}

	LOG_TRACE(TRACE_IDE, "IDE: wget($%x) = $%04x\n", addr, retval);
	return retval;
}


/**
 * Handle long-word read access from IDE IO memory.
 */
uae_u32 REGPARAM3 Ide_Mem_lget(uaecptr addr)
{
	uint32_t retval;
	uaecptr addr_in = addr;

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	if (addr >= 0xf00040 || !Ide_IsAvailable())
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr_in, BUS_ERROR_READ, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, 0);
		return -1;
	}

	if (addr == 0xf00000)
	{
		retval = ide_data_readl(ide_state, 0);
	}
	else
	{
		retval = 0xFFFFFFFF;
	}

	/* word swap for long access to data register */
	retval = ((retval >> 16) & 0x0000ffff) | ((retval & 0x0000ffff) << 16);

	LOG_TRACE(TRACE_IDE, "IDE: lget($%x) = $%08x\n", addr, retval);
	return retval;
}


/**
 * Handle byte write access to IDE IO memory.
 * Note: Registers are available from usermode, too, so there is no check for
 * the supervisor mode required here.
 */
void REGPARAM3 Ide_Mem_bput(uaecptr addr, uae_u32 val)
{
	int ideport;
	uaecptr addr_in = addr;

	addr &= 0x00ffffff;                           /* Use a 24 bit address */
	val &= 0x0ff;

	LOG_TRACE(TRACE_IDE, "IDE: bput($%x, $%x)\n", addr, val);

	if (addr >= 0xf00040 || !Ide_IsAvailable())
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr_in, BUS_ERROR_WRITE, BUS_ERROR_SIZE_BYTE, BUS_ERROR_ACCESS_DATA, val);
		return;
	}

	ideport = fcha2io(addr);

	if (ideport >= 1 && ideport <= 7)
	{
		ide_ioport_write(ide_state, ideport, val);
	}
	else if (ideport == 8 || ideport == 22)
	{
		ide_ctrl_write(ide_state, 0, val);
	}
}


/**
 * Handle word write access to IDE IO memory.
 */
void REGPARAM3 Ide_Mem_wput(uaecptr addr, uae_u32 val)
{
	uaecptr addr_in = addr;

	addr &= 0x00ffffff;                           /* Use a 24 bit address */
	val &= 0x0ffff;

	LOG_TRACE(TRACE_IDE, "IDE: wput($%x, $%x)\n", addr, val);

	if (addr >= 0xf00040 || !Ide_IsAvailable())
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr_in, BUS_ERROR_WRITE, BUS_ERROR_SIZE_WORD, BUS_ERROR_ACCESS_DATA, val);
		return;
	}

	if (addr == 0xf00000 || addr == 0xf00002)
	{
		ide_data_writew(ide_state, 0, val);
	}
}


/**
 * Handle long-word write access to IDE IO memory.
 */
void REGPARAM3 Ide_Mem_lput(uaecptr addr, uae_u32 val)
{
	uaecptr addr_in = addr;

	addr &= 0x00ffffff;                           /* Use a 24 bit address */

	LOG_TRACE(TRACE_IDE, "IDE: lput($%x, $%x)\n", addr, val);

	if (addr >= 0xf00040 || !Ide_IsAvailable())
	{
		/* invalid memory addressing --> bus error */
		M68000_BusError(addr_in, BUS_ERROR_WRITE, BUS_ERROR_SIZE_LONG, BUS_ERROR_ACCESS_DATA, val);
		return;
	}

	/* word swap for long access to data register */
	val = ((val >> 16) & 0x0000ffff) | ((val & 0x0000ffff) << 16);

	if (addr == 0xf00000)
	{
		ide_data_writel(ide_state, 0, val);
	}
}


/*----------------------------------------------------------------------------*/


/*
 * QEMU IDE disk and CD-ROM Emulator
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
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
 */

#define FW_VERSION "1.0"


#define BDRV_TYPE_HD     0
#define BDRV_TYPE_CDROM  1
#define BDRV_TYPE_FLOPPY 2
#define BIOS_ATA_TRANSLATION_AUTO   0
#define BIOS_ATA_TRANSLATION_NONE   1
#define BIOS_ATA_TRANSLATION_LBA    2
#define BIOS_ATA_TRANSLATION_LARGE  3
#define BIOS_ATA_TRANSLATION_RECHS  4

#ifndef ENOMEDIUM           // It's not defined on macOS for example
#define ENOMEDIUM ENODEV
#endif


struct BlockDriverState {
    int64_t total_sectors; /* if we are reading a disk image, give its
                              size in sectors */
    int read_only; /* if true, the media is read only */
    int removable; /* if true, the media can be removed */
    int locked;    /* if true, the media cannot temporarily be ejected */
    int sg;        /* if true, the device is a /dev/sg* */
    /* event callback when inserting/removing */
    void (*change_cb)(void *opaque);
    void *change_opaque;

    FILE *fhndl;
    off_t file_size;
    int media_changed;
    int byteswap;
    int sector_size;

    /* I/O stats (display with "info blockstats"). */
    uint64_t rd_bytes;
    uint64_t wr_bytes;
    uint64_t rd_ops;
    uint64_t wr_ops;

    /* NOTE: the following infos are only hints for real hardware
       drivers. They are not used by the block driver */
    int cyls, heads, secs, translation;
    int type;
};


static inline void cpu_to_be16wu(uint16_t *p, uint16_t v)
{
	uint8_t *p1 = (uint8_t *)p;

	p1[0] = v >> 8;
	p1[1] = v;
}


#define le32_to_cpu SDL_SwapLE32
#define le16_to_cpu SDL_SwapLE16
#define cpu_to_le32 SDL_SwapLE32
#define cpu_to_le16 SDL_SwapLE16


#define MIN(a, b) (((a) < (b)) ? (a) : (b))


/**
 * return 0 as number of sectors if no device present or error
 */
static void bdrv_get_geometry(BlockDriverState *bs, uint64_t *nb_sectors_ptr)
{
	off_t length = bs->file_size;

	if (length < 0)
		length = 0;
	else
		length = length / bs->sector_size;
	*nb_sectors_ptr = length;
}

static void bdrv_get_geometry_hint(BlockDriverState *bs,
                            int *pcyls, int *pheads, int *psecs)
{
	*pcyls = bs->cyls;
	*pheads = bs->heads;
	*psecs = bs->secs;
}

static void bdrv_set_translation_hint(BlockDriverState *bs, int translation)
{
	bs->translation = translation;
}

static void bdrv_set_geometry_hint(BlockDriverState *bs,
                            int cyls, int heads, int secs)
{
	bs->cyls = cyls;
	bs->heads = heads;
	bs->secs = secs;
}

static int bdrv_get_type_hint(BlockDriverState *bs)
{
	return bs->type;
}

static int bdrv_get_translation_hint(BlockDriverState *bs)
{
	return bs->translation;
}

/* XXX: no longer used */
static void bdrv_set_change_cb(BlockDriverState *bs,
                        void (*change_cb)(void *opaque), void *opaque)
{
	bs->change_cb = change_cb;
	bs->change_opaque = opaque;
}


/**
 * Return TRUE if the media is present
 */
static int bdrv_is_inserted(BlockDriverState *bs)
{
	return (bs->fhndl != NULL);
}


static int bdrv_is_locked(BlockDriverState *bs)
{
	return bs->locked;
}

/**
 * Lock or unlock the media (if it is locked, the user won't be able
 * to eject it manually).
 */
static void bdrv_set_locked(BlockDriverState *bs, int locked)
{
	bs->locked = locked;
}

/* return < 0 if error. See bdrv_write() for the return codes */
static int bdrv_read(BlockDriverState *bs, int64_t sector_num,
                     uint8_t *buf, int nb_sectors)
{
	int ret, len;

	if (!bs->fhndl)
		return -ENOMEDIUM;

	len = nb_sectors * bs->sector_size;

	if (fseeko(bs->fhndl, sector_num * bs->sector_size, SEEK_SET) != 0)
	{
		perror("bdrv_read");
		return -errno;
	}
	ret = fread(buf, 1, len, bs->fhndl);
	if (ret != len)
	{
		Log_Printf(LOG_ERROR, "IDE: bdrv_read error (%d != %d length) at sector %lu!\n",
		           ret, len, (unsigned long)sector_num);
		return -EINVAL;
	}

	bs->rd_bytes += (unsigned) len;
	bs->rd_ops ++;

	if (bs->byteswap)
	{
		uint16_t *buf16 = (uint16_t *)buf;
		while (len > 0) {
			*buf16 = SDL_Swap16(*buf16);
			buf16++;
			len -= 2;
		}
	}

	return 0;
}


/* Return < 0 if error. Important errors are:
  -EIO         generic I/O error (may happen for all errors)
  -ENOMEDIUM   No media inserted.
  -EINVAL      Invalid sector number or nb_sectors
  -EACCES      Trying to write a read-only device
*/
static int bdrv_write(BlockDriverState *bs, int64_t sector_num,
                      const uint8_t *buf, int nb_sectors)
{
	int ret, len, idx;
	uint16_t *buf16;

	if (!bs->fhndl)
		return -ENOMEDIUM;
	if (bs->read_only)
		return -EACCES;

	len = nb_sectors * bs->sector_size;

	if (fseeko(bs->fhndl, sector_num * bs->sector_size, SEEK_SET) != 0)
	{
		perror("bdrv_write");
		return -errno;
	}

	if (!bs->byteswap)
	{
		ret = fwrite(buf, 1, len, bs->fhndl);
	}
	else
	{
		buf16 = malloc(len);
		if (!buf16)
			return -ENOMEM;
		for (idx = 0; idx < len; idx += 2)
		{
			buf16[idx / 2] = SDL_Swap16(*(const uint16_t *)&buf[idx]);
		}
		ret = fwrite(buf16, 1, len, bs->fhndl);
		free(buf16);
	}
	if (ret != len)
	{
		Log_Printf(LOG_ERROR, "IDE: bdrv_write error (%d != %d length) at sector %lu!\n",
		           ret, len,  (unsigned long)sector_num);
		return -EIO;
	}

	bs->wr_bytes += (unsigned) len;
	bs->wr_ops ++;

	return 0;
}


static int bdrv_open(BlockDriverState *bs, const char *filename, unsigned long blockSize, int flags)
{
	Log_Printf(LOG_INFO, "Mounting IDE hard drive image %s\n", filename);

	bs->read_only = 0;
	bs->file_size = HDC_CheckAndGetSize("IDE", filename, blockSize);
	if (bs->file_size <= 0)
		return -1;
	if (bs->file_size < 2 * 16 * 63 * bs->sector_size)
	{
		Log_AlertDlg(LOG_ERROR, "IDE disk image size (%"PRId64" bytes) is "
		                        "too small for an IDE disk image "
		                        "(min. 1032192 byte)", (int64_t)bs->file_size);
		return -1;
	}

	bs->fhndl = fopen(filename, "rb+");
	if (!bs->fhndl) {
		/* Maybe the file is read-only? */
		bs->fhndl = fopen(filename, "rb");
		if (!bs->fhndl)
		{
			perror("bdrv_open");
			Log_AlertDlg(LOG_ERROR, "Cannot open IDE HD for reading\n'%s'.\n", filename);
			return -1;
		}
		Log_AlertDlg(LOG_WARN, "IDE HD file is read-only, no writes will go through\n'%s'.\n",
			     filename);
		bs->read_only = 1;
	}
	else if (!File_Lock(bs->fhndl))
	{
		Log_AlertDlg(LOG_ERROR, "Locking IDE HD file for writing failed\n'%s'!\n", filename);
		fclose(bs->fhndl);
		bs->fhndl = NULL;
		return -1;
	}

	/* call the change callback */
	bs->media_changed = 1;
	if (bs->change_cb)
		bs->change_cb(bs->change_opaque);

	return 0;
}

static void bdrv_flush(BlockDriverState *bs)
{
	fflush(bs->fhndl);
}

static void bdrv_close(BlockDriverState *bs)
{
	File_UnLock(bs->fhndl);
	fclose(bs->fhndl);
	bs->fhndl = NULL;
}

/**
 * If eject_flag is TRUE, eject the media. Otherwise, close the tray
 */
static void bdrv_eject(BlockDriverState *bs, int eject_flag)
{
	if (eject_flag)
		bdrv_close(bs);
}


// #define USE_DMA_CDROM

/* Bits of HD_STATUS */
#define ERR_STAT		0x01
#define INDEX_STAT		0x02
#define ECC_STAT		0x04	/* Corrected error */
#define DRQ_STAT		0x08
#define SEEK_STAT		0x10
#define SRV_STAT		0x10
#define WRERR_STAT		0x20
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits for HD_ERROR */
#define MARK_ERR		0x01	/* Bad address mark */
#define TRK0_ERR		0x02	/* couldn't find track 0 */
#define ABRT_ERR		0x04	/* Command aborted */
#define MCR_ERR			0x08	/* media change request */
#define ID_ERR			0x10	/* ID field not found */
#define MC_ERR			0x20	/* media changed */
#define ECC_ERR			0x40	/* Uncorrectable ECC error */
#define BBD_ERR			0x80	/* pre-EIDE meaning:  block marked bad */
#define ICRC_ERR		0x80	/* new meaning:  CRC error during transfer */

/* Bits of HD_NSECTOR */
#define CD			0x01
#define IO			0x02
#define REL			0x04
#define TAG_MASK		0xf8

/* Bits of Device Control register */
#define IDE_CTRL_HOB		0x80
#define IDE_CTRL_RESET		0x04
#define IDE_CTRL_DISABLE_IRQ	0x02

/* ATA/ATAPI Commands pre T13 Spec */
#define WIN_NOP				0x00
/*
 *	0x01->0x02 Reserved
 */
#define CFA_REQ_EXT_ERROR_CODE		0x03 /* CFA Request Extended Error Code */
/*
 *	0x04->0x07 Reserved
 */
#define WIN_SRST			0x08 /* ATAPI soft reset command */
#define WIN_DEVICE_RESET		0x08
/*
 *	0x09->0x0F Reserved
 */
#define WIN_RECAL			0x10
#define WIN_RESTORE			WIN_RECAL
/*
 *	0x10->0x1F Reserved
 */
#define WIN_READ			0x20 /* 28-Bit */
#define WIN_READ_ONCE			0x21 /* 28-Bit without retries */
#define WIN_READ_LONG			0x22 /* 28-Bit */
#define WIN_READ_LONG_ONCE		0x23 /* 28-Bit without retries */
#define WIN_READ_EXT			0x24 /* 48-Bit */
#define WIN_READDMA_EXT			0x25 /* 48-Bit */
#define WIN_READDMA_QUEUED_EXT		0x26 /* 48-Bit */
#define WIN_READ_NATIVE_MAX_EXT		0x27 /* 48-Bit */
/*
 *	0x28
 */
#define WIN_MULTREAD_EXT		0x29 /* 48-Bit */
/*
 *	0x2A->0x2F Reserved
 */
#define WIN_WRITE			0x30 /* 28-Bit */
#define WIN_WRITE_ONCE			0x31 /* 28-Bit without retries */
#define WIN_WRITE_LONG			0x32 /* 28-Bit */
#define WIN_WRITE_LONG_ONCE		0x33 /* 28-Bit without retries */
#define WIN_WRITE_EXT			0x34 /* 48-Bit */
#define WIN_WRITEDMA_EXT		0x35 /* 48-Bit */
#define WIN_WRITEDMA_QUEUED_EXT		0x36 /* 48-Bit */
#define WIN_SET_MAX_EXT			0x37 /* 48-Bit */
#define CFA_WRITE_SECT_WO_ERASE		0x38 /* CFA Write Sectors without erase */
#define WIN_MULTWRITE_EXT		0x39 /* 48-Bit */
/*
 *	0x3A->0x3B Reserved
 */
#define WIN_WRITE_VERIFY		0x3C /* 28-Bit */
/*
 *	0x3D->0x3F Reserved
 */
#define WIN_VERIFY			0x40 /* 28-Bit - Read Verify Sectors */
#define WIN_VERIFY_ONCE			0x41 /* 28-Bit - without retries */
#define WIN_VERIFY_EXT			0x42 /* 48-Bit */
/*
 *	0x43->0x4F Reserved
 */
#define WIN_FORMAT			0x50
/*
 *	0x51->0x5F Reserved
 */
#define WIN_INIT			0x60
/*
 *	0x61->0x5F Reserved
 */
#define WIN_SEEK			0x70 /* 0x70-0x7F Reserved */
#define CFA_TRANSLATE_SECTOR		0x87 /* CFA Translate Sector */
#define WIN_DIAGNOSE			0x90
#define WIN_SPECIFY			0x91 /* set drive geometry translation */
#define WIN_DOWNLOAD_MICROCODE		0x92
#define WIN_STANDBYNOW2			0x94
#define CFA_IDLEIMMEDIATE		0x95 /* force drive to become "ready" */
#define WIN_STANDBY2			0x96
#define WIN_SETIDLE2			0x97
#define WIN_CHECKPOWERMODE2		0x98
#define WIN_SLEEPNOW2			0x99
/*
 *	0x9A VENDOR
 */
#define WIN_PACKETCMD			0xA0 /* Send a packet command. */
#define WIN_PIDENTIFY			0xA1 /* identify ATAPI device	*/
#define WIN_QUEUED_SERVICE		0xA2
#define WIN_SMART			0xB0 /* self-monitoring and reporting */
#define CFA_ACCESS_METADATA_STORAGE	0xB8
#define CFA_ERASE_SECTORS       	0xC0 /* microdrives implement as NOP */
#define WIN_MULTREAD			0xC4 /* read sectors using multiple mode*/
#define WIN_MULTWRITE			0xC5 /* write sectors using multiple mode */
#define WIN_SETMULT			0xC6 /* enable/disable multiple mode */
#define WIN_READDMA_QUEUED		0xC7 /* read sectors using Queued DMA transfers */
#define WIN_READDMA			0xC8 /* read sectors using DMA transfers */
#define WIN_READDMA_ONCE		0xC9 /* 28-Bit - without retries */
#define WIN_WRITEDMA			0xCA /* write sectors using DMA transfers */
#define WIN_WRITEDMA_ONCE		0xCB /* 28-Bit - without retries */
#define WIN_WRITEDMA_QUEUED		0xCC /* write sectors using Queued DMA transfers */
#define CFA_WRITE_MULTI_WO_ERASE	0xCD /* CFA Write multiple without erase */
#define WIN_GETMEDIASTATUS		0xDA
#define WIN_ACKMEDIACHANGE		0xDB /* ATA-1, ATA-2 vendor */
#define WIN_POSTBOOT			0xDC
#define WIN_PREBOOT			0xDD
#define WIN_DOORLOCK			0xDE /* lock door on removable drives */
#define WIN_DOORUNLOCK			0xDF /* unlock door on removable drives */
#define WIN_STANDBYNOW1			0xE0
#define WIN_IDLEIMMEDIATE		0xE1 /* force drive to become "ready" */
#define WIN_STANDBY             	0xE2 /* Set device in Standby Mode */
#define WIN_SETIDLE1			0xE3
#define WIN_READ_BUFFER			0xE4 /* force read only 1 sector */
#define WIN_CHECKPOWERMODE1		0xE5
#define WIN_SLEEPNOW1			0xE6
#define WIN_FLUSH_CACHE			0xE7
#define WIN_WRITE_BUFFER		0xE8 /* force write only 1 sector */
#define WIN_WRITE_SAME			0xE9 /* read ata-2 to use */
/* SET_FEATURES 0x22 or 0xDD */
#define WIN_FLUSH_CACHE_EXT		0xEA /* 48-Bit */
#define WIN_IDENTIFY			0xEC /* ask drive to identify itself	*/
#define WIN_MEDIAEJECT			0xED
#define WIN_IDENTIFY_DMA		0xEE /* same as WIN_IDENTIFY, but DMA */
#define WIN_SETFEATURES			0xEF /* set special drive features */
#define EXABYTE_ENABLE_NEST		0xF0
#define IBM_SENSE_CONDITION		0xF0 /* measure disk temperature */
#define WIN_SECURITY_SET_PASS		0xF1
#define WIN_SECURITY_UNLOCK		0xF2
#define WIN_SECURITY_ERASE_PREPARE	0xF3
#define WIN_SECURITY_ERASE_UNIT		0xF4
#define WIN_SECURITY_FREEZE_LOCK	0xF5
#define CFA_WEAR_LEVEL			0xF5 /* microdrives implement as NOP */
#define WIN_SECURITY_DISABLE		0xF6
#define WIN_READ_NATIVE_MAX		0xF8 /* return the native maximum address */
#define WIN_SET_MAX			0xF9
#define DISABLE_SEAGATE			0xFB

/* set to 1 set disable mult support */
#define MAX_MULT_SECTORS 16

/* maximum physical IDE hard disk drive sector size */
#define MAX_SECTOR_SIZE 4096

/* ATAPI defines */

#define ATAPI_PACKET_SIZE 12

/* The generic packet command opcodes for CD/DVD Logical Units,
 * From Table 57 of the SFF8090 Ver. 3 (Mt. Fuji) draft standard. */
#define GPCMD_BLANK			    0xa1
#define GPCMD_CLOSE_TRACK		    0x5b
#define GPCMD_FLUSH_CACHE		    0x35
#define GPCMD_FORMAT_UNIT		    0x04
#define GPCMD_GET_CONFIGURATION		    0x46
#define GPCMD_GET_EVENT_STATUS_NOTIFICATION 0x4a
#define GPCMD_GET_PERFORMANCE		    0xac
#define GPCMD_INQUIRY			    0x12
#define GPCMD_LOAD_UNLOAD		    0xa6
#define GPCMD_MECHANISM_STATUS		    0xbd
#define GPCMD_MODE_SELECT_10		    0x55
#define GPCMD_MODE_SENSE_10		    0x5a
#define GPCMD_PAUSE_RESUME		    0x4b
#define GPCMD_PLAY_AUDIO_10		    0x45
#define GPCMD_PLAY_AUDIO_MSF		    0x47
#define GPCMD_PLAY_AUDIO_TI		    0x48
#define GPCMD_PLAY_CD			    0xbc
#define GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL  0x1e
#define GPCMD_READ_10			    0x28
#define GPCMD_READ_12			    0xa8
#define GPCMD_READ_CDVD_CAPACITY	    0x25
#define GPCMD_READ_CD			    0xbe
#define GPCMD_READ_CD_MSF		    0xb9
#define GPCMD_READ_DISC_INFO		    0x51
#define GPCMD_READ_DVD_STRUCTURE	    0xad
#define GPCMD_READ_FORMAT_CAPACITIES	    0x23
#define GPCMD_READ_HEADER		    0x44
#define GPCMD_READ_TRACK_RZONE_INFO	    0x52
#define GPCMD_READ_SUBCHANNEL		    0x42
#define GPCMD_READ_TOC_PMA_ATIP		    0x43
#define GPCMD_REPAIR_RZONE_TRACK	    0x58
#define GPCMD_REPORT_KEY		    0xa4
#define GPCMD_REQUEST_SENSE		    0x03
#define GPCMD_RESERVE_RZONE_TRACK	    0x53
#define GPCMD_SCAN			    0xba
#define GPCMD_SEEK			    0x2b
#define GPCMD_SEND_DVD_STRUCTURE	    0xad
#define GPCMD_SEND_EVENT		    0xa2
#define GPCMD_SEND_KEY			    0xa3
#define GPCMD_SEND_OPC			    0x54
#define GPCMD_SET_READ_AHEAD		    0xa7
#define GPCMD_SET_STREAMING		    0xb6
#define GPCMD_START_STOP_UNIT		    0x1b
#define GPCMD_STOP_PLAY_SCAN		    0x4e
#define GPCMD_TEST_UNIT_READY		    0x00
#define GPCMD_VERIFY_10			    0x2f
#define GPCMD_WRITE_10			    0x2a
#define GPCMD_WRITE_AND_VERIFY_10	    0x2e
/* This is listed as optional in ATAPI 2.6, but is (curiously)
 * missing from Mt. Fuji, Table 57.  It _is_ mentioned in Mt. Fuji
 * Table 377 as an MMC command for SCSi devices though...  Most ATAPI
 * drives support it. */
#define GPCMD_SET_SPEED			    0xbb
/* This seems to be a SCSI specific CD-ROM opcode
 * to play data at track/index */
#define GPCMD_PLAYAUDIO_TI		    0x48
/*
 * From MS Media Status Notification Support Specification. For
 * older drives only.
 */
#define GPCMD_GET_MEDIA_STATUS		    0xda
#define GPCMD_MODE_SENSE_6		    0x1a

/* Mode page codes for mode sense/set */
#define GPMODE_R_W_ERROR_PAGE		0x01
#define GPMODE_WRITE_PARMS_PAGE		0x05
#define GPMODE_AUDIO_CTL_PAGE		0x0e
#define GPMODE_POWER_PAGE		0x1a
#define GPMODE_FAULT_FAIL_PAGE		0x1c
#define GPMODE_TO_PROTECT_PAGE		0x1d
#define GPMODE_CAPABILITIES_PAGE	0x2a
#define GPMODE_ALL_PAGES		0x3f
/* Not in Mt. Fuji, but in ATAPI 2.6 -- deprecated now in favor
 * of MODE_SENSE_POWER_PAGE */
#define GPMODE_CDROM_PAGE		0x0d

#define ATAPI_INT_REASON_CD             0x01 /* 0 = data transfer */
#define ATAPI_INT_REASON_IO             0x02 /* 1 = transfer to the host */
#define ATAPI_INT_REASON_REL            0x04
#define ATAPI_INT_REASON_TAG            0xf8

/* same constants as bochs */
#define ASC_ILLEGAL_OPCODE                   0x20
#define ASC_LOGICAL_BLOCK_OOR                0x21
#define ASC_INV_FIELD_IN_CMD_PACKET          0x24
#define ASC_MEDIUM_NOT_PRESENT               0x3a
#define ASC_SAVING_PARAMETERS_NOT_SUPPORTED  0x39

#define SENSE_NONE            0
#define SENSE_NOT_READY       2
#define SENSE_ILLEGAL_REQUEST 5
#define SENSE_UNIT_ATTENTION  6


static void padstr(char *str, const char *src, int len)
{
	int i, v;
	for (i = 0; i < len; i++)
	{
		if (*src)
			v = *src++;
		else
			v = ' ';
		str[i^1] = v;
	}
}

static void padstr8(uint8_t *buf, int buf_size, const char *src)
{
	int i;
	for (i = 0; i < buf_size; i++)
	{
		if (*src)
			buf[i] = *src++;
		else
			buf[i] = ' ';
	}
}

static void put_le16(uint16_t *p, unsigned int v)
{
	*p = SDL_SwapLE16(v);
}

static void ide_identify(IDEState *s)
{
	uint16_t *p;
	unsigned int oldsize;
	char buf[40];
	int64_t nb_sectors_lba28;

	if (s->identify_set)
	{
		memcpy(s->io_buffer, s->identify_data, sizeof(s->identify_data));
		return;
	}

	memset(s->io_buffer, 0, 512);
	p = (uint16_t *)s->io_buffer;
	put_le16(p + 0, 0x0040);
	put_le16(p + 1, s->cylinders);
	put_le16(p + 3, s->heads);
	put_le16(p + 4, 512 * s->sectors); /* XXX: retired, remove ? */
	put_le16(p + 5, 512); /* XXX: retired, remove ? */
	put_le16(p + 6, s->sectors);
	snprintf(buf, sizeof(buf), "QM%05d", s->drive_serial);
	padstr((char *)(p + 10), buf, 20); /* serial number */
	put_le16(p + 20, 3); /* XXX: retired, remove ? */
	put_le16(p + 21, 512); /* cache size in sectors */
	put_le16(p + 22, 4); /* ecc bytes */
	padstr((char *)(p + 23), FW_VERSION, 8); /* firmware version */
	/* Use the same convention for the name as SCSI disks are using: The
	 * first 8 characters should be the vendor, i.e. use 2 spaces here */
	snprintf(buf, sizeof(buf), "Hatari  IDE disk %liM",
	         (long)(s->nb_sectors / (1024 * 1024 / s->bs->sector_size)));
	padstr((char *)(p + 27), buf, 40);
#if MAX_MULT_SECTORS > 1
	put_le16(p + 47, 0x8000 | MAX_MULT_SECTORS);
#endif
	put_le16(p + 48, 1); /* dword I/O */
	put_le16(p + 49, (1 << 11) | (1 << 9) | (1 << 8)); /* DMA and LBA supported */
	put_le16(p + 51, 0x200); /* PIO transfer cycle */
	put_le16(p + 52, 0x200); /* DMA transfer cycle */
	put_le16(p + 53, 1 | (1 << 1) | (1 << 2)); /* words 54-58,64-70,88 are valid */
	put_le16(p + 54, s->cylinders);
	put_le16(p + 55, s->heads);
	put_le16(p + 56, s->sectors);
	oldsize = s->cylinders * s->heads * s->sectors;
	put_le16(p + 57, oldsize);
	put_le16(p + 58, oldsize >> 16);
	if (s->mult_sectors)
		put_le16(p + 59, 0x100 | s->mult_sectors);

	nb_sectors_lba28 = s->nb_sectors;
	if (nb_sectors_lba28 >= 1 << 28) {
		nb_sectors_lba28 = (1 << 28) - 1;
	}
	put_le16(p + 60, nb_sectors_lba28);
	put_le16(p + 61, nb_sectors_lba28 >> 16);

	put_le16(p + 63, 0x07); /* mdma0-2 supported */
	put_le16(p + 65, 120);
	put_le16(p + 66, 120);
	put_le16(p + 67, 120);
	put_le16(p + 68, 120);
	put_le16(p + 80, 0xf0); /* ata3 -> ata6 supported */
	put_le16(p + 81, 0x16); /* conforms to ata5 */
	put_le16(p + 82, (1 << 14));
	/* 13=flush_cache_ext,12=flush_cache,10=lba48 */
	put_le16(p + 83, (1 << 14) | (1 << 13) | (1 <<12) | (1 << 10));
	put_le16(p + 84, (1 << 14));
	put_le16(p + 85, (1 << 14));
	/* 13=flush_cache_ext,12=flush_cache,10=lba48 */
	put_le16(p + 86, (1 << 14) | (1 << 13) | (1 <<12) | (1 << 10));
	put_le16(p + 87, (1 << 14));
	put_le16(p + 88, 0x3f | (1 << 13)); /* udma5 set and supported */
	put_le16(p + 93, 1 | (1 << 14) | 0x2000);
	/* LBA-48 sector count */
	put_le16(p + 100, s->nb_sectors);
	put_le16(p + 101, s->nb_sectors >> 16);
	put_le16(p + 102, s->nb_sectors >> 32);
	put_le16(p + 103, s->nb_sectors >> 48);
	/* ratio logical/physical: 0, logicalSectorSizeSupported */
	put_le16(p + 106, 1 << 12);
	/* words per logical sector */
	put_le16(p + 117, s->bs->sector_size >> 1);
	put_le16(p + 118, s->bs->sector_size >> 17);

	memcpy(s->identify_data, p, sizeof(s->identify_data));
	s->identify_set = 1;
}

static void ide_atapi_identify(IDEState *s)
{
	uint16_t *p;
	char buf[20];

	if (s->identify_set)
	{
		memcpy(s->io_buffer, s->identify_data, sizeof(s->identify_data));
		return;
	}

	memset(s->io_buffer, 0, 512);
	p = (uint16_t *)s->io_buffer;
	/* Removable CDROM, 50us response, 12 byte packets */
	put_le16(p + 0, (2 << 14) | (5 << 8) | (1 << 7) | (2 << 5) | (0 << 0));
	snprintf(buf, sizeof(buf), "QM%05d", s->drive_serial);
	padstr((char *)(p + 10), buf, 20); /* serial number */
	put_le16(p + 20, 3); /* buffer type */
	put_le16(p + 21, 512); /* cache size in sectors */
	put_le16(p + 22, 4); /* ecc bytes */
	padstr((char *)(p + 23), FW_VERSION, 8); /* firmware version */
	padstr((char *)(p + 27), "Hatari CD-ROM", 40); /* model */
	put_le16(p + 48, 1); /* dword I/O (XXX: should not be set on CDROM) */
#ifdef USE_DMA_CDROM
	put_le16(p + 49, 1 << 9 | 1 << 8); /* DMA and LBA supported */
	put_le16(p + 53, 7); /* words 64-70, 54-58, 88 valid */
	put_le16(p + 63, 7);  /* mdma0-2 supported */
	put_le16(p + 64, 0x3f); /* PIO modes supported */
#else
	put_le16(p + 49, 1 << 9); /* LBA supported, no DMA */
	put_le16(p + 53, 3); /* words 64-70, 54-58 valid */
	put_le16(p + 63, 0x103); /* DMA modes XXX: may be incorrect */
	put_le16(p + 64, 1); /* PIO modes */
#endif
	put_le16(p + 65, 0xb4); /* minimum DMA multiword tx cycle time */
	put_le16(p + 66, 0xb4); /* recommended DMA multiword tx cycle time */
	put_le16(p + 67, 0x12c); /* minimum PIO cycle time without flow control */
	put_le16(p + 68, 0xb4); /* minimum PIO cycle time with IORDY flow control */

	put_le16(p + 71, 30); /* in ns */
	put_le16(p + 72, 30); /* in ns */

	put_le16(p + 80, 0x1e); /* support up to ATA/ATAPI-4 */
#ifdef USE_DMA_CDROM
	put_le16(p + 88, 0x3f | (1 << 13)); /* udma5 set and supported */
#endif
	memcpy(s->identify_data, p, sizeof(s->identify_data));
	s->identify_set = 1;
}


static void ide_set_signature(IDEState *s)
{
	s->select &= 0xf0; /* clear head */
	/* put signature */
	s->nsector = 1;
	s->sector = 1;
	if (s->is_cdrom)
	{
		s->lcyl = 0x14;
		s->hcyl = 0xeb;
	}
	else if (s->bs)
	{
		s->lcyl = 0;
		s->hcyl = 0;
	}
	else
	{
		s->lcyl = 0xff;
		s->hcyl = 0xff;
	}
}

static inline void ide_abort_command(IDEState *s)
{
	s->status = READY_STAT | ERR_STAT;
	s->error = ABRT_ERR;
}

static inline void ide_set_irq(IDEState *s)
{
	if (!(s->cmd & IDE_CTRL_DISABLE_IRQ))
	{
		/* Set IRQ (set line to low) */
		MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_FDC_HDC , MFP_GPIP_STATE_LOW );
	}
}

/* prepare data transfer and tell what to do after */
static void ide_transfer_start(IDEState *s, uint8_t *buf, int size,
                               EndTransferFunc *end_transfer_func)
{
	s->end_transfer_func = end_transfer_func;
	s->data_ptr = buf;
	s->data_end = buf + size;
	if (!(s->status & ERR_STAT))
		s->status |= DRQ_STAT;
}

static void ide_transfer_stop(IDEState *s)
{
	s->end_transfer_func = ide_transfer_stop;
	s->data_ptr = s->io_buffer;
	s->data_end = s->io_buffer;
	s->status &= ~DRQ_STAT;
}

static int64_t ide_get_sector(IDEState *s)
{
	int64_t sector_num;
	if (s->select & 0x40)
	{
		/* lba */
		if (!s->lba48)
		{
			sector_num = ((s->select & 0x0f) << 24) | (s->hcyl << 16) |
			             (s->lcyl << 8) | s->sector;
		}
		else
		{
			sector_num = ((int64_t)s->hob_hcyl << 40) |
			             ((int64_t) s->hob_lcyl << 32) |
			             ((int64_t) s->hob_sector << 24) |
			             ((int64_t) s->hcyl << 16) |
			             ((int64_t) s->lcyl << 8) | s->sector;
		}
	}
	else
	{
		sector_num = ((s->hcyl << 8) | s->lcyl) * s->heads * s->sectors +
		             (s->select & 0x0f) * s->sectors + (s->sector - 1);
	}
	return sector_num;
}

static void ide_set_sector(IDEState *s, int64_t sector_num)
{
	unsigned int cyl, r;
	if (s->select & 0x40)
	{
		if (!s->lba48)
		{
			s->select = (s->select & 0xf0) | (sector_num >> 24);
			s->hcyl = (sector_num >> 16);
			s->lcyl = (sector_num >> 8);
			s->sector = (sector_num);
		}
		else
		{
			s->sector = sector_num;
			s->lcyl = sector_num >> 8;
			s->hcyl = sector_num >> 16;
			s->hob_sector = sector_num >> 24;
			s->hob_lcyl = sector_num >> 32;
			s->hob_hcyl = sector_num >> 40;
		}
	}
	else
	{
		cyl = sector_num / (s->heads * s->sectors);
		r = sector_num % (s->heads * s->sectors);
		s->hcyl = cyl >> 8;
		s->lcyl = cyl;
		s->select = (s->select & 0xf0) | ((r / s->sectors) & 0x0f);
		s->sector = (r % s->sectors) + 1;
	}
}

static void ide_sector_read(IDEState *s)
{
	int64_t sector_num;
	int ret, n;

	s->status = READY_STAT | SEEK_STAT;
	s->error = 0; /* not needed by IDE spec, but needed by Windows */
	sector_num = ide_get_sector(s);
	n = s->nsector;
	if (n == 0)
	{
		/* no more sector to read from disk */
		ide_transfer_stop(s);
	}
	else
	{
		LOG_TRACE(TRACE_IDE, "IDE: read sector=%"PRId64"\n", sector_num);

		if (n > s->req_nb_sectors)
			n = s->req_nb_sectors;
		ret = bdrv_read(s->bs, sector_num, s->io_buffer, n);
		if (ret != 0)
		{
			ide_abort_command(s);
			ide_set_irq(s);
			return;
		}
		ide_transfer_start(s, s->io_buffer, s->bs->sector_size * n, ide_sector_read);
		ide_set_irq(s);
		ide_set_sector(s, sector_num + n);
		s->nsector -= n;
	}
}


static void ide_sector_write(IDEState *s)
{
	int64_t sector_num;
	int ret, n, n1;

	s->status = READY_STAT | SEEK_STAT;
	sector_num = ide_get_sector(s);
	LOG_TRACE(TRACE_IDE, "IDE: write sector=%"PRId64"\n", sector_num);

	n = s->nsector;
	if (n > s->req_nb_sectors)
		n = s->req_nb_sectors;
	ret = bdrv_write(s->bs, sector_num, s->io_buffer, n);
	if (ret != 0)
	{
		ide_abort_command(s);
		ide_set_irq(s);
		return;
	}
	s->nsector -= n;
	if (s->nsector == 0)
	{
		/* no more sectors to write */
		ide_transfer_stop(s);
	}
	else
	{
		n1 = s->nsector;
		if (n1 > s->req_nb_sectors)
			n1 = s->req_nb_sectors;
		ide_transfer_start(s, s->io_buffer, s->bs->sector_size * n1, ide_sector_write);
	}
	ide_set_sector(s, sector_num + n);

	ide_set_irq(s);
}


static void ide_atapi_cmd_ok(IDEState *s)
{
	s->error = 0;
	s->status = READY_STAT;
	s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
	ide_set_irq(s);
}

static void ide_atapi_cmd_error(IDEState *s, int sense_key, int asc)
{
	LOG_TRACE(TRACE_IDE, "IDE: ATAPI cmd error sense=0x%x asc=0x%x\n", sense_key, asc);

	s->error = sense_key << 4;
	s->status = READY_STAT | ERR_STAT;
	s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
	s->sense_key = sense_key;
	s->asc = asc;
	ide_set_irq(s);
}

static inline void cpu_to_ube16(uint8_t *buf, int val)
{
	buf[0] = val >> 8;
	buf[1] = val;
}

static inline void cpu_to_ube32(uint8_t *buf, unsigned int val)
{
	buf[0] = val >> 24;
	buf[1] = val >> 16;
	buf[2] = val >> 8;
	buf[3] = val;
}

static inline int ube16_to_cpu(const uint8_t *buf)
{
	return (buf[0] << 8) | buf[1];
}

static inline int ube32_to_cpu(const uint8_t *buf)
{
	return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static void lba_to_msf(uint8_t *buf, int lba)
{
	lba += 150;
	buf[0] = (lba / 75) / 60;
	buf[1] = (lba / 75) % 60;
	buf[2] = lba % 75;
}

static void cd_data_to_raw(uint8_t *buf, int lba)
{
	/* sync bytes */
	buf[0] = 0x00;
	memset(buf + 1, 0xff, 10);
	buf[11] = 0x00;
	buf += 12;
	/* MSF */
	lba_to_msf(buf, lba);
	buf[3] = 0x01; /* mode 1 data */
	buf += 4;
	/* data */
	buf += 2048;
	/* XXX: ECC not computed */
	memset(buf, 0, 288);
}

static int cd_read_sector(BlockDriverState *bs, int lba, uint8_t *buf,
                          int sector_size)
{
	int ret;

	switch (sector_size)
	{
	case 2048:
		ret = bdrv_read(bs, (int64_t)lba << 2, buf, 4);
		break;
	case 2352:
		ret = bdrv_read(bs, (int64_t)lba << 2, buf + 16, 4);
		if (ret < 0)
			return ret;
		cd_data_to_raw(buf, lba);
		break;
	default:
		ret = -EIO;
		break;
	}
	return ret;
}

static void ide_atapi_io_error(IDEState *s, int ret)
{
	/* XXX: handle more errors */
	if (ret == -ENOMEDIUM)
	{
		ide_atapi_cmd_error(s, SENSE_NOT_READY,
		                    ASC_MEDIUM_NOT_PRESENT);
	}
	else
	{
		ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
		                    ASC_LOGICAL_BLOCK_OOR);
	}
}

/* The whole ATAPI transfer logic is handled in this function */
static void ide_atapi_cmd_reply_end(IDEState *s)
{
	int byte_count_limit, size, ret;

	LOG_TRACE(TRACE_IDE, "IDE: ATAPI reply tx_size=%d elem_tx_size=%d index=%d\n",
	       s->packet_transfer_size,
	       s->elementary_transfer_size,
	       s->io_buffer_index);

	if (s->packet_transfer_size <= 0)
	{
		/* end of transfer */
		ide_transfer_stop(s);
		s->status = READY_STAT;
		s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
		ide_set_irq(s);
		LOG_TRACE(TRACE_IDE, "IDE: ATAPI status=0x%x\n", s->status);
	}
	else
	{
		/* see if a new sector must be read */
		if (s->lba != -1 && s->io_buffer_index >= s->cd_sector_size)
		{
			ret = cd_read_sector(s->bs, s->lba, s->io_buffer, s->cd_sector_size);
			if (ret < 0)
			{
				ide_transfer_stop(s);
				ide_atapi_io_error(s, ret);
				return;
			}
			s->lba++;
			s->io_buffer_index = 0;
		}
		if (s->elementary_transfer_size > 0)
		{
			/* there are some data left to transmit in this elementary
			   transfer */
			size = s->cd_sector_size - s->io_buffer_index;
			if (size > s->elementary_transfer_size)
				size = s->elementary_transfer_size;
			ide_transfer_start(s, s->io_buffer + s->io_buffer_index,
			                   size, ide_atapi_cmd_reply_end);
			s->packet_transfer_size -= size;
			s->elementary_transfer_size -= size;
			s->io_buffer_index += size;
		}
		else
		{
			/* a new transfer is needed */
			s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO;
			byte_count_limit = s->lcyl | (s->hcyl << 8);
			LOG_TRACE(TRACE_IDE, "IDE: ATAPI byte_count_limit=%d\n", byte_count_limit);

			if (byte_count_limit == 0xffff)
				byte_count_limit--;
			size = s->packet_transfer_size;
			if (size > byte_count_limit)
			{
				/* byte count limit must be even if this case */
				if (byte_count_limit & 1)
					byte_count_limit--;
				size = byte_count_limit;
			}
			s->lcyl = size;
			s->hcyl = size >> 8;
			s->elementary_transfer_size = size;
			/* we cannot transmit more than one sector at a time */
			if (s->lba != -1)
			{
				if (size > (s->cd_sector_size - s->io_buffer_index))
					size = (s->cd_sector_size - s->io_buffer_index);
			}
			ide_transfer_start(s, s->io_buffer + s->io_buffer_index,
			                   size, ide_atapi_cmd_reply_end);
			s->packet_transfer_size -= size;
			s->elementary_transfer_size -= size;
			s->io_buffer_index += size;
			ide_set_irq(s);

			LOG_TRACE(TRACE_IDE, "IDE: ATAPI status=0x%x\n", s->status);
		}
	}
}

/* send a reply of 'size' bytes in s->io_buffer to an ATAPI command */
static void ide_atapi_cmd_reply(IDEState *s, int size, int max_size)
{
	if (size > max_size)
		size = max_size;
	s->lba = -1; /* no sector read */
	s->packet_transfer_size = size;
	s->io_buffer_size = size;    /* dma: send the reply data as one chunk */
	s->elementary_transfer_size = 0;
	s->io_buffer_index = 0;

	s->status = READY_STAT;
	ide_atapi_cmd_reply_end(s);
}

/* start a CD-CDROM read command */
static void ide_atapi_cmd_read(IDEState *s, int lba, int nb_sectors,
                               int sector_size)
{
	LOG_TRACE(TRACE_IDE, "IDE: ATAPI read pio LBA=%d nb_sectors=%d\n", lba, nb_sectors);

	s->lba = lba;
	s->packet_transfer_size = nb_sectors * sector_size;
	s->elementary_transfer_size = 0;
	s->io_buffer_index = sector_size;
	s->cd_sector_size = sector_size;

	s->status = READY_STAT;
	ide_atapi_cmd_reply_end(s);
}


static void ide_atapi_cmd(IDEState *s)
{
	const uint8_t *packet;
	uint8_t *buf;
	int max_len;

	packet = s->io_buffer;
	buf = s->io_buffer;
	if (LOG_TRACE_LEVEL(TRACE_IDE))
	{
		int i;
		LOG_TRACE_DIRECT_INIT();
		LOG_TRACE_DIRECT("IDE: ATAPI limit=0x%x packet", s->lcyl | (s->hcyl << 8));
		for (i = 0; i < ATAPI_PACKET_SIZE; i++)
		{
			LOG_TRACE_DIRECT(" %02x", packet[i]);
		}
		LOG_TRACE_DIRECT("\n");
		LOG_TRACE_DIRECT_FLUSH();
	}

	switch (s->io_buffer[0])
	{
	case GPCMD_TEST_UNIT_READY:
		if (bdrv_is_inserted(s->bs))
		{
			ide_atapi_cmd_ok(s);
		}
		else
		{
			ide_atapi_cmd_error(s, SENSE_NOT_READY,
			                    ASC_MEDIUM_NOT_PRESENT);
		}
		break;
	case GPCMD_MODE_SENSE_6:
	case GPCMD_MODE_SENSE_10:
	{
		int action, code;
		if (packet[0] == GPCMD_MODE_SENSE_10)
			max_len = ube16_to_cpu(packet + 7);
		else
			max_len = packet[4];
		action = packet[2] >> 6;
		code = packet[2] & 0x3f;
		switch (action)
		{
		case 0: /* current values */
			switch (code)
			{
			case 0x01: /* error recovery */
				cpu_to_ube16(&buf[0], 16 + 6);
				buf[2] = 0x70;
				buf[3] = 0;
				buf[4] = 0;
				buf[5] = 0;
				buf[6] = 0;
				buf[7] = 0;

				buf[8] = 0x01;
				buf[9] = 0x06;
				buf[10] = 0x00;
				buf[11] = 0x05;
				buf[12] = 0x00;
				buf[13] = 0x00;
				buf[14] = 0x00;
				buf[15] = 0x00;
				ide_atapi_cmd_reply(s, 16, max_len);
				break;
			case 0x2a:
				cpu_to_ube16(&buf[0], 28 + 6);
				buf[2] = 0x70;
				buf[3] = 0;
				buf[4] = 0;
				buf[5] = 0;
				buf[6] = 0;
				buf[7] = 0;

				buf[8] = 0x2a;
				buf[9] = 0x12;
				buf[10] = 0x00;
				buf[11] = 0x00;

				buf[12] = 0x70;
				buf[13] = 3 << 5;
				buf[14] = (1 << 0) | (1 << 3) | (1 << 5);
				if (bdrv_is_locked(s->bs))
					buf[6] |= 1 << 1;
				buf[15] = 0x00;
				cpu_to_ube16(&buf[16], 706);
				buf[18] = 0;
				buf[19] = 2;
				cpu_to_ube16(&buf[20], 512);
				cpu_to_ube16(&buf[22], 706);
				buf[24] = 0;
				buf[25] = 0;
				buf[26] = 0;
				buf[27] = 0;
				ide_atapi_cmd_reply(s, 28, max_len);
				break;
			default:
				goto error_cmd;
			}
			break;
		case 1: /* changeable values */
			goto error_cmd;
		case 2: /* default values */
			goto error_cmd;
		default:
		case 3: /* saved values */
			ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
			                    ASC_SAVING_PARAMETERS_NOT_SUPPORTED);
			break;
		}
	}
	break;
	case GPCMD_REQUEST_SENSE:
		max_len = packet[4];
		memset(buf, 0, 18);
		buf[0] = 0x70 | (1 << 7);
		buf[2] = s->sense_key;
		buf[7] = 10;
		buf[12] = s->asc;
		ide_atapi_cmd_reply(s, 18, max_len);
		break;
	case GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
		if (bdrv_is_inserted(s->bs))
		{
			bdrv_set_locked(s->bs, packet[4] & 1);
			ide_atapi_cmd_ok(s);
		}
		else
		{
			ide_atapi_cmd_error(s, SENSE_NOT_READY,
			                    ASC_MEDIUM_NOT_PRESENT);
		}
		break;
	case GPCMD_READ_10:
	case GPCMD_READ_12:
	{
		int nb_sectors, lba;

		if (packet[0] == GPCMD_READ_10)
			nb_sectors = ube16_to_cpu(packet + 7);
		else
			nb_sectors = ube32_to_cpu(packet + 6);
		lba = ube32_to_cpu(packet + 2);
		if (nb_sectors == 0)
		{
			ide_atapi_cmd_ok(s);
			break;
		}
		ide_atapi_cmd_read(s, lba, nb_sectors, 2048);
	}
	break;
	case GPCMD_READ_CD:
	{
		int nb_sectors, lba, transfer_request;

		nb_sectors = (packet[6] << 16) | (packet[7] << 8) | packet[8];
		lba = ube32_to_cpu(packet + 2);
		if (nb_sectors == 0)
		{
			ide_atapi_cmd_ok(s);
			break;
		}
		transfer_request = packet[9];
		switch (transfer_request & 0xf8)
		{
		case 0x00:
			/* nothing */
			ide_atapi_cmd_ok(s);
			break;
		case 0x10:
			/* normal read */
			ide_atapi_cmd_read(s, lba, nb_sectors, 2048);
			break;
		case 0xf8:
			/* read all data */
			ide_atapi_cmd_read(s, lba, nb_sectors, 2352);
			break;
		default:
			ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
			                    ASC_INV_FIELD_IN_CMD_PACKET);
			break;
		}
	}
	break;
	case GPCMD_SEEK:
	{
		unsigned int lba;
		uint64_t total_sectors;

		bdrv_get_geometry(s->bs, &total_sectors);
		if (total_sectors == 0)
		{
			ide_atapi_cmd_error(s, SENSE_NOT_READY,
			                    ASC_MEDIUM_NOT_PRESENT);
			break;
		}
		lba = ube32_to_cpu(packet + 2);
		if (lba >= total_sectors)
		{
			ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
			                    ASC_LOGICAL_BLOCK_OOR);
			break;
		}
		ide_atapi_cmd_ok(s);
	}
	break;
	case GPCMD_START_STOP_UNIT:
	{
		int start, eject;
		start = packet[4] & 1;
		eject = (packet[4] >> 1) & 1;

		if (eject && !start)
		{
			/* eject the disk */
			bdrv_eject(s->bs, 1);
		}
		else if (eject && start)
		{
			/* close the tray */
			bdrv_eject(s->bs, 0);
		}
		ide_atapi_cmd_ok(s);
	}
	break;
	case GPCMD_MECHANISM_STATUS:
	{
		max_len = ube16_to_cpu(packet + 8);
		cpu_to_ube16(buf, 0);
		/* no current LBA */
		buf[2] = 0;
		buf[3] = 0;
		buf[4] = 0;
		buf[5] = 1;
		cpu_to_ube16(buf + 6, 0);
		ide_atapi_cmd_reply(s, 8, max_len);
	}
	break;
	case GPCMD_READ_TOC_PMA_ATIP:
	{
		int format, len;
		// int msf, start_track;
		uint64_t total_sectors;

		bdrv_get_geometry(s->bs, &total_sectors);
		if (total_sectors == 0)
		{
			ide_atapi_cmd_error(s, SENSE_NOT_READY,
			                    ASC_MEDIUM_NOT_PRESENT);
			break;
		}
		max_len = ube16_to_cpu(packet + 7);
		format = packet[9] >> 6;
		// msf = (packet[1] >> 1) & 1;
		// start_track = packet[6];
		switch (format)
		{
		case 0:
			Log_Printf(LOG_ERROR, "IDE FIXME: cdrom_read_toc not implemented");
			len=-1;
			//len = cdrom_read_toc(total_sectors, buf, msf, start_track);
			if (len < 0)
				goto error_cmd;
			ide_atapi_cmd_reply(s, len, max_len);
			break;
		case 1:
			/* multi session : only a single session defined */
			memset(buf, 0, 12);
			buf[1] = 0x0a;
			buf[2] = 0x01;
			buf[3] = 0x01;
			ide_atapi_cmd_reply(s, 12, max_len);
			break;
		case 2:
			Log_Printf(LOG_ERROR, "IDE FIXME: cdrom_read_toc_raw not implemented");
			len=-1;
			//len = cdrom_read_toc_raw(total_sectors, buf, msf, start_track);
			if (len < 0)
				goto error_cmd;
			ide_atapi_cmd_reply(s, len, max_len);
			break;
		default:
error_cmd:
			ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
			                    ASC_INV_FIELD_IN_CMD_PACKET);
			break;
		}
	}
	break;
	case GPCMD_READ_CDVD_CAPACITY:
	{
		uint64_t total_sectors;

		bdrv_get_geometry(s->bs, &total_sectors);
		if (total_sectors == 0)
		{
			ide_atapi_cmd_error(s, SENSE_NOT_READY,
			                    ASC_MEDIUM_NOT_PRESENT);
			break;
		}
		/* NOTE: it is really the number of sectors minus 1 */
		cpu_to_ube32(buf, total_sectors - 1);
		cpu_to_ube32(buf + 4, 2048);
		ide_atapi_cmd_reply(s, 8, 8);
	}
	break;
	case GPCMD_READ_DVD_STRUCTURE:
	{
		int media = packet[1];
		int layer = packet[6];
		int format = packet[2];
		uint64_t total_sectors;

		if (media != 0 || layer != 0)
		{
			ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
			                    ASC_INV_FIELD_IN_CMD_PACKET);
		}

		switch (format)
		{
		case 0:
			bdrv_get_geometry(s->bs, &total_sectors);
			if (total_sectors == 0)
			{
				ide_atapi_cmd_error(s, SENSE_NOT_READY,
				                    ASC_MEDIUM_NOT_PRESENT);
				break;
			}

			memset(buf, 0, 2052);

			buf[4] = 1;   // DVD-ROM, part version 1
			buf[5] = 0xf; // 120mm disc, maximum rate unspecified
			buf[6] = 0;   // one layer, embossed data
			buf[7] = 0;

			cpu_to_ube32(buf + 8, 0);
			cpu_to_ube32(buf + 12, total_sectors - 1);
			cpu_to_ube32(buf + 16, total_sectors - 1);

			cpu_to_be16wu((uint16_t *)buf, 2048 + 4);

			ide_atapi_cmd_reply(s, 2048 + 3, 2048 + 4);
			break;

		default:
			ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
			                    ASC_INV_FIELD_IN_CMD_PACKET);
			break;
		}
	}
	break;
	case GPCMD_SET_SPEED:
		ide_atapi_cmd_ok(s);
		break;
	case GPCMD_INQUIRY:
		max_len = packet[4];
		buf[0] = 0x05; /* CD-ROM */
		buf[1] = 0x80; /* removable */
		buf[2] = 0x00; /* ISO */
		buf[3] = 0x21; /* ATAPI-2 (XXX: put ATAPI-4 ?) */
		buf[4] = 31; /* additional length */
		buf[5] = 0; /* reserved */
		buf[6] = 0; /* reserved */
		buf[7] = 0; /* reserved */
		padstr8(buf + 8, 8, "Hatari");
		padstr8(buf + 16, 16, "CD/DVD-ROM");
		padstr8(buf + 32, 4, FW_VERSION);
		ide_atapi_cmd_reply(s, 36, max_len);
		break;
	case GPCMD_GET_CONFIGURATION:
	{
		uint64_t total_sectors;

		/* only feature 0 is supported */
		if (packet[2] != 0 || packet[3] != 0)
		{
			ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
			                    ASC_INV_FIELD_IN_CMD_PACKET);
			break;
		}
		memset(buf, 0, 32);
		bdrv_get_geometry(s->bs, &total_sectors);
		buf[3] = 16;
		buf[7] = total_sectors <= 1433600 ? 0x08 : 0x10; /* current profile */
		buf[10] = 0x10 | 0x1;
		buf[11] = 0x08; /* size of profile list */
		buf[13] = 0x10; /* DVD-ROM profile */
		buf[14] = buf[7] == 0x10; /* (in)active */
		buf[17] = 0x08; /* CD-ROM profile */
		buf[18] = buf[7] == 0x08; /* (in)active */
		ide_atapi_cmd_reply(s, 32, 32);
		break;
	}
	default:
		ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
		                    ASC_ILLEGAL_OPCODE);
		break;
	}
}


/* called when the inserted state of the media has changed */
static void cdrom_change_cb(void *opaque)
{
	IDEState *s = opaque;
	uint64_t nb_sectors;

	/* XXX: send interrupt too */
	bdrv_get_geometry(s->bs, &nb_sectors);
	s->nb_sectors = nb_sectors;
}

static void ide_cmd_lba48_transform(IDEState *s, int lba48)
{
	s->lba48 = lba48;

	/* handle the 'magic' 0 nsector count conversion here. to avoid
	 * fiddling with the rest of the read logic, we just store the
	 * full sector count in ->nsector and ignore ->hob_nsector from now
	 */
	if (!s->lba48)
	{
		if (!s->nsector)
			s->nsector = 256;
	}
	else
	{
		if (!s->nsector && !s->hob_nsector)
			s->nsector = 65536;
		else
		{
			int lo = s->nsector;
			int hi = s->hob_nsector;

			s->nsector = (hi << 8) | lo;
		}
	}
}

static void ide_clear_hob(IDEState *ide_if)
{
	/* any write clears HOB high bit of device control register */
	ide_if[0].cmd &= ~IDE_CTRL_HOB;
}

/* IOport [W]rite [R]egisters */
enum ATA_IOPORT_WR {
	ATA_IOPORT_WR_DATA = 0,
	ATA_IOPORT_WR_FEATURES = 1,
	ATA_IOPORT_WR_SECTOR_COUNT = 2,
	ATA_IOPORT_WR_SECTOR_NUMBER = 3,
	ATA_IOPORT_WR_CYLINDER_LOW = 4,
	ATA_IOPORT_WR_CYLINDER_HIGH = 5,
	ATA_IOPORT_WR_DEVICE_HEAD = 6,
	ATA_IOPORT_WR_COMMAND = 7,
	ATA_IOPORT_WR_NUM_REGISTERS,
};

const char *ATA_IOPORT_WR_lookup[ATA_IOPORT_WR_NUM_REGISTERS] = {
	[ATA_IOPORT_WR_DATA] = "Data",
	[ATA_IOPORT_WR_FEATURES] = "Features",
	[ATA_IOPORT_WR_SECTOR_COUNT] = "Sector Count",
	[ATA_IOPORT_WR_SECTOR_NUMBER] = "Sector Number",
	[ATA_IOPORT_WR_CYLINDER_LOW] = "Cylinder Low",
	[ATA_IOPORT_WR_CYLINDER_HIGH] = "Cylinder High",
	[ATA_IOPORT_WR_DEVICE_HEAD] = "Device/Head",
	[ATA_IOPORT_WR_COMMAND] = "Command"
};

static void ide_ioport_write(IDEState *ide_if, uint32_t addr, uint32_t val)
{
	IDEState *s;
	int unit, n;
	int lba48 = 0;
	int reg_num = addr & 7;

	LOG_TRACE(TRACE_IDE, "IDE: write addr=0x%x reg='%s' val=0x%02x\n",
	          addr, ATA_IOPORT_WR_lookup[reg_num], val);

	/* NOTE: Device0 and Device1 both receive incoming register writes.
	 * (They're on the same bus! They have to!) */

	switch (reg_num)
	{
	case 0:
		break;
	case ATA_IOPORT_WR_FEATURES:
		ide_clear_hob(ide_if);
		ide_if[0].hob_feature = ide_if[0].feature;
		ide_if[1].hob_feature = ide_if[1].feature;
		ide_if[0].feature = val;
		ide_if[1].feature = val;
		break;
	case ATA_IOPORT_WR_SECTOR_COUNT:
		ide_clear_hob(ide_if);
		ide_if[0].hob_nsector = ide_if[0].nsector;
		ide_if[1].hob_nsector = ide_if[1].nsector;
		ide_if[0].nsector = val;
		ide_if[1].nsector = val;
		break;
	case ATA_IOPORT_WR_SECTOR_NUMBER:
		ide_clear_hob(ide_if);
		ide_if[0].hob_sector = ide_if[0].sector;
		ide_if[1].hob_sector = ide_if[1].sector;
		ide_if[0].sector = val;
		ide_if[1].sector = val;
		break;
	case ATA_IOPORT_WR_CYLINDER_LOW:
		ide_clear_hob(ide_if);
		ide_if[0].hob_lcyl = ide_if[0].lcyl;
		ide_if[1].hob_lcyl = ide_if[1].lcyl;
		ide_if[0].lcyl = val;
		ide_if[1].lcyl = val;
		break;
	case ATA_IOPORT_WR_CYLINDER_HIGH:
		ide_clear_hob(ide_if);
		ide_if[0].hob_hcyl = ide_if[0].hcyl;
		ide_if[1].hob_hcyl = ide_if[1].hcyl;
		ide_if[0].hcyl = val;
		ide_if[1].hcyl = val;
		break;
	case ATA_IOPORT_WR_DEVICE_HEAD:
		ide_clear_hob(ide_if);
		ide_if[0].select = val | 0xa0;
		ide_if[1].select = val | 0xa0;
		/* select drive */
		unit = (val >> 4) & 1;
		s = ide_if + unit;
		ide_if->cur_drive = s;
		break;
	default:
	case ATA_IOPORT_WR_COMMAND:
		ide_clear_hob(ide_if);
		LOG_TRACE(TRACE_IDE, "IDE: CMD=%02x\n", val);

		s = ide_if->cur_drive;
		/* ignore commands to non existent IDE device 1 */
		if (s != ide_if && !s->bs)
		{
			Log_Printf(LOG_INFO, "IDE: Tried to send command to "
			           "non-existent IDE device #1!\n");
			break;
		}

		switch (val)
		{
		case WIN_IDENTIFY:
			if (s->bs && !s->is_cdrom)
			{
				ide_identify(s);
				s->status = READY_STAT | SEEK_STAT;
				ide_transfer_start(s, s->io_buffer, 512, ide_transfer_stop);
			}
			else
			{
				if (s->is_cdrom)
				{
					ide_set_signature(s);
				}
				ide_abort_command(s);
			}
			ide_set_irq(s);
			break;
		case WIN_SPECIFY:
		case WIN_RECAL:
			s->error = 0;
			s->status = READY_STAT | SEEK_STAT;
			ide_set_irq(s);
			break;
		case WIN_SETMULT:
			if ((s->nsector & 0xff) != 0 &&
			    ((s->nsector & 0xff) > MAX_MULT_SECTORS ||
			     (s->nsector & (s->nsector - 1)) != 0))
			{
				ide_abort_command(s);
			}
			else
			{
				s->mult_sectors = s->nsector & 0xff;
				s->status = READY_STAT;
			}
			ide_set_irq(s);
			break;
		case WIN_VERIFY_EXT:
			lba48 = 1;
			/* fall through */
		case WIN_VERIFY:
		case WIN_VERIFY_ONCE:
			/* do sector number check ? */
			ide_cmd_lba48_transform(s, lba48);
			s->status = READY_STAT;
			ide_set_irq(s);
			break;
		case WIN_FORMAT:
			ide_cmd_lba48_transform(s, lba48);
			s->error = 0;
			s->status = READY_STAT | SEEK_STAT;
			s->req_nb_sectors = s->mult_sectors;
			n = s->nsector;
			if (n > s->req_nb_sectors)
				n = s->req_nb_sectors;
			ide_transfer_start(s, s->io_buffer, s->bs->sector_size * n, ide_sector_write);
			s->media_changed = 1;
			break;
		case WIN_READ_EXT:
			lba48 = 1;
			/* fall through */
		case WIN_READ:
		case WIN_READ_ONCE:
			if (!s->bs)
				goto abort_cmd;
			ide_cmd_lba48_transform(s, lba48);
			s->req_nb_sectors = 1;
			ide_sector_read(s);
			break;
		case WIN_WRITE_EXT:
			lba48 = 1;
			/* fall through */
		case WIN_WRITE:
		case WIN_WRITE_ONCE:
		case CFA_WRITE_SECT_WO_ERASE:
		case WIN_WRITE_VERIFY:
			ide_cmd_lba48_transform(s, lba48);
			s->error = 0;
			s->status = SEEK_STAT | READY_STAT;
			s->req_nb_sectors = 1;
			ide_transfer_start(s, s->io_buffer, s->bs->sector_size, ide_sector_write);
			s->media_changed = 1;
			break;
		case WIN_MULTREAD_EXT:
			lba48 = 1;
			/* fall through */
		case WIN_MULTREAD:
			if (!s->mult_sectors)
				goto abort_cmd;
			ide_cmd_lba48_transform(s, lba48);
			s->req_nb_sectors = s->mult_sectors;
			ide_sector_read(s);
			break;
		case WIN_MULTWRITE_EXT:
			lba48 = 1;
			/* fall through */
		case WIN_MULTWRITE:
		case CFA_WRITE_MULTI_WO_ERASE:
			if (!s->mult_sectors)
				goto abort_cmd;
			ide_cmd_lba48_transform(s, lba48);
			s->error = 0;
			s->status = SEEK_STAT | READY_STAT;
			s->req_nb_sectors = s->mult_sectors;
			n = s->nsector;
			if (n > s->req_nb_sectors)
				n = s->req_nb_sectors;
			ide_transfer_start(s, s->io_buffer, s->bs->sector_size * n, ide_sector_write);
			s->media_changed = 1;
			break;
		case WIN_READDMA_EXT:
			lba48 = 1;
			/* fall through */
		case WIN_READDMA:
		case WIN_READDMA_ONCE:
			if (!s->bs)
				goto abort_cmd;
			ide_cmd_lba48_transform(s, lba48);
			// ide_sector_read_dma(s);
			Log_Printf(LOG_ERROR, "IDE: DMA read not supported!\n");
			break;
		case WIN_WRITEDMA_EXT:
			lba48 = 1;
			/* fall through */
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_ONCE:
			if (!s->bs)
				goto abort_cmd;
			ide_cmd_lba48_transform(s, lba48);
			// ide_sector_write_dma(s);
			Log_Printf(LOG_ERROR, "IDE: DMA write not supported!\n");
			s->media_changed = 1;
			break;
		case WIN_READ_NATIVE_MAX_EXT:
			lba48 = 1;
			/* fall through */
		case WIN_READ_NATIVE_MAX:
			ide_cmd_lba48_transform(s, lba48);
			ide_set_sector(s, s->nb_sectors - 1);
			s->status = READY_STAT;
			ide_set_irq(s);
			break;
		case WIN_CHECKPOWERMODE1:
		case WIN_CHECKPOWERMODE2:
			s->nsector = 0xff; /* device active or idle */
			s->status = READY_STAT;
			ide_set_irq(s);
			break;
		case WIN_SETFEATURES:
			if (!s->bs)
				goto abort_cmd;
			/* XXX: valid for CDROM ? */
			switch (s->feature)
			{
			case 0xcc: /* reverting to power-on defaults enable */
			case 0x66: /* reverting to power-on defaults disable */
			case 0x02: /* write cache enable */
			case 0x82: /* write cache disable */
			case 0xaa: /* read look-ahead enable */
			case 0x55: /* read look-ahead disable */
			case 0x05: /* set advanced power management mode */
			case 0x85: /* disable advanced power management mode */
			case 0x69: /* NOP */
			case 0x67: /* NOP */
			case 0x96: /* NOP */
			case 0x9a: /* NOP */
			case 0x42: /* enable Automatic Acoustic Mode */
			case 0xc2: /* disable Automatic Acoustic Mode */
				s->status = READY_STAT | SEEK_STAT;
				ide_set_irq(s);
				break;
			case 0x03:   /* set transfer mode */
			{
				uint8_t mval = s->nsector & 0x07;

				switch (s->nsector >> 3)
				{
				case 0x00: /* pio default */
				case 0x01: /* pio mode */
					put_le16(s->identify_data + 63,0x07);
					put_le16(s->identify_data + 88,0x3f);
					break;
				case 0x04: /* mdma mode */
					put_le16(s->identify_data + 63,0x07 | (1 << (mval + 8)));
					put_le16(s->identify_data + 88,0x3f);
					break;
				case 0x08: /* udma mode */
					put_le16(s->identify_data + 63,0x07);
					put_le16(s->identify_data + 88,0x3f | (1 << (mval + 8)));
					break;
				default:
					goto abort_cmd;
				}
				s->status = READY_STAT | SEEK_STAT;
				ide_set_irq(s);
				break;
			}
			default:
				goto abort_cmd;
			}
			break;
		case WIN_FLUSH_CACHE:
		case WIN_FLUSH_CACHE_EXT:
			if (s->bs)
				bdrv_flush(s->bs);
			s->status = READY_STAT;
			ide_set_irq(s);
			break;
		case WIN_STANDBY:
		case WIN_STANDBY2:
		case WIN_STANDBYNOW1:
		case WIN_STANDBYNOW2:
		case WIN_IDLEIMMEDIATE:
		case CFA_IDLEIMMEDIATE:
		case WIN_SETIDLE1:
		case WIN_SETIDLE2:
		case WIN_SLEEPNOW1:
		case WIN_SLEEPNOW2:
			s->status = READY_STAT;
			ide_set_irq(s);
			break;
			/* ATAPI commands */
		case WIN_PIDENTIFY:
			if (s->is_cdrom)
			{
				ide_atapi_identify(s);
				s->status = READY_STAT | SEEK_STAT;
				ide_transfer_start(s, s->io_buffer, 512, ide_transfer_stop);
			}
			else
			{
				ide_abort_command(s);
			}
			ide_set_irq(s);
			break;
		case WIN_DIAGNOSE:
			ide_set_signature(s);
			s->status = 0x00; /* NOTE: READY is _not_ set */
			s->error = 0x01;
			ide_set_irq(s);
			break;
		case WIN_SRST:
			if (!s->is_cdrom)
				goto abort_cmd;
			ide_set_signature(s);
			s->status = 0x00; /* NOTE: READY is _not_ set */
			s->error = 0x01;
			break;
		case WIN_PACKETCMD:
			if (!s->is_cdrom)
				goto abort_cmd;
			/* overlapping commands not supported */
			if (s->feature & 0x02)
				goto abort_cmd;
			s->status = READY_STAT;
			// s->atapi_dma = s->feature & 1;
			s->nsector = 1;
			ide_transfer_start(s, s->io_buffer, ATAPI_PACKET_SIZE,
			                   ide_atapi_cmd);
			break;
		default:
		abort_cmd:
			ide_abort_command(s);
			ide_set_irq(s);
			break;
		}
	}
}

/* IOport [R]ead [R]egisters */
enum ATA_IOPORT_RR {
	ATA_IOPORT_RR_DATA = 0,
	ATA_IOPORT_RR_ERROR = 1,
	ATA_IOPORT_RR_SECTOR_COUNT = 2,
	ATA_IOPORT_RR_SECTOR_NUMBER = 3,
	ATA_IOPORT_RR_CYLINDER_LOW = 4,
	ATA_IOPORT_RR_CYLINDER_HIGH = 5,
	ATA_IOPORT_RR_DEVICE_HEAD = 6,
	ATA_IOPORT_RR_STATUS = 7,
	ATA_IOPORT_RR_NUM_REGISTERS,
};

const char *ATA_IOPORT_RR_lookup[ATA_IOPORT_RR_NUM_REGISTERS] = {
	[ATA_IOPORT_RR_DATA] = "Data",
	[ATA_IOPORT_RR_ERROR] = "Error",
	[ATA_IOPORT_RR_SECTOR_COUNT] = "Sector Count",
	[ATA_IOPORT_RR_SECTOR_NUMBER] = "Sector Number",
	[ATA_IOPORT_RR_CYLINDER_LOW] = "Cylinder Low",
	[ATA_IOPORT_RR_CYLINDER_HIGH] = "Cylinder High",
	[ATA_IOPORT_RR_DEVICE_HEAD] = "Device/Head",
	[ATA_IOPORT_RR_STATUS] = "Status"
};

static uint32_t ide_ioport_read(IDEState *ide_if, uint32_t addr)
{
	IDEState *s = ide_if->cur_drive;
	uint32_t reg_num;
	int ret, hob;

	reg_num = addr & 7;
	hob = ide_if[0].cmd & IDE_CTRL_HOB;

	switch (reg_num)
	{
	case ATA_IOPORT_RR_DATA:
		ret = 0xff;
		break;
	case ATA_IOPORT_RR_ERROR:
		if (!ide_if[0].bs && !ide_if[1].bs)
			ret = 0;
		else if (!hob)
			ret = s->error;
		else
			ret = s->hob_feature;
		break;
	case ATA_IOPORT_RR_SECTOR_COUNT:
		if (!ide_if[0].bs && !ide_if[1].bs)
			ret = 0;
		else if (!hob)
			ret = s->nsector & 0xff;
		else
			ret = s->hob_nsector;
		break;
	case ATA_IOPORT_RR_SECTOR_NUMBER:
		if (!ide_if[0].bs && !ide_if[1].bs)
			ret = 0;
		else if (!hob)
			ret = s->sector;
		else
			ret = s->hob_sector;
		break;
	case ATA_IOPORT_RR_CYLINDER_LOW:
		if (!ide_if[0].bs && !ide_if[1].bs)
			ret = 0;
		else if (!hob)
			ret = s->lcyl;
		else
			ret = s->hob_lcyl;
		break;
	case ATA_IOPORT_RR_CYLINDER_HIGH:
		if (!ide_if[0].bs && !ide_if[1].bs)
			ret = 0;
		else if (!hob)
			ret = s->hcyl;
		else
			ret = s->hob_hcyl;
		break;
	case ATA_IOPORT_RR_DEVICE_HEAD:
		if (!ide_if[0].bs && !ide_if[1].bs)
			ret = 0;
		else
			ret = s->select;
		break;
	default:
	case ATA_IOPORT_RR_STATUS:
		if ((!ide_if[0].bs && !ide_if[1].bs) ||
		        (s != ide_if && !s->bs))
			ret = 0;
		else
			ret = s->status;

		/* Clear IRQ (set line to high) */
		MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_FDC_HDC , MFP_GPIP_STATE_HIGH );
		break;
	}
	LOG_TRACE(TRACE_IDE, "IDE: read addr=0x%x reg='%s' val=%02x\n",
	          addr, ATA_IOPORT_RR_lookup[reg_num], ret);
	return ret;
}

static uint32_t ide_status_read(IDEState *ide_if, uint32_t addr)
{
	IDEState *s = ide_if->cur_drive;
	int ret;

	if ((!ide_if[0].bs && !ide_if[1].bs) ||
	        (s != ide_if && !s->bs))
		ret = 0;
	else
		ret = s->status;

	LOG_TRACE(TRACE_IDE, "IDE: read status addr=0x%x val=%02x\n", addr, ret);
	return ret;
}

static void ide_ctrl_write(IDEState *ide_if, uint32_t addr, uint32_t val)
{
	IDEState *s;
	int i;

	LOG_TRACE(TRACE_IDE, "IDE: write control addr=0x%x val=%02x\n", addr, val);

	/* common for both drives */
	if (!(ide_if[0].cmd & IDE_CTRL_RESET) &&
	        (val & IDE_CTRL_RESET))
	{
		/* reset low to high */
		for (i = 0;i < 2; i++)
		{
			s = &ide_if[i];
			s->status = BUSY_STAT | SEEK_STAT;
			s->error = 0x01;
		}
	}
	else if ((ide_if[0].cmd & IDE_CTRL_RESET) &&
	         !(val & IDE_CTRL_RESET))
	{
		/* high to low */
		for (i = 0;i < 2; i++)
		{
			s = &ide_if[i];
			if (s->is_cdrom)
				s->status = 0x00; /* NOTE: READY is _not_ set */
			else
				s->status = READY_STAT | SEEK_STAT;
			ide_set_signature(s);
		}
	}

	ide_if[0].cmd = val;
	ide_if[1].cmd = val;
}

static void ide_data_writew(IDEState *ide_if, uint32_t addr, uint32_t val)
{
	IDEState *s = ide_if->cur_drive;
	uint8_t *p;

	if (!s->data_ptr || s->data_ptr > s->data_end)
		return;

	p = s->data_ptr;
	*(uint16_t *)p = le16_to_cpu(val);
	p += 2;
	s->data_ptr = p;
	if (p >= s->data_end)
		s->end_transfer_func(s);
}

static uint32_t ide_data_readw(IDEState *ide_if, uint32_t addr)
{
	IDEState *s = ide_if->cur_drive;
	uint8_t *p;
	int ret;

	if (!s->data_ptr || s->data_ptr > s->data_end)
		return 0xffff;

	p = s->data_ptr;
	ret = cpu_to_le16(*(uint16_t *)p);
	p += 2;
	s->data_ptr = p;
	if (p >= s->data_end)
		s->end_transfer_func(s);
	return ret;
}

static void ide_data_writel(IDEState *ide_if, uint32_t addr, uint32_t val)
{
	IDEState *s = ide_if->cur_drive;
	uint8_t *p;

	if (!s->data_ptr || s->data_ptr > s->data_end)
		return;

	p = s->data_ptr;
	*(uint32_t *)p = le32_to_cpu(val);
	p += 4;
	s->data_ptr = p;
	if (p >= s->data_end)
		s->end_transfer_func(s);
}

static uint32_t ide_data_readl(IDEState *ide_if, uint32_t addr)
{
	IDEState *s = ide_if->cur_drive;
	uint8_t *p;
	uint32_t ret;

	if (!s->data_ptr || s->data_ptr > s->data_end)
		return 0xffffffff;

	p = s->data_ptr;
	ret = cpu_to_le32(*(uint32_t *)p);
	p += 4;
	s->data_ptr = p;
	if (p >= s->data_end)
		s->end_transfer_func(s);
	return ret;
}

static void ide_dummy_transfer_stop(IDEState *s)
{
	s->data_ptr = s->io_buffer;
	s->data_end = s->io_buffer;
	s->io_buffer[0] = 0xff;
	s->io_buffer[1] = 0xff;
	s->io_buffer[2] = 0xff;
	s->io_buffer[3] = 0xff;
}

static void ide_reset(IDEState *s)
{
	s->mult_sectors = MAX_MULT_SECTORS;
	s->cur_drive = s;
	s->select = 0xa0;
	s->status = READY_STAT | SEEK_STAT;

	ide_set_signature(s);
	/* init the transfer handler so that 0xffff is returned on data
	   accesses */
	s->end_transfer_func = ide_dummy_transfer_stop;
	ide_dummy_transfer_stop(s);
	s->media_changed = 0;
}

struct partition
{
	uint8_t boot_ind;		/* 0x80 - active */
	uint8_t head;		/* starting head */
	uint8_t sector;		/* starting sector */
	uint8_t cyl;		/* starting cylinder */
	uint8_t sys_ind;		/* What partition type */
	uint8_t end_head;		/* end head */
	uint8_t end_sector;	/* end sector */
	uint8_t end_cyl;		/* end cylinder */
	uint32_t start_sect;	/* starting sector counting from 0 */
	uint32_t nr_sects;		/* nr of sectors in partition */
};

/* try to guess the disk logical geometry from the MSDOS partition table. Return 0 if OK, -1 if could not guess */
static int guess_disk_lchs(IDEState *s,
                           int *pcylinders, int *pheads, int *psectors)
{
	uint8_t *buf;
	int ret, i, heads, sectors, cylinders;
	struct partition *p;
	uint32_t nr_sects;

	buf = malloc(MAX_SECTOR_SIZE);
	if (buf == NULL)
		return -1;
	ret = bdrv_read(s->bs, 0, buf, 1);
	if (ret < 0)
	{
		free(buf);
		return -1;
	}
	/* test msdos magic */
	if (buf[510] != 0x55 || buf[511] != 0xaa)
	{
		free(buf);
		return -1;
	}
	for (i = 0; i < 4; i++)
	{
		p = ((struct partition *)(buf + 0x1be)) + i;
		nr_sects = le32_to_cpu(p->nr_sects);
		if (nr_sects && p->end_head)
		{
			/* We make the assumption that the partition terminates on
			   a cylinder boundary */
			heads = p->end_head + 1;
			sectors = p->end_sector & 63;
			if (sectors == 0)
				continue;
			cylinders = s->nb_sectors / (heads * sectors);
			if (cylinders < 1 || cylinders > 16383)
				continue;
			*pheads = heads;
			*psectors = sectors;
			*pcylinders = cylinders;
			free(buf);
			return 0;
		}
	}
	free(buf);
	return -1;
}

static void ide_init_one(IDEState *s, BlockDriverState *bds)
{
	static int drive_serial = 1;
	int cylinders, heads, secs, translation, lba_detected = 0;
	uint64_t nb_sectors;

	s->io_buffer = malloc(MAX_MULT_SECTORS * MAX_SECTOR_SIZE + 4);
	assert(s->io_buffer);
	s->bs = bds;

	bdrv_get_geometry(s->bs, &nb_sectors);
	s->nb_sectors = nb_sectors;
	/* if a geometry hint is available, use it */
	bdrv_get_geometry_hint(s->bs, &cylinders, &heads, &secs);
	translation = bdrv_get_translation_hint(s->bs);
	if (cylinders != 0)
	{
		s->cylinders = cylinders;
		s->heads = heads;
		s->sectors = secs;
	}
	else
	{
		if (guess_disk_lchs(s, &cylinders, &heads, &secs) == 0)
		{
			if (heads > 16)
			{
				/* if heads > 16, it means that a BIOS LBA
				   translation was active, so the default
				   hardware geometry is OK */
				lba_detected = 1;
				goto default_geometry;
			}
			else
			{
				s->cylinders = cylinders;
				s->heads = heads;
				s->sectors = secs;
				/* disable any translation to be in sync with
				   the logical geometry */
				if (translation == BIOS_ATA_TRANSLATION_AUTO)
				{
					bdrv_set_translation_hint(s->bs,
					                          BIOS_ATA_TRANSLATION_NONE);
				}
			}
		}
		else
		{
default_geometry:
			/* if no geometry, use a standard physical disk geometry */
			cylinders = nb_sectors / (16 * 63);
			if (cylinders > 16383)
				cylinders = 16383;
			else if (cylinders < 2)
				cylinders = 2;
			s->cylinders = cylinders;
			s->heads = 16;
			s->sectors = 63;
			if (lba_detected == 1 && translation == BIOS_ATA_TRANSLATION_AUTO)
			{
				if ((s->cylinders * s->heads) <= 131072)
				{
					bdrv_set_translation_hint(s->bs,
					                          BIOS_ATA_TRANSLATION_LARGE);
				}
				else
				{
					bdrv_set_translation_hint(s->bs,
					                          BIOS_ATA_TRANSLATION_LBA);
				}
			}
		}
		bdrv_set_geometry_hint(s->bs, s->cylinders, s->heads, s->sectors);
	}
	LOG_TRACE(TRACE_IDE, "IDE: using geometry LCHS=%d %d %d\n",
	          s->cylinders, s->heads, s->sectors);
	if (bdrv_get_type_hint(s->bs) == BDRV_TYPE_CDROM)
	{
		s->is_cdrom = 1;
		bdrv_set_change_cb(s->bs, cdrom_change_cb, s);
	}

	s->drive_serial = drive_serial++;

	ide_reset(s);
}


/*----------------------------------------------------------------------------*/


static BlockDriverState *hd_table[2];


/**
 * Initialize the IDE subsystem
 */
void Ide_Init(void)
{
	int i;

	if (!Ide_IsAvailable() )
		return;

	memset(ide_state, 0, sizeof(ide_state));

	for (i = 0; i < 2; i++)
	{
		hd_table[i] = malloc(sizeof(BlockDriverState));
		assert(hd_table[i]);
		memset(hd_table[i], 0, sizeof(BlockDriverState));
		ide_state[i].cur_drive = &ide_state[i];
		if (ConfigureParams.Ide[i].bUseDevice)
		{
			int is_byteswap;
			if (bdrv_open(hd_table[i], ConfigureParams.Ide[i].sDeviceFile, ConfigureParams.Ide[i].nBlockSize, 0) < 0)
			{
				ConfigureParams.Ide[i].bUseDevice = false;
				continue;
			}
			nIDEPartitions += HDC_PartitionCount(hd_table[i]->fhndl, TRACE_IDE, &is_byteswap);
			/* Our IDE implementation is little endian by default,
			 * so we need to byteswap if the image is not swapped! */
			if (ConfigureParams.Ide[i].nByteSwap == BYTESWAP_AUTO)
				hd_table[i]->byteswap = !is_byteswap;
			else
				hd_table[i]->byteswap = !ConfigureParams.Ide[i].nByteSwap;
			LOG_TRACE(TRACE_IDE, "IDE: little->big endian byte-swapping %s for drive %d\n",
				  hd_table[i]->byteswap ? "enabled" : "disabled", i);
			hd_table[i]->sector_size = ConfigureParams.Ide[i].nBlockSize;
			hd_table[i]->type = ConfigureParams.Ide[i].nDeviceType;
			ide_init_one(&ide_state[i], hd_table[i]);
		}
	}
}


/**
 * Free resources from the IDE subsystem
 */
void Ide_UnInit(void)
{
	int i;

	for (i = 0; i < 2; i++)
	{
		if (hd_table[i])
		{
			if (bdrv_is_inserted(hd_table[i]))
			{
				bdrv_close(hd_table[i]);
			}
			free(hd_table[i]);
			hd_table[i] = NULL;
		}
	}

	for (i = 0; i < 2; i++)
	{
		if (ide_state[i].io_buffer)
		{
			free(ide_state[i].io_buffer);
			ide_state[i].io_buffer = NULL;
		}
	}

	nIDEPartitions = 0;
}
