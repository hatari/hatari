/*
  Hatari - hdc.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains definitions which are used for hardware-level
  harddrive emulation.
*/

#ifndef HATARI_HDC_H
#define HATARI_HDC_H


/* HD Command packet macros */
#define HD_OPCODE(a) (a.command[0] & 0x1F)             /* get opcode (bit 0-4) */
#define HD_CONTROLLER(a) ((a.command[0] & 0xE0)>>5)    /* get HD controller # (5-7) */
#define HD_DRIVENUM(a) ((a.command[1] & 0xE0) >>5)     /* get drive # (5-7) */

#define HD_LBA_MSB(a) ((unsigned) a.command[1] & 0x1F) /* Logical Block adress, MSB */
#define HD_LBA_MID(a) ((unsigned) a.command[2])        /* Logical Block adress */
#define HD_LBA_LSB(a) ((unsigned) a.command[3])        /* Logical Block adress, LSB */

#define HD_SECTORCOUNT(a) (a.command[4] & 0xFF)        /* get sector count */
#define HD_CONTROL(a) (a.command[5] & 0xFF)            /* get control byte */


/* Opcodes */
/* The following are multi-sector transfers with seek implied */
#define HD_VERIFY_TRACK    0x05               /* Verify track */
#define HD_FORMAT_TRACK    0x06               /* Format track */
#define HD_READ_SECTOR     0x08               /* Read sector */
#define HD_WRITE_SECTOR    0x0A               /* Write sector */

/* other codes */
#define HD_FORMAT_DRIVE    0x04               /* Format the whole drive */
#define HD_SEEK            0x0B               /* Seek */
#define HD_CORRECTION      0x0D               /* Correction */
#define HD_INQUIRY         0x12               /* Inquiry */
#define HD_MODESELECT      0x15               /* Mode select */
#define HD_MODESENSE       0x1A               /* Mode sense */
#define HD_REQ_SENSE       0x03               /* Request sense */
#define HD_SHIP            0x1B               /* Ship drive */

/* Status codes */
#define HD_STATUS_OK       0x00
#define HD_STATUS_ERROR    0x02
#define HD_STATUS_BUSY     0x08

/* Error codes for REQUEST SENSE: */
#define HD_REQSENS_OK       0x00              /* OK return status */
#define HD_REQSENS_NOSECTOR 0x01              /* No index or sector */
#define HD_REQSENS_WRITEERR 0x03              /* Write fault */
#define HD_REQSENS_OPCODE   0x20              /* Opcode not supported */
#define HD_REQSENS_INVADDR  0x21              /* Invalid block address */
#define HD_REQSENS_INVARG   0x24              /* Invalid argument */
#define HD_REQSENS_NODRIVE  0x25              /* Invalid drive */

#define ACSI_EMU_ON        bAcsiEmuOn         /* Do we have HDC emulation? */

/* 
   Structure representing an ACSI command block.
*/
typedef struct {
  int byteCount;         /* count of number of command bytes written */
  unsigned char command[6]; 
  short int returnCode;  /* return code from the HDC operation */
} HDCOMMAND;

extern HDCOMMAND HDCCommand;
extern short int HDCSectorCount;
extern int nPartitions;
extern bool bAcsiEmuOn;

extern bool HDC_Init(char *filename);
extern void HDC_UnInit(void);
extern void HDC_WriteCommandPacket(void);

#endif /* HATARI_HDC_H */
