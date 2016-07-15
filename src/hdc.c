/*
  Hatari - hdc.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Low-level hard drive emulation
*/
const char HDC_fileid[] = "Hatari hdc.c : " __DATE__ " " __TIME__;

#include <errno.h>

#include "main.h"
#include "configuration.h"
#include "debugui.h"
#include "file.h"
#include "fdc.h"
#include "hdc.h"
#include "ioMem.h"
#include "log.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "stMemory.h"
#include "tos.h"
#include "statusbar.h"


/*
  ACSI emulation: 
  ACSI commands are six byte-packets sent to the
  hard drive controller (which is on the HD unit, not in the ST)

  While the hard drive is busy, DRQ is high, polling the DRQ during
  operation interrupts the current operation. The DRQ status can
  be polled non-destructively in GPIP.

  (For simplicity, the operation is finished immediately,
  this is a potential bug, but I doubt it is significant,
  we just appear to have a very fast hard drive.)

  The ACSI command set is a subset of the SCSI standard.
  (for details, see the X3T9.2 SCSI draft documents
  from 1985, for an example of writing ACSI commands,
  see the TOS DMA boot code) 
*/

// #define DISALLOW_HDC_WRITE
// #define HDC_VERBOSE           /* display operations */
#define WITH_NCR5380 0

#define HDC_ReadInt16(a, i) (((unsigned) a[i] << 8) | a[i + 1])
#define HDC_ReadInt24(a, i) (((unsigned) a[i] << 16) | ((unsigned) a[i + 1] << 8) | a[i + 2])
#define HDC_ReadInt32(a, i) (((unsigned) a[i] << 24) | ((unsigned) a[i + 1] << 16) | ((unsigned) a[i + 2] << 8) | a[i + 3])

/**
 * Information about a ACSI/SCSI drive
 */
typedef struct {
	bool enabled;
	FILE *image_file;
	Uint32 nLastBlockAddr;      /* The specified sector number */
	bool bSetLastBlockAddr;
	Uint8 nLastError;
	unsigned long hdSize;       /* Size of the hard disk in sectors */
} SCSI_DEV;

/**
 * Status of the ACSI/SCSI bus/controller including the current command block.
 */
typedef struct {
	int target;
	int byteCount;              /* number of command bytes received */
	Uint8 command[16];
	Uint8 opcode;
	bool bDmaError;
	short int returnCode;       /* return code from the HDC operation */
	Uint8 *resp;                /* Response buffer */
	int respbufsize;
	int respcnt;
	int respidx;
	SCSI_DEV devs[8];
} SCSI_CTRLR;

/* HDC globals */
static SCSI_CTRLR AcsiBus;
int nAcsiPartitions = 0;
bool bAcsiEmuOn = false;

#if WITH_NCR5380
static SCSI_CTRLR ScsiBus;
#endif

/* Our dummy INQUIRY response data */
static unsigned char inquiry_bytes[] =
{
	0,                /* device type 0 = direct access device */
	0,                /* device type qualifier (nonremovable) */
	1,                /* ACSI/SCSI version */
	0,                /* reserved */
	31,               /* length of the following data */
	0, 0, 0,          /* Vendor specific data */
	'H','a','t','a','r','i',' ',' ',    /* Vendor ID */
	'E','m','u','l','a','t','e','d',    /* Product ID 1 */
	'H','a','r','d','d','i','s','k',    /* Product ID 2 */
	'0','1','8','0',                    /* Revision */
};


/*---------------------------------------------------------------------*/
/**
 * Return the LUN (logical unit number) specified in the current
 * ACSI/SCSI command block.
 */
static unsigned char HDC_GetLUN(SCSI_CTRLR *ctr)
{
	return (ctr->command[1] & 0xE0) >> 5;
}

/**
 * Return the start sector (logical block address) specified in the
 * current ACSI/SCSI command block.
 */
static unsigned long HDC_GetLBA(SCSI_CTRLR *ctr)
{
	/* offset = logical block address * 512 */
	if (ctr->opcode < 0x20)				/* Class 0? */
		return HDC_ReadInt24(ctr->command, 1) & 0x1FFFFF;
	else
		return HDC_ReadInt32(ctr->command, 2);	/* Class 1 */
}

/**
 * Return the count specified in the current ACSI command block.
 */
static int HDC_GetCount(SCSI_CTRLR *ctr)
{
	if (ctr->opcode < 0x20)
		return ctr->command[4];			/* Class 0 */
	else
		return HDC_ReadInt16(ctr->command, 7);	/* Class 1 */
}

/**
 * Return the control byte specified in the current ACSI command block.
 */
static inline Uint8 HDC_GetControl(SCSI_CTRLR *ctr)
{
	if (ctr->opcode < 0x20)
		return ctr->command[5];			/* Class 0 */
	else
		return ctr->command[9];			/* Class 1 */
}

/**
 * Get pointer to response buffer, set up size indicator - and allocate
 * a new buffer if it is not big enough
 */
static Uint8 *HDC_PrepRespBuf(SCSI_CTRLR *ctr, int size)
{
	ctr->respcnt = size;
	ctr->respidx = 0;

	if (size > ctr->respbufsize)
	{
		ctr->respbufsize = size;
		ctr->resp = realloc(ctr->resp, size);
	}

	return ctr->resp;
}

/**
 * Get info string for SCSI/ACSI command packets.
 */
static inline char *HDC_CmdInfoStr(SCSI_CTRLR *ctr)
{
	static char str[80];

	snprintf(str, sizeof(str), "t=%i, lun=%i, opc=%i, cnt=0x%x, ctrl=0x%x",
	         ctr->target, HDC_GetLUN(ctr), ctr->opcode, HDC_GetCount(ctr),
	         HDC_GetControl(ctr));

	return str;
}


/**
 * Seek - move to a sector
 */
static void HDC_Cmd_Seek(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];

	dev->nLastBlockAddr = HDC_GetLBA(ctr);

	LOG_TRACE(TRACE_SCSI_CMD, "HDC: SEEK (%s), LBA=%i",
	          HDC_CmdInfoStr(ctr), dev->nLastBlockAddr);

	if (dev->nLastBlockAddr < dev->hdSize &&
	    fseeko(dev->image_file, (off_t)dev->nLastBlockAddr * 512L, SEEK_SET) == 0)
	{
		LOG_TRACE(TRACE_SCSI_CMD, " -> OK\n");
		ctr->returnCode = HD_STATUS_OK;
		dev->nLastError = HD_REQSENS_OK;
	}
	else
	{
		LOG_TRACE(TRACE_SCSI_CMD, " -> ERROR\n");
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_INVADDR;
	}

	dev->bSetLastBlockAddr = true;
}


/**
 * Inquiry - return some disk information
 */
static void HDC_Cmd_Inquiry(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	Uint8 *buf;
	int count;

	count = HDC_GetCount(ctr);

	LOG_TRACE(TRACE_SCSI_CMD, "HDC: INQUIRY (%s)", HDC_CmdInfoStr(ctr));

	buf = HDC_PrepRespBuf(ctr, count);
	memcpy(buf, inquiry_bytes, count);

	if (count > (int)sizeof(inquiry_bytes))
		count = sizeof(inquiry_bytes);

	/* For unsupported LUNs set the Peripheral Qualifier and the
	 * Peripheral Device Type according to the SCSI standard */
	buf[0] = HDC_GetLUN(ctr) == 0 ? 0 : 0x7F;

	buf[4] = count - 5;

	ctr->returnCode = HD_STATUS_OK;

	dev->nLastError = HD_REQSENS_OK;
	dev->bSetLastBlockAddr = false;
}


/**
 * Request sense - return some disk information
 */
static void HDC_Cmd_RequestSense(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	int nRetLen;
	Uint8 *retbuf;

	nRetLen = HDC_GetCount(ctr);

	LOG_TRACE(TRACE_SCSI_CMD, "HDC: REQUEST SENSE (%s).\n", HDC_CmdInfoStr(ctr));

	if ((nRetLen < 4 && nRetLen != 0) || nRetLen > 22)
	{
		Log_Printf(LOG_WARN, "HDC: *** Strange REQUEST SENSE ***!\n");
	}

	/* Limit to sane length */
	if (nRetLen <= 0)
	{
		nRetLen = 4;
	}
	else if (nRetLen > 22)
	{
		nRetLen = 22;
	}

	retbuf = HDC_PrepRespBuf(ctr, nRetLen);
	memset(retbuf, 0, nRetLen);

	if (nRetLen <= 4)
	{
		retbuf[0] = dev->nLastError;
		if (dev->bSetLastBlockAddr)
		{
			retbuf[0] |= 0x80;
			retbuf[1] = dev->nLastBlockAddr >> 16;
			retbuf[2] = dev->nLastBlockAddr >> 8;
			retbuf[3] = dev->nLastBlockAddr;
		}
	}
	else
	{
		retbuf[0] = 0x70;
		if (dev->bSetLastBlockAddr)
		{
			retbuf[0] |= 0x80;
			retbuf[4] = dev->nLastBlockAddr >> 16;
			retbuf[5] = dev->nLastBlockAddr >> 8;
			retbuf[6] = dev->nLastBlockAddr;
		}
		switch (dev->nLastError)
		{
		 case HD_REQSENS_OK:  retbuf[2] = 0; break;
		 case HD_REQSENS_OPCODE:  retbuf[2] = 5; break;
		 case HD_REQSENS_INVADDR:  retbuf[2] = 5; break;
		 case HD_REQSENS_INVARG:  retbuf[2] = 5; break;
		 case HD_REQSENS_INVLUN:  retbuf[2] = 5; break;
		 default: retbuf[2] = 4; break;
		}
		retbuf[7] = 14;
		retbuf[12] = dev->nLastError;
		retbuf[19] = dev->nLastBlockAddr >> 16;
		retbuf[20] = dev->nLastBlockAddr >> 8;
		retbuf[21] = dev->nLastBlockAddr;
	}

	/*
	fprintf(stderr,"*** Requested sense packet:\n");
	int i;
	for (i = 0; i<nRetLen; i++) fprintf(stderr,"%2x ",retbuf[i]);
	fprintf(stderr,"\n");
	*/

	ctr->returnCode = HD_STATUS_OK;
}


/**
 * Mode sense - Get parameters from disk.
 * (Just enough to make the HDX tool from AHDI 5.0 happy)
 */
static void HDC_Cmd_ModeSense(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	Uint8 *buf;

	LOG_TRACE(TRACE_SCSI_CMD, "HDC: MODE SENSE (%s).\n", HDC_CmdInfoStr(ctr));

	dev->bSetLastBlockAddr = false;

	if (ctr->command[2] != 0 || HDC_GetCount(ctr) != 0x10)
	{
		Log_Printf(LOG_TODO, "HDC: Unsupported MODE SENSE command\n");
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_INVARG;
		return;
	}

	buf = HDC_PrepRespBuf(ctr, 16);

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 8;
	buf[4] = 0;

	buf[5] = dev->hdSize >> 16;  // Number of blocks, high
	buf[6] = dev->hdSize >> 8;   // Number of blocks, med
	buf[7] = dev->hdSize;        // Number of blocks, low

	buf[8] = 0;

	buf[9] = 0;      // Block size in bytes, high
	buf[10] = 2;     // Block size in bytes, med
	buf[11] = 0;     // Block size in bytes, low

	buf[12] = 0;
	buf[13] = 0;
	buf[14] = 0;
	buf[15] = 0;

	ctr->returnCode = HD_STATUS_OK;
	dev->nLastError = HD_REQSENS_OK;
}


/**
 * Format drive.
 */
static void HDC_Cmd_FormatDrive(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];

	LOG_TRACE(TRACE_SCSI_CMD, "HDC: FORMAT DRIVE (%s).\n", HDC_CmdInfoStr(ctr));

	/* Should erase the whole image file here... */

	ctr->returnCode = HD_STATUS_OK;
	dev->nLastError = HD_REQSENS_OK;
	dev->bSetLastBlockAddr = false;
}


/**
 * Read capacity of our disk.
 */
static void HDC_Cmd_ReadCapacity(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	int nSectors = dev->hdSize - 1;
	Uint8 *buf;

	LOG_TRACE(TRACE_SCSI_CMD, "HDC: READ CAPACITY (%s)\n", HDC_CmdInfoStr(ctr));

	buf = HDC_PrepRespBuf(ctr, 8);

	buf[0] = (nSectors >> 24) & 0xFF;
	buf[1] = (nSectors >> 16) & 0xFF;
	buf[2] = (nSectors >> 8) & 0xFF;
	buf[3] = (nSectors) & 0xFF;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x02;
	buf[7] = 0x00;

	ctr->returnCode = HD_STATUS_OK;
	dev->nLastError = HD_REQSENS_OK;
	dev->bSetLastBlockAddr = false;
}


/**
 * Write a sector off our disk - (seek implied)
 */
static void HDC_Cmd_WriteSector(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	Uint32 nDmaAddr = FDC_GetDMAAddress();
	int n = 0;

	dev->nLastBlockAddr = HDC_GetLBA(ctr);

	LOG_TRACE(TRACE_SCSI_CMD, "HDC: WRITE SECTOR (%s) with LBA 0x%x from 0x%x",
	          HDC_CmdInfoStr(ctr), dev->nLastBlockAddr, nDmaAddr);

	/* seek to the position */
	if (dev->nLastBlockAddr >= dev->hdSize ||
	    fseeko(dev->image_file, (off_t)dev->nLastBlockAddr * 512L, SEEK_SET) != 0)
	{
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_INVADDR;
	}
	else
	{
		/* write - if allowed */
#ifndef DISALLOW_HDC_WRITE
		if ( STMemory_CheckAreaType ( nDmaAddr , 512 * HDC_GetCount(ctr) , ABFLAG_RAM ) )
		{
			n = fwrite(&STRam[nDmaAddr], 512,
				   HDC_GetCount(ctr), dev->image_file);
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC sector write uses invalid RAM range 0x%x+%i\n",
				   nDmaAddr, 512 * HDC_GetCount(ctr));
			ctr->bDmaError = true;
		}
#endif
		if (n == HDC_GetCount(ctr))
		{
			ctr->returnCode = HD_STATUS_OK;
			dev->nLastError = HD_REQSENS_OK;
		}
		else
		{
			ctr->returnCode = HD_STATUS_ERROR;
			dev->nLastError = HD_REQSENS_WRITEERR;
		}

		/* Update DMA counter */
		FDC_WriteDMAAddress(nDmaAddr + 512*n);
	}
	LOG_TRACE(TRACE_SCSI_CMD, " -> %s (%d)\n",
		  ctr->returnCode == HD_STATUS_OK ? "OK" : "ERROR",
		  dev->nLastError);

	dev->bSetLastBlockAddr = true;
}


/**
 * Read a sector off our disk - (implied seek)
 */
static void HDC_Cmd_ReadSector(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	Uint8 *buf;
	int n;

	dev->nLastBlockAddr = HDC_GetLBA(ctr);

	LOG_TRACE(TRACE_SCSI_CMD, "HDC: READ SECTOR (%s) with LBA 0x%x",
	          HDC_CmdInfoStr(ctr), dev->nLastBlockAddr);

	/* seek to the position */
	if (dev->nLastBlockAddr >= dev->hdSize ||
	    fseeko(dev->image_file, (off_t)dev->nLastBlockAddr * 512L, SEEK_SET) != 0)
	{
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_INVADDR;
	}
	else
	{
		buf = HDC_PrepRespBuf(ctr, 512 * HDC_GetCount(ctr));
		n = fread(buf, 512, HDC_GetCount(ctr), dev->image_file);
		if (n == HDC_GetCount(ctr))
		{
			ctr->returnCode = HD_STATUS_OK;
			dev->nLastError = HD_REQSENS_OK;
		}
		else
		{
			ctr->returnCode = HD_STATUS_ERROR;
			dev->nLastError = HD_REQSENS_NOSECTOR;
		}
	}
	LOG_TRACE(TRACE_SCSI_CMD, " -> %s (%d)\n",
		  ctr->returnCode == HD_STATUS_OK ? "OK" : "ERROR",
		  dev->nLastError);

	dev->bSetLastBlockAddr = true;
}


/**
 * Test unit ready
 */
static void HDC_Cmd_TestUnitReady(SCSI_CTRLR *ctr)
{
	LOG_TRACE(TRACE_SCSI_CMD, "HDC: TEST UNIT READY (%s).\n", HDC_CmdInfoStr(ctr));
	ctr->returnCode = HD_STATUS_OK;
}


/**
 * Emulation routine for HDC command packets.
 */
static void HDC_EmulateCommandPacket(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];

	ctr->respcnt = 0;

	switch (ctr->opcode)
	{
	 case HD_TEST_UNIT_RDY:
		HDC_Cmd_TestUnitReady(ctr);
		break;

	 case HD_READ_CAPACITY1:
		HDC_Cmd_ReadCapacity(ctr);
		break;

	 case HD_READ_SECTOR:
	 case HD_READ_SECTOR1:
		HDC_Cmd_ReadSector(ctr);
		break;

	 case HD_WRITE_SECTOR:
	 case HD_WRITE_SECTOR1:
		HDC_Cmd_WriteSector(ctr);
		break;

	 case HD_INQUIRY:
		HDC_Cmd_Inquiry(ctr);
		break;

	 case HD_SEEK:
		HDC_Cmd_Seek(ctr);
		break;

	 case HD_SHIP:
		LOG_TRACE(TRACE_SCSI_CMD, "HDC: SHIP (%s).\n", HDC_CmdInfoStr(ctr));
		ctr->returnCode = 0xFF;
		break;

	 case HD_REQ_SENSE:
		HDC_Cmd_RequestSense(ctr);
		break;

	 case HD_MODESELECT:
		LOG_TRACE(TRACE_SCSI_CMD, "HDC: MODE SELECT (%s) TODO!\n", HDC_CmdInfoStr(ctr));
		ctr->returnCode = HD_STATUS_OK;
		dev->nLastError = HD_REQSENS_OK;
		dev->bSetLastBlockAddr = false;
		break;

	 case HD_MODESENSE:
		HDC_Cmd_ModeSense(ctr);
		break;

	 case HD_FORMAT_DRIVE:
		HDC_Cmd_FormatDrive(ctr);
		break;

	 /* as of yet unsupported commands */
	 case HD_VERIFY_TRACK:
	 case HD_FORMAT_TRACK:
	 case HD_CORRECTION:

	 default:
		LOG_TRACE(TRACE_SCSI_CMD, "HDC: Unsupported command (%s)!\n", HDC_CmdInfoStr(ctr));
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_OPCODE;
		dev->bSetLastBlockAddr = false;
		break;
	}

	/* Update the led each time a command is processed */
	Statusbar_EnableHDLed( LED_STATE_ON );
}


/*---------------------------------------------------------------------*/
/**
 * Return given image file (primary) partition count and with tracing
 * print also partition table.
 *
 * Supports both DOS and Atari master boot record partition tables
 * (with 4 entries).
 *
 * TODO:
 * - Support also Atari ICD (12 entries, at offset 0x156) and
 *   extended partition schemes.  Linux kernel has code for those:
 *   http://lxr.free-electrons.com/source/block/partitions/atari.c
 * - Support partition tables with other endianness
 */
int HDC_PartitionCount(FILE *fp, const Uint64 tracelevel)
{
	unsigned char *pinfo, bootsector[512];
	Uint32 start, sectors, total = 0;
	int i, parts = 0;
	off_t offset;

	if (!fp)
		return 0;
	offset = ftello(fp);

	if (fseeko(fp, 0, SEEK_SET) != 0
	    || fread(bootsector, sizeof(bootsector), 1, fp) != 1)
	{
		perror("HDC_PartitionCount");
		return 0;
	}

	if (bootsector[0x1FE] == 0x55 && bootsector[0x1FF] == 0xAA)
	{
		int ptype, boot;

		LOG_TRACE(tracelevel, "DOS MBR:\n");
		/* first partition table entry */
		pinfo = bootsector + 0x1BE;
		for (i = 0; i < 4; i++, pinfo += 16)
		{
			boot = pinfo[0];
			ptype = pinfo[4];
			start = SDL_SwapLE32(*(long*)(pinfo+8));
			sectors = SDL_SwapLE32(*(long*)(pinfo+12));
			total += sectors;
			LOG_TRACE(tracelevel, "- Partition %d: type=0x%02x, start=0x%08x, size=%.1f MB %s%s\n",
				  i, ptype, start, sectors/2048.0, boot ? "(boot)" : "", sectors ? "" : "(invalid)");
			if (ptype)
				parts++;
		}
		LOG_TRACE(tracelevel, "- Total size: %.1f MB in %d partitions\n", total/2048.0, parts);
	}
	else
	{
		/* Partition table contains hd size + 4 partition entries
		 * (composed of flag byte, 3 char ID, start offset
		 * and size), this is followed by bad sector list +
		 * count and the root sector checksum. Before this
		 * there's the boot code.
		 */
		char c, pid[4];
		int j, flags;

		LOG_TRACE(tracelevel, "ATARI MBR:\n");
		pinfo = bootsector + 0x1C6;
		for (i = 0; i < 4; i++, pinfo += 12)
		{
			flags = pinfo[0];
			for (j = 0; j < 3; j++)
			{
				c = pinfo[j+1];
				if (c < 32 || c >= 127)
					c = '.';
				pid[j] = c;
			}
			pid[3] = '\0';
			start = HDC_ReadInt32(pinfo, 4);
			sectors = HDC_ReadInt32(pinfo, 8);
			LOG_TRACE(tracelevel, "- Partition %d: ID=%s, start=0x%08x, size=%.1f MB, flags=0x%x\n",
				  i, pid, start, sectors/2048.0, flags);
			if (flags & 0x1)
				parts++;
		}
		total = HDC_ReadInt32(bootsector, 0x1C2);
		LOG_TRACE(tracelevel, "- Total size: %.1f MB in %d partitions\n", total/2048.0, parts);
	}

	if (fseeko(fp, offset, SEEK_SET) != 0)
		perror("HDC_PartitionCount");
	return parts;
}

/**
 * Check file size for sane values (non-zero, multiple of 512),
 * and return the size
 */
off_t HDC_CheckAndGetSize(const char *filename)
{
	off_t filesize;
	char shortname[48];

	File_ShrinkName(shortname, filename, sizeof(shortname));

	filesize = File_Length(filename);
	if (filesize < 0)
	{
		Log_AlertDlg(LOG_ERROR, "Unable to get size of HD image file\n'%s'!",
		             shortname);
		if (sizeof(off_t) < 8)
		{
			Log_Printf(LOG_ERROR, "Note: This version of Hatari has been built"
			                      " _without_ support for large files,\n"
			                      "      so you can not use HD images > 2 GB.\n");
		}
		return -EFBIG;
	}
	if (filesize == 0)
	{
		Log_AlertDlg(LOG_ERROR, "Can not use HD image file\n'%s'\n"
		                        "since the file is empty.",
		             shortname);
		return -EINVAL;
	}
	if ((filesize & 0x1ff) != 0)
	{
		Log_AlertDlg(LOG_ERROR, "Can not use the hard disk image file\n"
		                        "'%s'\nsince its size is not a multiple"
		                        " of 512.",
		            shortname);
		return -EINVAL;
	}

	return filesize;
}

/**
 * Open a disk image file
 */
static int HDC_InitDevice(SCSI_DEV *dev, char *filename)
{
	off_t filesize;
	FILE *fp;

	dev->enabled = false;
	Log_Printf(LOG_INFO, "Mounting hard drive image '%s'\n", filename);

	/* Check size for sanity */
	filesize = HDC_CheckAndGetSize(filename);
	if (filesize < 0)
		return filesize;

	fp = fopen(filename, "rb+");
	if (fp == NULL)
	{
		Log_Printf(LOG_ERROR, "ERROR: cannot open HD file read/write!\n");
		return -ENOENT;
	}
	if (!File_Lock(fp))
	{
		Log_Printf(LOG_ERROR, "ERROR: cannot lock HD file for writing!\n");
		fclose(fp);
		return -ENOLCK;
	}

	dev->hdSize = filesize / 512;
	dev->image_file = fp;
	dev->enabled = true;

	return 0;
}

/**
 * Open the disk image file, set partitions.
 */
bool HDC_Init(void)
{
	int i;

	/* ACSI */
	nAcsiPartitions = 0;
	bAcsiEmuOn = false;
	memset(&AcsiBus, 0, sizeof(AcsiBus));
	AcsiBus.respbufsize = 512;
	AcsiBus.resp = malloc(AcsiBus.respbufsize);
	if (!AcsiBus.resp)
	{
		perror("HDC_Init");
		return false;
	}
	for (i = 0; i < MAX_ACSI_DEVS; i++)
	{
		if (!ConfigureParams.Acsi[i].bUseDevice)
			continue;
		if (HDC_InitDevice(&AcsiBus.devs[i], ConfigureParams.Acsi[i].sDeviceFile) == 0)
		{
			bAcsiEmuOn = true;
			nAcsiPartitions += HDC_PartitionCount(AcsiBus.devs[i].image_file, TRACE_SCSI_CMD);
		}
	}

	/* SCSI */
#if WITH_NCR5380
	memset(&ScsiBus, 0, sizeof(ScsiBus));
	ScsiBus.respbufsize = 512;
	ScsiBus.resp = malloc(ScsiBus.respbufsize);
	if (!ScsiBus.resp)
	{
		perror("HDC_Init");
		return bAcsiEmuOn;
	}
	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		if (!ConfigureParams.Scsi[i].bUseDevice)
			continue;
		HDC_InitDevice(&ScsiBus.devs[i], ConfigureParams.Scsi[i].sDeviceFile);
	}
#endif

	/* set number of partitions */
	nNumDrives += nAcsiPartitions;

	return bAcsiEmuOn;
}


/*---------------------------------------------------------------------*/
/**
 * HDC_UnInit - close image file
 *
 */
void HDC_UnInit(void)
{
	int i;

	if (!bAcsiEmuOn)
		return;

	for (i = 0; i < MAX_ACSI_DEVS; i++)
	{
		if (!AcsiBus.devs[i].enabled)
			continue;
		File_UnLock(AcsiBus.devs[i].image_file);
		fclose(AcsiBus.devs[i].image_file);
		AcsiBus.devs[i].image_file = NULL;
		AcsiBus.devs[i].enabled = false;
	}
	free(AcsiBus.resp);
	AcsiBus.resp = NULL;

#if WITH_NCR5380
	for (i = 0; i < MAX_SCSI_DEVS; i++)
	{
		if (!ScsiBus.devs[i].enabled)
			continue;
		File_UnLock(ScsiBus.devs[i].image_file);
		fclose(ScsiBus.devs[i].image_file);
		ScsiBus.devs[i].image_file = NULL;
		ScsiBus.devs[i].enabled = false;
	}
	free(ScsiBus.resp);
	ScsiBus.resp = NULL;
#endif

	nNumDrives -= nAcsiPartitions;
	nAcsiPartitions = 0;
	bAcsiEmuOn = false;
}


/*---------------------------------------------------------------------*/
/**
 * Reset command status.
 */
void HDC_ResetCommandStatus(void)
{
	if (!Config_IsMachineFalcon())
		AcsiBus.returnCode = 0;
}


/**
 * Process HDC command packets (SCSI/ACSI) bytes.
 * @return true if command has been executed.
 */
static bool HDC_WriteCommandPacket(SCSI_CTRLR *ctr, Uint8 b)
{
	bool bDidCmd = false;
	SCSI_DEV *dev = &ctr->devs[ctr->target];

	/* Abort if the target device is not enabled */
	if (!dev->enabled)
	{
		ctr->returnCode = HD_STATUS_ERROR;
		return false;
	}

	/* Extract ACSI/SCSI opcode */
	if (ctr->byteCount == 0)
	{
		ctr->opcode = b;
		ctr->bDmaError = false;
	}

	/* Successfully received one byte, and increase the byte-count */
	if (ctr->byteCount < (int)sizeof(ctr->command))
		ctr->command[ctr->byteCount] = b;
	++ctr->byteCount;

	/* have we received a complete 6-byte class 0 or 10-byte class 1 packet yet? */
	if ((ctr->opcode < 0x20 && ctr->byteCount >= 6) ||
	    (ctr->opcode < 0x60 && ctr->byteCount >= 10))
	{
		/* We currently only support LUN 0, however INQUIRY must
		 * always be handled, see SCSI standard */
		if (HDC_GetLUN(ctr) == 0 || ctr->opcode == HD_INQUIRY)
		{
			HDC_EmulateCommandPacket(ctr);
			bDidCmd = true;
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC: Access to non-existing LUN."
				   " Command = 0x%02x\n", ctr->opcode);
			dev->nLastError = HD_REQSENS_INVLUN;
			/* REQUEST SENSE is still handled for invalid LUNs */
			if (ctr->opcode == HD_REQ_SENSE)
			{
				HDC_Cmd_RequestSense(ctr);
				bDidCmd = true;
			}
			else
			{
				ctr->returnCode = HD_STATUS_ERROR;
			}
		}

		ctr->byteCount = 0;
	}
	else if (ctr->opcode >= 0x60)
	{
		/* Commands >= 0x60 are not supported right now */
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_OPCODE;
		dev->bSetLastBlockAddr = false;
		if (ctr->byteCount == 10)
		{
			LOG_TRACE(TRACE_SCSI_CMD, "HDC: Unsupported command (%s).\n",
			          HDC_CmdInfoStr(ctr));
		}
	}
	else
	{
		ctr->returnCode = HD_STATUS_OK;
	}

	return bDidCmd;
}

/*---------------------------------------------------------------------*/

static struct ncr5380_regs
{
	Uint8 initiator_cmd;
	Uint8 current_bus_status;
	Uint8 bus_and_status;
} ncr_regs;

/**
 * Emulate external reset "pin": Clear registers etc.
 */
void Ncr5380_Reset(void)
{
	ncr_regs.initiator_cmd &= 0x7f;
}

/**
 * Write a command byte to the NCR 5380 SCSI controller
 */
static void Ncr5380_WriteByte(int addr, Uint8 byte)
{
#if WITH_NCR5380
	switch (addr)
	{
	case 0:			/* Output Data register */
		ncr_regs.current_bus_status |= 0x40;
		break;
	case 1:			/* Initiator Command register */
		ncr_regs.initiator_cmd = byte;
		break;
	case 2:			/* Mode register */
		break;
	case 3:			/* Target Command register */
		break;
	case 4:			/* Select Enable register */
		break;
	case 5:			/* Start DMA Send register */
		break;
	case 6:			/* Start DMA Target Receive register */
		break;
	case 7:			/* Start DMA Initiator Receive register */
		break;
	default:
		fprintf(stderr, "Unexpected NCR5380 address\n");
	}
#endif
}

/**
 * Read a command byte from the NCR 5380 SCSI controller
 */
static Uint8 Ncr5380_ReadByte(int addr)
{
#if WITH_NCR5380
	switch (addr)
	{
	case 0:			/* Current SCSI Data register */
		break;
	case 1:			/* Initiator Command register */
		return ncr_regs.initiator_cmd & 0x9f;
	case 2:			/* Mode register */
		break;
	case 3:			/* Target Command register */
		break;
	case 4:			/* Current SCSI Bus Status register */
		if (ncr_regs.current_bus_status & 0x40)	/* BUSY? */
			ncr_regs.current_bus_status |= 0x20;
		else
			ncr_regs.current_bus_status &= ~0x20;
		if (ncr_regs.initiator_cmd & 0x80)	/* ASSERT RST? */
			ncr_regs.current_bus_status |= 0x80;
		else
			ncr_regs.current_bus_status &= ~0x80;
		if (ncr_regs.initiator_cmd & 0x04)	/* ASSERT BUSY? */
			ncr_regs.current_bus_status |= 0x40;
		else
			ncr_regs.current_bus_status &= ~0x40;
		return ncr_regs.current_bus_status;
	case 5:			/* Bus and Status register */
		return ncr_regs.bus_and_status;
	case 6:			/* Input Data register */
		break;
	case 7:			/* Reset Parity/Interrupts register */
		/* Reset PARITY ERROR, IRQ REQUEST and BUSY ERROR bits */
		ncr_regs.bus_and_status &= 0xcb;
		return 0;  /* TODO: Is this return value ok? */
	default:
		fprintf(stderr, "Unexpected NCR5380 address\n");
	}
#endif

	return 0;
}


static void Acsi_WriteCommandByte(int addr, Uint8 byte)
{
	/* Clear IRQ initially (will be set again if byte has been accepted) */
	FDC_ClearHdcIRQ();

	/* When the A1 pin is pushed to 0, we want to start a new command.
	 * We ignore the pin for the second byte in the packet since this
	 * seems to happen on real hardware too (some buggy driver relies
	 * on this behavior). */
	if ((addr & 2) == 0 && AcsiBus.byteCount != 1)
	{
		AcsiBus.byteCount = 0;
		AcsiBus.target = ((byte & 0xE0) >> 5);
		/* Only process the first byte if it is not
		 * an extended ICD command marker byte */
		if ((byte & 0x1F) != 0x1F)
		{
			HDC_WriteCommandPacket(&AcsiBus, byte & 0x1F);
		}
		else
		{
			AcsiBus.returnCode = HD_STATUS_OK;
			AcsiBus.bDmaError = false;
		}
	}
	else
	{
		/* Process normal command byte */
		bool bDidCmd = HDC_WriteCommandPacket(&AcsiBus, byte);
		if (bDidCmd && AcsiBus.returnCode == HD_STATUS_OK && AcsiBus.respcnt)
		{
			/* DMA transfer necessary */
			Uint32 nDmaAddr = FDC_GetDMAAddress();
			if (!STMemory_SafeCopy(nDmaAddr, AcsiBus.resp, AcsiBus.respcnt, "ACSI DMA"))
			{
				AcsiBus.bDmaError = true;
				AcsiBus.returnCode = HD_STATUS_ERROR;
			}
			FDC_WriteDMAAddress(nDmaAddr + AcsiBus.respcnt);
			AcsiBus.respcnt = 0;
		}
	}

	if (AcsiBus.devs[AcsiBus.target].enabled)
	{
		FDC_SetDMAStatus(AcsiBus.bDmaError);	/* Mark DMA error */
		FDC_SetIRQ(FDC_IRQ_SOURCE_HDC);
	}
}

/**
 * Called when command bytes have been written to $FFFF8606 and
 * the HDC (not the FDC) is selected.
 */
void HDC_WriteCommandByte(int addr, Uint8 byte)
{
	// fprintf(stderr, "HDC: Write cmd byte addr=%i, byte=%02x\n", addr, byte);

	if (Config_IsMachineFalcon())
		Ncr5380_WriteByte(addr, byte);
	else if (bAcsiEmuOn)
		Acsi_WriteCommandByte(addr, byte);
}

/**
 * Get command byte.
 */
short int HDC_ReadCommandByte(int addr)
{
	Uint16 ret;
	if (Config_IsMachineFalcon())
		ret = Ncr5380_ReadByte(addr);
	else
		ret = AcsiBus.returnCode;	/* ACSI status */
	return ret;
}
