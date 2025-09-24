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
#include "fdc.h"
#include "floppy.h"
#include "floppy_scp.h"
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




SCP_STRUCT	SCP_State;			/* All variables related to the SCP support */






/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

static bool	SCP_Insert_internal ( int Drive , const char *FilenameSTX , uint8_t *pImageBuffer , long ImageSize );
static void	SCP_FreeStruct ( SCP_MAIN_STRUCT *pScpMain );

static int	scp_select_track (struct fd_stream *s, unsigned int tracknr);
static void	scp_reset (struct fd_stream *s);
static int	scp_next_flux (struct fd_stream *s);



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void SCP_MemorySnapShot_Capture(bool bSave)
{
#if 0		// TODO
	int	StructSize;
	int	Drive;
	int	Track , Side;
	int	TrackSize;
	uint8_t	*p;

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
uint8_t *SCP_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType)
{
	uint8_t *pFile;

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
bool SCP_WriteDisk(int Drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize)
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
		SCP_State.ImageStruct[ i ] = NULL;

	}

	return true;
}



/*-----------------------------------------------------------------------*/
/*
 * Init the resources to handle the SCP image inserted into a drive (0=A: 1=B:)
 */
bool	SCP_Insert ( int Drive , const char *FilenameSTX , uint8_t *pImageBuffer , long ImageSize )
{
	/* Process the current SCP image */
	if ( SCP_Insert_internal ( Drive , FilenameSTX , pImageBuffer , ImageSize ) == false )
		return false;

//SCP_LoadTrack ( 0,0,0);	// boot
//SCP_LoadTrack ( 0,2,0);		// overlapping syncs

	return true;
}


/*-----------------------------------------------------------------------*/
/*
 * Init the resources to handle the SCP image inserted into a drive (0=A: 1=B:)
 */
static bool	SCP_Insert_internal ( int Drive , const char *FilenameSCP , uint8_t *pImageBuffer , long ImageSize )
{
	Log_Printf ( LOG_DEBUG , "SCP : SCP_Insert_internal drive=%d file=%s buf=%p size=%ld\n" , Drive , FilenameSCP , pImageBuffer , ImageSize );

	SCP_State.ImageStruct[ Drive ] = SCP_BuildStruct ( pImageBuffer , SCP_DEBUG_FLAG );
	if ( SCP_State.ImageStruct[ Drive ] == NULL )
	{
		Log_Printf ( LOG_ERROR , "SCP : SCP_Insert_internal drive=%d file=%s buf=%p size=%ld, error in SCP_BuildStruct\n" , Drive , FilenameSCP , pImageBuffer , ImageSize );
		return false;
	}

	/* Init the flux decoder for an SCP stream */
	SCP_State.SCP_Stream_Type[ Drive ].select_track = scp_select_track;
	SCP_State.SCP_Stream_Type[ Drive ].reset = scp_reset;
	SCP_State.SCP_Stream_Type[ Drive ].next_flux = scp_next_flux;
	SCP_State.SCP_Stream_Type[ Drive ].flux_struct_param = &(SCP_State.SCP_Stream[ Drive ]);

	fd_stream_setup ( &(SCP_State.SCP_Stream[ Drive ].s) , &(SCP_State.SCP_Stream_Type[ Drive ]) , 300 , 300 );
	fd_stream_reset ( &(SCP_State.SCP_Stream[ Drive ].s) );

	SCP_State.SCP_Stream[ Drive ].Drive = Drive;
	SCP_State.SCP_Stream[ Drive ].track = -1;		/* no track loaded */
	SCP_State.SCP_Stream[ Drive ].dat = NULL;		/* no track loaded */
	SCP_State.SCP_Stream[ Drive ].revs = SCP_State.ImageStruct[ Drive ]->RevolutionsNbr;
	SCP_State.SCP_Stream[ Drive ].index_cued = !!(SCP_State.ImageStruct[ Drive ]->Flags & (1u<<0)) || (SCP_State.SCP_Stream[ Drive ].revs == 1);
	if ( !SCP_State.SCP_Stream[ Drive ].index_cued )
		SCP_State.SCP_Stream[ Drive ].revs--;

	SCP_State.SCP_Stream[ Drive ].index_off = malloc ( SCP_State.ImageStruct[ Drive ]->RevolutionsNbr * sizeof ( unsigned int ) );
	if ( SCP_State.SCP_Stream[ Drive ].index_off == NULL )
	{
		Log_Printf ( LOG_ERROR , "SCP : SCP_Insert_internal drive=%d file=%s buf=%p size=%ld, malloc error\n" , Drive , FilenameSCP , pImageBuffer , ImageSize );
		SCP_FreeStruct ( SCP_State.ImageStruct[ Drive ] );
		SCP_State.ImageStruct[ Drive ] = NULL;
		return false;
	}


	return true;
}



struct fd_stream	*SCP_Get_Fd_Stream ( uint8_t Drive )
{
	return &(SCP_State.SCP_Stream[ Drive ].s);
}




/*-----------------------------------------------------------------------*/
/*
 * When ejecting a disk, free the resources associated with an SCP image
 */
bool	SCP_Eject ( int Drive )
{
	Log_Printf ( LOG_DEBUG , "SCP : SCP_Eject drive=%d\n" , Drive );

	if ( SCP_State.ImageStruct[ Drive ] )
	{
		SCP_FreeStruct ( SCP_State.ImageStruct[ Drive ] );
		SCP_State.ImageStruct[ Drive ] = NULL;

		/* Free the stream's data */
		free ( SCP_State.SCP_Stream[ Drive ].index_off );
		if ( SCP_State.SCP_Stream[ Drive ].dat )
			free ( SCP_State.SCP_Stream[ Drive ].dat );
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
		if ( pScpMain->pTracks[ Track ].TrackHeaderOffset != 0 )
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
SCP_MAIN_STRUCT	*SCP_BuildStruct ( uint8_t *pFileBuffer , int Debug )
{
	SCP_MAIN_STRUCT		*pScpMain;
	SCP_TRACK_STRUCT	*pScpTracks;
	SCP_TRACK_REV_STRUCT	*pScpTrackRevs;


	uint8_t			*p;
	int			Track;
	uint8_t			*pTrack;
	uint32_t		Track_offset;
	int			Rev;
	bool			error;


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
	pScpMain->CellTimeWidth		=	*p++;
	pScpMain->HeadsNbr		=	*p++;
	pScpMain->CaptureRes		=	*p++;
	pScpMain->CRC			=	Mem_ReadU32_LE ( p ); p += 4;


	if ( Debug & SCP_DEBUG_FLAG_STRUCTURE )
		fprintf ( stderr , "SCP header ID='%.3s' Version=0x%2.2x DiskType=0x%2.2x RevolutionsNbr=%d"
			" StartTrack=0x%2.2x EndTrack=0x%2.2x Flags=0x%2.2x CellTimeWidth=%d HeadsNbr=%d CaptureRes=%d"
			" CRC=0x%8.8x\n" , pScpMain->FileID , pScpMain->Version , pScpMain->DiskType ,
			pScpMain->RevolutionsNbr , pScpMain->StartTrack , pScpMain->EndTrack , pScpMain->Flags ,
			pScpMain->CellTimeWidth , pScpMain->HeadsNbr , pScpMain->CaptureRes , pScpMain->CRC );


	/* Check that the SCP file is supported */
	error = false;
	if ( pScpMain->RevolutionsNbr == 0 )
		{ error = true ; fprintf ( stderr , "SCP file error : RevolutionsNbr=0\n" ); }

	if ( ( pScpMain->CellTimeWidth != 0 ) && ( pScpMain->CellTimeWidth != 16 ) )
		{ error = true ; fprintf ( stderr , "SCP file error : unsupported cell time width=%d\n" , pScpMain->CellTimeWidth ); }

	if ( ( pScpMain->Flags & SCP_FLAG_RPM ) != 0 )
		{ error = true ; fprintf ( stderr , "SCP file error : unsupported RPM!=300\n" ); }

	if ( error )
	{
		SCP_FreeStruct ( pScpMain );
		return NULL;
	}


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
			pScpTracks[ Track ].TrackHeaderOffset = 0;	/* ignore this track */
			pScpTracks[ Track ].pTrackHeader = NULL;
			continue;
		}

		pTrack = pFileBuffer + Track_offset;		/* point to the start of the track block */
		pScpTracks[ Track ].TrackHeaderOffset = Track_offset;
		pScpTracks[ Track ].pTrackHeader = pTrack;

		memcpy ( pScpTracks[ Track ].TrackId , pTrack , SCP_HEADER_ID_LEN ); pTrack += SCP_TRACK_HEADER_ID_LEN;
		pScpTracks[ Track ].TrackNumber	= *pTrack++;

		if ( Debug & SCP_DEBUG_FLAG_STRUCTURE )
			fprintf ( stderr , "    Track_scp=0x%2.2x Offset=0x%8.8x ID='%.3s' TrackNumber=0x%2.2x (tr %2.2d side %d)\n" ,
				Track , pScpTracks[ Track ].TrackHeaderOffset , pScpTracks[ Track ].TrackId , pScpTracks[ Track ].TrackNumber ,
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





/*-----------------------------------------------------------------------*/
/*
 * Flux to MFM bit decoding - Support for SCP disk image - BEGIN
 * based on code by Keir Fraser https://github.com/keirf/Disk-Utilities  
 */

static int scp_select_track (struct fd_stream *s, unsigned int tracknr)
{
	struct scp_stream *scss = s->type->flux_struct_param;
	unsigned int rev;
	SCP_TRACK_STRUCT	*pScpTracks;
	SCP_TRACK_REV_STRUCT	*pScpTrackRevs;

	if (scss->dat && (scss->track == tracknr))
	    return 0;

	pScpTracks = &(SCP_State.ImageStruct[ scss->Drive ]->pTracks[ tracknr ]);
	pScpTrackRevs = pScpTracks->pTrackRevs;
	if ( pScpTracks->TrackHeaderOffset == 0 )		/* No flux data for this track */
		return -1;

	free(scss->dat);
	scss->dat = NULL;
	scss->datsz = 0;

	if (!scss->index_cued)
	{
		/* Skip first partial revolution. */
		pScpTrackRevs++;
	}

	scss->total_ticks = 0;
	for (rev = 0 ; rev < scss->revs ; rev++)
	{
		scss->index_off[rev] = pScpTrackRevs[ rev ].FluxNbr;
		scss->total_ticks += pScpTrackRevs[ rev ].Duration_ns;
		scss->datsz += scss->index_off[rev];
	}

	scss->dat = malloc(scss->datsz * sizeof(scss->dat[0]));
	if (scss->dat==NULL)
		return -1;
	scss->datsz = 0;

	for (rev = 0 ; rev < scss->revs ; rev++)
	{
		memcpy(&scss->dat[scss->datsz], pScpTracks->pTrackHeader + pScpTrackRevs[ rev ].DataOffset ,
		      scss->index_off[rev] * sizeof(scss->dat[0]));
		scss->datsz += scss->index_off[rev];
		scss->index_off[rev] = scss->datsz;
	}

	scss->track = tracknr;

	/* [NP] Original libdisk limits to revs + 1. Why ? This will not work if the same track */
	/* is read more than "revs+1" times. */
	/* We use ~0u instead (UINT_MAX) to allow "unlimited" reads of the same track */
//	s->max_revolutions = scss->revs + 1;
	s->max_revolutions = ~0u;
	s->RevolutionsNbr = SCP_State.ImageStruct[ scss->Drive ]->RevolutionsNbr;
	return 0;
}


static void scp_reset (struct fd_stream *s)
{
	struct scp_stream *scss = s->type->flux_struct_param;

	scss->jitter = 0;
	scss->dat_idx = 0;
	scss->index_pos = 0;
	scss->acc_ticks = 0;
}


static int scp_next_flux (struct fd_stream *s)
{
	struct scp_stream *scss = s->type->flux_struct_param;
	uint32_t val = 0, t;
	unsigned int nr_index_seen = 0;

	for (;;) {
		if (scss->dat_idx >= scss->index_pos)
		{
			uint32_t rev = s->nr_index % scss->revs;
			if ((rev == 0) && (scss->index_pos != 0))
			{
				/* We are wrapping back to the start of the dump. Unless a flux
				* reversal sits exactly on the index we have some time to
				* donate to the first reversal of the first revolution. */
				val = scss->total_ticks - scss->acc_ticks;
				scss->acc_ticks = -val;
			}
			scss->index_pos = scss->index_off[rev];
			if (rev == 0)
				scss->dat_idx = 0;
			s->ns_to_index = s->flux;
			/* Some drives return no flux transitions for tracks >= 160.
			* Bail if we see no flux transitions in a complete revolution. */
			if (nr_index_seen++)
				break;
		}

//printf ( "idx %04x %04x\n" , scss->dat_idx , be16toh(scss->dat[scss->dat_idx]) );
		t = be16toh(scss->dat[scss->dat_idx++]);

		if (t == 0)	/* overflow */
		{
			val += 0x10000;
			continue;
		}

		val += t;
		break;
	}

	scss->acc_ticks += val;

	/* If we are replaying a single revolution then jitter it a little to
	* trigger weak-bit variations. */
	if (scss->revs == 1)
	{
		int32_t jitter = fd_stream_rnd16(&s->prng_seed) & 3;
		if ((scss->jitter >= 4) || (scss->jitter <= -4))
		{
			/* Already accumulated significant jitter; adjust for it. */
			jitter = scss->jitter / 2;
		}
		else if (jitter & 1)
		{
			/* Add one bit of jitter. */
			jitter >>= 1;
		}
		else
		{
			/* Subtract one bit of jitter. */
			jitter >>= 1;
			jitter = -jitter;
		}
		scss->jitter -= jitter;
		val += jitter;
	}

	val = (uint64_t)val * SCK_NS_PER_TICK;

	/* If we are replaying a single revolution then randomly ignore 
	* very short pulses (<1us). */
	if ((scss->revs == 1) && (val < 1000) && (fd_stream_rnd16(&s->prng_seed) & 1))
	{
		scss->jitter += val;
		val = 0;
	}

	s->flux += val;
	return 0;
}

/*
 * Flux to MFM bit decoding - Support for SCP disk image - END
 */





int	FDC_GetBytesPerTrack_SCP ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	return 6268;			// TODO see FDC_TRACK_BYTES_STANDARD
}



uint32_t	FDC_GetCyclesPerRev_FdcCycles_SCP ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	int			TrackSize;

	TrackSize = FDC_GetBytesPerTrack_SCP ( Drive , Track , Side );

	// TODO : use a common function FDC_GetCyclesPerRev_FdcCycles ( ... , tracksize )
//	return TrackSize * FDC_DELAY_CYCLE_MFM_BYTE / FDC_GetFloppyDensity ( Drive );	/* Take density into account for HD/ED floppies */;
	return TrackSize * 256 / FDC_GetFloppyDensity ( Drive );	/* Take density into account for HD/ED floppies */;
}



