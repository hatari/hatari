/*
  Hatari - screenSnapShot.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Screen Snapshots.
*/
const char ScreenSnapShot_rcsid[] = "Hatari $Id: screenSnapShot.c,v 1.18 2008-08-19 20:53:50 eerot Exp $";

#include <SDL.h>
#include <dirent.h>
#include <string.h>
#include "main.h"
#include "log.h"
#include "paths.h"
#include "screen.h"
#include "screenSnapShot.h"
#include "video.h"
/* after above that bring in config.h */
#if HAVE_LIBPNG
# include <png.h>
# include <assert.h>
#endif


bool bRecordingAnimation = FALSE;           /* Recording animation? */
static int nScreenShots = 0;                /* Number of screen shots saved */
static bool bGrabWhenChange;
static int GrabFrameCounter, GrabFrameLatch;


/*-----------------------------------------------------------------------*/
/**
 * Scan working directory to get the screenshot number
 */
static void ScreenSnapShot_GetNum(void)
{
	char dummy[5];
	int i, num;
	DIR *workingdir = opendir(Paths_GetWorkingDir());
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
/*-----------------------------------------------------------------------*/
/**
 * Unpack 8-bit data with RGB palette to 24-bit RGB pixels
 */
static inline void ScreenSnapShot_8to24Bits(Uint8 *dst, Uint8 *src, int w, SDL_Color *colors)
{
	int x;
	for (x = 0; x < w; x++, src++) {
		*dst++ = colors[*src].r;
		*dst++ = colors[*src].g;
		*dst++ = colors[*src].b;
	}
}

/**
 * Unpack 16-bit RGB pixels to 24-bit RGB pixels
 */
static inline void ScreenSnapShot_16to24Bits(Uint8 *dst, Uint16 *src, int w, SDL_PixelFormat *fmt)
{
	int x;
	for (x = 0; x < w; x++, src++) {
		*dst++ = (((*src & fmt->Rmask) >> fmt->Rshift) << fmt->Rloss);
		*dst++ = (((*src & fmt->Gmask) >> fmt->Gshift) << fmt->Gloss);
		*dst++ = (((*src & fmt->Bmask) >> fmt->Bshift) << fmt->Bloss);
	}
}

/**
 *  unpack 32-bit RGBA pixels to 24-bit RGB pixels
 */
static inline void ScreenSnapShot_32to24Bits(Uint8 *dst, Uint8 *src, int w)
{
	int x;
	for (x = 0; x < w; x++, src += 4) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		*dst++ = src[1];
		*dst++ = src[2];
		*dst++ = src[3];
#else
		*dst++ = src[2];
		*dst++ = src[1];
		*dst++ = src[0];
#endif
	}
}

/**
 * Save given SDL surface as PNG. Return zero for success.
 */
static int ScreenSnapShot_SavePNG(SDL_Surface *surface, const char *filename)
{
	int y, ret = -1;
	int w = surface->w;
	int h = surface->h;
	Uint8 *src_ptr, *row_ptr;
	Uint8 rowbuf[3*surface->w];
	SDL_PixelFormat *fmt = surface->format;
	png_colorp palette_ptr = NULL;
	png_infop info_ptr = NULL;
	png_structp png_ptr;
	png_text pngtext;
	char key[] = "Title";
	char text[] = "Hatari screenshot";
	FILE *fp = NULL;
	
	/* Create and initialize the png_struct with error handler functions. */
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) 
	{
		return ret;
	}
	
	/* Allocate/initialize the image information data. */
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
		goto png_cleanup;

	/* libpng ugliness: Set error handling when not supplying own
	 * error handling functions in the png_create_write_struct() call.
	 */
	if (setjmp(png_jmpbuf(png_ptr)))
		goto png_cleanup;

	fp = fopen(filename, "wb");
	if (!fp)
		goto png_cleanup;

	/* initialize the png structure */
	png_init_io(png_ptr, fp);

	/* image data properties */
	png_set_IHDR(png_ptr, info_ptr, w, h, 8, PNG_COLOR_TYPE_RGB,
		     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
		     PNG_FILTER_TYPE_DEFAULT);
	
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

	/* write surface data row at the time */
	src_ptr = surface->pixels;
	for (y = 0; y < h; y++) {
		switch (fmt->BytesPerPixel) {
		case 1:
			/* unpack 8-bit data with RGB palette */
			row_ptr = rowbuf;
			ScreenSnapShot_8to24Bits(row_ptr, src_ptr, w, fmt->palette->colors);
			break;
		case 2:
			/* unpack 16-bit RGB pixels */
			row_ptr = rowbuf;
			ScreenSnapShot_16to24Bits(row_ptr, (Uint16*)src_ptr, w, fmt);
			break;
		case 3:
			/* PNG can handle 24-bits */
			row_ptr = src_ptr;
			break;
		case 4:
			/* unpack 32-bit RGBA pixels */
			row_ptr = rowbuf;
			ScreenSnapShot_32to24Bits(row_ptr, src_ptr, w);
			break;
		}
		src_ptr += surface->pitch;
		SDL_UnlockSurface(surface);
		png_write_row(png_ptr, rowbuf);
	}
	
	/* write the additional chuncks to the PNG file */
	png_write_end(png_ptr, info_ptr);

	ret = 0;
png_cleanup:
	if (fp)
		fclose(fp);
	if (palette_ptr)
		free(palette_ptr);
	if (png_ptr)
		png_destroy_write_struct(&png_ptr, NULL);
	return ret;
}
#endif


/*-----------------------------------------------------------------------*/
/**
 * Save screen shot file with filename 'grab0000.<ext>','grab0001.<ext>'...
 * Whether screen shots are saved as BMP or PNG depends on Hatari configuration.
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
	sprintf(szFileName,"%s/grab%4.4d.png", Paths_GetWorkingDir(), nScreenShots);
	if (ScreenSnapShot_SavePNG(sdlscrn, szFileName) == 0)
	{
		fprintf(stderr, "Screen dump saved to: %s\n", szFileName);
		free(szFileName);
		return;
	}
#endif
	sprintf(szFileName,"%s/grab%4.4d.bmp", Paths_GetWorkingDir(), nScreenShots);
	if (SDL_SaveBMP(sdlscrn, szFileName))
		fprintf(stderr, "Screen dump failed!\n");
	else
		fprintf(stderr, "Screen dump saved to: %s\n", szFileName);

	free(szFileName);
}


/*-----------------------------------------------------------------------*/
/**
 * Are we recording an animation?
 */
bool ScreenSnapShot_AreWeRecording(void)
{
	return bRecordingAnimation;
}


/*-----------------------------------------------------------------------*/
/**
 * Start recording animation
 */
void ScreenSnapShot_BeginRecording(bool bCaptureChange, int nFramesPerSecond)
{
	/* Set in globals */
	bGrabWhenChange = bCaptureChange;
	/* Set animation timer rate */
	GrabFrameCounter = 0;
	GrabFrameLatch = (int)(50.0f/(float)nFramesPerSecond);
	/* Start animation */
	bRecordingAnimation = TRUE;

	/* And inform user */
	Log_AlertDlg(LOG_INFO, "Screenshot recording started.");
}


/*-----------------------------------------------------------------------*/
/**
 * Stop recording animation
 */
void ScreenSnapShot_EndRecording()
{
	/* Were we recording? */
	if (bRecordingAnimation)
	{
		/* Stop animation */
		bRecordingAnimation = FALSE;

		/* And inform user */
		Log_AlertDlg(LOG_INFO, "Screenshot recording stopped.");
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Recording animation frame
 */
void ScreenSnapShot_RecordFrame(bool bFrameChanged)
{
	/* As we recording? */
	if (bRecordingAnimation)
	{
		/* Yes, but on a change basis or a timer? */
		if (bGrabWhenChange)
		{
			/* On change, so did change this frame? */
			if (bFrameChanged)
				ScreenSnapShot_SaveScreen();
		}
		else
		{
			/* On timer, check for latch and save */
			GrabFrameCounter++;
			if (GrabFrameCounter>=GrabFrameLatch)
			{
				ScreenSnapShot_SaveScreen();
				GrabFrameCounter = 0;
			}
		}
	}
}
