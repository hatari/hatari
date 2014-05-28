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
#include "utils.h"


#define	STX_DEBUG_FLAG_STRUCTURE	1
#define	STX_DEBUG_FLAG_DATA		2

#define	STX_DEBUG_FLAG			( STX_DEBUG_FLAG_STRUCTURE )
// #define	STX_DEBUG_FLAG			( STX_DEBUG_FLAG_STRUCTURE | STX_DEBUG_FLAG_DATA )


#define FDC_DELAY_CYCLE_MFM_BIT			( 4 * 8 )	/* 4 us per bit, 8 MHz clock -> 32 cycles */
#define FDC_DELAY_CYCLE_MFM_BYTE		( 4 * 8 * 8 )	/* 4 us per bit, 8 bits per byte, 8 MHz clock -> 256 cycles */

#define FDC_TRACK_BYTES_STANDARD	6250


typedef struct
{
	STX_MAIN_STRUCT		*ImageBuffer[ MAX_FLOPPYDRIVES ];	/* For the STX disk images */

	Uint32			NextSectorStruct_Nbr;		/* Sector Number in pSectorsStruct after a call to FDC_NextSectorID_FdcCycles_STX() */
	Uint8			NextSector_ID_Field_TR;		/* Track value in the next ID Field after a call to FDC_NextSectorID_FdcCycles_STX() */
	Uint8			NextSector_ID_Field_SR;		/* Sector value in the next ID Field after a call to FDC_NextSectorID_FdcCycles_STX() */
	Uint8			NextSector_ID_Field_CRC_OK;	/* CRC OK or not in the next ID Field after a call to FDC_NextSectorID_FdcCycles_STX() */
	
} STX_STRUCT;


static STX_STRUCT	STX_State;			/* All variables related to the STX support */


/* Default timing table for Macrodos when revision=0 */
/* 1 unit of timing means 32 FDC cycles ; + 28 cycles every 16 bytes, so a standard block of 16 bytes */
/* should have a value of 0x7f or 0x80, which gives 4092-4124 cycles */
Uint8	TimingDataDefault[] = {
	0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,
	0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,
	0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,
	0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f
	};



/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static void	STX_BuildSectorsSimple ( STX_TRACK_STRUCT *pStxTrack , Uint8 *p );
static Uint16	STX_BuildSectorID_CRC ( STX_SECTOR_STRUCT *pStxSector );
static STX_TRACK_STRUCT	*STX_FindTrack ( Uint8 Drive , Uint8 Track , Uint8 Side );
static STX_SECTOR_STRUCT *STX_FindSector ( Uint8 Drive , Uint8 Track , Uint8 Side , Uint8 SectorStruct_Nb );



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
	}

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Init the ressources to handle the STX image inserted into a drive (0=A: 1=B:)
 */
bool	STX_Insert ( int Drive , Uint8 *pImageBuffer , long ImageSize )
{
	fprintf ( stderr , "STX : STX_Insert drive=%d buf=%p size=%ld\n" , Drive , pImageBuffer , ImageSize );

	STX_State.ImageBuffer[ Drive ] = STX_BuildStruct ( pImageBuffer , STX_DEBUG_FLAG );

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * When ejecting a disk, free the ressources associated with an STX image
 */
bool	STX_Eject ( int Drive )
{
	fprintf ( stderr , "STX : STX_Eject drive=%d\n" , Drive );

	if ( STX_State.ImageBuffer[ Drive ] )
		STX_FreeStruct ( STX_State.ImageBuffer[ Drive ] );
	STX_State.ImageBuffer[ Drive ] = NULL;

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Read words and longs stored in little endian order
 */
static Uint16	STX_ReadU16 ( Uint8 *p )
{
	return (p[1]<<8) +p[0];
}

static Uint32	STX_ReadU32 ( Uint8 *p )
{
	return (p[3]<<24) + (p[2]<<16) + (p[1]<<8) +p[0];
}


/*-----------------------------------------------------------------------*/
/**
 * Free all the memory allocated to store an STX file
 */
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


/*-----------------------------------------------------------------------*/
/**
 * Parse an STX file.
 * The file is in pFileBuffer and we dynamically allocate memory to store
 * the components (main header, tracks, sectors).
 * Some internal variables/pointers are also computed, to speed up
 * data access when the FDC emulates an STX file.
 */
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
	{
		p_cur = p;

		pStxTrack->BlockSize		=	STX_ReadU32 ( p ); p += 4;
		pStxTrack->FuzzySize		=	STX_ReadU32 ( p ); p += 4;
		pStxTrack->SectorsCount		=	STX_ReadU16 ( p ); p += 2;
		pStxTrack->Flags		=	STX_ReadU16 ( p ); p += 2;
		pStxTrack->MFMSize		=	STX_ReadU16 ( p ); p += 2;
		pStxTrack->TrackNumber		=	*p++;
		pStxTrack->RecordType		=	*p++;

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

		if ( pStxTrack->SectorsCount == 0 )			/* No sector (track image only, or empty / non formatted track) */
			goto next_track;

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
 * no timings informations, we must compute some default values for each
 * sector, as well as the position of the corresponding 512 bytes of data.
 * This is only used when storing unprotected tracks.
 */
static void	STX_BuildSectorsSimple ( STX_TRACK_STRUCT *pStxTrack , Uint8 *p )
{
	int	Sector;
	int	BytePosition;
	Uint16	CRC;

	BytePosition = FDC_TRACK_LAYOUT_STANDARD_GAP1 + FDC_TRACK_LAYOUT_STANDARD_GAP2;		/* Points to the 3x$A1 before the 1st IDAM $FE */
	BytePosition += 4;						/* Pasti seems to point after the 3x$A1 and the IDAM $FE */
	
	for ( Sector = 0 ; Sector < pStxTrack->SectorsCount ; Sector++ )
	{
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
static Uint16	STX_BuildSectorID_CRC ( STX_SECTOR_STRUCT *pStxSector )
{
        Uint16  CRC;

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



static STX_TRACK_STRUCT	*STX_FindTrack ( Uint8 Drive , Uint8 Track , Uint8 Side )
{
	int	i;

	if ( STX_State.ImageBuffer[ Drive ] == NULL )
		return NULL;

	for ( i=0 ; i<STX_State.ImageBuffer[ Drive ]->TracksCount ; i++ )
		if ( STX_State.ImageBuffer[ Drive ]->pTracksStruct[ i ].TrackNumber == ( ( Track & 0x7f ) | ( Side << 7 ) ) )
			return &(STX_State.ImageBuffer[ Drive ]->pTracksStruct[ i ]);

	return NULL;
}


static STX_SECTOR_STRUCT	*STX_FindSector ( Uint8 Drive , Uint8 Track , Uint8 Side , Uint8 SectorStruct_Nb )
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
 * Return the number of FDC cycles to go from one index pulse to the next
 * one on a given drive/track/side.
 * We take the TrackSize into account to return this delay.
 */
extern Uint32	FDC_GetCyclesPerRev_FdcCycles_STX ( Uint8 Drive , Uint8 Track , Uint8 Side )
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
	return TrackSize * FDC_DELAY_CYCLE_MFM_BYTE;
}



/*-----------------------------------------------------------------------*/
/**
 * Return the number of FDC cycles to wait before reaching the next
 * sector's ID Field in the track ($A1 $A1 $A1 $FE TR SIDE SR LEN CRC1 CRC2)
 * If no ID Field is found before the end of the track, we use the 1st
 * ID Field of the track (which simulates a full spin of the floppy).
 * We also store the next sector's number into NextSectorStruct_Nbr,
 * the next sector's number into NextSector_ID_Field_SR and if the CRC is correct
 * or not into NextSector_ID_Field_CRC_OK.
 * This function assumes the sectors of each track are sorted in ascending order
 * using BitPosition.
 * If there's no available drive/floppy or no ID field in the track, we return -1
 */
extern int	FDC_NextSectorID_FdcCycles_STX ( Uint8 Drive , Uint8 NumberOfHeads , Uint8 Track , Uint8 Side )
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

	/* Compare CurrentPos_FdcCycles with each sector's position in ascending order */
	for ( i=0 ; i<pStxTrack->SectorsCount ; i++ )
	{
		if ( CurrentPos_FdcCycles < (int)pStxTrack->pSectorsStruct[ i ].BitPosition*FDC_DELAY_CYCLE_MFM_BIT )	/* 1 bit = 32 cycles at 8 MHz */
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
extern Uint8	FDC_NextSectorID_TR_STX ( void )
{
	return STX_State.NextSector_ID_Field_TR;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the value of the sector number in the next ID field set by
 * FDC_NextSectorID_FdcCycles_STX.
 */
extern Uint8	FDC_NextSectorID_SR_STX ( void )
{
	return STX_State.NextSector_ID_Field_SR;
}


/*-----------------------------------------------------------------------*/
/**
 * Return the status of the CRC in the next ID field set by
 * FDC_NextSectorID_FdcCycles_STX.
 * If '0', CRC is bad, else CRC is OK
 */
extern Uint8	FDC_NextSectorID_CRC_OK_STX ( void )
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
 * Return RNF if sector was not found, else return CRC and RECORD_TYPE values
 * for the status register.
 */
extern Uint8	FDC_ReadSector_STX ( Uint8 Drive , Uint8 Track , Uint8 Sector , Uint8 Side , int *pSectorSize )
{
	STX_SECTOR_STRUCT	*pStxSector;
	int			i;
	Uint8			Byte;
	Uint16			Timing;
	Uint32			Sector_ReadTime;
	double			Total_cur;				/* To compute closest integer timings for each byte */
	double			Total_prev;

	pStxSector = STX_FindSector ( Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
	if ( pStxSector == NULL )
	{
		fprintf ( stderr , "FDC_ReadSector_STX drive=%d track=%d side=%d sector=%d returns null !\n" ,
				Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
		return STX_SECTOR_FLAG_RNF;				/* Should not happen if FDC_NextSectorID_FdcCycles_STX succeeded before */
	}

	/* If RNF is set, return FDC_STR_BIT_RNF */
	if ( pStxSector->FDC_Status & STX_SECTOR_FLAG_RNF )
		return STX_SECTOR_FLAG_RNF;				/* RNF in FDC's status register */

	*pSectorSize = pStxSector->SectorSize;

	Sector_ReadTime = pStxSector->ReadTime;
	if ( Sector_ReadTime == 0 )					/* Sector has a standard delay (32 us per byte) */
		Sector_ReadTime = 32 * pStxSector->SectorSize;		/* Use the real standard value instead of 0 */
	Sector_ReadTime *= 8;						/* Convert delay in us to a number of FDC cycles at 8 MHz */

	Total_prev = 0;
	for ( i=0 ; i<pStxSector->SectorSize ; i++ )
	{
		/* Get the value of each byte, with possible fuzzy bits */
		Byte = pStxSector->pData[ i ];
		if ( pStxSector->pFuzzyData )
			Byte = ( Byte & pStxSector->pFuzzyData[ i ] ) | ( rand() & ~pStxSector->pFuzzyData[ i ] );

		/* Compute the timing in FDC cycles to transfer this byte */
		if ( pStxSector->pTimingData )				/* Specific timing for each block of 16 bytes */
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
 * Read an address field from a floppy image in STX format (used in type III command)
 * We return the address field NextSectorStruct_Nbr, whose value was set
 * by the latest call to FDC_NextSectorID_FdcCycles_STX
 * Each byte of the ID field is added to the FDC buffer with a default timing
 * (32 microsec)
 * Return 0 if OK, or a CRC error
 */
extern Uint8	FDC_ReadAddress_STX ( Uint8 Drive , Uint8 Track , Uint8 Sector , Uint8 Side )
{
	STX_SECTOR_STRUCT	*pStxSector;

	pStxSector = STX_FindSector ( Drive , Track , Side , STX_State.NextSectorStruct_Nbr );
	if ( pStxSector == NULL )
	{
		fprintf ( stderr , "FDC_ReadAddress_STX drive=%d track=%d side=%d sector=%d returns null !\n" ,
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
extern Uint8	FDC_ReadTrack_STX ( Uint8 Drive , Uint8 Track , Uint8 Side )
{
	STX_TRACK_STRUCT	*pStxTrack;
	STX_SECTOR_STRUCT	*pStxSector;
	int			i;
	Uint16			Timing;
	Uint32			Track_ReadTime;
	double			Total_cur;				/* To compute closest integer timings for each byte */
	double			Total_prev;
	int			TrackSize;
	int			Sector;
	int			SectorSize;
	Uint16  		CRC;
	Uint8			Byte;
	
	if ( STX_State.ImageBuffer[ Drive ] == NULL )
	{
		fprintf ( stderr , "FDC_ReadTrack_STX drive=%d track=%d side=%d, no image buffer !\n" , Drive , Track , Side );
		return STX_SECTOR_FLAG_RNF;				/* Should not happen, just in case of a bug */
	}

	pStxTrack = STX_FindTrack ( Drive , Track , Side );
	if ( pStxTrack == NULL )					/* Track/Side don't exist in this STX image */
	{
		fprintf ( stderr , "fdc stx : track info not found for read track drive=%d track=%d side=%d, returning random bytes\n" , Drive , Track , Side );
 		for ( i=0 ; i<FDC_GetBytesPerTrack ( Drive ) ; i++ )
			FDC_Buffer_Add ( rand() & 0xff );		/* Fill the track buffer with random bytes */
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
			fprintf ( stderr , "fdc stx : no track image and no sector for read track drive=%d track=%d side=%d, building an unformatted track\n" , Drive , Track , Side );
			for ( i=0 ; i<TrackSize ; i++ )
				FDC_Buffer_Add ( rand() & 0xff );	/* Fill the track buffer with random bytes */
			return 0;
		}

		/* Use the available sectors and add some default GAPs to build the track */
		fprintf ( stderr , "fdc stx : no track image for read track drive=%d track=%d side=%d, building a standard track\n" , Drive , Track , Side );

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
				fprintf ( stderr , "fdc stx : no track image for read track drive=%d track=%d side=%d, too many data sector=%d\n" , Drive , Track , Side , Sector );
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
			for ( i=0 ; i<SectorSize ; i++ )
			{
				Byte = pStxSector->pData[ i ];
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



