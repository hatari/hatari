/*
  Hatari - screen.h

  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREEN_H
#define HATARI_SCREEN_H

#include <SDL_video.h>    /* for SDL_Surface */


/* Frame buffer, used to store details in screen conversion */
typedef struct
{
  unsigned short int HBLPalettes[(NUM_VISIBLE_LINES+1)*16];  /* 1x16 colour palette per screen line, +1 line as may write after line 200 */
  unsigned long HBLPaletteMasks[NUM_VISIBLE_LINES+1];        /* Bit mask of palette colours changes, top bit set is resolution change */
  unsigned char *pSTScreen;          /* Copy of screen built up during frame(copy each line on HBL to simulate monitor raster) */
  unsigned char *pSTScreenCopy;      /* Previous frames copy of above  */
  int OverscanModeCopy;              /* Previous screen overscan mode */
  BOOL bFullUpdate;                  /* Set TRUE to cause full update on next draw */
} FRAMEBUFFER;

/* Number of frame buffers (1 or 2) - should be 2 for supporting screen flipping */
#define NUM_FRAMEBUFFERS  2

/* Details for each display screen - both Window and FullScreen */
typedef struct
{
  int STScreenLeftSkipBytes,STScreenWidthBytes;     /* Bytes to skip on left and number of bytes of screen to draw */
  int STScreenStartHorizLine,STScreenEndHorizLine;  /* Start/End points in ST screen(28 is top of normal screen) */
  int PCStartHorizLine,PCStartXOffset;              /* Source ST lines/bytes to skip, Destination screen lines/bytes to skip */
} SCREENDRAW_OVERSCAN;

typedef struct
{
  void *pDrawFunction;              /* Draw function */
  int Width,Height,BitDepth,VertPixelsPerLine;
  SCREENDRAW_OVERSCAN Overscan[4];  /* Details for starting offset for each overscan mode(none,top,bottom,both) */
} SCREENDRAW;

typedef struct
{
  SCREENDRAW *pLowRes;
  SCREENDRAW *pMediumRes;
  SCREENDRAW *pHighRes;
  SCREENDRAW *pLowMediumMixRes;
} SCREENDRAW_DISPLAYOPTIONS;

/* ST Resolution defines */
enum
{
  ST_LOW_RES,
  ST_MEDIUM_RES,
  ST_HIGH_RES,
  ST_LOWMEDIUM_MIX_RES
};

/* Update Palette defines */
enum
{
  UPDATE_PALETTE_NONE,
  UPDATE_PALETTE_UPDATE,
  UPDATE_PALETTE_FULLUPDATE
};

/* Palette mask values for 'HBLPaletteMask[]' */
#define PALETTEMASK_RESOLUTION  0x00040000
#define PALETTEMASK_PALETTE     0x0000ffff
#define PALETTEMASK_UPDATERES   0x20000000
#define PALETTEMASK_UPDATEPAL   0x40000000
#define PALETTEMASK_UPDATEFULL  0x80000000
#define PALETTEMASK_UPDATEMASK  (PALETTEMASK_UPDATEFULL|PALETTEMASK_UPDATEPAL|PALETTEMASK_UPDATERES)

/* Overscan values */
enum
{
  OVERSCANMODE_NONE,     /* 0x00 */
  OVERSCANMODE_TOP,      /* 0x01 */
  OVERSCANMODE_BOTTOM    /* 0x02 (Top+Bottom) 0x03 */
};

/* Available fullscreen modes */
#define NUM_DISPLAYMODEOPTIONS	6
enum
{
  DISPLAYMODE_LOWCOL_LOWRES,     /* low color, low resolution (fastest) */
  DISPLAYMODE_LOWCOL_HIGHRES,    /* low color, zoomed resolution */
  DISPLAYMODE_LOWCOL_DUMMY,      /* unused */
  DISPLAYMODE_HICOL_LOWRES,      /* high color, low resolution */
  DISPLAYMODE_HICOL_HIGHRES,     /* high color, zoomed resolution (slowest) */
  DISPLAYMODE_HICOL_DUMMY        /* unused */
};


/* For palette we don't go from colour '0' as the whole background would change, so go from this value */
#define  BASECOLOUR       0x0A
#define  BASECOLOUR_LONG  0x0A0A0A0A

extern FRAMEBUFFER *pFrameBuffer;
extern unsigned char *pSTScreen,*pSTScreenCopy;
extern unsigned char *pPCScreenDest;
extern int STScreenStartHorizLine,STScreenEndHorizLine;
extern int PCScreenBytesPerLine,STScreenWidthBytes,STScreenLeftSkipBytes;
extern BOOL bInFullScreen;
extern BOOL bScreenContentsChanged;
extern int STRes,PrevSTRes;
extern int STScreenLineOffset[NUM_VISIBLE_LINES];
extern unsigned long STRGBPalette[16];
extern unsigned long ST2RGB[2048];
extern SDL_Surface *sdlscrn;
extern BOOL bGrabMouse;

extern void Screen_Init(void);
extern void Screen_UnInit(void);
extern void Screen_Reset(void);
extern void Screen_SetScreenLineOffsets(void);
extern void Screen_SetFullUpdate(void);
extern void Screen_SetupRGBTable(void);
extern void Screen_EnterFullScreen(void);
extern void Screen_ReturnFromFullScreen(void);
extern void Screen_ClearScreen(void);
extern void Screen_SetDrawModes(void);
extern void Screen_DidResolutionChange(void);
extern void Screen_Handle8BitPalettes(void);
extern void Screen_Blit(BOOL bSwapScreen);
extern void Screen_DrawFrame(BOOL bForceFlip);
extern void Screen_Draw(void);

#endif  /* ifndef HATARI_SCREEN_H */
