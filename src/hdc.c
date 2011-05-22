/*
  Hatari - hdc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Low-level hard drive emulation
*/
const char HDC_fileid[] = "Hatari hdc.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "debugui.h"
#include "file.h"
#include "fdc.h"
#include "hdc.h"
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

  (For simplicity, the operation is finished immediatly,
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

/* HDC globals */
HDCOMMAND HDCCommand;
int nPartitions = 0;
short int HDCSectorCount;
bool bAcsiEmuOn = false;

static FILE *hd_image_file = NULL;
static Uint32 nLastBlockAddr;
static bool bSetLastBlockAddr;
static Uint8 nLastError;

/*
  FDC registers used:
  - FDCSectorCountRegister
  - DiskControllerStatus_ff8604rd
  - DMAModeControl_ff8606wr
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
 * Return the file offset of the sector specified in the current
 * ACSI command block.
 */
static unsigned long HDC_GetOffset(void)
{
	unsigned long offset;

	/* construct the logical block adress */
	offset = ((HD_LBA_MSB(HDCCommand) << 16)
	          |  (HD_LBA_MID(HDCCommand)  << 8)
	          |  (HD_LBA_LSB(HDCCommand))) ;

	/* return value in bytes */
	return(offset * 512);
}


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

	nDmaAddr = FDC_ReadDMAAddress();
	count = HD_SECTORCOUNT(HDCCommand);

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

	nRetLen = HD_SECTORCOUNT(HDCCommand);

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

	nDmaAddr = FDC_ReadDMAAddress();

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

	nDmaAddr = FDC_ReadDMAAddress();

	if (!STMemory_ValidArea(nDmaAddr, 16))
	{
		Log_Printf(LOG_WARN, "HCD mode sense uses invalid RAM range 0x%x+%i\n", nDmaAddr, 16);
		HDCCommand.returnCode = HD_STATUS_ERROR;
	}
	else if (HDCCommand.command[2] == 0 && HD_SECTORCOUNT(HDCCommand) == 0x10)
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
		Uint32 nDmaAddr = FDC_ReadDMAAddress();
#ifndef DISALLOW_HDC_WRITE
		if (STMemory_ValidArea(nDmaAddr, 512*HD_SECTORCOUNT(HDCCommand)))
		{
			n = fwrite(&STRam[nDmaAddr], 512,
				   HD_SECTORCOUNT(HDCCommand), hd_image_file);
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC sector write uses invalid RAM range 0x%x+%i\n",
				   nDmaAddr, 512*HD_SECTORCOUNT(HDCCommand));
		}
#endif
		if (n == HD_SECTORCOUNT(HDCCommand))
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
	        HD_SECTORCOUNT(HDCCommand), nLastBlockAddr, FDC_ReadDMAAddress());
#endif

	/* seek to the position */
	if (fseek(hd_image_file, nLastBlockAddr, SEEK_SET) != 0)
	{
		HDCCommand.returnCode = HD_STATUS_ERROR;
		nLastError = HD_REQSENS_INVADDR;
	}
	else
	{
		Uint32 nDmaAddr = FDC_ReadDMAAddress();
		if (STMemory_ValidArea(nDmaAddr, 512*HD_SECTORCOUNT(HDCCommand)))
		{
			n = fread(&STRam[nDmaAddr], 512,
				   HD_SECTORCOUNT(HDCCommand), hd_image_file);
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC sector read uses invalid RAM range 0x%x+%i\n",
				   nDmaAddr, 512*HD_SECTORCOUNT(HDCCommand));
			n = 0;
		}
		if (n == HD_SECTORCOUNT(HDCCommand))
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

	switch(HD_OPCODE(HDCCommand))
	{

	 case HD_TEST_UNIT_RDY:
		HDC_Cmd_TestUnitReady();
		break;

	 case HD_READ_SECTOR:
		HDC_Cmd_ReadSector();
		break;
	 case HD_WRITE_SECTOR:
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
	Statusbar_EnableHDLed();
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
		"TEST UNIT READY",
		"REZERO",
		"???",
		"REQUEST SENSE",
		"FORMAT DRIVE",
		"VERIFY TRACK (?)",
		"FORMAT TRACK (?)",
		"REASSIGN BLOCK",
		"READ SECTOR(S)",
		"???",
		"WRITE SECTOR(S)",
		"SEEK",
		"???",
		"CORRECTION",
		"???",
		"TRANSLATE",
		"SET ERROR THRESHOLD",	/* 0x10 */
		"USAGE COUNTERS",
		"INQUIRY",
		"WRITE DATA BUFFER",
		"READ DATA BUFFER",
		"MODE SELECT",
		"???",
		"???",
		"EXTENDED READ",
		"READ TOC",
		"MODE SENSE",
		"SHIP",
		"RECEIVE DIAGNOSTICS",
		"SEND DIAGNOSTICS"
	};

	opcode = HD_OPCODE(HDCCommand);

	fprintf(hdlogFile,"----\n");

	if (opcode >= 0 && opcode <= (int)(sizeof(psComNames)/sizeof(psComNames[0])))
	{
		fprintf(hdlogFile, "HDC opcode 0x%x : %s\n",opcode,psComNames[opcode]);
	}
	else
	{
		fprintf(hdlogFile, "Unknown HDC opcode!! Value = 0x%x\n", opcode);
	}

	fprintf(hdlogFile, "Target: %i\n", HD_TARGET(HDCCommand));
	fprintf(hdlogFile, "Device: %i\n", HD_DEVICE(HDCCommand));
	fprintf(hdlogFile, "LBA: 0x%lx\n", HDC_GetOffset()/512);

	fprintf(hdlogFile, "Sector count: 0x%x\n", HD_SECTORCOUNT(HDCCommand));
	fprintf(hdlogFile, "HDC sector count: 0x%x\n", HDCSectorCount);
	//fprintf(hdlogFile, "FDC sector count: 0x%x\n", FDCSectorCountRegister);
	fprintf(hdlogFile, "Control byte: 0x%x\n", HD_CONTROL(HDCCommand));
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
#ifdef HDC_VERBOSE
	unsigned long size;
#endif

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

#ifdef HDC_VERBOSE
	size = (((unsigned long) hdinfo[0] << 24)
	        | ((unsigned long) hdinfo[1] << 16)
	        | ((unsigned long) hdinfo[2] << 8)
	        | ((unsigned long) hdinfo[3]));

	fprintf(stderr, "Total disk size %li Mb\n", size>>11);
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
 * Process HDC command packets, called when bytes are 
 * written to $FFFF8606 and the HDC (not the FDC) is selected.
 */
void HDC_WriteCommandPacket(void)
{
	/* is HDC emulation enabled? */
	if (!bAcsiEmuOn)
		return;

	/* command byte sent, store it. */
	HDCCommand.command[HDCCommand.byteCount] = (DiskControllerWord_ff8604wr&0xFF);

	/* We only support one target with ID 0 */
	if (HD_TARGET(HDCCommand) != 0)
	{
		//FDC_SetDMAStatus(true);
		//FDC_AcknowledgeInterrupt();
		//FDCSectorCountRegister = 0;
		/* If there's no controller, the interrupt line stays high */
		HDCCommand.returnCode = HD_STATUS_ERROR;
		MFP_GPIP |= 0x20;
		return;
	}

	/* Successfully received one byte, so increase the byte-count */
	++HDCCommand.byteCount;

	/* have we received a complete 6-byte packet yet? */
	if (HDCCommand.byteCount >= 6)
	{
#ifdef HDC_REALLY_VERBOSE
		HDC_DebugCommandPacket(stderr);
#endif
		/* If it's aimed for our drive, emulate it! */
		if (HD_DEVICE(HDCCommand) == 0)
		{
			HDC_EmulateCommandPacket();
		}
		else
		{
			Log_Printf(LOG_WARN, "HDC: Access to non-existing drive.\n");
			HDCCommand.returnCode = HD_STATUS_ERROR;
		}

		HDCCommand.byteCount = 0;
	}
	else
	{
		FDC_AcknowledgeInterrupt();
		FDC_SetDMAStatus(false);
		HDCCommand.returnCode = HD_STATUS_OK;
	}
}
