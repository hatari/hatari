/*
 * Hatari - NCR 5380 SCSI controller emulation
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#ifndef NCR5380_H
#define NCR5380_H

void Ncr5380_Init(void);
void Ncr5380_UnInit(void);
void Ncr5380_Reset(void);
void Ncr5380_WriteByte(int addr, Uint8 byte);
Uint8 Ncr5380_ReadByte(int addr);
void Ncr5380_DmaTransfer_Falcon(void);

#endif
