/*
  Hatari - conv_st.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This code converts a low/medium resolution ST format screen to 32-bit PC
  format. An awful lot of processing is needed to do this conversion - we
  cannot simply change palettes on  interrupts as it is possible with old
  systems from the 1980s / early 1990s.
  The main code processes the palette/resolution mask tables to find exactly
  which lines need to updating and the conversion routines themselves only
  update 16-pixel blocks which differ from the previous frame - this gives a
  large performance increase.
  Each conversion routine can convert any part of the source ST screen (which
  includes the overscan border, usually set to colour zero) so they can be used
  for both window and full-screen mode.
  Note that in Hi-Resolution we have no overscan and just two colors so we can
  optimise things further.
  In color mode it seems possible to display 47 lines in the bottom border
  with a second 60/50 Hz switch, but most programs consider there are 45
  visible lines in the bottom border only, which gives a total of 274 lines
  for a screen. So not displaying the last two lines fixes garbage that could
  appear in the last two lines when displaying 47 lines (Digiworld 2 by ICE,
  Tyranny by DHS).
*/

#include <assert.h>

#include "main.h"
#include "configuration.h"
#include "conv_gen.h"
#include "conv_st.h"
#include "avi_record.h"
#include "file.h"
#include "log.h"
#include "paths.h"
#include "options.h"
#include "screen.h"
#include "control.h"
#include "convert/routines.h"
#include "spec512.h"
#include "statusbar.h"
#include "vdi.h"
#include "video.h"
#include "falcon/videl.h"

#define DEBUG 0

#if DEBUG
# define DEBUGPRINT(x) printf x
#else
# define DEBUGPRINT(x)
#endif

/* extern for several purposes */
int nScreenZoomX, nScreenZoomY;             /* Zooming factors, used for scaling mouse motions */
int nBorderPixelsLeft, nBorderPixelsRight;  /* Pixels in left and right border */
static int nBorderPixelsTop, nBorderPixelsBottom;  /* Lines in top and bottom border */

/* extern for spec512.c */
int STScreenLeftSkipBytes;
int STScreenStartHorizLine;   /* Start lines to be converted */
uint32_t STRGBPalette[16];    /* Palette buffer used in conversion routines */
uint32_t ST2RGB[4096];        /* Table to convert ST 0x777 / STe 0xfff palette to PC format RGB551 (2 pixels each entry) */

/* extern for video.c */
uint8_t *pSTScreen;
FRAMEBUFFER *pFrameBuffer;    /* Pointer into current 'FrameBuffer' */

/* extern for screen snapshot palettes */
uint32_t *ConvertPalette = STRGBPalette;
int ConvertPaletteSize = 0;

uint16_t HBLPalettes[HBL_PALETTE_LINES];          /* 1x16 colour palette per screen line, +1 line just in case write after line 200 */
uint16_t *pHBLPalettes;                           /* Pointer to current palette lists, one per HBL */
uint32_t HBLPaletteMasks[HBL_PALETTE_MASKS];      /* Bit mask of palette colours changes, top bit set is resolution change */
uint32_t *pHBLPaletteMasks;


static FRAMEBUFFER FrameBuffer;     /* Store frame buffer details to tell how to update */
static uint8_t *pSTScreenCopy;      /* Keep track of current and previous ST screen data */
static uint32_t *pPCScreenDest;     /* Destination PC buffer */
static int STScreenEndHorizLine;    /* End lines to be converted */
static int PCScreenBytesPerLine;
static int STScreenWidthBytes;
static int PCScreenOffsetX;         /* how many pixels to skip from left when drawing */
static int PCScreenOffsetY;         /* how many pixels to skip from top when drawing */

int STScreenLineOffset[NUM_VISIBLE_LINES];          /* Offsets for ST screen lines eg, 0,160,320... */
static uint16_t HBLPalette[16], PrevHBLPalette[16]; /* Current palette for line, also copy of first line */

static void (*ScreenDrawFunctionsNormal[3])(void);  /* Screen draw functions */

static bool bScreenContentsChanged;     /* true if buffer changed and requires blitting */
static bool bScrDoubleY;                /* true if double on Y */
static int ScrUpdateFlag;               /* Bit mask of how to update screen */


/**
 * Create ST 0x777 / STe 0xfff color format to 16 or 32 bits per pixel
 * conversion table. Called each time when changed resolution or to/from
 * fullscreen mode.
 */
static void ConvST_SetupRGBTable(void)
{
	uint16_t STColor;
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
				rr = ((r & 0x7) << 1) | ((r & 0x8) >> 3);
				rr |= rr << 4;
				gg = ((g & 0x7) << 1) | ((g & 0x8) >> 3);
				gg |= gg << 4;
				bb = ((b & 0x7) << 1) | ((b & 0x8) >> 3);
				bb |= bb << 4;
				ST2RGB[STColor] = Screen_MapRGB(rr, gg, bb);
			}
		}
	}
}


/**
 * Convert 640x400 monochrome screen
 */
static void ConvST_ConvertHighRes(void)
{
	int linewidth = 640 / 16;

	ConvGen_Convert(VideoBase, pSTScreen, 640, 400, 1, linewidth, 0, 0, 0, 0, 0);
	bScreenContentsChanged = true;
}

/**
 * Set screen draw functions.
 */
static void ConvST_SetDrawFunctions(bool bDoubleLowRes)
{
	if (bDoubleLowRes)
		ScreenDrawFunctionsNormal[ST_LOW_RES] = ConvertLowRes_640x32Bit;
	else
		ScreenDrawFunctionsNormal[ST_LOW_RES] = ConvertLowRes_320x32Bit;
	ScreenDrawFunctionsNormal[ST_MEDIUM_RES] = ConvertMediumRes_640x32Bit;
}


/**
 * Set amount of border pixels
 */
static void ConvST_SetBorderPixels(int leftX, int leftY)
{
	/* All screen widths need to be aligned to 16-bits */
	nBorderPixelsLeft = Opt_ValueAlignMinMax(leftX/2, 16, 0, 48);
	nBorderPixelsRight = nBorderPixelsLeft;

	/* assertain assumption of code below */
	assert(OVERSCAN_TOP < MAX_OVERSCAN_BOTTOM);
	
	if (leftY > 2*OVERSCAN_TOP)
	{
		nBorderPixelsTop = OVERSCAN_TOP;
		if (leftY >= OVERSCAN_TOP + MAX_OVERSCAN_BOTTOM)
			nBorderPixelsBottom = MAX_OVERSCAN_BOTTOM;
		else
			nBorderPixelsBottom = leftY - OVERSCAN_TOP;
	}
	else
	{
		if (leftY > 0)
			nBorderPixelsTop = nBorderPixelsBottom = leftY/2;
		else
			nBorderPixelsTop = nBorderPixelsBottom = 0;
	}
}


/**
 * store Y offset for each horizontal line in our source ST screen for
 * reference in the convert functions.
 */
static void ConvST_SetSTScreenOffsets(void)
{
	int i;

	/* Store offset to each horizontal line, uses
	 * nBorderPixels* variables.
	 */
	for (i = 0; i < NUM_VISIBLE_LINES; i++)
	{
		STScreenLineOffset[i] = i * SCREENBYTES_LINE;
	}
}


/**
 * Initialize ST/STE screen resolution.
 */
void ConvST_SetSTResolution(bool bForceChange)
{
	int Width, Height, nZoom, SBarHeight, maxW, maxH;
	bool bDoubleLowRes = false;

	nBorderPixelsTop = nBorderPixelsBottom = 0;
	nBorderPixelsLeft = nBorderPixelsRight = 0;

	nScreenZoomX = 1;
	nScreenZoomY = 1;

	if (STRes == ST_LOW_RES)
	{
		Width = 320;
		Height = 200;
		nZoom = 1;
	}
	else    /* else use 640x400, also for med-rez */
	{
		Width = 640;
		Height = 400;
		nZoom = 2;
	}

	/* Statusbar height for doubled screen size */
	SBarHeight = Statusbar_GetHeightForSize(640, 400);

	ConvGen_GetLimits(&maxW, &maxH);

	/* Zoom if necessary, factors used for scaling mouse motions */
	if (STRes == ST_LOW_RES &&
	    2*Width <= maxW && 2*Height+SBarHeight <= maxH)
	{
		nZoom = 2;
		Width *= 2;
		Height *= 2;
		nScreenZoomX = 2;
		nScreenZoomY = 2;
		bDoubleLowRes = true;
	}
	else if (STRes == ST_MEDIUM_RES)
	{
		/* med-rez conversion functions want always
		 * to double vertically, they don't support
		 * skipping that (only leaving doubled lines
		 * black for the TV mode).
		 */
		nScreenZoomX = 1;
		nScreenZoomY = 2;
	}

	/* Adjust width/height for overscan borders, if mono or VDI we have no overscan */
	if (ConfigureParams.Screen.bAllowOverscan && !bUseHighRes)
	{
		int leftX = maxW - Width;
		int leftY = maxH - (Height + Statusbar_GetHeightForSize(Width, Height));

		ConvST_SetBorderPixels(leftX/nZoom, leftY/nZoom);
		DEBUGPRINT(("resolution limit:\n\t%d x %d\nlimited resolution:\n\t", maxW, maxH));
		DEBUGPRINT(("%d * (%d + %d + %d) x (%d + %d + %d)\n", nZoom,
			    nBorderPixelsLeft, Width/nZoom, nBorderPixelsRight,
			    nBorderPixelsTop, Height/nZoom, nBorderPixelsBottom));
		Width += (nBorderPixelsRight + nBorderPixelsLeft)*nZoom;
		Height += (nBorderPixelsTop + nBorderPixelsBottom)*nZoom;
		DEBUGPRINT(("\t= %d x %d (+ statusbar)\n", Width, Height));
	}

	ConvST_SetSTScreenOffsets();
	Height += Statusbar_SetHeight(Width, Height);

	PCScreenOffsetX = PCScreenOffsetY = 0;

	if (Screen_SetVideoSize(Width, Height, bForceChange))
	{
		ConvST_SetupRGBTable();   /* Create color conversion table */
	}

	/* Set drawing functions */
	ConvST_SetDrawFunctions(bDoubleLowRes);

	ConvST_SetFullUpdate();           /* Cause full update of screen */
}


/**
 * Change resolution, according to the machine and display type
 * that we're currently emulating.
 */
void ConvST_ChangeResolution(bool bForceChange)
{
	if (bUseVDIRes)
	{
		ConvGen_SetSize(VDIWidth, VDIHeight, bForceChange);
	}
	else if (Config_IsMachineFalcon())
	{
		Videl_ScreenModeChanged(bForceChange);
	}
	else if (Config_IsMachineTT())
	{
		int width, height, bpp;
		Video_GetTTRes(&width, &height, &bpp);
		ConvGen_SetSize(width, height, bForceChange);
	}
	else if (bUseHighRes)
	{
		ConvGen_SetSize(640, 400, bForceChange);
	}
	else
	{
		ConvST_SetSTResolution(bForceChange);
	}

	Screen_GrabMouseIfNecessary();
}


/**
 * Init buffers/tables needed for ST to PC screen conversion
 */
void ConvST_Init(void)
{
	/* Clear frame buffer structures and set current pointer */
	memset(&FrameBuffer, 0, sizeof(FRAMEBUFFER));

	/* Allocate screen check workspace. */
	assert(MAX_VDI8_BYTES >= MAX_VDI_BYTES);
	FrameBuffer.pSTScreen = malloc(MAX_VDI8_BYTES);
	FrameBuffer.pSTScreenCopy = malloc(MAX_VDI8_BYTES);
	if (!FrameBuffer.pSTScreen || !FrameBuffer.pSTScreenCopy)
	{
		Main_ErrorExit("Failed to allocate frame buffer memory", NULL, -1);
	}
	pFrameBuffer = &FrameBuffer;  /* TODO: Replace pFrameBuffer with FrameBuffer everywhere */

	ScreenDrawFunctionsNormal[ST_HIGH_RES] = ConvST_ConvertHighRes;

	Video_SetScreenRasters();                       /* Set rasters ready for first screen */
}


/**
 * Free allocated screen convertion resources
 */
void ConvST_UnInit(void)
{
	/* Free memory used for copies */
	free(FrameBuffer.pSTScreen);
	free(FrameBuffer.pSTScreenCopy);
	FrameBuffer.pSTScreen = NULL;
	FrameBuffer.pSTScreenCopy = NULL;
}


/**
 * Reset screen
 */
void ConvST_Reset(void)
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
	Screen_ModeChanged(false);
}


/**
 * Set flags so screen will be TOTALLY re-drawn (clears whole of full-screen)
 * next time around
 */
void ConvST_SetFullUpdate(void)
{
	/* Update frame buffers */
	FrameBuffer.bFullUpdate = true;
}


/**
 * Force screen redraw.  Does the right thing regardless of whether
 * we're in ST/STe, Falcon or TT mode.  Needed when switching modes
 * while emulation is paused.
 */
void ConvST_Refresh(bool force_flip)
{
	if (bUseVDIRes)
	{
		ConvGen_Draw(VideoBase, VDIWidth, VDIHeight, VDIPlanes,
		             VDIWidth * VDIPlanes / 16, 0, 0, 0, 0);
	}
	else if (Config_IsMachineFalcon())
	{
		VIDEL_renderScreen();
	}
	else if (Config_IsMachineTT())
	{
		Video_RenderTTScreen();
	}
	else
	{
		Screen_Draw(force_flip);
	}
}


/**
 * Have we changed between low/med/high res?
 */
static void ConvST_DidResolutionChange(int new_res)
{
	if (new_res != STRes)
	{
		STRes = new_res;
		Screen_ModeChanged(false);
	}
	else
	{
		/* Did change overscan mode? Causes full update */
		if (pFrameBuffer->VerticalOverscanCopy != VerticalOverscan)
			pFrameBuffer->bFullUpdate = true;
	}
}


/**
 * Compare current resolution on line with previous, and set 'UpdateLine' accordingly
 * Return if swap between low/medium resolution
 */
static bool ConvST_CompareResolution(int y, int *pUpdateLine, int oldres)
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
	return false;
}


/**
 * Check to see if palette changes cause screen update and keep 'HBLPalette[]' up-to-date
 */
static void ConvST_ComparePalette(int y, int *pUpdateLine)
{
	bool bPaletteChanged = false;
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
				bPaletteChanged = true;
		}
		if (bPaletteChanged)
			*pUpdateLine |= PALETTEMASK_UPDATEPAL;
		else
			*pUpdateLine &= ~PALETTEMASK_UPDATEPAL;
	}
}


/**
 * Check for differences in Palette and Resolution from Mask table and update
 * and store off which lines need updating and create full-screen palette.
 * (It is very important for these routines to check for colour changes with
 * the previous screen so only the very minimum parts are updated).
 * Return new STRes value.
 */
static int ConvST_ComparePaletteMask(int res)
{
	bool bLowMedMix = false;
	int LineUpdate = 0;
	int y;

	/* Set for monochrome? */
	if (bUseHighRes)
	{
		VerticalOverscan = V_OVERSCAN_NONE;

		/* Just copy mono colors */
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
			pFrameBuffer->bFullUpdate = true;

		/* Set bit to flag 'full update' */
		if (pFrameBuffer->bFullUpdate)
			ScrUpdateFlag = PALETTEMASK_UPDATEFULL;
		else
			ScrUpdateFlag = 0x00000000;

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
			bLowMedMix |= ConvST_CompareResolution(y, &LineUpdate, res);
			ConvST_ComparePalette(y,&LineUpdate);
			HBLPaletteMasks[y] = (HBLPaletteMasks[y]&(~PALETTEMASK_UPDATEMASK)) | LineUpdate;
			/* Copy palette and mask for next frame */
			memcpy(&pFrameBuffer->HBLPalettes[y*16],HBLPalette,sizeof(short int)*16);
			pFrameBuffer->HBLPaletteMasks[y] = HBLPaletteMasks[y];
		}
		/* Did mix/have medium resolution? */
		if (bLowMedMix || (res & ST_MEDIUM_RES_BIT))
			res = ST_MEDIUM_RES;
	}

	/* Copy old palette for compare */
	memcpy(PrevHBLPalette, HBLPalettes, sizeof(uint16_t) * 16);

	return res;
}


/**
 * Update Palette Mask to show 'full-update' required. This is usually done after a resolution change
 * or when going between a Window and full-screen display
 */
static void ConvST_SetFullUpdateMask(void)
{
	int y;

	for (y = 0; y < NUM_VISIBLE_LINES; y++)
		HBLPaletteMasks[y] |= PALETTEMASK_UPDATEFULL;
}


/**
 * Set details for ST screen conversion.
 */
static void ConvST_SetConvertDetails(void)
{
	Screen_GetDimension(&pPCScreenDest, NULL, NULL, &PCScreenBytesPerLine);

	pSTScreen = pFrameBuffer->pSTScreen;          /* Source in ST memory */
	pSTScreenCopy = pFrameBuffer->pSTScreenCopy;  /* Previous ST screen */

	/* Center to available framebuffer */
	pPCScreenDest += PCScreenOffsetY * PCScreenBytesPerLine / sizeof(*pPCScreenDest) + PCScreenOffsetX;

	pHBLPalettes = pFrameBuffer->HBLPalettes;     /* HBL palettes pointer */
	/* Not in TV-Mode? Then double up on Y: */
	bScrDoubleY = !(ConfigureParams.Screen.nMonitorType == MONITOR_TYPE_TV);

	if (ConfigureParams.Screen.bAllowOverscan)  /* Use borders? */
	{
		/* Always draw to WHOLE screen including ALL borders */
		STScreenLeftSkipBytes = 0;              /* Number of bytes to skip on ST screen for left (border) */

		if (bUseHighRes)
		{
				pFrameBuffer->VerticalOverscanCopy = VerticalOverscan = V_OVERSCAN_NONE;
			STScreenStartHorizLine = 0;
			STScreenEndHorizLine = 400;
		}
		else
		{
			STScreenWidthBytes = SCREENBYTES_LINE;  /* Number of horizontal bytes in our ST screen */
			STScreenStartHorizLine = OVERSCAN_TOP - nBorderPixelsTop;
			STScreenEndHorizLine = OVERSCAN_TOP + 200 + nBorderPixelsBottom;
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


/**
 * Draw ST screen to window/full-screen framebuffer
 * @return  true if screen contents changed
 */
bool ConvST_DrawFrame(void)
{
	int new_res;
	void (*pDrawFunction)(void);
	static bool bPrevFrameWasSpec512 = false;

	assert(!bUseVDIRes);

	/* Scan palette/resolution masks for each line and build up palette/difference tables */
	new_res = ConvST_ComparePaletteMask(STRes);
	/* Did we change resolution this frame - allocate new screen if did so */
	ConvST_DidResolutionChange(new_res);
	/* Is need full-update, tag as such */
	if (pFrameBuffer->bFullUpdate)
		ConvST_SetFullUpdateMask();

	/* Lock screen for direct screen surface format writes */
	if (ConfigureParams.Screen.DisableVideo || !Screen_Lock())
	{
		return false;
	}

	bScreenContentsChanged = false;      /* Did change (ie needs blit?) */

	/* Set details */
	ConvST_SetConvertDetails();

	/* Clear screen on full update to clear out borders and also interleaved lines */
	if (pFrameBuffer->bFullUpdate)
		Screen_ClearScreen();

	/* Call drawing for full-screen */
	pDrawFunction = ScreenDrawFunctionsNormal[STRes];
	/* Check if is Spec512 image */
	if (Spec512_IsImage())
	{
		bPrevFrameWasSpec512 = true;
		/* What mode were we in? Keep to 320xH or 640xH */
		if (pDrawFunction==ConvertLowRes_320x32Bit)
			pDrawFunction = ConvertLowRes_320x32Bit_Spec;
		else if (pDrawFunction==ConvertLowRes_640x32Bit)
			pDrawFunction = ConvertLowRes_640x32Bit_Spec;
		else if (pDrawFunction==ConvertMediumRes_640x32Bit)
			pDrawFunction = ConvertMediumRes_640x32Bit_Spec;
	}
	else if (bPrevFrameWasSpec512)
	{
		/* If we switch back from Spec512 mode to normal
		 * screen rendering, we have to make sure to do
		 * a full update of the screen. */
		ConvST_SetFullUpdateMask();
		bPrevFrameWasSpec512 = false;
	}

	/* Store palette for screenshots
	 * pDrawFunction may override this if it calls ConvGen_Convert */
	ConvertPalette = STRGBPalette;
	ConvertPaletteSize = (STRes == ST_MEDIUM_RES) ? 4 : 16;

	if (pDrawFunction)
		CALL_VAR(pDrawFunction);

	/* Unlock screen */
	Screen_UnLock();

	/* Clear flags, remember type of overscan as if change need screen full update */
	pFrameBuffer->bFullUpdate = false;
	pFrameBuffer->VerticalOverscanCopy = VerticalOverscan;

	return bScreenContentsChanged;
}


/* -------------- ST/STE screen conversion routines ---------------------------
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

/**
 * Update the STRGBPalette[] array with current colours for this raster line.
 *
 * Return 'ScrUpdateFlag', 0x80000000=Full update, 0x40000000=Update
 * as palette changed
 */
static int AdjustLinePaletteRemap(int y)
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
	static const int endiantable[16] = {0,2,1,3,8,10,9,11,4,6,5,7,12,14,13,15};
#endif
	uint16_t *actHBLPal;
	int i;

	/* Copy palette and convert to RGB in display format */
	actHBLPal = pHBLPalettes + (y<<4);    /* offset in palette */
	for (i=0; i<16; i++)
	{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		STRGBPalette[endiantable[i]] = ST2RGB[*actHBLPal++];
#else
		STRGBPalette[i] = ST2RGB[*actHBLPal++];
#endif
	}
	ScrUpdateFlag = HBLPaletteMasks[y];
	return ScrUpdateFlag;
}


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


/**
 * Copy given line (address) of given length, to line below it
 * as-is, or halve its intensity, depending on bScrDoubleY.
 *
 * Source line is already in host format, so we don't need to
 * care about endianness.
 *
 * Return address to next line after the copy
 */
static uint32_t *Double_ScreenLine32(uint32_t *line, int size)
{
	int fmt_size = size/4;
	uint32_t *next;
	uint32_t mask, rmask, gmask, bmask;

	next = line + fmt_size;
	/* copy as-is */
	if (bScrDoubleY)
	{
		memcpy(next, line, size);
		return next + fmt_size;
	}

	Screen_GetPixelFormat(&rmask, &gmask, &bmask, NULL, NULL, NULL);

	/* TV-mode -- halve the intensity while copying */
	mask = ((rmask >> 1) & rmask)
	     | ((gmask >> 1) & gmask)
	     | ((bmask >> 1) & bmask);
	do {
		*next++ = (*line++ >> 1) & mask;
	}
	while (--fmt_size);

	return next;
}


/* lookup tables and conversion macros */
#include "convert/macros.h"

/* Conversion routines */

#include "convert/low320x32.c"		/* LowRes To 320xH x 32-bit color */
#include "convert/low640x32.c"		/* LowRes To 640xH x 32-bit color */
#include "convert/med640x32.c"		/* MediumRes To 640xH x 32-bit color */
#include "convert/low320x32_spec.c"	/* LowRes Spectrum 512 To 320xH x 32-bit color */
#include "convert/low640x32_spec.c"	/* LowRes Spectrum 512 To 640xH x 32-bit color */
#include "convert/med640x32_spec.c"	/* MediumRes Spectrum 512 To 640xH x 32-bit color */
