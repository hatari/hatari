/*
  Hatari - dim.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  DIM Disc support.
*/
char DIM_rcsid[] = "Hatari $Id: dim.c,v 1.1 2004-04-28 09:04:57 thothy Exp $";

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "memAlloc.h"
#include "dim.h"

#undef SAVE_TO_DIM_IMAGES

/*
    .DIM FILE FORMAT
  --===============-------------------------------------------------------------

  The file format of the .DIM image files are quite the same as the .ST image
  files (see st.c) - the .DIM image files just have an additional header of
  32 bytes.
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
		/* Simply use disc contents without the DIM header: */
		*pImageSize -= 32;
		pDiscBuffer = malloc (*pImageSize);
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

	/* Oops, cannot save yet */
	return(FALSE);

#else   /*SAVE_TO_ST_IMAGES*/

	/* Oops, cannot save */
	return(FALSE);

#endif  /*SAVE_TO_ST_IMAGES*/
}
