/*
  Hatari - avi_record.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  AVI File recording

  This allows Hatari to record a video file, with both video and audio
  streams, at full frame rate.
  
  Video frames are saved using the current video frequency of the emulated
  machine (50 Hz, 60 Hz, 70 Hz, ...). Frames can be stored using different
  codecs. So far, supported codecs are :
   - BMP : uncompressed RGB images. Very fast to save, very few cpu needed
     but requires a lot of disk bandwidth and a lot of space.
   - PNG : compressed RGB images. Depending on the compression level, this
     can require more cpu and could slow down Hatari. As compressed images
     are much smaller than BMP images, this will require less space on disk
     and much less disk bandwidth. Compression levels 3 or 4 give good
     tradeoff between cpu usage and file size and should not slow down Hatari
     with recent computers.

  PNG compression will often give a x20 ratio when compared to BMP and should
  be used if you have a powerful enough cpu.

  Sound is saved as 16 bits pcm stereo, using the current Hatari sound output
  frequency. For best accuracy, sound frequency should be a multiple of the
  video frequency (to get an integer number of samples per frame) ; this means
  44.1 or 48 kHz are the best choices for 50/60 Hz video.

  The AVI file is divided into multiple chunks. Hatari will save one video stream
  and one audio stream, so the overall structure of the file is the following :

  Previously, Hatari was limited to a standard AVI file < 4GB (using 32 bit offsets).
  Since 08/2017, we support the Open DML AVI file format extension, which allows AVI files
  of any size (using 64 bits offsets). In that case, the AVI file is divided in
  several RIFF chunks. The 1st chunk is a standard RIFF AVI chunk, the next ones
  are RIFF AVIX extension chunks. Each RIFF chunk will contain 1 movi chunk
  and each movi chunk will contain 2 indexes (video and audio).
  All the movi's indexes are then indexed in a super index stored in the main
  RIFF AVI file header.

  RIFF AVI
      LIST
	hdrl
	  avih
	  LIST
	    strl
	      strh (vids)
	      strf
	      indx
	  LIST
	    strl
	      strh (auds)
	      strf
	      indx
	  LIST
	    odml
	      dmlh
      LIST
	INFO
      LIST
	movi
	  00db
	  01wb
	  ...
	  ix00
	  ix01
  RIFF AVIX
      LIST
	movi
	  00db
	  01wb
	  ...
	  ix00
	  ix01
  ...
*/

const char AVIRecord_fileid[] = "Hatari avi_record.c";

#include <SDL.h>
#include <SDL_endian.h>
#include <assert.h>
#include <sys/types.h>  /* For off_t */

#include "main.h"
#include "version.h"
#include "audio.h"
#include "configuration.h"
#include "file.h"
#include "log.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "sound.h"
#include "statusbar.h"
#include "avi_record.h"

/* after above that brings in config.h */
#if HAVE_LIBPNG
#include <png.h>
#endif

#include "pixel_convert.h"				/* inline functions */



typedef struct
{
	uint8_t			ChunkName[4];		/* '00db', '00dc', '01wb', 'ix00' , 'ix01' */
	uint8_t			ChunkSize[4];
} AVI_CHUNK;


typedef struct
{
	uint8_t			identifier[4];		/* '00db', '00dc', '01wb', 'ix00' , 'ix01' */
	uint8_t			flags[4];
	uint8_t			offset[4];
	uint8_t			length[4];
} AVI_CHUNK_INDEX;


typedef struct
{
	uint8_t			ChunkName[4];		/* 'strh' */
	uint8_t			ChunkSize[4];

	uint8_t			stream_type[4];		/* 'vids' or 'auds' */
	uint8_t			stream_handler[4];
	uint8_t			flags[4];
	uint8_t			priority[2];
	uint8_t			language[2];
	uint8_t			initial_frames[4];
	uint8_t			time_scale[4];
	uint8_t			data_rate[4];
	uint8_t			start_time[4];
	uint8_t			data_length[4];
	uint8_t			buffer_size[4];
	uint8_t			quality[4];
	uint8_t			sample_size[4];
	uint8_t			dest_left[2];
	uint8_t			dest_top[2];
	uint8_t			dest_right[2];
	uint8_t			dest_bottom[2];
} AVI_STREAM_HEADER;


#define	AVI_SUPER_INDEX_SIZE	256			/* Up to 256 entries in a super index */

#define AVI_INDEX_OF_INDEXES	0x00			/* Possibles values for index_type */
#define AVI_INDEX_OF_CHUNKS	0x01

typedef struct
{
	uint8_t			offset[8];		/* 64 bit offset in avi file */
	uint8_t			size[4];
	uint8_t			duration[4];
} AVI_STREAM_SUPER_INDEX_ENTRY;

typedef struct
{
	uint8_t			ChunkName[4];		/* 'indx' */
	uint8_t			ChunkSize[4];

	uint8_t			longs_per_entry[2];	/* 4 */
	uint8_t			index_sub_type;		/* 0 */
	uint8_t			index_type;		/* must be AVI_INDEX_OF_INDEXES */
	uint8_t			entries_in_use[4];
	uint8_t			chunk_id[4];		/* '00db', '00dc', '01wb' */
	uint8_t			reserved[12];
	AVI_STREAM_SUPER_INDEX_ENTRY	index[AVI_SUPER_INDEX_SIZE];
} AVI_STREAM_SUPER_INDEX;


typedef struct
{
	uint8_t			offset[4];		/* 32 bit offset in current 'movi' chunk */
	uint8_t			size[4];
} AVI_STREAM_INDEX_ENTRY;

typedef struct
{
	uint8_t			ChunkName[4];		/* 'ix00', 'ix01' */
	uint8_t			ChunkSize[4];

	uint8_t			longs_per_entry[2];	/* 2 */
	uint8_t			index_sub_type;		/* must be 0 */
	uint8_t			index_type;		/* must be AVI_INDEX_OF_CHUNKS */
	uint8_t			entries_in_use[4];
	uint8_t			chunk_id[4];		/* '00db', '00dc', '01wb' */
	uint8_t			base_offset[8];		/* all offsets in index array are relative to this */
	uint8_t			reserved[4];

//	AVI_STREAM_INDEX_ENTRY	*index;			/* array size is dynamic, don't include it here */
} AVI_STREAM_INDEX;


typedef struct
{
	uint8_t			ChunkName[4];		/* 'strf' */
	uint8_t			ChunkSize[4];

	uint8_t			size[4];
	uint8_t			width[4];
	uint8_t			height[4];
	uint8_t			planes[2];
	uint8_t			bit_count[2];
	uint8_t			compression[4];
	uint8_t			size_image[4];
	uint8_t			xpels_meter[4];
	uint8_t			ypels_meter[4];
	uint8_t			clr_used[4];
	uint8_t			clr_important[4];
} AVI_STREAM_FORMAT_VIDS;

typedef struct
{
	uint8_t			ChunkName[4];		/* 'LIST' */
	uint8_t			ChunkSize[4];

	uint8_t			Name[4];		/* 'strl' */
	AVI_STREAM_HEADER	Header;			/* 'strh' */
	AVI_STREAM_FORMAT_VIDS	Format;			/* 'strf' */
	AVI_STREAM_SUPER_INDEX	SuperIndex;		/* 'indx' */
} AVI_STREAM_LIST_VIDS;


typedef struct
{
	uint8_t			ChunkName[4];		/* 'strf' */
	uint8_t			ChunkSize[4];

	uint8_t			codec[2];
	uint8_t			channels[2];
	uint8_t			sample_rate[4];
	uint8_t			bit_rate[4];
	uint8_t			block_align[2];
	uint8_t			bits_per_sample[2];
	uint8_t			ext_size[2];
} AVI_STREAM_FORMAT_AUDS;

typedef struct
{
	uint8_t			ChunkName[4];		/* 'LIST' */
	uint8_t			ChunkSize[4];

	uint8_t			Name[4];		/* 'strl' */
	AVI_STREAM_HEADER	Header;			/* 'strh' */
	AVI_STREAM_FORMAT_AUDS	Format;			/* 'strf' */
	AVI_STREAM_SUPER_INDEX	SuperIndex;		/* 'indx' */
} AVI_STREAM_LIST_AUDS;


typedef struct {
	uint8_t			ChunkName[4];		/* 'avih' */
	uint8_t			ChunkSize[4];

	uint8_t			microsec_per_frame[4];
	uint8_t			max_bytes_per_second[4];
	uint8_t			padding_granularity[4];
	uint8_t			flags[4];
	uint8_t			total_frames[4];	/* total number of frames in the 1st 'movi' chunk */
	uint8_t			init_frame[4];
	uint8_t			nb_streams[4];
	uint8_t			buffer_size[4];
	uint8_t			width[4];
	uint8_t			height[4];
	uint8_t			scale[4];
	uint8_t			rate[4];
	uint8_t			start[4];
	uint8_t			length[4];
} AVI_STREAM_AVIH;

typedef struct
{
	uint8_t			ChunkName[4];		/* 'LIST' */
	uint8_t			ChunkSize[4];

	uint8_t			Name[4];		/* 'hdrl' */
	AVI_STREAM_AVIH		Header;
} AVI_STREAM_LIST_AVIH;


typedef struct {
	uint8_t			ChunkName[4];		/* 'dmlh' */
	uint8_t			ChunkSize[4];

	uint8_t			total_frames[4];	/* total number of frames in the whole avi file */
	uint8_t			reserved[244];
} AVI_STREAM_DMLH;

typedef struct
{
	uint8_t			ChunkName[4];		/* 'LIST' */
	uint8_t			ChunkSize[4];

	uint8_t			Name[4];		/* 'odml' */
	AVI_STREAM_DMLH		Header;
} AVI_STREAM_LIST_ODML;


typedef struct {
	uint8_t			ChunkName[4];		/* 'ISFT' (software used) */
	uint8_t			ChunkSize[4];

//	uint8_t			Text[2];		/* Text's size should be multiple of 2 (including '\0') */
} AVI_STREAM_INFO;

typedef struct
{
	uint8_t			ChunkName[4];		/* 'LIST' */
	uint8_t			ChunkSize[4];

	uint8_t			Name[4];		/* 'INFO' */
	AVI_STREAM_INFO		Info;
} AVI_STREAM_LIST_INFO;


typedef struct
{
	uint8_t			ChunkName[4];		/* 'LIST' */
	uint8_t			ChunkSize[4];

	uint8_t			Name[4];		/* 'movi' */
} AVI_STREAM_LIST_MOVI;


typedef struct
{
  uint8_t		signature[4];				/* 'RIFF' */
  uint8_t		filesize[4];
  uint8_t		type[4];				/* 'AVI ' */
  
} RIFF_HEADER;


typedef struct {
  RIFF_HEADER			RiffHeader;

  AVI_STREAM_LIST_AVIH		AviHeader;
  
  AVI_STREAM_LIST_VIDS		VideoStream;
  AVI_STREAM_LIST_AUDS		AudioStream;

  AVI_STREAM_LIST_ODML  	Odml;

} AVI_FILE_HEADER;



#define	AUDIO_STREAM_WAVE_FORMAT_PCM		0x0001

#define	VIDEO_STREAM_RGB			0x00000000		/* fourcc for BMP video frames */
#define	VIDEO_STREAM_PNG			"MPNG"			/* fourcc for PNG video frames */

#define	AVIF_HASINDEX				0x00000010		/* index at the end of the file */
#define	AVIF_ISINTERLEAVED			0x00000100		/* data are interleaved */
#define	AVIF_TRUSTCKTYPE			0x00000800		/* trust chunk type */


#define	AVI_FRAME_INDEX_ALLOC_SIZE		50000			/* How many more entries to alloc each time pAviFrameIndex is full */
									/* We use 50000 (~800 KB) at a time to avoid allocating too often */
typedef struct {
  uint32_t	VideoFrame_Pos;
  uint32_t	VideoFrame_Length;
  uint32_t	AudioFrame_Pos;
  uint32_t	AudioFrame_Length;
} RECORD_AVI_FRAME_INDEX;



typedef struct {
  /* Input params to start recording */
  int		VideoCodec;
  int		VideoCodecCompressionLevel;					/* 0-9 for png compression */

  SDL_Surface	*Surface;

  int		CropLeft;
  int		CropRight;
  int		CropTop;
  int		CropBottom;

  int		Fps;					/* Fps << 24 */
  int		Fps_scale;				/* 1 << 24 */

  int		AudioCodec;
  int		AudioFreq;

  /* Internal data used by the avi recorder */
  int		Width;
  int		Height;
  int		BitCount;
  FILE		*FileOut;				/* file to write to */
  int		TotalVideoFrames;			/* number of recorded video frames */
  int		TotalAudioFrames;			/* number of recorded audio frames */
  int		TotalAudioSamples;			/* number of recorded audio samples */

  off_t		RiffChunkPosStart;			/* as returned by ftello() */
  off_t		MoviChunkPosStart;

  int		MoviChunkCount;				/* current 'movi' chunk nbr (0..n) */
  off_t		VideoFrames_Base_Offset;		/* for video indexes */
  off_t		AudioFrames_Base_Offset;		/* for audio indexes */

  off_t		SuperIndexChunk_Video_Pos;
  off_t		SuperIndexChunk_Audio_Pos;

  /* Internal video/audio index, written to file at the end of each 'movi' chunk */
  RECORD_AVI_FRAME_INDEX	*pAviFrameIndex;	/* array of max AviFrameIndex_AllocSize entries */
  int			AviFrameIndex_AllocSize;	/* Number of elements allocated in *pAviFrameIndex */
  int			AviFrameIndex_Count;		/* Number of elements used in *pAviFrameIndex (must be <AllocSize) */

} RECORD_AVI_PARAMS;


#define	AVI_MOVI_CHUNK_MAX_SIZE			( 1024 * 1024 * 1024 )	/* Max size in bytes of a 'movi' chunk : we take 1 GB */
							/* As we have 256 entries in the super index, this gives a max filesize of 256 GB */



bool		bRecordingAvi = false;


static RECORD_AVI_PARAMS	AviParams;
static AVI_FILE_HEADER		AviFileHeader;




static void	Avi_StoreU8 ( uint8_t *p , uint8_t val );
static void	Avi_StoreU16 ( uint8_t *p , uint16_t val );
static void	Avi_StoreU32 ( uint8_t *p , uint32_t val );
static void	Avi_StoreU64 ( uint8_t *p , uint64_t val );
static void	Avi_Store4cc ( uint8_t *p , const char *text );

static bool	Avi_FrameIndex_GrowIfNeeded ( RECORD_AVI_PARAMS *pAviParams );
static bool	Avi_FrameIndex_Free ( RECORD_AVI_PARAMS *pAviParams );
static bool	Avi_FrameIndex_Add ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader , int type , off_t Frame_Pos , int Frame_Length );

static bool	Avi_WriteMoviIndex ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader ,
				     int type , off_t *pPosition , int *pSize , int *pDuration );
static bool	Avi_WriteMoviAllIndexes ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader );

static bool	Avi_CloseMoviChunk ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader );
static bool	Avi_CreateNewMoviChunk ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader );

static int	Avi_GetBmpSize ( int Width , int Height , int BitCount );

static bool	Avi_RecordVideoStream_BMP ( RECORD_AVI_PARAMS *pAviParams );
#if HAVE_LIBPNG
static bool	Avi_RecordVideoStream_PNG ( RECORD_AVI_PARAMS *pAviParams );
#endif
static bool	Avi_RecordAudioStream_PCM ( RECORD_AVI_PARAMS *pAviParams , int16_t pSamples[][2], int SampleIndex, int SampleLength );

static void	Avi_BuildFileHeader ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader );

static bool	Avi_StartRecording_WithParams ( RECORD_AVI_PARAMS *pAviParams , char *AviFileName );
static bool	Avi_StopRecording_WithParams ( RECORD_AVI_PARAMS *pAviParams );



static void	Avi_StoreU8 ( uint8_t *p , uint8_t val )
{
	*p = val;
}


static void	Avi_StoreU16 ( uint8_t *p , uint16_t val )
{
	*p++ = val & 0xff;
	val >>= 8;
	*p = val & 0xff;
}


static void	Avi_StoreU32 ( uint8_t *p , uint32_t val )
{
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p = val & 0xff;
}


static void	Avi_StoreU64 ( uint8_t *p , uint64_t val )
{
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p = val & 0xff;
}


static void	Avi_Store4cc ( uint8_t *p , const char *text )
{
	memcpy ( p , text , 4 );
}




/*-----------------------------------------------------------------------*/
/**
 * Check if our internal index array is full or not (to add new index frames)
 * If the array is full, we extend it by allocating AVI_FRAME_INDEX_ALLOC_SIZE
 * new entries in the current array.
 */
static	bool	Avi_FrameIndex_GrowIfNeeded ( RECORD_AVI_PARAMS *pAviParams )
{
	void	*mem;


	if ( pAviParams->pAviFrameIndex == NULL )				/* Nothing allocated so far */
	{
		mem = malloc ( sizeof ( RECORD_AVI_FRAME_INDEX ) * AVI_FRAME_INDEX_ALLOC_SIZE );
		if ( mem == NULL )
			return false;
		pAviParams->AviFrameIndex_AllocSize = AVI_FRAME_INDEX_ALLOC_SIZE;
		pAviParams->AviFrameIndex_Count = 0;
	}
	else if ( pAviParams->AviFrameIndex_Count == pAviParams->AviFrameIndex_AllocSize )	/* Grow an existing array */
	{
		mem = realloc ( pAviParams->pAviFrameIndex ,
				sizeof ( RECORD_AVI_FRAME_INDEX ) * ( pAviParams->AviFrameIndex_AllocSize + AVI_FRAME_INDEX_ALLOC_SIZE ) );
		if ( mem == NULL )
			return false;
		pAviParams->AviFrameIndex_AllocSize += AVI_FRAME_INDEX_ALLOC_SIZE;
	}
	else
		return true;							/* Enough space for now */

	pAviParams->pAviFrameIndex = mem;
//fprintf ( stderr , "avi_grow2 max=%d cur=%d\n" , pAviParams->AviFrameIndex_AllocSize , pAviParams->AviFrameIndex_Count );
	return true;
}




/*-----------------------------------------------------------------------*/
/**
 * Free our internal index array
 */
static	bool	Avi_FrameIndex_Free ( RECORD_AVI_PARAMS *pAviParams )
{
	if ( pAviParams->pAviFrameIndex != NULL )
		free ( pAviParams->pAviFrameIndex );
	return true;
}



/*-----------------------------------------------------------------------*/
/**
 * Store the position / length of a frame in our internal index array
 * If 'type' = 0, we store a video frame, else we store an audio frame
 * If the last video frame exceed AVI_MOVI_CHUNK_MAX_SIZE, we create a new
 * 'movi' chunk to handle avi files > 4GB
 */
static	bool	Avi_FrameIndex_Add ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader , int type , off_t Frame_Pos , int Frame_Length )
{
//fprintf ( stderr , "avi_add type=%d pos=%ld length=%d count=%d %d %d\n" , type , Frame_Pos , Frame_Length , pAviParams->AviFrameIndex_Count , pAviParams->TotalVideoFrames , pAviParams->TotalAudioFrames );
	if ( Avi_FrameIndex_GrowIfNeeded ( pAviParams ) == false )
		return false;

	if ( type == 0 )							/* Video frame */
	{
		if ( pAviParams->AviFrameIndex_Count == 0 )			/* The 1st frame will be the base offset for all entries in the index */
			pAviParams->VideoFrames_Base_Offset = Frame_Pos;

		pAviParams->pAviFrameIndex[ pAviParams->AviFrameIndex_Count ].VideoFrame_Pos = (uint32_t)(Frame_Pos - pAviParams->VideoFrames_Base_Offset );
		pAviParams->pAviFrameIndex[ pAviParams->AviFrameIndex_Count ].VideoFrame_Length = Frame_Length;
	}
	else									/* Audio frame */
	{
		if ( pAviParams->AviFrameIndex_Count == 0 )			/* The 1st frame will be the base offset for all entries in the index */
			pAviParams->AudioFrames_Base_Offset = Frame_Pos;

		pAviParams->pAviFrameIndex[ pAviParams->AviFrameIndex_Count ].AudioFrame_Pos = (uint32_t)(Frame_Pos - pAviParams->AudioFrames_Base_Offset );
		pAviParams->pAviFrameIndex[ pAviParams->AviFrameIndex_Count ].AudioFrame_Length = Frame_Length;
	}

	/* If positions were stored for these audio and video frames, increment index counter for next frames */
	if ( pAviParams->TotalVideoFrames == pAviParams->TotalAudioFrames )
	{
		pAviParams->AviFrameIndex_Count++;

		/* If we exceed the size of a 'movi' chunk with the video frame we just added to the index, */
		/* we "close" it and we create a new 'movi' chunk */
		if ( pAviParams->pAviFrameIndex[ pAviParams->AviFrameIndex_Count - 1 ].VideoFrame_Pos > AVI_MOVI_CHUNK_MAX_SIZE )
			return Avi_CreateNewMoviChunk ( pAviParams , pAviFileHeader );
	}

	return true;
}



/*-----------------------------------------------------------------------*/
/**
 * Write one index (video or audio)
 * If 'type' = 0, we write a video index 'ix00', else we write an audio index 'ix01'.
 * We return the value for Position, Size and Duration that must be stored in the corresponding super index entry
 *  - for video super index, duration = entries_in_use in the video index (=AviFrameIndex_Count)
 *  - for audio super index, duration = sum of all AudioFrame_Length
 */
static	bool	Avi_WriteMoviIndex ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader ,
				     int type , off_t *pPosition , int *pSize , int *pDuration )
{
	AVI_STREAM_INDEX	IndexChunk;
	int			IndexChunk_Size;
	AVI_STREAM_INDEX_ENTRY	IndexEntry;
	int	i;


//fprintf ( stderr , "avi_write_index type=%d count=%d %d %d\n" , type , pAviParams->AviFrameIndex_Count , pAviParams->TotalVideoFrames , pAviParams->TotalAudioFrames );
	memset ( &IndexChunk , 0 , sizeof ( IndexChunk ) );

	*pPosition = ftello ( pAviParams->FileOut );

	/* Write the 'ix0#' chunk header */
	if ( type == 0 )							/* Video index */
	{
		Avi_Store4cc ( IndexChunk.ChunkName , "ix00" );
		if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
			Avi_Store4cc ( IndexChunk.chunk_id , "00db" );
		else if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
			Avi_Store4cc ( IndexChunk.chunk_id , "00dc" );
		Avi_StoreU64 ( IndexChunk.base_offset , pAviParams->VideoFrames_Base_Offset );
		*pDuration = pAviParams->AviFrameIndex_Count;			/* For video super index, duration=entries_in_use */
	}
	else									/* Audio index */
	{
		Avi_Store4cc ( IndexChunk.ChunkName , "ix01" );
		if ( pAviParams->AudioCodec == AVI_RECORD_AUDIO_CODEC_PCM )
			Avi_Store4cc ( IndexChunk.chunk_id , "01wb" );
		Avi_StoreU64 ( IndexChunk.base_offset , pAviParams->AudioFrames_Base_Offset );
		*pDuration = 0;
	}

	IndexChunk_Size = sizeof ( AVI_STREAM_INDEX ) + sizeof ( AVI_STREAM_INDEX_ENTRY ) * pAviParams->AviFrameIndex_Count - 8;
	Avi_StoreU32 ( IndexChunk.ChunkSize , IndexChunk_Size );
	*pSize = IndexChunk_Size+8;						/* For video super index */

	Avi_StoreU16 ( IndexChunk.longs_per_entry , 2 );
	Avi_StoreU8 ( &(IndexChunk.index_sub_type) , 0 );
	Avi_StoreU8 ( &(IndexChunk.index_type) , AVI_INDEX_OF_CHUNKS );
	Avi_StoreU32 ( IndexChunk.entries_in_use , pAviParams->AviFrameIndex_Count );

	/* Write the header */
	if ( fwrite ( &IndexChunk , sizeof ( AVI_STREAM_INDEX ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "Avi_WriteMoviIndex" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write index header" );
		return false;
	}

	/* Write the index array */
	for ( i=0 ; i<pAviParams->AviFrameIndex_Count ; i++ )
	{
		if ( type == 0 )
		{
			Avi_StoreU32 ( IndexEntry.offset , pAviParams->pAviFrameIndex[ i ].VideoFrame_Pos );
			Avi_StoreU32 ( IndexEntry.size , pAviParams->pAviFrameIndex[ i ].VideoFrame_Length );
		}
		else
		{
			Avi_StoreU32 ( IndexEntry.offset , pAviParams->pAviFrameIndex[ i ].AudioFrame_Pos );
			Avi_StoreU32 ( IndexEntry.size , pAviParams->pAviFrameIndex[ i ].AudioFrame_Length );
			*pDuration += pAviParams->pAviFrameIndex[ i ].AudioFrame_Length;	/* For audio super index, duration=sum of all audio frames length */
		}

		if ( fwrite ( &IndexEntry , sizeof ( IndexEntry ) , 1 , pAviParams->FileOut ) != 1 )
		{
			perror ( "Avi_WriteMoviIndex" );
			Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write index entry" );
			return false;
		}
	}

	return true;
}



/*-----------------------------------------------------------------------*/
/**
 * Write video and audio indexes at the current file position (after the
 * 'movi' data) and update the 2 super indexes in the avi header.
 */
static	bool	Avi_WriteMoviAllIndexes ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader )
{
	off_t	IndexPos;
	int	IndexSize;
	int	IndexDuration;


	/* Write video index + update super index */
	if ( Avi_WriteMoviIndex ( pAviParams , pAviFileHeader , 0 , &IndexPos , &IndexSize , &IndexDuration ) == false )
		return false;

	Avi_StoreU64 ( pAviFileHeader->VideoStream.SuperIndex.index[ pAviParams->MoviChunkCount ].offset , IndexPos );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.SuperIndex.index[ pAviParams->MoviChunkCount ].size , IndexSize );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.SuperIndex.index[ pAviParams->MoviChunkCount ].duration , IndexDuration );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.SuperIndex.entries_in_use , pAviParams->MoviChunkCount + 1 );


	/* Write audio index + update super index */
	if ( Avi_WriteMoviIndex ( pAviParams , pAviFileHeader , 1 , &IndexPos , &IndexSize , &IndexDuration ) == false )
		return false;

	Avi_StoreU64 ( pAviFileHeader->AudioStream.SuperIndex.index[ pAviParams->MoviChunkCount ].offset , IndexPos );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.SuperIndex.index[ pAviParams->MoviChunkCount ].size , IndexSize );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.SuperIndex.index[ pAviParams->MoviChunkCount ].duration , IndexDuration );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.SuperIndex.entries_in_use , pAviParams->MoviChunkCount + 1 );


	return true;
}




/*-----------------------------------------------------------------------*/
/**
 * Complete the current 'movi' chunk (when starting a new 'movi' chunk' or
 * when recording is stopped)
 */
static	bool	Avi_CloseMoviChunk ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader )
{
	off_t	Pos_End;
	uint8_t	TempSize[4];


//fprintf ( stderr , "avi_close_movi nb=%d fr=%d\n" , pAviParams->MoviChunkCount , pAviParams->TotalVideoFrames );
	/* Write the index chunks just after the 'movi' data */
	if ( Avi_WriteMoviAllIndexes ( pAviParams , pAviFileHeader ) == false )
	{
		return false;
	}


	Pos_End = ftello ( pAviParams->FileOut );

	/* Update the size of the 'movi' chunk (including the indexes) */
	Avi_StoreU32 ( TempSize , Pos_End - pAviParams->MoviChunkPosStart - 8 );
	if ( fseeko ( pAviParams->FileOut , pAviParams->MoviChunkPosStart+4 , SEEK_SET ) != 0 )
	{
		perror ( "Avi_CloseMoviChunk" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to seek to movi start" );
		return false;
	}
	if ( fwrite ( TempSize , sizeof ( TempSize ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "Avi_CloseMoviChunk" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write movi size" );
		return false;
	}


	Avi_StoreU32 ( pAviFileHeader->Odml.Header.total_frames , pAviParams->TotalVideoFrames );	/* number of video frames */

	/* If an AVI file has more than 1 'movi' chunk (to support >4 GB file), then AVI header */
	/* should be updated with only the information of the 1st chunk (to keep a standard non-odml AVI header) */
	if ( pAviParams->MoviChunkCount == 0 )
	{
		Avi_StoreU32 ( pAviFileHeader->RiffHeader.filesize , Pos_End - 8 );	/* 32 bits, limited to 4GB */
		Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.total_frames , pAviParams->TotalVideoFrames );	/* number of video frames */
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.data_length , pAviParams->TotalVideoFrames );	/* number of video frames */
		Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.data_length , pAviParams->TotalAudioSamples );/* number of audio samples */
	}

	/* For 'riff' / 'movi' chunks 2 ... n */
	else
	{
		Avi_StoreU32 ( TempSize , (uint32_t)(Pos_End - pAviParams->RiffChunkPosStart - 8 ) );
		if ( fseeko ( pAviParams->FileOut , pAviParams->RiffChunkPosStart+4 , SEEK_SET ) != 0 )
		{
			perror ( "Avi_CloseMoviChunk" );
			Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to seek to riff start" );
			return false;
		}
		if ( fwrite ( TempSize , sizeof ( TempSize ) , 1 , pAviParams->FileOut ) != 1 )
		{
			perror ( "Avi_CloseMoviChunk" );
			Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write riff size" );
			return false;
		}
	}


	if ( fseeko ( pAviParams->FileOut , 0 , SEEK_END ) != 0 )
	{
		perror ( "Avi_CloseMoviChunk" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to seek to end of file" );
		return false;
	}

	return true;
}




/*-----------------------------------------------------------------------*/
/**
 * Create a new 'movi' chunk. This uses the ODML extended AVIX chunk to
 * record several 'movi' chunk in a single avi file (allowing to handle
 * files > 4GB).
 */
static	bool	Avi_CreateNewMoviChunk ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader )
{
	RIFF_HEADER		RiffHeader;
	AVI_STREAM_LIST_MOVI	ListMovi;


	/* Complete current 'movi' chunk + write indexes */
	if ( Avi_CloseMoviChunk ( pAviParams , pAviFileHeader ) == false )
	{
		return false;
	}

	pAviParams->MoviChunkCount++;
	pAviParams->AviFrameIndex_Count = 0;
//fprintf ( stderr , "avi_create_movi nb=%d fr=%d\n" , pAviParams->MoviChunkCount , pAviParams->TotalVideoFrames );

	/* Write a new RIFF / AVIX header */
	Avi_Store4cc ( RiffHeader.signature , "RIFF" );
	Avi_StoreU32 ( RiffHeader.filesize , 0 );				/* completed when closing this chunk */
	Avi_Store4cc ( RiffHeader.type , "AVIX" );
	pAviParams->RiffChunkPosStart = ftello ( pAviParams->FileOut );
	if ( fwrite ( &RiffHeader , sizeof ( RiffHeader ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "Avi_CreateNewMoviChunk" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write next riff header" );
		return false;
	}

	/* Write a new 'movi' header */
	Avi_Store4cc ( ListMovi.ChunkName , "LIST" );
	Avi_StoreU32 ( ListMovi.ChunkSize , 0 );				/* completed when closing this chunk */
	Avi_Store4cc ( ListMovi.Name , "movi" );
	pAviParams->MoviChunkPosStart = ftello ( pAviParams->FileOut );
	if ( fwrite ( &ListMovi , sizeof ( ListMovi ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "Avi_CreateNewMoviChunk" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write next movi header" );
		return false;
	}

	return true;
}





static int	Avi_GetBmpSize ( int Width , int Height , int BitCount )
{
	return ( Width * Height * BitCount / 8 );						/* bytes in one video frame */
}


static bool	Avi_RecordVideoStream_BMP ( RECORD_AVI_PARAMS *pAviParams )
{
	AVI_CHUNK	Chunk;
	int		SizeImage;
	uint8_t		*pBitmapIn , *pBitmapOut;
	int		y, src_y;
	int		NeedLock;
	uint8_t		*LineBuf = alloca(3 * pAviParams->Width);		/* temp buffer to convert to 24-bit BGR format */

	assert(pAviParams->Surface->format->BytesPerPixel == 4);

	SizeImage = Avi_GetBmpSize ( pAviParams->Width , pAviParams->Height , pAviParams->BitCount );

	/* Write the video frame header */
	Avi_Store4cc ( Chunk.ChunkName , "00db" );				/* stream 0, uncompressed DIB bytes */
	Avi_StoreU32 ( Chunk.ChunkSize , SizeImage );				/* max size of RGB image */
	if ( fwrite ( &Chunk , sizeof ( Chunk ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "Avi_RecordVideoStream_BMP" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write bmp frame header" );
		return false;
	}


	/* Write the video frame data */
	NeedLock = SDL_MUSTLOCK( pAviParams->Surface );

	for ( y=0 ; y<pAviParams->Height ; y++ )
	{
		if ( NeedLock )
			SDL_LockSurface ( pAviParams->Surface );

		/* Points to the top left pixel after cropping borders. For BMP
		 * format, frame is stored from bottom to top (origin is in
		 * bottom left corner) and bytes are in BGR order (not RGB) */
		src_y = pAviParams->Surface->h - 1 - pAviParams->CropTop - pAviParams->CropBottom;
		src_y = src_y - (y * (src_y + 1) + pAviParams->Height/2) / pAviParams->Height;
		pBitmapIn = (uint8_t *)pAviParams->Surface->pixels
			+ pAviParams->Surface->pitch * src_y
			+ pAviParams->CropLeft * pAviParams->Surface->format->BytesPerPixel;

		pBitmapOut = LineBuf;
		PixelConvert_32to24Bits_BGR(LineBuf, (uint32_t *)pBitmapIn, pAviParams->Width, pAviParams->Surface);

		if ( NeedLock )
			SDL_UnlockSurface ( pAviParams->Surface );

		if ( (int)fwrite ( pBitmapOut , 1 , pAviParams->Width*3 , pAviParams->FileOut ) != pAviParams->Width*3 )
		{
			perror ( "Avi_RecordVideoStream_BMP" );
			Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write bmp video frame" );
			return false;
		}
	}

	return true;
}



#if HAVE_LIBPNG
static bool	Avi_RecordVideoStream_PNG ( RECORD_AVI_PARAMS *pAviParams )
{
	AVI_CHUNK	Chunk;
	int		SizeImage;
	off_t		ChunkPos;
	uint8_t	TempSize[4];
	

	/* Write the video frame header */
	ChunkPos = ftello ( pAviParams->FileOut );
	Avi_Store4cc ( Chunk.ChunkName , "00dc" );				/* stream 0, compressed DIB bytes */
	Avi_StoreU32 ( Chunk.ChunkSize , 0 );					/* size of PNG image (-> completed later) */
	if ( fwrite ( &Chunk , sizeof ( Chunk ) , 1 , pAviParams->FileOut ) != 1 )
		goto png_error;

	/* Write the video frame data */
	SizeImage = ScreenSnapShot_SavePNG_ToFile(pAviParams->Surface,
		pAviParams->Width, pAviParams->Height, pAviParams->FileOut,
		pAviParams->VideoCodecCompressionLevel , PNG_FILTER_NONE ,
		pAviParams->CropLeft , pAviParams->CropRight , pAviParams->CropTop , pAviParams->CropBottom );
	if ( SizeImage <= 0 )
		goto png_error;

	/* Update the size of the video chunk */
	Avi_StoreU32 ( TempSize , SizeImage );
	if ( fseeko ( pAviParams->FileOut , ChunkPos+4 , SEEK_SET ) != 0 )
		goto png_error;
	if ( fwrite ( TempSize , sizeof ( TempSize ) , 1 , pAviParams->FileOut ) != 1 )
		goto png_error;

	/* Go to the end of the video frame data */
	if ( fseeko ( pAviParams->FileOut , 0 , SEEK_END ) != 0 )
		goto png_error;
	return true;

png_error:
	perror ( "Avi_RecordVideoStream_PNG" );
	Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write png frame" );
	return false;
}
#endif  /* HAVE_LIBPNG */



bool	Avi_RecordVideoStream ( void )
{
	off_t		Pos_Start , Pos_End;

	Pos_Start = ftello ( AviParams.FileOut );

	if ( AviParams.VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
	{
		if ( Avi_RecordVideoStream_BMP ( &AviParams ) == false )
		{
			return false;
		}
	}
#if HAVE_LIBPNG
	else if ( AviParams.VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
	{
		if ( Avi_RecordVideoStream_PNG ( &AviParams ) == false )
		{
			return false;
		}
	}
#endif
	else
	{
		return false;
	}

	Pos_End = ftello ( AviParams.FileOut );
	AviParams.TotalVideoFrames++;

	/* Store index for this video frame */
	Pos_Start += 8;								/* skip header */
	if ( Avi_FrameIndex_Add ( &AviParams , &AviFileHeader , 0 , Pos_Start , (uint32_t)( Pos_End - Pos_Start ) ) == false )
		return false;

	if (AviParams.TotalVideoFrames % ( AviParams.Fps / AviParams.Fps_scale ) == 0)
	{
		char str[20];
		int secs , hours , mins;

		secs = AviParams.TotalVideoFrames / ( AviParams.Fps / AviParams.Fps_scale );
		hours = secs / 3600;
		mins = ( secs % 3600 ) / 60;
		secs = secs % 60;
		snprintf ( str , 20 , "%d:%02d:%02d" , hours , mins , secs );
		Main_SetTitle(str);
	}
	return true;
}



static bool	Avi_RecordAudioStream_PCM ( RECORD_AVI_PARAMS *pAviParams , int16_t pSamples[][2] , int SampleIndex , int SampleLength )
{
	AVI_CHUNK	Chunk;
	int16_t		sample[2];
	int		i;
	int		idx;

	/* Write the audio frame header */
	Avi_Store4cc ( Chunk.ChunkName , "01wb" );				/* stream 1, wave bytes */
	Avi_StoreU32 ( Chunk.ChunkSize , SampleLength * 4 );			/* 16 bits, stereo -> 4 bytes */
	if ( fwrite ( &Chunk , sizeof ( Chunk ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "Avi_RecordAudioStream_PCM" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write pcm frame header" );
		return false;
	}

	/* Write the audio frame data */
	idx = SampleIndex & AUDIOMIXBUFFER_SIZE_MASK;
	for ( i = 0 ; i < SampleLength; i++ )
	{
		/* Convert sample to little endian */
		sample[0] = SDL_SwapLE16 ( pSamples[ idx ][0]);
		sample[1] = SDL_SwapLE16 ( pSamples[ idx ][1]);
		idx = ( idx+1 ) & AUDIOMIXBUFFER_SIZE_MASK;
		/* And store */
		if ( fwrite ( &sample , sizeof ( sample ) , 1 , pAviParams->FileOut ) != 1 )
		{
			perror ( "Avi_RecordAudioStream_PCM" );
			Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write pcm frame" );
			return false;
		}
	}

	return true;
}



bool	Avi_RecordAudioStream ( int16_t pSamples[][2] , int SampleIndex , int SampleLength )
{
	off_t		Pos_Start , Pos_End;

	Pos_Start = ftello ( AviParams.FileOut );

	if ( AviParams.AudioCodec == AVI_RECORD_AUDIO_CODEC_PCM )
	{
		if ( Avi_RecordAudioStream_PCM ( &AviParams , pSamples , SampleIndex , SampleLength ) == false )
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	Pos_End = ftello ( AviParams.FileOut );
	AviParams.TotalAudioFrames++;
	AviParams.TotalAudioSamples += SampleLength;

	/* Store index for this audio frame */
	Pos_Start += 8;								/* skip header */
	if ( Avi_FrameIndex_Add ( &AviParams , &AviFileHeader , 1 , Pos_Start , (uint32_t)( Pos_End - Pos_Start ) ) == false )
		return false;

	return true;
}




static void	Avi_BuildFileHeader ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader )
{
	int	Width , Height , BitCount , Fps , Fps_scale , SizeImage;
	int	AudioFreq;

	memset ( pAviFileHeader , 0 , sizeof ( *pAviFileHeader ) );

	Width = pAviParams->Width;
	Height =pAviParams->Height;
	BitCount = pAviParams->BitCount;
	Fps = pAviParams->Fps;
	Fps_scale = pAviParams->Fps_scale;
	AudioFreq = pAviParams->AudioFreq;

	SizeImage = 0;
	if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
		SizeImage = Avi_GetBmpSize ( Width , Height , BitCount );			/* size of a BMP image */
	else if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
		SizeImage = Avi_GetBmpSize ( Width , Height , BitCount );			/* max size of a PNG image */


	/* RIFF / AVI headers */
	Avi_Store4cc ( pAviFileHeader->RiffHeader.signature , "RIFF" );
	Avi_StoreU32 ( pAviFileHeader->RiffHeader.filesize , 0 );				/* total file size (-> completed later) */
	Avi_Store4cc ( pAviFileHeader->RiffHeader.type , "AVI " );
	pAviParams->RiffChunkPosStart = 0;

	Avi_Store4cc ( pAviFileHeader->AviHeader.ChunkName , "LIST" );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.ChunkSize , sizeof ( AVI_STREAM_LIST_AVIH )
		+ sizeof ( AVI_STREAM_LIST_VIDS ) + sizeof ( AVI_STREAM_LIST_AUDS ) - 8 );
	Avi_Store4cc ( pAviFileHeader->AviHeader.Name , "hdrl" );

	Avi_Store4cc ( pAviFileHeader->AviHeader.Header.ChunkName , "avih" );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.ChunkSize , sizeof ( AVI_STREAM_AVIH ) - 8 );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.microsec_per_frame , (uint32_t)( ( 1000000 * (int64_t)Fps_scale ) / Fps ) );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.max_bytes_per_second , (uint32_t)( ( (int64_t)SizeImage * Fps ) / Fps_scale + AudioFreq * 4 ) );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.padding_granularity , 0 );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.flags , AVIF_HASINDEX | AVIF_ISINTERLEAVED | AVIF_TRUSTCKTYPE );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.total_frames , 0 );			/* number of video frames (-> completed later) */
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.init_frame , 0 );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.nb_streams , 2 );			/* 1 video and 1 audio */
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.buffer_size , SizeImage );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.width , Width );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.height , Height );
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.scale , 0 );				/* reserved */
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.rate , 0 );				/* reserved */
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.start , 0 );				/* reserved */
	Avi_StoreU32 ( pAviFileHeader->AviHeader.Header.length , 0 );				/* reserved */


	/* Video Stream : strl ( strh + strf + indx ) */
	Avi_Store4cc ( pAviFileHeader->VideoStream.ChunkName , "LIST" );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.ChunkSize , sizeof ( AVI_STREAM_LIST_VIDS ) - 8 );
	Avi_Store4cc ( pAviFileHeader->VideoStream.Name , "strl" );

	Avi_Store4cc ( pAviFileHeader->VideoStream.Header.ChunkName , "strh" );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.ChunkSize , sizeof ( AVI_STREAM_HEADER ) - 8 );
	Avi_Store4cc ( pAviFileHeader->VideoStream.Header.stream_type , "vids" );
	if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.stream_handler , VIDEO_STREAM_RGB );
	else if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
		Avi_Store4cc ( pAviFileHeader->VideoStream.Header.stream_handler , VIDEO_STREAM_PNG );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.flags , 0 );
	Avi_StoreU16 ( pAviFileHeader->VideoStream.Header.priority , 0 );
	Avi_StoreU16 ( pAviFileHeader->VideoStream.Header.language , 0 );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.initial_frames , 0 );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.time_scale , Fps_scale );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.data_rate , Fps );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.start_time , 0 );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.data_length , 0 );			/* number of video frames (-> completed later) */
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.buffer_size , SizeImage );		/* size of an uncompressed frame */
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.quality , -1 );			/* use default quality */
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Header.sample_size , 0 );			/* 0 for video */
	Avi_StoreU16 ( pAviFileHeader->VideoStream.Header.dest_left , 0 );
	Avi_StoreU16 ( pAviFileHeader->VideoStream.Header.dest_top , 0 );
	Avi_StoreU16 ( pAviFileHeader->VideoStream.Header.dest_right , Width );
	Avi_StoreU16 ( pAviFileHeader->VideoStream.Header.dest_bottom , Height );

	Avi_Store4cc ( pAviFileHeader->VideoStream.Format.ChunkName , "strf" );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.ChunkSize , sizeof ( AVI_STREAM_FORMAT_VIDS ) - 8 );
	if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
	{
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.size , sizeof ( AVI_STREAM_FORMAT_VIDS ) - 8 );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.width , Width );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.height , Height );
		Avi_StoreU16 ( pAviFileHeader->VideoStream.Format.planes , 1 );			/* always 1 */
		Avi_StoreU16 ( pAviFileHeader->VideoStream.Format.bit_count , BitCount );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.compression , VIDEO_STREAM_RGB );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.size_image , SizeImage );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.xpels_meter , 0 );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.ypels_meter , 0 );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.clr_used , 0 );		/* no color map */
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.clr_important , 0 );		/* no color map */
	}
	else if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
	{
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.size , sizeof ( AVI_STREAM_FORMAT_VIDS ) - 8 );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.width , Width );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.height , Height );
		Avi_StoreU16 ( pAviFileHeader->VideoStream.Format.planes , 1 );			/* always 1 */
		Avi_StoreU16 ( pAviFileHeader->VideoStream.Format.bit_count , BitCount );
		Avi_Store4cc ( pAviFileHeader->VideoStream.Format.compression , VIDEO_STREAM_PNG );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.size_image , SizeImage );	/* max size if uncompressed */
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.xpels_meter , 0 );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.ypels_meter , 0 );
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.clr_used , 0 );		/* no color map */
		Avi_StoreU32 ( pAviFileHeader->VideoStream.Format.clr_important , 0 );		/* no color map */
	}

	Avi_Store4cc ( pAviFileHeader->VideoStream.SuperIndex.ChunkName , "indx" );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.SuperIndex.ChunkSize , sizeof ( AVI_STREAM_SUPER_INDEX ) - 8 );
	Avi_StoreU16 ( pAviFileHeader->VideoStream.SuperIndex.longs_per_entry , 4 );
	Avi_StoreU8 ( &(pAviFileHeader->VideoStream.SuperIndex.index_sub_type) , 0 );
	Avi_StoreU8 ( &(pAviFileHeader->VideoStream.SuperIndex.index_type) , AVI_INDEX_OF_INDEXES );
	Avi_StoreU32 ( pAviFileHeader->VideoStream.SuperIndex.entries_in_use , 0 );		/* number of entries (-> completed later) */
	if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
		Avi_Store4cc ( pAviFileHeader->VideoStream.SuperIndex.chunk_id , "00db" );
	else if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
		Avi_Store4cc ( pAviFileHeader->VideoStream.SuperIndex.chunk_id , "00dc" );


	/* Audio Stream  : strl ( strh + strf + indx ) */
	Avi_Store4cc ( pAviFileHeader->AudioStream.ChunkName , "LIST" );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.ChunkSize , sizeof ( AVI_STREAM_LIST_AUDS ) - 8 );
	Avi_Store4cc ( pAviFileHeader->AudioStream.Name , "strl" );

	Avi_Store4cc ( pAviFileHeader->AudioStream.Header.ChunkName , "strh" );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.ChunkSize , sizeof ( AVI_STREAM_HEADER ) - 8 );
	Avi_Store4cc ( pAviFileHeader->AudioStream.Header.stream_type , "auds" );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.stream_handler , 0 );			/* not used (or could be 1 for pcm ?) */
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.flags , 0 );
	Avi_StoreU16 ( pAviFileHeader->AudioStream.Header.priority , 0 );
	Avi_StoreU16 ( pAviFileHeader->AudioStream.Header.language , 0 );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.initial_frames , 0 );			/* should be 1 in interleaved ? */
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.time_scale , 1 );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.data_rate , AudioFreq );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.start_time , 0 );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.data_length , 0 );			/* number of audio samples (-> completed later) */
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.buffer_size , AudioFreq * 4 / 50 );	/* min VBL freq is 50 Hz */
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.quality , -1 );			/* use default quality */
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Header.sample_size , 4 );			/* 2 bytes, stereo */
	Avi_StoreU16 ( pAviFileHeader->AudioStream.Header.dest_left , 0 );
	Avi_StoreU16 ( pAviFileHeader->AudioStream.Header.dest_top , 0 );
	Avi_StoreU16 ( pAviFileHeader->AudioStream.Header.dest_right , 0 );
	Avi_StoreU16 ( pAviFileHeader->AudioStream.Header.dest_bottom , 0 );

	Avi_Store4cc ( pAviFileHeader->AudioStream.Format.ChunkName , "strf" );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.Format.ChunkSize , sizeof ( AVI_STREAM_FORMAT_AUDS ) - 8 );
	if ( pAviParams->AudioCodec == AVI_RECORD_AUDIO_CODEC_PCM )				/* 16 bits stereo pcm */
	{
		Avi_StoreU16 ( pAviFileHeader->AudioStream.Format.codec , AUDIO_STREAM_WAVE_FORMAT_PCM );	/* 0x0001 */
		Avi_StoreU16 ( pAviFileHeader->AudioStream.Format.channels , 2 );
		Avi_StoreU32 ( pAviFileHeader->AudioStream.Format.sample_rate , AudioFreq );
		Avi_StoreU32 ( pAviFileHeader->AudioStream.Format.bit_rate , AudioFreq * 2 * 2 );	/* 2 channels * 2 bytes */
		Avi_StoreU16 ( pAviFileHeader->AudioStream.Format.block_align , 4 );
		Avi_StoreU16 ( pAviFileHeader->AudioStream.Format.bits_per_sample , 16 );
		Avi_StoreU16 ( pAviFileHeader->AudioStream.Format.ext_size , 0 );
	}

	Avi_Store4cc ( pAviFileHeader->AudioStream.SuperIndex.ChunkName , "indx" );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.SuperIndex.ChunkSize , sizeof ( AVI_STREAM_SUPER_INDEX ) - 8 );
	Avi_StoreU16 ( pAviFileHeader->AudioStream.SuperIndex.longs_per_entry , 4 );
	Avi_StoreU8 ( &(pAviFileHeader->AudioStream.SuperIndex.index_sub_type) , 0 );
	Avi_StoreU8 ( &(pAviFileHeader->AudioStream.SuperIndex.index_type) , AVI_INDEX_OF_INDEXES );
	Avi_StoreU32 ( pAviFileHeader->AudioStream.SuperIndex.entries_in_use , 0 );			/* number of entries (-> completed later) */
	if ( pAviParams->AudioCodec == AVI_RECORD_AUDIO_CODEC_PCM )				/* 16 bits stereo pcm */
		Avi_Store4cc ( pAviFileHeader->AudioStream.SuperIndex.chunk_id , "01wb" );


	/* ODML infos */
	Avi_Store4cc ( pAviFileHeader->Odml.ChunkName , "LIST" );
	Avi_StoreU32 ( pAviFileHeader->Odml.ChunkSize , sizeof ( AVI_STREAM_LIST_ODML ) - 8 );
	Avi_Store4cc ( pAviFileHeader->Odml.Name , "odml" );

	Avi_Store4cc ( pAviFileHeader->Odml.Header.ChunkName , "dmlh" );
	Avi_StoreU32 ( pAviFileHeader->Odml.Header.ChunkSize , sizeof ( AVI_STREAM_DMLH ) - 8 );
	Avi_StoreU32 ( pAviFileHeader->Odml.Header.total_frames , 0 );				/* number of video frames (-> completed later) */
}




static bool	Avi_StartRecording_WithParams ( RECORD_AVI_PARAMS *pAviParams , char *AviFileName )
{
	AVI_STREAM_LIST_INFO	ListInfo;
	char			InfoString[ 100 ];
	int			Len , Len_rounded;
	AVI_STREAM_LIST_MOVI	ListMovi;


	if ( bRecordingAvi == true )						/* already recording ? */
		return false;

	/* Compute some video parameters */
	pAviParams->Width = pAviParams->Surface->w - pAviParams->CropLeft - pAviParams->CropRight;
	pAviParams->Height = pAviParams->Surface->h - pAviParams->CropTop - pAviParams->CropBottom;
	pAviParams->BitCount = 24;
	
#if !HAVE_LIBPNG
	if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : Hatari was not built with libpng support" );
		return false;
	}
#endif

	/* Open the file */
	pAviParams->FileOut = fopen ( AviFileName , "wb+" );
	if ( !pAviParams->FileOut )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to open file" );
		return false;
	}

	/* Alloc memory to store frames' index */
	if ( Avi_FrameIndex_GrowIfNeeded ( pAviParams ) == false )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to alloc index memory" );
		return false;
	}

	/* Build the AVI header */
	Avi_BuildFileHeader ( pAviParams , &AviFileHeader );
	
	/* Write the AVI header */
	if ( fwrite ( &AviFileHeader , sizeof ( AviFileHeader ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write avi header" );
		return false;
	}

	/* Write the INFO header */
	memset ( InfoString , 0 , sizeof ( InfoString ) );
	Len = snprintf ( InfoString , sizeof ( InfoString ) , "%s - the Atari ST, STE, TT and Falcon emulator" , PROG_NAME ) + 1;
	Len_rounded = Len + ( Len % 2 == 0 ? 0 : 1 );				/* round Len to the next multiple of 2 */
	Avi_Store4cc ( ListInfo.ChunkName , "LIST" );
	Avi_StoreU32 ( ListInfo.ChunkSize , sizeof ( AVI_STREAM_LIST_INFO ) - 8 + Len_rounded );
	Avi_Store4cc ( ListInfo.Name , "INFO" );
	Avi_Store4cc ( ListInfo.Info.ChunkName , "ISFT" );
	Avi_StoreU32 ( ListInfo.Info.ChunkSize , Len );
	if ( fwrite ( &ListInfo , sizeof ( ListInfo ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write info header" );
		return false;
	}
	/* Write the info string + '\0' and write an optional extra '\0' byte to get a total multiple of 2 */
	if ( fwrite ( InfoString , Len_rounded , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write info header" );
		return false;
	}

	/* Write the MOVI header */
	Avi_Store4cc ( ListMovi.ChunkName , "LIST" );
	Avi_StoreU32 ( ListMovi.ChunkSize , 0 );				/* completed when recording stops */
	Avi_Store4cc ( ListMovi.Name , "movi" );
	pAviParams->MoviChunkPosStart = ftello ( pAviParams->FileOut );
	if ( fwrite ( &ListMovi , sizeof ( ListMovi ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write movi header" );
		return false;
	}


	/* We're ok to record */
	Log_AlertDlg ( LOG_INFO, "AVI recording has been started");
	bRecordingAvi = true;

	return true;
}



static bool	Avi_StopRecording_WithParams ( RECORD_AVI_PARAMS *pAviParams )
{
	if ( bRecordingAvi == false )						/* no recording ? */
		return true;


	/* Complete the current 'movi' chunk */
	if ( Avi_CloseMoviChunk ( pAviParams , &AviFileHeader ) == false )
		goto stoprec_error;

	/* Write the updated AVI header */
	if ( fseeko ( pAviParams->FileOut , 0 , SEEK_SET ) != 0 )
		goto stoprec_error;
	if ( fwrite ( &AviFileHeader , sizeof ( AviFileHeader ) , 1 , pAviParams->FileOut ) != 1 )
		goto stoprec_error;

	/* Close the file */
	fclose ( pAviParams->FileOut );

	/* Free index' memory */
	Avi_FrameIndex_Free ( pAviParams );

	Log_AlertDlg ( LOG_INFO, "AVI recording has been stopped");
	bRecordingAvi = false;

	return true;

stoprec_error:
	fclose (pAviParams->FileOut);
	Avi_FrameIndex_Free ( pAviParams );
	perror("AviStopRecording");
	Log_AlertDlg(LOG_ERROR, "AVI recording : failed to update header");
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Are we recording an AVI ?
 */
bool	Avi_AreWeRecording ( void )
{
        return bRecordingAvi;
}


/* PNG compression level, 0-9 */
static int compression_level = 9;

/**
 * Set recording level from given string
 * return true for valid, false for invalid value
 */
bool Avi_SetCompressionLevel(const char *str)
{
	char *end;
	long level = strtol(str, &end, 10);
	if (*end)
		return false;
	if (level < 0 || level > 9)
		return false;
	compression_level = level;
	return true;
}


bool	Avi_StartRecording ( char *FileName , bool CropGui , uint32_t Fps , uint32_t Fps_scale , int VideoCodec )
{
	memset ( &AviParams , 0 , sizeof ( AviParams ) );

	AviParams.VideoCodec = VideoCodec;
	AviParams.VideoCodecCompressionLevel = compression_level;
	AviParams.AudioCodec = AVI_RECORD_AUDIO_CODEC_PCM;
	AviParams.AudioFreq = ConfigureParams.Sound.nPlaybackFreq;
	AviParams.Surface = sdlscrn;

	/* Some video players (quicktime, ...) don't support a value of Fps_scale */
	/* above 100000. So we decrease the precision from << 24 to << 16 for Fps and Fps_scale */
	AviParams.Fps = Fps >> 8;			/* refresh rate << 16 */
	AviParams.Fps_scale = Fps_scale >> 8;		/* 1 << 16 */

	if ( !CropGui )					/* Keep gui's status bar */
	{
		AviParams.CropLeft = 0;
		AviParams.CropRight = 0;
		AviParams.CropTop = 0;
		AviParams.CropBottom = 0;
	}
	else						/* Record only the content of the Atari's screen */
	{
		AviParams.CropLeft = 0;
		AviParams.CropRight = 0;
		AviParams.CropTop = 0;
		AviParams.CropBottom = Statusbar_GetHeight();
	}
	
	
	if (Avi_StartRecording_WithParams ( &AviParams , FileName ))
	{
		Main_SetTitle("00:00");
		return true;
	}
	return false;
}

void Avi_SetSurface(SDL_Surface *surf)
{
	AviParams.Surface = surf;
}

bool	Avi_StopRecording ( void )
{
	if (Avi_StopRecording_WithParams ( &AviParams ))
	{
		Main_SetTitle(NULL);
		return true;
	}
	return false;
}


