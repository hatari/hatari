/*
  Hatari - avi_record.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  AVI File recording

  This allows Hatari to record a video file, with both video and audio
  streams, at full frame rate.
  
  Video frames are saved using the current video frequency of the emulated
  machine (50 Hz, 60 Hz, 70 Hz, ...). Frames can be stored using different
  codecs. So far, supported codecs are :
   - BMP : uncompressed RGB images. Very fast to save, very few cpu needed
     but requires a lot of disk bandwidth and a lot of space.
   - PNG : compressed RBG images. Depending on the compression level, this
     can require more cpu and could slow down Hatari. As compressed images
     are much smaller than BMP images, this will require less space on disk
     and much less disk bandwidth. Compression levels 3 or 4 give could
     tradeoff between cpu usage and file size and should not slow down Hatari
     with recent computers.

  PNG compression will often give a x20 ratio when compared to BMP and should
  be used if you have a powerful enough cpu.

  Sound is saved as 16 bits pcm stereo, using the current Hatari sound output
  frequency. For best accuracy, sound frequency should be a multiple of the
  video frequency ; this means 44.1 kHz is the best choice for 50/60 Hz video.

  The AVI file is divided into multiple chunks. Hatari will save one video stream
  and one audio stream, so the overall strucutre of the file is the following :

  RIFF avi
      LIST
	hdrl
	  avih
	  LIST
	    strl
	      strh (vids)
	      strf
	  LIST
	    strl
	      strh (auds)
	      strf
      LIST
	INFO
      LIST
	movi
	  00db
	  01wb
	  ...
      idx1
*/

const char AVIRecord_fileid[] = "Hatari avi_record.c : " __DATE__ " " __TIME__;

#include <SDL.h>
#include <SDL_endian.h>

#include "main.h"
#include "audio.h"
#include "configuration.h"
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


typedef struct
{
	Uint8			ChunkName[4];		/* '00db', '01wb', 'idx1' */
	Uint8			ChunkSize[4];
} AVI_CHUNK;


typedef struct
{
	Uint8			identifier[4];		/* '00db', '01wb', 'idx1' */
	Uint8			flags[4];
	Uint8			offset[4];
	Uint8			length[4];
} AVI_CHUNK_INDEX;


typedef struct
{
	Uint8			ChunkName[4];		/* 'strh' */
	Uint8			ChunkSize[4];

	Uint8			stream_type[4];		/* 'vids' or 'auds' */
	Uint8			stream_handler[4];
	Uint8			flags[4];
	Uint8			priority[2];
	Uint8			language[2];
	Uint8			initial_frames[4];
	Uint8			time_scale[4];
	Uint8			data_rate[4];
	Uint8			start_time[4];
	Uint8			data_length[4];
	Uint8			buffer_size[4];
	Uint8			quality[4];
	Uint8			sample_size[4];
	Uint8			dest_left[2];
	Uint8			dest_top[2];
	Uint8			dest_right[2];
	Uint8			dest_bottom[2];
} AVI_STREAM_HEADER;


typedef struct
{
	Uint8			ChunkName[4];		/* 'strf' */
	Uint8			ChunkSize[4];

	Uint8			size[4];
	Uint8			width[4];
	Uint8			height[4];
	Uint8			planes[2];
	Uint8			bit_count[2];
	Uint8			compression[4];
	Uint8			size_image[4];
	Uint8			xpels_meter[4];
	Uint8			ypels_meter[4];
	Uint8			clr_used[4];
	Uint8			clr_important[4];
} AVI_STREAM_FORMAT_VIDS;

typedef struct
{
	Uint8			ChunkName[4];		/* 'LIST' */
	Uint8			ChunkSize[4];

	Uint8			Name[4];		/* 'strl' */
	AVI_STREAM_HEADER	Header;			/* 'strh' */
	AVI_STREAM_FORMAT_VIDS	Format;			/* 'strf' */
} AVI_STREAM_LIST_VIDS;


typedef struct
{
	Uint8			ChunkName[4];		/* 'strf' */
	Uint8			ChunkSize[4];

	Uint8			codec[2];
	Uint8			channels[2];
	Uint8			sample_rate[4];
	Uint8			bit_rate[4];
	Uint8			block_align[2];
	Uint8			bits_per_sample[2];
	Uint8			ext_size[2];
} AVI_STREAM_FORMAT_AUDS;

typedef struct
{
	Uint8			ChunkName[4];		/* 'LIST' */
	Uint8			ChunkSize[4];

	Uint8			Name[4];		/* 'strl' */
	AVI_STREAM_HEADER	Header;			/* 'strh' */
	AVI_STREAM_FORMAT_AUDS	Format;			/* 'strf' */
} AVI_STREAM_LIST_AUDS;


typedef struct {
	Uint8			ChunkName[4];		/* 'avih' */
	Uint8			ChunkSize[4];

	Uint8			microsec_per_frame[4];
	Uint8			max_bytes_per_second[4];
	Uint8			padding_granularity[4];
	Uint8			flags[4];
	Uint8			total_frames[4];
	Uint8			init_frame[4];
	Uint8			nb_streams[4];
	Uint8			buffer_size[4];
	Uint8			width[4];
	Uint8			height[4];
	Uint8			scale[4];
	Uint8			rate[4];
	Uint8			start[4];
	Uint8			length[4];
} AVI_STREAM_AVIH;

typedef struct
{
	Uint8			ChunkName[4];		/* 'LIST' */
	Uint8			ChunkSize[4];

	Uint8			Name[4];		/* 'hdrl' */
	AVI_STREAM_AVIH		Header;
} AVI_STREAM_LIST_AVIH;


typedef struct {
	Uint8			ChunkName[4];		/* 'ISFT' (software used) */
	Uint8			ChunkSize[4];

//	Uint8			Text[2];		/* Text's size should be multiple of 2 (including '\0') */
} AVI_STREAM_INFO;

typedef struct
{
	Uint8			ChunkName[4];		/* 'LIST' */
	Uint8			ChunkSize[4];

	Uint8			Name[4];		/* 'INFO' */
	AVI_STREAM_INFO		Info;
} AVI_STREAM_LIST_INFO;


typedef struct
{
	Uint8			ChunkName[4];		/* 'LIST' */
	Uint8			ChunkSize[4];

	Uint8			Name[4];		/* 'movi' */
} AVI_STREAM_LIST_MOVI;


typedef struct
{
  Uint8		signature[4];				/* 'RIFF' */
  Uint8		filesize[4];
  Uint8		type[4];				/* 'AVI ' */
  
} RIFF_HEADER;


typedef struct {
  RIFF_HEADER			RiffHeader;

  AVI_STREAM_LIST_AVIH		AviHeader;
  
  AVI_STREAM_LIST_VIDS		VideoStream;
  AVI_STREAM_LIST_AUDS		AudioStream;
  
} AVI_FILE_HEADER;



#define	AUDIO_STREAM_WAVE_FORMAT_PCM		0x0001

#define	VIDEO_STREAM_RGB			0x00000000			/* fourcc for BMP video frames */
#define	VIDEO_STREAM_PNG			"MPNG"				/* fourcc for PNG video frames */

#define	AVIF_HASINDEX				0x00000010			/* index at the end of the file */
#define	AVIF_ISINTERLEAVED			0x00000100			/* data are interleaved */
#define	AVIF_TRUSTCKTYPE			0x00000800			/* trust chunk type */

#define	AVIIF_KEYFRAME				0x00000010			/* frame is a keyframe */


typedef struct {
  /* Input params to start recording */
  int		VideoCodec;
  int		VideoCodecCompressionLevel;					/* 0-9 for png compression */

  SDL_Surface	*Surface;

  int		CropLeft;
  int		CropRight;
  int		CropTop;
  int		CropBottom;

  int		Fps;

  int		AudioCodec;
  int		AudioFreq;

  /* Internal data used by the avi recorder */
  int		Width;
  int		Height;
  int		BitCount;
  FILE		*FileOut;				/* file to write to */
  int		TotalVideoFrames;			/* number of recorded video frames */
  int		TotalAudioSamples;			/* number of recorded audio samples */
  long		MoviChunkPosStart;			/* as returned by ftell() */
  long		MoviChunkPosEnd;			/* as returned by ftell() */
} RECORD_AVI_PARAMS;



bool		AviRecording = false;
#if HAVE_LIBPNG
int		AviRecordDefaultVcodec = AVI_RECORD_VIDEO_CODEC_PNG;
#else
int		AviRecordDefaultVcodec = AVI_RECORD_VIDEO_CODEC_BMP;
#endif
bool		AviRecordDefaultCrop = true;
int		AviRecordDefaultFps = 50;
char		AviRecordFile[FILENAME_MAX] = "hatari.avi";


static RECORD_AVI_PARAMS	AviParams;
static AVI_FILE_HEADER		AviFileHeader;


static void	AviStoreU16 ( Uint8 *p , Uint16 val );
static void	AviStoreU32 ( Uint8 *p , Uint32 val );
static void	AviStore4cc ( Uint8 *p , const char *text );
static Uint32	AviReadU32 ( Uint8 *p );

static int	AviGetBmpSize ( int Width , int Height , int BitCount );

static inline void PixelConvert_8to24Bits_BGR(Uint8 *dst, Uint8 *src, int w, SDL_Color *colors);
static inline void PixelConvert_16to24Bits_BGR(Uint8 *dst, Uint16 *src, int w, SDL_PixelFormat *fmt);
static inline void PixelConvert_24to24Bits_BGR(Uint8 *dst, Uint8 *src, int w);
static inline void PixelConvert_32to24Bits_BGR(Uint8 *dst, Uint8 *src, int w);

static bool	AviRecordVideoStream_BMP ( RECORD_AVI_PARAMS *pAviParams );
#if HAVE_LIBPNG
static bool	AviRecordVideoStream_PNG ( RECORD_AVI_PARAMS *pAviParams );
#endif
static bool	AviRecordAudioStream_PCM ( RECORD_AVI_PARAMS *pAviParams , Sint16 pSamples[][2], int SampleIndex, int SampleLength );

static void	AviBuildFileHeader ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader );
static bool	AviBuildIndex ( RECORD_AVI_PARAMS *pAviParams );

static bool	AviStartRecording_WithParams ( RECORD_AVI_PARAMS *pAviParams , char *AviFileName );
static bool	AviStopRecording_WithParams ( RECORD_AVI_PARAMS *pAviParams );




static void	AviStoreU16 ( Uint8 *p , Uint16 val )
{
	*p++ = val & 0xff;
	val >>= 8;
	*p = val & 0xff;
}


static void	AviStoreU32 ( Uint8 *p , Uint32 val )
{
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p++ = val & 0xff;
	val >>= 8;
	*p = val & 0xff;
}


static void	AviStore4cc ( Uint8 *p , const char *text )
{
	memcpy ( p , text , 4 );
}


static Uint32	AviReadU32 ( Uint8 *p )
{
	return (p[3]<<24) + (p[2]<<16) + (p[1]<<8) +p[0];
}


static int	AviGetBmpSize ( int Width , int Height , int BitCount )
{
	return ( Width * Height * BitCount / 8 );						/* bytes in one video frame */
}



/*-----------------------------------------------------------------------*/
/**
 * Unpack 8-bit data with RGB palette to 24-bit BGR pixels
 */
static inline void PixelConvert_8to24Bits_BGR(Uint8 *dst, Uint8 *src, int w, SDL_Color *colors)
{
	int x;
	for (x = 0; x < w; x++, src++) {
		*dst++ = colors[*src].b;
		*dst++ = colors[*src].g;
		*dst++ = colors[*src].r;
	}
}

/**
 * Unpack 16-bit RGB pixels to 24-bit BGR pixels
 */
static inline void PixelConvert_16to24Bits_BGR(Uint8 *dst, Uint16 *src, int w, SDL_PixelFormat *fmt)
{
	int x;
	for (x = 0; x < w; x++, src++) {
		*dst++ = (((*src & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
		*dst++ = (((*src & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((*src & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
	}
}

/**
 *  unpack 24-bit RGB pixels to 24-bit BGR pixels
 */
static inline void PixelConvert_24to24Bits_BGR(Uint8 *dst, Uint8 *src, int w)
{
	int x;
	for (x = 0; x < w; x++, src += 3) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		*dst++ = src[2];	/* B */
		*dst++ = src[1];	/* G */
		*dst++ = src[0];	/* R */
#else
		*dst++ = src[0];	/* B */
		*dst++ = src[1];	/* G */
		*dst++ = src[2];	/* R */
#endif
	}
}

/**
 *  unpack 32-bit RGBA pixels to 24-bit BGR pixels
 */
static inline void PixelConvert_32to24Bits_BGR(Uint8 *dst, Uint8 *src, int w)
{
	int x;
	for (x = 0; x < w; x++, src += 4) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		*dst++ = src[3];	/* B */
		*dst++ = src[2];	/* G */
		*dst++ = src[1];	/* R */
#else
		*dst++ = src[0];	/* B */
		*dst++ = src[1];	/* G */
		*dst++ = src[2];	/* R */
#endif
	}
}



static bool	AviRecordVideoStream_BMP ( RECORD_AVI_PARAMS *pAviParams )
{
	AVI_CHUNK	Chunk;
	int		SizeImage;
	Uint8		LineBuf[ 3 * pAviParams->Width ];			/* temp buffer to convert to 24-bit BGR format */
	Uint8		*pBitmapIn , *pBitmapOut;
	int		y;
	int		NeedLock;
	
	SizeImage = AviGetBmpSize ( pAviParams->Width , pAviParams->Height , pAviParams->BitCount );

	/* Write the video frame header */
	AviStore4cc ( Chunk.ChunkName , "00db" );				/* stream 0, uncompressed DIB bytes */
	AviStoreU32 ( Chunk.ChunkSize , SizeImage );					/* max size of RGB image */
	if ( fwrite ( &Chunk , sizeof ( Chunk ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviRecordVideoStream_BMP" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write bmp frame header" );
		return false;
	}


	/* Write the video frame data */
	NeedLock = SDL_MUSTLOCK( pAviParams->Surface );

	/* Points to the top left pixel after cropping borders */
	/* For BMP format, frame is stored from bottom to top (origin is in bottom left corner) */
	/* and bytes are in BGR order (not RGB) */
	pBitmapIn = (Uint8 *)pAviParams->Surface->pixels
			+ pAviParams->Surface->pitch * ( pAviParams->CropTop + pAviParams->Height )
			+ pAviParams->CropLeft * pAviParams->Surface->format->BytesPerPixel;

	for ( y=0 ; y<pAviParams->Height ; y++ )
	{
		if ( NeedLock )
			SDL_LockSurface ( pAviParams->Surface );

		pBitmapOut = LineBuf;
		switch ( pAviParams->Surface->format->BytesPerPixel ) {
			case 1 :	PixelConvert_8to24Bits_BGR(LineBuf, pBitmapIn, pAviParams->Width, pAviParams->Surface->format->palette->colors);
					break;
			case 2 :	PixelConvert_16to24Bits_BGR(LineBuf, (Uint16 *)pBitmapIn, pAviParams->Width, pAviParams->Surface->format);
					break;
			case 3 :	PixelConvert_24to24Bits_BGR(LineBuf, pBitmapIn, pAviParams->Width);
					break;
			case 4 :	PixelConvert_32to24Bits_BGR(LineBuf, pBitmapIn, pAviParams->Width);
					break;
		}

		if ( NeedLock )
			SDL_UnlockSurface ( pAviParams->Surface );

		if ( (int)fwrite ( pBitmapOut , 1 , pAviParams->Width*3 , pAviParams->FileOut ) != pAviParams->Width*3 )
		{
			perror ( "AviRecordVideoStream_BMP" );
			Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write bmp video frame" );
			return false;
		}

		pBitmapIn -= pAviParams->Surface->pitch;			/* go from bottom to top */
	}

	return true;
}



#if HAVE_LIBPNG
static bool	AviRecordVideoStream_PNG ( RECORD_AVI_PARAMS *pAviParams )
{
	AVI_CHUNK	Chunk;
	int		SizeImage;
	long		ChunkPos;
	Uint8	TempSize[4];
	

	/* Write the video frame header */
	ChunkPos = ftell ( pAviParams->FileOut );
	AviStore4cc ( Chunk.ChunkName , "00dc" );				/* stream 0, compressed DIB bytes */
	AviStoreU32 ( Chunk.ChunkSize , 0 );					/* size of PNG image (-> completed later) */
	if ( fwrite ( &Chunk , sizeof ( Chunk ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviRecordVideoStream_PNG" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write png frame header" );
		return false;
	}


	/* Write the video frame data */
	SizeImage = ScreenSnapShot_SavePNG_ToFile ( pAviParams->Surface , pAviParams->FileOut ,
		pAviParams->VideoCodecCompressionLevel , PNG_FILTER_NONE ,
		pAviParams->CropLeft , pAviParams->CropRight , pAviParams->CropTop , pAviParams->CropBottom );
	if ( SizeImage <= 0 )
	{
		perror ( "AviRecordVideoStream_PNG" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write png video frame" );
		return false;
	}
	if ( SizeImage & 1 )
	{
		SizeImage++;							/* add an extra '\0' byte to get an even size */
		fputc ( '\0' , pAviParams->FileOut );				/* next chunk must be aligned on 16 bits boundary */
	}

	/* Update the size of the video chunk */
	AviStoreU32 ( TempSize , SizeImage );
	if ( fseek ( pAviParams->FileOut , ChunkPos+4 , SEEK_SET ) != 0 )
	{
		perror ( "AviRecordVideoStream_PNG" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to update png frame header" );
		return false;
	}
	if ( fwrite ( TempSize , sizeof ( TempSize ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviRecordVideoStream_PNG" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to update png frame header" );
		return false;
	}


	/* Go to the end of the video frame data */
	if ( fseek ( pAviParams->FileOut , 0 , SEEK_END ) != 0 )
	{
		perror ( "AviRecordVideoStream_PNG" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to seek png video frame" );
		return false;
	}

	return true;
}
#endif  /* HAVE_LIBPNG */



bool	AviRecordVideoStream ( void )
{
	if ( AviParams.VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
	{
		if ( AviRecordVideoStream_BMP ( &AviParams ) == false )
		{
			return false;
		}
	}
#if HAVE_LIBPNG
	else if ( AviParams.VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
	{
		if ( AviRecordVideoStream_PNG ( &AviParams ) == false )
		{
			return false;
		}
	}
#endif
	else
	{
		return false;
	}

	AviParams.TotalVideoFrames++;
	return true;
}



static bool	AviRecordAudioStream_PCM ( RECORD_AVI_PARAMS *pAviParams , Sint16 pSamples[][2] , int SampleIndex , int SampleLength )
{
	AVI_CHUNK	Chunk;
	Sint16		sample[2];
	int		i;

	/* Write the audio frame header */
	AviStore4cc ( Chunk.ChunkName , "01wb" );				/* stream 1, wave bytes */
	AviStoreU32 ( Chunk.ChunkSize , SampleLength * 4 );			/* 16 bits, stereo -> 4 bytes */
	if ( fwrite ( &Chunk , sizeof ( Chunk ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviRecordAudioStream_PCM" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write pcm frame header" );
		return false;
	}

	/* Write the audio frame data */
	for ( i = 0 ; i < SampleLength; i++ )
	{
		/* Convert sample to little endian */
		sample[0] = SDL_SwapLE16 ( pSamples[ (SampleIndex+i) % MIXBUFFER_SIZE ][0]);
		sample[1] = SDL_SwapLE16 ( pSamples[ (SampleIndex+i) % MIXBUFFER_SIZE ][1]);
		/* And store */
		if ( fwrite ( &sample , sizeof ( sample ) , 1 , pAviParams->FileOut ) != 1 )
		{
			perror ( "AviRecordAudioStream_PCM" );
			Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write pcm frame" );
			return false;
		}
	}

	return true;
}



bool	AviRecordAudioStream ( Sint16 pSamples[][2] , int SampleIndex , int SampleLength )
{
	if ( AviParams.AudioCodec == AVI_RECORD_AUDIO_CODEC_PCM )
	{
		if ( AviRecordAudioStream_PCM ( &AviParams , pSamples , SampleIndex , SampleLength ) == false )
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	AviParams.TotalAudioSamples += SampleLength;
	return true;
}




static void	AviBuildFileHeader ( RECORD_AVI_PARAMS *pAviParams , AVI_FILE_HEADER *pAviFileHeader )
{
	int	Width , Height , BitCount , Fps , SizeImage;
	int	AudioFreq;

	memset ( pAviFileHeader , 0 , sizeof ( *pAviFileHeader ) );

	Width = pAviParams->Width;
	Height =pAviParams->Height;
	BitCount = pAviParams->BitCount;
	Fps = pAviParams->Fps;
	AudioFreq = pAviParams->AudioFreq;

	SizeImage = 0;
	if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
		SizeImage = AviGetBmpSize ( Width , Height , BitCount );		/* size of a BMP image */
	else if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
		SizeImage = AviGetBmpSize ( Width , Height , BitCount );		/* max size of a PNG image */


	/* RIFF / AVI headers */
	AviStore4cc ( pAviFileHeader->RiffHeader.signature , "RIFF" );
	AviStoreU32 ( pAviFileHeader->RiffHeader.filesize , 0 );				/* total file size (-> completed later) */
	AviStore4cc ( pAviFileHeader->RiffHeader.type , "AVI " );

	AviStore4cc ( pAviFileHeader->AviHeader.ChunkName , "LIST" );
	AviStoreU32 ( pAviFileHeader->AviHeader.ChunkSize , sizeof ( AVI_STREAM_LIST_AVIH )
		+ sizeof ( AVI_STREAM_LIST_VIDS ) + sizeof ( AVI_STREAM_LIST_AUDS ) - 8 );
	AviStore4cc ( pAviFileHeader->AviHeader.Name , "hdrl" );

	AviStore4cc ( pAviFileHeader->AviHeader.Header.ChunkName , "avih" );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.ChunkSize , sizeof ( AVI_STREAM_AVIH ) - 8 );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.microsec_per_frame , 1000000 / Fps );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.max_bytes_per_second , SizeImage * Fps + AudioFreq * 4 );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.padding_granularity , 0 );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.flags , AVIF_HASINDEX | AVIF_ISINTERLEAVED | AVIF_TRUSTCKTYPE );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.total_frames , 0 );			/* number of video frames (-> completed later) */
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.init_frame , 0 );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.nb_streams , 2 );			/* 1 video and 1 audio */
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.buffer_size , SizeImage );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.width , Width );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.height , Height );
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.scale , 0 );				/* reserved */
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.rate , 0 );				/* reserved */
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.start , 0 );				/* reserved */
	AviStoreU32 ( pAviFileHeader->AviHeader.Header.length , 0 );				/* reserved */


	/* Video Stream */
	AviStore4cc ( pAviFileHeader->VideoStream.ChunkName , "LIST" );
	AviStoreU32 ( pAviFileHeader->VideoStream.ChunkSize , sizeof ( AVI_STREAM_LIST_VIDS ) - 8 );
	AviStore4cc ( pAviFileHeader->VideoStream.Name , "strl" );

	AviStore4cc ( pAviFileHeader->VideoStream.Header.ChunkName , "strh" );
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.ChunkSize , sizeof ( AVI_STREAM_HEADER ) - 8 );
	AviStore4cc ( pAviFileHeader->VideoStream.Header.stream_type , "vids" );
	if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
		AviStoreU32 ( pAviFileHeader->VideoStream.Header.stream_handler , VIDEO_STREAM_RGB );
	else if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
		AviStore4cc ( pAviFileHeader->VideoStream.Header.stream_handler , VIDEO_STREAM_PNG );
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.flags , 0 );
	AviStoreU16 ( pAviFileHeader->VideoStream.Header.priority , 0 );
	AviStoreU16 ( pAviFileHeader->VideoStream.Header.language , 0 );
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.initial_frames , 0 );
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.time_scale , 1 );
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.data_rate , Fps );
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.start_time , 0 );
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.data_length , 0 );			/* number of video frames (-> completed later) */
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.buffer_size , SizeImage );		/* size of an uncompressed frame */
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.quality , -1 );			/* use default quality */
	AviStoreU32 ( pAviFileHeader->VideoStream.Header.sample_size , 0 );			/* 0 for video */
	AviStoreU16 ( pAviFileHeader->VideoStream.Header.dest_left , 0 );
	AviStoreU16 ( pAviFileHeader->VideoStream.Header.dest_top , 0 );
	AviStoreU16 ( pAviFileHeader->VideoStream.Header.dest_right , Width );
	AviStoreU16 ( pAviFileHeader->VideoStream.Header.dest_bottom , Height );

	AviStore4cc ( pAviFileHeader->VideoStream.Format.ChunkName , "strf" );
	AviStoreU32 ( pAviFileHeader->VideoStream.Format.ChunkSize , sizeof ( AVI_STREAM_FORMAT_VIDS ) - 8 );
	if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_BMP )
	{
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.size , sizeof ( AVI_STREAM_FORMAT_VIDS ) - 8 );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.width , Width );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.height , Height );
		AviStoreU16 ( pAviFileHeader->VideoStream.Format.planes , 1 );			/* always 1 */
		AviStoreU16 ( pAviFileHeader->VideoStream.Format.bit_count , BitCount );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.compression , VIDEO_STREAM_RGB );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.size_image , SizeImage );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.xpels_meter , 0 );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.ypels_meter , 0 );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.clr_used , 0 );		/* no color map */
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.clr_important , 0 );		/* no color map */
	}
	else if ( pAviParams->VideoCodec == AVI_RECORD_VIDEO_CODEC_PNG )
	{
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.size , sizeof ( AVI_STREAM_FORMAT_VIDS ) - 8 );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.width , Width );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.height , Height );
		AviStoreU16 ( pAviFileHeader->VideoStream.Format.planes , 1 );			/* always 1 */
		AviStoreU16 ( pAviFileHeader->VideoStream.Format.bit_count , BitCount );
		AviStore4cc ( pAviFileHeader->VideoStream.Format.compression , VIDEO_STREAM_PNG );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.size_image , SizeImage );	/* max size if uncompressed */
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.xpels_meter , 0 );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.ypels_meter , 0 );
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.clr_used , 0 );		/* no color map */
		AviStoreU32 ( pAviFileHeader->VideoStream.Format.clr_important , 0 );		/* no color map */
	}


	/* Audio Stream */
	AviStore4cc ( pAviFileHeader->AudioStream.ChunkName , "LIST" );
	AviStoreU32 ( pAviFileHeader->AudioStream.ChunkSize , sizeof ( AVI_STREAM_LIST_AUDS ) - 8 );
	AviStore4cc ( pAviFileHeader->AudioStream.Name , "strl" );

	AviStore4cc ( pAviFileHeader->AudioStream.Header.ChunkName , "strh" );
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.ChunkSize , sizeof ( AVI_STREAM_HEADER ) - 8 );
	AviStore4cc ( pAviFileHeader->AudioStream.Header.stream_type , "auds" );
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.stream_handler , 0 );			/* not used (or could be 1 for pcm ?) */
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.flags , 0 );
	AviStoreU16 ( pAviFileHeader->AudioStream.Header.priority , 0 );
	AviStoreU16 ( pAviFileHeader->AudioStream.Header.language , 0 );
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.initial_frames , 0 );			/* should be 1 in interleaved ? */
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.time_scale , 1 );
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.data_rate , AudioFreq );
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.start_time , 0 );
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.data_length , 0 );			/* number of audio samples (-> completed later) */
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.buffer_size , AudioFreq * 4 / 50 );	/* min VBL freq is 50 Hz */
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.quality , -1 );			/* use default quality */
	AviStoreU32 ( pAviFileHeader->AudioStream.Header.sample_size , 4 );			/* 2 bytes, stereo */
	AviStoreU16 ( pAviFileHeader->AudioStream.Header.dest_left , 0 );
	AviStoreU16 ( pAviFileHeader->AudioStream.Header.dest_top , 0 );
	AviStoreU16 ( pAviFileHeader->AudioStream.Header.dest_right , 0 );
	AviStoreU16 ( pAviFileHeader->AudioStream.Header.dest_bottom , 0 );

	AviStore4cc ( pAviFileHeader->AudioStream.Format.ChunkName , "strf" );
	AviStoreU32 ( pAviFileHeader->AudioStream.Format.ChunkSize , sizeof ( AVI_STREAM_FORMAT_AUDS ) - 8 );
	if ( pAviParams->AudioCodec == AVI_RECORD_AUDIO_CODEC_PCM )				/* 16 bits stereo pcm */
	{
		AviStoreU16 ( pAviFileHeader->AudioStream.Format.codec , AUDIO_STREAM_WAVE_FORMAT_PCM );	/* 0x0001 */
		AviStoreU16 ( pAviFileHeader->AudioStream.Format.channels , 2 );
		AviStoreU32 ( pAviFileHeader->AudioStream.Format.sample_rate , AudioFreq );
		AviStoreU32 ( pAviFileHeader->AudioStream.Format.bit_rate , AudioFreq * 2 * 2 );	/* 2 channels * 2 bytes */
		AviStoreU16 ( pAviFileHeader->AudioStream.Format.block_align , 4 );
		AviStoreU16 ( pAviFileHeader->AudioStream.Format.bits_per_sample , 16 );
		AviStoreU16 ( pAviFileHeader->AudioStream.Format.ext_size , 0 );
	}
}


static bool	AviBuildIndex ( RECORD_AVI_PARAMS *pAviParams )
{
	AVI_CHUNK	Chunk;
	long		IndexChunkPosStart;
	long		Pos , PosWrite;
	Uint8		TempSize[4];
	AVI_CHUNK_INDEX	ChunkIndex;
	Uint32		Size;

	fseek ( pAviParams->FileOut , 0 , SEEK_END );				/* go to the end of the file */

	/* Write the 'idx1' chunk header */
	IndexChunkPosStart = ftell ( pAviParams->FileOut );
	AviStore4cc ( Chunk.ChunkName , "idx1" );				/* stream 0, uncompressed DIB bytes */
	AviStoreU32 ( Chunk.ChunkSize , 0 );					/* index size (-> completed later) */
	if ( fwrite ( &Chunk , sizeof ( Chunk ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviBuildIndex" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write index header" );
		return false;
	}
	PosWrite = ftell ( pAviParams->FileOut );				/* position to start writing indexes */

	/* Go to the first data chunk in the 'movi' chunk */
	fseek ( pAviParams->FileOut , pAviParams->MoviChunkPosStart + sizeof ( AVI_STREAM_LIST_MOVI ) , SEEK_SET );
	Pos = ftell ( pAviParams->FileOut );

	/* Build the index : we seek/read one data chunk and seek/write the */
	/* corresponding infos at the end of the index, until we reach the */
	/* end of the 'movi' chunk. */
	while ( Pos < pAviParams->MoviChunkPosEnd )
	{
		/* Read the header for this data chunk */
		fread ( &Chunk , sizeof ( Chunk ) , 1 , pAviParams->FileOut );	/* ChunkName and ChunkSize */
		Size = AviReadU32 ( Chunk.ChunkSize );

		/* Write the index infos for this chunk */
		fseek ( pAviParams->FileOut , PosWrite , SEEK_SET );
		AviStore4cc ( ChunkIndex.identifier , (char *)Chunk.ChunkName );	/* 00dc, 00db, 01wb, ... */
		AviStoreU32 ( ChunkIndex.flags , AVIIF_KEYFRAME );		/* AVIIF_KEYFRAME */
		AviStoreU32 ( ChunkIndex.offset , Pos - pAviParams->MoviChunkPosStart - 8  );	/* pos relative to 'movi' */
		AviStoreU32 ( ChunkIndex.length , Size );
		fwrite ( &ChunkIndex , sizeof ( ChunkIndex ) , 1 , pAviParams->FileOut );
		PosWrite = ftell ( pAviParams->FileOut );			/* position for the next index */

		/* Go to the next data chunk in the 'movi' chunk */
		Pos = Pos + sizeof ( Chunk ) + Size;				/* position of the next data chunk */
		fseek ( pAviParams->FileOut , Pos , SEEK_SET );
	}

	/* Update the size of the 'idx1' chunk */
	AviStoreU32 ( TempSize , PosWrite - IndexChunkPosStart - 8 );
	if ( fseek ( pAviParams->FileOut , IndexChunkPosStart+4 , SEEK_SET ) != 0 )
	{
		perror ( "AviBuildIndex" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to update idx1 header" );
		return false;
	}
	if ( fwrite ( TempSize , sizeof ( TempSize ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviBuildIndex" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to update idx1 header" );
		return false;
	}

	return true;
}


static bool	AviStartRecording_WithParams ( RECORD_AVI_PARAMS *pAviParams , char *AviFileName )
{
	AVI_STREAM_LIST_INFO	ListInfo;
	char			InfoString[ 100 ];
	int			Len , Len_rounded;
	AVI_STREAM_LIST_MOVI	ListMovi;


	if ( AviRecording == true )						/* already recording ? */
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

	/* Build the AVI header */
	AviBuildFileHeader ( pAviParams , &AviFileHeader );
	
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
	AviStore4cc ( ListInfo.ChunkName , "LIST" );
	AviStoreU32 ( ListInfo.ChunkSize , sizeof ( AVI_STREAM_LIST_INFO ) - 8 + Len_rounded );
	AviStore4cc ( ListInfo.Name , "INFO" );
	AviStore4cc ( ListInfo.Info.ChunkName , "ISFT" );
	AviStoreU32 ( ListInfo.Info.ChunkSize , Len );
	if ( fwrite ( &ListInfo , sizeof ( ListInfo ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write info header" );
		return false;
	}
	/* Write the info string + '\0' and write an optionnal extra '\0' byte to get a total multiple of 2 */
	if ( fwrite ( InfoString , Len_rounded , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write info header" );
		return false;
	}

	/* Write the MOVI header */
	AviStore4cc ( ListMovi.ChunkName , "LIST" );
	AviStoreU32 ( ListMovi.ChunkSize , 0 );					/* completed when recording stops */
	AviStore4cc ( ListMovi.Name , "movi" );
	pAviParams->MoviChunkPosStart = ftell ( pAviParams->FileOut );
	if ( fwrite ( &ListMovi , sizeof ( ListMovi ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviStartRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to write movi header" );
		return false;
	}


	/* We're ok to record */
	Log_AlertDlg ( LOG_INFO, "AVI recording has been started");
	AviRecording = true;

	return true;
}



static bool	AviStopRecording_WithParams ( RECORD_AVI_PARAMS *pAviParams )
{
	long	FileSize;
	Uint8	TempSize[4];


	if ( AviRecording == false )						/* no recording ? */
		return true;

	/* Update the size of the 'movi' chunk */
	fseek ( pAviParams->FileOut , 0 , SEEK_END );				/* go to the end of the 'movi' chunk */
	pAviParams->MoviChunkPosEnd = ftell ( pAviParams->FileOut );
	AviStoreU32 ( TempSize , pAviParams->MoviChunkPosEnd - pAviParams->MoviChunkPosStart - 8 );

	if ( fseek ( pAviParams->FileOut , pAviParams->MoviChunkPosStart+4 , SEEK_SET ) != 0 )
	{
		perror ( "AviStopRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to update movi header" );
		return false;
	}
	if ( fwrite ( TempSize , sizeof ( TempSize ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviStopRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to update movi header" );
		return false;
	}

	/* Build the index chunk */
	if ( ! AviBuildIndex ( pAviParams ) )
	{
		perror ( "AviStopRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to build index" );
		return false;
	}
	
	/* Update the avi header (file size, number of output frames, ...) */
	fseek ( pAviParams->FileOut , 0 , SEEK_END );				/* go to the end of the file */
	FileSize = ftell ( pAviParams->FileOut );

	AviStoreU32 ( AviFileHeader.RiffHeader.filesize , FileSize - 8 );	/* 32 bits, limited to 4GB */
	AviStoreU32 ( AviFileHeader.AviHeader.Header.total_frames , pAviParams->TotalVideoFrames );	/* number of video frames */
	AviStoreU32 ( AviFileHeader.VideoStream.Header.data_length , pAviParams->TotalVideoFrames );	/* number of video frames */
	AviStoreU32 ( AviFileHeader.AudioStream.Header.data_length , pAviParams->TotalAudioSamples );	/* number of audio samples */

	if ( fseek ( pAviParams->FileOut , 0 , SEEK_SET ) != 0 )
	{
		perror ( "AviStopRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to update avi header" );
		return false;
	}
	if ( fwrite ( &AviFileHeader , sizeof ( AviFileHeader ) , 1 , pAviParams->FileOut ) != 1 )
	{
		perror ( "AviStopRecording" );
		Log_AlertDlg ( LOG_ERROR, "AVI recording : failed to update avi header" );
		return false;
	}


	/* Close the file */
	fclose ( pAviParams->FileOut );

	Log_AlertDlg ( LOG_INFO, "AVI recording has been stopped");
	AviRecording = false;

	return true;
}




/*-----------------------------------------------------------------------*/
/**
 * Are we recording an AVI ?
 */
bool	Avi_AreWeRecording ( void )
{
        return AviRecording;
}



void	AviStartRecording ( char *FileName , bool CropGui , int Fps , int VideoCodec )
{
	memset ( &AviParams , 0 , sizeof ( AviParams ) );

	AviParams.VideoCodec = VideoCodec;
	AviParams.VideoCodecCompressionLevel = 4;	/* png compression level */
	AviParams.AudioCodec = AVI_RECORD_AUDIO_CODEC_PCM;
	AviParams.AudioFreq = ConfigureParams.Sound.nPlaybackFreq;
	AviParams.Surface = sdlscrn;
	AviParams.Fps = Fps;

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
	
	
	AviStartRecording_WithParams ( &AviParams , FileName );
}


void	AviStopRecording ( void )
{
	AviStopRecording_WithParams ( &AviParams );
}


