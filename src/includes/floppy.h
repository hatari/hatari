/*
  Hatari - floppy.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FLOPPY_H
#define HATARI_FLOPPY_H

#include "configuration.h"



#define MAX_FLOPPYDRIVES    2					/* A: and B: */
#define NUMBYTESPERSECTOR 512					/* All supported disk images are 512 bytes per sector */

#define	FLOPPY_DRIVE_TRANSITION_STATE_INSERT		1
#define	FLOPPY_DRIVE_TRANSITION_STATE_EJECT		2
#define	FLOPPY_DRIVE_TRANSITION_DELAY_VBL		18	/* min of 16 VBLs */

#define	FLOPPY_IMAGE_TYPE_NONE			0		/* no recognized image inserted */
#define	FLOPPY_IMAGE_TYPE_ST			1
#define	FLOPPY_IMAGE_TYPE_MSA			2
#define	FLOPPY_IMAGE_TYPE_DIM			3
#define	FLOPPY_IMAGE_TYPE_IPF			4		/* handled by capsimage library */
#define	FLOPPY_IMAGE_TYPE_STX			5

/* Structure for each drive connected as emulation */
typedef struct
{
	int ImageType;
	Uint8 *pBuffer;
	char sFileName[FILENAME_MAX];
	int nImageBytes;
	bool bDiskInserted;
	bool bContentsChanged;
	bool bOKToSave;

	/* For the emulation of the WPRT bit when a disk is changed */
	int TransitionState1;
	int TransitionState1_VBL;
	int TransitionState2;
	int TransitionState2_VBL;
} EMULATION_DRIVE;

extern EMULATION_DRIVE EmulationDrives[MAX_FLOPPYDRIVES];
extern int nBootDrive;


extern void Floppy_Init(void);
extern void Floppy_UnInit(void);
extern void Floppy_Reset(void);
extern void Floppy_MemorySnapShot_Capture(bool bSave);
extern void Floppy_GetBootDrive(void);
extern bool Floppy_IsWriteProtected(int Drive);
extern const char* Floppy_SetDiskFileNameNone(int Drive);
extern const char* Floppy_SetDiskFileName(int Drive, const char *pszFileName, const char *pszZipPath);
extern int Floppy_DriveTransitionUpdateState ( int Drive );
extern bool Floppy_InsertDiskIntoDrive(int Drive);
extern bool Floppy_EjectDiskFromDrive(int Drive);
extern void Floppy_FindDiskDetails(const Uint8 *pBuffer, int nImageBytes, Uint16 *pnSectorsPerTrack, Uint16 *pnSides);
extern bool Floppy_ReadSectors(int Drive, Uint8 **pBuffer, Uint16 Sector, Uint16 Track, Uint16 Side, short Count, int *pnSectorsPerTrack, int *pSectorSize);
extern bool Floppy_WriteSectors(int Drive, Uint8 *pBuffer, Uint16 Sector, Uint16 Track, Uint16 Side, short Count, int *pnSectorsPerTrack, int *pSectorSize);

#endif
