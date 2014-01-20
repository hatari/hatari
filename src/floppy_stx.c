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
#include "str.h"


#define	STX_DEBUG_FLAG_STRUCTURE	1
#define	STX_DEBUG_FLAG_DATA		2

#define	STX_DEBUG_FLAG			( STX_DEBUG_FLAG_STRUCTURE )
// #define	STX_DEBUG_FLAG			( STX_DEBUG_FLAG_STRUCTURE | STX_DEBUG_FLAG_DATA )



typedef struct
{
	STX_MAIN_STRUCT		*ImageBuffer[ MAX_FLOPPYDRIVES ];	/* For the STX disk images */

} STX_STRUCT;


static STX_STRUCT	STX_State;			/* All variables related to the STX support */


Uint8	TimingDataDefault[] = {				/* Default timing table for Macrodos when revision=0 */
	0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,
	0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,
	0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,
	0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f
	};



/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static void	STX_BuildSectorsSimple ( STX_TRACK_STRUCT *pStxTrack , Uint8 *p );



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
	int	i;

	for ( i=0 ; i<MAX_FLOPPYDRIVES ; i++ )
	{
		STX_State.ImageBuffer[ i ] = NULL;
	}

	return true;
}




/*
 * Init the ressources to handle the STX image inserted into a drive (0=A: 1=B:)
 */
bool	STX_Insert ( int Drive , Uint8 *pImageBuffer , long ImageSize )
{
	fprintf ( stderr , "STX : STX_Insert drive=%d buf=%p size=%ld\n" , Drive , pImageBuffer , ImageSize );

	STX_BuildStruct ( pImageBuffer , STX_DEBUG_FLAG );

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





static Uint16	STX_ReadU16 ( Uint8 *p )
{
	return (p[1]<<8) +p[0];
}

static Uint32	STX_ReadU32 ( Uint8 *p )
{
	return (p[3]<<24) + (p[2]<<16) + (p[1]<<8) +p[0];
}


void	STX_FreeStruct ( STX_MAIN_STRUCT *pStxMain )
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


STX_MAIN_STRUCT	*STX_BuildStruct ( Uint8 *pFileBuffer , int Debug )
{

	STX_MAIN_STRUCT		*pStxMain;
	STX_TRACK_STRUCT	*pStxTrack;
	STX_SECTOR_STRUCT	*pStxSector;
	Uint8			*p;
	Uint8			*p_cur;
	int			Track;
	int			Sector;
	Uint8			*pFuzzyData;
	Uint8			*pTimingData;
	Uint32			MaxOffsetSectorEnd;
	int			VariableTimings;

	pStxMain = malloc ( sizeof ( STX_MAIN_STRUCT ) );
	if ( !pStxMain )
		return NULL;
	memset ( pStxMain , 0 , sizeof ( STX_MAIN_STRUCT ) );

	p = pFileBuffer;

	/* Read file's header */
	memcpy ( pStxMain->FileID , p , 4 ); p += 4;
	pStxMain->Version	=	STX_ReadU16 ( p ); p += 2;
	pStxMain->ImagingTool	=	STX_ReadU16 ( p ); p += 2;
	pStxMain->Reserved_1	=	STX_ReadU16 ( p ); p += 2;
	pStxMain->TracksCount	=	*p++;;
	pStxMain->Revision	=	*p++;
	pStxMain->Reserved_2	=	STX_ReadU32 ( p ); p += 4;

	if ( Debug & STX_DEBUG_FLAG_STRUCTURE )
		fprintf ( stderr , "STX header ID='%.4s' Version=%4.4x ImagingTool=%4.4x Reserved1=%4.4x"
			" TrackCount=%d Revision=%2.2x Reserved2=%x\n" , pStxMain->FileID , pStxMain->Version ,
			pStxMain->ImagingTool  , pStxMain->Reserved_1 , pStxMain->TracksCount , pStxMain->Revision ,
			pStxMain->Reserved_2 );


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
// 	for ( Track = 0 ; Track < 4 ; Track++ )
	{
		p_cur = p;

		pStxTrack->BlockSize		=	STX_ReadU32 ( p ); p += 4;
		pStxTrack->FuzzySize		=	STX_ReadU32 ( p ); p += 4;
		pStxTrack->SectorsCount		=	STX_ReadU16 ( p ); p += 2;
		pStxTrack->Flags		=	STX_ReadU16 ( p ); p += 2;
		pStxTrack->MFMSize		=	STX_ReadU16 ( p ); p += 2;
		pStxTrack->TrackNumber		=	*p++;
		pStxTrack->RecordType		=	*p++;

		if ( pStxTrack->SectorsCount == 0 )			/* Empty / non formatted track */
		{
			pStxTrack->pSectorsStruct = NULL;
			goto next_track;
		}

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
			STX_BuildSectorsSimple ( pStxTrack , p );
			goto next_track;
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
			pStxTrack->TrackImageSize = STX_ReadU16 ( pStxTrack->pTrackData );
			pStxTrack->pTrackImageData = pStxTrack->pTrackData + 2;
			pStxTrack->pSectorsImageData = pStxTrack->pTrackImageData + pStxTrack->TrackImageSize;
		}
		else									/* Track with sync offset + size + data */
		{
			pStxTrack->TrackImageSyncPosition = STX_ReadU16 ( pStxTrack->pTrackData );
			pStxTrack->TrackImageSize = STX_ReadU16 ( pStxTrack->pTrackData + 2 );
			pStxTrack->pTrackImageData = pStxTrack->pTrackData + 4;
			pStxTrack->pSectorsImageData = pStxTrack->pTrackImageData + pStxTrack->TrackImageSize;
		}


		/* Parse all the sectors in this track */
		pFuzzyData = pStxTrack->pFuzzyData;
		VariableTimings = 0;
		MaxOffsetSectorEnd = 0;
		for ( Sector = 0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
		{
			pStxSector = &(pStxTrack->pSectorsStruct[ Sector ]);

			pStxSector->DataOffset = STX_ReadU32 ( p ); p += 4;
			pStxSector->BitPosition = STX_ReadU16 ( p ); p += 2;
			pStxSector->ReadTime = STX_ReadU16 ( p ); p += 2;
			pStxSector->ID_Track = *p++;
			pStxSector->ID_Head = *p++;
			pStxSector->ID_Sector = *p++;
			pStxSector->ID_Size = *p++;
			pStxSector->ID_CRC = STX_ReadU16 ( p ); p += 2;
			pStxSector->FDC_Status = *p++;
			pStxSector->Reserved = *p++;

			/* Check if sector has data */
			if ( ( pStxSector->FDC_Status & STX_SECTOR_FLAG_RNF ) == 0 )
			{
				/* Check if SectorSize is valid */
				if ( pStxSector->ID_Size & ~FDC_SECTOR_SIZE_MASK )
				{
					fprintf ( stderr , "STX : invalid ID_Size=%d on track %d sector %d\n" ,
						  pStxSector->ID_Size , Track , Sector );
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
		}

		/* Start of the optional timings data, after the optional sectors image data */
		pStxTrack->pTiming = pStxTrack->pTrackData + MaxOffsetSectorEnd;
		if ( pStxTrack->pTiming < pStxTrack->pSectorsImageData )	/* If all sectors image were inside the track image */
			pStxTrack->pTiming = pStxTrack->pSectorsImageData;	/* then timings data are just after the track image */

		if ( VariableTimings == 1 )				/* Track has at least one variable sector */
		{
			if ( pStxMain->Revision == 2 )			/* Specific timing table  */
			{
				pStxTrack->TimingFlags = STX_ReadU16 ( pStxTrack->pTiming );	/* always '5' ? */
				pStxTrack->TimingSize = STX_ReadU16 ( pStxTrack->pTiming + 2 );
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
				" MFMSize=%d TrackNb=%d Side=%d RecordType=%x"
				" TrackImage=%s (%d bytes, sync=%4.4x) Timings=%d,%d\n" ,
				Track , pStxTrack->BlockSize ,
				pStxTrack->FuzzySize , pStxTrack->SectorsCount , pStxTrack->Flags , pStxTrack->MFMSize ,
				pStxTrack->TrackNumber & 0x7f , ( pStxTrack->TrackNumber >> 7 ) & 0x01 , pStxTrack->RecordType ,
				pStxTrack->pTrackImageData ? "yes" : "no" , pStxTrack->TrackImageSize , pStxTrack->TrackImageSyncPosition ,
				pStxTrack->TimingFlags , pStxTrack->TimingSize );

			if ( pStxTrack->SectorsCount == 0 )
				fprintf ( stderr , "    track empty / not formatted\n" );
			else
				for ( Sector = 0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
				{
					pStxSector = &(pStxTrack->pSectorsStruct[ Sector ]);
					fprintf ( stderr , "    sector %2d DataOffset=%d BitPosition=%d ReadTime=%d"
						" [track=%2.2x head=%2.2x sector=%2.2x size=%2.2x crc=%4.4x]"
						" FdcStatus=%2.2x Reserved=%2.2x TimingsOffset=%d\n" ,
						Sector , pStxSector->DataOffset , pStxSector->BitPosition ,
						pStxSector->ReadTime ,  pStxSector->ID_Track ,  pStxSector->ID_Head ,
						pStxSector->ID_Sector , pStxSector->ID_Size , pStxSector->ID_CRC ,
						pStxSector->FDC_Status , pStxSector->Reserved ,
						pStxSector->pTimingData ? pStxTrack->pTimingData - pStxTrack->pTrackData : 0 );

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
						Str_Dump_Hex_Ascii ( (char *)pStxSector->pTimingData , pStxSector->SectorSize / 16 ,
								16 , "        " , stderr );
					}
				}
		}

		p = p_cur + pStxTrack->BlockSize;			/* Next Track block */
		pStxTrack++;
	}



exit(0);

	return pStxMain;
}




/*-----------------------------------------------------------------------*/
/**
 * When a track only consists of the content of each 512 bytes sector and
 * no timings informations, we must compute some default values for each
 * sector, as well as the position of the corresponding 512 bytes of data.
 * This is only used when storing unprotected tracks.
 */

static void	STX_BuildSectorsSimple ( STX_TRACK_STRUCT *pStxTrack , Uint8 *p )
{
	int	Sector;

	for ( Sector = 0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
	{
		pStxTrack->pSectorsStruct[ Sector ].DataOffset = 0;
		pStxTrack->pSectorsStruct[ Sector ].BitPosition = 0;
		pStxTrack->pSectorsStruct[ Sector ].ReadTime = 0;
		pStxTrack->pSectorsStruct[ Sector ].ID_Track = pStxTrack->TrackNumber & 0x7f;
		pStxTrack->pSectorsStruct[ Sector ].ID_Head = ( pStxTrack->TrackNumber >> 7 ) & 0x01;
		pStxTrack->pSectorsStruct[ Sector ].ID_Sector = Sector + 1;
		pStxTrack->pSectorsStruct[ Sector ].ID_Size = FDC_SECTOR_SIZE_512;
		pStxTrack->pSectorsStruct[ Sector ].ID_CRC = 0;
		pStxTrack->pSectorsStruct[ Sector ].FDC_Status = 0;
		pStxTrack->pSectorsStruct[ Sector ].Reserved = 0;

		pStxTrack->pSectorsStruct[ Sector ].pData = p + Sector * 512;
	}
}


