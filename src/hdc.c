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
 *  Structure representing an ACSI command block.
 */
typedef struct {
  int readCount;    /* count of number of command bytes written */
  unsigned char target;
  unsigned char opcode;
  bool extended;

  int byteCount;             /* count of number of command bytes written */
  unsigned char command[10];
  short int returnCode;      /* return code from the HDC operation */
} HDCOMMAND;

/* HDC globals */
HDCOMMAND HDCCommand;
int nPartitions = 0;
unsigned long hdSize = 0;
short int HDCSectorCount;
bool bAcsiEmuOn = false;

static FILE *hd_image_file = NULL;
static Uint32 nLastBlockAddr;
static bool bSetLastBlockAddr;
static Uint8 nLastError;

/*
  FDC registers used:
  - FDCSectorCountRegister
*/


/* Our dummy INQUIRY response data */
static unsigned char inquiry_bytes[] =
{
	0,                /* device type 0 = direct access device */
	0,                /* device type qualifier (nonremovable) */
	1,                /* ANSI version */
	0,                /* reserved */
	26,               /* length of the following data */
	' ', ' ', ' ',                         /* Vendor specific data */
	'H','a','t','a','r','i',' ',' ',       /* Vendor */
	'E','m','u','l','a','t','e','d',       /* Model */
	' ',' ',' ',' ',                       /* Revision */
	0,0,0,0,0,0,0,0,0,0                    /* ?? */
};


/*---------------------------------------------------------------------*/
/**
 * Return the device specified in the current ACSI command block.
 */
static unsigned char HDC_GetDevice(void)
{
	return (HDCCommand.command[1] & 0xE0) >> 5;
}

/**
 * Return the file offset of the sector specified in the current ACSI command block.
 */
static unsigned long HDC_GetOffset(void)
{
	/* offset = logical block address * 512 */
	return HDCCommand.opcode < 0x20?
		// class 0
		(HDC_ReadInt24(HDCCommand.command, 1) & 0x1FFFFF) << 9 :
		// class 1
		HDC_ReadInt32(HDCCommand.command, 2) << 9;
}

/**
 * Return the count specified in the current ACSI command block.
 */
static int HDC_GetCount(void)
{
	return HDCCommand.opcode < 0x20?
		// class 0
		HDCCommand.command[4] :
		// class1
		HDC_ReadInt16(HDCCommand.command, 7);
}

/**
 * Return the control byte specified in the current ACSI command block.
 */
#ifdef HDC_REALLY_VERBOSE
static unsigned char HDC_GetControl(void)
{
	return HDCCommand.opcode < 0x20?
		// class 0
		HDCCommand.command[5] :
		// class1
		HDCCommand.command[9];
}
#endif


/*---------------------------------------------------------------------*/
/**
 * Seek - move to a sector
 */
static void HDC_Cmd_Seek(void)
{
	nLastBlockAddr = HDC_GetOffset();

	if (fseek(hd_image_file, nLastBlockAddr, SEEK_SET) == 0)
	{
		HDCCommand.returnCode = HD_STATUS_OK;
		nLastError = HD_REQSENS_OK;
	}
	else
	{
		HDCCommand.returnCode = HD_STATUS_ERROR;
		nLastError = HD_REQSENS_INVADDR;
	}

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_AcknowledgeInterrupt();
	bSetLastBlockAddr = true;
	//FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/**
 * Inquiry - return some disk information
 */
static void HDC_Cmd_Inquiry(void)
{
	Uint32 nDmaAddr;
	int count;

	nDmaAddr = FDC_GetDMAAddress();
	count = HDC_GetCount();

#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Inquiry, %i bytes to 0x%x.\n", count, nDmaAddr);
#endif

	if (count > (int)sizeof(inquiry_bytes))
		count = sizeof(inquiry_bytes);

	inquiry_bytes[4] = count - 8;

	if (STMemory_SafeCopy(nDmaAddr, inquiry_bytes, count, "HDC DMA inquiry"))
		HDCCommand.returnCode = HD_STATUS_OK;
	else
		HDCCommand.returnCode = HD_STATUS_ERROR;

	FDC_WriteDMAAddress(nDmaAddr + count);

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_AcknowledgeInterrupt();
	nLastError = HD_REQSENS_OK;
	bSetLastBlockAddr = false;
	//FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/**
 * Request sense - return some disk information
 */
static void HDC_Cmd_RequestSense(void)
{
	Uint32 nDmaAddr;
	int nRetLen;
	Uint8 retbuf[22];

#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Request Sense.\n");
#endif

	nRetLen = HDC_GetCount();

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
		retbuf[0] = nLastError;
		if (bSetLastBlockAddr)
		{
			retbuf[0] |= 0x80;
			retbuf[1] = nLastBlockAddr >> 16;
			retbuf[2] = nLastBlockAddr >> 8;
			retbuf[3] = nLastBlockAddr;
		}
	}
	else
	{
		retbuf[0] = 0x70;
		if (bSetLastBlockAddr)
		{
			retbuf[0] |= 0x80;
			retbuf[4] = nLastBlockAddr >> 16;
			retbuf[5] = nLastBlockAddr >> 8;
			retbuf[6] = nLastBlockAddr;
		}
		switch (nLastError)
		{
		 case HD_REQSENS_OK:  retbuf[2] = 0; break;
		 case HD_REQSENS_OPCODE:  retbuf[2] = 5; break;
		 case HD_REQSENS_INVADDR:  retbuf[2] = 5; break;
		 case HD_REQSENS_INVARG:  retbuf[2] = 5; break;
		 case HD_REQSENS_NODRIVE:  retbuf[2] = 2; break;
		 default: retbuf[2] = 4; break;
		}
		retbuf[7] = 14;
		retbuf[12] = nLastError;
		retbuf[19] = nLastBlockAddr >> 16;
		retbuf[20] = nLastBlockAddr >> 8;
		retbuf[21] = nLastBlockAddr;
	}

	/*
	fprintf(stderr,"*** Requested sense packet:\n");
	int i;
	for (i = 0; i<nRetLen; i++) fprintf(stderr,"%2x ",retbuf[i]);
	fprintf(stderr,"\n");
	*/

	if (STMemory_SafeCopy(nDmaAddr, retbuf, nRetLen, "HDC request sense"))
		HDCCommand.returnCode = HD_STATUS_OK;
	else
		HDCCommand.returnCode = HD_STATUS_ERROR;

	FDC_WriteDMAAddress(nDmaAddr + nRetLen);

	FDC_SetDMAStatus(false);            /* no DMA error */
	FDC_AcknowledgeInterrupt();
	//FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/**
 * Mode sense - Get parameters from disk.
 * (Just enough to make the HDX tool from AHDI 5.0 happy)
 */
static void HDC_Cmd_ModeSense(void)
{
	Uint32 nDmaAddr;

#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Mode Sense.\n");
#endif

	nDmaAddr = FDC_GetDMAAddress();

	if (!STMemory_ValidArea(nDmaAddr, 16))
	{
		Log_Printf(LOG_WARN, "HCD mode sense uses invalid RAM range 0x%x+%i\n", nDmaAddr, 16);
		HDCCommand.returnCode = HD_STATUS_ERROR;
	}
	else if (HDCCommand.command[2] == 0 && HDC_GetCount() == 0x10)
	{
		size_t blocks;
		blocks = File_Length(ConfigureParams.HardDisk.szHardDiskImage) / 512;

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

		HDCCommand.returnCode = HD_STATUS_OK;
		nLastError = HD_REQSENS_OK;
	}
	else
	{
		Log_Printf(LOG_TODO, "HDC: Unsupported MODE SENSE command\n");
		HDCCommand.returnCode = HD_STATUS_ERROR;
		nLastError = HD_REQSENS_INVARG;
	}

	FDC_SetDMAStatus(false);            /* no DMA error */
	FDC_AcknowledgeInterrupt();
	bSetLastBlockAddr = false;
	//FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/**
 * Format drive.
 */
static void HDC_Cmd_FormatDrive(void)
{
#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Format drive!\n");
#endif

	/* Should erase the whole image file here... */

	FDC_SetDMAStatus(false);            /* no DMA error */
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_OK;
	nLastError = HD_REQSENS_OK;
	bSetLastBlockAddr = false;
	//FDCSectorCountRegister = 0;
}

/*---------------------------------------------------------------------*/
/**
 * Read capacity of our disk.
 */
static void HDC_Cmd_ReadCapacity(void)
{
	Uint32 nDmaAddr = FDC_GetDMAAddress();

#ifdef HDC_VERBOSE
	fprintf(stderr,"Reading 8 bytes capacity data to addr: 0x%x\n", nDmaAddr);
#endif

	/* seek to the position */
	if (STMemory_ValidArea(nDmaAddr, 8))
	{
		int nSectors = hdSize / 512;
		STRam[nDmaAddr++] = (nSectors >> 24) & 0xFF;
		STRam[nDmaAddr++] = (nSectors >> 16) & 0xFF;
		STRam[nDmaAddr++] = (nSectors >> 8) & 0xFF;
		STRam[nDmaAddr++] = (nSectors) & 0xFF;
		STRam[nDmaAddr++] = 0x00;
		STRam[nDmaAddr++] = 0x00;
		STRam[nDmaAddr++] = 0x02;
		STRam[nDmaAddr++] = 0x00;

		/* Update DMA counter */
		FDC_WriteDMAAddress(nDmaAddr + 8);

		HDCCommand.returnCode = HD_STATUS_OK;
		nLastError = HD_REQSENS_OK;
	}
	else
	{
		Log_Printf(LOG_WARN, "HDC capacity read uses invalid RAM range 0x%x+%i\n", nDmaAddr, 8);
		HDCCommand.returnCode = HD_STATUS_ERROR;
		nLastError = HD_REQSENS_NOSECTOR;
	}

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_AcknowledgeInterrupt();
	bSetLastBlockAddr = false;
	//FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/**
 * Write a sector off our disk - (seek implied)
 */
static void HDC_Cmd_WriteSector(void)
{
	int n = 0;

	nLastBlockAddr = HDC_GetOffset();

	/* seek to the position */
	if (fseek(hd_image_file, nLastBlockAddr, SEEK_SET) != 0)
	{
		HDCCommand.returnCode = HD_STATUS_ERROR;
		nLastError = HD_REQSENS_INVADDR;
	}
	else
	{
		/* write - if allowed */
		Uint32 nDmaAddr = FDC_GetDMAAddress();
#ifndef DISALLOW_HDC_WRITE
		if (STMemory_ValidArea(nDmaAddr, 512*HDC_GetCount()))
		{
			n = fwrite(&STRam[nDmaAddr], 512,
				   HDC_GetCount(), hd_image_file);
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC sector write uses invalid RAM range 0x%x+%i\n",
				   nDmaAddr, 512*HDC_GetCount());
		}
#endif
		if (n == HDC_GetCount())
		{
			HDCCommand.returnCode = HD_STATUS_OK;
			nLastError = HD_REQSENS_OK;
		}
		else
		{
			HDCCommand.returnCode = HD_STATUS_ERROR;
			nLastError = HD_REQSENS_WRITEERR;
		}

		/* Update DMA counter */
		FDC_WriteDMAAddress(nDmaAddr + 512*n);
	}

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_AcknowledgeInterrupt();
	bSetLastBlockAddr = true;
	//FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/**
 * Read a sector off our disk - (implied seek)
 */
static void HDC_Cmd_ReadSector(void)
{
	int n;

	nLastBlockAddr = HDC_GetOffset();

#ifdef HDC_VERBOSE
	fprintf(stderr,"Reading %i sectors from 0x%x to addr: 0x%x\n",
	        HDC_GetCount(), nLastBlockAddr, FDC_GetDMAAddress());
#endif

	/* seek to the position */
	if (fseek(hd_image_file, nLastBlockAddr, SEEK_SET) != 0)
	{
		HDCCommand.returnCode = HD_STATUS_ERROR;
		nLastError = HD_REQSENS_INVADDR;
	}
	else
	{
		Uint32 nDmaAddr = FDC_GetDMAAddress();
		if (STMemory_ValidArea(nDmaAddr, 512*HDC_GetCount()))
		{
			n = fread(&STRam[nDmaAddr], 512,
				   HDC_GetCount(), hd_image_file);
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC sector read uses invalid RAM range 0x%x+%i\n",
				   nDmaAddr, 512*HDC_GetCount());
			n = 0;
		}
		if (n == HDC_GetCount())
		{
			HDCCommand.returnCode = HD_STATUS_OK;
			nLastError = HD_REQSENS_OK;
		}
		else
		{
			HDCCommand.returnCode = HD_STATUS_ERROR;
			nLastError = HD_REQSENS_NOSECTOR;
		}

		/* Update DMA counter */
		FDC_WriteDMAAddress(nDmaAddr + 512*n);
	}

	FDC_SetDMAStatus(false);              /* no DMA error */
	FDC_AcknowledgeInterrupt();
	bSetLastBlockAddr = true;
	//FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/**
 * Test unit ready
 */
static void HDC_Cmd_TestUnitReady(void)
{
	FDC_SetDMAStatus(false);            /* no DMA error */
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_OK;
}


/*---------------------------------------------------------------------*/
/**
 * Emulation routine for HDC command packets.
 */
static void HDC_EmulateCommandPacket(void)
{

	switch(HDCCommand.opcode)
	{

	 case HD_TEST_UNIT_RDY:
		HDC_Cmd_TestUnitReady();
		break;

	case HD_READ_CAPACITY1:
		HDC_Cmd_ReadCapacity();
		break;

	 case HD_READ_SECTOR:
	 case HD_READ_SECTOR1:
		HDC_Cmd_ReadSector();
		break;

	 case HD_WRITE_SECTOR:
	 case HD_WRITE_SECTOR1:
		HDC_Cmd_WriteSector();
		break;

	 case HD_INQUIRY:
		HDC_Cmd_Inquiry();
		break;

	 case HD_SEEK:
		HDC_Cmd_Seek();
		break;

	 case HD_SHIP:
		HDCCommand.returnCode = 0xFF;
		FDC_AcknowledgeInterrupt();
		break;

	 case HD_REQ_SENSE:
		HDC_Cmd_RequestSense();
		break;

	 case HD_MODESELECT:
		Log_Printf(LOG_TODO, "HDC: MODE SELECT call not implemented yet.\n");
		HDCCommand.returnCode = HD_STATUS_OK;
		nLastError = HD_REQSENS_OK;
		bSetLastBlockAddr = false;
		FDC_SetDMAStatus(false);
		FDC_AcknowledgeInterrupt();
		break;

	 case HD_MODESENSE:
		HDC_Cmd_ModeSense();
		break;

	 case HD_FORMAT_DRIVE:
		HDC_Cmd_FormatDrive();
		break;

	 /* as of yet unsupported commands */
	 case HD_VERIFY_TRACK:
	 case HD_FORMAT_TRACK:
	 case HD_CORRECTION:

	 default:
		HDCCommand.returnCode = HD_STATUS_ERROR;
		nLastError = HD_REQSENS_OPCODE;
		bSetLastBlockAddr = false;
		FDC_AcknowledgeInterrupt();
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
static void HDC_DebugCommandPacket(FILE *hdlogFile)
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

	opcode = HDCCommand.opcode;

	fprintf(hdlogFile,"----\n");

	if (opcode >= 0 && opcode <= (int)(sizeof(psComNames)/sizeof(psComNames[0])))
	{
		fprintf(hdlogFile, "HDC opcode 0x%x : %s\n",opcode,psComNames[opcode]);
	}
	else
	{
		fprintf(hdlogFile, "Unknown HDC opcode!! Value = 0x%x\n", opcode);
	}

	fprintf(hdlogFile, "Target: %i\n", HDCCommand.target);
	fprintf(hdlogFile, "Device: %i\n", HDC_GetDevice());
	fprintf(hdlogFile, "LBA: 0x%lx\n", HDC_GetOffset()/512);

	fprintf(hdlogFile, "Sector count: 0x%x\n", HDC_GetCount());
	fprintf(hdlogFile, "HDC sector count: 0x%x\n", HDCSectorCount);
	//fprintf(hdlogFile, "FDC sector count: 0x%x\n", FDCSectorCountRegister);
	fprintf(hdlogFile, "Control byte: 0x%x\n", HDC_GetControl());
}
#endif


/*---------------------------------------------------------------------*/
/**
 * Print data about the hard drive image
 */
static void HDC_GetInfo(void)
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
	if (hd_image_file == NULL)
		return;
	offset = ftell(hd_image_file);

	fseek(hd_image_file, HD_PARTITIONTABLE_OFFSET, 0);
	if (fread(hdinfo, HD_PARTITIONTABLE_SIZE, 1, hd_image_file) != 1)
	{
		perror("HDC_GetInfo");
		return;
	}

	hdSize = HDC_ReadInt32(hdinfo, 0);

#ifdef HDC_VERBOSE
	fprintf(stderr, "Total disk size %li Mb\n", hdSize>>11);
	/* flags for each partition entry are zero if they are not valid */
	fprintf(stderr, "Partition 0 exists?: %s\n", (hdinfo[4] != 0)?"Yes":"No");
	fprintf(stderr, "Partition 1 exists?: %s\n", (hdinfo[4+12] != 0)?"Yes":"No");
	fprintf(stderr, "Partition 2 exists?: %s\n", (hdinfo[4+24] != 0)?"Yes":"No");
	fprintf(stderr, "Partition 3 exists?: %s\n", (hdinfo[4+36] != 0)?"Yes":"No");
#endif

	for(i=0;i<4;i++)
		if(hdinfo[4 + 12*i])
			nPartitions++;

	fseek(hd_image_file, offset, 0);
}


/*---------------------------------------------------------------------*/
/**
 * Open the disk image file, set partitions.
 */
bool HDC_Init(void)
{
	char *filename;
	bAcsiEmuOn = false;

	if (!ConfigureParams.HardDisk.bUseHardDiskImage)
		return false;
	filename = ConfigureParams.HardDisk.szHardDiskImage;

	/* Sanity check - is file length a multiple of 512? */
	if (File_Length(filename) & 0x1ff)
	{
		Log_Printf(LOG_ERROR, "HD file '%s' has strange size!\n", filename);
		return false;
	}

	if ((hd_image_file = fopen(filename, "rb+")) == NULL)
	{
		Log_Printf(LOG_ERROR, "Can not open HD file '%s'!\n", filename);
		return false;
	}

	HDC_GetInfo();

	/* set number of partitions */
	nNumDrives += nPartitions;

	bAcsiEmuOn = true;
	HDCCommand.readCount = 0;
	HDCCommand.byteCount = 0;

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

	fclose(hd_image_file);
	hd_image_file = NULL;

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
	/*HDCCommand.byteCount = 0;*/  /* Not done on real ST? */
	HDCCommand.returnCode = 0;
}


/**
 * Get command status.
 */
short int HDC_GetCommandStatus(void)
{
	return HDCCommand.returnCode;
}


/*---------------------------------------------------------------------*/
/**
 * Get sector count.
 */
short int HDC_GetSectorCount(void)
{
	return HDCSectorCount;
}


/*---------------------------------------------------------------------*/
/**
 * Process HDC command packets, called when bytes are 
 * written to $FFFF8606 and the HDC (not the FDC) is selected.
 */
void HDC_WriteCommandPacket(void)
{
	unsigned char b;

	/* is HDC emulation enabled? */
	if (!bAcsiEmuOn)
		return;

	/* command byte sent */
	b = IoMem_ReadByte(0xff8605);

	/* Extract target and extended mode early, read acsi opcode */
	if (HDCCommand.readCount == 0)
	{
		HDCCommand.target = ((b & 0xE0) >> 5);
		HDCCommand.opcode = (b & 0x1F);
		HDCCommand.extended = (HDCCommand.opcode == 0x1F);
	}
	/* In extended mode, the scsi opcode is at position 1 */
	else if (HDCCommand.extended && HDCCommand.readCount == 1)
	{
		HDCCommand.opcode = (b & 0xFF);
	}

	/* We only support one target with ID 0 */
	if (HDCCommand.target != 0)
	{
		//FDC_SetDMAStatus(true);
		//FDC_AcknowledgeInterrupt();
		//FDCSectorCountRegister = 0;
		/* If there's no controller, the interrupt line stays high */
		HDCCommand.returnCode = HD_STATUS_ERROR;
		MFP_GPIP |= 0x20;
		return;
	}

	/* Successfully received one byte, so increase the byte-count, but in extended mode skip the first byte */
	if (!HDCCommand.extended || HDCCommand.readCount != 0)
	{
		HDCCommand.command[HDCCommand.byteCount++] = b;
	}
	++HDCCommand.readCount;

	/* have we received a complete 6-byte class 0 or 10-byte class 1 packet yet? */
	if ((HDCCommand.opcode < 0x20 && HDCCommand.byteCount >= 6) ||
		(HDCCommand.opcode < 0x40 && HDCCommand.byteCount >= 10))
	{
#ifdef HDC_REALLY_VERBOSE
		HDC_DebugCommandPacket(stderr);
#endif
		/* If it's aimed for our drive, emulate it! */
		if (HDC_GetDevice() == 0)
		{
			HDC_EmulateCommandPacket();
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC: Access to non-existing drive.\n");
			HDCCommand.returnCode = HD_STATUS_ERROR;
		}

		HDCCommand.readCount = 0;
		HDCCommand.byteCount = 0;
	}
	else
	{
		FDC_AcknowledgeInterrupt();
		FDC_SetDMAStatus(false);
		HDCCommand.returnCode = HD_STATUS_OK;
	}
}
