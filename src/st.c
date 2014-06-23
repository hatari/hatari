/*
  Hatari - st.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  ST disk image support.

  The file format of the .ST image files is simplicity itself. They are just
  straight images of the disk in question, with sectors stored in the expected
  logical order.
  So, on a sector basis the images run from sector 0 (bootsector) to however
  many sectors are on the disk. On a track basis the layout is the same as for
  MSA files but obviously the data is raw, no track header or compression or
  anything like that.

  TRACK 0, SIDE 0
  TRACK 0, SIDE 1
  TRACK 1, SIDE 0
  TRACK 1, SIDE 1
  TRACK 2, SIDE 0
  TRACK 2, SIDE 1
*/
const char ST_fileid[] = "Hatari st.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "st.h"

#define SAVE_TO_ST_IMAGES


#if defined(__riscos)
/* The following two lines are required on RISC OS for preventing it from
 * interfering with the .ST image files: */
#include <unixlib/local.h>
int __feature_imagefs_is_file = 1;
#endif


/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a .ST extension? If so, return true.
 */
bool ST_FileNameIsST(const char *pszFileName, bool bAllowGZ)
{
	return(File_DoesFileExtensionMatch(pszFileName,".st")
	       || (bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".st.gz")));
}


/*-----------------------------------------------------------------------*/
/**
 * Load .ST file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
Uint8 *ST_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType)
{
	Uint8 *pStFile;

	*pImageSize = 0;

	/* Just load directly a buffer, and set ImageSize accordingly */
	pStFile = File_Read(pszFileName, pImageSize, NULL);
	if (!pStFile)
	{
		*pImageSize = 0;
		return NULL;
	}

	*pImageType = FLOPPY_IMAGE_TYPE_ST;
	return pStFile;
}


/*-----------------------------------------------------------------------*/
/**
 * Save .ST file from memory buffer. Returns true is all OK.
 */
bool ST_WriteDisk(int Drive, const char *pszFileName, Uint8 *pBuffer, int ImageSize)
{
#ifdef SAVE_TO_ST_IMAGES

	/* Just save buffer directly to file */
	return File_Save(pszFileName, pBuffer, ImageSize, false);

#else   /*SAVE_TO_ST_IMAGES*/

	/* Oops, cannot save */
	return false;

#endif  /*SAVE_TO_ST_IMAGES*/
}
