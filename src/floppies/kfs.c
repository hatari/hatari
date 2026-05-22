/*
  Hatari - floppy_kfs.c

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  KFS (KryoFlux Stream raw) disk image support.

  KFS files contain low level flux transitions as directly read from the floppy drive.
  These flux transitions will be converted to MFM bits and the resulting MFM buffer
  will be processed by emulating the WD1772 internal work.

  */
const char floppy_kfs_fileid[] = "Hatari floppy_kfs.c";

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "fdc.h"
#include "floppies/kfs.h"
#include "log.h"
#include "memorySnapShot.h"
#include "screen.h"
#include "video.h"
#include "m68000.h"
#include "cycles.h"
#include "utils.h"
#include "str.h"



static KFS_STRUCT		KFS_State;




/*--------------------------------------------------------------*/
/* Local functions prototypes					*/
/*--------------------------------------------------------------*/

//static bool	SCP_Insert_internal ( int Drive , const char *FilenameSCP , uint8_t *pImageBuffer , long ImageSize , bool KeepState );
//static void	SCP_FreeStruct ( SCP_MAIN_STRUCT *pScpMain );
static char	*KFS_FilenameFindTrackSide (char *FileName);



static unsigned int *kfs_decode_index(unsigned char *dat, unsigned int datsz);

static int	kfs_select_track(struct mfm_stream *s, unsigned int tracknr);
static void	kfs_reset(struct mfm_stream *s);
static int	kfs_next_flux(struct mfm_stream *s);




/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables ('MemorySnapShot_Store' handles type)
 */
void	KFS_MemorySnapShot_Capture(bool bSave)
{
	int	Drive;
	int	Track , Side;
	int	TrackSize;
	uint8_t	*p;


	if ( bSave )					/* Saving snapshot */
	{
		for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
		{
			MemorySnapShot_Store(&KFS_State.KFS_Stream[ Drive ], sizeof(struct kfs_stream));

			/* Save the content of KFS_State.TracksImage[ Drive ] */
			for ( Track=0 ; Track<KF_MAX_TRACK_RAW_STREAM_IMAGE ; Track++ )
				for ( Side=0 ; Side<KF_MAX_SIDE_RAW_STREAM_IMAGE ; Side++ )
				{
					TrackSize = KFS_State.TracksImage[ Drive ][ Track ][Side].TrackSize;
//						fprintf ( stderr , "KFS : save raw stream drive=%d track=%d side=%d : %d\n" , Drive , Track , Side , TrackSize );
					MemorySnapShot_Store(&TrackSize, sizeof(TrackSize));
					if ( TrackSize > 0 )
						MemorySnapShot_Store(KFS_State.TracksImage[ Drive ][ Track ][Side].TrackData, TrackSize);
				}
		}
	}

	else						/* Restoring snapshot */
	{
		/* NOTE  : KFS_Insert might already have read the raw tracks from disk, */
		/* so we free all those tracks and read them again from the snapshot instead */
		/* (not very efficient, but it's a rare case anyway) */
		for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
		{
			MemorySnapShot_Store(&KFS_State.KFS_Stream[ Drive ], sizeof(struct kfs_stream));

			/* Restore the content of KFS_State.TracksImage[ Drive ] */
			KFS_Eject ( Drive );

			for ( Track=0 ; Track<KF_MAX_TRACK_RAW_STREAM_IMAGE ; Track++ )
				for ( Side=0 ; Side<KF_MAX_SIDE_RAW_STREAM_IMAGE ; Side++ )
				{
					MemorySnapShot_Store(&TrackSize, sizeof(TrackSize));
//						fprintf ( stderr , "KFS : restore raw stream drive=%d track=%d side=%d : %d\n" , Drive , Track , Side , TrackSize );
					KFS_State.TracksImage[ Drive ][ Track ][Side].TrackSize = TrackSize;
					KFS_State.TracksImage[ Drive ][ Track ][Side].TrackData = NULL;
					if ( TrackSize > 0 )
					{
						p = malloc ( TrackSize );
						if ( p == NULL )
						{
							Log_AlertDlg(LOG_ERROR, "Error restoring KFS raw track drive %d track %d side %d size %d" ,
								Drive, Track, Side , TrackSize );
							return;
						}
						MemorySnapShot_Store(p, TrackSize);
						KFS_State.TracksImage[ Drive ][ Track ][Side].TrackData = p;
					}
				}
		}
	}
}




/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a .RAW extension ? (Kryoflux stream)
 */
bool KFS_FileNameIsKFS ( const char *pszFileName, bool bAllowGZ )
{
	return ( File_DoesFileExtensionMatch(pszFileName,".raw" )
		|| ( bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".raw.gz") )
		);
}


/*-----------------------------------------------------------------------*/
/**
 * Load KFS .RAW file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
uint8_t *KFS_ReadDisk ( int Drive, const char *pszFileName, long *pImageSize, int *pImageType )
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
	
	*pImageType = FLOPPY_IMAGE_TYPE_KFS;
	return pFile;
}



/*-----------------------------------------------------------------------*/
/**
 * Save KFS .RAW file from memory buffer. Returns true is all OK.
 *  -> saving is not supported for .RAW files
 */
bool	KFS_WriteDisk ( int Drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize )
{
	return false;
}


/*
 * Return a pointer to the part "tt.s.raw" at the end of the filename
 * (there can be an extra suffix to ignore if the file is compressed).
 * If we found a string where "tt" and "s" are digits, then we return
 * a pointer to this string.
 * If not found, we return NULL
 */
static char *KFS_FilenameFindTrackSide (char *FileName)
{
	char	ext[] = ".raw";
	int	len;
	char	*p;

	len = strlen ( FileName );
	len -= strlen ( ext );

	while ( len >= 4 )				/* need at least 4 chars for "tt.s" */
	{
		if ( strncasecmp ( ext , FileName + len , strlen ( ext ) ) == 0 )
		{
			p = FileName + len - 4;
			if ( isdigit( p[0] ) && isdigit( p[1] )
			  && ( p[2] == '.' ) && isdigit( p[3] ) )
				return p;
		}

		len--;
	}

	return NULL;
}



/*-----------------------------------------------------------------------*/
/*
 * Init the resources to handle the KFS image inserted into a drive (0=A: 1=B:)
 */

/*
 * Load all the raw stream files for all tracks/sides of a dump.
 * We use the filename of the raw file in drive 'Drive' as a template
 * where we replace track and side with all the possible values.
 */
bool	KFS_Insert ( int Drive , const char *FilenameKFS , uint8_t *pImageBuffer , long ImageSize )
{
	int	Track , Side;
	char	TrackFileName[ FILENAME_MAX ];
	char	*TrackSide_pointer;
	char	TrackSide_buf[ 4 + 1 ];			/* "tt.s" + \0 */
	int	TrackCount_0 , TrackCount_1;
	uint8_t	*p;
	long	Size;


	/* Ensure the previous tracks are removed from memory */
	KFS_Eject ( Drive );


	/* Get the path+filename of the raw file that was inserted in 'Drive' */
	/* then parse it to find the part with track/side */
	strcpy ( TrackFileName , ConfigureParams.DiskImage.szDiskFileName[Drive] );

	TrackSide_pointer = KFS_FilenameFindTrackSide ( TrackFileName );
	if ( TrackSide_pointer == NULL )
	{
		Log_Printf ( LOG_ERROR , "KFS : error parsing track/side in raw filename\n" );
		return false;
	}

	/* We try to load all the tracks for all the sides */
	/* We ignore errors, as some tracks/side can really be missing from the image dump */
	TrackCount_0 = 0;
	TrackCount_1 = 0;
	for ( Track=0 ; Track<KF_MAX_TRACK_RAW_STREAM_IMAGE ; Track++ )
	{
		for ( Side=0 ; Side<KF_MAX_SIDE_RAW_STREAM_IMAGE ; Side++ )
		{
			sprintf ( TrackSide_buf , "%02d.%d" , Track , Side );
			memcpy ( TrackSide_pointer , TrackSide_buf , 4 );
			Log_Printf ( LOG_INFO , "KFS : insert raw stream drive=%d track=%d side=%d %s\n" , Drive , Track , Side , TrackFileName );

			p = File_Read ( TrackFileName , &Size , NULL);

			if ( p )
			{
				/* Basic check : raw track has "KryoFlux" string in the 1st 200 bytes in OOB "info" */
				if ( Str_FindInMem ( (const uint8_t *)"KryoFlux" , p , 200 ) == NULL )
				{
					Log_Printf ( LOG_ERROR , "KFS : no Kryoflux signature in raw stream drive=%d track=%d side=%d %s\n" , Drive , Track , Side , TrackFileName );
					free(p);
					continue;			/* ignore this raw file */
				}

				KFS_State.TracksImage[ Drive ][ Track ][Side].TrackData = p;
				KFS_State.TracksImage[ Drive ][ Track ][Side].TrackSize = Size;
				if ( Side==0 )		TrackCount_0++;
				else			TrackCount_1++;
			}
			else
			{
				Log_Printf ( LOG_INFO , "KFS : insert raw stream drive=%d track=%d side=%d %s -> not found\n" , Drive , Track , Side , TrackFileName );
				/* File not loaded : either this track really doesn't exist or there was a system error */
				KFS_State.TracksImage[ Drive ][ Track ][Side].TrackData = NULL;
				KFS_State.TracksImage[ Drive ][ Track ][Side].TrackSize = 0;
			}
		}
	}


	/* If we didn't load any track, there's a problem, we stop here */
	if ( TrackCount_0 + TrackCount_1 == 0 )
	{
		Log_Printf ( LOG_WARN , "KFS : error, no raw track file could be loaded for %s\n" , ConfigureParams.DiskImage.szDiskFileName[Drive] );
		/* Free all the tracks that were loaded so far */
		KFS_Eject ( Drive );
		return false;
	}



	mfm_stream_setup ( &(MFM_STREAMS[ Drive ]) , 300 , 300 );

	MFM_STREAMS[ Drive ].type.select_track = kfs_select_track;
	MFM_STREAMS[ Drive ].type.reset = kfs_reset;
	MFM_STREAMS[ Drive ].type.next_flux = kfs_next_flux;
	MFM_STREAMS[ Drive ].type.flux_struct_param = &(KFS_State.KFS_Stream[ Drive ]);

	mfm_stream_reset ( &(MFM_STREAMS[ Drive ]) );

	KFS_State.KFS_Stream[ Drive ].Drive = Drive;
	KFS_State.KFS_Stream[ Drive ].dat = NULL;		/* no track loaded with scp_select_track */
//TODO		KFS_State.KFS_Stream[ Drive ].revs = KFS_State.ImageStruct[ Drive ]->RevolutionsNbr;





	Log_Printf ( LOG_INFO , "KFS : insert raw stream drive=%d, loaded %d tracks for side 0 and %d tracks for side 1\n", Drive, TrackCount_0, TrackCount_1 );

	return true;
}



/*-----------------------------------------------------------------------*/
/*
 * When ejecting a RAW stream image we must free all the individual tracks
 */
bool	KFS_Eject ( int Drive )
{
	int	Track , Side;

	Log_Printf ( LOG_DEBUG , "KFS : KFS_Eject drive=%d\n" , Drive );

	for ( Track=0 ; Track<KF_MAX_TRACK_RAW_STREAM_IMAGE ; Track++ )
		for ( Side=0 ; Side<KF_MAX_SIDE_RAW_STREAM_IMAGE ; Side++ )
		{
			if ( KFS_State.TracksImage[ Drive ][ Track ][Side].TrackData != NULL )
			{
				Log_Printf ( LOG_DEBUG , "KFS : eject raw stream drive=%d track=%d side=%d\n" , Drive , Track , Side );
				free ( KFS_State.TracksImage[ Drive ][ Track ][Side].TrackData );
				KFS_State.TracksImage[ Drive ][ Track ][Side].TrackData = NULL;
				KFS_State.TracksImage[ Drive ][ Track ][Side].TrackSize = 0;
			}
		}

	return true;
}




/*-----------------------------------------------------------------------*/
/*
 * Flux to MFM bit decoding - Support for Kryoflux raw stream disk image - BEGIN
 * based on code by Keir Fraser https://github.com/keirf/Disk-Utilities
 */

#define MAX_INDEX 128

#define MCK_FREQ (((18432000 * 73) / 14) / 2)
#define SCK_FREQ (MCK_FREQ / 2)
#define ICK_FREQ (MCK_FREQ / 16)
#define SCK_PS_PER_TICK (1000000000/(SCK_FREQ/1000))


static unsigned int *kfs_decode_index(unsigned char *dat, unsigned int datsz)
{
	unsigned int i, idx_i = 0;
	unsigned int *idxs = malloc((MAX_INDEX+1) * sizeof(*idxs));

fprintf ( stderr , "kfs_decode_index\n" );

	for (i = 0; i < datsz; ) {
		switch (dat[i]) {
		case 0xd: /* oob */ {
			uint32_t pos;
			uint16_t sz = le16toh(*(uint16_t *)&dat[i+2]);
// TODO 		uint16_t sz = le_swap16(*(uint16_t *)&dat[i+2]);

			i += 4;
			pos = le32toh(*(uint32_t *)&dat[i+0]);
// TODO			pos = le_swap32(*(uint32_t *)&dat[i+0]);
			if (dat[i-3] == 2) { /* index */
				if (idx_i == MAX_INDEX)
				goto fail;
				idxs[idx_i++] = pos;
			}
			i += sz;
			break;
		}
		case 0xa: /* nop3 */
		case 0xc: /* value16 */
			i++;
		case 0x00 ... 0x07:
		case 0x9: /* nop2 */
			i++;
		case 0x8: /* nop1 */
		case 0xb: /* overflow16 */
		default: /* 1-byte sample */
			i++;
			break;
		}
	}

	idxs[idx_i] = ~0u;
	return idxs;

fail:
	free(idxs);
	return NULL;
}


static int kfs_select_track(struct mfm_stream *s, unsigned int tracknr)
{
	struct kfs_stream *kfss = s->type.flux_struct_param;

fprintf ( stderr , "kfs_select_track 1 tr=%d\n" , tracknr );

	if (kfss->dat && (kfss->track == tracknr))
		return 0;

fprintf ( stderr , "kfs_select_track 2\n" );

	if ( kfss->idxs )
		free(kfss->idxs);
	kfss->idxs = NULL;


	kfss->dat = KFS_State.TracksImage[ kfss->Drive ][ tracknr >> 1 ][ tracknr & 1 ].TrackData;
	kfss->datsz = KFS_State.TracksImage[ kfss->Drive ][ tracknr >> 1 ][ tracknr & 1 ].TrackSize;;

	if ( kfss->dat == NULL )
		return -1;					/* No flux data for this track */

	kfss->track = tracknr;

	kfss->idxs = kfs_decode_index(kfss->dat, kfss->datsz);
	if (kfss->idxs == NULL) {
		kfss->dat = NULL;
		return -1;
	}

	s->max_revolutions = ~0u;
	//    s->RevolutionsNbr = SCP_State.ImageStruct[ scss->Drive ]->RevolutionsNbr;	// TODO
	s->RevolutionsNbr = ~0u;
	return 0;
}


static void kfs_reset(struct mfm_stream *s)
{
	struct kfs_stream *kfss = s->type.flux_struct_param;

	kfss->dat_idx = kfss->stream_idx = 0;
	kfss->idx_i = 0;
}

static int kfs_next_flux(struct mfm_stream *s)
{
	struct kfs_stream *kfss = s->type.flux_struct_param;
	unsigned int i = kfss->dat_idx;
	unsigned char *dat = kfss->dat;
	uint32_t val = 0;
	bool done = 0;

	if (kfss->stream_idx >= kfss->idxs[kfss->idx_i]) {
		kfss->idx_i++;
		s->ns_to_index = s->flux;
	}

	while (!done && (i < kfss->datsz)) {
		switch (dat[i]) {
		case 0x00 ... 0x07: two_byte_sample:
		val += ((uint32_t)dat[i] << 8) + dat[i+1];
		i += 2; kfss->stream_idx += 2;
		done = 1;
		break;
		case 0x8: /* nop1 */
			i += 1; kfss->stream_idx += 1;
			break;
		case 0x9: /* nop2 */
			i += 2; kfss->stream_idx += 2;
			break;
		case 0xa: /* nop3 */
			i += 3; kfss->stream_idx += 3;
			break;
		case 0xb: /* overflow16 */
			val += 0x10000;
			i += 1; kfss->stream_idx += 1;
			break;
		case 0xc: /* value16 */
			i += 1; kfss->stream_idx += 1;
			goto two_byte_sample;
		case 0xd: /* oob */ {
			uint32_t pos;
			uint16_t sz = le16toh(*(uint16_t *)&dat[i+2]);
// TODO 		uint16_t sz = le_swap16(*(uint16_t *)&dat[i+2]);
			i += 4;
			pos = le32toh(*(uint32_t *)&dat[i+0]);
// TODO 		pos = le_swap32(*(uint32_t *)&dat[i+0]);
			switch (dat[i-3]) {
			case 0x1: /* stream read */
			case 0x3: /* stream end */
				if (pos != kfss->stream_idx)
					fprintf (stderr, "Out-of-sync during track read\n");
				break;
			case 0x2: /* index */
				break;
			case 0xd: /* eof */
				i = kfss->datsz;
				sz = 0;
				break;
			}
			i += sz;
			break;
		}
		default: /* 1-byte sample */
			val += dat[i];
			i += 1; kfss->stream_idx += 1;
			done = 1;
			break;
		}
	}

	kfss->dat_idx = i;

	if (!done)
		return -1;

	val = (val * (uint32_t)SCK_PS_PER_TICK) / 1000u;
//    val = (val * s->drive_rpm) / s->data_rpm;
	s->flux += val;
	return val;
}


/*
 * Flux to MFM bit decoding - Support for Kryoflux raw stream disk image - END
 */




int	FDC_GetBytesPerTrack_KFS ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	return 6268;			// TODO see FDC_TRACK_BYTES_STANDARD
}



uint32_t	FDC_GetCyclesPerRev_FdcCycles_KFS ( uint8_t Drive , uint8_t Track , uint8_t Side )
{
	int			TrackSize;

	TrackSize = FDC_GetBytesPerTrack_KFS ( Drive , Track , Side );

	// TODO : use a common function FDC_GetCyclesPerRev_FdcCycles ( ... , tracksize )
//	return TrackSize * FDC_DELAY_CYCLE_MFM_BYTE / FDC_GetFloppyDensity ( Drive );	/* Take density into account for HD/ED floppies */;
	return TrackSize * 256 / FDC_GetFloppyDensity ( Drive );	/* Take density into account for HD/ED floppies */;
}

