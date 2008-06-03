/*
  Hatari - screen.c

  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.

  This code converts a 1/2/4 plane ST format screen to either 8 or 16-bit PC
  format. An awful lot of processing is needed to do this conversion - we
  cannot simply change palettes on  interrupts as it is possible with DOS.
  The main code processes the palette/resolution mask tables to find exactly
  which lines need to updating and the conversion routines themselves only
  update 16-pixel blocks which differ from the previous frame - this gives a
  large performance increase.
  Each conversion routine can convert any part of the source ST screen (which
  includes the overscan border, usually set to colour zero) so they can be used
  for both window and full-screen mode.
  Note that in Hi-Resolution we have no overscan and just two colors so we can
  optimise things further. Also when running in maximum speed we make sure we
  only convert the screen every 50 times a second - inbetween frames are not
  processed.
*/

/* 2008/06/01	[NP]	Although it seems possible to display 47 lines in the bottom border	*/
/*			with a second 60/50 Hz switch, most programs consider there're 45	*/
/*			visible lines in the bottom border, which gives a total of 274 lines	*/
/*			for a screen. This fixes garbages that could appear in the last two	*/
/*			lines when displaying 47 lines (Digiwolrd 2 by ICE, Tyranny by DHS).	*/


const char Screen_rcsid[] = "Hatari $Id: screen.c,v 1.84 2008-06-03 19:41:27 eerot Exp $";

#include <SDL.h>
#include <SDL_endian.h>

#include "main.h"
#include "configuration.h"
#include "ikbd.h"
#include "log.h"
#include "m68000.h"
#include "paths.h"
#include "screen.h"
#include "convert/routines.h"
#include "screenSnapShot.h"
#include "sound.h"
#include "spec512.h"
#include "vdi.h"
#include "video.h"
#include "falcon/videl.h"
#include "falcon/hostscreen.h"


/* extern for several purposes */
SDL_Surface *sdlscrn = NULL;                /* The SDL screen surface */
int nScreenZoomX, nScreenZoomY;             /* Zooming factors, used for scaling mouse motions */
int nBorderPixelsLeft, nBorderPixelsRight;  /* Pixels in left and right border */
int nBorderPixelsBottom;                    /* Lines in bottom border */

/* extern for shortcuts and falcon/hostscreen.c */
bool bGrabMouse = FALSE;      /* Grab the mouse cursor in the window */
bool bInFullScreen = FALSE;   /* TRUE if in full screen */

/* extern for spec512.c */
int STScreenLeftSkipBytes;
int STScreenStartHorizLine;   /* Start lines to be converted */
Uint32 STRGBPalette[16];      /* Palette buffer used in conversion routines */
Uint32 ST2RGB[4096];          /* Table to convert ST 0x777 / STe 0xfff palette to PC format RGB551 (2 pixels each entry) */

/* extern for video.c */
Uint8 *pSTScreen;
FRAMEBUFFER *pFrameBuffer;    /* Pointer into current 'FrameBuffer' */

static FRAMEBUFFER FrameBuffers[NUM_FRAMEBUFFERS]; /* Store frame buffer details to tell how to update */
static Uint8 *pSTScreenCopy;                       /* Keep track of current and previous ST screen data */
static Uint8 *pPCScreenDest;                       /* Destination PC buffer */
static int STScreenEndHorizLine;                   /* End lines to be converted */
static int PCScreenBytesPerLine;
static int STScreenWidthBytes;

static int STScreenLineOffset[NUM_VISIBLE_LINES];  /* Offsets for ST screen lines eg, 0,160,320... */
static Uint16 HBLPalette[16], PrevHBLPalette[16];  /* Current palette for line, also copy of first line */

static void (*ScreenDrawFunctionsNormal[3])(void); /* Screen draw functions */
static void (*ScreenDrawFunctionsVDI[3])(void) =
{
	ConvertVDIRes_16Colour,
	ConvertVDIRes_4Colour,
	ConvertVDIRes_2Colour
};

static bool bScreenContentsChanged;     /* TRUE if buffer changed and requires blitting */
static bool bScrDoubleY;                /* TRUE if double on Y */
static int ScrUpdateFlag;               /* Bit mask of how to update screen */


/*-----------------------------------------------------------------------*/
/**
 * Create ST 0x777 / STe 0xfff color format to 16 or 32 bits per pixel
 * conversion table. Called each time when changed resolution or to/from
 * fullscreen mode.
 */
static void Screen_SetupRGBTable(void)
{
	Uint16 STColor;
	Uint32 RGBColor;
	int r, g, b;
	int rr, gg, bb;

	/* Do Red, Green and Blue for all 16*16*16 = 4096 STe colors */
	for (r = 0; r < 16; r++)
	{
		for (g = 0; g < 16; g++)
		{
			for (b = 0; b < 16; b++)
			{
				/* STe 0xfff format */
				STColor = (r<<8) | (g<<4) | (b);
				rr = ((r & 0x7) << 5) | ((r & 0x8) << 1);
				gg = ((g & 0x7) << 5) | ((g & 0x8) << 1);
				bb = ((b & 0x7) << 5) | ((b & 0x8) << 1);
				RGBColor = SDL_MapRGB(sdlscrn->format, rr, gg, bb);
				if (sdlscrn->format->BitsPerPixel <= 16)
				{
					/* As longs, for speed (write two pixels at once) */
					ST2RGB[STColor] = (RGBColor<<16) | RGBColor;
				}
				else
				{
					ST2RGB[STColor] = RGBColor;
				}
			}
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Create new palette for display.
 */
static void Screen_CreatePalette(void)
{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	static const int endiantable[16] = {0,2,1,3,8,10,9,11,4,6,5,7,12,14,13,15};
#endif
	SDL_Color sdlColors[16];
	int i, j;

	if (bUseHighRes)
	{
		/* Colors for monochrome screen mode emulation */
		if (HBLPalettes[0])
		{
			sdlColors[0].r = sdlColors[0].g = sdlColors[0].b = 255;
			sdlColors[1].r = sdlColors[1].g = sdlColors[1].b = 0;
		}
		else
		{
			sdlColors[0].r = sdlColors[0].g = sdlColors[0].b = 0;
			sdlColors[1].r = sdlColors[1].g = sdlColors[1].b = 255;
		}
		SDL_SetColors(sdlscrn, sdlColors, 10, 2);
		/*SDL_SetColors(sdlscrn, sdlColors, 0, 2);*/
	}
	else
	{
		int r, g, b;
		/* Colors for STe color screen mode emulation */
		for (i = 0; i < 16; i++)
		{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			j = endiantable[i];
#else
			j = i;
#endif
			/* normalize all to 0x1e0 */
			r = HBLPalettes[i] >> 3;
			g = HBLPalettes[i] << 1;
			b = HBLPalettes[i] << 5;
			/* move top bit of 0x1e0 to lowest in 0xf0 */
			sdlColors[j].r = (r & 0xe0) | ((r & 0x100) >> 4);
			sdlColors[j].g = (g & 0xe0) | ((g & 0x100) >> 4);
			sdlColors[j].b = (b & 0xe0) | ((b & 0x100) >> 4);
		}
		SDL_SetColors(sdlscrn, sdlColors, 10, 16);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Create 8-Bit palette for display if needed.
 */
static void Screen_Handle8BitPalettes(void)
{
	bool bPaletteChanged=FALSE;
	int i;

	/* Do need to check for 8-Bit palette change? Ie, update whole screen */
	/* VDI screens and monochrome modes are ALL 8-Bit at the moment! */
	if (sdlscrn->format->BitsPerPixel == 8)
	{
		/* If using HiRes palette update with full update flag */
		if (!bUseHighRes)
		{
			/* Check if palette of 16 colours changed from previous frame */
			for (i = 0; i < 16 && !bPaletteChanged; i++)
			{
				/* Check with first line palette (stored in 'Screen_ComparePaletteMask') */
				if (HBLPalettes[i] != PrevHBLPalette[i])
					bPaletteChanged = TRUE;
			}
		}

		/* Did palette change or do we require a full update? */
		if (bPaletteChanged || pFrameBuffer->bFullUpdate)
		{
			/* Create palette, for Full-Screen of Window */
			Screen_CreatePalette();
			/* Make sure update whole screen */
			pFrameBuffer->bFullUpdate = TRUE;
		}
	}

	/* Copy old palette for 8-Bit compare as this routine writes over it */
	memcpy(PrevHBLPalette,HBLPalettes, sizeof(Uint16)*16);
}


/*-----------------------------------------------------------------------*/
/**
 * Set screen draw functions.
 */
static void Screen_SetDrawFunctions(int nBitCount)
{
	if (nBitCount == 8)
	{
		/* Low color */
		if (ConfigureParams.Screen.bZoomLowRes)
			ScreenDrawFunctionsNormal[ST_LOW_RES] = ConvertLowRes_640x8Bit;
		else
			ScreenDrawFunctionsNormal[ST_LOW_RES] = ConvertLowRes_320x8Bit;
		ScreenDrawFunctionsNormal[ST_MEDIUM_RES] = ConvertMediumRes_640x8Bit;
		ScreenDrawFunctionsNormal[ST_HIGH_RES] = ConvertHighRes_640x8Bit;
	}
	else if (nBitCount <= 16)
	{
		/* High color */
		if (ConfigureParams.Screen.bZoomLowRes)
			ScreenDrawFunctionsNormal[ST_LOW_RES] = ConvertLowRes_640x16Bit;
		else
			ScreenDrawFunctionsNormal[ST_LOW_RES] = ConvertLowRes_320x16Bit;
		ScreenDrawFunctionsNormal[ST_MEDIUM_RES] = ConvertMediumRes_640x16Bit;
		ScreenDrawFunctionsNormal[ST_HIGH_RES] = ConvertHighRes_640x8Bit;
	}
	else /* Assume 32 bit drawing functions */
	{
		/* True color */
		if (ConfigureParams.Screen.bZoomLowRes)
			ScreenDrawFunctionsNormal[ST_LOW_RES] = ConvertLowRes_640x32Bit;
		else
			ScreenDrawFunctionsNormal[ST_LOW_RES] = ConvertLowRes_320x32Bit;
		ScreenDrawFunctionsNormal[ST_MEDIUM_RES] = ConvertMediumRes_640x32Bit;
		ScreenDrawFunctionsNormal[ST_HIGH_RES] = ConvertHighRes_640x8Bit;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Set amount of border pixels for windowed and full-screen mode and
 * store Y offset for each horizontal line in our source ST screen for
 * reference in tje convert functions.
 */
static void Screen_SetSTScreenOffsets(void)
{
	int i;

	/* Determine border pixels */
	if (bInFullScreen)
	{
		nBorderPixelsLeft = ConfigureParams.Screen.nFullScreenBorderPixelsLeft;
		nBorderPixelsRight = ConfigureParams.Screen.nFullScreenBorderPixelsRight;
		nBorderPixelsBottom = ConfigureParams.Screen.nFullScreenBorderPixelsBottom;
	}
	else
	{
		nBorderPixelsLeft = ConfigureParams.Screen.nWindowBorderPixelsLeft;
		nBorderPixelsRight = ConfigureParams.Screen.nWindowBorderPixelsRight;
		nBorderPixelsBottom = ConfigureParams.Screen.nWindowBorderPixelsBottom;
	}

	/* Store offset to each horizontal line */
	for (i = 0; i < NUM_VISIBLE_LINES; i++)
	{
		STScreenLineOffset[i] = i * SCREENBYTES_LINE;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Initialize SDL screen surface / set resolution.
 */
static void Screen_SetResolution(void)
{
	int Width, Height, BitCount;
	unsigned int sdlVideoFlags;

	Screen_SetSTScreenOffsets();  

	/* Determine which resolution to use */
	if (bUseVDIRes)
	{
		Width = VDIWidth;
		Height = VDIHeight;
	}
	else
	{
		if (STRes == ST_LOW_RES && !ConfigureParams.Screen.bZoomLowRes)
		{
			Width = 320;
			Height = 200;
		}
		else    /* else use 640x400 */
		{
			Width = 640;
			Height = 400;
		}

		/* Adjust width/height for overscan borders, if mono or VDI we have no overscan */
		if (ConfigureParams.Screen.bAllowOverscan && !bUseHighRes)
		{
			int nZoom = ((Width == 640) ? 2 : 1);
			/* Add in overscan borders (if 640x200 bitmap is double on Y) */
			Width += (nBorderPixelsLeft+nBorderPixelsRight) * nZoom;
			Height += (OVERSCAN_TOP+nBorderPixelsBottom) * nZoom;
		}
	}

	/* Bits per pixel */
	if (STRes == ST_HIGH_RES || bUseVDIRes)
	{
		BitCount = 8;
	}
	else
	{
		BitCount = ConfigureParams.Screen.nForceBpp;
	}

	/* Set zoom factors, used for scaling mouse motions */
	if (STRes == ST_LOW_RES && ConfigureParams.Screen.bZoomLowRes && !bUseVDIRes)
	{
		nScreenZoomX = 2;
		nScreenZoomY = 2;
	}
	else if (STRes == ST_MEDIUM_RES && !bUseVDIRes)
	{
		nScreenZoomX = 1;
		nScreenZoomY = 2;
	}
	else
	{
		nScreenZoomX = 1;
		nScreenZoomY = 1;
	}

	/* SDL Video attributes: */
	if (bInFullScreen)
	{
		sdlVideoFlags  = SDL_HWSURFACE|SDL_FULLSCREEN|SDL_HWPALETTE/*|SDL_DOUBLEBUF*/;
		/* SDL_DOUBLEBUF is a good idea, but the GUI doesn't work with double buffered
		 * screens yet, so double buffering is currently disabled. */
	}
	else
	{
		sdlVideoFlags  = SDL_SWSURFACE|SDL_HWPALETTE;
	}

	/* Check if we really have to change the video mode: */
	if (!sdlscrn || sdlscrn->w != Width || sdlscrn->h != Height
	    || sdlscrn->format->BitsPerPixel != BitCount
	    || (sdlscrn->flags&SDL_FULLSCREEN) != (sdlVideoFlags&SDL_FULLSCREEN))
	{
		/* Set new video mode */
		//fprintf(stderr,"Requesting video mode %i %i %i\n", Width, Height, BitCount);
		sdlscrn = SDL_SetVideoMode(Width, Height, BitCount, sdlVideoFlags);
		//fprintf(stderr,"Got video mode %i %i %i\n", sdlscrn->w, sdlscrn->h, sdlscrn->format->BitsPerPixel);
		/* We do not support 24 bpp (yet) */
		if (sdlscrn && sdlscrn->format->BitsPerPixel == 24)
		{
			fprintf(stderr, "Unsupported color depth 24, trying 32 bpp instead...\n");
			sdlscrn = SDL_SetVideoMode(Width, Height, 32, sdlVideoFlags);
		}
		/* Exit if we can not open a screen */
		if (!sdlscrn)
		{
			fprintf(stderr, "Could not set video mode:\n %s\n", SDL_GetError() );
			SDL_Quit();
			exit(-2);
		}

		/* Re-init screen palette: */
		if (sdlscrn->format->BitsPerPixel == 8)
			Screen_Handle8BitPalettes();    /* Initialize new 8 bit palette */
		else
			Screen_SetupRGBTable();         /* Create color convertion table */

		/* Un-grab mouse pointer in windowed mode: */
		if (!bGrabMouse)
			SDL_WM_GrabInput(SDL_GRAB_OFF);
	}

	/* Set drawing functions */
	Screen_SetDrawFunctions(sdlscrn->format->BitsPerPixel);

	Screen_SetFullUpdate();           /* Cause full update of screen */
}


/*-----------------------------------------------------------------------*/
/**
 * Init Screen bitmap and buffers/tables needed for ST to PC screen conversion
 */
void Screen_Init(void)
{
	int i;
	SDL_Surface *pIconSurf;
	char sIconFileName[FILENAME_MAX];

	/* Clear frame buffer structures and set current pointer */
	memset(FrameBuffers, 0, NUM_FRAMEBUFFERS * sizeof(FRAMEBUFFER));

	/* Allocate previous screen check workspace. We are going to double-buffer a double-buffered screen. Oh. */
	for (i = 0; i < NUM_FRAMEBUFFERS; i++)
	{
		FrameBuffers[i].pSTScreen = malloc(((MAX_VDI_WIDTH*MAX_VDI_PLANES)/8)*MAX_VDI_HEIGHT);
		FrameBuffers[i].pSTScreenCopy = malloc(((MAX_VDI_WIDTH*MAX_VDI_PLANES)/8)*MAX_VDI_HEIGHT);
		if (!FrameBuffers[i].pSTScreen || !FrameBuffers[i].pSTScreenCopy)
		{
			fprintf(stderr, "Failed to allocate frame buffer memory.\n");
			exit(-1);
		}
	}
	pFrameBuffer = &FrameBuffers[0];

	/* Load and set icon */
	snprintf(sIconFileName, sizeof(sIconFileName), "%s%chatari-icon.bmp",
	         Paths_GetDataDir(), PATHSEP);
	pIconSurf = SDL_LoadBMP(sIconFileName);
	if (pIconSurf)
	{
		SDL_SetColorKey(pIconSurf, SDL_SRCCOLORKEY, SDL_MapRGB(pIconSurf->format, 255, 255, 255));
		SDL_WM_SetIcon(pIconSurf, NULL);
		SDL_FreeSurface(pIconSurf);
	}

	/* Set initial window resolution */
	bInFullScreen = ConfigureParams.Screen.bFullScreen;
	Screen_SetResolution();

	Video_SetScreenRasters();                       /* Set rasters ready for first screen */

	/* Configure some SDL stuff: */
	SDL_WM_SetCaption(PROG_NAME, "Hatari");
	SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
	SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_ENABLE);
	SDL_EventState(SDL_MOUSEBUTTONUP, SDL_ENABLE);
	SDL_ShowCursor(SDL_DISABLE);
}


/*-----------------------------------------------------------------------*/
/**
 * Free screen bitmap and allocated resources
 */
void Screen_UnInit(void)
{
	int i;

	/* Free memory used for copies */
	for (i = 0; i < NUM_FRAMEBUFFERS; i++)
	{
		free(FrameBuffers[i].pSTScreen);
		free(FrameBuffers[i].pSTScreenCopy);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Reset screen
 */
void Screen_Reset(void)
{
	/* On re-boot, always correct ST resolution for monitor, eg Colour/Mono */
	if (bUseVDIRes)
	{
		STRes = VDIRes;
	}
	else
	{
		if (bUseHighRes)
		{
			STRes = ST_HIGH_RES;
			TTRes = TT_HIGH_RES;
		}
		else
		{
			STRes = ST_LOW_RES;
			TTRes = TT_MEDIUM_RES;
		}
	}
	/* Cause full update */
	Screen_ModeChanged();
}


/*-----------------------------------------------------------------------*/
/**
 * Set flags so screen will be TOTALLY re-drawn (clears whole of full-screen)
 * next time around
 */
void Screen_SetFullUpdate(void)
{
	int i;

	/* Update frame buffers */
	for (i = 0; i < NUM_FRAMEBUFFERS; i++)
		FrameBuffers[i].bFullUpdate = TRUE;
}


/*-----------------------------------------------------------------------*/
/**
 * Clear Window display memory
 */
static void Screen_ClearScreen(void)
{
	SDL_FillRect(sdlscrn,NULL, SDL_MapRGB(sdlscrn->format, 0, 0, 0) );
}

/*-----------------------------------------------------------------------*/
/**
 * Enter Full screen mode
 */
void Screen_EnterFullScreen(void)
{
	if (!bInFullScreen)
	{
		Main_PauseEmulation();         /* Hold things... */
		bInFullScreen = TRUE;
		Main_ReparentWindow(bInFullScreen);

		if ((ConfigureParams.System.nMachineType == MACHINE_FALCON
		     || ConfigureParams.System.nMachineType == MACHINE_TT) && !bUseVDIRes)
		{
			HostScreen_toggleFullScreen();
		}
		else
		{
			Screen_SetResolution();
			Screen_ClearScreen();       /* Black out screen bitmap as will be invalid when return */
		}

		SDL_Delay(20);                  /* To give monitor time to change to new resolution */
		Main_UnPauseEmulation();        /* And off we go... */

		SDL_WM_GrabInput(SDL_GRAB_ON);  /* Grab mouse pointer in fullscreen */
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Return from Full screen mode back to a window
 */
void Screen_ReturnFromFullScreen(void)
{
	if (bInFullScreen)
	{
		Main_PauseEmulation();        /* Hold things... */
		bInFullScreen = FALSE;

		if ((ConfigureParams.System.nMachineType == MACHINE_FALCON
		     || ConfigureParams.System.nMachineType == MACHINE_TT) && !bUseVDIRes)
		{
			HostScreen_toggleFullScreen();
			if (!bGrabMouse)
				SDL_WM_GrabInput(SDL_GRAB_OFF); /* Un-grab mouse pointer in windowed mode */
		}
		else
		{
			Screen_SetResolution();
		}
		Main_ReparentWindow(bInFullScreen);
		SDL_Delay(20);                /* To give monitor time to switch resolution */
		Main_UnPauseEmulation();      /* And off we go... */
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Have we changed between low/med/high res?
 */
static void Screen_DidResolutionChange(int new_res)
{
	if (new_res != STRes)
	{
		STRes = new_res;
		Screen_ModeChanged();
	}
	else
	{
		/* Did change overscan mode? Causes full update */
		if (pFrameBuffer->OverscanModeCopy != OverscanMode)
			pFrameBuffer->bFullUpdate = TRUE;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Force things associated with changing between low/medium/high res.
 */
void Screen_ModeChanged(void)
{
	if (!sdlscrn)
	{
		/* screen not yet initialized */
		return;
	}
	/* Don't run this function if Videl emulation is running! */
	if (ConfigureParams.System.nMachineType == MACHINE_FALCON && !bUseVDIRes)
	{
		VIDEL_ZoomModeChanged();
		return;
	}
	else if (ConfigureParams.System.nMachineType == MACHINE_TT && !bUseVDIRes)
	{
		int width, height, bpp;
		Video_GetTTRes(&width, &height, &bpp);
		HostScreen_setWindowSize(width, height, 8);
		return;
	}
	/* Set new display mode, if differs from current */
	Screen_SetResolution();
	Screen_SetFullUpdate();
}


/*-----------------------------------------------------------------------*/
/**
 * Compare current resolution on line with previous, and set 'UpdateLine' accordingly
 * Return if swap between low/medium resolution
 */
static bool Screen_CompareResolution(int y, int *pUpdateLine, int oldres)
{
	/* Check if wrote to resolution register */
	if (HBLPaletteMasks[y]&PALETTEMASK_RESOLUTION)  /* See 'Intercept_ShifterMode_WriteByte' */
	{
		int newres = (HBLPaletteMasks[y]>>16)&ST_MEDIUM_RES_BIT;
		/* Did resolution change? */
		if (newres != (int)((pFrameBuffer->HBLPaletteMasks[y]>>16)&ST_MEDIUM_RES_BIT))
			*pUpdateLine |= PALETTEMASK_UPDATERES;
		else
			*pUpdateLine &= ~PALETTEMASK_UPDATERES;
		/* Have used any low/medium res mix? */
		return (newres != (oldres&ST_MEDIUM_RES_BIT));
	}
	return FALSE;
}


/*-----------------------------------------------------------------------*/
/**
 * Check to see if palette changes cause screen update and keep 'HBLPalette[]' up-to-date
 */
static void Screen_ComparePalette(int y, int *pUpdateLine)
{
	bool bPaletteChanged = FALSE;
	int i;

	/* Did write to palette in this or previous frame? */
	if (((HBLPaletteMasks[y]|pFrameBuffer->HBLPaletteMasks[y])&PALETTEMASK_PALETTE)!=0)
	{
		/* Check and update ones which changed */
		for (i = 0; i < 16; i++)
		{
			if (HBLPaletteMasks[y]&(1<<i))        /* Update changes in ST palette */
				HBLPalette[i] = HBLPalettes[(y*16)+i];
		}
		/* Now check with same palette from previous frame for any differences(may be changing palette back) */
		for (i = 0; (i < 16) && (!bPaletteChanged); i++)
		{
			if (HBLPalette[i]!=pFrameBuffer->HBLPalettes[(y*16)+i])
				bPaletteChanged = TRUE;
		}
		if (bPaletteChanged)
			*pUpdateLine |= PALETTEMASK_UPDATEPAL;
		else
			*pUpdateLine &= ~PALETTEMASK_UPDATEPAL;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Check for differences in Palette and Resolution from Mask table and update
 * and store off which lines need updating and create full-screen palette.
 * (It is very important for these routines to check for colour changes with
 * the previous screen so only the very minimum parts are updated).
 * Return new STRes value.
 */
static int Screen_ComparePaletteMask(int res)
{
	bool bLowMedMix = FALSE;
	int LineUpdate = 0;
	int y;

	/* Set for monochrome? */
	if (bUseHighRes)
	{
		OverscanMode = OVERSCANMODE_NONE;

		/* Just copy mono colours, 0x777 checked also in convert/vdi2.c */
		if (HBLPalettes[0] & 0x777)
		{
			HBLPalettes[0] = 0x777;
			HBLPalettes[1] = 0x000;
		}
		else
		{
			HBLPalettes[0] = 0x000;
			HBLPalettes[1] = 0x777;
		}

		/* Colors changed? */
		if (HBLPalettes[0] != PrevHBLPalette[0])
			pFrameBuffer->bFullUpdate = TRUE;

		/* Set bit to flag 'full update' */
		if (pFrameBuffer->bFullUpdate)
			ScrUpdateFlag = PALETTEMASK_UPDATEFULL;
		else
			ScrUpdateFlag = 0x00000000;
	}

	/* Use VDI resolution? */
	if (bUseVDIRes)
	{
		/* Force to VDI resolution screen, without overscan */
		res = VDIRes;

		/* Colors changed? */
		if (HBLPalettes[0] != PrevHBLPalette[0])
			pFrameBuffer->bFullUpdate = TRUE;

		/* Set bit to flag 'full update' */
		if (pFrameBuffer->bFullUpdate)
			ScrUpdateFlag = PALETTEMASK_UPDATEFULL;
		else
			ScrUpdateFlag = 0x00000000;
	}
	/* Are in Mono? Force to monochrome and no overscan */
	else if (bUseHighRes)
	{
		/* Force to standard hi-resolution screen, without overscan */
		res = ST_HIGH_RES;
	}
	else    /* Full colour */
	{
		/* Get resolution */
		//res = (HBLPaletteMasks[0]>>16)&ST_RES_MASK;
		/* [NP] keep only low/med bit (could be hires in case of overscan on the 1st line) */
		res = (HBLPaletteMasks[0]>>16)&ST_MEDIUM_RES_BIT;

		/* Do all lines - first is tagged as full-update */
		for (y = 0; y < NUM_VISIBLE_LINES; y++)
		{
			/* Find any resolution/palette change and update palette/mask buffer */
			/* ( LineUpdate has top two bits set to say if line needs updating due to palette or resolution change ) */
			bLowMedMix |= Screen_CompareResolution(y, &LineUpdate, res);
			Screen_ComparePalette(y,&LineUpdate);
			HBLPaletteMasks[y] = (HBLPaletteMasks[y]&(~PALETTEMASK_UPDATEMASK)) | LineUpdate;
			/* Copy palette and mask for next frame */
			memcpy(&pFrameBuffer->HBLPalettes[y*16],HBLPalette,sizeof(short int)*16);
			pFrameBuffer->HBLPaletteMasks[y] = HBLPaletteMasks[y];
		}
		/* Did mix/have medium resolution? */
		if (bLowMedMix || (res & ST_MEDIUM_RES_BIT))
			res = ST_MEDIUM_RES;
	}

	return res;
}


/*-----------------------------------------------------------------------*/
/**
 * Update Palette Mask to show 'full-update' required. This is usually done after a resolution change
 * or when going between a Window and full-screen display
 */
static void Screen_SetFullUpdateMask(void)
{
	int y;

	for (y = 0; y < NUM_VISIBLE_LINES; y++)
		HBLPaletteMasks[y] |= PALETTEMASK_UPDATEFULL;
}


/*-----------------------------------------------------------------------*/
/**
 * Set details for ST screen conversion.
 */
static void Screen_SetConvertDetails(void)
{
	pSTScreen = pFrameBuffer->pSTScreen;          /* Source in ST memory */
	pSTScreenCopy = pFrameBuffer->pSTScreenCopy;  /* Previous ST screen */
	pPCScreenDest = sdlscrn->pixels;              /* Destination PC screen */

	PCScreenBytesPerLine = sdlscrn->pitch;        /* Bytes per line */
	pHBLPalettes = pFrameBuffer->HBLPalettes;     /* HBL palettes pointer */
	/* Not in TV-Mode? Then double up on Y: */
	bScrDoubleY = !(ConfigureParams.Screen.nMonitorType == MONITOR_TYPE_TV);

	if (bUseVDIRes)
	{
		/* Select screen draw for standard or VDI display */
		STScreenLeftSkipBytes = 0;
		STScreenWidthBytes = VDIWidth * VDIPlanes / 8;
		STScreenStartHorizLine = 0;
		STScreenEndHorizLine = VDIHeight;
	}
	else
	{
		if (ConfigureParams.Screen.bAllowOverscan)  /* Use borders? */
		{
			/* Always draw to WHOLE screen including ALL borders */
			STScreenLeftSkipBytes = 0;              /* Number of bytes to skip on ST screen for left (border) */
			STScreenStartHorizLine = 0;             /* Full height */

			if (bUseHighRes)
			{
				pFrameBuffer->OverscanModeCopy = OverscanMode = OVERSCANMODE_NONE;
				STScreenEndHorizLine = 400;
			}
			else
			{
				STScreenWidthBytes = SCREENBYTES_LINE;  /* Number of horizontal bytes in our ST screen */
				STScreenEndHorizLine = NUM_VISIBLE_LINES;
			}
		}
		else
		{
			/* Only draw main area and centre on Y */
			STScreenLeftSkipBytes = SCREENBYTES_LEFT;
			STScreenWidthBytes = SCREENBYTES_MIDDLE;
			STScreenStartHorizLine = OVERSCAN_TOP;
			STScreenEndHorizLine = OVERSCAN_TOP + (bUseHighRes ? 400 : 200);
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Lock full-screen for drawing
 */
static bool Screen_Lock(void)
{
	if (SDL_MUSTLOCK(sdlscrn))
	{
		if (SDL_LockSurface(sdlscrn))
		{
			Screen_ReturnFromFullScreen();   /* All OK? If not need to jump back to a window */
			return(FALSE);
		}
	}

	return TRUE;
}

/*-----------------------------------------------------------------------*/
/**
 * UnLock full-screen
 */
static void Screen_UnLock(void)
{
	if ( SDL_MUSTLOCK(sdlscrn) )
		SDL_UnlockSurface(sdlscrn);
}


/*-----------------------------------------------------------------------*/
/**
 * Swap ST Buffers, used for full-screen where have double-buffering
 */
static void Screen_SwapSTBuffers(void)
{
#if NUM_FRAMEBUFFERS > 1
	if (sdlscrn->flags & SDL_DOUBLEBUF)
	{
		if (pFrameBuffer==&FrameBuffers[0])
			pFrameBuffer = &FrameBuffers[1];
		else
			pFrameBuffer = &FrameBuffers[0];
	}
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Blit our converted ST screen to window/full-screen
 * Note that our source image includes all borders so if have them disabled simply blit a smaller source rectangle!
 */
static void Screen_Blit(bool bSwapScreen)
{
	unsigned char *pTmpScreen;

	/* Blit to full screen or window? */
	if (bInFullScreen)
	{
		Screen_SwapSTBuffers();
		/* Swap screen */
		if (bSwapScreen)
			SDL_Flip(sdlscrn);
	}
	else
	{
		/* Blit image */
		SDL_UpdateRect(sdlscrn, 0,0,0,0);
	}

	/* Swap copy/raster buffers in screen. */
	pTmpScreen = pFrameBuffer->pSTScreenCopy;
	pFrameBuffer->pSTScreenCopy = pFrameBuffer->pSTScreen;
	pFrameBuffer->pSTScreen = pTmpScreen;
}


/*-----------------------------------------------------------------------*/
/**
 * Draw ST screen to window/full-screen framebuffer
 */
static void Screen_DrawFrame(bool bForceFlip)
{
	int new_res;
	void (*pDrawFunction)(void);
	static bool bPrevFrameWasSpec512 = FALSE;

	/* Scan palette/resolution masks for each line and build up palette/difference tables */
	new_res = Screen_ComparePaletteMask(STRes);
	/* Do require palette? Check if changed and update */
	Screen_Handle8BitPalettes();
	/* Did we change resolution this frame - allocate new screen if did so */
	Screen_DidResolutionChange(new_res);
	/* Is need full-update, tag as such */
	if (pFrameBuffer->bFullUpdate)
		Screen_SetFullUpdateMask();

	/* Lock screen ready for drawing */
	if (Screen_Lock())
	{
		bScreenContentsChanged = FALSE;      /* Did change (ie needs blit?) */
		/* Set details */
		Screen_SetConvertDetails();
		/* Clear screen on full update to clear out borders and also interleaved lines */
		if (pFrameBuffer->bFullUpdate && !bUseVDIRes)
			Screen_ClearScreen();
		/* Call drawing for full-screen */
		if (bUseVDIRes)
		{
			pDrawFunction = ScreenDrawFunctionsVDI[VDIRes];
		}
		else
		{
			pDrawFunction = ScreenDrawFunctionsNormal[STRes];
			/* Check if is Spec512 image */
			if (Spec512_IsImage())
			{
				bPrevFrameWasSpec512 = TRUE;
				/* What mode were we in? Keep to 320xH or 640xH */
				if (pDrawFunction==ConvertLowRes_320x16Bit)
					pDrawFunction = ConvertSpec512_320x16Bit;
				else if (pDrawFunction==ConvertLowRes_640x16Bit)
					pDrawFunction = ConvertSpec512_640x16Bit;
				else if (pDrawFunction==ConvertLowRes_320x32Bit)
					pDrawFunction = ConvertSpec512_320x32Bit;
				else if (pDrawFunction==ConvertLowRes_640x32Bit)
					pDrawFunction = ConvertSpec512_640x32Bit;
			}
			else if (bPrevFrameWasSpec512)
			{
				/* If we switch back from Spec512 mode to normal
				 * screen rendering, we have to make sure to do
				 * a full update of the screen. */
				Screen_SetFullUpdateMask();
				bPrevFrameWasSpec512 = FALSE;
			}
		}

		if (pDrawFunction)
			CALL_VAR(pDrawFunction)

			/* Unlock screen */
			Screen_UnLock();
		/* Clear flags, remember type of overscan as if change need screen full update */
		pFrameBuffer->bFullUpdate = FALSE;
		pFrameBuffer->OverscanModeCopy = OverscanMode;

		/* And show to user */
		if (bScreenContentsChanged || bForceFlip)
			Screen_Blit(TRUE);

		/* Grab any animation */
		if (bRecordingAnimation)
			ScreenSnapShot_RecordFrame(bScreenContentsChanged);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Draw ST screen to window/full-screen
 */
void Screen_Draw(void)
{
	if (!bQuitProgram && VideoBase)
	{
		/* And draw (if screen contents changed) */
		Screen_DrawFrame(FALSE);
	}
}


/* -------------- screen conversion routines --------------------------------
  Screen conversion routines. We have a number of routines to convert ST screen
  to PC format. We split these into Low, Medium and High each with 8/16-bit
  versions. To gain extra speed, as almost half of the processing time can be
  spent in these routines, we check for any changes from the previously
  displayed frame. AdjustLinePaletteRemap() sets a flag to tell the routines
  if we need to totally update a line (ie full update, or palette/res change)
  or if we just can do a difference check.
  We convert each screen 16 pixels at a time by use of a couple of look-up
  tables. These tables convert from 2-plane format to bbp and then we can add
  two of these together to get 4-planes. This keeps the tables small and thus
  improves speed. We then look these bbp values up as an RGB/Index value to
  copy to the screen.
*/


/*-----------------------------------------------------------------------*/
/**
 * Update the STRGBPalette[] array with current colours for this raster line.
 *
 * Return 'ScrUpdateFlag', 0x80000000=Full update, 0x40000000=Update
 * as palette changed
 */
static int AdjustLinePaletteRemap(int y)
{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	static const int endiantable[16] = {0,2,1,3,8,10,9,11,4,6,5,7,12,14,13,15};
#endif
	Uint16 *actHBLPal;
	int i;

	/* Copy palette and convert to RGB in display format */
	actHBLPal = pHBLPalettes + (y<<4);    /* offset in palette */
	for (i=0; i<16; i++)
	{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		STRGBPalette[endiantable[i]] = ST2RGB[*actHBLPal++];
#else
		STRGBPalette[i] = ST2RGB[*actHBLPal++];
#endif
	}
	ScrUpdateFlag = HBLPaletteMasks[y];
	return ScrUpdateFlag;
}


/*-----------------------------------------------------------------------*/
/**
 * Run updates to palette(STRGBPalette[]) until get to screen line
 * we are to convert from
 */
static void Convert_StartFrame(void)
{
	int y = 0;
	/* Get #lines before conversion starts */
	int lines = STScreenStartHorizLine;
	while (lines--)
		AdjustLinePaletteRemap(y++);     /* Update palette */
}

/* lookup tables and conversion macros */
#include "convert/macros.h"

/* Conversion routines */

#include "convert/low320x8.c"     /* LowRes To 320xH x 8-bit color */
#include "convert/low640x8.c"     /* LowRes To 640xH x 8-bit color */
#include "convert/med640x8.c"     /* MediumRes To 640xH x 8-bit color */
#include "convert/high640x8.c"    /* HighRes To 640xH x 8-bit color */

#include "convert/low320x16.c"    /* LowRes To 320xH x 16-bit color */
#include "convert/low640x16.c"    /* LowRes To 640xH x 16-bit color */
#include "convert/med640x16.c"    /* MediumRes To 640xH x 16-bit color */
#include "convert/spec320x16.c"   /* Spectrum 512 To 320xH x 16-bit color */
#include "convert/spec640x16.c"   /* Spectrum 512 To 640xH x 16-bit color */

#include "convert/low320x32.c"    /* LowRes To 320xH x 32-bit color */
#include "convert/low640x32.c"    /* LowRes To 640xH x 32-bit color */
#include "convert/med640x32.c"    /* MediumRes To 640xH x 32-bit color */
#include "convert/spec320x32.c"   /* Spectrum 512 To 320xH x 32-bit color */
#include "convert/spec640x32.c"   /* Spectrum 512 To 640xH x 32-bit color */

#include "convert/vdi16.c"        /* VDI x 16 color */
#include "convert/vdi4.c"         /* VDI x 4 color */
#include "convert/vdi2.c"         /* VDI x 2 color */
