/*
  Hatari - createBlankImage.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Create Blank .ST/.MSA Disc Images
*/
char CreateBlankImage_rcsid[] = "Hatari $Id: createBlankImage.c,v 1.6 2003-12-25 14:19:38 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "floppy.h"
#include "memAlloc.h"
#include "misc.h"
#include "msa.h"
#include "st.h"


/*-----------------------------------------------------------------------*/
/*
           40 track SS   40 track DS   80 track SS   80 track DS
 0- 1   Branch instruction to boot program if executable
 2- 7   'Loader'
 8-10   24-bit serial number
11-12   BPS    512           512           512           512
13      SPC     1             2             2             2
14-15   RES     1             1             1             1
16      FAT     2             2             2             2
17-18   DIR     64           112           112           112
19-20   SEC    360           720           720          1440
21      MEDIA  252           253           248           249 (isn't used by ST-BIOS)
22-23   SPF     2             2             5             5
24-25   SPT     9             9             9             9
26-27   SIDE    1             2             1             2
28-29   HID     0             0             0             0
510-511 CHECKSUM
*/


/*-----------------------------------------------------------------------*/
/*
  Calculate the size of a disc in dialog
*/
int CreateBlankImage_GetDiscImageCapacity(int nTracks, int nSectors, int nSides)
{
  /* Find size of disc image */
  return nTracks*nSectors*nSides*NUMBYTESPERSECTOR;
}


/*-----------------------------------------------------------------------*/
/*
  Write a short integer to addr using little endian byte order
  (needed for 16 bit values in the bootsector of the disc image).
*/
static inline void WriteShortLE(void *addr, Uint16 val)
{
  Uint8 *p = (Uint8 *)addr;

  p[0] = (Uint8)val;
  p[1] = (Uint8)(val >> 8);
}


/*-----------------------------------------------------------------------*/
/*
  Create .ST/.MSA disc image according to 'Tracks,Sector,Sides' and save as filename
*/
void CreateBlankImage_CreateFile(char *pszFileName, int nTracks, int nSectors, int nSides)
{
  Uint8 *pDiscFile;
  unsigned long DiscSize;
  unsigned short int SPC,DIR,MEDIA,SPF;
  BOOL bRet=FALSE;

  /* Calculate size of disc image */
  DiscSize = CreateBlankImage_GetDiscImageCapacity(nTracks, nSectors, nSides);

  /* Allocate space for our 'file', and blank */
  pDiscFile = (Uint8 *)Memory_Alloc(DiscSize);
  Memory_Clear(pDiscFile,DiscSize);

  /* Fill in boot-sector, this would better as a structure but 'C' pads the variables out */
  Memory_Set(pDiscFile+2,0x4e,6);                           /* 2-7 'Loader' */
  WriteShortLE(pDiscFile+8, rand());                        /* 8-10 24-bit serial number */
  *(Uint8 *)(pDiscFile+10) = rand();
  WriteShortLE(pDiscFile+11, NUMBYTESPERSECTOR);            /* 11-12 BPS */

  if ( (nTracks==40) && (nSides==1) )
    SPC = 1;
  else
    SPC = 2;
  *(Uint8 *)(pDiscFile+13) = SPC;                           /* 13 SPC */

  WriteShortLE(pDiscFile+14, 1);                            /* 14-15 RES */
  *(Uint8 *)(pDiscFile+16) = 2;                             /* 16 FAT */

  if (SPC==1)
    DIR = 64;
  else
    DIR = 112;
  WriteShortLE(pDiscFile+17, DIR);                          /* 17-18 DIR */

  WriteShortLE(pDiscFile+19, nTracks*nSectors*nSides);      /* 19-20 SEC */

  if (nTracks==40)
    MEDIA = 252;
  else
    MEDIA = 248;
  if (nSides==2)
    MEDIA++;
  *(Uint8 *)(pDiscFile+21) = MEDIA;                         /* 21 MEDIA */

  if (nTracks>=80)
    SPF = 5;
  else
    SPF = 2;
  WriteShortLE(pDiscFile+22, SPF);                          /* 22-23 SPF */

  WriteShortLE(pDiscFile+24, nSectors);                     /* 24-25 SPT */
  WriteShortLE(pDiscFile+26, nSides);                       /* 26-27 SIDE */
  WriteShortLE(pDiscFile+28, 0);                            /* 28-29 HID */

  /* Ask if OK to overwrite, if exists? */
  if (File_QueryOverwrite(pszFileName))
  {
    /* Save image to file, as .ST or compressed .MSA */
    if (File_FileNameIsMSA(pszFileName))
      bRet = MSA_WriteDisc(pszFileName,pDiscFile,DiscSize);
    else if (File_FileNameIsST(pszFileName))
      bRet = ST_WriteDisc(pszFileName,pDiscFile,DiscSize);

    /* Did create successfully? */
    if (bRet)
    {
      /* Say OK, */
      Main_Message("Disc image created successfully.", PROG_NAME /*,MB_OK | MB_ICONINFORMATION*/);
    }
    else
    {
      char *szString = Memory_Alloc(FILENAME_MAX + 32);
      /* Warn user we were unable to create image */
      sprintf(szString, "Unable to create disc image '%s'.", pszFileName);
      Main_Message(szString, PROG_NAME /*,MB_OK | MB_ICONSTOP*/);
      Memory_Free(szString);
    }
  }

  /* Free image */
  Memory_Free(pDiscFile);
}
