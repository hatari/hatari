/*
  Hatari - floppy_scp.c

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  SCP (SuperCard Pro) disk image support.

  SCP files contain low level flux transitions as directly read from the floppy drive.
  These flux transitions will be converted to MFM bits and the resulting MFM buffer
  will be processed by emulating the WD1772 internal work.

  SCP files are mainly created using the SuperCard Pro board or using the Greaseweazle board,
  but they can also be converted from another disk image format using specific software.
  */
const char floppy_scp_fileid[] = "Hatari floppy_scp.c";

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "floppy_scp.h"
#include "fdc.h"
#include "log.h"
#include "memorySnapShot.h"
#include "screen.h"
#include "video.h"
#include "m68000.h"
#include "cycles.h"
#include "utils.h"


#define	SCP_DEBUG_FLAG_STRUCTURE	1
#define	SCP_DEBUG_FLAG_DATA		2

//#define	SCP_DEBUG_FLAG			0
// #define	SCP_DEBUG_FLAG			( SCP_DEBUG_FLAG_STRUCTURE )
#define	SCP_DEBUG_FLAG			( SCP_DEBUG_FLAG_STRUCTURE | SCP_DEBUG_FLAG_DATA )



typedef struct
{
	SCP_MAIN_STRUCT		*ImageBuffer[ MAX_FLOPPYDRIVES ];	/* For the SCP disk images */


} SCP_STRUCT;


static SCP_STRUCT	SCP_State;			/* All variables related to the SCP support */






/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static bool	SCP_Insert_internal ( int Drive , const char *FilenameSTX , Uint8 *pImageBuffer , long ImageSize );

static void	SCP_FreeStruct ( SCP_MAIN_STRUCT *pScpMain );



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void SCP_MemorySnapShot_Capture(bool bSave)
{
	int	StructSize;
	int	Drive;
	int	Track , Side;
	int	TrackSize;
	Uint8	*p;

#if 0		// TODO
	if ( bSave )					/* Saving snapshot */
	{
		StructSize = sizeof ( IPF_State );	/* 0 if HAVE_CAPSIMAGE is not defined */
		MemorySnapShot_Store(&StructSize, sizeof(StructSize));
		if ( StructSize > 0 )
		{
			MemorySnapShot_Store(&IPF_State, sizeof(IPF_State));

			/* Save the content of IPF_RawStreamImage[] */
			for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
				for ( Track=0 ; Track<IPF_MAX_TRACK_RAW_STREAM_IMAGE ; Track++ )
					for ( Side=0 ; Side<IPF_MAX_SIDE_RAW_STREAM_IMAGE ; Side++ )
					{
						TrackSize = IPF_RawStreamImage[ Drive ][ Track ][Side].TrackSize;
//						fprintf ( stderr , "IPF : save raw stream drive=%d track=%d side=%d : %d\n" , Drive , Track , Side , TrackSize );
						MemorySnapShot_Store(&TrackSize, sizeof(TrackSize));
						if ( TrackSize > 0 )
							MemorySnapShot_Store(IPF_RawStreamImage[ Drive ][ Track ][Side].TrackData, TrackSize);
					}
		}
	}

	else						/* Restoring snapshot */
	{
		MemorySnapShot_Store(&StructSize, sizeof(StructSize));
		if ( ( StructSize == 0 ) && ( sizeof ( IPF_State ) > 0 ) )
		{
			Log_AlertDlg(LOG_ERROR, "Hatari built with IPF floppy support, but no IPF data in memory snapshot -> skip");
			return;				/* Continue restoring the rest of the memory snapshot */
		}
		else if ( ( StructSize > 0 ) && ( sizeof ( IPF_State ) == 0 ) )
		{
			Log_AlertDlg(LOG_ERROR, "Memory snapshot with IPF floppy data, but Hatari built without IPF support -> skip");
			MemorySnapShot_Skip( StructSize );	/* Ignore the IPF data */
			return;				/* Continue restoring the rest of the memory snapshot */
		}
		else if ( ( StructSize > 0 ) && ( StructSize != sizeof ( IPF_State ) ) )
		{
			Log_AlertDlg(LOG_ERROR, "Memory snapshot IPF floppy data incompatible with this Hatari version -> skip");
			MemorySnapShot_Skip( StructSize );	/* Ignore the IPF data */
			return;				/* Continue restoring the rest of the memory snapshot */
		}

		if ( StructSize > 0 )
		{
			MemorySnapShot_Store(&IPF_State, sizeof(IPF_State));

#ifdef HAVE_CAPSIMAGE
			/* For IPF structures, we need to update some pointers in Fdc/Drive/CapsImage */
			/* drive : PUBYTE trackbuf, PUDWORD timebuf */
			/* fdc : PCAPSDRIVE driveprc, PCAPSDRIVE drive, CAPSFDCHOOK callback functions */
			IPF_State.Fdc.drive = IPF_State.Drive;		/* Connect drives array to the FDC */
			if ( IPF_State.Fdc.driveprc != NULL )		/* Recompute active drive's pointer */
				IPF_State.Fdc.driveprc = IPF_State.Fdc.drive + IPF_State.Fdc.driveact;

			CAPSFdcInvalidateTrack ( &IPF_State.Fdc , 0 );	/* Invalidate buffered track data for drive 0 */
			CAPSFdcInvalidateTrack ( &IPF_State.Fdc , 1 );	/* Invalidate buffered track data for drive 1 */

			/* Set callback functions */
			IPF_State.Fdc.cbirq = IPF_CallBack_Irq;
			IPF_State.Fdc.cbdrq = IPF_CallBack_Drq;
			IPF_State.Fdc.cbtrk = IPF_CallBack_Trk;
#endif

			/* Call IPF_Insert to recompute IPF_State.CapsImage[ Drive ] */
			for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
				if ( EmulationDrives[Drive].ImageType == FLOPPY_IMAGE_TYPE_IPF )
					if ( IPF_Insert ( Drive , EmulationDrives[Drive].pBuffer , EmulationDrives[Drive].nImageBytes ) == false )
					{
						Log_AlertDlg(LOG_ERROR, "Error restoring IPF image %s in drive %d" ,
							EmulationDrives[Drive].sFileName , Drive );
						return;
					}

			/* Restore the content of IPF_RawStreamImage[] */
			/* NOTE  : IPF_Insert above might already have read the raw tracks from disk, */
			/* so we free all those tracks and read them again from the snapshot instead */
			/* (not very efficient, but it's a rare case anyway) */
			for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
			{
				IPF_Eject_RawStreamImage ( Drive );
				for ( Track=0 ; Track<IPF_MAX_TRACK_RAW_STREAM_IMAGE ; Track++ )
					for ( Side=0 ; Side<IPF_MAX_SIDE_RAW_STREAM_IMAGE ; Side++ )
					{
						MemorySnapShot_Store(&TrackSize, sizeof(TrackSize));
//						fprintf ( stderr , "IPF : restore raw stream drive=%d track=%d side=%d : %d\n" , Drive , Track , Side , TrackSize );
						IPF_RawStreamImage[ Drive ][ Track ][Side].TrackSize = TrackSize;
						IPF_RawStreamImage[ Drive ][ Track ][Side].TrackData = NULL;
						if ( TrackSize > 0 )
						{
							p = malloc ( TrackSize );
							if ( p == NULL )
							{
								Log_AlertDlg(LOG_ERROR, "Error restoring IPF raw track drive %d track %d side %d size %d" ,
									Drive, Track, Side , TrackSize );
								return;
							}
							MemorySnapShot_Store(p, TrackSize);
							IPF_RawStreamImage[ Drive ][ Track ][Side].TrackData = p;
						}
					}
			}
			Log_Printf ( LOG_DEBUG , "ipf load ok\n" );
		}
	}
#endif	// TODO
}




/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a .SCP extension ? If so, return true.
 */
bool SCP_FileNameIsSCP(const char *pszFileName, bool bAllowGZ)
{
	return ( File_DoesFileExtensionMatch(pszFileName,".scp" )
		|| ( bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".scp.gz") )
		);
}


/*-----------------------------------------------------------------------*/
/**
 * Load .SCP file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
Uint8 *SCP_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType)
{
	Uint8 *pFile;

	*pImageSize = 0;

	/* Just load directly a buffer, and set ImageSize accordingly */
	pFile = File_Read(pszFileName, pImageSize, NULL);
	if (!pFile)
	{
		*pImageSize = 0;
		return NULL;
	}
	
	*pImageType = FLOPPY_IMAGE_TYPE_SCP;
	return pFile;
}


/*-----------------------------------------------------------------------*/
/**
 * Save .SCP file from memory buffer. Returns true is all OK.
 */
bool SCP_WriteDisk(int Drive, const char *pszFileName, Uint8 *pBuffer, int ImageSize)
{
	/* saving is not supported for SCP files */
	return false;
}




/*
 * Init the FDC and the drives used to handle SCP images
 */
bool	SCP_Init ( void )
{
	int	i;

	for ( i=0 ; i<MAX_FLOPPYDRIVES ; i++ )
	{
		SCP_State.ImageBuffer[ i ] = NULL;

	}

	return true;
}



/*-----------------------------------------------------------------------*/
/*
 * Init the resources to handle the SCP image inserted into a drive (0=A: 1=B:)
 */
bool	SCP_Insert ( int Drive , const char *FilenameSTX , Uint8 *pImageBuffer , long ImageSize )
{
	/* Process the current SCP image */
	if ( SCP_Insert_internal ( Drive , FilenameSTX , pImageBuffer , ImageSize ) == false )
		return false;


	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Init the resources to handle the SCP image inserted into a drive (0=A: 1=B:)
 * This function is used when restoring a memory snapshot and does not load
 * an optional ".wd1772" save file (the saved data are already in the memory
 * snapshot)
 */
static bool	SCP_Insert_internal ( int Drive , const char *FilenameSCP , Uint8 *pImageBuffer , long ImageSize )
{
	Log_Printf ( LOG_DEBUG , "SCP : SCP_Insert_internal drive=%d file=%s buf=%p size=%ld\n" , Drive , FilenameSCP , pImageBuffer , ImageSize );

	SCP_State.ImageBuffer[ Drive ] = SCP_BuildStruct ( pImageBuffer , SCP_DEBUG_FLAG );
	if ( SCP_State.ImageBuffer[ Drive ] == NULL )
	{
		Log_Printf ( LOG_ERROR , "SCP : SCP_Insert_internal drive=%d file=%s buf=%p size=%ld, error in SCP_BuildStruct\n" , Drive , FilenameSCP , pImageBuffer , ImageSize );
		return false;
	}

	return true;
}




/*-----------------------------------------------------------------------*/
/*
 * When ejecting a disk, free the resources associated with an SCP image
 */
bool	SCP_Eject ( int Drive )
{
	Log_Printf ( LOG_DEBUG , "SCP : SCP_Eject drive=%d\n" , Drive );

	if ( SCP_State.ImageBuffer[ Drive ] )
	{
		SCP_FreeStruct ( SCP_State.ImageBuffer[ Drive ] );
		SCP_State.ImageBuffer[ Drive ] = NULL;
	}

	return true;
}




/*-----------------------------------------------------------------------*/
/**
 * Free all the memory allocated to store an SCP file
 */
static void	SCP_FreeStruct ( SCP_MAIN_STRUCT *pScpMain )
{
	int			Track;

	if ( !pScpMain )
		return;

	for ( Track = 0 ; Track <= pScpMain->EndTrack ; Track++ )
	{
		if ( pScpMain->pTracks[ Track ].FileOffset != 0 )
			free ( pScpMain->pTracks[ Track ].pTrackRevs );
	}

	free ( pScpMain->pTracks );
	free ( pScpMain );
}



/*-----------------------------------------------------------------------*/
/**
 * Parse an SCP file.
 * The file is in pFileBuffer and we dynamically allocate memory to store
 * the components
 * Some internal variables/pointers are also computed, to speed up
 * data access when the FDC emulates an SCP file.
 */
SCP_MAIN_STRUCT	*SCP_BuildStruct ( Uint8 *pFileBuffer , int Debug )
{
	SCP_MAIN_STRUCT		*pScpMain;
	SCP_TRACK_STRUCT	*pScpTracks;
	SCP_TRACK_REV_STRUCT	*pScpTrackRevs;


	Uint8			*p;
	int			Track;
	Uint8			*pTrack;
	Uint32			Track_offset;
	int			Rev;



	pScpMain = calloc ( 1 , sizeof ( SCP_MAIN_STRUCT ) );
	if ( !pScpMain )
		return NULL;

	p = pFileBuffer;

	/* Read file's header */
	memcpy ( pScpMain->FileID , p , SCP_HEADER_ID_LEN ); p += SCP_HEADER_ID_LEN;
	pScpMain->Version		=	*p++;
	pScpMain->DiskType		=	*p++;
	pScpMain->RevolutionsNbr	=	*p++;
	pScpMain->StartTrack		=	*p++;
	pScpMain->EndTrack		=	*p++;
	pScpMain->Flags			=	*p++;
	pScpMain->CellTimeBits		=	*p++;
	pScpMain->HeadsNbr		=	*p++;
	pScpMain->CaptureRes		=	*p++;
	pScpMain->CRC			=	Mem_ReadU32_LE ( p ); p += 4;


	if ( Debug & SCP_DEBUG_FLAG_STRUCTURE )
		fprintf ( stderr , "SCP header ID='%.3s' Version=0x%2.2x DiskType=0x%2.2x RevolutionsNbr=%d"
			" StartTrack=0x%2.2x EndTrack=0x%2.2x Flags=0x%2.2x CellTimeBits=%d HeadsNbr=%d CaptureRes=%d"
			" CRC=0x%8.8x\n" , pScpMain->FileID , pScpMain->Version , pScpMain->DiskType ,
			pScpMain->RevolutionsNbr , pScpMain->StartTrack , pScpMain->EndTrack , pScpMain->Flags ,
			pScpMain->CellTimeBits , pScpMain->HeadsNbr , pScpMain->CaptureRes , pScpMain->CRC );

	pScpMain->WarnedWriteSector = false;
	pScpMain->WarnedWriteTrack = false;

	pScpTracks = calloc ( pScpMain->EndTrack + 1 , sizeof ( SCP_TRACK_STRUCT ) );
	if ( !pScpTracks )
	{
		SCP_FreeStruct ( pScpMain );
		return NULL;
	}
	pScpMain->pTracks = pScpTracks;

	/* Parse the tracks table */
	/* Even entries are for bottom side (side=0) ; odd entries are for top side (side=1) */
	/* NOTE : the doc is not very clear if only 1 side is imaged ; are there still 168 entries max */
	/* (with half of them being '0' or just 84 ? Assume it's 168 for now) */
 	for ( Track = 0 ; Track <= pScpMain->EndTrack ; Track++ )
	{
		Track_offset = Mem_ReadU32_LE ( p ); p += 4;

		if ( Track_offset == 0 )			/* No flux data for this track */
		{
			if ( Debug & SCP_DEBUG_FLAG_STRUCTURE )
				fprintf ( stderr , "    Track_scp=0x%2.2x no data (tr %2.2d side %d)\n" ,
					Track , Track / 2 , Track & 1 );
			pScpTracks[ Track ].FileOffset = 0;	/* ignore this track */
			continue;
		}

		pScpTracks[ Track ].FileOffset = Track_offset;
		pTrack = pFileBuffer + Track_offset;		/* point to the start of the track block */

		memcpy ( pScpTracks[ Track ].TrackId , pTrack , SCP_HEADER_ID_LEN ); pTrack += SCP_TRACK_HEADER_ID_LEN;
		pScpTracks[ Track ].TrackNumber	= *pTrack++;

		if ( Debug & SCP_DEBUG_FLAG_STRUCTURE )
			fprintf ( stderr , "    Track_scp=0x%2.2x Offset=0x%8.8x ID='%.3s' TrackNumber=0x%2.2x (tr %2.2d side %d)\n" ,
				Track , pScpTracks[ Track ].FileOffset , pScpTracks[ Track ].TrackId , pScpTracks[ Track ].TrackNumber ,
				Track / 2 , Track & 1 );

		if ( Track != pScpTracks[ Track ].TrackNumber )
		{
			fprintf ( stderr , "Entry for Track=%d has different track number %d\n" , Track , pScpTracks[ Track ].TrackNumber );
			SCP_FreeStruct ( pScpMain );
			return NULL;
		}

		pScpTrackRevs = calloc ( pScpMain->RevolutionsNbr , sizeof ( SCP_TRACK_REV_STRUCT ) );
		if ( !pScpTrackRevs )
		{
			SCP_FreeStruct ( pScpMain );
			return NULL;
		}
		pScpTracks[ Track ].pTrackRevs = pScpTrackRevs;

		for ( Rev = 0 ; Rev < pScpMain->RevolutionsNbr ; Rev++ )
		{
			pScpTrackRevs[ Rev ].Duration_ns	=	Mem_ReadU32_LE ( pTrack ); pTrack += 4;
			pScpTrackRevs[ Rev ].FluxNbr		=	Mem_ReadU32_LE ( pTrack ); pTrack += 4;
			pScpTrackRevs[ Rev ].DataOffset		=	Mem_ReadU32_LE ( pTrack ); pTrack += 4;

			if ( Debug & SCP_DEBUG_FLAG_STRUCTURE )
				fprintf ( stderr , "         Rev=%d Duration_ns=0x%x Flux_nbr=0x%x DataOffset=0x%x\n" ,
					Rev , pScpTrackRevs[ Rev ].Duration_ns , pScpTrackRevs[ Rev ].FluxNbr ,
					pScpTrackRevs[ Rev ].DataOffset );
		}

	}


	return pScpMain;
}


