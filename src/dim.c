/*
  Hatari - dim.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  DIM Disc support.
*/
char DIM_rcsid[] = "Hatari $Id: dim.c,v 1.3 2005-02-13 16:18:48 thothy Exp $";

#include <zlib.h>

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "dim.h"

#undef SAVE_TO_DIM_IMAGES

/*
    .DIM FILE FORMAT
  --===============-------------------------------------------------------------

  The file format of the .DIM image files are quite the same as the .ST image
  files (see st.c) - the .DIM image files just have an additional header of
  32 bytes.
  
  The header contains following information:

  Offset  Size  Description
  ------  ----  -----------
  0x0000  Word  ID Header (0x4242('BB'))
  0x0003  Byte  Image contains all sectors (0) or only used sectors (1)
  0x0006  Byte  Sides (0 or 1; add 1 to this to get correct number of sides)
  0x0008  Byte  Sectors per track
  0x000A  Byte  Starting Track (0 based)
  0x000C  Byte  Ending Track (0 based)
  0x000D  Byte  Double-Density(0) or High-Density (1)

  All other header fields are unknown.
  If you have information about them, please help!
*/


/*-----------------------------------------------------------------------*/
/*
  Does filename end with a .DIM extension? If so, return TRUE
*/
BOOL DIM_FileNameIsDIM(char *pszFileName, BOOL bAllowGZ)
{
	return(File_DoesFileExtensionMatch(pszFileName,".dim")
	       || (bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".dim.gz")));
}


/*-----------------------------------------------------------------------*/
/*
  Load .DIM file into memory, set number of bytes loaded and return a pointer
  to the buffer.
*/
Uint8 *DIM_ReadDisc(char *pszFileName, long *pImageSize)
{
	Uint8 *pDimFile;
	Uint8 *pDiscBuffer = NULL;

	/* Load file into buffer */
	pDimFile = File_Read(pszFileName, NULL, pImageSize, NULL);
	if (pDimFile)
	{
		/* Check header for valid image: */
		if (*(Uint16 *)pDimFile != 0x4242 || pDimFile[0x03] != 0 || pDimFile[0x0A] != 0)
		{
			fprintf(stderr, "This is not a valid DIM image!\n");
			*pImageSize = 0;
			free(pDimFile);
			return NULL;
		}

		/* Simply use disc contents without the DIM header: */
		*pImageSize -= 32;
		pDiscBuffer = malloc(*pImageSize);
		if (pDiscBuffer)
			memcpy(pDiscBuffer, pDimFile+32, *pImageSize);
		else
			perror("DIM_ReadDisc");

		/* Free DIM file we loaded */
		free(pDimFile);
	}

	if (pDiscBuffer == NULL)
	{
		*pImageSize = 0;
	}

	return(pDiscBuffer);
}


/*-----------------------------------------------------------------------*/
/*
  Save .DIM file from memory buffer. Returns TRUE is all OK
*/
BOOL DIM_WriteDisc(char *pszFileName, unsigned char *pBuffer, int ImageSize)
{
#ifdef SAVE_TO_DIM_IMAGES

	Uint8 *pDimFile;
	gzFile hGzFile;
	unsigned short int nSectorsPerTrack, nSides;
	int nTracks;
	BOOL bRet;

	/* Allocate memory for the whole DIM image: */
	pDimFile = malloc(ImageSize + 32);
	if (!pDimFile)
	{
		perror("DIM_WriteDisc");
		return FALSE;
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
	Floppy_FindDiscDetails(pBuffer, ImageSize, &nSectorsPerTrack, &nSides);
	nTracks = ((ImageSize / NUMBYTESPERSECTOR) / nSectorsPerTrack) / nSides;
	pDimFile[0x00] = pDimFile[0x01] = 0x42;     /* ID */
	pDimFile[0x03] = 0;                         /* Image contains all sectors */
	pDimFile[0x06] = nSides - 1;                /* Sides */
	pDimFile[0x08] = nSectorsPerTrack;          /* Sectors per track */
	pDimFile[0x0A] = 0;                         /* Starting track */
	pDimFile[0x0C] = nTracks - 1;               /* Ending track */
	pDimFile[0x0D] = (ImageSize > 1024*1024);   /* DD / HD flag */

	/* Now copy the disc data: */
	memcpy(pDimFile + 32, pBuffer, ImageSize);
	
	/* And finally save it: */
	bRet = File_Save(pszFileName, pDimFile, ImageSize + 32, FALSE);

	free(pDimFile);

	return bRet;

#else   /*SAVE_TO_ST_IMAGES*/

	/* Oops, cannot save */
	return FALSE;

#endif  /*SAVE_TO_ST_IMAGES*/
}
