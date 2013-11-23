/*
  Hatari - hdc.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Low-level hard drive emulation
*/
const char HDC_fileid[] = "Hatari hdc.c : " __DATE__ " " __TIME__;

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
// #define HDC_REALLY_VERBOSE    /* display command packets */

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
	short int returnCode;       /* return code from the HDC operation */
	SCSI_DEV devs[8];
} SCSI_CTRLR;

/* HDC globals */
SCSI_CTRLR AcsiBus;
int nPartitions = 0;
bool bAcsiEmuOn = false;


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
#ifdef HDC_REALLY_VERBOSE
static unsigned char HDC_GetControl(SCSI_CTRLR *ctr)
{
	if (ctr->opcode < 0x20)
		return ctr->command[5];			/* Class 0 */
	else
		return ctr->command[9];			/* Class 1 */
}
#endif


/**
 * Seek - move to a sector
 */
static void HDC_Cmd_Seek(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];

	dev->nLastBlockAddr = HDC_GetLBA(ctr);

	if (dev->nLastBlockAddr < dev->hdSize &&
	    fseeko(dev->image_file, (off_t)dev->nLastBlockAddr * 512L, SEEK_SET) == 0)
	{
		ctr->returnCode = HD_STATUS_OK;
		dev->nLastError = HD_REQSENS_OK;
	}
	else
	{
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_INVADDR;
	}

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_SetIRQ();
	dev->bSetLastBlockAddr = true;
}


/**
 * Inquiry - return some disk information
 */
static void HDC_Cmd_Inquiry(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	Uint32 nDmaAddr;
	int count;

	nDmaAddr = FDC_GetDMAAddress();
	count = HDC_GetCount(ctr);

#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Inquiry, %i bytes to 0x%x.\n", count, nDmaAddr);
#endif

	if (count > (int)sizeof(inquiry_bytes))
		count = sizeof(inquiry_bytes);

	/* For unsupported LUNs set the Peripheral Qualifier and the
	 * Peripheral Device Type according to the SCSI standard */
	inquiry_bytes[0] = HDC_GetLUN(ctr) == 0 ? 0 : 0x7F;

	inquiry_bytes[4] = count - 5;

	if (STMemory_SafeCopy(nDmaAddr, inquiry_bytes, count, "HDC DMA inquiry"))
		ctr->returnCode = HD_STATUS_OK;
	else
		ctr->returnCode = HD_STATUS_ERROR;

	FDC_WriteDMAAddress(nDmaAddr + count);

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_SetIRQ();
	dev->nLastError = HD_REQSENS_OK;
	dev->bSetLastBlockAddr = false;
}


/**
 * Request sense - return some disk information
 */
static void HDC_Cmd_RequestSense(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	Uint32 nDmaAddr;
	int nRetLen;
	Uint8 retbuf[22];

	nRetLen = HDC_GetCount(ctr);

#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Request Sense with length = %i.\n", nRetLen);
#endif

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

	nDmaAddr = FDC_GetDMAAddress();

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

	if (STMemory_SafeCopy(nDmaAddr, retbuf, nRetLen, "HDC request sense"))
		ctr->returnCode = HD_STATUS_OK;
	else
		ctr->returnCode = HD_STATUS_ERROR;

	FDC_WriteDMAAddress(nDmaAddr + nRetLen);

	FDC_SetDMAStatus(false);            /* no DMA error */
	FDC_SetIRQ();
}


/**
 * Mode sense - Get parameters from disk.
 * (Just enough to make the HDX tool from AHDI 5.0 happy)
 */
static void HDC_Cmd_ModeSense(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	Uint32 nDmaAddr;

#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Mode Sense.\n");
#endif

	nDmaAddr = FDC_GetDMAAddress();

	if (!STMemory_ValidArea(nDmaAddr, 16))
	{
		Log_Printf(LOG_WARN, "HCD mode sense uses invalid RAM range 0x%x+%i\n", nDmaAddr, 16);
		ctr->returnCode = HD_STATUS_ERROR;
	}
	else if (ctr->command[2] == 0 && HDC_GetCount(ctr) == 0x10)
	{
		size_t blocks;
		blocks = dev->hdSize;

		STRam[nDmaAddr+0] = 0;
		STRam[nDmaAddr+1] = 0;
		STRam[nDmaAddr+2] = 0;
		STRam[nDmaAddr+3] = 8;
		STRam[nDmaAddr+4] = 0;

		STRam[nDmaAddr+5] = blocks >> 16;  // Number of blocks, high (?)
		STRam[nDmaAddr+6] = blocks >> 8;   // Number of blocks, med (?)
		STRam[nDmaAddr+7] = blocks;        // Number of blocks, low (?)

		STRam[nDmaAddr+8] = 0;

		STRam[nDmaAddr+9] = 0;      // Block size in bytes, high
		STRam[nDmaAddr+10] = 2;     // Block size in bytes, med
		STRam[nDmaAddr+11] = 0;     // Block size in bytes, low

		STRam[nDmaAddr+12] = 0;
		STRam[nDmaAddr+13] = 0;
		STRam[nDmaAddr+14] = 0;
		STRam[nDmaAddr+15] = 0;

		FDC_WriteDMAAddress(nDmaAddr + 16);

		ctr->returnCode = HD_STATUS_OK;
		dev->nLastError = HD_REQSENS_OK;
	}
	else
	{
		Log_Printf(LOG_TODO, "HDC: Unsupported MODE SENSE command\n");
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_INVARG;
	}

	FDC_SetDMAStatus(false);            /* no DMA error */
	FDC_SetIRQ();
	dev->bSetLastBlockAddr = false;
}


/**
 * Format drive.
 */
static void HDC_Cmd_FormatDrive(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];

#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Format drive!\n");
#endif

	/* Should erase the whole image file here... */

	FDC_SetDMAStatus(false);            /* no DMA error */
	FDC_SetIRQ();
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
	Uint32 nDmaAddr = FDC_GetDMAAddress();

#ifdef HDC_VERBOSE
	fprintf(stderr,"Reading 8 bytes capacity data to addr: 0x%x\n", nDmaAddr);
#endif

	/* seek to the position */
	if (STMemory_ValidArea(nDmaAddr, 8))
	{
		int nSectors = dev->hdSize - 1;
		STRam[nDmaAddr++] = (nSectors >> 24) & 0xFF;
		STRam[nDmaAddr++] = (nSectors >> 16) & 0xFF;
		STRam[nDmaAddr++] = (nSectors >> 8) & 0xFF;
		STRam[nDmaAddr++] = (nSectors) & 0xFF;
		STRam[nDmaAddr++] = 0x00;
		STRam[nDmaAddr++] = 0x00;
		STRam[nDmaAddr++] = 0x02;
		STRam[nDmaAddr++] = 0x00;

		/* Update DMA counter */
		FDC_WriteDMAAddress(nDmaAddr);

		ctr->returnCode = HD_STATUS_OK;
		dev->nLastError = HD_REQSENS_OK;
	}
	else
	{
		Log_Printf(LOG_WARN, "HDC capacity read uses invalid RAM range 0x%x+%i\n", nDmaAddr, 8);
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_NOSECTOR;
	}

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_SetIRQ();
	dev->bSetLastBlockAddr = false;
}


/**
 * Write a sector off our disk - (seek implied)
 */
static void HDC_Cmd_WriteSector(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	int n = 0;

	dev->nLastBlockAddr = HDC_GetLBA(ctr);

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
		Uint32 nDmaAddr = FDC_GetDMAAddress();
#ifndef DISALLOW_HDC_WRITE
		if (STMemory_ValidArea(nDmaAddr, 512 * HDC_GetCount(ctr)))
		{
			n = fwrite(&STRam[nDmaAddr], 512,
				   HDC_GetCount(ctr), dev->image_file);
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC sector write uses invalid RAM range 0x%x+%i\n",
				   nDmaAddr, 512 * HDC_GetCount(ctr));
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

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_SetIRQ();
	dev->bSetLastBlockAddr = true;
}


/**
 * Read a sector off our disk - (implied seek)
 */
static void HDC_Cmd_ReadSector(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];
	int n;

	dev->nLastBlockAddr = HDC_GetLBA(ctr);

#ifdef HDC_VERBOSE
	fprintf(stderr,"Reading %i sectors starting from 0x%x to addr: 0x%x\n",
	        HDC_GetCount(ctr), dev->nLastBlockAddr, FDC_GetDMAAddress());
#endif

	/* seek to the position */
	if (dev->nLastBlockAddr >= dev->hdSize ||
	    fseeko(dev->image_file, (off_t)dev->nLastBlockAddr * 512L, SEEK_SET) != 0)
	{
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_INVADDR;
	}
	else
	{
		Uint32 nDmaAddr = FDC_GetDMAAddress();
		if (STMemory_ValidArea(nDmaAddr, 512 * HDC_GetCount(ctr)))
		{
			n = fread(&STRam[nDmaAddr], 512,
				   HDC_GetCount(ctr), dev->image_file);
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC sector read uses invalid RAM range 0x%x+%i\n",
				   nDmaAddr, 512 * HDC_GetCount(ctr));
			n = 0;
		}
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

		/* Update DMA counter */
		FDC_WriteDMAAddress(nDmaAddr + 512*n);
	}

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_SetIRQ();
	dev->bSetLastBlockAddr = true;
}


/**
 * Test unit ready
 */
static void HDC_Cmd_TestUnitReady(SCSI_CTRLR *ctr)
{
	FDC_SetDMAStatus(false);            /* no DMA error */
	FDC_SetIRQ();
	ctr->returnCode = HD_STATUS_OK;
}


/**
 * Emulation routine for HDC command packets.
 */
static void HDC_EmulateCommandPacket(SCSI_CTRLR *ctr)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];

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
		ctr->returnCode = 0xFF;
		FDC_SetIRQ();
		break;

	 case HD_REQ_SENSE:
		HDC_Cmd_RequestSense(ctr);
		break;

	 case HD_MODESELECT:
		Log_Printf(LOG_TODO, "HDC: MODE SELECT call not implemented yet.\n");
		ctr->returnCode = HD_STATUS_OK;
		dev->nLastError = HD_REQSENS_OK;
		dev->bSetLastBlockAddr = false;
		FDC_SetDMAStatus(false);
		FDC_SetIRQ();
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
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_OPCODE;
		dev->bSetLastBlockAddr = false;
		FDC_SetIRQ();
		break;
	}

	/* Update the led each time a command is processed */
	Statusbar_EnableHDLed( LED_STATE_ON );
}


/*---------------------------------------------------------------------*/
/**
 * Debug routine for HDC command packets.
 */
#ifdef HDC_REALLY_VERBOSE
static void HDC_DebugCommandPacket(FILE *hdlogFile, SCSI_CTRLR *ctr)
{
	int opcode;
	static const char *psComNames[] =
	{
		"TEST UNIT READY",         // 0x00
		"REZERO",                  // 0x01
		"???",                     // 0x02
		"REQUEST SENSE",           // 0x03
		"FORMAT DRIVE",            // 0x04
		"VERIFY TRACK (?)",        // 0x05
		"FORMAT TRACK (?)",        // 0x06
		"REASSIGN BLOCK",          // 0x07
		"READ SECTOR(S)",          // 0x08
		"???",                     // 0x09
		"WRITE SECTOR(S)",         // 0x0A
		"SEEK",                    // 0x0B
		"???",                     // 0x0C
		"CORRECTION",              // 0x0D
		"???",                     // 0x0E
		"TRANSLATE",               // 0x0F
		"SET ERROR THRESHOLD",     // 0x10
		"USAGE COUNTERS",          // 0x11
		"INQUIRY",                 // 0x12
		"WRITE DATA BUFFER",       // 0x13
		"READ DATA BUFFER",        // 0x14
		"MODE SELECT",             // 0x15
		"???",                     // 0x16
		"???",                     // 0x17
		"EXTENDED READ",           // 0x18
		"READ TOC",                // 0x19
		"MODE SENSE",              // 0x1A
		"SHIP",                    // 0x1B
		"RECEIVE DIAGNOSTICS",     // 0x1C
		"SEND DIAGNOSTICS",        // 0x1D
		"???",                     // 0x1E
		"SET TARGET (EXTENDED)",   // 0x1F
		"???",                     // 0x20
		"???",                     // 0x21
		"???",                     // 0x22
		"???",                     // 0x23
		"???",                     // 0x24
		"READ CAPACITY",           // 0x25
		"???",                     // 0x26
		"???",                     // 0x27
		"READ SECTOR(S)",          // 0x28
		"???",                     // 0x29
		"WRITE SECTOR(S)",         // 0x2A
	};

	opcode = ctr->opcode;

	fprintf(hdlogFile,"----\n");

	if (opcode >= 0 && opcode <= (int)(sizeof(psComNames)/sizeof(psComNames[0])))
	{
		fprintf(hdlogFile, "HDC opcode 0x%x : %s\n",opcode,psComNames[opcode]);
	}
	else
	{
		fprintf(hdlogFile, "Unknown HDC opcode!! Value = 0x%x\n", opcode);
	}

	fprintf(hdlogFile, "Target: %i\n", ctr->target);
	fprintf(hdlogFile, "LUN: %i\n", HDC_GetLUN(ctr));
	fprintf(hdlogFile, "LBA: 0x%lx\n", HDC_GetLBA(ctr));

	fprintf(hdlogFile, "Sector count: 0x%x\n", HDC_GetCount(ctr));
	fprintf(hdlogFile, "Control byte: 0x%x\n", HDC_GetControl(ctr));
}
#endif


/*---------------------------------------------------------------------*/
/**
 * Print data about the hard drive image
 */
static void HDC_GetInfo(SCSI_DEV *dev)
{
/* Partition table contains hd size + 4 partition entries
 * (composed of flag byte, 3 char ID, start offset and size),
 * this is followed by bad sector list + count and the root sector checksum.
 * Before this there's the boot code and with ICD hd driver additional 8
 * partition entries (at offset 0x156).
 */
#define HD_PARTITIONTABLE_SIZE (4+4*12)
#define HD_PARTITIONTABLE_OFFSET 0x1C2
	long offset;
	unsigned char hdinfo[HD_PARTITIONTABLE_SIZE];
	int i;

	nPartitions = 0;
	if (dev->image_file == NULL)
		return;
	offset = ftell(dev->image_file);

	fseek(dev->image_file, HD_PARTITIONTABLE_OFFSET, 0);
	if (fread(hdinfo, HD_PARTITIONTABLE_SIZE, 1, dev->image_file) != 1)
	{
		perror("HDC_GetInfo");
		return;
	}

#ifdef HDC_VERBOSE
	fprintf(stderr, "Total disk size according to MBR: %i Mb\n",
		HDC_ReadInt32(hdinfo, 0) >> 11);
	/* flags for each partition entry are zero if they are not valid */
	fprintf(stderr, "Partition 0 exists?: %s\n", (hdinfo[4] != 0)?"Yes":"No");
	fprintf(stderr, "Partition 1 exists?: %s\n", (hdinfo[4+12] != 0)?"Yes":"No");
	fprintf(stderr, "Partition 2 exists?: %s\n", (hdinfo[4+24] != 0)?"Yes":"No");
	fprintf(stderr, "Partition 3 exists?: %s\n", (hdinfo[4+36] != 0)?"Yes":"No");
#endif

	for(i=0;i<4;i++)
		if(hdinfo[4 + 12*i])
			nPartitions++;

	fseek(dev->image_file, offset, 0);
}


/**
 * Open the disk image file, set partitions.
 */
bool HDC_Init(void)
{
	char *filename;
	bAcsiEmuOn = false;
	off_t filesize;

	memset(&AcsiBus, 0, sizeof(AcsiBus));

	if (!ConfigureParams.HardDisk.bUseHardDiskImage)
		return false;
	filename = ConfigureParams.HardDisk.szHardDiskImage;

	/* Get size and do a sanity check - is file length a multiple of 512? */
	filesize = File_Length(filename);
	if (filesize <= 0 || (filesize & 0x1ff) != 0)
	{
		Log_Printf(LOG_ERROR, "HD file '%s' has strange size!\n", filename);
		return false;
	}
	AcsiBus.devs[0].hdSize = filesize / 512;

	if ((AcsiBus.devs[0].image_file = fopen(filename, "rb+")) == NULL)
	{
		Log_Printf(LOG_ERROR, "Can not open HD file '%s'!\n", filename);
		return false;
	}

	HDC_GetInfo(&AcsiBus.devs[0]);

	/* set number of partitions */
	nNumDrives += nPartitions;

	bAcsiEmuOn = true;

	printf("Hard drive image %s mounted.\n", filename);
	return true;
}


/*---------------------------------------------------------------------*/
/**
 * HDC_UnInit - close image file
 *
 */
void HDC_UnInit(void)
{
	if (!bAcsiEmuOn)
		return;

	fclose(AcsiBus.devs[0].image_file);
	AcsiBus.devs[0].image_file = NULL;

	nNumDrives -= nPartitions;
	nPartitions = 0;
	bAcsiEmuOn = false;
}


/*---------------------------------------------------------------------*/
/**
 * Reset command status.
 */
void HDC_ResetCommandStatus(void)
{
	if (ConfigureParams.System.nMachineType != MACHINE_FALCON)
		AcsiBus.returnCode = 0;
}


/**
 * Get sector count -- FIXME: seems not available for HDC?
 */
short int HDC_GetSectorCount(void)
{
	// fprintf(stderr,"HDC: Get sector count?\n");
	return 0;
}


/**
 * Process HDC command packets (SCSI/ACSI) bytes.
 */
static void HDC_WriteCommandPacket(SCSI_CTRLR *ctr, Uint8 b)
{
	SCSI_DEV *dev = &ctr->devs[ctr->target];

	/* We only support one target with ID 0 */
	if (ctr->target != 0)
	{
		//FDC_SetDMAStatus(true);
		//FDC_SetIRQ();
		/* If there's no controller, the interrupt line stays high */
		ctr->returnCode = HD_STATUS_ERROR;
		MFP_GPIP |= 0x20;
		return;
	}

	/* Extract ACSI/SCSI opcode */
	if (ctr->byteCount == 0)
	{
		ctr->opcode = b;
	}

	/* Successfully received one byte, and increase the byte-count */
	if (ctr->byteCount < (int)sizeof(ctr->command))
		ctr->command[ctr->byteCount] = b;
	++ctr->byteCount;

	/* have we received a complete 6-byte class 0 or 10-byte class 1 packet yet? */
	if ((ctr->opcode < 0x20 && ctr->byteCount >= 6) ||
	    (ctr->opcode < 0x60 && ctr->byteCount >= 10))
	{
#ifdef HDC_REALLY_VERBOSE
		HDC_DebugCommandPacket(stderr, ctr);
#endif
		/* We currently only support LUN 0, however INQUIRY must
		 * always be handled, see SCSI standard */
		if (HDC_GetLUN(ctr) == 0 || ctr->opcode == HD_INQUIRY)
		{
			HDC_EmulateCommandPacket(ctr);
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC: Access to non-existing LUN."
				   " Command = 0x%02x\n", ctr->opcode);
			dev->nLastError = HD_REQSENS_INVLUN;
			/* REQUEST SENSE is still handled for invalid LUNs */
			if (ctr->opcode == HD_REQ_SENSE)
				HDC_Cmd_RequestSense(ctr);
			else
				ctr->returnCode = HD_STATUS_ERROR;
		}

		ctr->byteCount = 0;
	}
	else if (ctr->opcode >= 0x60)
	{
		/* Commands >= 0x60 are not supported right now */
		ctr->returnCode = HD_STATUS_ERROR;
		dev->nLastError = HD_REQSENS_OPCODE;
		dev->bSetLastBlockAddr = false;
		FDC_SetIRQ();
		FDC_SetDMAStatus(false);
#ifdef HDC_VERBOSE
		if (ctr->byteCount == 10)
			Log_Printf(LOG_WARN, "HDC: Unsupported command 0x%02x\n",
			           ctr->opcode);
#endif
	}
	else
	{
		FDC_SetIRQ();
		FDC_SetDMAStatus(false);
		ctr->returnCode = HD_STATUS_OK;
	}
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
}

/**
 * Read a command byte from the NCR 5380 SCSI controller
 */
static Uint8 Ncr5380_ReadByte(int addr)
{
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

	return 0;
}


static void Acsi_WriteCommandByte(int addr, Uint8 byte)
{
	/* When the A1 pin is pushed to 0, we want to start a new command */
	if ((addr & 2) == 0)
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
			FDC_SetIRQ();
			FDC_SetDMAStatus(false);
			AcsiBus.returnCode = HD_STATUS_OK;
		}
		return;
	}

	HDC_WriteCommandPacket(&AcsiBus, byte);
}

/**
 * Called when command bytes have been written to $FFFF8606 and
 * the HDC (not the FDC) is selected.
 */
void HDC_WriteCommandByte(int addr, Uint8 byte)
{
	// fprintf(stderr, "HDC: Write cmd byte addr=%i, byte=%02x\n", addr, byte);

	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
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
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON)
		ret = Ncr5380_ReadByte(addr);
	else
		ret = AcsiBus.returnCode;	/* ACSI status */
	return ret;
}
