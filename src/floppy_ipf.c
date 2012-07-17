/*
  Hatari - floppy_ipf.c

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  IPF disk image support.

  IPF files are handled using the capsimage library, which emulates the FDC
  at low level and allows to read complex protections.
*/
const char floppy_ipf_fileid[] = "Hatari floppy_ipf.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "floppy_ipf.h"
#include "log.h"

#ifdef HAVE_CAPSIMAGE
#include <caps/capsimage.h>
#endif



/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a .IPF extension? If so, return true.
 */
bool IPF_FileNameIsIPF(const char *pszFileName, bool bAllowGZ)
{
	return(File_DoesFileExtensionMatch(pszFileName,".ipf")
	       || (bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".ipf.gz")));
}


/*-----------------------------------------------------------------------*/
/**
 * Load .IPF file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
Uint8 *IPF_ReadDisk(const char *pszFileName, long *pImageSize, int *pImageType)
{
#ifndef HAVE_CAPSIMAGE
	Log_AlertDlg(LOG_ERROR, "This version of Hatari was not built with IPF support, this disk image can't be handled.");
	return NULL;

#else
	Uint8 *pIPFFile;

	*pImageSize = 0;

	/* Just load directly a buffer, and set ImageSize accordingly */
	pIPFFile = File_Read(pszFileName, pImageSize, NULL);
	if (!pIPFFile)
	{
		*pImageSize = 0;
		return NULL;
	}
	
	*pImageType = FLOPPY_IMAGE_TYPE_IPF;
	return pIPFFile;
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Save .IPF file from memory buffer. Returns true is all OK.
 */
bool IPF_WriteDisk(const char *pszFileName, Uint8 *pBuffer, int ImageSize)
{
	/* saving is not supported for IPF files */
	return false;
}



bool	IPF_Insert ( Uint8 *pImageBuffer , long ImageSize )
{
#ifndef HAVE_CAPSIMAGE
	return false;

#else
		/* */
	int i, id = CAPSAddImage();

        if ( CAPSLockImageMemory ( id , pImageBuffer , (CapsULong)ImageSize , DI_LOCK_MEMREF ) == imgeOk)
        {
                struct CapsImageInfo cii;

                if (CAPSGetImageInfo(&cii, id) == imgeOk)
                {
                        printf("Type: %d\n", (int)cii.type);
                        printf("Release: %d\n", (int)cii.release);
                        printf("Revision: %d\n", (int)cii.revision);
                        printf("Min Cylinder: %d\n", (int)cii.mincylinder);
                        printf("Max Cylinder: %d\n", (int)cii.maxcylinder);
                        printf("Min Head: %d\n", (int)cii.minhead);
                        printf("Max Head: %d\n", (int)cii.maxhead);
                        printf("Creation Date: %04d/%02d/%02d %02d:%02d:%02d.%03d\n", (int)cii.crdt.year, (int)cii.crdt.month, (int)cii.crdt.day, (int)cii.crdt.hour, (int)cii.crdt.min, (int)cii.crdt.sec, (int)cii.crdt.tick);
                        printf("Platforms:");
                        for (i = 0; i < CAPS_MAXPLATFORM; i++)
                                if (cii.platform[i] != ciipNA)
                                        printf(" %s", CAPSGetPlatformName(cii.platform[i]));
                        printf("\n");
                }
                CAPSUnlockImage(id);
        }
        CAPSRemImage(id);

	
	return true;
#endif
}



