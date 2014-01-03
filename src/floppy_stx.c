/*
  Hatari - floppy_stx.c

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  STX disk image support.

  STX files are created using the program 'Pasti' made by Ijor.
  As no official documentation exists, this file is based on the reverse
  engineering and docs made by the following people :
   - Markus Fritze (Sarnau)
   - P. Putnik
   - Jean Louis Guerin (Dr CoolZic)
*/
const char floppy_stx_fileid[] = "Hatari floppy_stx.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "floppy_stx.h"
#include "fdc.h"
#include "log.h"
#include "memorySnapShot.h"
#include "screen.h"
#include "video.h"
#include "cycles.h"



typedef struct
{
} STX_STRUCT;


static STX_STRUCT	STX_State;			/* All variables related to the STX support */



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void STX_MemorySnapShot_Capture(bool bSave)
{
	int	Drive;

	if ( bSave )					/* Saving snapshot */
	{
		MemorySnapShot_Store(&STX_State, sizeof(STX_State));
	}

	else						/* Restoring snapshot */
	{
		MemorySnapShot_Store(&STX_State, sizeof(STX_State));

		/* Call STX_Insert to recompute STX_State */
		for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
			if ( EmulationDrives[Drive].ImageType == FLOPPY_IMAGE_TYPE_STX )
				if ( STX_Insert ( Drive , EmulationDrives[Drive].pBuffer , EmulationDrives[Drive].nImageBytes ) == false )
				{
					Log_AlertDlg(LOG_ERROR, "Error restoring STX image %s in drive %d" ,
						EmulationDrives[Drive].sFileName , Drive );
					return;
				}

		fprintf ( stderr , "stx load ok\n" );
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a .STX extension? If so, return true.
 */
bool STX_FileNameIsSTX(const char *pszFileName, bool bAllowGZ)
{
	return(File_DoesFileExtensionMatch(pszFileName,".stx")
	       || (bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".stx.gz")));
}


/*-----------------------------------------------------------------------*/
/**
 * Load .STX file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
Uint8 *STX_ReadDisk(const char *pszFileName, long *pImageSize, int *pImageType)
{
	Uint8 *pSTXFile;

	*pImageSize = 0;

	/* Just load directly a buffer, and set ImageSize accordingly */
	pSTXFile = File_Read(pszFileName, pImageSize, NULL);
	if (!pSTXFile)
	{
		*pImageSize = 0;
		return NULL;
	}
	
	*pImageType = FLOPPY_IMAGE_TYPE_STX;
	return pSTXFile;
}


/*-----------------------------------------------------------------------*/
/**
 * Save .STX file from memory buffer. Returns true is all OK.
 */
bool STX_WriteDisk(const char *pszFileName, Uint8 *pBuffer, int ImageSize)
{
	/* saving is not supported for STX files */
	return false;
}




/*
 * Init variables used to handle STX images
 */
bool	STX_Init ( void )
{
	return true;
}




/*
 * Init the ressources to handle the STX image inserted into a drive (0=A: 1=B:)
 */
bool	STX_Insert ( int Drive , Uint8 *pImageBuffer , long ImageSize )
{
	fprintf ( stderr , "STX : STX_Insert drive=%d buf=%p size=%ld\n" , Drive , pImageBuffer , ImageSize );

	return true;
}




/*
 * When ejecting a disk, free the ressources associated with an STX image
 */
bool	STX_Eject ( int Drive )
{
	fprintf ( stderr , "STX : STX_Eject drive=%d\n" , Drive );

	return true;
}



