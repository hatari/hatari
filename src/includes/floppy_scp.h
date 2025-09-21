/*
  Hatari - floppy_scp.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FLOPPY_SCP_H
#define HATARI_FLOPPY_SCP_H



#define	SCP_TRACK_HEADER_ID		"TRK"			/* All track blocks should start with these 3 bytes */
#define	SCP_TRACK_HEADER_ID_LEN		3			/* Header ID has 3 bytes */

typedef struct {
	uint32_t	Duration_ns;				/* +0 : Duration for 1st revolution (in nanosecs) */
	uint32_t	FluxNbr;				/* +4 : Number of sampled flux transition in this track */
	uint32_t	DataOffset;				/* +8 : Offset for the flux data (in bytes, from track header) */
} SCP_TRACK_REV_STRUCT;


typedef struct {

	/* Content of the SCP track header (16 bytes) */
	char		TrackId[ SCP_TRACK_HEADER_ID_LEN ];	/* +0 : Should be "TRK" */
	uint8_t		TrackNumber;				/* +3 : Track Number (0-167) */

	SCP_TRACK_REV_STRUCT	*pTrackRevs;			/* Contains as many entries as set in RevolutionsNbr */

	uint32_t	TrackHeaderOffset;			/* Offset for this track header (in bytes, from start of the scp file) */
								/* 0=no flux data for this track */
	uint8_t		*pTrackHeader;				/* Pointer to this track header in memory */
} SCP_TRACK_STRUCT;


#define	SCP_HEADER_ID			"SCP"			/* All SCP files should start with these 3 bytes */
#define	SCP_HEADER_ID_LEN		3			/* Header ID has 3 bytes */

typedef struct {
	/* Content of the SCP main header (16 bytes) */
	char		FileID[ SCP_HEADER_ID_LEN ];		/* +0 : Should be "SCP" */
	uint8_t		Version;				/* +3 : Upper 4 bits are version, lower 4 bits are revision */
	uint8_t		DiskType;				/* +4 : Disk Type */
	uint8_t		RevolutionsNbr;				/* +5 : Number of revolutions used to image the floppy */
	uint8_t		StartTrack;				/* +6 : Start Track (0-167) */
	uint8_t		EndTrack;				/* +7 : End Track (0-167) */
	uint8_t		Flags;					/* +8 :  */
	uint8_t		CellTimeWidth;				/* +9 : Number of bits to encode a cell time ; should be 0 = 16 bits */
	uint8_t		HeadsNbr;				/* +10 : Number of heads in the file ; 0=both 1=bottom 2=top */
	uint8_t		CaptureRes;				/* +11 : Resolution of the capture ; should be 0 = 25 nanosecs */
	uint32_t	CRC;					/* +12 : CRC  */

	/* Other internal variables */
	SCP_TRACK_STRUCT	*pTracks;			/* Array of all tracks */

	/* These variable are used to warn the user only one time if a write command is made */
	bool		WarnedWriteSector;			/* True if a 'write sector' command was made and user was warned */
	bool		WarnedWriteTrack;			/* True if a 'write track' command was made and user was warned */
} SCP_MAIN_STRUCT;

#define	SCP_FLAG_INDEX	(1<<0)			/* bit 0, 1=track was read just after index pulse */
#define	SCP_FLAG_TPI	(1<<1)			/* bit 1, 1=96TPI, 0=48 TPI */
#define	SCP_FLAG_RPM	(1<<2)			/* bit 2, 1=360 RPM, 0=300 RPM */
#define	SCP_FLAG_TYPE	(1<<3)			/* bit 3, 1=flux are normalized 0=preservation quality */
#define	SCP_FLAG_MODE	(1<<4)			/* bit 4, 1=image is writable, 0=read only */
#define	SCP_FLAG_FOOTER	(1<<5)			/* bit 5, 1=image contains a footer block, 0=no footer */
#define	SCP_FLAG_EXTENDED (1<<6)		/* bit 6, 1=image is for other media, 0=image is for a floppy drive */





extern void	SCP_MemorySnapShot_Capture(bool bSave);
extern bool	SCP_FileNameIsSCP(const char *pszFileName, bool bAllowGZ);
extern uint8_t	*SCP_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType);
extern bool	SCP_WriteDisk(int Drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize);

extern bool	SCP_Init ( void );
extern bool	SCP_Insert ( int Drive , const char *FilenameSTX , uint8_t *pImageBuffer , long ImageSize );
extern bool	SCP_Eject ( int Drive );

extern struct fd_stream	*SCP_Get_Fd_Stream ( uint8_t Drive );

extern SCP_MAIN_STRUCT *SCP_BuildStruct ( uint8_t *pFileBuffer , int Debug );

extern int	SCP_LoadTrack ( int Drive , int Track , int Side );

extern int	FDC_GetBytesPerTrack_SCP ( uint8_t Drive , uint8_t Track , uint8_t Side );
extern uint32_t	FDC_GetCyclesPerRev_FdcCycles_SCP ( uint8_t Drive , uint8_t Track , uint8_t Side );




/*-----------------------------------------------------------------------*/
/*
 * Flux to MFM bit decoding - Support for SCP disk image - BEGIN
 * based on code by Keir Fraser https://github.com/keirf/Disk-Utilities  
 */

struct scp_stream {
	struct fd_stream s;
	int Drive;		/* Drive number 0 or 1 used for this stream */

	/* Current track number. */
	unsigned int track;

	/* Raw track data. */
	uint16_t *dat;
	unsigned int datsz;

	bool index_cued;
	unsigned int revs;       /* stored disk revolutions */
	unsigned int dat_idx;    /* current index into dat[] */
	unsigned int index_pos;  /* next index offset */
	int jitter;              /* accumulated injected jitter */

	int total_ticks;         /* total ticks to final index pulse */
	int acc_ticks;           /* accumulated ticks so far */

	unsigned int *index_off; /* data offsets of each index */
};

#define SCK_NS_PER_TICK (25u)

/*
 * Flux to MFM bit decoding - Support for SCP disk image - END
 */



#endif		/* HATARI_FLOPPY_SCP_H */
