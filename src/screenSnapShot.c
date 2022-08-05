/*
  Hatari - screenSnapShot.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen Snapshots.
*/
const char ScreenSnapShot_fileid[] = "Hatari screenSnapShot.c";

#include <SDL.h>
#include <dirent.h>
#include <string.h>
#include "main.h"
#include "configuration.h"
#include "file.h"
#include "log.h"
#include "paths.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "statusbar.h"
#include "video.h"
/* after above that bring in config.h */
#if HAVE_LIBPNG
# include <png.h>
# include <assert.h>
# include "pixel_convert.h"				/* inline functions */
#endif


static int nScreenShots = 0;                /* Number of screen shots saved */


/*-----------------------------------------------------------------------*/
/**
 * Scan working directory to get the screenshot number
 */
static void ScreenSnapShot_GetNum(void)
{
	char dummy[5];
	int i, num;
	DIR *workingdir = opendir(Paths_GetScreenShotDir());
	struct dirent *file;

	nScreenShots = 0;
	if (workingdir == NULL)  return;

	file = readdir(workingdir);
	while (file != NULL)
	{
		if ( strncmp("grab", file->d_name, 4) == 0 )
		{
			/* copy next 4 numbers */
			for (i = 0; i < 4; i++)
			{
				if (file->d_name[4+i] >= '0' && file->d_name[4+i] <= '9')
					dummy[i] = file->d_name[4+i];
				else
					break;
			}

			dummy[i] = '\0'; /* null terminate */
			num = atoi(dummy);
			if (num > nScreenShots)  nScreenShots = num;
		}
		/* next file.. */
		file = readdir(workingdir);
	}

	closedir(workingdir);
}


#if HAVE_LIBPNG
/**
 * Save given SDL surface as PNG. Return png file size > 0 for success.
 */
static int ScreenSnapShot_SavePNG(SDL_Surface *surface, const char *filename)
{
	FILE *fp = NULL;
	int ret, bottom;
  
	fp = fopen(filename, "wb");
	if (!fp)
		return -1;

	if (ConfigureParams.Screen.bCrop)
		bottom = Statusbar_GetHeight();
	else
		bottom = 0;

	/* default compression/filter and configured cropping */
	ret = ScreenSnapShot_SavePNG_ToFile(surface, 0, 0, fp, -1, -1, 0, 0, 0, bottom);

	fclose (fp);
	return ret;					/* >0 if OK, -1 if error */
}


/**
 * Save given SDL surface as PNG in an already opened FILE, eventually cropping some borders.
 * Return png file size > 0 for success.
 * This function is also used by avi_record.c to save individual frames as png images.
 */
int ScreenSnapShot_SavePNG_ToFile(SDL_Surface *surface, int dw, int dh,
		FILE *fp, int png_compression_level, int png_filter,
		int CropLeft , int CropRight , int CropTop , int CropBottom )
{
	bool do_lock;
	int y, ret;
	int sw = surface->w - CropLeft - CropRight;
	int sh = surface->h - CropTop - CropBottom;
	Uint8 *src_ptr;
	Uint8 *rowbuf;
	SDL_PixelFormat *fmt = surface->format;
	png_infop info_ptr = NULL;
	png_structp png_ptr;
	png_text pngtext;
	char key[] = "Title";
	char text[] = "Hatari screenshot";
	off_t start;;


	if (!dw)
		dw = sw;
	if (!dh)
		dh = sh;

	rowbuf = alloca(3 * dw);

	/* Create and initialize the png_struct with error handler functions. */
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) 
	{
		return -1;
	}
	
	/* Allocate/initialize the image information data. */
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		ret = -1;
		goto png_cleanup;
	}

	/* libpng ugliness: Set error handling when not supplying own
	 * error handling functions in the png_create_write_struct() call.
	 */
	if (setjmp(png_jmpbuf(png_ptr))) {
		ret = -1;
		goto png_cleanup;
	}

	/* store current pos in fp (could be != 0 for avi recording) */
	start = ftello ( fp );

	/* initialize the png structure */
	png_init_io(png_ptr, fp);

	/* image data properties */
	png_set_IHDR(png_ptr, info_ptr, dw, dh, 8, PNG_COLOR_TYPE_RGB,
		     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
		     PNG_FILTER_TYPE_DEFAULT);

	if ( png_compression_level >= 0 )
		png_set_compression_level ( png_ptr , png_compression_level );
	if ( png_filter >= 0 )
		png_set_filter ( png_ptr , 0 , png_filter );
		     
	/* image info */
	pngtext.key = key;
	pngtext.text = text;
	pngtext.compression = PNG_TEXT_COMPRESSION_NONE;
#ifdef PNG_iTXt_SUPPORTED
	pngtext.lang = NULL;
#endif
	png_set_text(png_ptr, info_ptr, &pngtext, 1);

	/* write the file header information */
	png_write_info(png_ptr, info_ptr);

	/* write surface data rows one at a time (after cropping if necessary) */
	do_lock = SDL_MUSTLOCK(surface);
	for (y = 0; y < dh; y++)
	{
		/* need to lock the surface while accessing it directly */
		if (do_lock)
			SDL_LockSurface(surface);


		src_ptr = (Uint8 *)surface->pixels
		          + (CropTop + (y * sh + dh/2) / dh) * surface->pitch
		          + CropLeft * surface->format->BytesPerPixel;

		switch (fmt->BytesPerPixel)
		{
		 case 2:
			/* unpack 16-bit RGB pixels */
			PixelConvert_16to24Bits(rowbuf, (Uint16*)src_ptr, dw, surface);
			break;
		 case 4:
			/* unpack 32-bit RGBA pixels */
			PixelConvert_32to24Bits(rowbuf, (Uint32*)src_ptr, dw, surface);
			break;
		 default:
			abort();
		}
		/* and unlock surface before syscalls */
		if (do_lock)
			SDL_UnlockSurface(surface);
		png_write_row(png_ptr, rowbuf);
	}

	/* write the additional chunks to the PNG file */
	png_write_end(png_ptr, info_ptr);

	ret = (int)( ftello ( fp ) - start );			/* size of the png image */
png_cleanup:
	if (png_ptr)
		/* handles info_ptr being NULL */
		png_destroy_write_struct(&png_ptr, &info_ptr);
	return ret;
}
#endif


/*-----------------------------------------------------------------------*/
/**
 * Save screen shot file with filename like 'grab0000.[png|bmp]',
 * 'grab0001.[png|bmp]', etc... Whether screen shots are saved as BMP
 * or PNG depends on Hatari configuration.
 */
void ScreenSnapShot_SaveScreen(void)
{
	char *szFileName = malloc(FILENAME_MAX);

	if (!szFileName)  return;

	ScreenSnapShot_GetNum();
	/* Create our filename */
	nScreenShots++;
#if HAVE_LIBPNG
	/* try first PNG */
	sprintf(szFileName,"%s/grab%4.4d.png", Paths_GetScreenShotDir(), nScreenShots);
	if (ScreenSnapShot_SavePNG(sdlscrn, szFileName) > 0)
	{
		fprintf(stderr, "Screen dump saved to: %s\n", szFileName);
		free(szFileName);
		return;
	}
#endif
	sprintf(szFileName,"%s/grab%4.4d.bmp", Paths_GetScreenShotDir(), nScreenShots);
	if (SDL_SaveBMP(sdlscrn, szFileName))
		fprintf(stderr, "Screen dump failed!\n");
	else
		fprintf(stderr, "Screen dump saved to: %s\n", szFileName);

	free(szFileName);
}

/**
 * Save screen shot to given file.
 */
void ScreenSnapShot_SaveToFile(const char *szFileName)
{
	bool success = false;

	if (!szFileName)
	{
		fprintf(stderr, "ERROR: no screen dump file name specified\n");
		return;
	}
#if HAVE_LIBPNG
	if (File_DoesFileExtensionMatch(szFileName, ".png"))
	{
		success = ScreenSnapShot_SavePNG(sdlscrn, szFileName) > 0;
	}
	else
#endif
	if (File_DoesFileExtensionMatch(szFileName, ".bmp"))
	{
		success = SDL_SaveBMP(sdlscrn, szFileName) == 0;
	}
	else
	{
		fprintf(stderr, "ERROR: unknown screen dump file name extension: %s\n", szFileName);
		return;
	}
	fprintf(stderr, "Screen dump to '%s' %s\n", szFileName,
		success ? "succeeded" : "failed");
}
