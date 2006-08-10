/*
  Hatari - hdc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Low-level hard drive emulation
*/
const char HDC_rcsid[] = "Hatari $Id: hdc.c,v 1.14 2006-08-10 19:10:35 thothy Exp $";

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
FILE *hd_image_file = NULL;
int nPartitions = 0;
short int HDCSectorCount;
BOOL bAcsiEmuOn = FALSE;

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
	'H','a','t','a','r','i',' ','E',       /* Vendor */
	'm','u','l','a','t','e','d',' ',       /* Model */
	' ',' ',' ',' ',                       /* Revision */
	0,0,0,0,0,0,0,0,0,0                    /* ?? */
};


/*---------------------------------------------------------------------*/
/*
  Return the file offset of the sector specified in the current
  ACSI command block.
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
/*
  Seek - move to a sector
*/
static void HDC_Seek(void)
{
	fseek(hd_image_file, HDC_GetOffset(),0);

	FDC_SetDMAStatus(FALSE);              /* no DMA error */
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_OK;
	FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/*
  Inquiry - return some disk information
*/
static void HDC_Inquiry(void)
{
#ifdef HDC_VERBOSE
		fprintf(stderr,"Inquiry made.\n");
#endif

	inquiry_bytes[4] = HD_SECTORCOUNT(HDCCommand) - 8;
	memcpy(&STRam[FDC_ReadDMAAddress()], inquiry_bytes,
	       HD_SECTORCOUNT(HDCCommand));

	FDC_SetDMAStatus(FALSE);              /* no DMA error */
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_OK;
	FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/*
  Request sense - return some disk information
*/
static void HDC_RequestSense(void)
{
	Uint32 nDmaAddr;

#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Request Sense.\n");
#endif

	if (HD_SECTORCOUNT(HDCCommand) != 4)
	{
		Log_Printf(LOG_ERROR, "HDC: Strange REQUEST SENSE\n");
		return;
	}

	nDmaAddr = FDC_ReadDMAAddress();

	if (HD_CONTROLLER(HDCCommand) == 0 && HD_DRIVENUM(HDCCommand) == 0)
		STRam[nDmaAddr] = 0;            /* Always OK at the moment */
	else
		STRam[nDmaAddr] = 4;            /* Drive not ready */
	STRam[nDmaAddr+1] = 0;
	STRam[nDmaAddr+2] = 0;
	STRam[nDmaAddr+3] = 0;

	FDC_SetDMAStatus(FALSE);            /* no DMA error */
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_OK;
	FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/*
  Mode sense - Get parameters from disk.
  (Just enough to make the HDX tool from AHDI 5.0 happy)
*/
static void HDC_ModeSense(void)
{
	Uint32 nDmaAddr;

#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Mode Sense.\n");
#endif

	nDmaAddr = FDC_ReadDMAAddress();

	if (HDCCommand.command[2] == 0 && HD_SECTORCOUNT(HDCCommand) == 0x10)
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
	}
	else
	{
		Log_Printf(LOG_ERROR, "HDC: Unsupported MODE SENSE command\n");
		return;
	}

	FDC_SetDMAStatus(FALSE);            /* no DMA error */
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_OK;
	FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/*
  Format drive.
*/
static void HDC_FormatDrive(void)
{
#ifdef HDC_VERBOSE
	fprintf(stderr,"HDC: Format drive!\n");
#endif

	/* Should erase the whole image file here... */

	FDC_SetDMAStatus(FALSE);            /* no DMA error */
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_OK;
	FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/*
  Write a sector off our disk - (seek implied)
*/
static void HDC_WriteSector(void)
{
	/* seek to the position */
	fseek(hd_image_file, HDC_GetOffset(),0);

	/* write -if allowed */
#ifndef DISALLOW_HDC_WRITE
	fwrite(&STRam[FDC_ReadDMAAddress()], 512, HD_SECTORCOUNT(HDCCommand),
	       hd_image_file);
#endif

	FDC_SetDMAStatus(FALSE);              /* no DMA error */
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_OK;
	FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/*
  Read a sector off our disk - (implied seek)
*/
static void HDC_ReadSector(void)
{
	/* seek to the position */
	fseek(hd_image_file, HDC_GetOffset(),0);

#ifdef HDC_VERBOSE
	fprintf(stderr,"Reading %i sectors from 0x%lx to addr: 0x%x\n",
	        HD_SECTORCOUNT(HDCCommand), HDC_GetOffset() ,FDC_ReadDMAAddress());
#endif

	fread(&STRam[FDC_ReadDMAAddress()], 512, HD_SECTORCOUNT(HDCCommand),
	      hd_image_file);

	FDC_SetDMAStatus(FALSE);              /* no DMA error */
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_OK;
	FDCSectorCountRegister = 0;
}


/*---------------------------------------------------------------------*/
/*
  Emulation routine for HDC command packets.
*/
void HDC_EmulateCommandPacket()
{

	switch(HD_OPCODE(HDCCommand))
	{

	 case HD_READ_SECTOR:
		HDC_ReadSector();
		break;
	 case HD_WRITE_SECTOR:
		HDC_WriteSector();
		break;

	 case HD_INQUIRY:
		HDC_Inquiry();
		break;

	 case HD_SEEK:
		HDC_Seek();
		break;

	 case HD_SHIP:
		HDCCommand.returnCode = 0xFF;
		FDC_AcknowledgeInterrupt();
		break;

	 case HD_REQ_SENSE:
		HDC_RequestSense();
		break;

	 case HD_MODESENSE:
		HDC_ModeSense();
		break;

	 case HD_FORMAT_DRIVE:
		HDC_FormatDrive();
		break;

	 /* as of yet unsupported commands */
	 case HD_VERIFY_TRACK:
	 case HD_FORMAT_TRACK:
	 case HD_CORRECTION:

	 default:
		HDCCommand.returnCode = HD_STATUS_OPCODE;
		FDC_AcknowledgeInterrupt();
		break;
	}
}


/*---------------------------------------------------------------------*/
/*
  Debug routine for HDC command packets.
*/
#ifdef HDC_REALLY_VERBOSE
void HDC_DebugCommandPacket(FILE *hdlogFile)
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

	fprintf(hdlogFile, "Controller: %i\n", HD_CONTROLLER(HDCCommand));
	fprintf(hdlogFile, "Drive: %i\n", HD_DRIVENUM(HDCCommand));
	fprintf(hdlogFile, "LBA: 0x%lx\n", HDC_GetOffset());

	fprintf(hdlogFile, "Sector count: 0x%x\n", HD_SECTORCOUNT(HDCCommand));
	fprintf(hdlogFile, "HDC sector count: 0x%x\n", HDCSectorCount);
	fprintf(hdlogFile, "FDC sector count: 0x%x\n", FDCSectorCountRegister);
	fprintf(hdlogFile, "Control byte: 0x%x\n", HD_CONTROL(HDCCommand));
}
#endif


/*---------------------------------------------------------------------*/
/*
  Print data about the hard drive image
*/
static void HDC_GetInfo(void)
{
	long offset;
	unsigned char hdinfo[64];
	int i;
#ifdef HDC_VERBOSE
	unsigned long size;
#endif

	nPartitions = 0;
	if (hd_image_file == NULL)
		return;
	offset = ftell(hd_image_file);

	fseek(hd_image_file, 0x1C2, 0);
	fread(hdinfo, 64, 1, hd_image_file);

#ifdef HDC_VERBOSE
	size = (((unsigned long) hdinfo[0] << 24)
	        | ((unsigned long) hdinfo[1] << 16)
	        | ((unsigned long) hdinfo[2] << 8)
	        | ((unsigned long) hdinfo[3]));

	fprintf(stderr, "Total disk size %li Mb\n", size>>11);
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
/*
  Open the disk image file, set partitions.
 */
BOOL HDC_Init(char *filename)
{
	bAcsiEmuOn = FALSE;

	/* Sanity check - is file length a multiple of 512? */
	if (File_Length(filename) & 0x1ff)
	{
		Log_Printf(LOG_ERROR, "HD file '%s' has strange size!\n", filename);
		return FALSE;
	}


	if ((hd_image_file = fopen(filename, "rb+")) == NULL)
	{
		Log_Printf(LOG_ERROR, "Can not open HD file '%s'!\n", filename);
		return FALSE;
	}

	HDC_GetInfo();
	/*
	if (!nPartitions)
	{
		fclose( hd_image_file );
		hd_image_file = NULL;
		ConfigureParams.HardDisk.bUseHardDiskImage = FALSE;
		return FALSE;
	}
	*/
	/* set number of partitions */
	nNumDrives += nPartitions;

	bAcsiEmuOn = TRUE;

	return TRUE;
}


/*---------------------------------------------------------------------*/
/*
  HDC_UnInit - close image file
 
 */
void HDC_UnInit(void)
{
	if (!bAcsiEmuOn)
		return;

	fclose(hd_image_file);
	hd_image_file = NULL;

	nNumDrives -= nPartitions;
	nPartitions = 0;
	bAcsiEmuOn = FALSE;
}


/*---------------------------------------------------------------------*/
/*
  Process HDC command packets, called when bytes are 
  written to $FFFF8606 and the HDC (not the FDC) is selected.
*/
void HDC_WriteCommandPacket(void)
{
	/* check status byte */
	if ((DMAModeControl_ff8606wr & 0x0018) != 8)
		return;

	/* is HDC emulation enabled? */
	if (!bAcsiEmuOn)
		return;

	/* command byte sent, store it. */
	HDCCommand.command[HDCCommand.byteCount++] =  (DiskControllerWord_ff8604wr&0xFF);

	/* have we received a complete 6-byte packet yet? */
	if(HDCCommand.byteCount >= 6)
	{

#ifdef HDC_REALLY_VERBOSE
		HDC_DebugCommandPacket(stderr);
#endif

		/* If it's aimed for our drive, emulate it! */
		if((HD_CONTROLLER(HDCCommand)) == 0)
		{
			if(HD_DRIVENUM(HDCCommand) == 0)
				HDC_EmulateCommandPacket();
		}
		else
		{
			/* No drive/controller */
			FDC_SetDMAStatus(TRUE);
			//FDC_AcknowledgeInterrupt();
			HDCCommand.returnCode = HD_STATUS_NODRIVE;
			FDCSectorCountRegister = 0;
			MFP_GPIP |= 0x20;
		}

		HDCCommand.byteCount = 0;
	}
}
