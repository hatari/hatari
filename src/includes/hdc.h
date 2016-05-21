/*
  Hatari - hdc.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains definitions which are used for hardware-level
  harddrive emulation.
*/

#ifndef HATARI_HDC_H
#define HATARI_HDC_H


/* Opcodes */
/* The following are multi-sector transfers with seek implied */
#define HD_VERIFY_TRACK    0x05               /* Verify track */
#define HD_FORMAT_TRACK    0x06               /* Format track */
#define HD_READ_SECTOR     0x08               /* Read sector */
#define HD_READ_SECTOR1    0x28               /* Read sector (class 1) */
#define HD_WRITE_SECTOR    0x0A               /* Write sector */
#define HD_WRITE_SECTOR1   0x2A               /* Write sector (class 1) */

/* other codes */
#define HD_TEST_UNIT_RDY   0x00               /* Test unit ready */
#define HD_FORMAT_DRIVE    0x04               /* Format the whole drive */
#define HD_SEEK            0x0B               /* Seek */
#define HD_CORRECTION      0x0D               /* Correction */
#define HD_INQUIRY         0x12               /* Inquiry */
#define HD_MODESELECT      0x15               /* Mode select */
#define HD_MODESENSE       0x1A               /* Mode sense */
#define HD_REQ_SENSE       0x03               /* Request sense */
#define HD_SHIP            0x1B               /* Ship drive */
#define HD_READ_CAPACITY1  0x25               /* Read capacity (class 1) */

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
#define HD_REQSENS_INVLUN   0x25              /* Invalid LUN */

#define ACSI_EMU_ON        bAcsiEmuOn         /* Do we have HDC emulation? */

extern int nAcsiPartitions;
extern bool bAcsiEmuOn;

/**
 * API.
 */
extern bool HDC_Init(void);
extern void HDC_UnInit(void);
extern void HDC_ResetCommandStatus(void);
extern short int HDC_ReadCommandByte(int addr);
extern void HDC_WriteCommandByte(int addr, Uint8 byte);
extern int HDC_PartitionCount(FILE *fp, const Uint64 tracelevel);
extern off_t HDC_CheckAndGetSize(const char *filename);

void Ncr5380_Reset(void);

#endif /* HATARI_HDC_H */
