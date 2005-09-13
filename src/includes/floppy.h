/*
  Hatari - floppy.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FLOPPY_H
#define HATARI_FLOPPY_H

/* Structure for each drive connected as emulation */
typedef struct
{
  Uint8 *pBuffer;
  char szFileName[FILENAME_MAX];
  int nImageBytes;
  BOOL bDiskInserted;
  BOOL bMediaChanged;
  BOOL bContentsChanged;
  BOOL bOKToSave;
} EMULATION_DRIVE;

#define NUM_EMULATION_DRIVES  2            /* A:, B: */
#define NUMBYTESPERSECTOR    512           /* All disks are 512 bytes per sector */

extern EMULATION_DRIVE EmulationDrives[NUM_EMULATION_DRIVES];
extern int nBootDrive;

extern void Floppy_Init(void);
extern void Floppy_UnInit(void);
extern void Floppy_MemorySnapShot_Capture(BOOL bSave);
extern void Floppy_GetBootDrive(void);
extern BOOL Floppy_IsWriteProtected(int Drive);
extern BOOL Floppy_InsertDiskIntoDrive(int Drive, char *pszFileName);
extern BOOL Floppy_ZipInsertDiskIntoDrive(int Drive, char *pszFileName, char *pszZipPath);
extern void Floppy_EjectDiskFromDrive(int Drive,BOOL bInformUser);
extern void Floppy_EjectBothDrives(void);
extern void Floppy_FindDiskDetails(const Uint8 *pBuffer, int nImageBytes, unsigned short *pnSectorsPerTrack, unsigned short *pnSides);
extern BOOL Floppy_ReadSectors(int Drive,char *pBuffer,unsigned short int Sector,unsigned short int Track,unsigned short int Side, short int Count, int *pnSectorsPerTrack);
extern BOOL Floppy_WriteSectors(int Drive,char *pBuffer,unsigned short int Sector,unsigned short int Track,unsigned short int Side, short int Count, int *pnSectorsPerTrack);
extern int Floppy_GetPhysicalSectorsPerTrack(int Drive);
extern BOOL Floppy_ReadPhysicalSector(int Drive,char *pBuffer,unsigned short int Sector,unsigned short int Track,unsigned short int Side,unsigned short int Count);

#endif
