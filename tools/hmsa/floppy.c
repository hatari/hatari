/*
  Hatari tool: Magic Shadow Archiver - floppy.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Check for valid floppy disk geometry.
*/

#include <stdio.h>
#include <SDL_endian.h>

#include "hmsa.h"
#include "floppy.h"


/**
 * Double-check information read from boot-sector as this is sometimes found to
 * be incorrect. The .ST image file should be divisible by the sector size and
 * sectors per track.
 * NOTE - Pass information from boot-sector to this function (if we can't
 * decide we leave it alone).
 */
static void Floppy_DoubleCheckFormat(long nDiskSize, Uint16 *pnSides, Uint16 *pnSectorsPerTrack)
{
	int nSectorsPerTrack;
	long TotalSectors;

	/* Now guess at number of sides */
	if (nDiskSize < (500*1024))                    /* Is size is >500k assume 2 sides to disk! */
		*pnSides = 1;
	else
		*pnSides = 2;

	/* And Sectors Per Track(always 512 bytes per sector) */
	TotalSectors = nDiskSize/512;                 /* # Sectors on disk image */
	/* Does this match up with what we've read from boot-sector? */
	nSectorsPerTrack = *pnSectorsPerTrack;
	if (nSectorsPerTrack==0)                      /* Check valid, default to 9 */
		nSectorsPerTrack = 9;
	if ((TotalSectors%nSectorsPerTrack)!=0)
	{
		/* No, we have an invalid boot-sector - re-calculate from disk size */
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


/**
 * Find details of disk image. We need to do this via a function as sometimes the boot-block
 * is not actually correct with the image - some demos/game disks have incorrect bytes in the
 * boot sector and this attempts to find the correct values.
 */
void Floppy_FindDiskDetails(const Uint8 *pBuffer, int nImageBytes,
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
