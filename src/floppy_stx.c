/*
  Hatari - floppy_stx.c

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  STX disk image support.

  STX files are created using the program 'Pasti' made by Jorge Cwik (Ijor).
  As no official documentation exists, this file is based on the reverse
  engineering and docs made by the following people, mainly using Pasti 0.4b :
   - Markus Fritze (Sarnau)
   - P. Putnik
   - Jean Louis Guerin (Dr CoolZic)
   - Nicolas Pomarede
*/
const char floppy_stx_fileid[] = "Hatari floppy_stx.c";

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "floppy_stx.h"
#include "fdc.h"
#include "log.h"
#include "memorySnapShot.h"
#include "video.h"
#include "cycles.h"
#include "str.h"
#include "utils.h"


#define	STX_DEBUG_FLAG_STRUCTURE	1
#define	STX_DEBUG_FLAG_DATA		2

#define	STX_DEBUG_FLAG			0
// #define	STX_DEBUG_FLAG			( STX_DEBUG_FLAG_STRUCTURE )
// #define	STX_DEBUG_FLAG			( STX_DEBUG_FLAG_STRUCTURE | STX_DEBUG_FLAG_DATA )


#define FDC_DELAY_CYCLE_MFM_BIT			( 4 * 8 )	/* 4 us per bit, 8 MHz clock -> 32 cycles */
#define FDC_DELAY_CYCLE_MFM_BYTE		( 4 * 8 * 8 )	/* 4 us per bit, 8 bits per byte, 8 MHz clock -> 256 cycles */

#define FDC_TRACK_BYTES_STANDARD	6250


#define	WD1772_SAVE_FILE_EXT		".wd1772"
#define	WD1772_SAVE_FILE_ID		"WD1772"		/* 6 bytes */
#define	WD1772_SAVE_VERSION		1
#define	WD1772_SAVE_REVISION		0
#define	WD1772_SAVE_SECTOR_ID		"SECT"			/* 4 bytes */
#define	WD1772_SAVE_TRACK_ID		"TRCK"			/* 4 bytes */


typedef struct
{
	STX_MAIN_STRUCT		*ImageBuffer[ MAX_FLOPPYDRIVES ];	/* For the STX disk images */

	uint32_t			NextSectorStruct_Nbr;		/* Sector Number in pSectorsStruct after a call to FDC_NextSectorID_FdcCycles_STX() */
	uint8_t			NextSector_ID_Field_TR;		/* Track value in the next ID Field after a call to FDC_NextSectorID_FdcCycles_STX() */
	uint8_t			NextSector_ID_Field_SR;		/* Sector value in the next ID Field after a call to FDC_NextSectorID_FdcCycles_STX() */
	uint8_t			NextSector_ID_Field_LEN;	/* Sector's length in the next ID Field after a call to FDC_NextSectorID_FdcCycles_STX() */
	uint8_t			NextSector_ID_Field_CRC_OK;	/* CRC OK or not in the next ID Field after a call to FDC_NextSectorID_FdcCycles_STX() */

} STX_STRUCT;


static STX_STRUCT	STX_State;			/* All variables related to the STX support */

static STX_SAVE_STRUCT	STX_SaveStruct[ MAX_FLOPPYDRIVES ];	/* To save 'write sector' data */



/* Default timing table for Macrodos when revision=0 */
/* 1 unit of timing means 32 FDC cycles ; + 28 cycles every 16 bytes, so a standard block of 16 bytes */
/* should have a value of 0x7f or 0x80, which gives 4092-4124 cycles */
static uint8_t	TimingDataDefault[] = {
	0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,
	0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,
	0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,
	0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f
	};



/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static bool	STX_LoadSaveFile ( int Drive , const char *FilenameSave );
static bool	STX_LoadSaveFile_SECT ( int Drive, STX_SAVE_SECTOR_STRUCT *pStxSaveSector , uint8_t *p );
static bool	STX_LoadSaveFile_TRCK ( int Drive , STX_SAVE_TRACK_STRUCT *pStxSaveTrack , uint8_t *p );

static bool	STX_Insert_internal ( int Drive , const char *FilenameSTX , uint8_t *pImageBuffer , long ImageSize );

static uint16_t	STX_ReadU16_LE ( uint8_t *p );
static uint32_t	STX_ReadU32_LE ( uint8_t *p );
static uint16_t	STX_ReadU16_BE ( uint8_t *p );
static uint32_t	STX_ReadU32_BE ( uint8_t *p );
static void	STX_WriteU16_BE ( uint8_t *p , uint16_t val );
static void	STX_WriteU32_BE ( uint8_t *p , uint32_t val );

static void	STX_FreeStruct ( STX_MAIN_STRUCT *pStxMain );
static void	STX_FreeSaveStruct ( int Drive );
static void	STX_FreeSaveSectorsStructAll ( STX_SAVE_SECTOR_STRUCT *pSaveSectorsStruct , uint32_t SaveSectorsCount );
static void	STX_FreeSaveSectorsStruct ( STX_SAVE_SECTOR_STRUCT *pSaveSectorsStruct , int Nb );
static void	STX_FreeSaveTracksStructAll ( STX_SAVE_TRACK_STRUCT *pSaveTracksStruct , uint32_t SaveTracksCount );
static void	STX_FreeSaveTracksStruct ( STX_SAVE_TRACK_STRUCT *pSaveTracksStruct , int Nb );

static void	STX_BuildSectorsSimple ( STX_TRACK_STRUCT *pStxTrack , uint8_t *p );
static uint16_t	STX_BuildSectorID_CRC ( STX_SECTOR_STRUCT *pStxSector );
static STX_TRACK_STRUCT	*STX_FindTrack ( uint8_t Drive , uint8_t Track , uint8_t Side );
static STX_SECTOR_STRUCT *STX_FindSector ( uint8_t Drive , uint8_t Track , uint8_t Side , uint8_t SectorStruct_Nb );
static STX_SECTOR_STRUCT *STX_FindSector_By_Position ( uint8_t Drive , uint8_t Track , uint8_t Side , uint16_t BitPosition );



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void STX_MemorySnapShot_Capture(bool bSave)
{
	int	Drive;
	uint32_t	i;
	STX_SECTOR_STRUCT	*pStxSector;
	STX_TRACK_STRUCT	*pStxTrack;

	if ( bSave )					/* Saving snapshot */
	{
		MemorySnapShot_Store( &STX_State , sizeof (STX_State) );

		/* Also save the 'write sector' and 'write track' buffers */
		for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
		{
			/* Save the sectors' buffer */
			MemorySnapShot_Store ( &STX_SaveStruct[ Drive ].SaveSectorsCount , sizeof ( STX_SaveStruct[ Drive ].SaveSectorsCount ) );
			if ( STX_SaveStruct[ Drive ].SaveSectorsCount > 0 )
			{
				/* Save all sectors in the memory state */
				/* For each sector, we save the structure, then the data */
				for ( i=0 ; i<STX_SaveStruct[ Drive ].SaveSectorsCount ; i++ )
				{
//Str_Dump_Hex_Ascii ( (char *) &STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ], sizeof( STX_SAVE_SECTOR_STRUCT ), 16, "" , stderr );
					/* Save the structure */
					MemorySnapShot_Store ( &STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ] , sizeof( STX_SAVE_SECTOR_STRUCT ) );
					/* Save the sector's data */
					MemorySnapShot_Store ( STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].pData ,
							STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].SectorSize );
				}
			}
//fprintf ( stderr , "stx save write buffer drive=%d count=%d buf=%p\n" , Drive , STX_SaveStruct[ Drive ].SaveSectorsCount , STX_SaveStruct[ Drive ].pSaveSectorsStruct );

			/* Save the tracks' buffer */
			MemorySnapShot_Store ( &STX_SaveStruct[ Drive ].SaveTracksCount , sizeof ( STX_SaveStruct[ Drive ].SaveTracksCount ) );
			if ( STX_SaveStruct[ Drive ].SaveTracksCount > 0 )
			{
				/* Save all tracks in the memory state */
				/* For each track, we save the structure, then the data */
				for ( i=0 ; i<STX_SaveStruct[ Drive ].SaveTracksCount ; i++ )
				{
//Str_Dump_Hex_Ascii ( (char *) &STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ], sizeof( STX_SAVE_TRACK_STRUCT ), 16, "" , stderr );
					/* Save the structure */
					MemorySnapShot_Store ( &STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ] , sizeof( STX_SAVE_TRACK_STRUCT ) );
					/* Save the track's data (as it was written, don't save the interpreted track) */
					MemorySnapShot_Store ( STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].pDataWrite ,
							STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].TrackSizeWrite );
				}
			}
		}
	}

	else						/* Restoring snapshot */
	{
		MemorySnapShot_Store ( &STX_State , sizeof (STX_State) );

		/* Call STX_Insert_internal to recompute STX_State */
		/* (without loading an optional ".wd1772" file) */
		for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
			if ( EmulationDrives[Drive].ImageType == FLOPPY_IMAGE_TYPE_STX )
				if ( STX_Insert_internal ( Drive , EmulationDrives[Drive].sFileName , EmulationDrives[Drive].pBuffer ,
					EmulationDrives[Drive].nImageBytes ) == false )
				{
					Log_AlertDlg(LOG_ERROR, "Error restoring STX image %s in drive %d" ,
						EmulationDrives[Drive].sFileName , Drive );
					return;
				}

		/* Also restore the 'write sector' and 'write track' buffers */
		for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
		{
			/* Restore the sectors' buffer */
			MemorySnapShot_Store ( &STX_SaveStruct[ Drive ].SaveSectorsCount , sizeof ( STX_SaveStruct[ Drive ].SaveSectorsCount ) );
			if ( STX_SaveStruct[ Drive ].SaveSectorsCount > 0 )
			{
				/* Alloc a buffer for all the sectors */
				STX_SaveStruct[ Drive ].pSaveSectorsStruct = malloc ( STX_SaveStruct[ Drive ].SaveSectorsCount * sizeof ( STX_SAVE_SECTOR_STRUCT ) );
				if ( !STX_SaveStruct[ Drive ].pSaveSectorsStruct )
				{
					Log_AlertDlg(LOG_ERROR, "Error restoring STX sectors save buffer malloc size=%d in drive %d" ,
						STX_SaveStruct[ Drive ].SaveSectorsCount , Drive );
					return;
				}

				/* Load all the sectors from the memory state */
				/* For each sector, we load the structure, then the data */
				for ( i=0 ; i<STX_SaveStruct[ Drive ].SaveSectorsCount ; i++ )
				{
					/* Load the structure */
					MemorySnapShot_Store ( &STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ] , sizeof( STX_SAVE_SECTOR_STRUCT ) );
//Str_Dump_Hex_Ascii ( (char *) &STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ], sizeof( STX_SAVE_SECTOR_STRUCT ), 16, "" , stderr );

					/* Load the sector's data */
					STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].pData = malloc ( STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].SectorSize );
					if ( !STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].pData )
					{
						Log_AlertDlg(LOG_ERROR, "Error restoring STX save buffer for sector=%d in drive %d" ,
							i , Drive );
						return;
					}
					MemorySnapShot_Store ( STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].pData ,
							STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].SectorSize );

					/* Find the original sector to associate it with this saved sector */
					pStxSector = STX_FindSector_By_Position ( Drive , STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].Track ,
							STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].Side ,
							STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i ].BitPosition );
					if ( !pStxSector )
					{
						Log_AlertDlg(LOG_ERROR, "Error restoring STX save buffer for sector=%d in drive %d" ,
							i , Drive );
						return;
					}
					pStxSector->SaveSectorIndex = i;
				}
			}

			else
				STX_SaveStruct[ Drive ].pSaveSectorsStruct = NULL;
//fprintf ( stderr , "stx load write buffer drive=%d count=%d buf=%p\n" , Drive , STX_SaveStruct[ Drive ].SaveSectorsCount , STX_SaveStruct[ Drive ].pSaveSectorsStruct );

			/* Restore the tracks' buffer */
			MemorySnapShot_Store ( &STX_SaveStruct[ Drive ].SaveTracksCount , sizeof ( STX_SaveStruct[ Drive ].SaveTracksCount ) );
			if ( STX_SaveStruct[ Drive ].SaveTracksCount > 0 )
			{
				/* Alloc a buffer for all the tracks */
				STX_SaveStruct[ Drive ].pSaveTracksStruct = malloc ( STX_SaveStruct[ Drive ].SaveTracksCount * sizeof ( STX_SAVE_TRACK_STRUCT ) );
				if ( !STX_SaveStruct[ Drive ].pSaveTracksStruct )
				{
					Log_AlertDlg(LOG_ERROR, "Error restoring STX tracks save buffer malloc size=%d in drive %d" ,
						STX_SaveStruct[ Drive ].SaveTracksCount , Drive );
					return;
				}

				/* Load all the tracks from the memory state */
				/* For each track, we load the structure, then the data */
				for ( i=0 ; i<STX_SaveStruct[ Drive ].SaveTracksCount ; i++ )
				{
					/* Load the structure */
					MemorySnapShot_Store ( &STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ] , sizeof( STX_SAVE_TRACK_STRUCT ) );
//Str_Dump_Hex_Ascii ( (char *) &STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ], sizeof( STX_SAVE_TRACK_STRUCT ), 16, "" , stderr );

					/* Load the track's data (as it was written, don't load the interpreted track) */
					STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].pDataWrite = malloc ( STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].TrackSizeWrite );
					if ( !STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].pDataWrite )
					{
						Log_AlertDlg(LOG_ERROR, "Error restoring STX save buffer for track=%d in drive %d" ,
							i , Drive );
						return;
					}
					MemorySnapShot_Store ( STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].pDataWrite ,
							STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].TrackSizeWrite );

					STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].pDataRead = NULL;	/* TODO : compute interpreted track */
					STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].TrackSizeRead = 0;	/* TODO : compute interpreted track */

					/* Find the original track to associate it with this saved track */
					pStxTrack = STX_FindTrack ( Drive , STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].Track ,
							STX_SaveStruct[ Drive ].pSaveTracksStruct[ i ].Side );
					if ( !pStxTrack )
					{
						Log_AlertDlg(LOG_ERROR, "Error restoring STX save buffer for track=%d in drive %d" ,
							i , Drive );
						return;
					}
					pStxTrack->SaveTrackIndex = i;
				}
			}

			else
				STX_SaveStruct[ Drive ].pSaveTracksStruct = NULL;
		}

		Log_Printf ( LOG_DEBUG , "stx load ok\n" );
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
 * Create a filename to save modifications made to an STX file
 * We replace the ".stx" or ".stx.gz" extension with ".wd1772"
 * Return true if OK
 */
bool	STX_FileNameToSave ( const char *FilenameSTX , char *FilenameSave )
{
	if ( File_ChangeFileExtension ( FilenameSTX , ".stx.gz" , FilenameSave , WD1772_SAVE_FILE_EXT ) )
		return true;
	
	else if ( File_ChangeFileExtension ( FilenameSTX , ".stx" , FilenameSave , WD1772_SAVE_FILE_EXT ) )
		return true;

	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Load .STX file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
uint8_t *STX_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType)
{
	uint8_t		*pSTXFile;

	*pImageSize = 0;

	/* Just load directly a buffer, and set ImageSize accordingly */
	pSTXFile = File_Read(pszFileName, pImageSize, NULL);
	if (!pSTXFile)
	{
		*pImageSize = 0;
		return NULL;
	}

	/* Check the file's header is "RSY\0" */
	if ( ( *pImageSize <= STX_HEADER_ID_LEN )
	  || ( memcmp ( STX_HEADER_ID , pSTXFile , STX_HEADER_ID_LEN ) ) )
	{
		Log_Printf ( LOG_ERROR , "%s is not a valid STX image\n" , pszFileName );
		free ( pSTXFile );
		*pImageSize = 0;
		return NULL;
	}
	
	*pImageType = FLOPPY_IMAGE_TYPE_STX;
	return pSTXFile;
}


/*-----------------------------------------------------------------------*/
/**
 * Save .STX file from memory buffer. Returns true if all OK.
 * We create a file based on the initial filename by replacing the ".stx" extension
 * with ".wd1172".
 * We save all sectors, then all tracks.
 * If there're no sector and no track to save, return true and don't create
 * the save file
 */
bool STX_WriteDisk ( int Drive , const char *pszFileName , uint8_t *pBuffer , int ImageSize )
{
	FILE		*FileOut;
	char		FilenameSave[ FILENAME_MAX ];
	uint8_t		buf[ 100 ];
	uint8_t		*p;
	uint32_t		Sector;
	uint32_t		Track;
	uint32_t		BlockLen;
        uint32_t                  SaveSectorsCount_real;
	STX_SAVE_SECTOR_STRUCT	*pStxSaveSector;
	STX_SAVE_TRACK_STRUCT	*pStxSaveTrack;
	uint32_t		i;

	Log_Printf ( LOG_DEBUG , "stx write <%s>\n" , pszFileName );


	/* We can only save if the filename ends with ".stx" (or ".stx.gz"), not if it's a ".zip" file */
	if ( STX_FileNameIsSTX ( pszFileName , true ) == false )
	{
		Log_AlertDlg ( LOG_INFO , "WARNING : can't save changes made to this STX disk, bad file extension" );
		return false;
	}

	/* Count the saved sectors that are really used */
	SaveSectorsCount_real = 0;
	i = 0;
	while ( i < STX_SaveStruct[ Drive ].SaveSectorsCount )
		if ( STX_SaveStruct[ Drive ].pSaveSectorsStruct[ i++ ].StructIsUsed != 0 )
			SaveSectorsCount_real++;

	/* Do we have data to save ? */
	if ( ( SaveSectorsCount_real == 0 )
	  && ( STX_SaveStruct[ Drive ].SaveTracksCount == 0 ) )
		return true;


	if ( STX_FileNameToSave ( pszFileName , FilenameSave ) == false )
	{
		Log_Printf ( LOG_ERROR , "STX_WriteDisk drive=%d file=%s, error STX_FileNameToSave\n" , Drive , pszFileName );
		return false;
	}
	Log_Printf ( LOG_DEBUG , "stx write <%s>\n" , FilenameSave );

	
	FileOut = fopen ( FilenameSave , "wb+" );
	if ( !FileOut )
	{
		Log_Printf ( LOG_ERROR , "STX_WriteDisk drive=%d file=%s, error fopen\n" , Drive , pszFileName );
		return false;
	}

	/* Write the file's header : 6 + 1 + 1 + 4 + 4 = 16 bytes */
	p = buf;
	strcpy ( (char *) p , WD1772_SAVE_FILE_ID );				/* +0 .. +5 */
	p += strlen ( WD1772_SAVE_FILE_ID );
	*p++ = WD1772_SAVE_VERSION;						/* +6 */
	*p++ = WD1772_SAVE_REVISION;						/* +7 */

	STX_WriteU32_BE ( p , SaveSectorsCount_real );				/* +8 ... +11 */
	p += 4;
	
	STX_WriteU32_BE ( p , STX_SaveStruct[ Drive ].SaveTracksCount );	/* +12 ... +15 */
	p += 4;
	
	if ( fwrite ( buf , p-buf , 1 , FileOut ) != 1 )
	{
		Log_Printf ( LOG_ERROR , "STX_WriteDisk drive=%d file=%s, error fwrite header\n" , Drive , pszFileName );
		fclose(FileOut);
		return false;
	}


	/* Write the sectors' buffer */
	Sector = 0;
	while ( Sector < STX_SaveStruct[ Drive ].SaveSectorsCount )
	{
		pStxSaveSector = &STX_SaveStruct[ Drive ].pSaveSectorsStruct[ Sector ];

		if ( pStxSaveSector->StructIsUsed == 0 )
		{
			Sector++;
			continue;						/* This structure is not used anymore, ignore it */
		}

		/* Build the sector's header : 20 bytes */
		p = buf;
		strcpy ( (char *) p , WD1772_SAVE_SECTOR_ID );			/* +0 .. +3 */
		p += strlen ( WD1772_SAVE_SECTOR_ID );

		BlockLen = 20-4 + pStxSaveSector->SectorSize;
		STX_WriteU32_BE ( p , BlockLen );				/* +4 ... +7 */
		p += 4;

		*p++ = pStxSaveSector->Track;					/* +8 */
		*p++ = pStxSaveSector->Side;					/* +9 */
		STX_WriteU16_BE ( p , pStxSaveSector->BitPosition );		/* +10 ... +11 */
		p += 2;
		*p++ = pStxSaveSector->ID_Track;				/* +12 */
		*p++ = pStxSaveSector->ID_Head;					/* +13 */
		*p++ = pStxSaveSector->ID_Sector;				/* +14 */
		*p++ = pStxSaveSector->ID_Size;					/* +15 */
		STX_WriteU16_BE ( p , pStxSaveSector->ID_CRC );			/* +16 ... +17 */
		p += 2;
		
		STX_WriteU16_BE ( p , pStxSaveSector->SectorSize );		/* +18 ... +19 */
		p += 2;

		/* Write the header */
//Str_Dump_Hex_Ascii ( (char *) buf , p-buf, 16, "" , stderr );
		if ( fwrite ( buf , p-buf , 1 , FileOut ) != 1 )
		{
			Log_Printf ( LOG_ERROR , "STX_WriteDisk drive=%d file=%s, error fwrite sector header\n" , Drive , pszFileName );
			fclose(FileOut);
			return false;
		}

		/* Write the data */
//Str_Dump_Hex_Ascii ( (char *) pStxSaveSector->pData , pStxSaveSector->SectorSize, 16, "" , stderr );
		if ( fwrite ( pStxSaveSector->pData , pStxSaveSector->SectorSize , 1 , FileOut ) != 1 )
		{
			Log_Printf ( LOG_ERROR , "STX_WriteDisk drive=%d file=%s, error fwrite sector data\n" , Drive , pszFileName );
			fclose(FileOut);
			return false;
		}

		Sector++;
	}


	/* Write the tracks' buffer */
	Track = 0;
	while ( Track < STX_SaveStruct[ Drive ].SaveTracksCount )
	{
		pStxSaveTrack = &STX_SaveStruct[ Drive ].pSaveTracksStruct[ Track ];

		/* Build the track's header : 12 bytes */
		p = buf;
		strcpy ( (char *) p , WD1772_SAVE_TRACK_ID );			/* +0 ... +3 */
		p += strlen ( WD1772_SAVE_TRACK_ID );

		BlockLen = 12-4 + pStxSaveTrack->TrackSizeWrite;
		STX_WriteU32_BE ( p , BlockLen );				/* +4 ... +7 */
		p += 4;

		*p++ = pStxSaveTrack->Track;					/* +8 */			
		*p++ = pStxSaveTrack->Side;					/* +9 */

		STX_WriteU16_BE ( p , pStxSaveTrack->TrackSizeWrite );		/* +10 ... +11 */
		p += 2;

		/* Write the header */
//Str_Dump_Hex_Ascii ( (char *) buf , p-buf, 16, "" , stderr );
		if ( fwrite ( buf , p-buf , 1 , FileOut ) != 1 )
		{
			Log_Printf ( LOG_ERROR , "STX_WriteDisk drive=%d file=%s, error fwrite track header\n" , Drive , pszFileName );
			fclose(FileOut);
			return false;
		}

		/* Write the data at +12 */
//Str_Dump_Hex_Ascii ( (char *) pStxSaveTrack->pDataWrite , pStxSaveTrack->TrackSizeWrite, 16, "" , stderr );
		if ( fwrite ( pStxSaveTrack->pDataWrite , pStxSaveTrack->TrackSizeWrite , 1 , FileOut ) != 1 )
		{
			Log_Printf ( LOG_ERROR , "STX_WriteDisk drive=%d file=%s, error fwrite track data\n" , Drive , pszFileName );
			fclose(FileOut);
			return false;
		}

		Track++;
	}


	fclose ( FileOut );

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Load a ".wd1772" save file and add it to the STX structures
 * Return true if OK.
 */
static bool	STX_LoadSaveFile ( int Drive , const char *FilenameSave )
{
	uint8_t		*SaveFileBuffer;
	long		SaveFileSize;
	uint8_t		*p;
	uint8_t		*p_save;
	uint8_t		version , revision;
	uint32_t		SectorNb;
	uint32_t		TrackNb;
	STX_SECTOR_STRUCT	*pStxSector;
	STX_TRACK_STRUCT	*pStxTrack;


	SaveFileBuffer = File_Read ( FilenameSave, &SaveFileSize, NULL );
	if (!SaveFileBuffer)
	{
		Log_Printf ( LOG_ERROR , "STX_LoadSaveFile drive=%d file=%s error\n" , Drive , FilenameSave );
		return false;
	}

	p = SaveFileBuffer;

	if ( strncmp ( (char *) p , WD1772_SAVE_FILE_ID , strlen ( WD1772_SAVE_FILE_ID ) ) )	/* +0 ... +5 */
	{
		Log_Printf ( LOG_ERROR , "STX_LoadSaveFile drive=%d file=%s bad header\n" , Drive , FilenameSave );
		free ( SaveFileBuffer );
		return false;
	}
	p += strlen ( WD1772_SAVE_FILE_ID );

	version = *p++;								/* +6 */
	revision = *p++;							/* +7 */
	if ( ( version != WD1772_SAVE_VERSION ) || ( revision != WD1772_SAVE_REVISION ) )
	{
		Log_Printf ( LOG_ERROR , "STX_LoadSaveFile drive=%d file=%s bad version 0x%x revision 0x%x\n" , Drive , FilenameSave , version , revision );
		free ( SaveFileBuffer );
		return false;
	}

	STX_SaveStruct[ Drive ].SaveSectorsCount = STX_ReadU32_BE ( p );	/* +8 ... +11 */
	p += 4;

	STX_SaveStruct[ Drive ].SaveTracksCount = STX_ReadU32_BE ( p );		/* +12 ... +15 */
	p += 4;


	/* Alloc a buffer for all the sectors */
	if ( STX_SaveStruct[ Drive ].SaveSectorsCount > 0 )
	{
		STX_SaveStruct[ Drive ].pSaveSectorsStruct = malloc ( STX_SaveStruct[ Drive ].SaveSectorsCount * sizeof ( STX_SAVE_SECTOR_STRUCT ) );
		if ( !STX_SaveStruct[ Drive ].pSaveSectorsStruct )
		{
			Log_AlertDlg(LOG_ERROR, "Error loading STX sectors save file malloc size=%d in drive %d" ,
				STX_SaveStruct[ Drive ].SaveSectorsCount , Drive );
			STX_FreeSaveStruct ( Drive );
			free ( SaveFileBuffer );
			return false;
		}
	}

	/* Alloc a buffer for all the tracks */
	if ( STX_SaveStruct[ Drive ].SaveTracksCount > 0 )
	{
		STX_SaveStruct[ Drive ].pSaveTracksStruct = malloc ( STX_SaveStruct[ Drive ].SaveTracksCount * sizeof ( STX_SAVE_TRACK_STRUCT ) );
		if ( !STX_SaveStruct[ Drive ].pSaveTracksStruct )
		{
			Log_AlertDlg(LOG_ERROR, "Error loading STX tracks save file malloc size=%d in drive %d" ,
				STX_SaveStruct[ Drive ].SaveTracksCount , Drive );
			STX_FreeSaveStruct ( Drive );
			free ( SaveFileBuffer );
			return false;
		}
	}


	SectorNb = 0;
	TrackNb = 0;
	while ( p < SaveFileBuffer + SaveFileSize )
	{
		/* Start of a block */
		p_save = p;
//Str_Dump_Hex_Ascii ( (char *) p , 32, 16, "" , stderr );

		/* Check the name of this block */
		if ( strncmp ( (char *) p , WD1772_SAVE_SECTOR_ID , 4 ) == 0 )
		{
//fprintf ( stderr , "STX_LoadSaveFile drive=%d SECT block %d\n" , Drive , SectorNb );
			if ( STX_LoadSaveFile_SECT ( Drive , &STX_SaveStruct[ Drive ].pSaveSectorsStruct[ SectorNb ] , p+4+4 ) == false )
			{
				Log_AlertDlg(LOG_ERROR, "Error loading STX save file SECT block %d in drive %d" ,
					SectorNb , Drive );
				STX_FreeSaveStruct ( Drive );
				free ( SaveFileBuffer );
				return false;
			}

//Str_Dump_Hex_Ascii ( (char *) &STX_SaveStruct[ Drive ].pSaveSectorsStruct[ SectorNb ] , sizeof(STX_SAVE_SECTOR_STRUCT) , 16, "" , stderr );
//Str_Dump_Hex_Ascii ( (char *) STX_SaveStruct[ Drive ].pSaveSectorsStruct[ SectorNb ].pData , 32, 16, "" , stderr );

			/* Find the original sector to associate it with this saved sector */
			pStxSector = STX_FindSector_By_Position ( Drive , STX_SaveStruct[ Drive ].pSaveSectorsStruct[ SectorNb ].Track ,
					STX_SaveStruct[ Drive ].pSaveSectorsStruct[ SectorNb ].Side ,
					STX_SaveStruct[ Drive ].pSaveSectorsStruct[ SectorNb ].BitPosition );
			if ( !pStxSector )
			{
				Log_AlertDlg(LOG_ERROR, "Error restoring STX save buffer for sector=%d in drive %d" ,
					SectorNb , Drive );
				STX_FreeSaveStruct ( Drive );
				free ( SaveFileBuffer );
				return false;
			}
			pStxSector->SaveSectorIndex = SectorNb;

			SectorNb++;
		}
	
		else if ( strncmp ( (char *) p , WD1772_SAVE_TRACK_ID , 4 ) == 0 )
		{
//fprintf ( stderr , "STX_LoadSaveFile drive=%d TRCK block %d\n" , Drive , TrackNb );
			if ( STX_LoadSaveFile_TRCK ( Drive , &STX_SaveStruct[ Drive ].pSaveTracksStruct[ TrackNb ] , p+4+4 ) == false )
			{
				Log_AlertDlg(LOG_ERROR, "Error loading STX save file TRCK block %d in drive %d" ,
					TrackNb , Drive );
				STX_FreeSaveStruct ( Drive );
				free ( SaveFileBuffer );
				return false;
			}
	
			/* Find the original track to associate it with this saved track */
			pStxTrack = STX_FindTrack ( Drive , STX_SaveStruct[ Drive ].pSaveTracksStruct[ TrackNb ].Track ,
						STX_SaveStruct[ Drive ].pSaveTracksStruct[ TrackNb ].Side );
			if ( !pStxTrack )
			{
				Log_AlertDlg(LOG_ERROR, "Error loading STX save file TRCK block %d in drive %d" ,
					TrackNb , Drive );
				STX_FreeSaveStruct ( Drive );
				free ( SaveFileBuffer );
				return false;
			}
			pStxTrack->SaveTrackIndex = TrackNb;
	
			TrackNb++;
		}

		else
		{
			Log_Printf ( LOG_WARN , "STX_LoadSaveFile drive=%d file=%s, unknown block %4.4s, skipping\n" , Drive , FilenameSave , p );
		}

		/* Next block */
		p = p_save + 4;
		p += STX_ReadU32_BE ( p );
	}

	free ( SaveFileBuffer );
	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Parse the "SECT" block from a ".wd1772" save file
 * Return true if OK.
 */
static bool	STX_LoadSaveFile_SECT ( int Drive, STX_SAVE_SECTOR_STRUCT *pStxSaveSector , uint8_t *p )
{
	pStxSaveSector->Track = *p++;
	pStxSaveSector->Side = *p++;

	pStxSaveSector->BitPosition = STX_ReadU16_BE ( p );
	p += 2;

	pStxSaveSector->ID_Track = *p++;
	pStxSaveSector->ID_Head = *p++;
	pStxSaveSector->ID_Sector = *p++;
	pStxSaveSector->ID_Size = *p++;
	pStxSaveSector->ID_CRC = STX_ReadU16_BE ( p );
	p += 2;

	pStxSaveSector->SectorSize = STX_ReadU16_BE ( p );
	p += 2;

	/* Copy the sector's data */
	pStxSaveSector->pData = malloc ( pStxSaveSector->SectorSize );
	if ( !pStxSaveSector->pData )
	{
		Log_AlertDlg(LOG_ERROR, "Error loading STX save buffer for track=%d side=%d bitposition=%d in drive %d" ,
			pStxSaveSector->Track , pStxSaveSector->Side , pStxSaveSector->BitPosition , Drive );
		return false;
	}

	memcpy ( pStxSaveSector->pData , p , pStxSaveSector->SectorSize );

	pStxSaveSector->StructIsUsed = 1;

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Parse the "TRCK" block from a ".wd1772" save file
 * Return true if OK.
 */
static bool	STX_LoadSaveFile_TRCK ( int Drive , STX_SAVE_TRACK_STRUCT *pStxSaveTrack , uint8_t *p )
{
	pStxSaveTrack->Track = *p++;
	pStxSaveTrack->Side = *p++;

	pStxSaveTrack->TrackSizeWrite = STX_ReadU16_BE ( p );
	p += 2;

	/* Copy the track's data */
	pStxSaveTrack->pDataWrite = malloc ( pStxSaveTrack->TrackSizeWrite );
	if ( !pStxSaveTrack->pDataWrite )
	{
		Log_AlertDlg(LOG_ERROR, "Error loading STX save buffer for track=%d side=%d in drive %d" ,
			pStxSaveTrack->Track , pStxSaveTrack->Side , Drive );
		return false;
	}

	memcpy ( pStxSaveTrack->pDataWrite , p , pStxSaveTrack->TrackSizeWrite );

	pStxSaveTrack->pDataRead = NULL;	/* TODO : compute interpreted track */
	pStxSaveTrack->TrackSizeRead = 0;	/* TODO : compute interpreted track */

	return true;
}



/*-----------------------------------------------------------------------*/
/*
 * Init variables used to handle STX images
 */
bool	STX_Init ( void )
{
	int	i;

	for ( i=0 ; i<MAX_FLOPPYDRIVES ; i++ )
	{
		STX_State.ImageBuffer[ i ] = NULL;

		STX_SaveStruct[ i ].SaveSectorsCount = 0;
		STX_SaveStruct[ i ].pSaveSectorsStruct = NULL;
		STX_SaveStruct[ i ].SaveTracksCount = 0;
		STX_SaveStruct[ i ].pSaveTracksStruct = NULL;
	}

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Init the resources to handle the STX image inserted into a drive (0=A: 1=B:)
 * We also look for an optional save file with the ".wd1772" extension.
 * If this file exists, then we load it too.
 */
bool	STX_Insert ( int Drive , const char *FilenameSTX , uint8_t *pImageBuffer , long ImageSize )
{
	char		FilenameSave[ FILENAME_MAX ];

	/* Process the current STX image */
	if ( STX_Insert_internal ( Drive , FilenameSTX , pImageBuffer , ImageSize ) == false )
		return false;

	/* Try to load an optional ".wd1772" save file. In case of error, we continue anyway with the current STX image */
	if ( ( STX_FileNameToSave ( FilenameSTX , FilenameSave ) )
	  && ( File_Exists ( FilenameSave ) ) )
	{
		Log_Printf ( LOG_INFO , "STX : STX_Insert drive=%d file=%s buf=%p size=%ld load wd1172 %s\n" , Drive , FilenameSTX , pImageBuffer , ImageSize , FilenameSave );
		if ( STX_LoadSaveFile ( Drive , FilenameSave ) == false )
		{
			Log_AlertDlg ( LOG_ERROR , "Can't read the STX save file '%s'. Ignore it" , FilenameSave );
		}
	}

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Init the resources to handle the STX image inserted into a drive (0=A: 1=B:)
 * This function is used when restoring a memory snapshot and does not load
 * an optional ".wd1772" save file (the saved data are already in the memory
 * snapshot)
 */
static bool	STX_Insert_internal ( int Drive , const char *FilenameSTX , uint8_t *pImageBuffer , long ImageSize )
{
	Log_Printf ( LOG_DEBUG , "STX : STX_Insert_internal drive=%d file=%s buf=%p size=%ld\n" , Drive , FilenameSTX , pImageBuffer , ImageSize );

	STX_State.ImageBuffer[ Drive ] = STX_BuildStruct ( pImageBuffer , STX_DEBUG_FLAG );
	if ( STX_State.ImageBuffer[ Drive ] == NULL )
	{
		Log_Printf ( LOG_ERROR , "STX : STX_Insert_internal drive=%d file=%s buf=%p size=%ld, error in STX_BuildStruct\n" , Drive , FilenameSTX , pImageBuffer , ImageSize );
		return false;
	}

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * When ejecting a disk, free the resources associated with an STX image
 */
bool	STX_Eject ( int Drive )
{
	Log_Printf ( LOG_DEBUG , "STX : STX_Eject drive=%d\n" , Drive );

	if ( STX_State.ImageBuffer[ Drive ] )
	{
		STX_FreeStruct ( STX_State.ImageBuffer[ Drive ] );
		STX_State.ImageBuffer[ Drive ] = NULL;
	}

	STX_FreeSaveStruct ( Drive );

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Read words and longs stored in little endian order
 */
static uint16_t	STX_ReadU16_LE ( uint8_t *p )
{
	return (p[1]<<8) +p [0];
}

static uint32_t	STX_ReadU32_LE ( uint8_t *p )
{
	return (p[3]<<24) + (p[2]<<16) + (p[1]<<8) +p[0];
}


/*-----------------------------------------------------------------------*/
/*
 * Read words and longs stored in big endian order
 */
static uint16_t	STX_ReadU16_BE ( uint8_t *p )
{
	return (p[0]<<8) + p[1];
}

static uint32_t	STX_ReadU32_BE ( uint8_t *p )
{
	return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) +p[3];
}


/*-----------------------------------------------------------------------*/
/*
 * Store words and longs in big endian order
 */
static void	STX_WriteU16_BE ( uint8_t *p , uint16_t val )
{
	p[ 1 ] = val & 0xff;
	val >>= 8;
	p[ 0 ] = val & 0xff;
}

static void	STX_WriteU32_BE ( uint8_t *p , uint32_t val )
{
	p[ 3 ] = val & 0xff;
	val >>= 8;
	p[ 2 ] = val & 0xff;
	val >>= 8;
	p[ 1 ] = val & 0xff;
	val >>= 8;
	p[ 0 ] = val & 0xff;
}

/*-----------------------------------------------------------------------*/
/**
 * Free all the memory allocated to store an STX file
 */
static void	STX_FreeStruct ( STX_MAIN_STRUCT *pStxMain )
{
	int			Track;

	if ( !pStxMain )
		return;

	for ( Track = 0 ; Track < pStxMain->TracksCount ; Track++ )
	{
		free ( (pStxMain->pTracksStruct[ Track ]).pSectorsStruct );
	}

	free ( pStxMain->pTracksStruct );
	free ( pStxMain );
}


/*-----------------------------------------------------------------------*/
/**
 * Free all the memory allocated to store saved sectors / tracks
 */
static void	STX_FreeSaveStruct ( int Drive )
{
	if ( STX_SaveStruct[ Drive ].pSaveSectorsStruct )
	{
		STX_FreeSaveSectorsStructAll ( STX_SaveStruct[ Drive ].pSaveSectorsStruct , STX_SaveStruct[ Drive ].SaveSectorsCount );
		STX_SaveStruct[ Drive ].SaveSectorsCount = 0;
		STX_SaveStruct[ Drive ].pSaveSectorsStruct = NULL;
	}

	if ( STX_SaveStruct[ Drive ].pSaveTracksStruct )
	{
		STX_FreeSaveTracksStructAll ( STX_SaveStruct[ Drive ].pSaveTracksStruct , STX_SaveStruct[ Drive ].SaveTracksCount );
		STX_SaveStruct[ Drive ].SaveTracksCount = 0;
		STX_SaveStruct[ Drive ].pSaveTracksStruct = NULL;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Free the memory allocated to store all the STX_SAVE_SECTOR_STRUCT
 */
static void	STX_FreeSaveSectorsStructAll ( STX_SAVE_SECTOR_STRUCT *pSaveSectorsStruct , uint32_t SaveSectorsCount )
{
	uint32_t	i;

	if ( !pSaveSectorsStruct )
		return;

	for ( i = 0 ; i < SaveSectorsCount ; i++ )
	{
		STX_FreeSaveSectorsStruct ( pSaveSectorsStruct , i );
	}

	free ( pSaveSectorsStruct );
}


/*-----------------------------------------------------------------------*/
/**
 * Free the memory allocated to store one STX_SAVE_SECTOR_STRUCT
 */
static void	STX_FreeSaveSectorsStruct ( STX_SAVE_SECTOR_STRUCT *pSaveSectorsStruct , int Nb )
{
	if ( pSaveSectorsStruct[ Nb ].StructIsUsed == 0 )
		return;						/* This structure is already free */

	free(pSaveSectorsStruct[Nb].pData);

	pSaveSectorsStruct[ Nb ].StructIsUsed = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Free the memory allocated to store all the STX_SAVE_TRACK_STRUCT
 */
static void	STX_FreeSaveTracksStructAll ( STX_SAVE_TRACK_STRUCT *pSaveTracksStruct , uint32_t SaveTracksCount )
{
	uint32_t	i;

	if ( !pSaveTracksStruct )
		return;

	for ( i = 0 ; i < SaveTracksCount ; i++ )
	{
		STX_FreeSaveTracksStruct ( pSaveTracksStruct , i );
	}

	free ( pSaveTracksStruct );
}


/*-----------------------------------------------------------------------*/
/**
 * Free the memory allocated to store one STX_SAVE_TRACK_STRUCT
 */
static void	STX_FreeSaveTracksStruct ( STX_SAVE_TRACK_STRUCT *pSaveTracksStruct , int Nb )
{
	free(pSaveTracksStruct[Nb].pDataWrite);
	free(pSaveTracksStruct[Nb].pDataRead);
}


/*-----------------------------------------------------------------------*/
/**
 * Parse an STX file.
 * The file is in pFileBuffer and we dynamically allocate memory to store
 * the components (main header, tracks, sectors).
 * Some internal variables/pointers are also computed, to speed up
 * data access when the FDC emulates an STX file.
 */
STX_MAIN_STRUCT	*STX_BuildStruct ( uint8_t *pFileBuffer , int Debug )
{

	STX_MAIN_STRUCT		*pStxMain;
	STX_TRACK_STRUCT	*pStxTrack;
	STX_SECTOR_STRUCT	*pStxSector;
	uint8_t			*p;
	uint8_t			*p_cur;
	int			Track;
	int			Sector;
	uint8_t			*pFuzzyData;
	uint8_t			*pTimingData;
	uint32_t			MaxOffsetSectorEnd;
	int			VariableTimings;

	pStxMain = malloc ( sizeof ( STX_MAIN_STRUCT ) );
	if ( !pStxMain )
		return NULL;
	memset ( pStxMain , 0 , sizeof ( STX_MAIN_STRUCT ) );

	p = pFileBuffer;

	/* Read file's header */
	memcpy ( pStxMain->FileID , p , 4 ); p += 4;
	pStxMain->Version	=	STX_ReadU16_LE ( p ); p += 2;
	pStxMain->ImagingTool	=	STX_ReadU16_LE ( p ); p += 2;
	pStxMain->Reserved_1	=	STX_ReadU16_LE ( p ); p += 2;
	pStxMain->TracksCount	=	*p++;;
	pStxMain->Revision	=	*p++;
	pStxMain->Reserved_2	=	STX_ReadU32_LE ( p ); p += 4;

	if ( Debug & STX_DEBUG_FLAG_STRUCTURE )
		fprintf ( stderr , "STX header ID='%.4s' Version=%4.4x ImagingTool=%4.4x Reserved1=%4.4x"
			" TrackCount=%d Revision=%2.2x Reserved2=%x\n" , pStxMain->FileID , pStxMain->Version ,
			pStxMain->ImagingTool  , pStxMain->Reserved_1 , pStxMain->TracksCount , pStxMain->Revision ,
			pStxMain->Reserved_2 );

	pStxMain->WarnedWriteSector = false;
	pStxMain->WarnedWriteTrack = false;

	pStxTrack = malloc ( sizeof ( STX_TRACK_STRUCT ) * pStxMain->TracksCount );
	if ( !pStxTrack )
	{
		STX_FreeStruct ( pStxMain );
		return NULL;
	}
	memset ( pStxTrack , 0 , sizeof ( STX_TRACK_STRUCT ) * pStxMain->TracksCount );
	pStxMain->pTracksStruct = pStxTrack;

	/* Parse all the track blocks */
 	for ( Track = 0 ; Track < pStxMain->TracksCount ; Track++ )
	{
		p_cur = p;

		pStxTrack->BlockSize		=	STX_ReadU32_LE ( p ); p += 4;
		pStxTrack->FuzzySize		=	STX_ReadU32_LE ( p ); p += 4;
		pStxTrack->SectorsCount		=	STX_ReadU16_LE ( p ); p += 2;
		pStxTrack->Flags		=	STX_ReadU16_LE ( p ); p += 2;
		pStxTrack->MFMSize		=	STX_ReadU16_LE ( p ); p += 2;
		pStxTrack->TrackNumber		=	*p++;
		pStxTrack->RecordType		=	*p++;

		pStxTrack->SaveTrackIndex = -1;

		if ( pStxTrack->SectorsCount == 0 )			/* No sector (track image only, or empty / non formatted track) */
		{
			pStxTrack->pSectorsStruct = NULL;
		}
		else
		{
			/* Track contains some sectors */
			pStxSector = malloc ( sizeof ( STX_SECTOR_STRUCT ) * pStxTrack->SectorsCount );
			if ( !pStxSector )
			{
				STX_FreeStruct ( pStxMain );
				return NULL;
			}
			memset ( pStxSector , 0 , sizeof ( STX_SECTOR_STRUCT ) * pStxTrack->SectorsCount );
			pStxTrack->pSectorsStruct = pStxSector;

			/* Do we have some sector infos after the track header or only sector data ? */
			if ( ( pStxTrack->Flags & STX_TRACK_FLAG_SECTOR_BLOCK ) == 0 )
			{
				/* The track only contains SectorsCount sectors of 512 bytes */
				/* NOTE |NP] : in that case, pStxTrack->MFMSize seems to be in bits instead of bytes */
				STX_BuildSectorsSimple ( pStxTrack , p );
				goto next_track;
			}
		}

		/* Start of the optional fuzzy bits data */
		pStxTrack->pFuzzyData = p + pStxTrack->SectorsCount * STX_SECTOR_BLOCK_SIZE;

		/* Start of the optional track data */
		pStxTrack->pTrackData = pStxTrack->pFuzzyData + pStxTrack->FuzzySize;

		if ( ( pStxTrack->Flags & STX_TRACK_FLAG_TRACK_IMAGE ) == 0 )
		{
			pStxTrack->TrackImageSyncPosition = 0;
			pStxTrack->TrackImageSize = 0;
			pStxTrack->pTrackImageData = NULL;
			pStxTrack->pSectorsImageData = pStxTrack->pTrackData;
		}
		else if ( ( pStxTrack->Flags & STX_TRACK_FLAG_TRACK_IMAGE_SYNC ) == 0 )	/* Track with size+data */
		{
			pStxTrack->TrackImageSyncPosition = 0;
			pStxTrack->TrackImageSize = STX_ReadU16_LE ( pStxTrack->pTrackData );
			pStxTrack->pTrackImageData = pStxTrack->pTrackData + 2;
			pStxTrack->pSectorsImageData = pStxTrack->pTrackImageData + pStxTrack->TrackImageSize;
		}
		else									/* Track with sync offset + size + data */
		{
			pStxTrack->TrackImageSyncPosition = STX_ReadU16_LE ( pStxTrack->pTrackData );
			pStxTrack->TrackImageSize = STX_ReadU16_LE ( pStxTrack->pTrackData + 2 );
			pStxTrack->pTrackImageData = pStxTrack->pTrackData + 4;
			pStxTrack->pSectorsImageData = pStxTrack->pTrackImageData + pStxTrack->TrackImageSize;
		}

		if ( pStxTrack->SectorsCount == 0 )			/* No sector (track image only, or empty / non formatted track) */
			goto next_track;

		/* Parse all the sectors in this track */
		pFuzzyData = pStxTrack->pFuzzyData;
		VariableTimings = 0;
		MaxOffsetSectorEnd = 0;
		for ( Sector = 0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
		{
			pStxSector = &(pStxTrack->pSectorsStruct[ Sector ]);

			pStxSector->DataOffset = STX_ReadU32_LE ( p ); p += 4;
			pStxSector->BitPosition = STX_ReadU16_LE ( p ); p += 2;
			pStxSector->ReadTime = STX_ReadU16_LE ( p ); p += 2;
			pStxSector->ID_Track = *p++;
			pStxSector->ID_Head = *p++;
			pStxSector->ID_Sector = *p++;
			pStxSector->ID_Size = *p++;
			pStxSector->ID_CRC = ( p[0] << 8 ) | p[1] ; p +=2;
			pStxSector->FDC_Status = *p++;
			pStxSector->Reserved = *p++;

			/* Check if sector has data */
			if ( ( pStxSector->FDC_Status & STX_SECTOR_FLAG_RNF ) == 0 )
			{
				/* Check if SectorSize is valid (this is just a warning, we keep only bits 0-1 anyway) */
				if ( pStxSector->ID_Size & ~FDC_SECTOR_SIZE_MASK )
				{
//					fprintf ( stderr , "STX : invalid ID_Size=%d on track %d sector %d\n" ,
//						  pStxSector->ID_Size , Track , Sector );
				}

				pStxSector->SectorSize = 128 << ( pStxSector->ID_Size & FDC_SECTOR_SIZE_MASK );

				pStxSector->pData = pStxTrack->pTrackData + pStxSector->DataOffset;
				if ( pStxSector->FDC_Status & STX_SECTOR_FLAG_FUZZY )
				{
					pStxSector->pFuzzyData = pFuzzyData;
					pFuzzyData += pStxSector->SectorSize;
				}

				/* Max offset of the end of all sectors image in the track */
				if ( MaxOffsetSectorEnd < pStxSector->DataOffset + pStxSector->SectorSize )
					MaxOffsetSectorEnd = pStxSector->DataOffset + pStxSector->SectorSize;

 				if ( pStxSector->FDC_Status & STX_SECTOR_FLAG_VARIABLE_TIME )
					VariableTimings = 1;
			}

			pStxSector->SaveSectorIndex = -1;
		}

		/* Start of the optional timings data, after the optional sectors image data */
		pStxTrack->pTiming = pStxTrack->pTrackData + MaxOffsetSectorEnd;
		if ( pStxTrack->pTiming < pStxTrack->pSectorsImageData )	/* If all sectors image were inside the track image */
			pStxTrack->pTiming = pStxTrack->pSectorsImageData;	/* then timings data are just after the track image */

		if ( VariableTimings == 1 )				/* Track has at least one variable sector */
		{
			if ( pStxMain->Revision == 2 )			/* Specific timing table  */
			{
				pStxTrack->TimingFlags = STX_ReadU16_LE ( pStxTrack->pTiming );	/* always '5' ? */
				pStxTrack->TimingSize = STX_ReadU16_LE ( pStxTrack->pTiming + 2 );
				pStxTrack->pTimingData = pStxTrack->pTiming + 4;	/* 2 bytes of timing for each block of 16 bytes */
			}

			/* Compute the address of the timings data for each sector with variable timings */
			pTimingData = pStxTrack->pTimingData;
			for ( Sector = 0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
			{
				pStxSector = &(pStxTrack->pSectorsStruct[ Sector ]);
				pStxSector->pTimingData = NULL;				/* No timings by default */

				/* Check if sector has data + variable timings */
				if ( ( ( pStxSector->FDC_Status & STX_SECTOR_FLAG_RNF ) == 0 )
				    && ( pStxSector->FDC_Status & STX_SECTOR_FLAG_VARIABLE_TIME ) )
				{
					if ( pStxMain->Revision == 2 )				/* Specific table for revision 2 */
					{
						pStxSector->pTimingData = pTimingData;
						pTimingData += ( pStxSector->SectorSize / 16 ) * 2;
					}
					else
						pStxSector->pTimingData = TimingDataDefault;	/* Fixed table for revision 0 */
				}
			}
		}

next_track:
		if ( Debug & STX_DEBUG_FLAG_STRUCTURE )
		{
			fprintf ( stderr , "  track %3d BlockSize=%d FuzzySize=%d Sectors=%4.4x Flags=%4.4x"
				" MFMSize=%d TrackNb=%2.2x Side=%d RecordType=%x"
				" TrackImage=%s (%d bytes, sync=%4.4x) Timings=%d,%d\n" ,
				Track , pStxTrack->BlockSize ,
				pStxTrack->FuzzySize , pStxTrack->SectorsCount , pStxTrack->Flags , pStxTrack->MFMSize ,
				pStxTrack->TrackNumber & 0x7f , ( pStxTrack->TrackNumber >> 7 ) & 0x01 , pStxTrack->RecordType ,
				pStxTrack->pTrackImageData ? "yes" : "no" , pStxTrack->TrackImageSize , pStxTrack->TrackImageSyncPosition ,
				pStxTrack->TimingFlags , pStxTrack->TimingSize );

				if ( ( Debug & STX_DEBUG_FLAG_DATA ) && pStxTrack->pTrackImageData )
				{
					fprintf ( stderr , "    track image data :\n" );
					Str_Dump_Hex_Ascii ( (char *)pStxTrack->pTrackImageData , pStxTrack->TrackImageSize ,
							16 , "        " , stderr );
				}

			if ( pStxTrack->SectorsCount == 0 )
				fprintf ( stderr , "    no sector in this track, %s\n" ,
				       pStxTrack->pTrackImageData ? "only track image" : "track empty / not formatted" );
			else
				for ( Sector = 0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
				{
					/* If the sector use the internal timing table, we print TimingsOffset=-1 */
					pStxSector = &(pStxTrack->pSectorsStruct[ Sector ]);
					fprintf ( stderr , "    sector %2d DataOffset=%d BitPosition=%d ReadTime=%d"
						" [track=%2.2x head=%2.2x sector=%2.2x size=%2.2x crc=%4.4x]"
						" FdcStatus=%2.2x Reserved=%2.2x TimingsOffset=%d\n" ,
						Sector , pStxSector->DataOffset , pStxSector->BitPosition ,
						pStxSector->ReadTime ,  pStxSector->ID_Track ,  pStxSector->ID_Head ,
						pStxSector->ID_Sector , pStxSector->ID_Size , pStxSector->ID_CRC ,
						pStxSector->FDC_Status , pStxSector->Reserved ,
						pStxSector->pTimingData ?
							( pStxTrack->TimingSize > 0 ? (int)(pStxSector->pTimingData - pStxTrack->pTrackData) : -1 )
							: 0 );

					if ( ( Debug & STX_DEBUG_FLAG_DATA ) && pStxSector->pData )
					{
						fprintf ( stderr , "      sector data :\n" );
						Str_Dump_Hex_Ascii ( (char *)pStxSector->pData , pStxSector->SectorSize ,
								16 , "        " , stderr );
					}
					if ( ( Debug & STX_DEBUG_FLAG_DATA ) && pStxSector->pFuzzyData )
					{
						fprintf ( stderr , "      fuzzy data :\n" );
						Str_Dump_Hex_Ascii ( (char *)pStxSector->pFuzzyData , pStxSector->SectorSize ,
								16 , "        " , stderr );
					}
					if ( ( Debug & STX_DEBUG_FLAG_DATA ) && pStxSector->pTimingData )
					{
						fprintf ( stderr , "      timing data :\n" );
						Str_Dump_Hex_Ascii ( (char *)pStxSector->pTimingData , ( pStxSector->SectorSize / 16 ) * 2 ,
								16 , "        " , stderr );
					}
				}
		}

		p = p_cur + pStxTrack->BlockSize;			/* Next Track block */
		pStxTrack++;
	}


	return pStxMain;
}


/*-----------------------------------------------------------------------*/
/**
 * When a track only consists of the content of each 512 bytes sector and
 * no timings information, we must compute some default values for each
 * sector, as well as the position of the corresponding 512 bytes of data.
 * This is only used when storing unprotected tracks.
 */
static void	STX_BuildSectorsSimple ( STX_TRACK_STRUCT *pStxTrack , uint8_t *p )
{
	int	Sector;
	int	BytePosition;
	uint16_t	CRC;

	BytePosition = FDC_TRACK_LAYOUT_STANDARD_GAP1 + FDC_TRACK_LAYOUT_STANDARD_GAP2;		/* Points to the 3x$A1 before the 1st IDAM $FE */
	BytePosition += 4;						/* Pasti seems to point after the 3x$A1 and the IDAM $FE */
	
	for ( Sector = 0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
	{
		pStxTrack->pSectorsStruct[ Sector ].SaveSectorIndex = -1;
		pStxTrack->pSectorsStruct[ Sector ].DataOffset = 0;
		pStxTrack->pSectorsStruct[ Sector ].BitPosition = BytePosition * 8;
		pStxTrack->pSectorsStruct[ Sector ].ReadTime = 0;

		/* Build the ID Field */
		pStxTrack->pSectorsStruct[ Sector ].ID_Track = pStxTrack->TrackNumber & 0x7f;
		pStxTrack->pSectorsStruct[ Sector ].ID_Head = ( pStxTrack->TrackNumber >> 7 ) & 0x01;
		pStxTrack->pSectorsStruct[ Sector ].ID_Sector = Sector + 1;
		pStxTrack->pSectorsStruct[ Sector ].ID_Size = FDC_SECTOR_SIZE_512;
		CRC = STX_BuildSectorID_CRC ( &(pStxTrack->pSectorsStruct[ Sector ]) );
		pStxTrack->pSectorsStruct[ Sector ].ID_CRC = CRC;

		pStxTrack->pSectorsStruct[ Sector ].FDC_Status = 0;
		pStxTrack->pSectorsStruct[ Sector ].Reserved = 0;
		pStxTrack->pSectorsStruct[ Sector ].pData = p + Sector * 512;
		pStxTrack->pSectorsStruct[ Sector ].SectorSize = 128 << pStxTrack->pSectorsStruct[ Sector ].ID_Size;

		BytePosition += FDC_TRACK_LAYOUT_STANDARD_RAW_SECTOR_512;
	}
}



/*-----------------------------------------------------------------------*/
/**
 * Compute the CRC of the Address Field for a given sector.
 */
static uint16_t	STX_BuildSectorID_CRC ( STX_SECTOR_STRUCT *pStxSector )
{
        uint16_t  CRC;

	crc16_reset ( &CRC );
	crc16_add_byte ( &CRC , 0xa1 );
	crc16_add_byte ( &CRC , 0xa1 );
	crc16_add_byte ( &CRC , 0xa1 );
	crc16_add_byte ( &CRC , 0xfe );
	crc16_add_byte ( &CRC , pStxSector->ID_Track );
	crc16_add_byte ( &CRC , pStxSector->ID_Head );
	crc16_add_byte ( &CRC , pStxSector->ID_Sector );
	crc16_add_byte ( &CRC , pStxSector->ID_Size );

	return CRC;
}



/*-----------------------------------------------------------------------*/
/**
 * Find a track in the floppy image inserted into a drive.
 */
static STX_TRACK_STRUCT	*STX_FindTrack ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	int	i;

	if ( STX_State.ImageBuffer[ Drive ] == NULL )
		return NULL;

	for ( i=0 ; i<STX_State.ImageBuffer[ Drive ]->TracksCount ; i++ )
		if ( STX_State.ImageBuffer[ Drive ]->pTracksStruct[ i ].TrackNumber == ( ( Track & 0x7f ) | ( Side << 7 ) ) )
			return &(STX_State.ImageBuffer[ Drive ]->pTracksStruct[ i ]);

	return NULL;
}



/*-----------------------------------------------------------------------*/
/**
 * Find a sector in the floppy image inserted into a drive.
 * SectorStruct_Nb is a value set by a previous call to FDC_NextSectorID_FdcCycles_STX()
 */
static STX_SECTOR_STRUCT	*STX_FindSector ( uint8_t Drive , uint8_t Track , uint8_t Side , uint8_t SectorStruct_Nb )
{
	STX_TRACK_STRUCT	*pStxTrack;

	if ( STX_State.ImageBuffer[ Drive ] == NULL )
		return NULL;

	pStxTrack = STX_FindTrack ( Drive , Track , Side );
	if ( pStxTrack == NULL )
		return NULL;

	if ( pStxTrack->pSectorsStruct == NULL )
		return NULL;

	return &(pStxTrack->pSectorsStruct[ SectorStruct_Nb ]);
}



/*-----------------------------------------------------------------------*/
/**
 * Find a sector in the floppy image inserted into a drive.
 * The sector is identified by its BitPosition which is unique per track/side
 */
static STX_SECTOR_STRUCT	*STX_FindSector_By_Position ( uint8_t Drive , uint8_t Track , uint8_t Side , uint16_t BitPosition )
{
	STX_TRACK_STRUCT	*pStxTrack;
	int			Sector;

	if ( STX_State.ImageBuffer[ Drive ] == NULL )
		return NULL;

	pStxTrack = STX_FindTrack ( Drive , Track , Side );
	if ( pStxTrack == NULL )
		return NULL;

	if ( pStxTrack->pSectorsStruct == NULL )
		return NULL;

	for ( Sector=0 ; Sector<pStxTrack->SectorsCount ; Sector++ )
		if ( pStxTrack->pSectorsStruct[ Sector ].BitPosition == BitPosition )
			return &(pStxTrack->pSectorsStruct[ Sector ]);
	
	return NULL;
}



/*-----------------------------------------------------------------------*/
/**
 * Return the number of bytes in a raw track
 * For a DD floppy, tracks will usually have a size of more or less
 * FDC_TRACK_BYTES_STANDARD bytes (depending on the mastering process used
 * for different protections)
 * NOTE : Although STX format was supposed to handle only DD floppies, some tools like HxC
 * allow to convert a HD floppy image to an STX equivalent. In that case
 * TrackSize will be approximately 2 x FDC_TRACK_BYTES_STANDARD
 */
int	FDC_GetBytesPerTrack_STX ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	STX_TRACK_STRUCT	*pStxTrack;
	int			TrackSize;

	pStxTrack = STX_FindTrack ( Drive , Track , Side );
	if ( pStxTrack == NULL )
		TrackSize =  FDC_TRACK_BYTES_STANDARD;			/* Use a standard track length is track is not available */

	else if ( pStxTrack->pTrackImageData )
		TrackSize = pStxTrack->TrackImageSize;
	else if ( ( pStxTrack->Flags & STX_TRACK_FLAG_SECTOR_BLOCK ) == 0 )
		TrackSize = pStxTrack->MFMSize / 8;		/* When the track contains only sector data, MFMSize is in bits */
	else
		TrackSize = pStxTrack->MFMSize;

//fprintf ( stderr , "fdc stx drive=%d track=0x%x side=%d size=%d\n" , Drive , Track, Side , TrackSize );
	return TrackSize;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the number of FDC cycles to go from one index pulse to the next
 * one on a given drive/track/side.
 * We take the TrackSize into account to return this delay.
 * NOTE : in the case of HD/ED floppies (instead of DD), we must take the density
 * factor into account (it should take the same time to read a DD track and a HD track
 * as the drive spins at 300 RPM in both cases)
 */
uint32_t	FDC_GetCyclesPerRev_FdcCycles_STX ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	int			TrackSize;

	TrackSize = FDC_GetBytesPerTrack_STX ( Drive , Track , Side );

	return TrackSize * FDC_DELAY_CYCLE_MFM_BYTE / FDC_GetFloppyDensity ( Drive );	/* Take density into account for HD/ED floppies */;
}



/*-----------------------------------------------------------------------*/
/**
 * Return the number of FDC cycles to wait before reaching the next
 * sector's ID Field in the track ($A1 $A1 $A1 $FE TR SIDE SR LEN CRC1 CRC2)
 * If no ID Field is found before the end of the track, we use the 1st
 * ID Field of the track (which simulates a full spin of the floppy).
 * We also store the next sector's number into NextSectorStruct_Nbr,
 * the next sector's number into NextSector_ID_Field_SR, the next track's number
 * into NextSector_ID_Field_TR, the next sector's length into
 * NextSector_ID_Field_LEN and if the CRC is correct or not into NextSector_ID_Field_CRC_OK.
 * This function assumes the sectors of each track are sorted in ascending order
 * using BitPosition.
 * If there's no available drive/floppy or no ID field in the track, we return -1
 */
int	FDC_NextSectorID_FdcCycles_STX ( uint8_t Drive , uint8_t NumberOfHeads , uint8_t Track , uint8_t Side )
{
	STX_TRACK_STRUCT	*pStxTrack;
	int			CurrentPos_FdcCycles;
	int			i;
	int			Delay_FdcCycles;
	int			TrackSize;

	CurrentPos_FdcCycles = FDC_IndexPulse_GetCurrentPos_FdcCycles ( NULL );
	if ( CurrentPos_FdcCycles < 0 )					/* No drive/floppy available at the moment */
		return -1;

	if ( ( Side == 1 ) && ( NumberOfHeads == 1 ) )			/* Can't read side 1 on a single sided drive */
		return -1;

	pStxTrack = STX_FindTrack ( Drive , Track , Side );
	if ( pStxTrack == NULL )					/* Track/Side don't exist in this STX image */
		return -1;

	if ( pStxTrack->SectorsCount == 0 )				/* No sector (track image only, or empty / non formatted track) */
		return -1;

	if ( FDC_MachineHandleDensity ( Drive ) == false )		/* Can't handle the floppy's density */
		return -1;

	/* Compare CurrentPos_FdcCycles with each sector's position in ascending order */
	/* (minus 4 bytes, see below) */
	for ( i=0 ; i<pStxTrack->SectorsCount ; i++ )
	{
		if ( CurrentPos_FdcCycles < (int)pStxTrack->pSectorsStruct[ i ].BitPosition*FDC_DELAY_CYCLE_MFM_BIT /* 1 bit = 32 cycles at 8 MHz */
					 - 4 * FDC_DELAY_CYCLE_MFM_BYTE )
			break;						/* We found the next sector */
	}

	if ( i == pStxTrack->SectorsCount )				/* CurrentPos_FdcCycles is after the last ID Field of this track */
	{
		/* Reach end of track (new index pulse), then go to 1st sector from current position */
		if ( pStxTrack->pTrackImageData )
			TrackSize = pStxTrack->TrackImageSize;
		else if ( ( pStxTrack->Flags & STX_TRACK_FLAG_SECTOR_BLOCK ) == 0 )
			TrackSize = pStxTrack->MFMSize / 8;		/* When the track contains only sector data, MFMSize is in bits */
		else
			TrackSize = pStxTrack->MFMSize;

		Delay_FdcCycles = TrackSize * FDC_DELAY_CYCLE_MFM_BYTE - CurrentPos_FdcCycles
				+ pStxTrack->pSectorsStruct[ 0 ].BitPosition*FDC_DELAY_CYCLE_MFM_BIT;
		STX_State.NextSectorStruct_Nbr = 0;
//fprintf ( stderr , "size=%d pos=%d pos0=%d delay=%d\n" , TrackSize, CurrentPos_FdcCycles, pStxTrack->pSectorsStruct[ 0 ].BitPosition , Delay_FdcCycles );
	}
	else								/* There's an ID Field before end of track */
	{
		Delay_FdcCycles = (int)pStxTrack->pSectorsStruct[ i ].BitPosition*FDC_DELAY_CYCLE_MFM_BIT - CurrentPos_FdcCycles;
		STX_State.NextSectorStruct_Nbr = i;
//fprintf ( stderr , "i=%d pos=%d posi=%d delay=%d\n" , i, CurrentPos_FdcCycles, pStxTrack->pSectorsStruct[ i ].BitPosition*FDC_DELAY_CYCLE_MFM_BIT , Delay_FdcCycles );
	}

	/* Store the value of the track/sector numbers in the next ID field */
	STX_State.NextSector_ID_Field_TR = pStxTrack->pSectorsStruct[ STX_State.NextSectorStruct_Nbr ].ID_Track;
	STX_State.NextSector_ID_Field_SR = pStxTrack->pSectorsStruct[ STX_State.NextSectorStruct_Nbr ].ID_Sector;
	STX_State.NextSector_ID_Field_LEN = pStxTrack->pSectorsStruct[ STX_State.NextSectorStruct_Nbr ].ID_Size;

	/* If RNF is set and CRC error is set, then this ID field has a CRC error */
	if ( ( pStxTrack->pSectorsStruct[ STX_State.NextSectorStruct_Nbr ].FDC_Status & STX_SECTOR_FLAG_RNF )
	  && ( pStxTrack->pSectorsStruct[ STX_State.NextSectorStruct_Nbr ].FDC_Status & STX_SECTOR_FLAG_CRC ) )
		STX_State.NextSector_ID_Field_CRC_OK = 0;		/* CRC bad */
	else
		STX_State.NextSector_ID_Field_CRC_OK = 1;		/* CRC correct */

	/* BitPosition in STX seems to point just after the IDAM $FE ; we need to point 4 bytes earlier at the 1st $A1 */
	Delay_FdcCycles -= 4 * FDC_DELAY_CYCLE_MFM_BYTE;		/* Correct delay to point to $A1 $A1 $A1 $FE */
	
//fprintf ( stderr , "fdc bytes next sector pos=%d delay=%d maxsr=%d nextsr=%d\n" , CurrentPos_FdcCycles, Delay_FdcCycles, pStxTrack->SectorsCount, STX_State.NextSectorStruct_Nbr );
	return Delay_FdcCycles;
}



/*-----------------------------------------------------------------------*/
/**
 * Return the value of the track number in the next ID field set by
 * FDC_NextSectorID_FdcCycles_STX.
 */
uint8_t	FDC_NextSectorID_TR_STX ( void )
{
	return STX_State.NextSector_ID_Field_TR;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of the sector number in the next ID field set by
 * FDC_NextSectorID_FdcCycles_STX.
 */
uint8_t	FDC_NextSectorID_SR_STX ( void )
{
	return STX_State.NextSector_ID_Field_SR;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of the sector's length in the next ID field set by
 * FDC_NextSectorID_FdcCycles_STX.
 */
uint8_t	FDC_NextSectorID_LEN_STX ( void )
{
	return STX_State.NextSector_ID_Field_LEN;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the status of the CRC in the next ID field set by
 * FDC_NextSectorID_FdcCycles_STX.
 * If '0', CRC is bad, else CRC is OK
 */
uint8_t	FDC_NextSectorID_CRC_OK_STX ( void )
{
	return STX_State.NextSector_ID_Field_CRC_OK;
}


/*-----------------------------------------------------------------------*/
/**
 * Read a sector from a floppy image in STX format (used in type II command)
 * We return the sector NextSectorStruct_Nbr, whose value was set
 * by the latest call to FDC_NextSectorID_FdcCycles_STX
 * Each byte of the sector is added to the FDC buffer with a default timing
 * (32 microsec) or a variable timing, depending on the sector's flags.
 * Some sectors can also contains "fuzzy" bits.
 * Special care must be taken to compute the timing of each byte, which can
 * be a decimal value and must be rounded to the best possible integer.
 *
 * If the sector's data were changed by a 'write sector' command, then we assume
 * a sector with no fuzzy byte and standard timings.
 *
 * Return RNF if sector was not found, else return CRC and RECORD_TYPE values
 * for the status register.
 */
uint8_t	FDC_ReadSector_STX ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side , int *pSectorSize )
{
	STX_SECTOR_STRUCT	*pStxSector;
	int			i;
	uint8_t			Byte;
	uint16_t			Timing;
	uint32_t			Sector_ReadTime;
	double			Total_cur;				/* To compute closest integer timings for each byte */
	double			Total_prev;
	uint8_t			*pSector_WriteData;

	pStxSector = STX_FindSector ( Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
	if ( pStxSector == NULL )
	{
		Log_Printf ( LOG_WARN , "FDC_ReadSector_STX drive=%d track=%d side=%d sector=%d returns null !\n" ,
				Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
		return STX_SECTOR_FLAG_RNF;				/* Should not happen if FDC_NextSectorID_FdcCycles_STX succeeded before */
	}

	/* If RNF is set, return FDC_STR_BIT_RNF */
	if ( pStxSector->FDC_Status & STX_SECTOR_FLAG_RNF )
		return STX_SECTOR_FLAG_RNF;				/* RNF in FDC's status register */

	*pSectorSize = pStxSector->SectorSize;
	Sector_ReadTime = pStxSector->ReadTime;

	/* Check if this sector was changed by a 'write sector' command */
	/* If so, we use this recent buffer instead of the original STX content */
	if (STX_SaveStruct[Drive].SaveSectorsCount > 0 && pStxSector->SaveSectorIndex >= 0)
	{
		pSector_WriteData = STX_SaveStruct[ Drive ].pSaveSectorsStruct[ pStxSector->SaveSectorIndex ].pData;
		Sector_ReadTime = 0;					/* Standard timings */

		LOG_TRACE(TRACE_FDC, "fdc stx read sector drive=%d track=%d sect=%d side=%d using saved sector=%d\n" ,
			Drive, Track, Sector, Side , pStxSector->SaveSectorIndex );
	}
	else
		pSector_WriteData = NULL;

	if ( Sector_ReadTime == 0 )					/* Sector has a standard delay (32 us per byte) */
		Sector_ReadTime = 32 * pStxSector->SectorSize;		/* Use the real standard value instead of 0 */
	Sector_ReadTime *= 8;						/* Convert delay in us to a number of FDC cycles at 8 MHz */

	Total_prev = 0;
	for ( i=0 ; i<pStxSector->SectorSize ; i++ )
	{
		/* Get the value of each byte, with possible fuzzy bits */
		if ( pSector_WriteData == NULL )			/* Use original STX content */
		{
			Byte = pStxSector->pData[ i ];
			if ( pStxSector->pFuzzyData )
				Byte = ( Byte & pStxSector->pFuzzyData[ i ] ) | ( Hatari_rand() & ~pStxSector->pFuzzyData[ i ] );
		}

		else							/* Use data from 'write sector' */
			Byte = pSector_WriteData[ i ];

		/* Compute the timing in FDC cycles to transfer this byte */
		if ( ( pStxSector->pTimingData )			/* Specific timing for each block of 16 bytes */
		  && ( pSector_WriteData == NULL ) )
		{
			Timing = ( pStxSector->pTimingData[ ( i>>4 ) * 2 ] << 8 )
				+ pStxSector->pTimingData[ ( i>>4 ) * 2 + 1 ];	/* Get big endian timing for this block of 16 bytes */

			/* [NP] Formula to convert timing data comes from Pasti.prg 0.4b : */
			/* 1 unit of timing = 32 FDC cycles at 8 MHz + 28 cycles to complete each block of 16 bytes */
			Timing = Timing * 32 + 28;

			if ( i % 16 == 0 )	Total_prev = 0;		/* New block of 16 bytes */
			Total_cur = ( (double)Timing * ( ( i % 16 ) + 1 ) ) / 16;
			Timing = rint ( Total_cur - Total_prev );
			Total_prev += Timing;
		}
		else							/* Specific timing in us for the whole sector */
		{
			Total_cur = ( (double)Sector_ReadTime * ( i+1 ) ) / pStxSector->SectorSize;
			Timing = rint ( Total_cur - Total_prev );
			Total_prev += Timing;
		}

		/* Add the Byte to the buffer, Timing should be a number of FDC cycles at 8 MHz */
		FDC_Buffer_Add_Timing ( Byte , Timing );
	}

	/* Return only bits 3 and 5 of the FDC_Status */
	return pStxSector->FDC_Status & ( STX_SECTOR_FLAG_CRC | STX_SECTOR_FLAG_RECORD_TYPE );
}


/*-----------------------------------------------------------------------*/
/**
 * Write a sector to a floppy image in STX format (used in type II command)
 *
 * STX format doesn't support write command. For each 'write sector' we
 * store the sector data in a dedicated buffer STX_SaveStruct[].pSaveSectorsStruct.
 * When the sector is read later, we return the data from STX_SaveStruct[].pSaveSectorsStruct
 * instead of returning the data from the original STX file.
 *
 * We only allow writing for sectors whose ID field has a correct CRC and
 * where RNF is not set.
 * Any valid size can be written : 128, 256, 512 or 1024 bytes
 *
 * NOTE : data will saved in memory snapshot, as well as in an additional
 * file with the extension .wd1772.
 *
 * Return RNF if sector was not found or CRC if ID field has a CRC error.
 * Return 0 if OK.
 */
uint8_t	FDC_WriteSector_STX ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side , int SectorSize )
{
	STX_SECTOR_STRUCT	*pStxSector;
	int			i;
	uint8_t			*pSector_WriteData;
	void			*pNewBuf;
	STX_SAVE_SECTOR_STRUCT	*pStxSaveSector;

	pStxSector = STX_FindSector ( Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
	if ( pStxSector == NULL )
	{
		Log_Printf ( LOG_WARN , "FDC_WriteSector_STX drive=%d track=%d side=%d sector=%d returns null !\n" ,
				Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
		return STX_SECTOR_FLAG_RNF;				/* Should not happen if FDC_NextSectorID_FdcCycles_STX succeeded before */
	}

	/* If RNF is set, return FDC_STR_BIT_RNF */
	if ( pStxSector->FDC_Status & STX_SECTOR_FLAG_RNF )
		return STX_SECTOR_FLAG_RNF;				/* RNF in FDC's status register */

	/* If CRC is set, return FDC_STR_BIT_RNF */
	if ( pStxSector->FDC_Status & STX_SECTOR_FLAG_CRC )
		return STX_SECTOR_FLAG_CRC;				/* CRC in FDC's status register */


	/* Check if this sector was already changed by a 'write sector' command */
	/* If so, we use the same buffer. Else we alloc a new buffer for this sector */
	if ( pStxSector->SaveSectorIndex < 0 )
	{
//fprintf ( stderr , "realloc\n" );
		/* Increase save buffer by 1 */
		pNewBuf = realloc ( STX_SaveStruct[ Drive ].pSaveSectorsStruct ,
				    ( STX_SaveStruct[ Drive ].SaveSectorsCount + 1 ) * sizeof ( STX_SAVE_SECTOR_STRUCT ) );
		if ( pNewBuf == NULL )
		{
			Log_Printf ( LOG_ERROR , "FDC_WriteSector_STX drive=%d track=%d side=%d sector=%d realloc error !\n" ,
					Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
			return STX_SECTOR_FLAG_RNF;
		}

		/* Save the new buffer values */
		STX_SaveStruct[ Drive ].pSaveSectorsStruct = (STX_SAVE_SECTOR_STRUCT *) pNewBuf;;
		STX_SaveStruct[ Drive ].SaveSectorsCount++;

		/* Create the new entry in pSaveSectorsStruct */
		pNewBuf = malloc ( SectorSize );
		if ( pNewBuf == NULL )
		{
			Log_Printf ( LOG_ERROR , "FDC_WriteSector_STX drive=%d track=%d side=%d sector=%d malloc error !\n" ,
					Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
			return STX_SECTOR_FLAG_RNF;
		}

		pStxSector->SaveSectorIndex = STX_SaveStruct[ Drive ].SaveSectorsCount - 1;

		/* Fill the new SaveSectorStruct. We copy some of the original sector's values */
		/* in the saved sector */
		pStxSaveSector = &STX_SaveStruct[ Drive ].pSaveSectorsStruct[ pStxSector->SaveSectorIndex ];

		pStxSaveSector->Track		= Track;
		pStxSaveSector->Side		= Side;
		pStxSaveSector->BitPosition	= pStxSector->BitPosition;
		pStxSaveSector->ID_Track	= pStxSector->ID_Track;
		pStxSaveSector->ID_Head		= pStxSector->ID_Head;
		pStxSaveSector->ID_Sector	= pStxSector->ID_Sector;
		pStxSaveSector->ID_Size		= pStxSector->ID_Size;
		pStxSaveSector->ID_CRC		= pStxSector->ID_CRC;

		pStxSaveSector->SectorSize	= SectorSize;
		pStxSaveSector->pData		= (uint8_t *) pNewBuf;

		pStxSaveSector->StructIsUsed	= 1;
	}

	pSector_WriteData = STX_SaveStruct[ Drive ].pSaveSectorsStruct[ pStxSector->SaveSectorIndex ].pData;

	/* Get the sector's data (ignore timings) */
	for ( i=0 ; i<SectorSize ; i++ )
		pSector_WriteData[ i ] = FDC_Buffer_Read_Byte_pos ( i );

//fprintf ( stderr , "write drive=%d track=%d side=%d sector=%d size=%d index=%d\n", Drive, Track, Side, Sector, SectorSize , pStxSector->SaveSectorIndex );
//Str_Dump_Hex_Ascii ( (char *) pSector_WriteData, SectorSize, 16, "" , stderr );

	/* Warn that 'write sector' data will be lost or saved (if zipped or not) */
	if ( STX_State.ImageBuffer[ Drive ]->WarnedWriteSector == false )
	{
		if ( File_DoesFileExtensionMatch ( EmulationDrives[ Drive ].sFileName , ".zip" ) )
			Log_AlertDlg ( LOG_INFO , "WARNING : can't save changes made with 'write sector' to an STX disk inside a zip file" );
		else
			Log_AlertDlg ( LOG_INFO , "Changes made with 'write sector' to an STX disk will be saved into an additional .wd1772 file" );
		STX_State.ImageBuffer[ Drive ]->WarnedWriteSector = true;
	}


	/* No error */
	EmulationDrives[Drive].bContentsChanged = true;
	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Read an address field from a floppy image in STX format (used in type III command)
 * We return the address field NextSectorStruct_Nbr, whose value was set
 * by the latest call to FDC_NextSectorID_FdcCycles_STX
 * Each byte of the ID field is added to the FDC buffer with a default timing
 * (32 microsec)
 * Return 0 if OK, or a CRC error
 */
uint8_t	FDC_ReadAddress_STX ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side )
{
	STX_SECTOR_STRUCT	*pStxSector;

	pStxSector = STX_FindSector ( Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
	if ( pStxSector == NULL )
	{
		Log_Printf ( LOG_ERROR , "FDC_ReadAddress_STX drive=%d track=%d side=%d sector=%d returns null !\n" ,
				Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
		return STX_SECTOR_FLAG_RNF;				/* Should not happen if FDC_NextSectorID_FdcCycles_STX succeeded before */
	}

	FDC_Buffer_Add ( pStxSector->ID_Track );
	FDC_Buffer_Add ( pStxSector->ID_Head );
	FDC_Buffer_Add ( pStxSector->ID_Sector );
	FDC_Buffer_Add ( pStxSector->ID_Size );
	FDC_Buffer_Add ( pStxSector->ID_CRC >> 8 );
	FDC_Buffer_Add ( pStxSector->ID_CRC & 0xff );

	/* If RNF is set and CRC error is set, then this ID field has a CRC error */
	if ( ( pStxSector->FDC_Status & STX_SECTOR_FLAG_RNF ) && ( pStxSector->FDC_Status & STX_SECTOR_FLAG_CRC ) )
		return STX_SECTOR_FLAG_CRC;

	return 0;							/* No error */
}


/*-----------------------------------------------------------------------*/
/**
 * Read a track from a floppy image in STX format (used in type III command)
 * This function is called after an index pulse was encountered, and it will
 * always succeeds and fill the track buffer.
 * If the Track/Side infos exist in the STX image, then the corresponding
 * bytes from the track's image are returned.
 * If these Track/Side infos don't exist, we return some random bytes
 * (empty / not formatted track).
 * If the Track/Side infos exist but there's no track's image, then we build
 * a standard track by using the available sectors and standard GAP values.
 * Return 0 if OK
 */
uint8_t	FDC_ReadTrack_STX ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	STX_TRACK_STRUCT	*pStxTrack;
	STX_SECTOR_STRUCT	*pStxSector;
	int			i;
	uint16_t			Timing;
	uint32_t			Track_ReadTime;
	double			Total_cur;				/* To compute closest integer timings for each byte */
	double			Total_prev;
	int			TrackSize;
	int			Sector;
	int			SectorSize;
	uint16_t  		CRC;
	uint8_t			*pData;
	uint8_t			Byte;
	
	if ( STX_State.ImageBuffer[ Drive ] == NULL )
	{
		Log_Printf ( LOG_ERROR , "FDC_ReadTrack_STX drive=%d track=%d side=%d, no image buffer !\n" , Drive , Track , Side );
		return STX_SECTOR_FLAG_RNF;				/* Should not happen, just in case of a bug */
	}

	pStxTrack = STX_FindTrack ( Drive , Track , Side );
	if ( pStxTrack == NULL )					/* Track/Side don't exist in this STX image */
	{
		Log_Printf ( LOG_WARN , "fdc stx : track info not found for read track drive=%d track=%d side=%d, returning random bytes\n" , Drive , Track , Side );
		for ( i=0 ; i<FDC_GetBytesPerTrack_STX ( Drive , Track , Side ) ; i++ )
			FDC_Buffer_Add ( Hatari_rand() & 0xff );	/* Fill the track buffer with random bytes */
		return 0;
	}

	/* If the Track block contains a complete dump of the track image, use it directly */
	/* The timing for each byte is the average timing based on TrackImageSize */
	if ( pStxTrack->pTrackImageData )
	{
		Track_ReadTime = 8000000 / 5;				/* 300 RPM, gives 5 RPS and 1600000 cycles per revolution at 8 MHz */
		Total_prev = 0;
		for ( i=0 ; i<pStxTrack->TrackImageSize ; i++ )
		{
			Total_cur = ( (double)Track_ReadTime * ( i+1 ) ) / pStxTrack->TrackImageSize;
			Timing = rint ( Total_cur - Total_prev );
			Total_prev += Timing;
			/* Add each byte to the buffer, Timing should be a number of FDC cycles at 8 MHz */
			FDC_Buffer_Add_Timing ( pStxTrack->pTrackImageData[ i ] , Timing );
		}
	}

	/* If the track block doesn't contain a dump of the track image, we must build a track */
	/* using the sector blocks and some standard GAP values */
	/* [NP] NOTE : we build a track of pStxTrack->MFMSize bytes, as this seems to always be != 0 */
	/* even for empty / not formatted track */
	/* [NP] NOTE : instead of using standard GAP values, we could compute GAP based on pStxSector->BitPosition */
	/* but this seems unnecessary, as a track image would certainly be present if precise GAP values */
	/* were required */
	else
	{
		TrackSize = pStxTrack->MFMSize;
		if ( ( pStxTrack->Flags & STX_TRACK_FLAG_SECTOR_BLOCK ) == 0 )
			TrackSize /= 8;					/* When the track contains only sector data, MFMSize is in bits */

		/* If there's no image for this track, and no sector as well, then track is empty / not formatted */
		if ( pStxTrack->SectorsCount == 0 )
		{
			Log_Printf ( LOG_WARN , "fdc stx : no track image and no sector for read track drive=%d track=%d side=%d, building an unformatted track\n" , Drive , Track , Side );
			for ( i=0 ; i<TrackSize ; i++ )
				FDC_Buffer_Add ( Hatari_rand() & 0xff ); /* Fill the track buffer with random bytes */
			return 0;
		}

		/* Use the available sectors and add some default GAPs to build the track */
		Log_Printf ( LOG_WARN , "fdc stx : no track image for read track drive=%d track=%d side=%d, building a standard track\n" , Drive , Track , Side );

		for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP1 ; i++ )	/* GAP1 */
			FDC_Buffer_Add ( 0x4e );

		for ( Sector=0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
		{
			pStxSector = &(pStxTrack->pSectorsStruct[ Sector ]);
			SectorSize = pStxSector->SectorSize;

			/* Check that the data+GAPs for this sector will not be above track's length */
			/* (in case we build a track with a high / non standard number of sectors) */
			if ( FDC_Buffer_Get_Size () + SectorSize + FDC_TRACK_LAYOUT_STANDARD_GAP2 + 10 + FDC_TRACK_LAYOUT_STANDARD_GAP3a
				+ FDC_TRACK_LAYOUT_STANDARD_GAP3b + 4 + 2 + FDC_TRACK_LAYOUT_STANDARD_GAP4 >= TrackSize )
			{
				Log_Printf ( LOG_WARN , "fdc stx : no track image for read track drive=%d track=%d side=%d, too many data sector=%d\n" , Drive , Track , Side , Sector );
				break;					/* Exit the loop and fill the rest of the track */
			}

			for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP2 ; i++ )	/* GAP2 */
				FDC_Buffer_Add ( 0x00 );

			/* Add the ID field for the sector */
			for ( i=0 ; i<3 ; i++ )
				FDC_Buffer_Add ( 0xa1 );		/* SYNC (write $F5) */
			FDC_Buffer_Add ( 0xfe );			/* Index Address Mark */
			FDC_Buffer_Add ( pStxSector->ID_Track );
			FDC_Buffer_Add ( pStxSector->ID_Head );
			FDC_Buffer_Add ( pStxSector->ID_Sector );
			FDC_Buffer_Add ( pStxSector->ID_Size );
			FDC_Buffer_Add ( pStxSector->ID_CRC >> 8 );
			FDC_Buffer_Add ( pStxSector->ID_CRC & 0xff );

			for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP3a ; i++ )	/* GAP3a */
				FDC_Buffer_Add ( 0x4e );
			for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP3b ; i++ )	/* GAP3b */
				FDC_Buffer_Add ( 0x00 );

			/* Add the data for the sector + build the CRC */
			crc16_reset ( &CRC );
			for ( i=0 ; i<3 ; i++ )
			{
				FDC_Buffer_Add ( 0xa1 );		/* SYNC (write $F5) */
				crc16_add_byte ( &CRC , 0xa1 );
			}

			FDC_Buffer_Add ( 0xfb );			/* Data Address Mark */
			crc16_add_byte ( &CRC , 0xfb );

			/* [NP] NOTE : when building the sector, we assume there's no specific timing or fuzzy bytes */
			/* If it was not the case, there would certainly be a real track image (and STX format doesn't */
			/* support fuzzy bytes or specific timing for a track image anyway) */
			/* If the sector was changed by a 'write sector' command, we use the data from pSaveSectorsStruct */
			if ( pStxSector->SaveSectorIndex < 0 )		/* Use original data from the STX */
				pData = pStxSector->pData;
			else						/* Use data from the 'write sector' */
				pData = STX_SaveStruct[ Drive ].pSaveSectorsStruct[ pStxSector->SaveSectorIndex ].pData;

			for ( i=0 ; i<SectorSize ; i++ )
			{
				Byte = pData[ i ];
				FDC_Buffer_Add ( Byte );
				crc16_add_byte ( &CRC , Byte );
			}

			FDC_Buffer_Add ( CRC >> 8 );			/* CRC1 (write $F7) */
			FDC_Buffer_Add ( CRC & 0xff );			/* CRC2 */

			for ( i=0 ; i<FDC_TRACK_LAYOUT_STANDARD_GAP4 ; i++ )	/* GAP4 */
				FDC_Buffer_Add ( 0x4e );
		}

		while ( FDC_Buffer_Get_Size () < TrackSize )		/* Complete the track buffer */
		      FDC_Buffer_Add ( 0x4e );				/* GAP5 */
	}

	return 0;							/* No error */
}


/*-----------------------------------------------------------------------*/
/**
 * Write a track to a floppy image in STX format (used in type III command)
 *
 * STX format doesn't support write command. For each 'write track' we
 * store the track data in a dedicated buffer STX_SaveStruct[].pSaveTracksStruct.
 * When the track is read later, we return the data from STX_SaveStruct[].pSaveTracksStruct
 * instead of returning the data from the original STX file.
 *
 * NOTE : data will saved in memory snapshot, as well as in an additional
 * file with the extension .wd1772.
 *
 * Return 0 if track was written without error, or LOST_DATA if an error occurred
 */
uint8_t	FDC_WriteTrack_STX ( uint8_t Drive , uint8_t Track , uint8_t Side , int TrackSize )
{
	STX_TRACK_STRUCT	*pStxTrack;
	int			i;
	uint8_t			*pTrack_DataWrite;
	void			*pNewBuf;
	STX_SAVE_TRACK_STRUCT	*pStxSaveTrack;
	int			Sector;

	pStxTrack = STX_FindTrack ( Drive , Track , Side );
	if ( pStxTrack == NULL )
	{
		Log_Printf ( LOG_WARN , "FDC_WriteTrack_STX drive=%d track=%d side=%d returns null !\n" ,
				Drive , Track , Side );
		return STX_SECTOR_FLAG_LOST_DATA;
	}

	/* Check if this track was already changed by a 'write track' command */
	/* If so, we use the same structure. Else we alloc a new structure for this track */
	if ( pStxTrack->SaveTrackIndex < 0 )
	{
//fprintf ( stderr , "realloc\n" );
		/* Increase save buffer by 1 */
		pNewBuf = realloc ( STX_SaveStruct[ Drive ].pSaveTracksStruct ,
				    ( STX_SaveStruct[ Drive ].SaveTracksCount + 1 ) * sizeof ( STX_SAVE_TRACK_STRUCT ) );
		if ( pNewBuf == NULL )
		{
			Log_Printf ( LOG_WARN , "FDC_WriteTrack_STX drive=%d track=%d side=%d realloc error !\n" ,
					Drive , Track , Side );
			return STX_SECTOR_FLAG_LOST_DATA;
		}

		/* Save the new buffer values */
		STX_SaveStruct[ Drive ].pSaveTracksStruct = (STX_SAVE_TRACK_STRUCT *) pNewBuf;;
		STX_SaveStruct[ Drive ].SaveTracksCount++;

		pStxTrack->SaveTrackIndex = STX_SaveStruct[ Drive ].SaveTracksCount - 1;
	}

	/* Use the same structure : free previous DataWrite buffer */
	else
	{
		free ( STX_SaveStruct[ Drive ].pSaveTracksStruct[ pStxTrack->SaveTrackIndex ].pDataWrite );
		STX_SaveStruct[ Drive ].pSaveTracksStruct[ pStxTrack->SaveTrackIndex ].pDataWrite = NULL;
		/* TODO : also free pDataRead */
	}
		
	/* Create the new DataWrite buffer in pSaveTracksStruct */
	pNewBuf = malloc ( TrackSize );
	if ( pNewBuf == NULL )
	{
		Log_Printf ( LOG_WARN , "FDC_WriteTrack_STX drive=%d track=%d side=%d malloc error !\n" ,
				Drive , Track , Side );
		return STX_SECTOR_FLAG_LOST_DATA;
	}

	/* Fill the new SaveTrackStruct */
	pStxSaveTrack = &STX_SaveStruct[ Drive ].pSaveTracksStruct[ pStxTrack->SaveTrackIndex ];

	pStxSaveTrack->Track = Track;
	pStxSaveTrack->Side = Side;

	pStxSaveTrack->TrackSizeWrite = TrackSize;
	pStxSaveTrack->pDataWrite = (uint8_t *) pNewBuf;


	/* Get the track's data (ignore timings) */
	pTrack_DataWrite = STX_SaveStruct[ Drive ].pSaveTracksStruct[ pStxTrack->SaveTrackIndex ].pDataWrite;

	for ( i=0 ; i<pStxSaveTrack->TrackSizeWrite ; i++ )
		pTrack_DataWrite[ i ] = FDC_Buffer_Read_Byte_pos ( i );

//fprintf ( stderr , "write drive=%d track=%d side=%d size=%d index=%d\n", Drive, Track, Side, pStxSaveTrack->TrackSizeWrite , pStxTrack->SaveTrackIndex );
//Str_Dump_Hex_Ascii ( (char *) pTrack_DataWrite, pStxSaveTrack->TrackSizeWrite, 16, "" , stderr );

	// TODO : convert pDataWrite into pDataRead
	pStxSaveTrack->TrackSizeRead = 0;	/* TODO : compute interpreted track */
	pStxSaveTrack->pDataRead = NULL;	/* TODO : compute interpreted track */

	
	/* If some sectors were already saved for that track, we must remove them */
	/* as the 'write track' takes precedence over the previous 'write sector' */
	for ( Sector=0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
	{
		if ( pStxTrack->pSectorsStruct[ Sector ].SaveSectorIndex >= 0 )
		{
			STX_FreeSaveSectorsStruct ( STX_SaveStruct[ Drive ].pSaveSectorsStruct ,
					pStxTrack->pSectorsStruct[ Sector ].SaveSectorIndex );
			pStxTrack->pSectorsStruct[ Sector ].SaveSectorIndex = -1;
		}
	}


	/* Warn that 'write track' data will be lost or saved (if zipped or not) */
	if ( STX_State.ImageBuffer[ Drive ]->WarnedWriteTrack == false )
	{
		if ( File_DoesFileExtensionMatch ( EmulationDrives[ Drive ].sFileName , ".zip" ) )
			Log_AlertDlg ( LOG_INFO , "WARNING : can't save changes made with 'write track' to an STX disk inside a zip file" );
		else
			Log_AlertDlg ( LOG_INFO , "Changes made with 'write track' to an STX disk will be saved into an additional .wd1772 file" );
		STX_State.ImageBuffer[ Drive ]->WarnedWriteTrack = true;
	}


	/* No error */
	EmulationDrives[Drive].bContentsChanged = true;
	return 0;
}
