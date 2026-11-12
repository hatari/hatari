/*
  Hatari - dim.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  DIM disk image support.
*/
const char DIM_fileid[] = "Hatari dim.c";

#include <assert.h>

#include "main.h"
#include "file.h"
#include "filename.h"
#include "utils.h"
#include "floppy.h"
#include "floppies/dim.h"

#undef SAVE_TO_DIM_IMAGES

/*
    .DIM FILE FORMAT
  --===============-------------------------------------------------------------

  The file format of normal .DIM image files are quite the same as the .ST image
  files (see st.c) - the .DIM image files just have an additional header of
  32 bytes. However, there are also "compressed" images which only contain the
  used sectors of the disk. It is necessary to parse the FAT to "uncompress"
  these images.

  The header contains following information:

  Offset  Size      Description
  ------  --------  -----------
  0x0000  Word      ID Header (0x4242('BB'))
  0x0002  Byte      1 = disk configuration has been detected automatically (from the BPB)
                    0 = the user specified the disk configuration
  0x0003  Byte      Image contains all sectors (0) or only used sectors (1)
  0x0004  Word      Padding, always zero
  0x0005  Word      Sides (0 or 1; add 1 to this to get correct number of sides)
  0x0007  Word      Sectors per track
  0x0009  Word      Starting Track (0 based)
  0x000B  Byte      Ending Track (0 based)
  0x000D  Byte      Double-Density(0) or High-Density (1)
  0x000E  18 Bytes  A copy of the Bios Parameter Block (BPB) of this disk.

  Bytes $02-$0D are FastCopy's configuration block written as it is in memory, so
  the words sit at odd offsets and $04 is the compiler's alignment byte. The values
  are small, so the high bytes at $05, $07, $09 and $0B are always zero.

  $0D is the density FastCopy drove the drive at, so it cannot always be worked out
  from the sectors per track. When the configuration is read from the disc it is 0 on
  a double density drive; on a high density drive it comes from the drive's media
  detect bit if HD detection is set up in the options, else it is 1 for more than 11
  sectors per track. With a user specified configuration it is the HD setting of the
  dialog. An HD disc formatted as 720K and read with detection gives 1 with 9 sectors.

  The BPB is stored as nine 68000 words:

  0X000E  recsiz    Bytes per sector
  0X0010  clsiz     Sectors per cluster
  0X0012  clsizb    Bytes per cluster
  0X0014  rdlen     Root directory length in sectors
  0X0016  fsiz      FAT size in sectors
  0X0018  fatrec    First sector of the second FAT (the one TOS uses)
  0X001A  datrec    First data sector
  0X001C  numcl     Number of data clusters
  0X001E  bflags    Bit 0 clear for a 12-bit FAT, set for a 16-bit FAT

  FastCopy has three imaging modes, given by the flags at $02 and $03:

  $02 $03
   0   0   Geometry typed in by the user. Only tracks 'Starting track' to
   0   1  'Ending track' are stored, so the image may be part of a disc. The
             BPB is still filled in but cannot be relied on. $03 is the 'Get
             Sectors Used' setting, which only counts with $02 set.
   1   0   Geometry from the BPB. The whole disc is stored.
   1   1   Geometry from the BPB and FAT. Only used clusters are stored.

  Sector data follows the header, each track in turn with sides alternating as in
  a '.ST' image, starting at 'Starting track'.

  For a 'Get Sectors Used' image the boot sector, both FATs and the root directory
  (sectors 0 to datrec-1) are stored in full. These are followed only by the
  clusters marked as used in the FAT, in cluster order, each stored in full
  (clsiz sectors). Clusters marked free($000), reserved($001) or bad($FF0-$FF7) are
  not stored, and the image ends with the last used cluster. When restoring, FastCopy
  takes the used clusters from the second FAT in the image. FastCopy decides sector by
  sector, so sectors after the last whole cluster, when the disc has any, are stored if
  the FAT entry for the cluster they would be in is used.


  We take a copy of the header while reading the disk image, so we can
  preserve the unknown field when saving the disk image later.

*/


#define DIM_HEADER_SIZE				32

#define DIM_FIRST_DATA_CLUSTER			2		/* FAT entries 0 and 1 are reserved */
#define DIM_MAX_SIDES				1		/* Header holds sides-1 */
#define DIM_MAX_SECTORS_PER_TRACK		23

#define DIM_FAT12_ENTRY_MASK			0x0FFF

/* Special values for cluster number */
#define DIM_FAT12_FREE				0x000
#define DIM_FAT12_RESERVED			0x001
#define DIM_FAT12_BAD_FIRST			0xFF0		/* $FF0-$FF7 mark bad clusters */
#define DIM_FAT12_BAD_LAST			0xFF7

#define DIM_BPB_FLAG_FAT16			0x0001		/* 'bflags' bit set for a 16-bit FAT */

								/* Example values for 80 tracks, 9 sectors per track, 2 sides */
#define	DIM_HEADER_SIGNATURE			0x00		/* "BB" */
#define	DIM_HEADER_DISK_CONF			0x02		/* 1 */
#define	DIM_HEADER_USED_SECTORS_ONLY		0x03		/* 0 or 1 */
#define	DIM_HEADER_RESERVED			0x04		/* reserved, always 0 */
#define	DIM_HEADER_HIHG0_1			0x05		/* high byte of the side, always 0 */
#define	DIM_HEADER_SIDES			0x06		/* 1 (=2 sides) */
#define	DIM_HEADER_HIGH0_2			0x07		/* high byte of the sectors per track, always 0 */
#define	DIM_HEADER_SECTORS_PER_TRACK		0x08		/* 09 */
#define	DIM_HEADER_HIGH0_3			0x09		/* high byte of the starting track, always 0 */
#define	DIM_HEADER_STARTING_TRACK		0x0a		/* 00 */
#define	DIM_HEADER_HIGH0_4			0x0b		/* high byte of the ending track, always 0 */
#define	DIM_HEADER_ENDING_TRACK			0x0c		/* 4f */
#define	DIM_HEADER_DENSITY			0x0d		/* 0 (=double density) */
#define	DIM_HEADER_SECTOR_SIZE			0x0e		/* 0200 */
#define	DIM_HEADER_SECTORS_PER_CLUSTER		0x10		/* 0002 */
#define	DIM_HEADER_BYTES_PER_CLUSTER		0x12		/* 0400 */
#define	DIM_HEADER_ROOT_DIR_SIZE		0x14		/* 0007 */
#define	DIM_HEADER_SECTORS_PER_FAT		0x16		/* 0005 */
#define	DIM_HEADER_FAT_START_SECTOR		0x18		/* 0006 */
#define	DIM_HEADER_DATA_START_SECTOR		0x1a		/* 0012 */
#define	DIM_HEADER_CLUSTERS_COUNT		0x1c		/* 02c7 */
#define	DIM_HEADER_BPB_FLAGS			0x1e		/* 0000 */



#ifdef SAVE_TO_DIM_IMAGES
static struct {
	char filename[FILENAME_MAX];
	uint8_t data[DIM_HEADER_SIZE];
} headers[MAX_FLOPPYDRIVES];
#endif

/**
 * Does filename end with a .DIM extension? If so, return TRUE
 */
bool DIM_FileNameIsDIM(const char *pszFileName, bool bAllowGZ)
{
	return FileName_DoesExtensionMatch(pszFileName, ".dim")
	       || (bAllowGZ && FileName_DoesExtensionMatch(pszFileName, ".dim.gz"));
}



/**
 * Return number of entries a FAT12 table can hold, two entries are packed into every three bytes
 */
static int DIM_GetFAT12EntryCount(uint16_t FATSectors)
{
	return ( FATSectors*NUMBYTESPERSECTOR*2 ) / 3;
}

/**
 * Return entry 'Cluster' from a FAT12 table
 */
static uint16_t DIM_GetFAT12Entry(unsigned char *pFAT,int Cluster)
{
	uint8_t *pEntry;

	pEntry = pFAT + (Cluster*3)/2;
	if (Cluster & 1)
		return (uint16_t)((pEntry[0]>>4) | (pEntry[1]<<4));
	else
		return (uint16_t)((pEntry[0] | (pEntry[1]<<8)) & DIM_FAT12_ENTRY_MASK);
}

/**
 * Return TRUE if FAT entry marks a bad cluster
 */
static bool DIM_IsClusterBad(uint16_t FATEntry)
{
	return ( FATEntry >= DIM_FAT12_BAD_FIRST ) && ( FATEntry <= DIM_FAT12_BAD_LAST );
}

//-----------------------------------------------------------------------
/*
	FastCopy stores any cluster whose FAT entry is not free, reserved or bad
*/
static bool DIM_IsClusterUsed(unsigned short int FATEntry)
{
	return ( FATEntry != DIM_FAT12_FREE ) && ( FATEntry != DIM_FAT12_RESERVED ) && !DIM_IsClusterBad(FATEntry);
}




/**
 * Load .DIM file into memory, set number of bytes loaded and return a pointer
 * to the buffer.
 */
uint8_t *DIM_ReadDisk(int drive, const char *pszFileName, long *pImageSize, int *pImageType)
{
	uint8_t		*pDimFile;
	uint8_t		*pDiskBuffer = NULL;
	uint8_t		*pDim_Header;
	uint8_t		*pDim_Data;
	uint8_t		*pFAT;
	uint16_t	SectorsPerTrack, SectorsPerImage , Sector;
	uint16_t	SectorsPerCluster, SectorsPerFAT, FATStartSector, DataStartSector, ClusterCount;
	uint16_t	LastCluster , Cluster;

	assert(drive < MAX_FLOPPYDRIVES);

#ifdef SAVE_TO_DIM_IMAGES
	headers[drive].filename[0] = 0;		/* Invalidate header cache */
#endif

	/* Load file into buffer */
	pDimFile = File_Read(pszFileName, pImageSize, NULL);
	if (pDimFile)
	{
		pDim_Header = pDimFile;
		pDim_Data = pDimFile + DIM_HEADER_SIZE;

		/* Check header for valid image */
		SectorsPerTrack = pDim_Header[ DIM_HEADER_SECTORS_PER_TRACK ] * ( pDim_Header[ DIM_HEADER_SIDES ]+1 );	/* Both sides */
		SectorsPerImage = ( pDim_Header[ DIM_HEADER_ENDING_TRACK ] + 1 ) * SectorsPerTrack;

		if ( ( pDim_Header[0x00] == 0x42 && pDim_Header[0x01] == 0x43 )	/* 0x4242 "BB" */
		  && ( pDim_Header[ DIM_HEADER_STARTING_TRACK ] <= pDim_Header[ DIM_HEADER_ENDING_TRACK ] )
		  && ( pDim_Header[ DIM_HEADER_SIDES ] <= DIM_MAX_SIDES)
		  && ( pDim_Header[ DIM_HEADER_SECTORS_PER_TRACK ] >= 1 )
		  && ( pDim_Header[ DIM_HEADER_SECTORS_PER_TRACK ] <= DIM_MAX_SECTORS_PER_TRACK ) )
		{
			*pImageSize = SectorsPerImage * NUMBYTESPERSECTOR;

			pDiskBuffer = malloc(*pImageSize);
			if (!pDiskBuffer)
			{
				perror("DIM_ReadDisk");
				*pImageSize = 0;
				free(pDimFile);
				return NULL;
			}

			/* Anything not stored in the file is left blank */
			memset ( pDiskBuffer , 0 , *pImageSize );

			/* Only used sectors ? */
			if ( pDim_Header[ DIM_HEADER_DISK_CONF ] && pDim_Header[ DIM_HEADER_USED_SECTORS_ONLY ] )
			{
				/* Image is 'Get Sectors Used' */
				SectorsPerCluster = Mem_ReadU16_BE ( pDim_Header + DIM_HEADER_SECTORS_PER_CLUSTER );
				SectorsPerFAT = Mem_ReadU16_BE ( pDim_Header + DIM_HEADER_SECTORS_PER_FAT );
				FATStartSector = Mem_ReadU16_BE ( pDim_Header + DIM_HEADER_FAT_START_SECTOR );
				DataStartSector = Mem_ReadU16_BE ( pDim_Header + DIM_HEADER_DATA_START_SECTOR );
				ClusterCount = Mem_ReadU16_BE ( pDim_Header + DIM_HEADER_CLUSTERS_COUNT );
				LastCluster = DIM_FIRST_DATA_CLUSTER + ClusterCount - 1;

				/* Only whole discs with a 12-bit FAT can be used, and every cluster must fit on the disc and in the FAT */
				/* FastCopy also reads 16-bit FATs, but they are only used on hard disc partitions, never on floppies */
				if ( ( pDim_Header[ DIM_HEADER_STARTING_TRACK ] != 0 )
				  || ( Mem_ReadU16_BE ( pDim_Header + DIM_HEADER_BPB_FLAGS ) & DIM_BPB_FLAG_FAT16 )
				  || ( Mem_ReadU16_BE ( pDim_Header + DIM_HEADER_SECTOR_SIZE ) != NUMBYTESPERSECTOR)
				  || ( SectorsPerCluster == 0 )
				  || ( FATStartSector + SectorsPerFAT > DataStartSector )
				  || ( DataStartSector + ClusterCount * SectorsPerCluster > SectorsPerImage )
				  || ( LastCluster >= DIM_GetFAT12EntryCount ( SectorsPerFAT) ) )
				{
					fprintf(stderr, "Error : DIM image with only used sectors and incorrect values\n");
					*pImageSize = 0;
					free(pDimFile);
					return NULL;
				}

				else
				{
					/* Boot sector, both FATs and root directory are stored in full */
					memcpy ( pDiskBuffer , pDim_Data , DataStartSector*NUMBYTESPERSECTOR );
					pDim_Data += DataStartSector*NUMBYTESPERSECTOR;

					/* Copy each used cluster to its position on the disc, using the second FAT as TOS does */
					pFAT = pDiskBuffer + FATStartSector*NUMBYTESPERSECTOR;

					for ( Sector = DataStartSector ; Sector < SectorsPerImage ; Sector++ )
					{
						Cluster = DIM_FIRST_DATA_CLUSTER + (Sector-DataStartSector)/SectorsPerCluster;
//fprintf ( stderr , "dim sector=%03x cluster=%03x fat_entry=%03x\n" , Sector , Cluster , DIM_GetFAT12Entry( pFAT, Cluster ) );
						if ( ( Cluster<DIM_GetFAT12EntryCount(SectorsPerFAT) ) && DIM_IsClusterUsed( DIM_GetFAT12Entry(pFAT,Cluster) ) )
						{
							memcpy(pDiskBuffer + Sector*NUMBYTESPERSECTOR , pDim_Data , NUMBYTESPERSECTOR );
							pDim_Data += NUMBYTESPERSECTOR;
						}
					}
				}
			}

			/* All sectors */
			else
			{
				/* Image is 'Get Sectors All' : copy the stored tracks, which may not start at track 0 */
				/* Unused track will be blank memory from above */
				memcpy ( pDiskBuffer + (int)(pDim_Header[ DIM_HEADER_STARTING_TRACK ] ) * SectorsPerTrack * NUMBYTESPERSECTOR ,
					pDim_Data ,
					(int)( pDim_Header[ DIM_HEADER_ENDING_TRACK ] - pDim_Header[ DIM_HEADER_STARTING_TRACK ] + 1 ) * SectorsPerTrack * NUMBYTESPERSECTOR );
			}

#ifdef SAVE_TO_DIM_IMAGES
			memcpy(headers[drive].data, pDim_Header, DIM_HEADER_SIZE);
			strncpy(headers[drive].filename, pszFileName, FILENAME_MAX - 1);
			headers[drive].filename[FILENAME_MAX - 1] = 0;
#endif

			/* Free DIM file we loaded */
			free(pDimFile);
		}

		else
		{
			fprintf(stderr, "This is not a valid DIM image!\n");
			*pImageSize = 0;
			free(pDimFile);
			return NULL;
		}
	}

	if ( pDiskBuffer == NULL )
	{
		*pImageSize = 0;
		return NULL;
	}

#if 0
/* DEBUG : write the memory buffer as a .ST image to debug DIM loading when only used sectors are stored
 * we can then compare the original .ST file (that was converted to .DIM) with the new .ST image
 * obtained from loading the .DIM
 */
#include "floppies/st.h"
char name[400];
sprintf ( name , "%s.st" , pszFileName );
ST_WriteDisk(drive, name, pDiskBuffer, *pImageSize);
#endif

	*pImageType = FLOPPY_IMAGE_TYPE_DIM;
	return pDiskBuffer;
}


/*-----------------------------------------------------------------------*/
/**
 * Save .DIM file from memory buffer. Returns TRUE is all OK
 */
bool DIM_WriteDisk(int drive, const char *pszFileName, uint8_t *pBuffer, int ImageSize)
{
#ifdef SAVE_TO_DIM_IMAGES

	unsigned short int nSectorsPerTrack, nSides;
	uint8_t *pDimFile;
	int nTracks;
	bool bRet;

	assert(drive < MAX_FLOPPYDRIVES);

	/* Allocate memory for the whole DIM image: */
	pDimFile = malloc(ImageSize + 32);
	if (!pDimFile)
	{
		perror("DIM_WriteDisk");
		return false;
	}

	/* Try to preserve the old header data for fields that are unknown yet: */
	if (strcmp(pszFileName, headers[drive].filename) == 0)
		memcpy(pDimFile, headers[drive].data, DIM_HEADER_SIZE);
	else
		memset(pDimFile, 0, DIM_HEADER_SIZE);

	/* Now fill in the new header information: */
	Floppy_FindDiskDetails(pBuffer, ImageSize, &nSectorsPerTrack, &nSides);
	nTracks = ((ImageSize / NUMBYTESPERSECTOR) / nSectorsPerTrack) / nSides;
	pDimFile[0x00] = pDimFile[0x01] = 0x42;     /* ID */
	pDimFile[0x03] = 0;                         /* Image contains all sectors */
	pDimFile[0x06] = nSides - 1;                /* Sides */
	pDimFile[0x08] = nSectorsPerTrack;          /* Sectors per track */
	pDimFile[0x0A] = 0;                         /* Starting track */
	pDimFile[0x0C] = nTracks - 1;               /* Ending track */
	pDimFile[0x0D] = (ImageSize > 1024*1024);   /* DD / HD flag */

	/* Now copy the disk data: */
	memcpy(pDimFile + 32, pBuffer, ImageSize);
	
	/* And finally save it: */
	bRet = File_Save(pszFileName, pDimFile, ImageSize + 32, false);

	free(pDimFile);

	return bRet;

#else   /*SAVE_TO_ST_IMAGES*/

	/* Oops, cannot save */
	return false;

#endif  /*SAVE_TO_ST_IMAGES*/
}
