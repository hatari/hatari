/*
  Hatari - screen.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREEN_H
#define HATARI_SCREEN_H

#include <SDL_video.h>    /* for SDL_Surface */

#if WITH_SDL2
extern SDL_Window *sdlWindow;
static inline int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors,
                                int firstcolor, int ncolors)
{
	return SDL_SetPaletteColors(surface->format->palette, colors,
	                            firstcolor, ncolors);
}
void SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects);
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h);
#define SDL_GRAB_OFF false
#define SDL_GRAB_ON true
#define SDL_WM_GrabInput SDL_SetRelativeMouseMode
#endif

/* The 'screen' is a representation of the ST video memory	*/
/* taking into account all the border tricks. Data are stored	*/
/* in 'planar' format (1 word per plane) and are then converted	*/
/* to an SDL buffer that will be displayed.			*/
/* So, all video lines are converted to a unique line of	*/
/* SCREENBYTES_LINE bytes in planar format.			*/
/* SCREENBYTES_LINE should always be a multiple of 8.		*/

/* left/right borders must be multiple of 8 bytes */
#define SCREENBYTES_LEFT    (nBorderPixelsLeft/2)  /* Bytes for left border */
#define SCREENBYTES_MIDDLE  160                    /* Middle (320 pixels) */
#define SCREENBYTES_RIGHT   (nBorderPixelsRight/2) /* Right border */
#define SCREENBYTES_LINE    (SCREENBYTES_LEFT+SCREENBYTES_MIDDLE+SCREENBYTES_RIGHT)
#define SCREENBYTES_MONOLINE 80         /* Byte per line in ST-high resolution */

/* Overscan values */
#define OVERSCAN_TOP         29
#define MAX_OVERSCAN_BOTTOM  47 	/* number of bottom lines to display on screen */

/* Number of visible screen lines including top/bottom borders */
#define NUM_VISIBLE_LINES  (OVERSCAN_TOP+200+MAX_OVERSCAN_BOTTOM)

/* Number of visible pixels on each screen line including left/right borders */
#define NUM_VISIBLE_LINE_PIXELS (48+320+48)

/* 1x16 colour palette per screen line, +1 line as may write after line 200 */
#define HBL_PALETTE_LINES ((NUM_VISIBLE_LINES+1 +3 )*16)	/* [NP] FIXME we need to handle 313 hbl, not 310 ; palette code is a mess it should be removed */
/* Bit mask of palette colours changes, top bit set is resolution change */
#define HBL_PALETTE_MASKS (NUM_VISIBLE_LINES+1 +3 )		/* [NP] FIXME we need to handle 313 hbl, not 310 ; palette code is a mess it should be removed */


/* Frame buffer, used to store details in screen conversion */
typedef struct
{
  Uint16 HBLPalettes[HBL_PALETTE_LINES];
  Uint32 HBLPaletteMasks[HBL_PALETTE_MASKS];
  Uint8 *pSTScreen;             /* Copy of screen built up during frame (copy each line on HBL to simulate monitor raster) */
  Uint8 *pSTScreenCopy;         /* Previous frames copy of above  */
  int OverscanModeCopy;         /* Previous screen overscan mode */
  bool bFullUpdate;             /* Set TRUE to cause full update on next draw */
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

extern bool bGrabMouse;
extern bool bInFullScreen;
extern int nScreenZoomX, nScreenZoomY;
extern int nBorderPixelsLeft, nBorderPixelsRight;
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
extern bool Screen_Lock(void);
extern void Screen_UnLock(void);
extern void Screen_SetFullUpdate(void);
extern void Screen_EnterFullScreen(void);
extern void Screen_ReturnFromFullScreen(void);
extern void Screen_ModeChanged(bool bForceChange);
extern bool Screen_Draw(void);
extern bool Screen_SetSDLVideoSize(int width, int height, int bitdepth, bool bForceChange);
extern void Screen_SetGenConvSize(int width, int height, int bpp, bool bForceChange);
extern void Screen_GenConvUpdate(SDL_Rect *extra, bool forced);
extern Uint32 Screen_GetGenConvWidth(void);
extern Uint32 Screen_GetGenConvHeight(void);

#endif  /* ifndef HATARI_SCREEN_H */
