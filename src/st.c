/*
  Hatari - st.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  ST Disc support.
*/
char ST_rcsid[] = "Hatari $Id: st.c,v 1.4 2004-04-28 09:04:58 thothy Exp $";

#include "main.h"
#include "file.h"
#include "st.h"

#define SAVE_TO_ST_IMAGES

/*
    .ST FILE FORMAT
  --===============-------------------------------------------------------------

  The file format of the .ST image files used by PaCifiST is simplicity itself;
  they are just straight images of the disk in question, with sectors stored in
  the expected logical order. So, on a sector basis the images run from sector
  0 (bootsector) to however many sectors are on the disk. On a track basis the
  layout is the same as for MSA files but obviously the data is raw, no track
  header or compression or anything like that.

  TRACK 0, SIDE 0
  TRACK 0, SIDE 1
  TRACK 1, SIDE 0
  TRACK 1, SIDE 1
  TRACK 2, SIDE 0
  TRACK 2, SIDE 1
*/


/*-----------------------------------------------------------------------*/
/*
  Does filename end with a .ST extension? If so, return TRUE
*/
BOOL ST_FileNameIsST(char *pszFileName, BOOL bAllowGZ)
{
	return(File_DoesFileExtensionMatch(pszFileName,".st")
	       || (bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".st.gz")));
}


/*-----------------------------------------------------------------------*/
/*
  Load .ST file into memory, set number of bytes loaded and return a pointer
  to the buffer.
*/
Uint8 *ST_ReadDisc(char *pszFileName, long *pImageSize)
{
	void *pStFile;

	*pImageSize = 0;

	/* Just load directly a buffer, and set ImageSize accordingly */
	pStFile = File_Read(pszFileName, NULL, pImageSize, NULL);
	if (!pStFile)
		*pImageSize = 0;

	return(pStFile);
}


/*-----------------------------------------------------------------------*/
/*
  Save .ST file from memory buffer. Returns TRUE is all OK
*/
BOOL ST_WriteDisc(char *pszFileName, Uint8 *pBuffer, int ImageSize)
{
#ifdef SAVE_TO_ST_IMAGES

	/* Just save buffer directly to file */
	return( File_Save(pszFileName, pBuffer, ImageSize, FALSE) );

#else   /*SAVE_TO_ST_IMAGES*/

	/* Oops, cannot save */
	return(FALSE);

#endif  /*SAVE_TO_ST_IMAGES*/
}
