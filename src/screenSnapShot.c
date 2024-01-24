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
#include "screenConvert.h"
#include "screenSnapShot.h"
#include "statusbar.h"
#include "vdi.h"
#include "video.h"
#include "stMemory.h"
/* after above that bring in config.h */
#if HAVE_LIBPNG
# include <png.h>
# include <assert.h>
# include "pixel_convert.h"				/* inline functions */
#endif


static int nScreenShots = 0;                /* Number of screen shots saved */
static Uint8 NEOHeader[128];


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
	off_t start;
	bool do_palette = true;
	static png_color png_pal[256];
	static Uint8 palbuf[3];

	if (!dw)
		dw = sw;
	if (!dh)
		dh = sh;

	rowbuf = alloca(3 * dw);

	/* Use current ST palette if all colours in the image belong to it, otherwise RGB */
	do_lock = SDL_MUSTLOCK(surface);
	if (do_lock)
		SDL_LockSurface(surface);
	for (y = 0; y < dh && do_palette; y++)
	{
		src_ptr = (Uint8 *)surface->pixels
		          + (CropTop + (y * sh + dh/2) / dh) * surface->pitch
		          + CropLeft * surface->format->BytesPerPixel;
		switch (fmt->BytesPerPixel)
		{
		 case 2:
			if (!PixelConvert_16to8Bits(rowbuf, (Uint16*)src_ptr, dw, surface))
				do_palette = false;
			break;
		 case 4:
			if (!PixelConvert_32to8Bits(rowbuf, (Uint32*)src_ptr, dw, surface))
				do_palette = false;
			break;
		 default:
			abort();
		}
	}
	if (do_lock)
		SDL_UnlockSurface(surface);

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
	png_set_IHDR(png_ptr, info_ptr, dw, dh, 8,
		     do_palette ? PNG_COLOR_TYPE_PALETTE : PNG_COLOR_TYPE_RGB,
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

	if (do_palette)
	{
		/* Generate palette for PNG */
		for (y = 0; y < ConvertPaletteSize; y++)
		{
			switch (fmt->BytesPerPixel)
			{
			 case 2:
				PixelConvert_16to24Bits(palbuf, (Uint16*)(ConvertPalette+y), 1, surface);
				break;
			 case 4:
				PixelConvert_32to24Bits(palbuf, (Uint32*)(ConvertPalette+y), 1, surface);
				break;
			 default:
				abort();
			}
			png_pal[y].red   = palbuf[0];
			png_pal[y].green = palbuf[1];
			png_pal[y].blue  = palbuf[2];
		}
		png_set_PLTE(png_ptr, info_ptr, png_pal, ConvertPaletteSize);
	}

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

		if (!do_palette)
		{
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
		}
		else
		{
			/* Reindex back to ST palette
			 * Note that this cannot disambiguate indices if the palette has duplicate colors */
			switch (fmt->BytesPerPixel)
			{
			 case 2:
				PixelConvert_16to8Bits(rowbuf, (Uint16*)src_ptr, dw, surface);
				break;
			 case 4:
				PixelConvert_32to8Bits(rowbuf, (Uint32*)src_ptr, dw, surface);
				break;
			 default:
				abort();
			}
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



/**
 * Helper for writing NEO file header
 */
static void StoreU16NEO(Uint16 val, int offset)
{
	NEOHeader[offset+0] = (val >> 8) & 0xFF;
	NEOHeader[offset+1] = (val >> 0) & 0xFF;
}

/**
 * Save direct video memory dump to NEO file
 */
static int ScreenSnapShot_SaveNEO(const char *filename)
{
	FILE *fp = NULL;
	int i, res, sw, sh, bpp, offset;
	SDL_Color col;
	uint32_t video_base, line_size;
	bool genconv = Config_IsMachineFalcon() || Config_IsMachineTT() || bUseVDIRes;
	/* genconv here is almost the same as Screen_UseGenConvScreen, but omits bUseHighRes,
	 * which is a hybrid GenConvert that also fills pFrameBuffer. */

	res = (STRes == ST_HIGH_RES) ? 2 :
	      (STRes == ST_MEDIUM_RES) ? 1 :
	      0;
	sw = (res > 0) ? 640 : 320;
	sh = (res == 2) ? 400 : 200;
	bpp = 4;
	if      (res == 1) bpp = 2;
	else if (res == 2) bpp = 1;

	if (genconv)
	{
		/* Assume resolution based on GenConvert. */
		bpp = ConvertBPP;
		sw = ConvertW;
		sh = ConvertH;
		/* If BPP matches an ST resolution, use that.
		 * otherwise just use the BPP itself instead of that number. */
		res = bpp;
		if      (bpp == 4) res = 0;
		else if (bpp == 2) res = 1;
		else if (bpp == 1) res = 2;
	}

	/* Preventing NEO screenshots with unexpected BPP or dimensions. */
	if (res > 2)
	{
		/* The NEO header contains only 16 palette entries, so 8bpp would need extra palette information,
		 * and 16bpp true color mode is not supported by existing NEO tools. */
		Log_AlertDlg(LOG_ERROR,"The current video mode has too many colors for the .NEO screenshot format");
		return -1;
	}
	if ((res == 0 && sw != 320) || (res < 2 && sh != 200) || (res > 0 && sw != 640) || (res == 2 && sh != 400))
	{
		/* The NEO header contains dimension info, and any width that is a multiple of 16 pixels should be theoretically valid,
		 * but existing NEO tools mostly ignore the dimension fields. */
		Log_AlertDlg(LOG_ERROR,"The current video mode has non-standard resolution dimensions, unable to save in .NEO screenshot format");
		return -1;
	}

	fp = fopen(filename, "wb");
	if (!fp)
		return -1;

	memset(NEOHeader, 0, sizeof(NEOHeader));
	StoreU16NEO(res, 2); /* NEO resolution word is the primary indicator of BPP. */

	/* ST Low/Medium resolution stores a palette for each line. Using the centre line's palette. */
	if (!genconv && res != 2 && pFrameBuffer)
	{
		for (i=0; i<16; i++)
			StoreU16NEO(pFrameBuffer->HBLPalettes[i+((OVERSCAN_TOP+sh/2)<<4)], 4+(2*i));
	}
	else /* High resolution or other GenConvert: use stored GenConvert RGB palette. */
	{
		for (i=0; i<16; i++)
		{
			col = Screen_GetPaletteColor(i);
			StoreU16NEO(
				((col.r >> 5) << 8) |
				((col.g >> 5) << 4) |
				((col.b >> 5) << 0),
				4+(2*i));
		}
		/* Note that this 24-bit palette is being approximated as a 9-bit ST color palette,
		 * and 256 colors needed for 8bpp cannot be expressed in this header. */
	}
	memcpy(NEOHeader+36,"HATARI  4BPP",12); /* Use internal filename to give a hint about bitplanes. */
	NEOHeader[36+8] = '0' + (bpp % 10);
	if (bpp >= 10) NEOHeader[36+7] = '0' + (bpp / 10);
	StoreU16NEO(sw, 58);
	StoreU16NEO(sh, 60);

	fwrite(NEOHeader, 1, 128, fp);
	
	/* ST modes fill pFrameBuffer->pSTScreen from each scanline, during Video_EndHBL. */
	line_size = (uint32_t)(bpp * ((sw + 15) & 15)) / 8; /* size of line data in bytes */
	if (!genconv && pFrameBuffer && pFrameBuffer->pSTScreen)
	{
		for (i = 0; i < sh; i++)
		{
			offset = (res == 2) ?
				(SCREENBYTES_MONOLINE * i) :
				(STScreenLineOffset[i+OVERSCAN_TOP] + SCREENBYTES_LEFT);
			fwrite(pFrameBuffer->pSTScreen + offset, 1, line_size, fp);
		}
	}
	else /* TT/Falcon bypass Video_EndHBL, so pFrameBuffer is unused.
	      * As a fallback we just copy the video data from ST RAM. */
	{
		video_base = Video_GetScreenBaseAddr();
		
		for (i = 0; i < sh; i++)
		{
			if ((video_base + line_size) <= STRamEnd)
			{
				fwrite(STRam + video_base, 1, line_size, fp);
				video_base += ConvertNextLine;
			}
			else
			{
				fclose(fp);
				return -1;
			}
		}
	}

	fclose (fp);
	return 1; /* >0 if OK, -1 if error */
}


/**
 * Save direct video memory dump to XIMG file
 */
static int ScreenSnapShot_SaveXIMG(const char *filename)
{
	FILE *fp = NULL;
	int i, j, k, sw, sh, bpp, offset;
	SDL_Color col;
	uint16_t colst, colr, colg, colb;
	uint32_t video_base, line_size;
	uint16_t header_size;
	uint8_t *scanline;
	bool genconv = Config_IsMachineFalcon() || Config_IsMachineTT() || bUseVDIRes;

	sw = (STRes == ST_LOW_RES) ? 320 : 640;
	sh = (STRes == ST_HIGH_RES) ? 400 : 200;
	bpp = 4;
	if (STRes == ST_MEDIUM_RES) bpp = 2;
	if (STRes == ST_HIGH_RES) bpp = 1;

	if (genconv)
	{
		bpp = ConvertBPP;
		sw = ConvertW;
		sh = ConvertH;
	}

	if (bpp > 8 && bpp != 16)
	{
		/* bpp = 24 is a possible format for XIMG but Hatari's screenConvert only supports 16-bit true color.*/
		Log_AlertDlg(LOG_ERROR,"XIMG screenshot only supports up to 8-bit palette, or 16-bit true color.");
		return -1;
	}

	fp = fopen(filename, "wb");
	if (!fp)
		return -1;

	/* XIMG header */
	header_size = 16 + 6; /* IMG + XIMG */
	if (bpp <= 8) /* palette */
		header_size += (3 * 2) * (1 << bpp);
	memset(NEOHeader, 0, sizeof(NEOHeader));
	StoreU16NEO(1,0); /* version */
	StoreU16NEO(header_size/2,2);
	StoreU16NEO(bpp,4); /* bitplanes */
	StoreU16NEO(2,6); /* pattern length (unused) */
	StoreU16NEO(0x55,8); /* pixel width (microns) */
	StoreU16NEO(0x55,10); /* pixel height (microns) */
	StoreU16NEO(sw,12); /* pixel width */
	StoreU16NEO(sh,14); /* pixel height */
	memcpy(NEOHeader+16,"XIMG",4);
	StoreU16NEO(0,20); /* XIMG RGB palette format */
	fwrite(NEOHeader, 1, 16 + 6, fp);
	
	/* XIMG RGB format, word triples each 0-1000 */
	if (bpp <= 8)
	{
		for (i=0; i<(1<<bpp); i++)
		{
			if (!genconv && (sh < 300) && (bpp <= 4) && pFrameBuffer) /* ST palette, use centre line */
			{
				colst = pFrameBuffer->HBLPalettes[i+((OVERSCAN_TOP+sh/2)<<4)];
				colr = (uint16_t)((((colst >> 8) & 7) * 1000) / 7);
				colg = (uint16_t)((((colst >> 4) & 7) * 1000) / 7);
				colb = (uint16_t)((((colst >> 0) & 7) * 1000) / 7);
			}
			else /* High resolution or GenConvert palette */
			{
				col = Screen_GetPaletteColor(i);
				colr = (uint16_t)((col.r * 1000) / 255);
				colg = (uint16_t)((col.g * 1000) / 255);
				colb = (uint16_t)((col.b * 1000) / 255);
			}
			StoreU16NEO(colr,0);
			StoreU16NEO(colg,2);
			StoreU16NEO(colb,4);
			fwrite(NEOHeader,1,(3*2),fp);
		}
	}

	/* Image data, no compression is attempted */
	line_size = (uint32_t)(bpp * ((sw + 15) & 15)) / 8; /* size of line data in bytes */
	for (i = 0; i < sh; i++)
	{
		/* Find line of scanline data */
		if (!genconv && pFrameBuffer && pFrameBuffer->pSTScreen)
		{
			scanline = pFrameBuffer->pSTScreen + (
				(sh >= 300) ? (SCREENBYTES_MONOLINE * i) :
				(STScreenLineOffset[i+OVERSCAN_TOP] + SCREENBYTES_LEFT) );
		}
		else
		{
			video_base = Video_GetScreenBaseAddr() + (i * ConvertNextLine);
			if ((video_base + line_size) <= STRamEnd)
			{
				scanline = STRam + video_base;
			}
			else
			{
				fclose(fp);
				return -1;
			}
		}
		
		if (bpp <= 8)
		{
			/* de-interleave scanline into XIMG format */
			for (j=0; j<bpp; ++j)
			{
				fputc(0x80,fp); /* uncompressed data packet */
				fputc((sw+7)/8,fp); /* one plane of line per packet */
				for (k=0; k<sw; k+=8)
				{
					offset = ((((k / 16) * bpp) + j) * 2) + ((k / 8) & 1); /* interleaved word + byte pair*/
					fputc(scanline[offset],fp);
				}
			}
		}
		else if (bpp == 16)
		{
			/* Falcon native chunky 5:6:5 format */
			j = (sw * 2); /* bytes per line */
			while (j > 0) /* break into <= 254 byte packets */
			{
				k = (j > 254) ? 254 : j; /* bytes in packet */
				fputc(0x80,fp);
				fputc(k,fp);
				fwrite(scanline,1,k,fp);
				j -= k;
				scanline += k;
			}
		}
		else
		{
			fclose(fp);
			return -1;
		}
	}

	fclose (fp);
	return 1; /* >0 if OK, -1 if error */
}


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


	/* BMP format */
	if (ConfigureParams.Screen.ScreenShotFormat == SCREEN_SNAPSHOT_BMP )
	{
		sprintf(szFileName,"%s/grab%4.4d.bmp", Paths_GetScreenShotDir(), nScreenShots);
		if (SDL_SaveBMP(sdlscrn, szFileName))
			fprintf(stderr, "BMP screen dump failed!\n");
		else
			fprintf(stderr, "BMP screen dump saved to: %s\n", szFileName);
		free(szFileName);
		return;
	}

#if HAVE_LIBPNG
	/* PNG format */
	else if (ConfigureParams.Screen.ScreenShotFormat == SCREEN_SNAPSHOT_PNG )
	{
		sprintf(szFileName,"%s/grab%4.4d.png", Paths_GetScreenShotDir(), nScreenShots);
		if (ScreenSnapShot_SavePNG(sdlscrn, szFileName) > 0)
			fprintf(stderr, "PNG screen dump saved to: %s\n", szFileName);
		else
			fprintf(stderr, "PNG screen dump failed!\n");
		free(szFileName);
		return;
	}
#endif

	/* NEO format */
	else if (ConfigureParams.Screen.ScreenShotFormat == SCREEN_SNAPSHOT_NEO )
	{
		sprintf(szFileName,"%s/grab%4.4d.neo", Paths_GetScreenShotDir(), nScreenShots);
		if (ScreenSnapShot_SaveNEO(szFileName) > 0)
			fprintf(stderr, "NEO screen dump saved to: %s\n", szFileName);
		else
			fprintf(stderr, "NEO screen dump failed!\n");
		free(szFileName);
		return;
	}
	/* XIMG format */
	else if (ConfigureParams.Screen.ScreenShotFormat == SCREEN_SNAPSHOT_XIMG )
	{
		sprintf(szFileName,"%s/grab%4.4d.ximg", Paths_GetScreenShotDir(), nScreenShots);
		if (ScreenSnapShot_SaveXIMG(szFileName) > 0)
			fprintf(stderr, "XMIG screen dump saved to: %s\n", szFileName);
		else
			fprintf(stderr, "XIMG screen dump failed!\n");
		free(szFileName);
		return;
	}

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
	else if (File_DoesFileExtensionMatch(szFileName, ".neo"))
	{
		success = ScreenSnapShot_SaveNEO(szFileName) == 0;
	}
	else if (File_DoesFileExtensionMatch(szFileName, ".ximg") || File_DoesFileExtensionMatch(szFileName, ".img"))
	{
		success = ScreenSnapShot_SaveXIMG(szFileName) == 0;
	}
	else
	{
		fprintf(stderr, "ERROR: unknown screen dump file name extension: %s\n", szFileName);
		return;
	}
	fprintf(stderr, "Screen dump to '%s' %s\n", szFileName,
		success ? "succeeded" : "failed");
}
