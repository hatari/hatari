/*
  Hatari - floppy.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This where we read/write sectors to/from the disk image buffers. NOTE these
  buffers are in memory so we only need to write routines for the .ST format.
  When the buffer is to be saved (ie eject disk) we save it back to the original
  file in the correct format (.ST or .MSA).

  There are some important notes about image accessing - as we use TOS and the
  FDC to access the disk the boot-sector MUST be valid. Sometimes this is NOT
  the case! In these situations we must guess at the disk format. Eg, some disk
  images have a a boot sector which states single-sided, but the images have
  been created as double-sided. As sides are interleaved we need to read the
  image as if it was double-sided. Also note that 'NumBytesPerSector' is ALWAYS
  512 bytes, even if the boot-sector states otherwise.
  Also note that old versions of the MAKEDISK utility do not set the correct
  boot sector structure for a real ST (and also Hatari) to read it correctly.
  (PaCifiST will, however, read/write to these images as it does not perform
  FDC access as on a real ST)
*/
char Floppy_rcsid[] = "Hatari $Id: floppy.c,v 1.26 2005-06-06 22:29:43 thothy Exp $";

#include <sys/stat.h>

#include <SDL_endian.h>

#include "main.h"
#include "configuration.h"
#include "dim.h"
#include "file.h"
#include "floppy.h"
#include "log.h"
#include "memorySnapShot.h"
#include "msa.h"
#include "st.h"
#include "zip.h"

EMULATION_DRIVE EmulationDrives[NUM_EMULATION_DRIVES];  /* Emulation drive details, eg FileName, Inserted, Changed etc... */
int nBootDrive=0;               /* Drive A, default */

/* Possible disc image file extensions to scan for */
static const char *pszDiscImageNameExts[] =
{
  ".msa",
  ".st",
  ".dim",
  NULL
};


/*-----------------------------------------------------------------------*/
/*
  Initialize emulation floppy drives
*/
void Floppy_Init(void)
{
  int i;

  /* Clear drive structures */
  for(i=0; i<NUM_EMULATION_DRIVES; i++)
  {
    /* Clear */
    memset(&EmulationDrives[i], 0, sizeof(EMULATION_DRIVE));
  }
}


/*-----------------------------------------------------------------------*/
/*
  UnInitialize drives
*/
void Floppy_UnInit(void)
{
  Floppy_EjectBothDrives();
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void Floppy_MemorySnapShot_Capture(BOOL bSave)
{
  int i;

  /* If restoring then eject old drives first! */
  if (!bSave)
    Floppy_EjectBothDrives();

  /* Save/Restore details */
  for(i=0; i<NUM_EMULATION_DRIVES; i++)
  {
    MemorySnapShot_Store(&EmulationDrives[i].bDiscInserted,sizeof(EmulationDrives[i].bDiscInserted));
    MemorySnapShot_Store(&EmulationDrives[i].nImageBytes, sizeof(EmulationDrives[i].nImageBytes));
    if (!bSave && EmulationDrives[i].bDiscInserted)
    {
      EmulationDrives[i].pBuffer = malloc(EmulationDrives[i].nImageBytes);
      if (!EmulationDrives[i].pBuffer)
        perror("Floppy_MemorySnapShot_Capture");
    }
    if (EmulationDrives[i].pBuffer)
      MemorySnapShot_Store(EmulationDrives[i].pBuffer, EmulationDrives[i].nImageBytes);
    MemorySnapShot_Store(EmulationDrives[i].szFileName, sizeof(EmulationDrives[i].szFileName));
    MemorySnapShot_Store(&EmulationDrives[i].bMediaChanged,sizeof(EmulationDrives[i].bMediaChanged));
    MemorySnapShot_Store(&EmulationDrives[i].bContentsChanged,sizeof(EmulationDrives[i].bContentsChanged));
    MemorySnapShot_Store(&EmulationDrives[i].bOKToSave,sizeof(EmulationDrives[i].bOKToSave));
  }
}


/*-----------------------------------------------------------------------*/
/*
  Find which device to boot from
*/
void Floppy_GetBootDrive(void)
{
  /* If we've inserted a disc or not enabled boot from hard-drive, boot from the floppy drive */
  if ( (!ConfigureParams.HardDisc.bBootFromHardDisc) || (EmulationDrives[0].bDiscInserted) )
    nBootDrive = 0;  /* Drive A */
  else
    nBootDrive = 2;  /* Drive C */
}


/*-----------------------------------------------------------------------*/
/*
  Test disc image if it is write protected. Write protection can be configured
  in the GUI. When set to "automatic", we check the file permissions of the
  floppy disk image to decide.
*/
BOOL Floppy_IsWriteProtected(int Drive)
{
  if (ConfigureParams.DiscImage.nWriteProtection == WRITEPROT_OFF)
  {
    return FALSE;
  }
  else if (ConfigureParams.DiscImage.nWriteProtection == WRITEPROT_ON)
  {
    return TRUE;
  }
  else
  {
    struct stat FloppyStat;
    /* Check whether disk is writable */
    if (stat(EmulationDrives[Drive].szFileName, &FloppyStat) == 0 && (FloppyStat.st_mode & S_IWUSR))
      return FALSE;
    else
      return TRUE;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Test disc image for valid boot-sector

  It has been noticed that some discs, eg blank images made by the MakeDisk
  utility or PaCifiST emulator fill in the boot-sector with incorrect information.
  Such images cannot be read correctly using a real ST, and also Hatari.
  To try and prevent data loss, we check for this error and flag the drive so
  the image will not be saved back to the file.
*/
static BOOL Floppy_IsBootSectorOK(int Drive)
{
  unsigned char *pDiscBuffer;

  /* Does our drive have a disc in? */
  if (EmulationDrives[Drive].bDiscInserted)
  {
    pDiscBuffer = EmulationDrives[Drive].pBuffer;

    /* Check SPC (byte 13) for !=0 value. If is '0', invalid image and Hatari
     * won't be-able to read (nor will a real ST)! */
    if (pDiscBuffer[13]!=0)
    {
      return TRUE;      /* Disc sector is OK! */
    }
    else
    {
      Log_AlertDlg(LOG_WARN, "Disk in drive %c: maybe suffers from the Pacifist/Makedisk bug.\n"
                             "If it does not work, please repair the disk first!\n", 'A' + Drive);
    }
  }

  return FALSE;         /* Bad sector */
}


/*-----------------------------------------------------------------------*/
/*
  Try to create disc B filename, eg 'auto_100a' becomes 'auto_100b'
  Return TRUE if think we should try!
*/
static BOOL Floppy_CreateDiscBFileName(const char *pSrcFileName, char *pDestFileName)
{
  char *szDir, *szName, *szExt;
  BOOL bFileExists = FALSE;

  /* Allocate temporary memory for strings: */
  szDir = malloc(3 * FILENAME_MAX);
  if (!szDir)
  {
    perror("Floppy_CreateDiscBFileName");
    return FALSE;
  }
  szName = szDir + FILENAME_MAX;
  szExt = szName + FILENAME_MAX;
 
  /* So, first split name into parts */
  File_splitpath(pSrcFileName, szDir, szName, szExt);

  /* All OK? */
  if (strlen(szName) > 0)
  {
    /* Now, did filename end with an 'A' or 'a'? */
    if ((szName[strlen(szName)-1]=='A') || (szName[strlen(szName)-1]=='a'))
    {
      /* Change 'A' to a 'B' */
      szName[strlen(szName)-1] += 1;
      /* And re-build name into destination */
      File_makepath(pDestFileName, szDir, szName, szExt);
      /* Does file exist? */
      bFileExists = File_Exists(pDestFileName);
    }
  }

  free(szDir);

  return bFileExists;
}


/*-----------------------------------------------------------------------*/
/*
  Insert disc into floppy drive
  The WHOLE image is copied into our drive buffers, and uncompressed if necessary
*/
BOOL Floppy_InsertDiscIntoDrive(int Drive, char *pszFileName)
{
  return Floppy_ZipInsertDiscIntoDrive(Drive, pszFileName, NULL);
}

BOOL Floppy_ZipInsertDiscIntoDrive(int Drive, char *pszFileName, char *pszZipPath)
{
  long nImageBytes = 0;

  /* Eject disc, if one is inserted(don't inform user) */
  Floppy_EjectDiscFromDrive(Drive,FALSE);

  /* See if file exists, and if not get correct extension */
  if( !File_Exists(pszFileName) )
    File_FindPossibleExtFileName(pszFileName,pszDiscImageNameExts);

  /* Check disc image type and read the file: */
  if (MSA_FileNameIsMSA(pszFileName, TRUE))
    EmulationDrives[Drive].pBuffer = MSA_ReadDisc(pszFileName, &nImageBytes);
  else if (ST_FileNameIsST(pszFileName, TRUE))
    EmulationDrives[Drive].pBuffer = ST_ReadDisc(pszFileName, &nImageBytes);
  else if (DIM_FileNameIsDIM(pszFileName, TRUE))
    EmulationDrives[Drive].pBuffer = DIM_ReadDisc(pszFileName, &nImageBytes);
  else if (ZIP_FileNameIsZIP(pszFileName))
    EmulationDrives[Drive].pBuffer = ZIP_ReadDisc(pszFileName, pszZipPath, &nImageBytes);

  /* Did load OK? */
  if (EmulationDrives[Drive].pBuffer != NULL)
  {
    /* Store filename and size */
    strcpy(EmulationDrives[Drive].szFileName,pszFileName);
    EmulationDrives[Drive].nImageBytes = nImageBytes;
    /* And set drive states */
    EmulationDrives[Drive].bDiscInserted = TRUE;
    EmulationDrives[Drive].bContentsChanged = FALSE;
    EmulationDrives[Drive].bMediaChanged = TRUE;
    EmulationDrives[Drive].bOKToSave = Floppy_IsBootSectorOK(Drive);
  }

  /* If we insert a disc into Drive A, should be try to put disc 2 into drive B? */
  if ( (Drive==0) && (ConfigureParams.DiscImage.bAutoInsertDiscB) )
  {
    char *szDiscBFileName = malloc(FILENAME_MAX);
    /* Attempt to make up second filename, eg was 'auto_100a' to 'auto_100b' */
    if (szDiscBFileName && Floppy_CreateDiscBFileName(pszFileName, szDiscBFileName))
    {
      /* Put image into Drive B, clear out if fails */
      if (!Floppy_InsertDiscIntoDrive(1,szDiscBFileName))
        strcpy(EmulationDrives[1].szFileName,"");
    }
    free(szDiscBFileName);
  }


  /* Return TRUE if loaded OK */
  return (EmulationDrives[Drive].pBuffer != NULL);
}


/*-----------------------------------------------------------------------*/
/*
  Eject disc from floppy drive, save contents back to PCs hard-drive if has changed
*/
void Floppy_EjectDiscFromDrive(int Drive, BOOL bInformUser)
{
  /* Does our drive have a disc in? */
  if (EmulationDrives[Drive].bDiscInserted)
  {
    /* OK, has contents changed? If so, need to save */
    if (EmulationDrives[Drive].bContentsChanged)
    {
      /* Is OK to save image (if boot-sector is bad, don't allow a save) */
      if (EmulationDrives[Drive].bOKToSave && !Floppy_IsWriteProtected(Drive))
      {
        /* Save as .MSA or .ST image? */
        if (MSA_FileNameIsMSA(EmulationDrives[Drive].szFileName, TRUE))
          MSA_WriteDisc(EmulationDrives[Drive].szFileName, EmulationDrives[Drive].pBuffer, EmulationDrives[Drive].nImageBytes);
        else if (ST_FileNameIsST(EmulationDrives[Drive].szFileName, TRUE))
          ST_WriteDisc(EmulationDrives[Drive].szFileName, EmulationDrives[Drive].pBuffer, EmulationDrives[Drive].nImageBytes);
        else if (DIM_FileNameIsDIM(EmulationDrives[Drive].szFileName, TRUE))
          DIM_WriteDisc(EmulationDrives[Drive].szFileName, EmulationDrives[Drive].pBuffer, EmulationDrives[Drive].nImageBytes);
        else if (ZIP_FileNameIsZIP(EmulationDrives[Drive].szFileName))
          ZIP_WriteDisc(EmulationDrives[Drive].szFileName, EmulationDrives[Drive].pBuffer, EmulationDrives[Drive].nImageBytes);
      }
    }

    /* Inform user that disk has been ejected! */
    if (bInformUser)
    {
      Log_AlertDlg(LOG_INFO, "Disk has been removed from drive '%c:'.", 'A'+Drive);
    }
  }

  /* Drive is now empty */
  if (EmulationDrives[Drive].pBuffer != NULL)
  {
    free(EmulationDrives[Drive].pBuffer);
    EmulationDrives[Drive].pBuffer = NULL;
  }
  strcpy(EmulationDrives[Drive].szFileName,"");
  EmulationDrives[Drive].nImageBytes = 0;
  EmulationDrives[Drive].bDiscInserted = FALSE;
  EmulationDrives[Drive].bContentsChanged = FALSE;
  EmulationDrives[Drive].bOKToSave = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Eject all disc image from floppy drives - call when quit
*/
void Floppy_EjectBothDrives(void)
{
  /* Eject disc images from drives 'A' and 'B' */
  Floppy_EjectDiscFromDrive(0,FALSE);
  Floppy_EjectDiscFromDrive(1,FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Double-check information read from boot-sector as this is sometimes found to
  be incorrect. The .ST image file should be divisible by the sector size and
  sectors per track.
  NOTE - Pass information from boot-sector to this function (if we can't
  decide we leave it alone).
*/
static void Floppy_DoubleCheckFormat(long DiscSize, Uint16 *pnSides, Uint16 *pnSectorsPerTrack)
{
  int nSectorsPerTrack;
  long TotalSectors;

  /* Now guess at number of sides */
  if (DiscSize<(500*1024))                      /* Is size is >500k assume 2 sides to disc! */
    *pnSides = 1;
  else
    *pnSides = 2;

  /* And Sectors Per Track(always 512 bytes per sector) */
  TotalSectors = DiscSize/512;                  /* # Sectors on disc image */
  /* Does this match up with what we've read from boot-sector? */
  nSectorsPerTrack = *pnSectorsPerTrack;
  if (nSectorsPerTrack==0)                      /* Check valid, default to 9 */
    nSectorsPerTrack = 9;
  if ((TotalSectors%nSectorsPerTrack)!=0)
  {
    /* No, we have an invalid boot-sector - re-calculate from disc size */
    if ((TotalSectors%9)==0)                    /* Work in this order.... */
      *pnSectorsPerTrack = 9;
    else if ((TotalSectors%10)==0)
      *pnSectorsPerTrack = 10;
    else if ((TotalSectors%11)==0)
      *pnSectorsPerTrack = 11;
    else if ((TotalSectors%12)==0)
      *pnSectorsPerTrack = 12;
  }
  /* else unknown, assume boot-sector is correct!!! */
}


/*-----------------------------------------------------------------------*/
/*
  Find details of disc image. We need to do this via a function as sometimes the boot-block
  is not actually correct with the image - some demos/game discs have incorrect bytes in the
  boot sector and this attempts to find the correct values.
*/
void Floppy_FindDiscDetails(const Uint8 *pBuffer, int nImageBytes,
                            unsigned short *pnSectorsPerTrack, unsigned short *pnSides)
{
  Uint16 nSectorsPerTrack, nSides, nSectors;

  /* First do check to find number of sectors and bytes per sector */
  nSectorsPerTrack = SDL_SwapLE16(*(const Uint16 *)(pBuffer+24));   /* SPT */
  nSides = SDL_SwapLE16(*(const Uint16 *)(pBuffer+26));             /* SIDE */
  nSectors = pBuffer[19] | (pBuffer[20] << 8);                      /* total sectors */

  /* If the number of sectors announced is incorrect, the boot-sector may
   * contain incorrect information, eg the 'Eat.st' demo, or wrongly imaged
   * single/double sided floppies... */
  if (nSectors != nImageBytes/512)
    Floppy_DoubleCheckFormat(nImageBytes, &nSides, &nSectorsPerTrack);

  /* And set values */
  if (pnSectorsPerTrack)
    *pnSectorsPerTrack = nSectorsPerTrack;
  if (pnSides)
    *pnSides = nSides;
}


/*-----------------------------------------------------------------------*/
/*
  Read sectors from floppy disc image, return TRUE if all OK
  NOTE Pass -ve as Count to read whole track
*/
BOOL Floppy_ReadSectors(int Drive,char *pBuffer,unsigned short int Sector,unsigned short int Track,unsigned short int Side, short int Count, int *pnSectorsPerTrack)
{
  unsigned char *pDiscBuffer;
  unsigned short int nSectorsPerTrack,nSides,nBytesPerTrack;
  long Offset;
  int nImageTracks;

  /* Do we have a disc in our drive? */
  if (EmulationDrives[Drive].bDiscInserted)
  {
    /* Looks good */
    pDiscBuffer = EmulationDrives[Drive].pBuffer;

    /* Find #sides and #sectors per track */
    Floppy_FindDiscDetails(EmulationDrives[Drive].pBuffer,EmulationDrives[Drive].nImageBytes,&nSectorsPerTrack,&nSides);
    nImageTracks = ((EmulationDrives[Drive].nImageBytes / NUMBYTESPERSECTOR) / nSectorsPerTrack) / nSides;

    /* Need to read whole track? */
    if (Count<0)
      Count = nSectorsPerTrack;
    /* Write back number of sector per track */
    if (pnSectorsPerTrack)
      *pnSectorsPerTrack = nSectorsPerTrack;

    /* Debug check as if we read over the end of a track we read into side 2! */
    if (Count > nSectorsPerTrack)
    {
      Log_Printf(LOG_DEBUG, "Floppy_ReadSectors: reading over single track\n");
    }

    /* Check that the side number (0 or 1) does not exceed the amount of sides (1 or 2).
     * (E.g. some games like Drakkhen or Bolo can load additional data from the
     * second disc side, but they also work with single side floppy drives) */
    if (Side >= nSides)
    {
      Log_Printf(LOG_DEBUG, "Floppy_ReadSectors: Program tries to read from side %i "
                 "of a disk image with %i sides!\n", Side+1, nSides);
      return FALSE;
    }

    /* Check if track number is in range */
    if (Track >= nImageTracks)
    {
      Log_Printf(LOG_DEBUG, "Floppy_ReadSectors: Program tries to read from track %i "
                 "of a disk image with only %i tracks!\n", Track, nImageTracks);
      return FALSE;
    }

    /* Check if sector number is in range */
    if (Sector <= 0 || Sector > nSectorsPerTrack)
    {
      Log_Printf(LOG_DEBUG, "Floppy_ReadSectors: Program tries to read from sector %i "
                 "of a disk image with %i sectors per track!\n", Sector, nSectorsPerTrack);
      return FALSE;
    }

    /* Seek to sector */
    nBytesPerTrack = NUMBYTESPERSECTOR*nSectorsPerTrack;
    Offset = nBytesPerTrack*Side;                 /* First seek to side */
    Offset += (nBytesPerTrack*nSides)*Track;      /* Then seek to track */
    Offset += (NUMBYTESPERSECTOR*(Sector-1));     /* And finally to sector */

    /* Read sectors (usually 512 bytes per sector) */
    memcpy(pBuffer,pDiscBuffer+Offset,(int)Count*NUMBYTESPERSECTOR);

    return TRUE;
  }

  return FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Write sectors from floppy disc image, return TRUE if all OK
  NOTE Pass -ve as Count to write whole track
*/
BOOL Floppy_WriteSectors(int Drive,char *pBuffer,unsigned short int Sector,unsigned short int Track,unsigned short int Side, short int Count, int *pnSectorsPerTrack)
{
  unsigned char *pDiscBuffer;
  unsigned short int nSectorsPerTrack,nSides,nBytesPerTrack;
  long Offset;
  int nImageTracks;

  /* Do we have a writable disc in our drive? */
  if (EmulationDrives[Drive].bDiscInserted && !Floppy_IsWriteProtected(Drive))
  {
    /* Looks good */
    pDiscBuffer = EmulationDrives[Drive].pBuffer;

    /* Find #sides and #sectors per track */
    Floppy_FindDiscDetails(EmulationDrives[Drive].pBuffer,EmulationDrives[Drive].nImageBytes,&nSectorsPerTrack,&nSides);
    nImageTracks = ((EmulationDrives[Drive].nImageBytes / NUMBYTESPERSECTOR) / nSectorsPerTrack) / nSides;

    /* Need to write whole track? */
    if (Count<0)
      Count = nSectorsPerTrack;
    /* Write back number of sector per track */
    if (pnSectorsPerTrack)
      *pnSectorsPerTrack = nSectorsPerTrack;

    /* Debug check as if we write over the end of a track we write into side 2! */
    if (Count > nSectorsPerTrack)
    {
      Log_Printf(LOG_DEBUG, "Floppy_WriteSectors: writing over single track\n");
    }

    /* Check that the side number (0 or 1) does not exceed the amount of sides (1 or 2). */
    if (Side >= nSides)
    {
      Log_Printf(LOG_DEBUG, "Floppy_WriteSectors: Program tries to write to side %i "
                 "of a disk image with %i sides!\n", Side+1, nSides);
      return FALSE;
    }

    /* Check if track number is in range */
    if (Track >= nImageTracks)
    {
      Log_Printf(LOG_DEBUG, "Floppy_WriteSectors: Program tries to write to track %i "
                 "of a disk image with only %i tracks!\n", Track, nImageTracks);
      return FALSE;
    }

    /* Check if sector number is in range */
    if (Sector <= 0 || Sector > nSectorsPerTrack)
    {
      Log_Printf(LOG_DEBUG, "Floppy_WriteSectors: Program tries to write to sector %i "
                 "of a disk image with %i sectors per track!\n", Sector, nSectorsPerTrack);
      return FALSE;
    }

    /* Seek to sector */
    nBytesPerTrack = NUMBYTESPERSECTOR*nSectorsPerTrack;
    Offset = nBytesPerTrack*Side;               /* First seek to side */
    Offset += (nBytesPerTrack*nSides)*Track;    /* Then seek to track */
    Offset += (NUMBYTESPERSECTOR*(Sector-1));   /* And finally to sector */

    /* Write sectors (usually 512 bytes per sector) */
    memcpy(pDiscBuffer+Offset,pBuffer,(int)Count*NUMBYTESPERSECTOR);
    /* And set 'changed' flag */
    EmulationDrives[Drive].bContentsChanged = TRUE;

    return TRUE;
  }

  return FALSE;
}
