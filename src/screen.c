/*
  Hatari - screen.c

  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.

  This code converts a 1/2/4 plane ST format screen to either 8 or 16-bit PC
  format. An awful lost of processing is needed to do this conversion - we
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
static char rcsid[] = "Hatari $Id: screen.c,v 1.23 2003-03-27 11:24:45 emanne Exp $";

#include <SDL.h>

#include "main.h"
#include "dialog.h"
#include "ikbd.h"
#include "m68000.h"
#include "memAlloc.h"
#include "misc.h"
#include "printer.h"
#include "screen.h"
#include "screenConvert.h"
#include "screenDraw.h"
#include "screenSnapShot.h"
#include "sound.h"
#include "spec512.h"
#include "vdi.h"
#include "video.h"

/* SDL Video attributes for fullscreen */
const unsigned int sdlvmode = SDL_HWSURFACE|SDL_DOUBLEBUF|SDL_FULLSCREEN|SDL_HWPALETTE;

SCREENDRAW ScreenDrawWindow[4];                   /* Set up with details of drawing functions for ST_xxx_RES */
SCREENDRAW ScreenDrawFullScreen[4];               /* And for full-screen */
SCREENDRAW ScreenDrawVDIWindow[4];
SCREENDRAW ScreenDrawVDIFullScreen[4];            /* And for full-screen */
FRAMEBUFFER FrameBuffers[NUM_FRAMEBUFFERS];       /* Store frame buffer details to tell how to update */
FRAMEBUFFER *pFrameBuffer;                        /* Pointer into current 'FrameBuffer' */
unsigned char *pScreenBitmap=NULL;                /* Screen pixels in PC RGB format, allocated with 'CreateDIBSection' */
unsigned char *pSTScreen,*pSTScreenCopy;          /* Keep track of current and previous ST screen data */
unsigned char *pPCScreenDest;                     /* Destination PC buffer */
int STScreenStartHorizLine,STScreenEndHorizLine;  /* Start/End lines to be converted */
int PCScreenBytesPerLine, STScreenWidthBytes, STScreenLeftSkipBytes;
BOOL bInFullScreen=FALSE;                         /* TRUE if in full screen */
BOOL bFullScreenHold = FALSE;                     /* TRUE if hold display while full screen */
BOOL bScreenContentsChanged;                      /* TRUE if buffer changed and requires blitting */
int STRes=ST_LOW_RES, PrevSTRes=ST_LOW_RES;       /* Current and previous ST resolutions */
int nDroppedFrames=0;                             /* Number of dropped frames during emulation run */

int STScreenLineOffset[NUM_VISIBLE_LINES];        /* Offsets for ST screen lines eg, 0,160,320... */
unsigned long STRGBPalette[16];                   /* Palette buffer used in assembler conversion routines */
unsigned long ST2RGB[2048];                       /* Table to convert ST Palette 0x777 to PC format RGB551(2 pixels each entry) */
unsigned short int HBLPalette[16], PrevHBLPalette[16];  /* Current palette for line, also copy of first line */

SDL_Surface *sdlscrn;                             /* The SDL screen surface */
BOOL bGrabMouse = FALSE;                          /* Grab the mouse cursor in the window */


/*-----------------------------------------------------------------------*/
/*
  Set window size
*/
static void Screen_SetWindowRes()
{
  int Width, Height,BitCount;
  if (bUseVDIRes) {
    Width = VDIWidth;
    Height = VDIHeight;
    BitCount = 8;
  } else
    switch(STRes)
      {
      case ST_LOW_RES:
	if (ConfigureParams.Screen.ChosenDisplayMode == 0 ||
	    ConfigureParams.Screen.ChosenDisplayMode == 3) {
	  Width = 320;
	  Height = 200;
	  BitCount = 16;
	  break;
	}
	// else use 640x400
      default:
	Width = 640;
	Height = 400;
	if (STRes == ST_HIGH_RES)
	  BitCount = 8;
	else
	  BitCount = 16;
	break;
      }

  /* Adjust width/height for overscan borders, if mono or VDI we have no overscan */

  if ( !(bUseVDIRes || bUseHighRes) &&
       ConfigureParams.Screen.Advanced.bAllowOverscan)
  {
    /* If using 640 pixel wide screen, double overscan */
    if (Width==640)
    {
      Width += OVERSCAN_LEFT+OVERSCAN_RIGHT;
      Height += OVERSCAN_TOP+OVERSCAN_BOTTOM;
    }
    /* Add in overscan borders(if 640x200 bitmap is double on Y) */
    Width += OVERSCAN_LEFT+OVERSCAN_RIGHT;
    Height += OVERSCAN_TOP+OVERSCAN_BOTTOM;
  }

  sdlscrn=SDL_SetVideoMode(Width, Height, BitCount, SDL_SWSURFACE|SDL_HWPALETTE);
  if( sdlscrn==NULL )
  {
    fprintf(stderr, "Could not set video mode:\n %s\n", SDL_GetError() );
    SDL_Quit();
    exit(-2);
  }
  pScreenBitmap=sdlscrn->pixels;

  if(BitCount==8)
  {
    Screen_Handle8BitPalettes();
  }
  else
  {
    Screen_SetupRGBTable();
  }

  if(!bGrabMouse)
  {
    SDL_WM_GrabInput(SDL_GRAB_OFF);  /* Un-grab mouse pointer in windowed mode */
  }
  Screen_SetDrawModes();        /* Set draw modes(store which modes to use!) */
}

/*-----------------------------------------------------------------------*/
/*
  Init Screen bitmap and buffers/tables needed for ST to PC screen conversion
*/
void Screen_Init(void)
{
  int i;

  /* Clear frame buffer structures and set current pointer */
  Memory_Clear(FrameBuffers, NUM_FRAMEBUFFERS * sizeof(FRAMEBUFFER));

  /* Allocate previous screen check workspace. We are going to double-buffer a double-buffered screen. Oh. */
  for(i=0; i<NUM_FRAMEBUFFERS; i++)
  {
    FrameBuffers[i].pSTScreen = (unsigned char *)Memory_Alloc(((MAX_VDI_WIDTH*MAX_VDI_PLANES)/8)*MAX_VDI_HEIGHT);
    FrameBuffers[i].pSTScreenCopy = (unsigned char *)Memory_Alloc(((MAX_VDI_WIDTH*MAX_VDI_PLANES)/8)*MAX_VDI_HEIGHT);
  }
  pFrameBuffer = &FrameBuffers[0];

  Screen_SetWindowRes();

  Video_SetScreenRasters();                       /* Set rasters ready for first screen */

  Screen_SetScreenLineOffsets();                  /* Store offset to each horizontal line */
  Screen_SetDrawModes();                          /* Set draw modes */
  Screen_SetupRGBTable();                         /* Create color convertion table */
  Screen_SetFullUpdate();                         /* Cause full update of screen */

  /* Configure some SDL stuff: */
  SDL_WM_SetCaption(PROG_NAME, "Hatari");
  SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
  SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_ENABLE);
  SDL_EventState(SDL_MOUSEBUTTONUP, SDL_ENABLE);
  SDL_ShowCursor(SDL_DISABLE);
}


/*-----------------------------------------------------------------------*/
/*
  Free screen bitmap and allocated resources
*/
void Screen_UnInit(void)
{
  int i;

  /* Free memory used for copies */
  for(i=0; i<NUM_FRAMEBUFFERS; i++)
  {
    Memory_Free(FrameBuffers[i].pSTScreen);
    Memory_Free(FrameBuffers[i].pSTScreenCopy);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Reset screen
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
      STRes = ST_HIGH_RES;
    else
      STRes = ST_LOW_RES;
  }
  PrevSTRes = -1;
  /* Cause full update */
  Screen_SetFullUpdate();
}

/*-----------------------------------------------------------------------*/
/*
  Store Y offset for each horizontal line in our source ST screen for each reference in assembler(no multiply)
*/
void Screen_SetScreenLineOffsets(void)
{
  int i;

  for(i=0; i<NUM_VISIBLE_LINES; i++)
    STScreenLineOffset[i] = i * SCREENBYTES_LINE;
}


/*-----------------------------------------------------------------------*/
/*
  Set flags so screen will be TOTALLY re-drawn (clears whole of full-screen)
  next time around
*/
void Screen_SetFullUpdate(void)
{
  int i;

  /* Update frame buffers */
  for(i=0; i<NUM_FRAMEBUFFERS; i++)
    FrameBuffers[i].bFullUpdate = TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Create ST 777 colour format to 16-bits per pixel - call each time change
  resolution or to/from Window display
*/
void Screen_SetupRGBTable(void)
{
  unsigned int STColour, RGBColour;
  unsigned int r, g, b;

  /* Do Red,Green and Blue for all 512 ST colours */
  for(r=0; r<8; r++)
  {
    for(g=0; g<8; g++)
    {
      for(b=0; b<8; b++)
      {
        STColour = (r<<8) | (g<<4) | (b);      /* ST format 0x777 */

        /*RGBColour = (r<<12) | (g<<7) | (b<<2);*/  /* format 0x1555 */
        RGBColour = SDL_MapRGB(sdlscrn->format, (r<<5), (g<<5), (b<<5));

        ST2RGB[STColour] = (RGBColour<<16) | RGBColour;  /* As long's, for speed(write two pixels at once) */
      }
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Enter Full screen mode
*/

static SDL_Surface *set_new_sdl_fsmode() {
  SDL_Surface *newsdlscrn;
  if (bUseVDIRes) {
    newsdlscrn = SDL_SetVideoMode(ScreenDrawVDIFullScreen[STRes].Width, ScreenDrawVDIFullScreen[STRes].Height,
                                 ScreenDrawVDIFullScreen[STRes].BitDepth, sdlvmode);
  } else {
    int Width = ScreenDrawFullScreen[STRes].Width,
      Height = ScreenDrawFullScreen[STRes].Height;
    if ( !bUseHighRes && ConfigureParams.Screen.Advanced.bAllowOverscan ) {
      /* If using 640 pixel wide screen, double overscan */
      if (Width==640)
	{
	  Width += OVERSCAN_LEFT+OVERSCAN_RIGHT;
	  Height += OVERSCAN_TOP+OVERSCAN_BOTTOM;
	}
      /* Add in overscan borders(if 640x200 bitmap is double on Y) */
      Width += OVERSCAN_LEFT+OVERSCAN_RIGHT;
      Height += OVERSCAN_TOP+OVERSCAN_BOTTOM;
    }

    newsdlscrn = SDL_SetVideoMode(Width, Height,
				  ScreenDrawFullScreen[STRes].BitDepth, sdlvmode);
  }
  return newsdlscrn;
}

void Screen_EnterFullScreen(void)
{
  SDL_Surface *newsdlscrn;

  if (!bInFullScreen)
  {
    Main_PauseEmulation();        /* Hold things... */
    SDL_Delay(20);                /* To give sound time to clear! */

    Screen_SetDrawModes();        /* Set draw modes(store which modes to use!) */

    newsdlscrn = set_new_sdl_fsmode();

    if( newsdlscrn==NULL )
    {
      fprintf(stderr, "Could not set video mode:\n %s\n", SDL_GetError() );
    }
    else
    {
      sdlscrn = newsdlscrn;
      pScreenBitmap = newsdlscrn->pixels;
      bInFullScreen = TRUE;

      Screen_SetFullUpdate();           /* Cause full update of screen */
      /* Re-init screen palette: */
      if(sdlscrn->format->BitsPerPixel == 8)
        Screen_Handle8BitPalettes();    /* Initialize new 8 bit palette */
      else
        Screen_SetupRGBTable();         /* Set fullscreen RGB table*/
      Screen_ClearScreen();         /* Black out screen bitmap as will be invalid when return */
    }
    Main_UnPauseEmulation();        /* And off we go... */

    SDL_WM_GrabInput(SDL_GRAB_ON);  /* Grab mouse pointer in fullscreen */
  }
}


/*-----------------------------------------------------------------------*/
/*
  Return from Full screen mode back to a window
*/
void Screen_ReturnFromFullScreen(void)
{
  if (bInFullScreen)
  {
    Main_PauseEmulation();        /* Hold things... */
    SDL_Delay(20);                /* To give sound time to clear! */

    bInFullScreen = FALSE;

    Screen_SetWindowRes();

    Screen_SetupRGBTable();           /* Set window RGB */
    Screen_SetFullUpdate();           /* Cause full update of screen */

    Main_UnPauseEmulation();          /* And off we go... */
  }
}


/*-----------------------------------------------------------------------*/
/*
  Clear Window display memory
*/
void Screen_ClearScreen(void)
{
  SDL_FillRect(sdlscrn,NULL, SDL_MapRGB(sdlscrn->format, 0, 0, 0) );
}


/*-----------------------------------------------------------------------*/
/*
  Set ScreenDrawFullScreen[] and ScreenDrawWindow[] arrays with information for chosen display modes
*/
void Screen_SetDrawModes(void)
{
  SCREENDRAW_DISPLAYOPTIONS *pScreenDisplay;

  /* Clear out */
  Memory_Clear(ScreenDrawWindow,sizeof(SCREENDRAW)*4);
  Memory_Clear(ScreenDrawFullScreen,sizeof(SCREENDRAW)*4);
  Memory_Clear(ScreenDrawVDIWindow,sizeof(SCREENDRAW)*4);
  Memory_Clear(ScreenDrawVDIFullScreen,sizeof(SCREENDRAW)*4);

  /* First, store Window details(set for 16-bit Windows desktop) */
  ScreenDrawWindow[ST_LOW_RES].pDrawFunction = ConvertLowRes_320x16Bit;
  ScreenDrawWindow[ST_LOW_RES].Width = 320;     ScreenDrawWindow[ST_LOW_RES].Height = 200;     ScreenDrawWindow[ST_LOW_RES].BitDepth = 16;

  ScreenDrawWindow[ST_MEDIUM_RES].pDrawFunction = ConvertMediumRes_640x16Bit;
  ScreenDrawWindow[ST_MEDIUM_RES].Width = 640;  ScreenDrawWindow[ST_MEDIUM_RES].Height = 200;  ScreenDrawWindow[ST_MEDIUM_RES].BitDepth = 16;

  ScreenDrawWindow[ST_HIGH_RES].pDrawFunction = ConvertHighRes_640x8Bit;
  ScreenDrawWindow[ST_HIGH_RES].Width = 640;    ScreenDrawWindow[ST_HIGH_RES].Height = 400;    ScreenDrawWindow[ST_HIGH_RES].BitDepth = 8;

  /* (NOTE this is irrelevant as is directed to low/medium when starts) */
  ScreenDrawWindow[ST_LOWMEDIUM_MIX_RES].pDrawFunction = ConvertLowRes_640x16Bit;
  ScreenDrawWindow[ST_LOWMEDIUM_MIX_RES].Width = 640;  ScreenDrawWindow[ST_LOWMEDIUM_MIX_RES].Height = 200;  ScreenDrawWindow[ST_LOWMEDIUM_MIX_RES].BitDepth = 16;

  /* And for VDI screens (set for 8-bit) */
  ScreenDrawVDIWindow[ST_LOW_RES].pDrawFunction = ConvertVDIRes_16Colour;
  ScreenDrawVDIWindow[ST_LOW_RES].Width = VDIWidth;  ScreenDrawVDIWindow[ST_LOW_RES].Height = VDIHeight;  ScreenDrawVDIWindow[ST_LOW_RES].BitDepth = 8;
  ScreenDrawVDIWindow[ST_MEDIUM_RES].pDrawFunction = ConvertVDIRes_4Colour;
  ScreenDrawVDIWindow[ST_MEDIUM_RES].Width = VDIWidth;  ScreenDrawVDIWindow[ST_MEDIUM_RES].Height = VDIHeight;  ScreenDrawVDIWindow[ST_MEDIUM_RES].BitDepth = 8;
  ScreenDrawVDIWindow[ST_HIGH_RES].pDrawFunction = ConvertVDIRes_2Colour;
  ScreenDrawVDIWindow[ST_HIGH_RES].Width = VDIWidth;  ScreenDrawVDIWindow[ST_HIGH_RES].Height = VDIHeight;  ScreenDrawVDIWindow[ST_HIGH_RES].BitDepth = 8;

  /* And full-screen, select from Overscan/Non-Overscan */
  if (ConfigureParams.Screen.Advanced.bAllowOverscan) {
    pScreenDisplay = &ScreenDisplayOptions[ConfigureParams.Screen.ChosenDisplayMode];
  } else {
    pScreenDisplay = &ScreenDisplayOptions_NoOverscan[ConfigureParams.Screen.ChosenDisplayMode];
  }

  /* Assign full-screen draw modes from chosen option under dialog */
  ScreenDrawFullScreen[ST_LOW_RES] = *pScreenDisplay->pLowRes;
  ScreenDrawFullScreen[ST_MEDIUM_RES] = *pScreenDisplay->pMediumRes;
  ScreenDrawFullScreen[ST_HIGH_RES] = *pScreenDisplay->pHighRes;
  ScreenDrawFullScreen[ST_LOWMEDIUM_MIX_RES] = *pScreenDisplay->pLowMediumMixRes;

  /* And VDI(8-bit), according to select resolution */
  switch(VDIWidth)
  {
    case 640:
      ScreenDrawVDIFullScreen[ST_LOW_RES] = VDIScreenDraw_640x480[0];
      ScreenDrawVDIFullScreen[ST_MEDIUM_RES] = VDIScreenDraw_640x480[1];
      ScreenDrawVDIFullScreen[ST_HIGH_RES] = VDIScreenDraw_640x480[2];
      break;
    case 800:
      ScreenDrawVDIFullScreen[ST_LOW_RES] = VDIScreenDraw_800x600[0];
      ScreenDrawVDIFullScreen[ST_MEDIUM_RES] = VDIScreenDraw_800x600[1];
      ScreenDrawVDIFullScreen[ST_HIGH_RES] = VDIScreenDraw_800x600[2];
      break;
    case 1024:
      ScreenDrawVDIFullScreen[ST_LOW_RES] = VDIScreenDraw_1024x768[0];
      ScreenDrawVDIFullScreen[ST_MEDIUM_RES] = VDIScreenDraw_1024x768[1];
      ScreenDrawVDIFullScreen[ST_HIGH_RES] = VDIScreenDraw_1024x768[2];
      break;
  }
}

/*-----------------------------------------------------------------------*/
/*
  Have we changes beteen low/medium/high res?
*/
void Screen_DidResolutionChange(void)
{
  /* Did change res? */
  if (STRes!=PrevSTRes)
  {
    /* Set new fullscreen display mode, if differs from current */
    if (bInFullScreen)
    {
      SDL_Surface *newsdlscrn;

      newsdlscrn = set_new_sdl_fsmode();

      if( newsdlscrn )
      {
        sdlscrn = newsdlscrn;
        /* Re-init screen palette: */
        if(sdlscrn->format->BitsPerPixel == 8)
          Screen_Handle8BitPalettes();  /* Initialize new 8 bit palette */
        else
          Screen_SetupRGBTable();       /* Set fullscreen RGB table */
      }
      else
      {
        bInFullScreen = FALSE;
      }
    }

    if( !bInFullScreen )
    {
      Screen_SetWindowRes();
    }

    PrevSTRes = STRes;
    Screen_SetFullUpdate();
  }

  /* Did change overscan mode? Causes full update */
  if (pFrameBuffer->OverscanModeCopy!=OverscanMode)
    pFrameBuffer->bFullUpdate = TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Compare current resolution on line with previous, and set 'UpdateLine' accordingly
  Also check if swap between low/medium resolution and return in 'bLowMedMix'
*/
void Screen_CompareResolution(int y, int *pUpdateLine, BOOL *pbLowMedMix)
{
  int Resolution;

  /* Check if wrote to resolution register */
  if (HBLPaletteMasks[y]&PALETTEMASK_RESOLUTION)  /* See 'Intercept_ShifterMode_WriteByte' */
  {
    Resolution = (HBLPaletteMasks[y]>>16)&0x1;
    /* Have used any low/medium res mix? */
    if (Resolution!=(STRes&0x1))
      *pbLowMedMix = TRUE;
    /* Did change resolution */
    if (Resolution!=(int)((pFrameBuffer->HBLPaletteMasks[y]>>16)&0x1))
      *pUpdateLine |= PALETTEMASK_UPDATERES;
    else
      *pUpdateLine &= ~PALETTEMASK_UPDATERES;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Check to see if palette changes cause screen update and keep 'HBLPalette[]' up-to-date
*/
void Screen_ComparePalette(int y, int *pUpdateLine)
{
  BOOL bPaletteChanged = FALSE;
  int i;

  /* Did write to palette in this or previous frame? */
  if (((HBLPaletteMasks[y]|pFrameBuffer->HBLPaletteMasks[y])&PALETTEMASK_PALETTE)!=0)
  {
    /* Check and update ones which changed */
    for(i=0; i<16; i++)
    {
      if (HBLPaletteMasks[y]&(1<<i))        /* Update changes in ST palette */
        HBLPalette[i] = HBLPalettes[(y*16)+i];
    }
    /* Now check with same palette from previous frame for any differences(may be changing palette back) */
    for(i=0; (i<16) && (!bPaletteChanged); i++)
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
/*
  Check for differences in Palette and Resolution from Mask table and update and
  store off which lines need updating and create full-screen palette.
  (It is very important for these routines to check for colour changes with the previous
  screen so only the very minimum parts are updated)
*/
int Screen_ComparePaletteMask(void)
{
  BOOL bLowMedMix=FALSE;
  int LineUpdate = 0;
  int y;

  /* Set for monochrome? */
  if (bUseHighRes)
  {
    OverscanMode = OVERSCANMODE_NONE;

    /* Just copy mono colours */
    if (HBLPalettes[0]&0x777)
    {
      HBLPalettes[0] = 0x777;  HBLPalettes[1] = 0x000;
    }
    else
    {
      HBLPalettes[0] = 0x000;  HBLPalettes[1] = 0x777;
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
    STRes = VDIRes;

    /* Colors changed? */
    if (HBLPalettes[0]!=PrevHBLPalette[0])
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
    STRes = ST_HIGH_RES;
  }
  else    /* Full colour */
  {
    /* Get resolution */
    STRes = (HBLPaletteMasks[0]>>16)&0x3;
    /* Do all lines - first is tagged as full-update */
    for(y=0; y<NUM_VISIBLE_LINES; y++)
    {
      /* Find any resolution/palette change and update palette/mask buffer */
      /* ( LineUpdate has top two bits set to say if line needs updating due to palette or resolution change ) */
      Screen_CompareResolution(y,&LineUpdate,&bLowMedMix);
      Screen_ComparePalette(y,&LineUpdate);
      HBLPaletteMasks[y] = (HBLPaletteMasks[y]&(~PALETTEMASK_UPDATEMASK)) | LineUpdate;
      /* Copy palette and mask for next frame */
      memcpy(&pFrameBuffer->HBLPalettes[y*16],HBLPalette,sizeof(short int)*16);
      pFrameBuffer->HBLPaletteMasks[y] = HBLPaletteMasks[y];
    }
    /* Did mix resolution? */
    if (bLowMedMix)
      STRes = ST_LOWMEDIUM_MIX_RES;
  }

  return(STRes);
}


/*-----------------------------------------------------------------------*/
/*
  Create 8-Bit palette for display if needed
*/
void Screen_CreatePalette(void)
{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
  static const int endiantable[16] = {0,2,1,3,8,10,9,11,4,6,5,7,12,14,13,15};
#endif
  SDL_Color sdlColors[16];
  int i, j;

  fprintf(stderr,"Creating palette %x\n",HBLPalettes[0]);
  if(bUseHighRes)
  {
    /* Colors for monochrome screen mode emulation */
    if(HBLPalettes[0])
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
    /* Colors for color screen mode emulation */
    for(i=0; i<16; i++)
    {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
      j = endiantable[i];
#else
      j = i;
#endif
      sdlColors[j].r = ((HBLPalettes[i]>>8)&0x7)<<5;
      sdlColors[j].g = ((HBLPalettes[i]>>4)&0x7)<<5;
      sdlColors[j].b = (HBLPalettes[i]&0x7)<<5;
    }
    SDL_SetColors(sdlscrn, sdlColors, 10, 16);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Create 8-Bit palette for display if needed.
*/
void Screen_Handle8BitPalettes(void)
{
  BOOL bPaletteChanged=FALSE;
  int i;

  /* Do need to check for 8-Bit palette change? Ie, update whole screen */
  /* VDI screens and monochrome modes are ALL 8-Bit at the moment! */
  if(sdlscrn->format->BitsPerPixel == 8)
  {
    /* If using HiRes palette update with full update flag */
    if (!bUseHighRes)
    {
      /* Check if palette of 16 colours changed from previous frame */
      for(i=0; (i<16) && (!bPaletteChanged); i++)
      {
        /* Check with first line palette(stored in 'Screen_ComparePaletteMask') */
        if (HBLPalettes[i]!=PrevHBLPalette[i])
          bPaletteChanged = TRUE;
      }
    }

    /* Did palette change or do we require a full update? */
    if ( (bPaletteChanged) || (pFrameBuffer->bFullUpdate) )
    {
      /* Create palette, for Full-Screen of Window */
      Screen_CreatePalette();
      /* Make sure update whole screen */
      pFrameBuffer->bFullUpdate = TRUE;
    }
  }

  /* Copy old palette for 8-Bit compare as this routine writes over it */
  memcpy(PrevHBLPalette,HBLPalettes,sizeof(short int)*16);
}


/*-----------------------------------------------------------------------*/
/*
  Update Palette Mask to show 'full-update' required. This is usually done after a resolution change
  or when going between a Window and full-screen display
*/
void Screen_SetFullUpdateMask(void)
{
  int y;

  for(y=0; y<NUM_VISIBLE_LINES; y++)
    HBLPaletteMasks[y] |= PALETTEMASK_UPDATEFULL;
}


/*-----------------------------------------------------------------------*/
/*
  Set details for ST screen conversion(Window version)
*/
void Screen_SetWindowConvertDetails(void)
{
  /* Reset Double Y, used for Window medium res' and in full screen */
  bScrDoubleY = FALSE;

  pSTScreen = pFrameBuffer->pSTScreen;          /* Source in ST memory */
  pSTScreenCopy = pFrameBuffer->pSTScreenCopy;  /* Previous ST screen */
  pPCScreenDest = pScreenBitmap;                /* Destination PC screen */

  STScreenStartHorizLine = 0;                   /* Full height */

  if (ConfigureParams.Screen.bUseHighRes && !bUseVDIRes)
  {
    pFrameBuffer->OverscanModeCopy = OverscanMode = OVERSCANMODE_NONE;
    STScreenEndHorizLine = 400;
  }
  else
  {
    /* Always draw to WHOLE screen including ALL borders */
    STScreenLeftSkipBytes = 0;                  /* Number of bytes to skip on ST screen for left(border) */
    STScreenWidthBytes = SCREENBYTES_LINE;      /* Number of horizontal bytes in our ST screen */

    STScreenEndHorizLine = NUM_VISIBLE_LINES;

    if (bUseVDIRes)
    {
      PCScreenBytesPerLine = VDIWidth;
    }
    else
    {
      PCScreenBytesPerLine = sdlscrn->pitch;
/*       PCScreenBytesPerLine = (SCREENBYTES_LINE*2)*sizeof(short int); */
/*       if (STRes!=ST_LOW_RES)                    /\* Bytes per line in PC screen are DOUBLE when in medium/mix *\/ */
/*         PCScreenBytesPerLine <<= 1; */
    }

    pHBLPalettes = pFrameBuffer->HBLPalettes;
  }

  /* Are we in a Window and medium/mix res? Need to Double Y */
  // if ( (!bInFullScreen) && ((STRes==ST_MEDIUM_RES) || (STRes==ST_LOWMEDIUM_MIX_RES)) ) {
  if (!ConfigureParams.Screen.Advanced.bInterlacedFullScreen) {
    bScrDoubleY = TRUE;
  }
}

/*-----------------------------------------------------------------------*/
/*
  Set details for ST screen conversion (Full-Screen version)
*/
void Screen_SetFullScreenConvertDetails(void)
{
  SCREENDRAW *pScreenDraw;

  /* Reset Double Y, used for window medium res' and in full screen */
  bScrDoubleY = FALSE;

  /* Select screen draw for standard or VDI display */
  if (bUseVDIRes)
    pScreenDraw = &ScreenDrawVDIFullScreen[VDIRes];
  else
    pScreenDraw = &ScreenDrawFullScreen[STRes];

  /* Only draw what can fit into full-screen and centre on Y */
  STScreenLeftSkipBytes = pScreenDraw->Overscan[OverscanMode].STScreenLeftSkipBytes;
  STScreenWidthBytes = pScreenDraw->Overscan[OverscanMode].STScreenWidthBytes;

  STScreenStartHorizLine = pScreenDraw->Overscan[OverscanMode].STScreenStartHorizLine;
  STScreenEndHorizLine = pScreenDraw->Overscan[OverscanMode].STScreenEndHorizLine;

  pSTScreen = pFrameBuffer->pSTScreen;          /* Source in ST memory */
  pSTScreenCopy = pFrameBuffer->pSTScreenCopy;  /* Previous ST screen */
  PCScreenBytesPerLine = sdlscrn->pitch;
  pPCScreenDest = (unsigned char *)sdlscrn->pixels;  /* Destination PC screen */
  /* Get start of line on screen according to overscan (scale if double line mode) */
  pPCScreenDest += pScreenDraw->Overscan[OverscanMode].PCStartHorizLine
      * pScreenDraw->VertPixelsPerLine*PCScreenBytesPerLine;
  /* And offset on X */
  pPCScreenDest += pScreenDraw->Overscan[OverscanMode].PCStartXOffset;
  pHBLPalettes = pFrameBuffer->HBLPalettes;

  /* Is non-interlaced? May need to double up on Y */
  if (!ConfigureParams.Screen.Advanced.bInterlacedFullScreen) {
    bScrDoubleY = TRUE;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Lock full-screen for drawing
*/
BOOL Screen_Lock(void)
{
  if(SDL_MUSTLOCK(sdlscrn))
  {
    if(SDL_LockSurface(sdlscrn))
    {
      Screen_ReturnFromFullScreen();   /* All OK? If not need to jump back to a window */
      return(FALSE);
    }
  }

  return(TRUE);
}

/*-----------------------------------------------------------------------*/
/*
  UnLock full-screen
*/
void Screen_UnLock(void)
{
  if( SDL_MUSTLOCK(sdlscrn) )
    SDL_UnlockSurface(sdlscrn);
}


/*-----------------------------------------------------------------------*/
/*
  Blit our converted ST screen to window/full-screen
  Note that our source image includes all borders so if have them disabled simply blit a smaller source rectangle!
*/
void Screen_Blit(BOOL bSwapScreen)
{
  /* Rectangle areas to Blit according to if overscan is enabled or not (source always includes all borders) */
/*   static SDL_Rect SrcWindowBitmapSizes[] = */
/*   { */
/*     { OVERSCAN_LEFT,OVERSCAN_TOP, 320,200 },                /\* ST_LOW_RES *\/ */
/*     { (OVERSCAN_LEFT<<1),(OVERSCAN_TOP<<1), 640,400 },      /\* ST_MEDIUM_RES *\/ */
/*     { 0,0, 640,400 },                                       /\* ST_HIGH_RES *\/ */
/*     { (OVERSCAN_LEFT<<1),(OVERSCAN_BOTTOM<<1), 640,400 },   /\* ST_LOWMEDIUM_MIX_RES *\/ */
/*   }; */
  static SDL_Rect SrcWindowBitmapSizes[] =
  {
    { 0,0, 320,200 },                /* ST_LOW_RES */
    { 0,0, 640,400 },      /* ST_MEDIUM_RES */
    { 0,0, 640,400 },                                       /* ST_HIGH_RES */
    { 0,0, 640,400 },   /* ST_LOWMEDIUM_MIX_RES */
  };

  static SDL_Rect SrcWindowOverscanBitmapSizes[] =
  {
    { 0,0, OVERSCAN_LEFT+320+OVERSCAN_RIGHT,OVERSCAN_TOP+200+OVERSCAN_BOTTOM },
    { 0,0, (OVERSCAN_LEFT<<1)+640+(OVERSCAN_RIGHT<<1),(OVERSCAN_TOP<<1)+400+(OVERSCAN_BOTTOM<<1) },
    { 0,0, 640,400 },
    { 0,0, (OVERSCAN_LEFT<<1)+640+(OVERSCAN_RIGHT<<1),(OVERSCAN_TOP<<1)+400+(OVERSCAN_BOTTOM<<1) },
  };

  unsigned char *pSTScreen;
  SDL_Rect *SrcRect;

  /* Blit to full screen or window? */
  if (bInFullScreen)
  {
    /* Swap screen */
    if (bSwapScreen)
      SDL_Flip(sdlscrn);
  }
  else
  {
    /* VDI resolution? */
    if (bUseVDIRes || ConfigureParams.Screen.bUseHighRes)
    {
      /* Show VDI or mono resolution, no overscan */
      SDL_UpdateRect(sdlscrn, 0,0,0,0);
    }
    else
    {
      /* Find rectangle to draw from... */
      if (ConfigureParams.Screen.Advanced.bAllowOverscan)
        SrcRect = &SrcWindowOverscanBitmapSizes[STRes];
      else {
        SrcRect = &SrcWindowBitmapSizes[STRes];
      }
      /* Blit image */
      SDL_UpdateRect(sdlscrn, 0,0,0,0);
      //SDL_UpdateRects(sdlscrn, 1, SrcRect);
    }
  }

  /* Swap copy/raster buffers in screen. */
  pSTScreen = pFrameBuffer->pSTScreenCopy;
  pFrameBuffer->pSTScreenCopy = pFrameBuffer->pSTScreen;
  pFrameBuffer->pSTScreen = pSTScreen;
}


/*-----------------------------------------------------------------------*/
/*
  Swap ST Buffers, used for full-screen where have double-buffering
*/
void Screen_SwapSTBuffers(void)
{
#if NUM_FRAMEBUFFERS > 1
  if (bInFullScreen)
  {
    if (pFrameBuffer==&FrameBuffers[0])
      pFrameBuffer = &FrameBuffers[1];
    else
      pFrameBuffer = &FrameBuffers[0];
  }
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Draw ST screen to window/full-screen framebuffer
*/
void Screen_DrawFrame(BOOL bForceFlip)
{
  void *pDrawFunction;
  static void *last;

  /* Scan palette/resolution masks for each line and build up palette/difference tables */
  STRes = Screen_ComparePaletteMask();
  /* Do require palette? Check if changed and update */
  Screen_Handle8BitPalettes();
  /* Did we change resolution this frame - allocate new screen if did so */
  Screen_DidResolutionChange();
  /* Is need full-update, tag as such */
  if (pFrameBuffer->bFullUpdate)
    Screen_SetFullUpdateMask();

  /* Lock screen ready for drawing */
  if (Screen_Lock()) {
    bScreenContentsChanged = FALSE;      /* Did change(ie needs blit?) */
    /* Set details */
    if (ConfigureParams.Screen.Advanced.bAllowOverscan) {
      Screen_SetWindowConvertDetails();
      pPCScreenDest = (unsigned char *)sdlscrn->pixels;  /* Destination PC screen */
    } else
      Screen_SetFullScreenConvertDetails();
    /* Clear screen on full update to clear out borders and also interlaced lines */
    if (pFrameBuffer->bFullUpdate)
      Screen_ClearScreen();
    /* Call drawing for full-screen */
    if (bUseVDIRes)  {
      pDrawFunction = ScreenDrawVDIFullScreen[VDIRes].pDrawFunction;
    }
    else {
      pDrawFunction = ScreenDrawFullScreen[STRes].pDrawFunction;
      /* Check if is Spec512 image */
      if (Spec512_IsImage()) {
	/* What mode were we in? Keep to 320xH or 640xH */
	if (pDrawFunction==ConvertLowRes_320x16Bit)
	  pDrawFunction = ConvertSpec512_320x16Bit;
	else if (pDrawFunction==ConvertLowRes_640x16Bit)
	  pDrawFunction = ConvertSpec512_640x16Bit;
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
    if (bScreenContentsChanged || bForceFlip) {
      if (bInFullScreen)
        Screen_SwapSTBuffers();
      Screen_Blit(TRUE);
    }

    /* Grab any animation */
    if(bRecordingAnimation)
      ScreenSnapShot_RecordFrame(bScreenContentsChanged);
  }
}

/*-----------------------------------------------------------------------*/
/*
  Draw ST screen to window/full-screen
*/
void Screen_Draw(void)
{
  /* Are we holding screen? Ie let user choose options while in full-screen mode using GDI */
  if (bInFullScreen && bFullScreenHold)
  {
    /* Just update status bar */
    /*StatusBar_UpdateIcons();*/ /* No statusbar in Hatari */
    return;
  }

  if (!bQuitProgram)
  {
#if 0
    /* Wait for next display(at 50fps), is ignored if running max speed */
    if ( !(ConfigureParams.Screen.Advanced.bSyncToRetrace && bInFullScreen) )
    {
      /* If in Max speed mode, just get on with it, or else wait for VBL timer */
      if (ConfigureParams.Configure.nMinMaxSpeed!=MINMAXSPEED_MAX)
      {
        /* Has VBL already occured? Means we can't keep 50fps to warn user */
        if (Main_AlreadyWaitingVBLEvent())
        {
          /* Increase counter for number of consecutive dropped frames */
          nDroppedFrames++;
          /* If emulation has gone slow for 1/2 second or more, inform user */
          /*if (nDroppedFrames>=25)
            StatusBar_SetIcon(STATUS_ICON_FRAMERATE,ICONSTATE_UPDATE);*/ /* No statusbar in Hatari yet */
        }
        else
          nDroppedFrames = 0;
        /* Wait for VBL event */
        Main_WaitVBLEvent();
      }
    }
#endif

    if(VideoBase)
    {
      /* And draw(if screen contents changed) */
      Screen_DrawFrame(FALSE);

      /* And status bar */
      /*StatusBar_UpdateIcons();*/ /* Sorry - no statusbar in Hatari yet */
    }

    /* Check printer status */
    Printer_CheckIdleStatus();
  }

}
