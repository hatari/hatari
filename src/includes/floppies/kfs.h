/*
  Hatari - floppy_kfs.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FLOPPY_KFS_H
#define HATARI_FLOPPY_KFS_H



extern void	KFS_MemorySnapShot_Capture(bool bSave);
extern bool	KFS_FileNameIsKFS ( const char *pszFileName, bool bAllowGZ );
extern uint8_t	*KFS_ReadDisk ( int Drive, const char *pszFileName, long *pImageSize, int *pImageType );
extern bool	KFS_WriteDisk ( int Drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize );

extern bool	KFS_Insert ( int Drive , const char *FilenameKFS , uint8_t *pImageBuffer , long ImageSize );
extern bool	KFS_Eject ( int Drive );

extern int	FDC_GetBytesPerTrack_KFS ( uint8_t Drive , uint8_t Track , uint8_t Side );
extern uint32_t	FDC_GetCyclesPerRev_FdcCycles_KFS ( uint8_t Drive , uint8_t Track , uint8_t Side );




/*-----------------------------------------------------------------------*/
/*
 * Flux to MFM bit decoding - Support for Kryoflux raw stream disk image - BEGIN
 * based on code by Keir Fraser https://github.com/keirf/Disk-Utilities
 */

struct kfs_stream {
	int Drive;		/* Drive number 0 or 1 used for this stream */

	/* Current track number. */
	unsigned int track;

	/* Raw track data. */
	unsigned char *dat;
	unsigned int datsz;

	/* Index positions in the raw stream. */
	unsigned int *idxs;
	unsigned int idx_i;

	unsigned int dat_idx;    /* current index into dat[] */
	unsigned int stream_idx; /* current index into non-OOB data in dat[] */
};

/*
 * Flux to MFM bit decoding - Support for Kryoflux raw stream disk image - END
 */



/* To handle Kryoflux RAW stream images (kfs) with one file per track/side */
#define	KF_MAX_TRACK_RAW_STREAM_IMAGE	84			/* track number can be 0 .. 83 */
#define	KF_MAX_SIDE_RAW_STREAM_IMAGE	2			/* side number can be 0 or 1 */

typedef struct
{
	struct
	{
		int		TrackSize;
		uint8_t		*TrackData;
	} TracksImage[ MAX_FLOPPYDRIVES ][ KF_MAX_TRACK_RAW_STREAM_IMAGE ][ KF_MAX_SIDE_RAW_STREAM_IMAGE ];

	struct kfs_stream	KFS_Stream[ MAX_FLOPPYDRIVES ];
} KFS_STRUCT;





#endif		/* HATARI_FLOPPY_KFS_H */
