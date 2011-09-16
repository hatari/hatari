/*
  Hatari - floppy.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FLOPPY_H
#define HATARI_FLOPPY_H

#include "configuration.h"

/* Structure for each drive connected as emulation */
typedef struct
{
	Uint8 *pBuffer;
	char sFileName[FILENAME_MAX];
	int nImageBytes;
	bool bDiskInserted;
	bool bMediaChanged;
	bool bContentsChanged;
	bool bOKToSave;
} EMULATION_DRIVE;

#define MAX_FLOPPYDRIVES    2     /* A:, B: */
#define NUMBYTESPERSECTOR 512     /* All disks are 512 bytes per sector */

extern EMULATION_DRIVE EmulationDrives[MAX_FLOPPYDRIVES];
extern int nBootDrive;

extern void Floppy_Init(void);
extern void Floppy_UnInit(void);
extern void Floppy_MemorySnapShot_Capture(bool bSave);
extern void Floppy_GetBootDrive(void);
extern bool Floppy_IsWriteProtected(int Drive);
extern const char* Floppy_SetDiskFileNameNone(int Drive);
extern const char* Floppy_SetDiskFileName(int Drive, const char *pszFileName, const char *pszZipPath);
extern bool Floppy_InsertDiskIntoDrive(int Drive);
extern bool Floppy_EjectDiskFromDrive(int Drive);
extern void Floppy_FindDiskDetails(const Uint8 *pBuffer, int nImageBytes, Uint16 *pnSectorsPerTrack, Uint16 *pnSides);
extern bool Floppy_ReadSectors(int Drive, Uint8 *pBuffer, Uint16 Sector, Uint16 Track, Uint16 Side, short Count, int *pnSectorsPerTrack, int *pSectorSize);
extern bool Floppy_WriteSectors(int Drive, Uint8 *pBuffer, Uint16 Sector, Uint16 Track, Uint16 Side, short Count, int *pnSectorsPerTrack, int *pSectorSize);

#endif
