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
	Uint32		Duration_ns;				/* +0 : Duration for 1st revolution (in nanosecs) */
	Uint32		FluxNbr;				/* +4 : Number of sampled flux transition in this track */
	Uint32		DataOffset;				/* +8 : Offset for the flux data (in bytes, from track header) */
} SCP_TRACK_REV_STRUCT;


typedef struct {

	/* Content of the SCP track header (16 bytes) */
	char		TrackId[ SCP_TRACK_HEADER_ID_LEN ];	/* +0 : Should be "TRK" */
	Uint8		TrackNumber;				/* +3 : Track Number (0-167) */

	SCP_TRACK_REV_STRUCT	*pTrackRevs;			/* Contains as many entries as set in RevolutionsNbr */

	Uint32		FileOffset;				/* Offset for this track header (in bytes, from start of the scp file) */
								/* 0=no flux data for this track */
} SCP_TRACK_STRUCT;


#define	SCP_HEADER_ID			"SCP"			/* All SCP files should start with these 3 bytes */
#define	SCP_HEADER_ID_LEN		3			/* Header ID has 3 bytes */

typedef struct {
	/* Content of the SCP main header (16 bytes) */
	char		FileID[ SCP_HEADER_ID_LEN ];		/* +0 : Should be "SCP" */
	Uint8		Version;				/* +3 : Upper 4 bits are version, lower 4 bits are revision */
	Uint8		DiskType;				/* +4 : Disk Type */
	Uint8		RevolutionsNbr;				/* +5 : Number of revolutions used to image the floppy */
	Uint8		StartTrack;				/* +6 : Start Track (0-167) */
	Uint8		EndTrack;				/* +7 : End Track (0-167) */
	Uint8		Flags;					/* +8 :  */
	Uint8		CellTimeBits;				/* +9 : Number of bits to encode a cell time ; should be 0 = 16 bits */
	Uint8		HeadsNbr;				/* +10 : Number of heads in the file ; 0=both 1=bottom 2=top */
	Uint8		CaptureRes;				/* +11 : Resolution of the capture ; should be 0 = 25 nanosecs */
	Uint32		CRC;					/* +12 : CRC  */

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
extern Uint8	*SCP_ReadDisk(int Drive, const char *pszFileName, long *pImageSize, int *pImageType);
extern bool	SCP_WriteDisk(int Drive, const char *pszFileName, Uint8 *pBuffer, int ImageSize);

extern bool	SCP_Init ( void );
extern bool	SCP_Insert ( int Drive , const char *FilenameSTX , Uint8 *pImageBuffer , long ImageSize );
extern bool	SCP_Eject ( int Drive );

extern SCP_MAIN_STRUCT *SCP_BuildStruct ( Uint8 *pFileBuffer , int Debug );


extern int	FDC_GetBytesPerTrack_STX ( Uint8 Drive , Uint8 Track , Uint8 Side );
extern Uint32	FDC_GetCyclesPerRev_FdcCycles_STX ( Uint8 Drive , Uint8 Track , Uint8 Side );
extern int	FDC_NextSectorID_FdcCycles_STX ( Uint8 Drive , Uint8 NumberOfHeads , Uint8 Track , Uint8 Side );
extern Uint8	FDC_NextSectorID_TR_STX ( void );
extern Uint8	FDC_NextSectorID_SR_STX ( void );
extern Uint8	FDC_NextSectorID_LEN_STX ( void );
extern Uint8	FDC_NextSectorID_CRC_OK_STX ( void );
extern Uint8	FDC_ReadSector_STX ( Uint8 Drive , Uint8 Track , Uint8 Sector , Uint8 Side , int *pSectorSize );
extern Uint8	FDC_WriteSector_STX ( Uint8 Drive , Uint8 Track , Uint8 Sector , Uint8 Side , int SectorSize );
extern Uint8	FDC_ReadAddress_STX ( Uint8 Drive , Uint8 Track , Uint8 Sector , Uint8 Side );
extern Uint8	FDC_ReadTrack_STX ( Uint8 Drive , Uint8 Track , Uint8 Side );
extern Uint8	FDC_WriteTrack_STX ( Uint8 Drive , Uint8 Track , Uint8 Side , int TrackSize );

#endif		/* HATARI_FLOPPY_SCP_H */
