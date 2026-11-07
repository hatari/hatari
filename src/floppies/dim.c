/*
  Hatari - dim.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  DIM disk image support.
*/
const char DIM_fileid[] = "Hatari dim.c";

#include <assert.h>

#include "main.h"
#include "file.h"
#include "filename.h"
#include "floppy.h"
#include "floppies/dim.h"

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
  0x0002  Byte      1 = disk configuration has been detected automatically (from the BPB)
                    0 = the user specified the disk configuration
  0x0003  Byte      Image contains all sectors (0) or only used sectors (1)
  0x0004  Word      Reserved, always zero
  0x0006  Byte      Sides (0 or 1; add 1 to this to get correct number of sides)
  0x0007  Byte      Unused, always zero
  0x0008  Byte      Sectors per track
  0x0009  Byte      Unused, always zero
  0x000A  Byte      Starting Track (0 based)
  0x000B  Byte      Unused, always zero
  0x000C  Byte      Ending Track (0 based)
  0x000D  Byte      Double-Density(0) or High-Density (1)
  0x000E  18 Bytes  A copy of the Bios Parameter Block (BPB) of this disk.

  The BPB is stored as nine 68000 words:

  0X000E  recsiz    Bytes per sector
  0X0010  clsiz     Sectors per cluster
  0X0012  clsizb    Bytes per cluster
  0X0014  rdlen     Root directory length in sectors
  0X0016  fsiz      FAT size in sectors
  0X0018  fatrec    First sector of the second FAT (the one TOS uses)
  0X001A  datrec    First data sector
  0X001C  numcl     Number of data clusters
  0X001E  bflags    Bit 0 clear for a 12-bit FAT, set for a 16-bit FAT

  FastCopy has three imaging modes, given by the flags at $02 and $03:

  $02 $03
   0   0   Geometry typed in by the user. Only tracks 'Starting track' to
             'Ending track' are stored, so the image may be part of a disc. The
              BPB is still filled in but cannot be relied on.
   1   0   Geometry from the BPB. The whole disc is stored.
   1   1   Geometry from the BPB and FAT. Only used clusters are stored.

  Sector data follows the header, each track in turn with sides alternating as in
  a '.ST' image, starting at 'Starting track'.

  For a 'Get Sectors Used' image the boot sector, both FATs and the root directory
  (sectors 0 to datrec-1) are stored in full. These are followed only by the
  clusters marked as used in the FAT, in cluster order, each stored in full
  (clsiz sectors). Clusters marked free($000) or bad($FF0-$FF7) are not stored.


  We take a copy of the header while reading the disk image, so we can
  preserve the unknown field when saving the disk image later.

*/
#define DIM_HEADER_SIZE 32

#ifdef SAVE_TO_DIM_IMAGES
static struct {
	char filename[FILENAME_MAX];
	uint8_t data[DIM_HEADER_SIZE];
} headers[MAX_FLOPPYDRIVES];
#endif

/**
 * Does filename end with a .DIM extension? If so, return TRUE
 */
bool DIM_FileNameIsDIM(const char *pszFileName, bool bAllowGZ)
{
	return FileName_DoesExtensionMatch(pszFileName, ".dim")
	       || (bAllowGZ && FileName_DoesExtensionMatch(pszFileName, ".dim.gz"));
}


/**
 * Load .DIM file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
uint8_t *DIM_ReadDisk(int drive, const char *pszFileName, long *pImageSize, int *pImageType)
{
	uint8_t *pDimFile;
	uint8_t *pDiskBuffer = NULL;

	assert(drive < MAX_FLOPPYDRIVES);

#ifdef SAVE_TO_DIM_IMAGES
	headers[drive].filename[0] = 0;		/* Invalidate header cache */
#endif

	/* Load file into buffer */
	pDimFile = File_Read(pszFileName, pImageSize, NULL);
	if (pDimFile)
	{
		/* Check header for valid image: */
		if (*pImageSize < DIM_HEADER_SIZE + 512 ||
		    pDimFile[0x00] != 0x42 || pDimFile[0x01] != 0x42 ||
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
		{
			memcpy(pDiskBuffer, pDimFile + DIM_HEADER_SIZE, *pImageSize);
#ifdef SAVE_TO_DIM_IMAGES
			memcpy(headers[drive].data, pDimFile, DIM_HEADER_SIZE);
			strncpy(headers[drive].filename, pszFileName, FILENAME_MAX - 1);
			headers[drive].filename[FILENAME_MAX - 1] = 0;
#endif
		}
		else
		{
			perror("DIM_ReadDisk");
		}

		/* Free DIM file we loaded */
		free(pDimFile);
	}

	if (pDiskBuffer == NULL)
	{
		*pImageSize = 0;
		return NULL;
	}

	*pImageType = FLOPPY_IMAGE_TYPE_DIM;
	return pDiskBuffer;
}


/*-----------------------------------------------------------------------*/
/**
 * Save .DIM file from memory buffer. Returns TRUE is all OK
 */
bool DIM_WriteDisk(int drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize)
{
#ifdef SAVE_TO_DIM_IMAGES

	unsigned short int nSectorsPerTrack, nSides;
	uint8_t *pDimFile;
	int nTracks;
	bool bRet;

	assert(drive < MAX_FLOPPYDRIVES);

	/* Allocate memory for the whole DIM image: */
	pDimFile = malloc(ImageSize + 32);
	if (!pDimFile)
	{
		perror("DIM_WriteDisk");
		return false;
	}

	/* Try to preserve the old header data for fields that are unknown yet: */
	if (strcmp(pszFileName, headers[drive].filename) == 0)
		memcpy(pDimFile, headers[drive].data, DIM_HEADER_SIZE);
	else
		memset(pDimFile, 0, DIM_HEADER_SIZE);

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
