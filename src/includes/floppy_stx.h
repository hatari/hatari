/*
  Hatari - floppy_stx.h

  This file is distributed under the GNU General Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/




typedef struct {
	/* Content of the STX sector block (16 bytes) */
	uint32_t		DataOffset;				/* Offset of sector data in the track data */
	uint16_t		BitPosition;				/* Position in bits from the start of the track */
								/* (this seems to be the position of the start of the ID field, */
								/* just after the IDAM, but it's not always precise) */
	uint16_t		ReadTime;				/* in ms */

	uint8_t		ID_Track;				/* Content of the Address Field */
	uint8_t		ID_Head;
	uint8_t		ID_Sector;
	uint8_t		ID_Size;
	uint16_t		ID_CRC;

	uint8_t		FDC_Status;				/* FDC status and flags for this sector */
	uint8_t		Reserved;				/* Unused, always 0 */

	/* Other internal variables */
	uint16_t		SectorSize;				/* In bytes, depends on ID_Size */
	uint8_t		*pData;					/* Bytes for this sector or null if RNF */
	uint8_t		*pFuzzyData;				/* Fuzzy mask for this sector or null if no fuzzy bits */
	uint8_t		*pTimingData;				/* Data for variable bit width or null */

	int32_t		SaveSectorIndex;			/* Index in STX_SaveStruct[].pSaveSectorsStruct or -1 if not used */
} STX_SECTOR_STRUCT;

#define	STX_SECTOR_BLOCK_SIZE		( 4+2+2+1+1+1+1+2+1+1 )	/* Size of the sector block in an STX file = 16 bytes */

/* NOTE : bits 3,4,5 have the same meaning as in the FDC's Status register */
#define	STX_SECTOR_FLAG_VARIABLE_TIME	(1<<0)			/* bit 0, if set, this sector has variable bit width */
#define	STX_SECTOR_FLAG_LOST_DATA	(1<<3)			/* bit 3, if set, data were lost while reading/writing */
#define	STX_SECTOR_FLAG_CRC		(1<<3)			/* bit 3, if set, there's a CRC error */
#define	STX_SECTOR_FLAG_RNF		(1<<4)			/* bit 4, if set, there's no sector data */
#define	STX_SECTOR_FLAG_RECORD_TYPE	(1<<5)			/* bit 5, if set, deleted data */
#define	STX_SECTOR_FLAG_FUZZY		(1<<7)			/* bit 7, if set, this sector has fuzzy bits */

#define	STX_SECTOR_READ_TIME_DEFAULT	16384			/* Default value if ReadTime==0 */


typedef struct {
	/* Content of the STX track block (16 bytes) */
	uint32_t		BlockSize;				/* Number of bytes in this track block */
	uint32_t		FuzzySize;				/* Number of bytes in fuzzy mask */
	uint16_t		SectorsCount;				/* Number of sector blocks in this track */
	uint16_t		Flags;					/* Flags for this track */
	uint16_t		MFMSize;				/* Number of MFM bytes in this track */
	uint8_t		TrackNumber;				/* bits 0-6 = track number   bit 7 = side */
	uint8_t		RecordType;				/* Unused */

	/* Other internal variables */
	STX_SECTOR_STRUCT	*pSectorsStruct;		/* All the sectors struct for this track or null */

	uint8_t			*pFuzzyData;			/* Fuzzy mask data for all the fuzzy sectors of the track */

	uint8_t			*pTrackData;			/* Track data (after sectors data and fuzzy data) */
	uint16_t			TrackImageSyncPosition;
	uint16_t			TrackImageSize;			/* Number of bytes in pTrackImageData */
	uint8_t			*pTrackImageData;		/* Optional data as returned by the read track command */

	uint8_t			*pSectorsImageData;		/* Optional data for the sectors of this track */

	uint8_t			*pTiming;
	uint16_t			TimingFlags;			/* always '5' ? */
	uint16_t			TimingSize;
	uint8_t			*pTimingData;			/* Timing data for all the sectors of the track ; each timing */
								/* consists of 2 bytes per 16 FDC bytes */

	int32_t			SaveTrackIndex;			/* Index in STX_SaveStruct[].pSaveTracksStruct or -1 if not used */
} STX_TRACK_STRUCT;

#define	STX_TRACK_BLOCK_SIZE		( 4+4+2+2+2+1+1 )	/* Size of the track block in an STX file = 16 bytes */

#define	STX_TRACK_FLAG_SECTOR_BLOCK	(1<<0)			/* bit 0, if set, this track contains sector blocks */
#define	STX_TRACK_FLAG_TRACK_IMAGE	(1<<6)			/* bit 6, if set, this track contains a track image */
#define	STX_TRACK_FLAG_TRACK_IMAGE_SYNC	(1<<7)			/* bit 7, if set, the track image has a sync position */


#define	STX_HEADER_ID		"RSY\0"				/* All STX files should start with these 4 bytes */
#define	STX_HEADER_ID_LEN	4				/* Header ID has 4 bytes */

typedef struct {
	/* Content of the STX header block (16 bytes) */
	char		FileID[ 4 ];				/* Should be "RSY\0" */
	uint16_t		Version;				/* Only version 3 is supported */
	uint16_t		ImagingTool;				/* 0x01 (Atari Tool) or 0xCC (Discovery Cartridge) */
	uint16_t		Reserved_1;				/* Unused */
	uint8_t		TracksCount;				/* Number of track blocks in this file */
	uint8_t		Revision;				/* 0x00 (old Pasti file)   0x02 (new Pasti file) */
	uint32_t		Reserved_2;				/* Unused */

	/* Other internal variables */
	STX_TRACK_STRUCT	*pTracksStruct;

	/* These variable are used to warn the user only one time if a write command is made */
	bool		WarnedWriteSector;			/* True if a 'write sector' command was made and user was warned */
	bool		WarnedWriteTrack;			/* True if a 'write track' command was made and user was warned */
} STX_MAIN_STRUCT;

#define	STX_MAIN_BLOCK_SIZE		( 4+2+2+2+1+1+4 )	/* Size of the header block in an STX file = 16 bytes */


/* Additional structures used to save the data for the 'write sector' and 'write track' commands */
/* TODO : data are only saved in memory / snapshot and will be lost when exiting. */
/* We should have a file format to store them with the .STX file */
typedef struct {
	/* Copy track/side + ID field + BitPosition to uniquely identify each sector */
	uint8_t		Track;
	uint8_t		Side;
	uint16_t		BitPosition;
	uint8_t		ID_Track;				/* Content of the Address Field */
	uint8_t		ID_Head;
	uint8_t		ID_Sector;
	uint8_t		ID_Size;
	uint16_t		ID_CRC;
	
	uint16_t		SectorSize;				/* Number of bytes in this sector */
	uint8_t		*pData;					/* Data written for this sector */

	uint8_t		StructIsUsed;				/* >0 : this structure contains info (and must be saved) */
								/* =0 : this structure is free and can be reused for another sector */
} STX_SAVE_SECTOR_STRUCT;


typedef struct {
	uint8_t		Track;
	uint8_t		Side;
	
	uint16_t		TrackSizeWrite;				/* Number of bytes in this track (when writing) */
								/* (can be rounded to 16 because of DMA buffering */
	uint8_t		*pDataWrite;				/* Data written for this track */

	uint16_t		TrackSizeRead;				/* Number of bytes in this track (when reading) */
								/* Due to interpreting bytes $F5-$FF, TrackSizeRead will often be > TrackSizeWrite */
	uint8_t		*pDataRead;				/* Data saved for this track as they will be read */
								/* (after interpreting bytes $F5-$FF) */
} STX_SAVE_TRACK_STRUCT;


typedef struct {
	uint32_t			SaveSectorsCount;
	STX_SAVE_SECTOR_STRUCT	*pSaveSectorsStruct;

	uint32_t			SaveTracksCount;
	STX_SAVE_TRACK_STRUCT	*pSaveTracksStruct;
} STX_SAVE_STRUCT;



extern void	STX_MemorySnapShot_Capture(bool bSave);
extern bool	STX_FileNameIsSTX(const char *pszFileName, bool bAllowGZ);
extern bool	STX_FileNameToSave ( const char *FilenameSTX , char *FilenameSave );
extern uint8_t	*STX_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType);
extern bool	STX_WriteDisk(int Drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize);

extern bool	STX_Init ( void );
extern bool	STX_Insert ( int Drive , const char *FilenameSTX , uint8_t *pImageBuffer , long ImageSize );
extern bool	STX_Eject ( int Drive );

extern STX_MAIN_STRUCT *STX_BuildStruct ( uint8_t *pFileBuffer , int Debug );


extern int	FDC_GetBytesPerTrack_STX ( uint8_t Drive , uint8_t Track , uint8_t Side );
extern uint32_t	FDC_GetCyclesPerRev_FdcCycles_STX ( uint8_t Drive , uint8_t Track , uint8_t Side );
extern int	FDC_NextSectorID_FdcCycles_STX ( uint8_t Drive , uint8_t NumberOfHeads , uint8_t Track , uint8_t Side );
extern uint8_t	FDC_NextSectorID_TR_STX ( void );
extern uint8_t	FDC_NextSectorID_SR_STX ( void );
extern uint8_t	FDC_NextSectorID_LEN_STX ( void );
extern uint8_t	FDC_NextSectorID_CRC_OK_STX ( void );
extern uint8_t	FDC_ReadSector_STX ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side , int *pSectorSize );
extern uint8_t	FDC_WriteSector_STX ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side , int SectorSize );
extern uint8_t	FDC_ReadAddress_STX ( uint8_t Drive , uint8_t Track , uint8_t Sector , uint8_t Side );
extern uint8_t	FDC_ReadTrack_STX ( uint8_t Drive , uint8_t Track , uint8_t Side );
extern uint8_t	FDC_WriteTrack_STX ( uint8_t Drive , uint8_t Track , uint8_t Side , int TrackSize );

