/*
  Hatari - screenSnapShot.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen Snapshots.
*/
const char ScreenSnapShot_fileid[] = "Hatari screenSnapShot.c";

#include <dirent.h>
#include <string.h>
#include "main.h"
#include "configuration.h"
#include "file.h"
#include "log.h"
#include "scrConvSt.h"
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


/*-----------------------------------------------------------------------*/
/**
 * Scan working directory to get the screenshot number
 */
static void ScreenSnapShot_GetNum(void)
{
	char dummy[5];
	int i, num;
	DIR *workingdir = opendir(Configuration_GetScreenShotDir());
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
 * Save current screen surface as PNG.
 * Return PNG file size for success, -1 for fail
 */
static int ScreenSnapShot_SavePNG(const char *filename)
{
	FILE *fp = NULL;
	int ret, bottom;
	uint32_t *pixels;
	int sw, sh, pitch;
  
	fp = fopen(filename, "wb");
	if (!fp)
		return -1;

	if (ConfigureParams.Screen.bCrop)
		bottom = Statusbar_GetHeight();
	else
		bottom = 0;

	Screen_GetDimension(&pixels, &sw, &sh, &pitch);

	/* default compression/filter and configured cropping */
	ret = ScreenSnapShot_SavePNG_ToFile(pixels, pitch, sw, sh, 0, 0,
	                                    fp, -1, -1, 0, 0, 0, bottom);

	fclose (fp);
	return ret;					/* >0 if OK, -1 if error */
}


/**
 * Save given frame as PNG in an already opened FILE, eventually cropping some borders.
 * Return png file size > 0 for success.
 * This function is also used by avi_record.c to save individual frames as png images.
 */
int ScreenSnapShot_SavePNG_ToFile(uint32_t *pixels, int pitch, int src_w, int src_h,
		int dw, int dh, FILE *fp, int png_compression_level, int png_filter,
		int CropLeft , int CropRight , int CropTop , int CropBottom)
{
	int y, ret;
	int sw = src_w - CropLeft - CropRight;
	int sh = src_h - CropTop - CropBottom;
	uint32_t *src_ptr;
	uint8_t *rowbuf;
	png_infop info_ptr = NULL;
	png_structp png_ptr;
	png_text pngtext;
	char key[] = "Title";
	char text[] = "Hatari screenshot";
	off_t start;
	bool do_palette = true;
	png_color png_pal[256];
	uint8_t palbuf[3];

	if (!dw)
		dw = sw;
	if (!dh)
		dh = sh;

	rowbuf = alloca(3 * dw);

	Screen_Lock();
	/* Use current ST palette if all colours in the image belong to it, otherwise RGB */
	for (y = 0; y < dh; y++)
	{
		src_ptr = pixels + (CropTop + (y * sh + dh/2) / dh) * (pitch / 4)
		          + CropLeft;
		if (!PixelConvert_32to8Bits(rowbuf, src_ptr, dw, src_w))
			do_palette = false;
	}
	Screen_UnLock();

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
			PixelConvert_32to24Bits(palbuf, (uint32_t *)(ConvertPalette+y), 1, src_w);
			png_pal[y].red   = palbuf[0];
			png_pal[y].green = palbuf[1];
			png_pal[y].blue  = palbuf[2];
		}
		png_set_PLTE(png_ptr, info_ptr, png_pal, ConvertPaletteSize);
	}

	/* write the file header information */
	png_write_info(png_ptr, info_ptr);

	/* write surface data rows one at a time (after cropping if necessary) */
	for (y = 0; y < dh; y++)
	{
		/* need to lock the surface while accessing it directly */
		Screen_Lock();

		src_ptr = pixels + (CropTop + (y * sh + dh/2) / dh) * (pitch / 4)
		          + CropLeft;

		if (!do_palette)
		{
			/* unpack 32-bit RGBA pixels */
			PixelConvert_32to24Bits(rowbuf, src_ptr, dw, src_w);
		}
		else
		{
			/* Reindex back to ST palette
			 * Note that this cannot disambiguate indices if the palette has duplicate colors */
			PixelConvert_32to8Bits(rowbuf, src_ptr, dw, src_w);
		}
		/* and unlock surface before syscalls */
		Screen_UnLock();
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
 * Determine the internally used screen dimensions, bits per pixel, and whether GenConv is used.
 */
static void ScreenSnapShot_GetInternalFormat(bool *genconv, int *sw, int *sh, int *bpp, uint32_t* line_size)
{
	*genconv = Config_IsMachineFalcon() || Config_IsMachineTT() || bUseVDIRes;
	/* genconv here is almost the same as Screen_UseGenConvScreen, but omits bUseHighRes,
	 * which is a hybrid GenConvert that also fills pFrameBuffer. */

	*sw = (STRes == ST_LOW_RES) ? 320 : 640;
	*sh = (STRes == ST_HIGH_RES) ? 400 : 200;
	*bpp = 4;
	if (STRes == ST_MEDIUM_RES) *bpp = 2;
	if (STRes == ST_HIGH_RES) *bpp = 1;
	if (*genconv)
	{
		/* Assume resolution based on GenConvert. */
		*bpp = ConvertBPP;
		*sw = ConvertW;
		*sh = ConvertH;
	}
	*line_size = (uint32_t)(*bpp * ((*sw + 15) & ~15)) / 8; /* size of line data in bytes, rounded up to 16 pixels */
}


/**
 * Save direct video memory dump to NEO file
 * return 1 for success, -1 for fail
 */
static int ScreenSnapShot_SaveNEO(const char *filename)
{
	FILE *fp = NULL;
	int i, res, sw, sh, bpp, offset;
	bool genconv;
	uint32_t video_base, line_size;
	uint16_t header[64];

	ScreenSnapShot_GetInternalFormat(&genconv, &sw, &sh, &bpp, &line_size);
	/* Convert BPP to ST resolution number */
	if (bpp == 4)
		res = 0;
	else if (bpp == 2)
		res = 1;
	else if (bpp == 1)
		res = 2;
	else	/* Preventing NEO screenshots with unexpected BPP or dimensions. */
	{
		/* The NEO header contains only 16 palette entries, so 8bpp would need extra palette information,
		 * and 16bpp true color mode is not supported by existing NEO tools. */
		Log_AlertDlg(LOG_ERROR, "The .NEO screenshot format does not support the color depth of the current video mode.");
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

	memset(header, 0, sizeof(header));
	header[0] = be_swap16(0); /* Flags field (always 0). */
	header[1] = be_swap16(res); /* NEO resolution word is the primary indicator of BPP. */

	/* ST Low/Medium resolution stores a palette for each line. Using the centre line's palette. */
	if (!genconv && res != 2 && pFrameBuffer)
	{
		for (i=0; i<16; i++)
			header[2+i] = be_swap16(pFrameBuffer->HBLPalettes[i+((OVERSCAN_TOP+sh/2)<<4)]);
	}
	else /* High resolution or other GenConvert: use stored GenConvert RGB palette. */
	{
		uint8_t r, g, b;

		for (i=0; i<16; i++)
		{
			Screen_GetPaletteColor(i, &r, &g, &b);
			header[2+i] = be_swap16(
				((r >> 5) << 8) |
				((g >> 5) << 4) |
				((b >> 5) << 0));
		}
		/* Note that this 24-bit palette is being approximated as a 9-bit ST color palette,
		 * and 256 colors needed for 8bpp cannot be expressed in this header. */
	}

	/* Use filename field to indicate Hatari source and format. */
	memcpy(header + 18, "HATARI  0BPP", 12);
	((uint8_t*)(header+18))[8] = '0' + bpp;

	header[29] = be_swap16(sw); /* screen width */
	header[30] = be_swap16(sh); /* screen height */

	fwrite(header, 1, 128, fp);

	/* ST modes fill pFrameBuffer->pSTScreen from each scanline, during Video_EndHBL. */
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
 * return 1 for success, -1 for fail
 */
static int ScreenSnapShot_SaveXIMG(const char *filename)
{
	FILE *fp = NULL;
	int i, j, k, sw, sh, bpp, offset;
	bool genconv;
	uint16_t colst, colr, colg, colb;
	uint32_t video_base, line_size;
	uint16_t header_size;
	uint8_t *scanline;
	uint16_t header[11];

	ScreenSnapShot_GetInternalFormat(&genconv, &sw, &sh, &bpp, &line_size);

	if (bpp > 8 && bpp != 16)
	{
		/* bpp = 24 is a possible format for XIMG but Hatari's screenConvert only supports 16-bit true color. */
		Log_AlertDlg(LOG_ERROR,"XIMG screenshot only supports up to 8-bit palette, or 16-bit true color.");
		return -1;
	}

	fp = fopen(filename, "wb");
	if (!fp)
		return -1;

	/* XIMG header */
	header_size = (8 + 3) * 2;			/* IMG + XIMG */
	if (bpp <= 8)					/* palette */
		header_size += (3 * 2) * (1 << bpp);
	memset(header, 0, sizeof(header));
	header[0] = be_swap16(1);			/* version */
	header[1] = be_swap16(header_size/2);
	header[2] = be_swap16(bpp);			/* bitplanes */
	header[3] = be_swap16(2);			/* pattern length (unused) */
	header[4] = be_swap16(0x55);			/* pixel width (microns) */
	header[5] = be_swap16(0x55);			/* pixel height (microns) */
	header[6] = be_swap16(sw);			/* screen width */
	header[7] = be_swap16(sh);			/* screen height */
	memcpy(header+8,"XIMG",4);
	header[10] = be_swap16(0);			/* XIMG RGB palette format */
	fwrite(header, 1, (8 + 3) * 2, fp);

	/* XIMG RGB format, word triples each 0-1000 */
	if (bpp <= 8)
	{
		uint8_t r, g, b;

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
				Screen_GetPaletteColor(i, &r, &g, &b);
				colr = (uint16_t)((1000 * r) / 255);
				colg = (uint16_t)((1000 * g) / 255);
				colb = (uint16_t)((1000 * b) / 255);
			}
			header[0] = be_swap16(colr);
			header[1] = be_swap16(colg);
			header[2] = be_swap16(colb);
			fwrite(header, 1, 3 * 2, fp);
		}
	}

	/* Image data, no compression is attempted */
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
				fputc(0x80,fp);			/* uncompressed data packet */
				fputc((sw+7)/8,fp);		/* one plane of line per packet */
				for (k=0; k<sw; k+=8)
				{
					offset = ((((k / 16) * bpp) + j) * 2) + ((k / 8) & 1); /* interleaved word + byte pair */
					fputc(scanline[offset],fp);
				}
			}
		}
		else if (bpp == 16)
		{
			/* Falcon native chunky 5:6:5 format */
			j = (sw * 2);				/* bytes per line */
			while (j > 0)				/* break into <= 254 byte packets */
			{
				k = (j > 254) ? 254 : j;	/* bytes in packet */
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


/**
 * Save screen shot file with filename like 'grab0000.[png|bmp]',
 * 'grab0001.[png|bmp]', etc... Whether screen shots are saved as BMP
 * or PNG depends on Hatari configuration.
 */
void ScreenSnapShot_SaveScreen(void)
{
	char *szFileName = malloc(FILENAME_MAX);
	const char *ext, *name, *path;
	int (*savefn)(const char *);

	if (!szFileName)  return;

	/* Create our filename */
	path = Configuration_GetScreenShotDir();
	ScreenSnapShot_GetNum();
	nScreenShots++;

	switch(ConfigureParams.Screen.ScreenShotFormat)
	{
#if HAVE_LIBPNG
	case SCREEN_SNAPSHOT_PNG:
		savefn = ScreenSnapShot_SavePNG;
		name = "PNG";
		ext = "png";
		break;
#endif
	case SCREEN_SNAPSHOT_NEO:
		savefn = ScreenSnapShot_SaveNEO;
		name = "NEO";
		ext = "neo";
		break;
	case SCREEN_SNAPSHOT_XIMG:
		savefn = ScreenSnapShot_SaveXIMG;
		name = "XIMG";
		ext = "ximg";
		break;
	case SCREEN_SNAPSHOT_BMP:
	default:
		savefn = Screen_SaveBMP;
		name = "BMP";
		ext = "bmp";
		break;
	}

	sprintf(szFileName, "%s/grab%4.4d.%s", path, nScreenShots, ext);

	if (savefn(szFileName) > 0)
	{
		/* use LOG_WARN also for success as users want to see the path */
		Log_Printf(LOG_WARN, "%s screen dump saved to: %s", name, szFileName);
	}
	else
	{
		Log_Printf(LOG_WARN, "Failed to save %s screen dump to: %s!", name, szFileName);
	}

	free(szFileName);
}

/**
 * Save screen shot to given file.
 */
void ScreenSnapShot_SaveToFile(const char *szFileName)
{
	int ret;

	if (!szFileName)
	{
		fprintf(stderr, "ERROR: no screen dump file name specified\n");
		return;
	}
#if HAVE_LIBPNG
	if (File_DoesFileExtensionMatch(szFileName, ".png"))
	{
		ret = ScreenSnapShot_SavePNG(szFileName);
	}
	else
#endif
	if (File_DoesFileExtensionMatch(szFileName, ".bmp"))
	{
		ret = Screen_SaveBMP(szFileName);
	}
	else if (File_DoesFileExtensionMatch(szFileName, ".neo"))
	{
		ret = ScreenSnapShot_SaveNEO(szFileName);
	}
	else if (File_DoesFileExtensionMatch(szFileName, ".ximg") || File_DoesFileExtensionMatch(szFileName, ".img"))
	{
		ret = ScreenSnapShot_SaveXIMG(szFileName);
	}
	else
	{
		fprintf(stderr, "ERROR: unknown screen dump file name extension: %s\n", szFileName);
		return;
	}
	fprintf(stderr, "Screen dump to '%s' %s\n", szFileName,
		ret > 0 ? "succeeded" : "failed");
}
