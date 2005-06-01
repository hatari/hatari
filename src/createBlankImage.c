/*
  Hatari - createBlankImage.c
 
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
 
  Create Blank .ST/.MSA Disc Images
*/
char CreateBlankImage_rcsid[] = "Hatari $Id: createBlankImage.c,v 1.11 2005-06-01 13:44:39 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dim.h"
#include "file.h"
#include "floppy.h"
#include "misc.h"
#include "msa.h"
#include "st.h"
#include "createBlankImage.h"

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
21      MEDIA  $FC           $FD           $F8           $F9  (isn't used by ST-BIOS)
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
static int CreateBlankImage_GetDiscImageCapacity(int nTracks, int nSectors, int nSides)
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
	unsigned short int SPC, nDir, MediaByte, SPF;
	BOOL bRet=FALSE;

	/* Calculate size of disc image */
	DiscSize = CreateBlankImage_GetDiscImageCapacity(nTracks, nSectors, nSides);

	/* Allocate space for our 'file', and blank */
	pDiscFile = malloc(DiscSize);
	if (pDiscFile == NULL)
	{
		perror("Error while creating blank disc image");
		return;
	}
	memset(pDiscFile, 0, DiscSize);                       /* Clear buffer */

	/* Fill in boot-sector */
	pDiscFile[0] = 0xE9;                                  /* Needed for MS-DOS compatibility */
	memset(pDiscFile+2, 0x4e, 6);                         /* 2-7 'Loader' */

	WriteShortLE(pDiscFile+8, rand());                    /* 8-10 24-bit serial number */
	pDiscFile[10] = rand();

	WriteShortLE(pDiscFile+11, NUMBYTESPERSECTOR);        /* 11-12 BPS */

	if ((nTracks == 40) && (nSides == 1))
		SPC = 1;
	else
		SPC = 2;
	pDiscFile[13] = SPC;                                  /* 13 SPC */

	WriteShortLE(pDiscFile+14, 1);                        /* 14-15 RES */
	pDiscFile[16] = 2;                                    /* 16 FAT */

	if (SPC==1)
		nDir = 64;
	else
		nDir = 112;
	WriteShortLE(pDiscFile+17, nDir);                     /* 17-18 DIR */

	WriteShortLE(pDiscFile+19, nTracks*nSectors*nSides);  /* 19-20 SEC */

	if (nTracks <= 42)
		MediaByte = 0xFC;
	else
		MediaByte = 0xF8;
	if (nSides == 2)
		MediaByte |= 0x01;
	pDiscFile[21] = MediaByte;                            /* 21 MEDIA */

	if (nTracks >= 80)
		SPF = 5;
	else
		SPF = 2;
	WriteShortLE(pDiscFile+22, SPF);                      /* 22-23 SPF */

	WriteShortLE(pDiscFile+24, nSectors);                 /* 24-25 SPT */
	WriteShortLE(pDiscFile+26, nSides);                   /* 26-27 SIDE */
	WriteShortLE(pDiscFile+28, 0);                        /* 28-29 HID */

	/* Set correct media bytes in the 1st FAT: */
	pDiscFile[512] = MediaByte;
	pDiscFile[513] = pDiscFile[514] = 0xFF;
	/* Set correct media bytes in the 2nd FAT: */
	pDiscFile[512 + SPF * 512] = MediaByte;
	pDiscFile[513 + SPF * 512] = pDiscFile[514 + SPF * 512] = 0xFF;

	/* Ask if OK to overwrite, if exists? */
	if (File_QueryOverwrite(pszFileName))
	{
		/* Save image to file */
		if (MSA_FileNameIsMSA(pszFileName, TRUE))
			bRet = MSA_WriteDisc(pszFileName, pDiscFile, DiscSize);
		else if (ST_FileNameIsST(pszFileName, TRUE))
			bRet = ST_WriteDisc(pszFileName, pDiscFile, DiscSize);
		else if (DIM_FileNameIsDIM(pszFileName, TRUE))
			bRet = DIM_WriteDisc(pszFileName, pDiscFile, DiscSize);

		/* Did create successfully? */
		if (bRet)
		{
			/* Say OK, */
			Main_Message("Disc image created successfully.", PROG_NAME /*,MB_OK | MB_ICONINFORMATION*/);
		}
		else
		{
			/* Warn user we were unable to create image */
			Main_Message("Unable to create disc image!", PROG_NAME /*,MB_OK | MB_ICONSTOP*/);
		}
	}

	/* Free image */
	free(pDiscFile);
}
