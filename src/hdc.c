/*
  Hatari

  Low-level hard drive emulation

*/

#include "main.h"
#include "debug.h"
#include "decode.h"
#include "dialog.h"
#include "fdc.h"
#include "hdc.h"
#include "floppy.h"
#include "ikbd.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "mfp.h"
#include "misc.h"
#include "psg.h"
#include "stMemory.h"
#include "debugui.h"

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

/* #define DISALLOW_HDC_WRITE */
/* #define HDC_VERBOSE */        /* display operations */
/* #define HDC_REALLY_VERBOSE */ /* display command packets */

/* HDC globals */
HDCOMMAND HDCCommand;
FILE *hd_image_file = NULL;
int nPartitions = 0;
short int HDCSectorCount;

/* 
  FDC registers used 
*/
extern short int HDCSectorCount;
extern short int FDCSectorCountRegister;

extern unsigned short int DiscControllerStatus_ff8604rd;         /* 0xff8604 (read) */
extern unsigned short int DiscControllerWord_ff8604wr;           /* 0xff8604 (write) */
extern unsigned short int DMAStatus_ff8606rd;                    /* 0xff8606 (read) */
extern unsigned short int DMAModeControl_ff8606wr,DMAModeControl_ff8606wr_prev;  /* 0xff8606 (write,store prev for 'toggle' checks) */

/* Our dummy INQUIRY response data */
unsigned char inquiry_bytes[] = 
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
unsigned long HDC_GetOffset(){
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
void HDC_Seek()
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
void HDC_Inquiry()
{
  inquiry_bytes[4] = HD_SECTORCOUNT(HDCCommand) - 8;
  memcpy( (char *)((unsigned long)STRam+FDC_ReadDMAAddress()), inquiry_bytes, 
	  HD_SECTORCOUNT(HDCCommand));

  FDC_SetDMAStatus(FALSE);              /* no DMA error */
  FDC_AcknowledgeInterrupt();
  HDCCommand.returnCode = HD_STATUS_OK;
  FDCSectorCountRegister = 0;
}

/*---------------------------------------------------------------------*/
/*
  Write a sector off our disk - (seek implied)
*/
void HDC_WriteSector()
{
  /* seek to the position */
  fseek(hd_image_file, HDC_GetOffset(),0);

  /* write -if allowed */
#ifndef DISALLOW_HDC_WRITE
  fwrite((char *)((unsigned long)STRam+FDC_ReadDMAAddress()), 
	 512*HD_SECTORCOUNT(HDCCommand), 1, hd_image_file); 
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
void HDC_ReadSector()
{
  /* seek to the position */
  fseek(hd_image_file, HDC_GetOffset(),0);

#ifdef HDC_VERBOSE
  fprintf(stderr,"Reading %i sectors from 0x%lx to addr:%lx\n", HD_SECTORCOUNT(HDCCommand), HDC_GetOffset() ,FDC_ReadDMAAddress()); 
#endif

  fread((char *)((unsigned long)STRam+FDC_ReadDMAAddress()), 
	512*HD_SECTORCOUNT(HDCCommand), 1, hd_image_file); 

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

  switch(HD_OPCODE(HDCCommand)){

  case HD_READ_SECTOR: 
    HDC_ReadSector();
    break;
  case HD_WRITE_SECTOR:
    HDC_WriteSector();
    break;
    
  case HD_INQUIRY:
#ifdef HDC_VERBOSE
    fprintf(stderr,"Inquiry made.\n");
#endif
    HDC_Inquiry();
    break;

  case HD_SEEK:        
    HDC_Seek();
    break;

  case HD_SHIP:
    HDCCommand.returnCode = 0xFF;
    FDC_AcknowledgeInterrupt();
    break;

    /* as of yet unsupported commands */
  case HD_VERIFY_TRACK:
  case HD_FORMAT_TRACK:
  case HD_CORRECTION:  
  case HD_MODESENSE:   
  case HD_REQ_SENSE:   

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
void HDC_DebugCommandPacket(FILE *hdlogFile)
{
  switch(HD_OPCODE(HDCCommand)){
  case HD_VERIFY_TRACK:
    fprintf(hdlogFile, "OpCode: VERIFY TRACK\n");
    break;
  case HD_FORMAT_TRACK:
    fprintf(hdlogFile, "OpCode: FORMAT TRACK\n");
    break;  
  case HD_READ_SECTOR: 
    fprintf(hdlogFile, "OpCode: READ SECTOR(S)\n");
    break;
  case HD_WRITE_SECTOR:
    fprintf(hdlogFile, "OpCode: WRITE SECTOR(S)\n");
    break;
  case HD_INQUIRY:        
    fprintf(hdlogFile, "OpCode: INQUIRY\n");
    break;
  case HD_SEEK:        
    fprintf(hdlogFile, "OpCode: SEEK\n");
    break;
  case HD_CORRECTION:  
    fprintf(hdlogFile, "OpCode: CORRECTION\n");
    break;
  case HD_MODESENSE:   
    fprintf(hdlogFile, "OpCode: MODESENSE\n");
    break;

  case HD_SHIP:   
    fprintf(hdlogFile, "OpCode: SHIP\n");
    break;

  case HD_REQ_SENSE:   
    fprintf(hdlogFile, "OpCode: MODESENSE\n");
    break;

  default:
    fprintf(hdlogFile, "Unknown OpCode!! Value = %x\n", HD_OPCODE(HDCCommand));
    break;
  }
  fprintf(hdlogFile, "Controller: %i\n", HD_CONTROLLER(HDCCommand));
  fprintf(hdlogFile, "Drive: %i\n", HD_DRIVENUM(HDCCommand));
  fprintf(hdlogFile, "LBA: %lx\n", HDC_GetOffset());

  fprintf(hdlogFile, "Sector count: %x\n", HD_SECTORCOUNT(HDCCommand));
  fprintf(hdlogFile, "HDC sector count: %x\n", HDCSectorCount);
  fprintf(hdlogFile, "FDC sector count: %x\n", FDCSectorCountRegister);
  fprintf(hdlogFile, "Control byte: %x\n", HD_CONTROL(HDCCommand));
}

/*---------------------------------------------------------------------*/
/*
  Print data about the hard drive image
*/
void HDC_GetInfo()
{
  long offset;
  unsigned char hdinfo[64];
  unsigned long size;
  int i;

  nPartitions = 0;
  if(hd_image_file == NULL) return;
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
    if(hdinfo[4 + 12*i]) nPartitions++;

  fseek(hd_image_file, offset, 0);
}

/*---------------------------------------------------------------------*/
/*
  Open the disk image file, set partitions.
 */
BOOL HDC_Init(char *filename)
{
  if( (hd_image_file = fopen(filename, "r+")) == NULL)
    return( FALSE );

  HDC_GetInfo();
  if(!nPartitions) 
    {
      fclose( hd_image_file );
      hd_image_file = NULL;
      return( FALSE );
    }
  /* set number of partitions */
  ConfigureParams.HardDisc.nDriveList += nPartitions;

  return( TRUE );
}
/*---------------------------------------------------------------------*/
/*
  HDC_UnInit - close image file

 */
void HDC_UnInit()
{
  if(!(ACSI_EMU_ON)) return;
  fclose(hd_image_file);
  hd_image_file = NULL;
  nPartitions = 0;
}

/*---------------------------------------------------------------------*/
/*
  Process HDC command packets, called when bytes are 
  written to $FFFF8606 and the HDC (not the FDC) is selected.
*/
void HDC_WriteCommandPacket()
{

  /* check status byte */
  if(((DMAModeControl_ff8606wr & 0x0018) != 8)) return;
 
  /* is HDC emulation enabled? */
  if(!(ACSI_EMU_ON)) return;

  /* command byte sent, store it. */
  HDCCommand.command[HDCCommand.byteCount++] =  (DiscControllerWord_ff8604wr&0xFF);
  
  /* have we recived a complete 6-byte packet yet? */
  if(HDCCommand.byteCount >= 6){
    
#ifdef HDC_REALLY_VERBOSE
    HDC_DebugCommandPacket(stderr);
#endif

    /* If it's aimed for our drive, emulate it! */
    if((HD_CONTROLLER(HDCCommand)) == 0)
      {
	if(HD_DRIVENUM(HDCCommand) == 0) HDC_EmulateCommandPacket();
      }
    else 
      {
	FDC_SetDMAStatus(FALSE); 
	FDC_AcknowledgeInterrupt();
	HDCCommand.returnCode = HD_STATUS_NODRIVE; 
	FDCSectorCountRegister = 0;
	FDC_AcknowledgeInterrupt();
      }
    
    HDCCommand.byteCount = 0;
  }
}
















