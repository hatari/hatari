/*
  Hatari

  Create Disc Image functions
*/

#include <stdio.h>

#include "main.h"
#include "debug.h"

/*-----------------------------------------------------------------------*/
/*
  Lock disc volume
*/
BOOL CreateDiscImage_LockVolume(FILE *hDisk)
{
//  DWORD ReturnedByteCount;

//  return(DeviceIoControl(hDisk,FSCTL_LOCK_VOLUME,NULL,0,NULL,0,&ReturnedByteCount,NULL));
}

/*-----------------------------------------------------------------------*/
/*
  UnLock disc volume
*/
BOOL CreateDiscImage_UnLockVolume(FILE *hDisk)
{
//  DWORD ReturnedByteCount;

//  return(DeviceIoControl(hDisk,FSCTL_UNLOCK_VOLUME,NULL,0,NULL,0,&ReturnedByteCount,NULL));
}

/*-----------------------------------------------------------------------*/
/*
  Read PC floppy disc into image file
*/
void CreateDiscImage_ReadImage(char *pszDriveName)
{
 char szDrive[MAX_FILENAME_LENGTH];
 FILE *hDrive;

 /* Build PC floppy image name */
 sprintf(szDrive,"%s",pszDriveName);
 hDrive = fopen(szDrive, "rwb");
 if (hDrive!=NULL)
   {
    if (CreateDiscImage_LockVolume(hDrive))
      {
#ifdef DEBUG_TO_FILE
       Debug_File("all ok\n");
#endif
       CreateDiscImage_UnLockVolume(hDrive);
      }
     else
      {
#ifdef DEBUG_TO_FILE
       Debug_File("MFMT:Locking volume %s %s failed %d\n", szDrive,pszDriveName, GetLastError());
#endif
      }
   }
  else
   {
#ifdef DEBUG_TO_FILE
    Debug_File("MFMT: Open %s %s failed %d\n", szDrive,pszDriveName, GetLastError());
#endif
   }
}

