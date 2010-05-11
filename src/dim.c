/*
  Hatari - dim.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  DIM disk image support.
*/
const char DIM_fileid[] = "Hatari dim.c : " __DATE__ " " __TIME__;

#include <zlib.h>

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "dim.h"

#undef SAVE_TO_DIM_IMAGES

/*
    .DIM FILE FORMAT
  --===============-------------------------------------------------------------

  The file format of normal .DIM image files are quite the same as the .ST image
  files (see st.c) - the .DIM image files just have an additional header of
  32 bytes. However, there are also "compressed" images which only contain the
  used sectors of the disk. It is necessary to parse the FAT to "uncompress"
  these images.

  The header contains following information:

  Offset  Size      Description
  ------  --------  -----------
  0x0000  Word      ID Header (0x4242('BB'))
  0x0002  Byte      1 = disk configuration has been detected automatically
                    0 = the user specified the disk configuration
  0x0003  Byte      Image contains all sectors (0) or only used sectors (1)
  0x0006  Byte      Sides (0 or 1; add 1 to this to get correct number of sides)
  0x0008  Byte      Sectors per track
  0x000A  Byte      Starting Track (0 based)
  0x000C  Byte      Ending Track (0 based)
  0x000D  Byte      Double-Density(0) or High-Density (1)
  0x000E  18 Bytes  A copy of the Bios Parameter Block (BPB) of this disk.
*/


/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a .DIM extension? If so, return TRUE
 */
bool DIM_FileNameIsDIM(const char *pszFileName, bool bAllowGZ)
{
	return(File_DoesFileExtensionMatch(pszFileName,".dim")
	       || (bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".dim.gz")));
}


/*-----------------------------------------------------------------------*/
/**
 * Load .DIM file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
Uint8 *DIM_ReadDisk(const char *pszFileName, long *pImageSize)
{
	Uint8 *pDimFile;
	Uint8 *pDiskBuffer = NULL;

	/* Load file into buffer */
	pDimFile = File_Read(pszFileName, pImageSize, NULL);
	if (pDimFile)
	{
		/* Check header for valid image: */
		if (pDimFile[0x00] != 0x42 || pDimFile[0x01] != 0x42 ||
		    pDimFile[0x03] != 0 || pDimFile[0x0A] != 0)
		{
			fprintf(stderr, "This is not a valid DIM image!\n");
			*pImageSize = 0;
			free(pDimFile);
			return NULL;
		}

		/* Simply use disk contents without the DIM header: */
		*pImageSize -= 32;
		pDiskBuffer = malloc(*pImageSize);
		if (pDiskBuffer)
			memcpy(pDiskBuffer, pDimFile+32, *pImageSize);
		else
			perror("DIM_ReadDisk");

		/* Free DIM file we loaded */
		free(pDimFile);
	}

	if (pDiskBuffer == NULL)
	{
		*pImageSize = 0;
	}

	return pDiskBuffer;
}


/*-----------------------------------------------------------------------*/
/**
 * Save .DIM file from memory buffer. Returns TRUE is all OK
 */
bool DIM_WriteDisk(const char *pszFileName, Uint8 *pBuffer, int ImageSize)
{
#ifdef SAVE_TO_DIM_IMAGES

	Uint8 *pDimFile;
	gzFile hGzFile;
	unsigned short int nSectorsPerTrack, nSides;
	int nTracks;
	bool bRet;

	/* Allocate memory for the whole DIM image: */
	pDimFile = malloc(ImageSize + 32);
	if (!pDimFile)
	{
		perror("DIM_WriteDisk");
		return false;
	}

	/* Try to load the old header data to preserve the header fields that are unknown yet: */
    hGzFile = gzopen(pszFileName, "rb");
    if (hGzFile != NULL)
    {
		gzread(hGzFile, pDimFile, 32);
		gzclose(hGzFile);
	}
	else
	{
		memset(pDimFile, 0, 32);
	}

	/* Now fill in the new header information: */
	Floppy_FindDiskDetails(pBuffer, ImageSize, &nSectorsPerTrack, &nSides);
	nTracks = ((ImageSize / NUMBYTESPERSECTOR) / nSectorsPerTrack) / nSides;
	pDimFile[0x00] = pDimFile[0x01] = 0x42;     /* ID */
	pDimFile[0x03] = 0;                         /* Image contains all sectors */
	pDimFile[0x06] = nSides - 1;                /* Sides */
	pDimFile[0x08] = nSectorsPerTrack;          /* Sectors per track */
	pDimFile[0x0A] = 0;                         /* Starting track */
	pDimFile[0x0C] = nTracks - 1;               /* Ending track */
	pDimFile[0x0D] = (ImageSize > 1024*1024);   /* DD / HD flag */

	/* Now copy the disk data: */
	memcpy(pDimFile + 32, pBuffer, ImageSize);
	
	/* And finally save it: */
	bRet = File_Save(pszFileName, pDimFile, ImageSize + 32, false);

	free(pDimFile);

	return bRet;

#else   /*SAVE_TO_ST_IMAGES*/

	/* Oops, cannot save */
	return false;

#endif  /*SAVE_TO_ST_IMAGES*/
}
