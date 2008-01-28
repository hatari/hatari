/*
  Hatari - screen.h

  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREEN_H
#define HATARI_SCREEN_H

#include <SDL_video.h>    /* for SDL_Surface */


/* The 'screen' is a representation of the ST video memory	*/
/* taking into account all the border tricks. Data are stored	*/
/* in 'planar' format (1 word per plane) and are then converted	*/
/* to an SDL buffer that will be displayed.			*/
/* So, all video lines are converted to a unique line of	*/
/* SCREENBYTES_LINE bytes in planar format.			*/
/* SCREENBYTES_LINE should always be a multiple of 8.		*/

/* left/right borders must be multiple of 8 bytes */
#define SCREENBYTES_LEFT    24		/* Bytes for left border in ST screen */
#define SCREENBYTES_MIDDLE  160		/* Middle (320 pixels) */
#define SCREENBYTES_RIGHT   24		/* right border */
#define SCREENBYTES_LINE    (SCREENBYTES_LEFT+SCREENBYTES_MIDDLE+SCREENBYTES_RIGHT)
#define SCREENBYTES_MONOLINE 80         /* Byte per line in ST-high resolution */

/* Overscan values */
#define OVERSCAN_LEFT       (SCREENBYTES_LEFT*2)    /* Number of pixels in each border */
#define OVERSCAN_RIGHT      (SCREENBYTES_RIGHT*2)
#define OVERSCAN_TOP        29
#define OVERSCAN_BOTTOM     47

/* Number of visible screen lines including top/bottom borders */
#define NUM_VISIBLE_LINES  (OVERSCAN_TOP+200+OVERSCAN_BOTTOM)


/* Frame buffer, used to store details in screen conversion */
typedef struct
{
  Uint16 HBLPalettes[(NUM_VISIBLE_LINES+1)*16];   /* 1x16 colour palette per screen line, +1 line as may write after line 200 */
  Uint32 HBLPaletteMasks[NUM_VISIBLE_LINES+1];    /* Bit mask of palette colours changes, top bit set is resolution change */
  Uint8 *pSTScreen;             /* Copy of screen built up during frame (copy each line on HBL to simulate monitor raster) */
  Uint8 *pSTScreenCopy;         /* Previous frames copy of above  */
  int OverscanModeCopy;         /* Previous screen overscan mode */
  BOOL bFullUpdate;             /* Set TRUE to cause full update on next draw */
} FRAMEBUFFER;

/* Number of frame buffers (1 or 2) - should be 2 for supporting screen flipping */
#define NUM_FRAMEBUFFERS  2


/* ST/TT resolution defines */
enum
{
  ST_LOW_RES,
  ST_MEDIUM_RES,
  ST_HIGH_RES,
  TT_MEDIUM_RES = 4,
  TT_HIGH_RES = 6,
  TT_LOW_RES
};
#define ST_MEDIUM_RES_BIT 0x1
#define ST_RES_MASK 0x3

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


extern BOOL bGrabMouse;
extern BOOL bInFullScreen;
extern int nScreenZoomX, nScreenZoomY;
extern int STScreenStartHorizLine;
extern int STScreenLeftSkipBytes;
extern FRAMEBUFFER *pFrameBuffer;
extern Uint8 *pSTScreen;
extern SDL_Surface *sdlscrn;
extern Uint32 STRGBPalette[16];
extern Uint32 ST2RGB[4096];

extern void Screen_Init(void);
extern void Screen_UnInit(void);
extern void Screen_Reset(void);
extern void Screen_SetFullUpdate(void);
extern void Screen_EnterFullScreen(void);
extern void Screen_ReturnFromFullScreen(void);
extern void Screen_ModeChanged(void);
extern void Screen_Draw(void);

#endif  /* ifndef HATARI_SCREEN_H */
