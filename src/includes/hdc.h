/*
  Hatari - hdc.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains definitios used for hardware-level
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
#define HD_SEEK            0x0B               /* Seek */
#define HD_CORRECTION      0x0D               /* Correction */
#define HD_INQUIRY         0x12               /* Inquiry */
#define HD_MODESELECT      0x15               /* Mode select */
#define HD_MODESENSE       0x1A               /* Mode sense */
#define HD_REQ_SENSE       0x03               /* Request sense */
#define HD_SHIP            0x1B               /* Ship drive */

#define HD_STATUS_OK          0               /* OK return status */
#define HD_STATUS_NODRIVE  0x25               /* Invalid drive */
#define HD_STATUS_OPCODE   0x20               /* Opcode not supported */

#define ACSI_EMU_ON         (nPartitions != 0) 
/* do we have hdc emulation */
/* 
   Structure representing an ACSI command block.
*/
typedef struct {
  int byteCount;         /* count of number of command bytes written */
  unsigned char command[6]; 
  short int returnCode;  /* return code from the HDC operation */
} HDCOMMAND;

extern FILE *hd_image_file;
extern HDCOMMAND HDCCommand;
extern short int HDCSectorCount;
extern int nPartitions;

extern BOOL HDC_Init(char *filename);
extern void HDC_UnInit(void);
extern void HDC_WriteCommandPacket(void);
extern void HDC_DebugCommandPacket(FILE *hdlogFile);
extern void HDC_EmulateCommandPacket(void);

#endif /* HATARI_HDC_H */
