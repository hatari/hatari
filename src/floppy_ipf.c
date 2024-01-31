/*
  Hatari - floppy_ipf.c

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  IPF disk image support.

  IPF files are handled using the capsimage library, which emulates the FDC
  at low level and allows to read complex protections.

  RAW stream files made with KryoFlux board or CT RAW dumped with an Amiga are also handled
  by the capsimage library.
*/
const char floppy_ipf_fileid[] = "Hatari floppy_ipf.c";

#include "main.h"
#include "file.h"
#include "floppy.h"
#include "floppy_ipf.h"
#include "fdc.h"
#include "log.h"
#include "memorySnapShot.h"
#include "video.h"
#include "m68000.h"
#include "cycles.h"

#include <string.h>

#ifdef HAVE_CAPSIMAGE
#ifndef __cdecl
#define __cdecl  /* CAPS headers need this, but do not define it on their own */
#endif
#include <caps/CapsLibAll.h>
#define CapsLong SDWORD
#define CapsULong UDWORD
/* Macro to check release and revision */
#define	CAPS_LIB_REL_REV	( CAPS_LIB_RELEASE * 100 + CAPS_LIB_REVISION )
#endif


/* To handle RAW stream images with one file per track/side */
#define	IPF_MAX_TRACK_RAW_STREAM_IMAGE	84				/* track number can be 0 .. 83 */
#define	IPF_MAX_SIDE_RAW_STREAM_IMAGE	2				/* side number can be 0 or 1 */

struct
{
	int		TrackSize;
	uint8_t		*TrackData;
} IPF_RawStreamImage[ MAX_FLOPPYDRIVES ][ IPF_MAX_TRACK_RAW_STREAM_IMAGE ][ IPF_MAX_SIDE_RAW_STREAM_IMAGE ];



typedef struct
{
#ifdef HAVE_CAPSIMAGE
	uint32_t			CapsLibRelease;
	uint32_t			CapsLibRevision;

	struct CapsFdc		Fdc;				/* Fdc state */
	struct CapsDrive 	Drive[ MAX_FLOPPYDRIVES ];	/* Physical drives */
	CapsLong		CapsImage[ MAX_FLOPPYDRIVES ];	/* Image Id or -1 if drive empty */
	CapsLong		CapsImageType[ MAX_FLOPPYDRIVES ]; /* ImageType or -1 if not known */

	int			Rev_Track[ MAX_FLOPPYDRIVES ];	/* Needed to handle CAPSSetRevolution for type II/III commands */
	int			Rev_Side[ MAX_FLOPPYDRIVES ];

	bool			DriveEnabled[ MAX_FLOPPYDRIVES ];/* Is drive ON or OFF */
	bool			DoubleSided[ MAX_FLOPPYDRIVES ];/* Is drive double sided or not */
#endif

	int64_t			FdcClock;			/* Current value of CyclesGlobalClockCounter */
} IPF_STRUCT;


static IPF_STRUCT	IPF_State;			/* All variables related to the IPF support */


static bool	IPF_Eject_RawStreamImage ( int Drive );
#ifdef HAVE_CAPSIMAGE
static char	*IPF_FilenameFindTrackSide (char *FileName);
static bool	IPF_Insert_RawStreamImage ( int Drive );

static void	IPF_CallBack_Trk ( struct CapsFdc *pc , CapsULong State );
static void	IPF_CallBack_Irq ( struct CapsFdc *pc , CapsULong State );
static void	IPF_CallBack_Drq ( struct CapsFdc *pc , CapsULong State );
static void	IPF_Drive_Update_Enable_Side ( void );
static void	IPF_FDC_LogCommand ( uint8_t Command );
#endif




/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 * We must take care of whether Hatari was compiled with IPF support of not
 * when saving/restoring snapshots to avoid incompatibilities.
 */
void IPF_MemorySnapShot_Capture(bool bSave)
{
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
}




/*-----------------------------------------------------------------------*/
/**
 * Does filename end with a .IPF or .RAW or .CTR extension ? If so, return true.
 * .RAW and .CTR support requires caps lib >= 5.1
 */
bool IPF_FileNameIsIPF(const char *pszFileName, bool bAllowGZ)
{
	return ( File_DoesFileExtensionMatch(pszFileName,".ipf" )
		|| ( bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".ipf.gz") )
		|| File_DoesFileExtensionMatch(pszFileName,".raw" )
		|| ( bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".raw.gz") )
		|| File_DoesFileExtensionMatch(pszFileName,".ctr" )
		|| ( bAllowGZ && File_DoesFileExtensionMatch(pszFileName,".ctr.gz") )
		);
}


/*
 * Return a pointer to the part "tt.s.raw" at the end of the filename
 * (there can be an extra suffix to ignore if the file is compressed).
 * If we found a string where "tt" and "s" are digits, then we return
 * a pointer to this string.
 * If not found, we return NULL
 */
#ifdef HAVE_CAPSIMAGE
static char *IPF_FilenameFindTrackSide (char *FileName)
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
#endif


/*-----------------------------------------------------------------------*/
/**
 * Load .IPF file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
uint8_t *IPF_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType)
{
#ifndef HAVE_CAPSIMAGE
	Log_AlertDlg(LOG_ERROR, "Hatari built without IPF support -> can't handle floppy image");
	return NULL;

#else
	uint8_t *pIPFFile;

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
bool IPF_WriteDisk(int Drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize)
{
	/* saving is not supported for IPF files */
	return false;
}




/*
 * Init the FDC and the drives used to handle IPF images
 */
bool	IPF_Init ( void )
{
#ifndef HAVE_CAPSIMAGE
	return true;

#else
	int	i;
	struct CapsVersionInfo	caps_vi;

	Log_Printf ( LOG_DEBUG , "IPF : IPF_Init\n" );

	if ( CAPSInit() != imgeOk )
	{
		Log_Printf ( LOG_ERROR , "IPF : Could not initialize the capsimage library\n" );
		return false;
	}

	if ( CAPSGetVersionInfo ( &caps_vi , 0 ) != imgeOk )
	{
		Log_Printf ( LOG_ERROR , "IPF : CAPSVersionInfo failed\n" );
		return false;
	}
	Log_Printf ( LOG_INFO , "IPF : capsimage library version release=%d revision=%d\n" , (int)caps_vi.release , (int)caps_vi.revision );
	IPF_State.CapsLibRelease = caps_vi.release;
	IPF_State.CapsLibRevision = caps_vi.revision;

	/* Default values for each physical drive */
	memset ( IPF_State.Drive , 0 , sizeof ( IPF_State.Drive ) );
	for ( i=0 ; i < MAX_FLOPPYDRIVES ; i++ )
	{
		IPF_State.Drive[ i ].type = sizeof ( struct CapsDrive );
		IPF_State.Drive[ i ].rpm = CAPSDRIVE_35DD_RPM;
		IPF_State.Drive[ i ].maxtrack = CAPSDRIVE_35DD_HST;

		IPF_State.Rev_Track[ i ] = -1;
		IPF_State.Rev_Side[ i ] = -1;

		IPF_State.DriveEnabled[ i ] = true;
		IPF_State.DoubleSided[ i ] = true;

		IPF_State.CapsImageType[ i ] = -1;
	}

	/* Init FDC with 2 physical drives */
	memset ( &IPF_State.Fdc , 0 , sizeof ( IPF_State.Fdc ) );
	IPF_State.Fdc.type = sizeof( struct CapsFdc );
	IPF_State.Fdc.model = cfdcmWD1772;
	IPF_State.Fdc.drive = IPF_State.Drive;
	IPF_State.Fdc.drivecnt = MAX_FLOPPYDRIVES;

	if ( CAPSFdcInit ( &IPF_State.Fdc ) != imgeOk)
	{
		Log_Printf ( LOG_ERROR , "IPF : CAPSFdcInit failed\n" );
		return false;
	}

	/* 2 drives by default */
	IPF_State.Fdc.drivemax = MAX_FLOPPYDRIVES;
	/* Update drives' state in case we have some drives ON or OFF in config or parameters */
	IPF_Drive_Update_Enable_Side ();

	/* FDC clock */
	IPF_State.Fdc.clockfrq = 8000000;

	/* Set callback functions */
	IPF_State.Fdc.cbirq = IPF_CallBack_Irq;
	IPF_State.Fdc.cbdrq = IPF_CallBack_Drq;
	IPF_State.Fdc.cbtrk = IPF_CallBack_Trk;

	CAPSFdcReset ( &IPF_State.Fdc );

	return true;
#endif
}




/*
 * Exit
 */
void	IPF_Exit ( void )
{
#ifndef HAVE_CAPSIMAGE
#else
	CAPSExit();
#endif
}




/*
 * Init the resources to handle the IPF image inserted into a drive (0=A: 1=B:)
 */
bool	IPF_Insert ( int Drive , uint8_t *pImageBuffer , long ImageSize )
{
#ifndef HAVE_CAPSIMAGE
	return false;

#else
	CapsLong	ImageId;
	CapsLong	ImageType;
	const char	*ImageTypeStr;
	bool		Type_OK;


	ImageId = CAPSAddImage();
	if ( ImageId < 0 )
	{
		Log_Printf ( LOG_ERROR , "IPF : error CAPSAddImage\n" );
		return false;
	}

	ImageType = CAPSGetImageTypeMemory ( pImageBuffer , ImageSize );
	if ( ImageType == citError )
	{
		Log_Printf ( LOG_ERROR , "IPF : error CAPSGetImageTypeMemory\n" );
		CAPSRemImage ( ImageId ) ;
		return false;
	}
	else if ( ImageType == citUnknown )
	{
		Log_Printf ( LOG_ERROR , "IPF : unknown image type\n" );
		CAPSRemImage ( ImageId ) ;
		return false;
	}

	Type_OK = true;
	switch ( ImageType ) {
		case citIPF:		ImageTypeStr = "IPF"; break;
		case citCTRaw:		ImageTypeStr = "CT RAW"; break;
		case citKFStream:	ImageTypeStr = "KF STREAM" ; break;
		case citDraft:		ImageTypeStr = "DRAFT" ; break;
		default :		ImageTypeStr = "NOT SUPPORTED\n";
					Type_OK = false;
	}
	Log_Printf ( LOG_INFO , "IPF : IPF_Insert drive=%d buf=%p size=%ld imageid=%d type=%s\n" , Drive , pImageBuffer , ImageSize , ImageId , ImageTypeStr );

	if ( !Type_OK )
	{
		CAPSRemImage ( ImageId ) ;
		return false;
	}


	/* Special case for RAW stream image, we load all the tracks now */
	if ( ImageType == citKFStream )
	{
		if ( IPF_Insert_RawStreamImage ( Drive ) == false )
		{
			Log_Printf ( LOG_ERROR , "IPF : can't load raw stream files\n" );
			CAPSRemImage ( ImageId ) ;
			return false;
		}
	}


	if ( CAPSLockImageMemory ( ImageId , pImageBuffer , (CapsULong)ImageSize , DI_LOCK_MEMREF ) == imgeOk )
	{
		struct CapsImageInfo cii;
		int		i;

		/* Print some debug infos */
		if ( CAPSGetImageInfo ( &cii , ImageId ) == imgeOk )
		{
			Log_Printf ( LOG_INFO , "\tType: %d\n", (int)cii.type);
			Log_Printf ( LOG_INFO , "\tRelease: %d\n", (int)cii.release);
			Log_Printf ( LOG_INFO , "\tRevision: %d\n", (int)cii.revision);
			Log_Printf ( LOG_INFO , "\tMin Cylinder: %d\n", (int)cii.mincylinder);
			Log_Printf ( LOG_INFO , "\tMax Cylinder: %d\n", (int)cii.maxcylinder);
			Log_Printf ( LOG_INFO , "\tMin Head: %d\n", (int)cii.minhead);
			Log_Printf ( LOG_INFO , "\tMax Head: %d\n", (int)cii.maxhead);
			Log_Printf ( LOG_INFO , "\tCreation Date: %04d/%02d/%02d %02d:%02d:%02d.%03d\n", (int)cii.crdt.year, (int)cii.crdt.month, (int)cii.crdt.day, (int)cii.crdt.hour, (int)cii.crdt.min, (int)cii.crdt.sec, (int)cii.crdt.tick);
			Log_Printf ( LOG_INFO , "\tPlatforms:");
			for (i = 0; i < CAPS_MAXPLATFORM; i++)
				if (cii.platform[i] != ciipNA)
					Log_Printf ( LOG_INFO , " %s" , CAPSGetPlatformName(cii.platform[i]) );
			Log_Printf ( LOG_INFO , "\n");

			/* Some IPF disks are not correctly supported yet : display a warning */
			if ( (int)cii.release == 3222 ) 				/* Sundog */
				Log_AlertDlg ( LOG_INFO , "'Sundog' is not correctly supported yet, it requires write access." );
			else if ( (int)cii.release == 3058 ) 				/* Lethal Xcess */
				Log_AlertDlg ( LOG_INFO , "'Lethal Xcess' is not correctly supported yet, protection will fail" );
		}
	}
	else
	{
		CAPSRemImage ( ImageId ) ;
		return false;
	}

	if ( CAPSLoadImage ( ImageId , DI_LOCK_DENALT | DI_LOCK_DENVAR | DI_LOCK_UPDATEFD ) != imgeOk )
	{
		Log_Printf ( LOG_ERROR , "IPF : error CAPSLoadImage\n" );
		CAPSUnlockImage ( ImageId );
		CAPSRemImage ( ImageId ) ;
		return false;
	}

	
	IPF_State.CapsImage[ Drive ] = ImageId;
	IPF_State.CapsImageType[ Drive ] = ImageType;

	IPF_State.Drive[ Drive ].diskattr |= CAPSDRIVE_DA_IN;				/* Disk inserted, keep the value for "write protect" */

	CAPSFdcInvalidateTrack ( &IPF_State.Fdc , Drive );				/* Invalidate previous buffered track data for drive, if any */

	IPF_State.Rev_Track[ Drive ] = -1;						/* Invalidate previous track/side to handle revolution's count */
	IPF_State.Rev_Side[ Drive ] = -1;

	return true;
#endif
}


/*
 * Load all the raw stream files for all tracks/sides of a dump.
 * We use the filename of the raw file in drive 'Drive' as a template
 * where we replace track and side will all the possible values.
 */
#ifdef HAVE_CAPSIMAGE
static bool	IPF_Insert_RawStreamImage ( int Drive )
{
	int	Track , Side;
	char	TrackFileName[ FILENAME_MAX ];
	char	*TrackSide_pointer;
	char	TrackSide_buf[ 4 + 1 ];			/* "tt.s" + \0 */
	int	TrackCount;
	int	TrackCount_0 , TrackCount_1;
	uint8_t	*p;
	long	Size;


return true;						/* This function is not used for now, always return true */
	/* Ensure the previous tracks are removed from memory */
	IPF_Eject_RawStreamImage ( Drive );


	/* Get the path+filename of the raw file that was inserted in 'Drive' */
	/* then parse it to find the part with track/side */
	strcpy ( TrackFileName , ConfigureParams.DiskImage.szDiskFileName[Drive] );

	TrackSide_pointer = IPF_FilenameFindTrackSide ( TrackFileName );
	if ( TrackSide_pointer == NULL )
	{
		Log_Printf ( LOG_ERROR , "IPF : error parsing track/side in raw filename\n" );
		return false;
	}

	/* We try to load all the tracks for all the sides */
	/* We ignore errors, as some tracks/side can really be missing from the image dump */
	TrackCount = 0;
	TrackCount_0 = 0;
	TrackCount_1 = 0;
	for ( Track=0 ; Track<IPF_MAX_TRACK_RAW_STREAM_IMAGE ; Track++ )
	{
		for ( Side=0 ; Side<IPF_MAX_SIDE_RAW_STREAM_IMAGE ; Side++ )
		{
			sprintf ( TrackSide_buf , "%02d.%d" , Track , Side );
			memcpy ( TrackSide_pointer , TrackSide_buf , 4 );
			Log_Printf ( LOG_INFO , "IPF : insert raw stream drive=%d track=%d side=%d %s\n" , Drive , Track , Side , TrackFileName );

			p = File_Read ( TrackFileName , &Size , NULL);
			if ( p )
			{
				IPF_RawStreamImage[ Drive ][ Track ][Side].TrackData = p;
				IPF_RawStreamImage[ Drive ][ Track ][Side].TrackSize = Size;
				TrackCount++;
				if ( Side==0 )		TrackCount_0++;
				else			TrackCount_1++;
			}
			else
			{
				Log_Printf ( LOG_INFO , "IPF : insert raw stream drive=%d track=%d side=%d %s -> not found\n" , Drive , Track , Side , TrackFileName );
				/* File not loaded : either this track really doesn't exist or there was a system error */
				IPF_RawStreamImage[ Drive ][ Track ][Side].TrackData = NULL;
				IPF_RawStreamImage[ Drive ][ Track ][Side].TrackSize = 0;
			}
		}
	}


	/* If we didn't load any track, there's a problem, we stop here */
	if ( TrackCount == 0 )
	{
		Log_Printf ( LOG_WARN , "IPF : error, no raw track file could be loaded for %s\n" , ConfigureParams.DiskImage.szDiskFileName[Drive] );
		/* Free all the tracks that were loaded so far */
		IPF_Eject_RawStreamImage ( Drive );
		return false;
	}

	Log_Printf ( LOG_INFO , "IPF : insert raw stream drive=%d, loaded %d tracks for side 0 and %d tracks for side 1\n", Drive, TrackCount_0, TrackCount_1 );

	return true;
}
#endif



/*
 * When ejecting a disk, free the resources associated with an IPF image
 */
bool	IPF_Eject ( int Drive )
{
#ifndef HAVE_CAPSIMAGE
	return false;

#else
	Log_Printf ( LOG_DEBUG , "IPF : IPF_Eject drive=%d imageid=%d\n" , Drive , IPF_State.CapsImage[ Drive ] );

	CAPSFdcInvalidateTrack ( &IPF_State.Fdc , Drive );				/* Invalidate previous buffered track data for drive, if any */

	if ( CAPSUnlockImage ( IPF_State.CapsImage[ Drive ] ) < 0 )
	{
		Log_Printf ( LOG_ERROR , "IPF : error CAPSUnlockImage\n" );
		return false;
	}

	if ( CAPSRemImage ( IPF_State.CapsImage[ Drive ] ) < 0 )
	{
		Log_Printf ( LOG_ERROR , "IPF : error CAPSRemImage\n" );
		return false;
	}

	/* Special case for RAW stream image, we must free all the tracks */
	if ( IPF_State.CapsImageType[ Drive ] == citKFStream )
		IPF_Eject_RawStreamImage ( Drive );

	IPF_State.CapsImage[ Drive ] = -1;
	IPF_State.CapsImageType[ Drive ] = -1;

	IPF_State.Drive[ Drive ].diskattr &= ~CAPSDRIVE_DA_IN;

	return true;
#endif
}


/*
 * When ejecting a RAW stream image we must free all the individual tracks
 */
static bool	IPF_Eject_RawStreamImage ( int Drive )
{
#ifndef HAVE_CAPSIMAGE
	return true;

#else
	int	Track , Side;

return true;						/* This function is not used for now, always return true */
	for ( Track=0 ; Track<IPF_MAX_TRACK_RAW_STREAM_IMAGE ; Track++ )
		for ( Side=0 ; Side<IPF_MAX_SIDE_RAW_STREAM_IMAGE ; Side++ )
		{
			if ( IPF_RawStreamImage[ Drive ][ Track ][Side].TrackData != NULL )
			{
				Log_Printf ( LOG_DEBUG , "IPF : eject raw stream drive=%d track=%d side=%d\n" , Drive , Track , Side );
				free ( IPF_RawStreamImage[ Drive ][ Track ][Side].TrackData );
				IPF_RawStreamImage[ Drive ][ Track ][Side].TrackData = NULL;
				IPF_RawStreamImage[ Drive ][ Track ][Side].TrackSize = 0;
			}
		}

	return true;
#endif
}




/*
 * Reset FDC state when a reset signal was received
 *  - On cold reset, TR and DR should be reset
 *  - On warm reset, TR and DR value should be kept
 * Keeping TR and DR content is not supported in Caps library (bug), we handle it ourselves
 */
void IPF_Reset ( bool bCold )
{
#ifdef HAVE_CAPSIMAGE
	CapsULong	TR , DR;

	if ( !bCold )					/* Save TR and DR if warm reset */
	{
		TR = IPF_State.Fdc.r_track;
		DR = IPF_State.Fdc.r_data;
	}

	CAPSFdcReset ( &IPF_State.Fdc );		/* This always clear TR and DR, which is wrong */

	if ( !bCold )					/* Restore TR and DR if warm reset */
	{
		IPF_State.Fdc.r_track = TR;
		IPF_State.Fdc.r_data = DR;
	}

	IPF_State.FdcClock = CyclesGlobalClockCounter;
#endif
}




/*
 * Callback function used when track is changed.
 * We need to update the track data by calling CAPSLockTrack
 */
#ifdef HAVE_CAPSIMAGE
static void	IPF_CallBack_Trk ( struct CapsFdc *pc , CapsULong State )
{
	int	Drive = State;				/* State is the drive number in that case */
	struct CapsDrive *pd = pc->drive+Drive;		/* Current drive where the track change occurred */
	struct CapsTrackInfoT1 cti;

	cti.type=1;
	if ( CAPSLockTrack ( &cti , IPF_State.CapsImage[ Drive ] , pd->buftrack , pd->bufside ,
			DI_LOCK_DENALT|DI_LOCK_DENVAR|DI_LOCK_UPDATEFD|DI_LOCK_TYPE ) != imgeOk )
		return;

	LOG_TRACE(TRACE_FDC, "fdc ipf callback trk drive=%d buftrack=%d bufside=%d VBL=%d HBL=%d\n" , Drive ,
		  (int)pd->buftrack , (int)pd->bufside , nVBLs , nHBL );

	pd->ttype	= cti.type;
	pd->trackbuf	= cti.trackbuf;
	pd->timebuf	= cti.timebuf;
	pd->tracklen	= cti.tracklen;
	pd->overlap	= cti.overlap;
}
#endif




/*
 * Callback function used when the FDC change the IRQ signal
 */
#ifdef HAVE_CAPSIMAGE
static void	IPF_CallBack_Irq ( struct CapsFdc *pc , CapsULong State )
{
	LOG_TRACE(TRACE_FDC, "fdc ipf callback irq state=0x%x VBL=%d HBL=%d\n" , (int)State , nVBLs , nHBL );

	if ( State )
		FDC_SetIRQ ( FDC_IRQ_SOURCE_OTHER );	/* IRQ bit was set */
	else
		FDC_ClearIRQ ();			/* IRQ bit was reset */
}
#endif




/*
 * Callback function used when the FDC change the DRQ signal
 * -> copy the byte to/from the DMA's FIFO if it's a read or a write to the disk
 */
#ifdef HAVE_CAPSIMAGE
static void	IPF_CallBack_Drq ( struct CapsFdc *pc , CapsULong State )
{
	uint8_t	Byte;

	if ( State == 0 )
		return;					/* DRQ bit was reset, do nothing */

	if ( FDC_DMA_GetModeControl_R_WR () != 0 )	/* DMA write mode */
	{
		Byte = FDC_DMA_FIFO_Pull ();		/* Get a byte from the DMA FIFO */
		CAPSFdcWrite ( &IPF_State.Fdc , 3 , Byte );	/* Write to FDC's reg 3 */

		LOG_TRACE(TRACE_FDC, "fdc ipf callback drq state=0x%x write byte 0x%02x VBL=%d HBL=%d\n" , (int)State , Byte , nVBLs , nHBL );
	}

	else						/* DMA read mode */
	{
		Byte = CAPSFdcRead ( &IPF_State.Fdc , 3 );	/* Read from FDC's reg 3 */
		FDC_DMA_FIFO_Push ( Byte );		/* Add byte to the DMA FIFO */

		LOG_TRACE(TRACE_FDC, "fdc ipf callback drq state=0x%x read byte 0x%02x VBL=%d HBL=%d\n" , (int)State , Byte , nVBLs , nHBL );
	}
}
#endif



/*
 * This function is used to enable/disable a drive when
 * using the UI or command line parameters
 *
 * NOTE : for now, IPF only supports changing drive 1, drive 0
 * is always ON.
 */
void	IPF_Drive_Set_Enable ( int Drive , bool value )
{
#ifndef HAVE_CAPSIMAGE
	return;

#else
	IPF_State.DriveEnabled[ Drive ] = value;			/* Store the new state */

	IPF_Drive_Update_Enable_Side ();				/* Update IPF's internal state */
#endif
}


/*
 * This function is used to configure a drive as single sided
 * or double sided when using the UI or command line parameters
 */
void	IPF_Drive_Set_DoubleSided ( int Drive , bool value )
{
#ifndef HAVE_CAPSIMAGE
	return;

#else
	IPF_State.DoubleSided[ Drive ] = value;				/* Store the new state */

	IPF_Drive_Update_Enable_Side ();				/* Update IPF's internal state */
#endif
}


/*
 * Update IPF's internal state depending on which drives are ON or OFF
 * and if the drive is single or double sided (for capslib >= 5.1)
 */
#ifdef HAVE_CAPSIMAGE
static void	IPF_Drive_Update_Enable_Side ( void )
{
	int	i;

	if ( IPF_State.DriveEnabled[ 1 ] )
	        IPF_State.Fdc.drivemax = MAX_FLOPPYDRIVES;		/* Should be 2 */
	else
	        IPF_State.Fdc.drivemax = MAX_FLOPPYDRIVES - 1;		/* Should be 1 */

	for ( i=0 ; i < MAX_FLOPPYDRIVES ; i++ )
	{
		if ( IPF_State.DoubleSided[ i ] )
			IPF_State.Drive[ i ].diskattr &= ~CAPSDRIVE_DA_SS;	/* Double sided */
		else
			IPF_State.Drive[ i ].diskattr |= CAPSDRIVE_DA_SS;	/* Single sided */
	}
}
#endif


/*
 * Set the drive and the side to be used for the next FDC commands
 * io_porta_old is the previous value, io_porta_new is the new value
 * to take into account.
 * We report a side change only when a drive is selected.
 */
void	IPF_SetDriveSide ( uint8_t io_porta_old , uint8_t io_porta_new )
{
#ifndef HAVE_CAPSIMAGE
	return;

#else
	int	Side;

	LOG_TRACE(TRACE_FDC, "fdc ipf change drive/side io_porta_old=0x%x io_porta_new=0x%x VBL=%d HBL=%d\n" , io_porta_old , io_porta_new , nVBLs , nHBL );

	Side = ( (~io_porta_new) & 0x01 );		/* Side 0 or 1 */

	IPF_State.Fdc.drivenew = -1;			/* By default, don't select any drive */

	/* Check drive 1 first */
	if ( ( io_porta_new & 0x04 ) == 0 )
	{
		IPF_State.Drive[ 1 ].newside = Side;
		IPF_State.Fdc.drivenew = 1;		/* Select drive 1 */
	}

	/* If both drive 0 and drive 1 are enabled, we keep only drive 0 as newdrive */
	if ( ( io_porta_new & 0x02 ) == 0 )
	{
		IPF_State.Drive[ 0 ].newside = Side;
		IPF_State.Fdc.drivenew = 0;		/* Select drive 0 (and un-select drive 1 if set above) */
	}

	IPF_Emulate();					/* Update emulation's state up to this point, then set new drive/side */
#endif
}




/*
 * Write a byte into one of the FDC registers
 * 0=command   1=track   2=sector   3=data
 */
void	IPF_FDC_WriteReg ( uint8_t Reg , uint8_t Byte )
{
#ifndef HAVE_CAPSIMAGE
	return;						/* This should not be reached (an IPF image can't be inserted without capsimage) */

#else
	if ( Reg == 0 )					/* more detailed logs for command register */
		IPF_FDC_LogCommand ( Byte );
	else
		LOG_TRACE(TRACE_FDC, "fdc ipf write reg=%d data=0x%x VBL=%d HBL=%d\n" , Reg , Byte , nVBLs , nHBL );
	
	/* In the case of CTR images, we must reset the revolution counter */
	/* when a command access data on disk and track/side changed since last access */
	if ( Reg == 0 )
	{
		int	Type;
		int	Drive;

		Type = FDC_GetCmdType ( Byte );
		if ( ( Type == 2 ) || ( Type == 3 ) )
		{
			Drive = IPF_State.Fdc.driveact;
			if ( ( Drive >= 0 )
			  && ( ( IPF_State.Drive[ Drive ].side != IPF_State.Rev_Side[ Drive ] ) || ( IPF_State.Drive[ Drive ].track != IPF_State.Rev_Track[ Drive ] ) ) )
			{
				IPF_State.Rev_Side[ Drive ] = IPF_State.Drive[ Drive ].side;
				IPF_State.Rev_Track[ Drive ] = IPF_State.Drive[ Drive ].track;
				CAPSSetRevolution ( IPF_State.CapsImage[ Drive ] , 0 );
			}
		}
	}

	IPF_Emulate();					/* Update emulation's state up to this point */

	CAPSFdcWrite ( &IPF_State.Fdc , Reg , Byte );
#endif
}




/*
 * Read the content of one of the FDC registers
 * 0=status   1=track   2=sector   3=data
 */
uint8_t	IPF_FDC_ReadReg ( uint8_t Reg )
{
#ifndef HAVE_CAPSIMAGE
	return 0;					/* This should not be reached (an IPF image can't be inserted without capsimage) */
#else
	uint8_t	Byte;

	IPF_Emulate();					/* Update emulation's state up to this point */

	Byte = CAPSFdcRead ( &IPF_State.Fdc , Reg );
	LOG_TRACE(TRACE_FDC, "fdc ipf read reg=%d data=0x%x VBL=%d HBL=%d\n" , Reg , Byte , nVBLs , nHBL );

	return Byte;
#endif
}




/*
 * Return the content of some registers to display them in the statusbar
 * We should not call IPF_Emulate() or similar, reading should not change emulation's state
 */
void	IPF_FDC_StatusBar ( uint8_t *pCommand , uint8_t *pHead , uint8_t *pTrack , uint8_t *pSector , uint8_t *pSide )
{
#ifndef HAVE_CAPSIMAGE
	abort();					/* This should not be reached (an IPF image can't be inserted without capsimage) */
#else
	int	Drive;

	Drive = IPF_State.Fdc.driveact;
	if ( Drive < 0 )				/* If no drive enabled, use drive O for Head/Side */
		Drive = 0;

	/* We read directly in the structures, to be sure we don't change emulation's state */
	*pCommand	= IPF_State.Fdc.r_command;
	*pHead		= IPF_State.Drive[ Drive ].track;
	*pTrack 	= IPF_State.Fdc.r_track;
	*pSector	= IPF_State.Fdc.r_sector;
	*pSide		= IPF_State.Drive[ Drive ].side;
#endif
}



#ifdef HAVE_CAPSIMAGE
static void	IPF_FDC_LogCommand ( uint8_t Command )
{
	uint8_t	Head , Track , Sector , Side , DataReg;
	int	Drive;
	int	FrameCycles, HblCounterVideo, LineCycles;
	char	buf[ 200 ];


	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	Drive = IPF_State.Fdc.driveact;
	if ( Drive < 0 )				/* If no drive enabled, use drive O for Head/Side */
		Drive = 0;

	/* We read directly in the structures, to be sure we don't change emulation's state */
	Head	= IPF_State.Drive[ Drive ].track;
	Track 	= IPF_State.Fdc.r_track;
	Sector	= IPF_State.Fdc.r_sector;
	DataReg	= IPF_State.Fdc.r_data;
	Side	= IPF_State.Drive[ Drive ].side;

	if      ( ( Command & 0xf0 ) == 0x00 )						/* Restore */
		sprintf ( buf , "type I restore spinup=%s verify=%s steprate=%d drive=%d tr=0x%x head_track=0x%x" ,
			( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
			FDC_StepRate_ms[ Command & 0x03 ] , Drive , Track , Head );

	else if ( ( Command & 0xf0 ) == 0x10 )						/* Seek */
		sprintf ( buf , "type I seek dest_track=0x%x spinup=%s verify=%s steprate=%d drive=%d tr=0x%x head_track=0x%x" ,
			DataReg , ( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
			FDC_StepRate_ms[ Command & 0x03 ] , Drive , Track , Head );

	else if ( ( Command & 0xe0 ) == 0x20 )						/* Step */
		sprintf ( buf , "type I step %d spinup=%s verify=%s steprate_ms=%d drive=%d tr=0x%x head_track=0x%x",
			( IPF_State.Fdc.lineout & CAPSFDC_LO_DIRC ) ? 1 : -1 ,
			( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
			FDC_StepRate_ms[ Command & 0x03 ] , Drive , Track , Head );

	else if ( ( Command & 0xe0 ) == 0x40 )						/* Step In */
		sprintf ( buf , "type I step in spinup=%s verify=%s steprate=%d drive=%d tr=0x%x head_track=0x%x" ,
			( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
			FDC_StepRate_ms[ Command & 0x03 ] , Drive , Track , Head );

	else if ( ( Command & 0xe0 ) == 0x60 )						/* Step Out */
		sprintf ( buf , "type I step out spinup=%s verify=%s steprate=%d drive=%d tr=0x%x head_track=0x%x" ,
			( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_VERIFY ) ? "on" : "off" ,
			FDC_StepRate_ms[ Command & 0x03 ] , Drive , Track , Head );

	else if ( ( Command & 0xe0 ) == 0x80 )						/* Read Sector */
		sprintf ( buf , "type II read sector sector=0x%x multi=%s spinup=%s settle=%s tr=0x%x head_track=0x%x"
			      " side=%d drive=%d dmasector=%d addr=0x%x",
			Sector, ( Command & FDC_COMMAND_BIT_MULTIPLE_SECTOR ) ? "on" : "off" ,
			( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
			Track , Head , Side , Drive , FDC_DMA_GetSectorCount() , FDC_GetDMAAddress() );

	else if ( ( Command & 0xe0 ) == 0xa0 )						/* Write Sector */
		sprintf ( buf , "type II write sector sector=0x%x multi=%s spinup=%s settle=%s tr=0x%x head_track=0x%x"
			      " side=%d drive=%d dmasector=%d addr=0x%x",
			Sector, ( Command & FDC_COMMAND_BIT_MULTIPLE_SECTOR ) ? "on" : "off" ,
			( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
			Track , Head , Side , Drive , FDC_DMA_GetSectorCount() , FDC_GetDMAAddress() );

	else if ( ( Command & 0xf0 ) == 0xc0 )						/* Read Address */
		sprintf ( buf , "type III read address spinup=%s settle=%s tr=0x%x head_track=0x%x side=%d drive=%d addr=0x%x" ,
			( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
			Track , Head , Side , Drive , FDC_GetDMAAddress() );

	else if ( ( Command & 0xf0 ) == 0xe0 )						/* Read Track */
		sprintf ( buf , "type III read track spinup=%s settle=%s tr=0x%x head_track=0x%x side=%d drive=%d addr=0x%x" ,
			( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
			Track , Head , Side , Drive , FDC_GetDMAAddress() );

	else if ( ( Command & 0xf0 ) == 0xf0 )						/* Write Track */
		sprintf ( buf , "type III write track spinup=%s settle=%s tr=0x%x head_track=0x%x side=%d drive=%d addr=0x%x" ,
			( Command & FDC_COMMAND_BIT_SPIN_UP ) ? "off" : "on" ,
			( Command & FDC_COMMAND_BIT_HEAD_LOAD ) ? "on" : "off" ,
			Track , Head , Side , Drive , FDC_GetDMAAddress() );

	else										/* Force Int */
		sprintf ( buf , "type IV force int 0x%x irq=%d index=%d" ,
			Command , ( Command & 0x8 ) >> 3 , ( Command & 0x4 ) >> 2 );


	LOG_TRACE(TRACE_FDC, "fdc ipf %s VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
			buf , nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );
}
#endif



/*
 * Run the FDC emulation during NbCycles cycles (relative to the 8MHz FDC's clock)
 */
void	IPF_Emulate ( void )
{
#ifndef HAVE_CAPSIMAGE
	return;

#else
	int	NbCycles;
	int	Drive;

	NbCycles = CyclesGlobalClockCounter - IPF_State.FdcClock;	/* Number of cycles since last emulation */
	if ( NbCycles < 0 )
		NbCycles = 0;						/* We should call CAPSFdcEmulate even when NbCycles=0 */

//	LOG_TRACE(TRACE_FDC, "fdc ipf emulate cycles=%d VBL=%d HBL=%d clock=%lld\n" , NbCycles , nVBLs , nHBL , CyclesGlobalClockCounter );

	/* Update Write Protect status for each drive */
	for ( Drive=0 ; Drive < MAX_FLOPPYDRIVES ; Drive++ )
		if ( Floppy_IsWriteProtected ( Drive ) )
			IPF_State.Drive[ Drive ].diskattr |= CAPSDRIVE_DA_WP;		/* Disk write protected */
		else
			IPF_State.Drive[ Drive ].diskattr &= ~CAPSDRIVE_DA_WP;		/* Disk is not write protected */


	CAPSFdcEmulate ( &IPF_State.Fdc , NbCycles );			/* Process at max NbCycles */
	IPF_State.FdcClock += IPF_State.Fdc.clockact;			/* clockact can be < NbCycle in some cases */

	/* Update UI's LEDs depending on Status Register */
	FDC_Drive_Set_BusyLed ( (IPF_State.Fdc.r_st0 & ~IPF_State.Fdc.r_stm) | (IPF_State.Fdc.r_st1 & IPF_State.Fdc.r_stm) );
#endif
}



