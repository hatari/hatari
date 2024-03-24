/*
  Hatari - sdlgui.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  A tiny graphical user interface for Hatari.
*/
const char SDLGui_fileid[] = "Hatari sdlgui.c";

#include <SDL.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "main.h"
#include "screen.h"
#include "sdlgui.h"
#include "str.h"
#include "log.h"

#include "font5x8.h"
#include "font10x16.h"

#define DEBUG_INFO 0
#if DEBUG_INFO
# define Dprintf(a) printf a
#else
# define Dprintf(a)
#endif

static SDL_Surface *pSdlGuiScrn;            /* Pointer to the actual main SDL screen surface */
static SDL_Surface *pSmallFontGfx = NULL;   /* The small font graphics */
static SDL_Surface *pBigFontGfx = NULL;     /* The big font graphics */
static SDL_Surface *pFontGfx = NULL;        /* The actual font graphics */

static struct {
	Uint32 darkbar, midbar, lightbar;
	Uint32 darkgrey, midgrey, lightgrey;
	Uint32 focus, cursor, underline, editfield;
} colors;

int sdlgui_fontwidth;			/* Width of the actual font */
int sdlgui_fontheight;			/* Height of the actual font */

#define UNDERLINE_INDICATOR '_'


/*-----------------------------------------------------------------------*/
/**
 * Load an 1 plane XBM into a 8 planes SDL_Surface.
 */
static SDL_Surface *SDLGui_LoadXBM(int w, int h, const Uint8 *pXbmBits)
{
	SDL_Surface *bitmap;
	Uint8 *dstbits;
	const Uint8 *srcbits;
	int x, y, srcpitch;
	int mask;

	srcbits = pXbmBits;

	/* Allocate the bitmap */
	bitmap = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 8, 0, 0, 0, 0);
	if (bitmap == NULL)
	{
		Log_Printf(LOG_ERROR, "SDLGui: failed to allocate bitmap: %s", SDL_GetError());
		return NULL;
	}

	srcpitch = ((w + 7) / 8);
	dstbits = (Uint8 *)bitmap->pixels;
	mask = 1;

	/* Copy the pixels */
	for (y = 0 ; y < h ; y++)
	{
		for (x = 0 ; x < w ; x++)
		{
			dstbits[x] = (srcbits[x / 8] & mask) ? 1 : 0;
			mask <<= 1;
			mask |= (mask >> 8);
			mask &= 0xFF;
		}
		dstbits += bitmap->pitch;
		srcbits += srcpitch;
	}

	return bitmap;
}


/*-----------------------------------------------------------------------*/
/**
 * Initialize the GUI.
 */
int SDLGui_Init(void)
{
	SDL_Color blackWhiteColors[2] = {{255, 255, 255, 255}, {0, 0, 0, 255}};

	if (pSmallFontGfx && pBigFontGfx)
	{
		/* already initialized */
		return 0;
	}

	/* Initialize the font graphics: */
	pSmallFontGfx = SDLGui_LoadXBM(font5x8_width, font5x8_height, font5x8_bits);
	pBigFontGfx = SDLGui_LoadXBM(font10x16_width, font10x16_height, font10x16_bits);
	if (pSmallFontGfx == NULL || pBigFontGfx == NULL)
	{
		Log_Printf(LOG_ERROR, "SDLGui: cannot init font graphics!\n");
		return -1;
	}

	/* Set color palette of the font graphics: */
	SDL_SetPaletteColors(pSmallFontGfx->format->palette, blackWhiteColors, 0, 2);
	SDL_SetPaletteColors(pBigFontGfx->format->palette, blackWhiteColors, 0, 2);

	/* Set font color 0 as transparent: */
	SDL_SetColorKey(pSmallFontGfx, SDL_RLEACCEL, 0);
	SDL_SetColorKey(pBigFontGfx, SDL_RLEACCEL, 0);

	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Uninitialize the GUI.
 */
int SDLGui_UnInit(void)
{
	if (pSmallFontGfx)
	{
		SDL_FreeSurface(pSmallFontGfx);
		pSmallFontGfx = NULL;
	}

	if (pBigFontGfx)
	{
		SDL_FreeSurface(pBigFontGfx);
		pBigFontGfx = NULL;
	}

	return 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Inform the SDL-GUI about the actual SDL_Surface screen pointer and
 * prepare the font to suit the actual resolution.
 */
int SDLGui_SetScreen(SDL_Surface *pScrn)
{
	pSdlGuiScrn = pScrn;

	/* Decide which font to use - small or big one: */
	if (pSdlGuiScrn->w >= 640 && pSdlGuiScrn->h >= 400 && pBigFontGfx != NULL)
	{
		pFontGfx = pBigFontGfx;
	}
	else
	{
		pFontGfx = pSmallFontGfx;
	}

	if (pFontGfx == NULL)
	{
		Log_Printf(LOG_ERROR, "SDLGui: a problem with the font occurred!\n");
		return -1;
	}

	/* Get the font width and height: */
	sdlgui_fontwidth = pFontGfx->w/16;
	sdlgui_fontheight = pFontGfx->h/16;

	/* scrollbar */
	colors.darkbar   = SDL_MapRGB(pSdlGuiScrn->format, 64, 64, 64);
	colors.midbar    = SDL_MapRGB(pSdlGuiScrn->format,128,128,128);
	colors.lightbar  = SDL_MapRGB(pSdlGuiScrn->format,196,196,196);
	/* buttons, midgray is also normal bg color */
	colors.darkgrey  = SDL_MapRGB(pSdlGuiScrn->format,128,128,128);
	colors.midgrey   = SDL_MapRGB(pSdlGuiScrn->format,192,192,192);
	colors.lightgrey = SDL_MapRGB(pSdlGuiScrn->format,255,255,255);
	/* others */
	colors.focus     = SDL_MapRGB(pSdlGuiScrn->format,212,212,212);
	colors.cursor    = SDL_MapRGB(pSdlGuiScrn->format,128,128,128);
	if (sdlgui_fontheight < 16)
		colors.underline = SDL_MapRGB(pSdlGuiScrn->format,255,0,255);
	else
		colors.underline = SDL_MapRGB(pSdlGuiScrn->format,0,0,0);
	colors.editfield = SDL_MapRGB(pSdlGuiScrn->format,160,160,160);

	return 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Return character size for current font in given arguments.
 */
void SDLGui_GetFontSize(int *width, int *height)
{
	*width = sdlgui_fontwidth;
	*height = sdlgui_fontheight;
}

/*-----------------------------------------------------------------------*/
/**
 * Center a dialog so that it appears in the middle of the screen.
 * Note: We only store the coordinates in the root box of the dialog,
 * all other objects in the dialog are positioned relatively to this one.
 */
void SDLGui_CenterDlg(SGOBJ *dlg)
{
	dlg[0].x = (pSdlGuiScrn->w/sdlgui_fontwidth-dlg[0].w)/2;
	dlg[0].y = (pSdlGuiScrn->h/sdlgui_fontheight-dlg[0].h)/2;
}

/*-----------------------------------------------------------------------*/
/**
 * Return text length which ignores underlining.
 */
static int SDLGui_TextLen(const char *str)
{
	int len;
	for (len = 0; *str; str++)
	{
		if (*str != UNDERLINE_INDICATOR)
			len++;
	}
	return len;
}

/*-----------------------------------------------------------------------*/
/**
 * Draw a text string (internal version).
 */
static void SDLGui_TextInt(int x, int y, const char *txt, bool underline)
{
	int i, offset;
	unsigned char c;
	SDL_Rect sr, dr;

	/* underline offset needs to go outside the box for smaller font */
	if (sdlgui_fontheight < 16)
		offset = sdlgui_fontheight - 1;
	else
		offset = sdlgui_fontheight - 2;

	i = 0;
	while (txt[i])
	{
		dr.x=x;
		dr.y=y;
		dr.w=sdlgui_fontwidth;
		dr.h=sdlgui_fontheight;

		c = txt[i++];
		if (c == UNDERLINE_INDICATOR && underline)
		{
			dr.h = 1;
			dr.y += offset;
			SDL_FillRect(pSdlGuiScrn, &dr, colors.underline);
			continue;
		}
		/* for now, assume (only) Linux file paths are UTF-8 */
#if !(defined(WIN32) || defined(USE_LOCALE_CHARSET))
		/* Quick and dirty conversion for latin1 characters only... */
		if ((c & 0xc0) == 0xc0)
		{
			c = c << 6;
			c |= (txt[i++]) & 0x7f;
		}
		else if (c >= 0x80)
		{
			Log_Printf(LOG_WARN, "Unsupported character '%c' (0x%x)\n", c, c);
		}
#endif
		x += sdlgui_fontwidth;

		sr.x=sdlgui_fontwidth*(c%16);
		sr.y=sdlgui_fontheight*(c/16);
		sr.w=sdlgui_fontwidth;
		sr.h=sdlgui_fontheight;
		SDL_BlitSurface(pFontGfx, &sr, pSdlGuiScrn, &dr);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Draw a text string (generic).
 */
void SDLGui_Text(int x, int y, const char *txt)
{
	SDLGui_TextInt(x, y, txt, false);
}

/*-----------------------------------------------------------------------*/
/**
 * Draw a dialog text object.
 */
static void SDLGui_DrawText(const SGOBJ *tdlg, int objnum)
{
	int x, y;
	x = (tdlg[0].x+tdlg[objnum].x)*sdlgui_fontwidth;
	y = (tdlg[0].y+tdlg[objnum].y)*sdlgui_fontheight;

	if (tdlg[objnum].flags & SG_EXIT)
	{
		SDL_Rect rect;
		/* Draw background: */
		rect.x = x;
		rect.y = y;
		rect.w = tdlg[objnum].w * sdlgui_fontwidth;
		rect.h = tdlg[objnum].h * sdlgui_fontheight;
		if (tdlg[objnum].state & SG_FOCUSED)
			SDL_FillRect(pSdlGuiScrn, &rect, colors.focus);
		else
			SDL_FillRect(pSdlGuiScrn, &rect, colors.midgrey);
	}

	SDLGui_Text(x, y, tdlg[objnum].txt);
}


/*-----------------------------------------------------------------------*/
/**
 * Draw a edit field object.
 */
static void SDLGui_DrawEditField(const SGOBJ *edlg, int objnum)
{
	int x, y;
	SDL_Rect rect;

	x = (edlg[0].x+edlg[objnum].x)*sdlgui_fontwidth;
	y = (edlg[0].y+edlg[objnum].y)*sdlgui_fontheight;
	SDLGui_Text(x, y, edlg[objnum].txt);

	rect.x = x;
	rect.y = y + edlg[objnum].h * sdlgui_fontheight;
	rect.w = edlg[objnum].w * sdlgui_fontwidth;
	rect.h = 1;
	SDL_FillRect(pSdlGuiScrn, &rect, colors.editfield);
}


/*-----------------------------------------------------------------------*/
/**
 * Draw a dialog box object.
 */
static void SDLGui_DrawBox(const SGOBJ *bdlg, int objnum)
{
	SDL_Rect rect;
	int x, y, w, h, offset;
	Uint32 color, upleftc, downrightc;

	if (bdlg[objnum].state & SG_FOCUSED)
		color = colors.focus;
	else
		color = colors.midgrey;

	x = bdlg[objnum].x*sdlgui_fontwidth;
	y = bdlg[objnum].y*sdlgui_fontheight;
	if (objnum > 0)                 /* Since the root object is a box, too, */
	{
		/* we have to look for it now here and only */
		x += bdlg[0].x*sdlgui_fontwidth;   /* add its absolute coordinates if we need to */
		y += bdlg[0].y*sdlgui_fontheight;
	}
	w = bdlg[objnum].w*sdlgui_fontwidth;
	h = bdlg[objnum].h*sdlgui_fontheight;

	if (bdlg[objnum].state & SG_SELECTED)
	{
		upleftc = colors.darkgrey;
		downrightc = colors.lightgrey;
	}
	else
	{
		upleftc = colors.lightgrey;
		downrightc = colors.darkgrey;
	}

	/* The root box should be bigger than the screen, so we disable the offset there: */
	if (objnum != 0)
		offset = 1;
	else
		offset = 0;

	/* Draw background: */
	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;
	SDL_FillRect(pSdlGuiScrn, &rect, color);

	/* Draw upper border: */
	rect.x = x;
	rect.y = y - offset;
	rect.w = w;
	rect.h = 1;
	SDL_FillRect(pSdlGuiScrn, &rect, upleftc);

	/* Draw left border: */
	rect.x = x - offset;
	rect.y = y;
	rect.w = 1;
	rect.h = h;
	SDL_FillRect(pSdlGuiScrn, &rect, upleftc);

	/* Draw bottom border: */
	rect.x = x;
	rect.y = y + h - 1 + offset;
	rect.w = w;
	rect.h = 1;
	SDL_FillRect(pSdlGuiScrn, &rect, downrightc);

	/* Draw right border: */
	rect.x = x + w - 1 + offset;
	rect.y = y;
	rect.w = 1;
	rect.h = h;
	SDL_FillRect(pSdlGuiScrn, &rect, downrightc);
}


/*-----------------------------------------------------------------------*/
/**
 * Draw a normal button.
 */
static void SDLGui_DrawButton(const SGOBJ *bdlg, int objnum)
{
	int x,y;

	SDLGui_DrawBox(bdlg, objnum);

	x = (bdlg[0].x + bdlg[objnum].x + (bdlg[objnum].w-SDLGui_TextLen(bdlg[objnum].txt))/2) * sdlgui_fontwidth;
	y = (bdlg[0].y + bdlg[objnum].y + (bdlg[objnum].h-1)/2) * sdlgui_fontheight;

	if (bdlg[objnum].state & SG_SELECTED)
	{
		x+=1;
		y+=1;
	}
	SDLGui_TextInt(x, y, bdlg[objnum].txt, true);
}

/*-----------------------------------------------------------------------*/
/**
 * If object is focused, draw a focused background to it
 */
static void SDLGui_DrawFocusBg(const SGOBJ *obj, int x, int y)
{
	SDL_Rect rect;
	Uint32 color;

	if (obj->state & SG_WASFOCUSED)
		color = colors.midgrey;
	else if (obj->state & SG_FOCUSED)
		color = colors.focus;
	else
		return;

	rect.x = x;
	rect.y = y;
	rect.w = obj->w * sdlgui_fontwidth;
	rect.h = obj->h * sdlgui_fontheight;

	SDL_FillRect(pSdlGuiScrn, &rect, color);
}

/*-----------------------------------------------------------------------*/
/**
 * Draw a dialog radio button object.
 */
static void SDLGui_DrawRadioButton(const SGOBJ *rdlg, int objnum)
{
	char str[80];
	int x, y;

	x = (rdlg[0].x + rdlg[objnum].x) * sdlgui_fontwidth;
	y = (rdlg[0].y + rdlg[objnum].y) * sdlgui_fontheight;
	SDLGui_DrawFocusBg(&(rdlg[objnum]), x, y);

	if (rdlg[objnum].state & SG_SELECTED)
		str[0]=SGRADIOBUTTON_SELECTED;
	else
		str[0]=SGRADIOBUTTON_NORMAL;
	str[1]=' ';
	Str_Copy(&str[2], rdlg[objnum].txt, sizeof(str) - 2);

	SDLGui_TextInt(x, y, str, true);
}


/*-----------------------------------------------------------------------*/
/**
 * Draw a dialog check box object.
 */
static void SDLGui_DrawCheckBox(const SGOBJ *cdlg, int objnum)
{
	char str[80];
	int x, y;

	x = (cdlg[0].x + cdlg[objnum].x) * sdlgui_fontwidth;
	y = (cdlg[0].y + cdlg[objnum].y) * sdlgui_fontheight;
	SDLGui_DrawFocusBg(&(cdlg[objnum]), x, y);

	if ( cdlg[objnum].state&SG_SELECTED )
		str[0]=SGCHECKBOX_SELECTED;
	else
		str[0]=SGCHECKBOX_NORMAL;
	str[1]=' ';
	Str_Copy(&str[2], cdlg[objnum].txt, sizeof(str) - 2);

	SDLGui_TextInt(x, y, str, true);
}


/*-----------------------------------------------------------------------*/
/**
 * Draw a scrollbar button.
 */
static void SDLGui_DrawScrollbar(const SGOBJ *bdlg, int objnum)
{
	SDL_Rect rect;
	int x, y, w, h;

	x = bdlg[objnum].x * sdlgui_fontwidth;
	y = bdlg[objnum].y * sdlgui_fontheight + bdlg[objnum].h;

	x += bdlg[0].x*sdlgui_fontwidth;   /* add mainbox absolute coordinates */
	y += bdlg[0].y*sdlgui_fontheight;  /* add mainbox absolute coordinates */

	w = 1 * sdlgui_fontwidth;
	h = bdlg[objnum].w;

	/* Draw background: */
	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;
	SDL_FillRect(pSdlGuiScrn, &rect, colors.midbar);

	/* Draw upper border: */
	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = 1;
	SDL_FillRect(pSdlGuiScrn, &rect, colors.lightbar);

	/* Draw bottom border: */
	rect.x = x;
	rect.y = y + h - 1;
	rect.w = w;
	rect.h = 1;
	SDL_FillRect(pSdlGuiScrn, &rect, colors.darkbar);
}

/*-----------------------------------------------------------------------*/
/**
 *  Draw a dialog popup button object.
 */
static void SDLGui_DrawPopupButton(const SGOBJ *pdlg, int objnum)
{
	int x, y, w;
	const char *downstr = "\x02";

	SDLGui_DrawBox(pdlg, objnum);

	x = (pdlg[0].x + pdlg[objnum].x) * sdlgui_fontwidth;
	y = (pdlg[0].y + pdlg[objnum].y) * sdlgui_fontheight;
	w = pdlg[objnum].w * sdlgui_fontwidth;

	SDLGui_TextInt(x, y, pdlg[objnum].txt, true);
	SDLGui_Text(x+w-sdlgui_fontwidth, y, downstr);
}


/*-----------------------------------------------------------------------*/
/**
 * Let the user insert text into an edit field object.
 * NOTE: The dlg[objnum].txt must point to an an array that is big enough
 * for dlg[objnum].w characters!
 */
static void SDLGui_EditField(SGOBJ *dlg, int objnum)
{
	size_t cursorPos;                   /* Position of the cursor in the edit field */
	int blinkState = 0;                 /* Used for cursor blinking */
	int bStopEditing = false;           /* true if user wants to exit the edit field */
	char *txt;                          /* Shortcut for dlg[objnum].txt */
	SDL_Rect rect;
	SDL_Event event;

	rect.x = (dlg[0].x + dlg[objnum].x) * sdlgui_fontwidth;
	rect.y = (dlg[0].y + dlg[objnum].y) * sdlgui_fontheight;
	rect.w = (dlg[objnum].w + 1) * sdlgui_fontwidth - 1;
	rect.h = dlg[objnum].h * sdlgui_fontheight;

	SDL_SetTextInputRect(&rect);
	SDL_StartTextInput();

	txt = dlg[objnum].txt;
	cursorPos = strlen(txt);

	do
	{
		/* Look for events */
		if (SDL_PollEvent(&event) == 0)
		{
			/* No event: Wait some time for cursor blinking */
			SDL_Delay(250);
			blinkState ^= 1;
		}
		else
		{
			/* Handle events */
			do
			{
				switch (event.type)
				{
				 case SDL_QUIT:                     /* User wants to quit */
					bQuitProgram = true;
					bStopEditing = true;
					break;
				 case SDL_MOUSEBUTTONDOWN:          /* Mouse pressed -> stop editing */
					bStopEditing = true;
					break;
				 case SDL_TEXTINPUT:
					if (strlen(txt) < (size_t)dlg[objnum].w)
					{
						memmove(&txt[cursorPos+1], &txt[cursorPos],
						        strlen(&txt[cursorPos])+1);
 						txt[cursorPos] = event.text.text[0];
						cursorPos += 1;
					}
					break;
				 case SDL_KEYDOWN:                  /* Key pressed */
					switch (event.key.keysym.sym)
					{
					 case SDLK_RETURN:
					 case SDLK_KP_ENTER:
						bStopEditing = true;
						break;
					 case SDLK_LEFT:
						if (cursorPos > 0)
							cursorPos -= 1;
						break;
					 case SDLK_RIGHT:
						if (cursorPos < strlen(txt))
							cursorPos += 1;
						break;
					 case SDLK_BACKSPACE:
						if (cursorPos > 0)
						{
							memmove(&txt[cursorPos-1], &txt[cursorPos], strlen(&txt[cursorPos])+1);
							cursorPos -= 1;
						}
						break;
					 case SDLK_DELETE:
						if (cursorPos < strlen(txt))
							memmove(&txt[cursorPos], &txt[cursorPos+1], strlen(&txt[cursorPos+1])+1);
						break;
					 default:
						break;
					}
					break;
				}
			}
			while (SDL_PollEvent(&event));

			blinkState = 1;
		}

		/* Redraw the text field: */
		SDL_FillRect(pSdlGuiScrn, &rect, colors.midgrey);  /* Draw background */
		/* Draw the cursor: */
		if (blinkState && !bStopEditing)
		{
			SDL_Rect cursorrect;
			cursorrect.x = rect.x + cursorPos * sdlgui_fontwidth;
			cursorrect.y = rect.y;
			cursorrect.w = sdlgui_fontwidth;
			cursorrect.h = rect.h;
			SDL_FillRect(pSdlGuiScrn, &cursorrect, colors.cursor);
		}
		SDLGui_Text(rect.x, rect.y, dlg[objnum].txt);  /* Draw text */
		Screen_UpdateRects(pSdlGuiScrn, 1, &rect);
	}
	while (!bStopEditing);

	SDL_StopTextInput();
}


/*-----------------------------------------------------------------------*/
/**
 * Draw single object based on its type
 */
static void SDLGui_DrawObj(const SGOBJ *dlg, int i)
{
	switch (dlg[i].type)
	{
	case SGBOX:
		SDLGui_DrawBox(dlg, i);
		break;
	case SGTEXT:
		SDLGui_DrawText(dlg, i);
		break;
	case SGEDITFIELD:
		SDLGui_DrawEditField(dlg, i);
		break;
	case SGBUTTON:
		SDLGui_DrawButton(dlg, i);
		break;
	case SGRADIOBUT:
		SDLGui_DrawRadioButton(dlg, i);
		break;
	case SGCHECKBOX:
		SDLGui_DrawCheckBox(dlg, i);
		break;
	case SGPOPUP:
		SDLGui_DrawPopupButton(dlg, i);
		break;
	case SGSCROLLBAR:
		SDLGui_DrawScrollbar(dlg, i);
		break;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Draw a whole dialog.
 */
void SDLGui_DrawDialog(const SGOBJ *dlg)
{
	int i;

	for (i = 0; dlg[i].type != SGSTOP; i++)
	{
		SDLGui_DrawObj(dlg, i);
	}
	Screen_UpdateRect(pSdlGuiScrn, 0,0,0,0);
}


/*-----------------------------------------------------------------------*/
/**
 * Search an object at a certain position.
 * If found, return its index, otherwise SDLGUI_NOTFOUND.
 */
static int SDLGui_FindObj(const SGOBJ *dlg, int fx, int fy)
{
	int len, i;
	int ob = SDLGUI_NOTFOUND;
	int xpos, ypos;

	len = 0;
	while (dlg[len].type != SGSTOP)
		len++;

	xpos = fx / sdlgui_fontwidth;
	ypos = fy / sdlgui_fontheight;

	/* Now search for the object.
	 * Searching is done from end to start,
	 * as later objects cover earlier ones
	 */
	for (i = len; i >= 0; i--)
	{
		/* clicked on a scrollbar ? */
		if (dlg[i].type == SGSCROLLBAR) {
			if (xpos >= dlg[0].x+dlg[i].x && xpos < dlg[0].x+dlg[i].x+1) {
				ypos = dlg[i].y * sdlgui_fontheight + dlg[i].h + dlg[0].y * sdlgui_fontheight;
				if (fy >= ypos && fy < ypos + dlg[i].w) {
					ob = i;
					break;
				}
			}
		}
		/* clicked on another object ? */
		else if (xpos >= dlg[0].x+dlg[i].x && ypos >= dlg[0].y+dlg[i].y
		    && xpos < dlg[0].x+dlg[i].x+dlg[i].w && ypos < dlg[0].y+dlg[i].y+dlg[i].h)
		{
			ob = i;
			break;
		}
	}

	return ob;
}


/*-----------------------------------------------------------------------*/
/**
 * Search an object with a special flag (e.g. SG_DEFAULT or SG_CANCEL).
 * If found, return its index, otherwise SDLGUI_NOTFOUND.
 */
static int SDLGui_SearchFlags(const SGOBJ *dlg, int flag)
{
	int i = 0;

	while (dlg[i].type != SGSTOP)
	{
		if (dlg[i].flags & flag)
			return i;
		i++;
	}
	return SDLGUI_NOTFOUND;
}

/*-----------------------------------------------------------------------*/
/**
 * Search an object with a special state (e.g. SG_FOCUSED).
 * If found, return its index, otherwise SDLGUI_NOTFOUND.
 */
static int SDLGui_SearchState(const SGOBJ *dlg, int state)
{
	int i = 0;

	while (dlg[i].type != SGSTOP)
	{
		if (dlg[i].state & state)
			return i;
		i++;
	}
	return SDLGUI_NOTFOUND;
}

/*-----------------------------------------------------------------------*/
/**
 * Print dialog object flags & state for debug purposes.
 */
static void SDLGui_DebugPrintDialog(const SGOBJ *dlg)
{
#if DEBUG_INFO
	int i;
	printf("obj: flags | state\n");
	for (i = 0; dlg[i].type != SGSTOP; i++)
		printf("%3d:  0x%02x | 0x%02x\n", i, dlg[i].flags, dlg[i].state);
#endif
}

/*-----------------------------------------------------------------------*/
/**
 * For given dialog object type, returns whether it could have shortcut key
 */
static bool SDLGui_CanHaveShortcut(int kind)
{
	if (kind == SGBUTTON || kind == SGRADIOBUT || kind == SGCHECKBOX)
		return true;
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * Check & set dialog item shortcut values based on their text strings.
 * Asserts if dialog has same shortcut defined multiple times.
 */
static void SDLGui_SetShortcuts(SGOBJ *dlg)
{
	unsigned chr, used[256];
	const char *str;
	unsigned int i;

	memset(used, 0, sizeof(used));
	for (i = 0; dlg[i].type != SGSTOP; i++)
	{
		if (!SDLGui_CanHaveShortcut(dlg[i].type))
			continue;
		if (!(str = dlg[i].txt))
			continue;
		while(*str)
		{
			if (*str++ == UNDERLINE_INDICATOR)
			{
				/* TODO: conversion */
				chr = toupper(*str);
				dlg[i].shortcut = chr;
				if (used[chr])
				{
					fprintf(stderr, "ERROR: Duplicate Hatari SDL GUI shortcut in '%s'!\n", dlg[i].txt);
					exit(1);
				}
				used[chr] = 1;
			}
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Unfocus given button
 */
static void SDLGui_RemoveFocus(SGOBJ *dlg, int old)
{
	if (old == SDLGUI_NOTFOUND)
		return;
	dlg[old].state &= ~SG_FOCUSED;
	dlg[old].state |= SG_WASFOCUSED;
	SDLGui_DrawObj(dlg, old);
	dlg[old].state ^= SG_WASFOCUSED;
}

/*-----------------------------------------------------------------------*/
/**
 * Search next button to focus, and focus it.
 * If found, return its index, otherwise given starting index.
 */
static int SDLGui_FocusNext(SGOBJ *dlg, int i, int inc)
{
	int old = i;
	if (i == SDLGUI_NOTFOUND)
		return i;

	for (;;)
	{
		i += inc;

		/* wrap */
		if (dlg[i].type == SGSTOP)
		{
			assert(inc > 0);
			i = 0;
		}
		else if (i == 0)
		{
			assert(inc < 0);
			while (dlg[i].type != SGSTOP)
				i++;
			i--;
		}
		/* change focus for items that can have shortcuts
		 * and for items in Fsel lists
		 */
		if (SDLGui_CanHaveShortcut(dlg[i].type) || (dlg[i].flags & SG_EXIT) != 0)
		{
			dlg[i].state |= SG_FOCUSED;
			SDLGui_DrawObj(dlg, i);
			Screen_UpdateRect(pSdlGuiScrn, 0,0,0,0);
			return i;
		}
		/* wrapped around without even initial one matching */
		if (i == old)
			return 0;
	}
	return old;
}


/*-----------------------------------------------------------------------*/
/**
 * Handle button selection, either with mouse or keyboard.
 * If handled, return its index, otherwise SDLGUI_NOTFOUND.
 */
static int SDLGui_HandleSelection(SGOBJ *dlg, int obj, int oldbutton)
{
	SDL_Rect rct;
	int i, retbutton = SDLGUI_NOTFOUND;

	switch (dlg[obj].type)
	{
	case SGBUTTON:
		if (oldbutton==obj)
			retbutton=obj;
		break;
	case SGSCROLLBAR:
		dlg[obj].state &= ~SG_MOUSEDOWN;

		if (oldbutton==obj)
			retbutton=obj;
		break;
	case SGEDITFIELD:
		SDLGui_EditField(dlg, obj);
		break;
	case SGRADIOBUT:
		for (i = obj-1; i > 0 && dlg[i].type == SGRADIOBUT; i--)
		{
			dlg[i].state &= ~SG_SELECTED;  /* Deselect all radio buttons in this group */
			rct.x = (dlg[0].x+dlg[i].x)*sdlgui_fontwidth;
			rct.y = (dlg[0].y+dlg[i].y)*sdlgui_fontheight;
			rct.w = sdlgui_fontwidth;
			rct.h = sdlgui_fontheight;
			SDL_FillRect(pSdlGuiScrn, &rct, colors.midgrey); /* Clear old */
			SDLGui_DrawRadioButton(dlg, i);
			Screen_UpdateRects(pSdlGuiScrn, 1, &rct);
		}
		for (i = obj+1; dlg[i].type == SGRADIOBUT; i++)
		{
			dlg[i].state &= ~SG_SELECTED;  /* Deselect all radio buttons in this group */
			rct.x = (dlg[0].x+dlg[i].x)*sdlgui_fontwidth;
			rct.y = (dlg[0].y+dlg[i].y)*sdlgui_fontheight;
			rct.w = sdlgui_fontwidth;
			rct.h = sdlgui_fontheight;
			SDL_FillRect(pSdlGuiScrn, &rct, colors.midgrey); /* Clear old */
			SDLGui_DrawRadioButton(dlg, i);
			Screen_UpdateRects(pSdlGuiScrn, 1, &rct);
		}
		dlg[obj].state |= SG_SELECTED;  /* Select this radio button */
		rct.x = (dlg[0].x+dlg[obj].x)*sdlgui_fontwidth;
		rct.y = (dlg[0].y+dlg[obj].y)*sdlgui_fontheight;
		rct.w = sdlgui_fontwidth;
		rct.h = sdlgui_fontheight;
		SDL_FillRect(pSdlGuiScrn, &rct, colors.midgrey); /* Clear old */
		SDLGui_DrawRadioButton(dlg, obj);
		Screen_UpdateRects(pSdlGuiScrn, 1, &rct);
		break;
	case SGCHECKBOX:
		dlg[obj].state ^= SG_SELECTED;
		rct.x = (dlg[0].x+dlg[obj].x)*sdlgui_fontwidth;
		rct.y = (dlg[0].y+dlg[obj].y)*sdlgui_fontheight;
		rct.w = sdlgui_fontwidth;
		rct.h = sdlgui_fontheight;
		SDL_FillRect(pSdlGuiScrn, &rct, colors.midgrey); /* Clear old */
		SDLGui_DrawCheckBox(dlg, obj);
		Screen_UpdateRects(pSdlGuiScrn, 1, &rct);
		break;
	case SGPOPUP:
		dlg[obj].state |= SG_SELECTED;
		SDLGui_DrawPopupButton(dlg, obj);
		Screen_UpdateRect(pSdlGuiScrn,
			       (dlg[0].x+dlg[obj].x)*sdlgui_fontwidth-2,
			       (dlg[0].y+dlg[obj].y)*sdlgui_fontheight-2,
			       dlg[obj].w*sdlgui_fontwidth+4,
			       dlg[obj].h*sdlgui_fontheight+4);
		retbutton=obj;
		break;
	}

	if (retbutton == SDLGUI_NOTFOUND && (dlg[obj].flags & SG_EXIT) != 0)
	{
		retbutton = obj;
	}

	return retbutton;
}


/*-----------------------------------------------------------------------*/
/**
 * If object with given shortcut is found, handle that.
 * If handled, return its index, otherwise SDLGUI_NOTFOUND.
 */
static int SDLGui_HandleShortcut(SGOBJ *dlg, int key)
{
	int i = 0;
	while (dlg[i].type != SGSTOP)
	{
		if (dlg[i].shortcut == key)
			return SDLGui_HandleSelection(dlg, i, i);
		i++;
	}
	return SDLGUI_NOTFOUND;
}

/**
 * Scale mouse state coordinates in case we've got a re-sized SDL2 window
 *
 * NOTE: while scaling done here fixes SDL2 reported mouse coords to
 * match Hatari framebuffer coords in scaled SDL2 windows, there's
 * another issue with (mouse _state_) coords in _fullscreen_.
 *
 * SDL2 deducts fullscreen letterboxing borders from those coords,
 * but not from the values returns by SDL2 window size functions
 * (and there's no function providing the letterbox border size).
 *
 * Atari resolutions are more narrow than today's widescreen monitor
 * resolutions, so typically fullscreen letterboxing borders are on
 * the sides => y-coord gets scaled OK, x-coord will be too small.
 */
void SDLGui_ScaleMouseStateCoordinates(int *x, int *y)
{
	int win_width, win_height;
	SDL_GetWindowSize(sdlWindow, &win_width, &win_height);

	*x = *x * pSdlGuiScrn->w / win_width;
	*y = *y * pSdlGuiScrn->h / win_height;
}

/**
 * Scale mouse event coordinates in case we've got a re-sized SDL2 window
 */
static void SDLGui_ScaleMouseButtonCoordinates(SDL_MouseButtonEvent *bev)
{
	if (bInFullScreen)
		return;

	int x = bev->x, y = bev->y;
	SDLGui_ScaleMouseStateCoordinates(&x, &y);
	bev->x = x; bev->y = y;
}

/*-----------------------------------------------------------------------*/
/**
 * Show and process a dialog.
 *
 * Dialogs using a scrollbar, or other objects with SG_REPEAT flag,
 * must return the previous return value in 'current_object' arg, as
 * the same dialog is displayed in a loop to handle scrolling. Other
 * dialogs should give zero as 'current_object' (ie no object
 * selected at start when displaying the dialog)
 *
 * Returns either:
 * - index of the GUI item that was invoked
 * - SDLGUI_QUIT if user wants to close Hatari
 * - SDLGUI_ERROR if unable to show dialog
 * - for events not handled here, 'isEventOut' callback is checked
 *   for whether caller is interested about given event type:
 *   => event is stored to pEventOut and SDLGUI_UNKNOWNEVENT returned
 * GUI item indices are positive, other return values are negative
 */
int SDLGui_DoDialogExt(SGOBJ *dlg, bool (*isEventOut)(SDL_EventType), SDL_Event *pEventOut, int current_object)
{
	int oldbutton = SDLGUI_NOTFOUND;
	int retbutton = SDLGUI_NOTFOUND;
	int b, x, y, value, obj;
	SDL_Keycode key;
	int focused;
	SDL_Event sdlEvent;
	SDL_Surface *pBgSurface;
	SDL_Rect dlgrect, bgrect;
	SDL_Joystick *joy = NULL;

	/* either both, or neither of these should be present */
	assert((isEventOut && pEventOut) || (!isEventOut && !pEventOut));

	if (pSdlGuiScrn->h / sdlgui_fontheight < dlg[0].h)
	{
		Log_Printf(LOG_ERROR, "Screen size too small for dialog!\n");
		return SDLGUI_ERROR;
	}

	dlgrect.x = dlg[0].x * sdlgui_fontwidth;
	dlgrect.y = dlg[0].y * sdlgui_fontheight;
	dlgrect.w = dlg[0].w * sdlgui_fontwidth;
	dlgrect.h = dlg[0].h * sdlgui_fontheight;

	bgrect.x = bgrect.y = 0;
	bgrect.w = dlgrect.w;
	bgrect.h = dlgrect.h;

	/* Save background */
	pBgSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, dlgrect.w, dlgrect.h, pSdlGuiScrn->format->BitsPerPixel,
	                                  pSdlGuiScrn->format->Rmask, pSdlGuiScrn->format->Gmask, pSdlGuiScrn->format->Bmask, pSdlGuiScrn->format->Amask);
	if (pSdlGuiScrn->format->palette != NULL)
	{
		SDL_SetPaletteColors(pBgSurface->format->palette, pSdlGuiScrn->format->palette->colors, 0, pSdlGuiScrn->format->palette->ncolors-1);
	}

	if (pBgSurface != NULL)
	{
		SDL_BlitSurface(pSdlGuiScrn,  &dlgrect, pBgSurface, &bgrect);
	}
	else
	{
		Log_Printf(LOG_ERROR, "SDLGUI_DoDialog: CreateRGBSurface failed: %s\n", SDL_GetError());
	}
	SDLGui_DebugPrintDialog(dlg);

	/* focus default button if nothing else is focused */
	focused = SDLGui_SearchState(dlg, SG_FOCUSED);
	if (focused == SDLGUI_NOTFOUND)
	{
		int defbutton = SDLGui_SearchFlags(dlg, SG_DEFAULT);
		if (defbutton != SDLGUI_NOTFOUND)
		{
			dlg[defbutton].state |= SG_FOCUSED;
			focused = defbutton;
		}
	}
	Dprintf(("focused: %d\n", focused));
	SDLGui_SetShortcuts(dlg);

	/* (Re-)draw the dialog */
	SDLGui_DrawDialog(dlg);

	/* Is the left mouse button still pressed? Yes -> Handle TOUCHEXIT objects here */
	SDL_PumpEvents();
	b = SDL_GetMouseState(&x, &y);

	/* Report repeat objects until mouse button is released,
	 * regardless of mouse position.  Used for scrollbar
	 * object interactions */
	if (current_object >= 0 && (dlg[current_object].flags & SG_REPEAT)) {
		obj = current_object;
		oldbutton = obj;
		if (b & SDL_BUTTON(1))
		{
			retbutton = obj;
			dlg[obj].state |= SG_MOUSEDOWN;
		}
		else
		{
			dlg[obj].state &= ~SG_MOUSEDOWN;
		}
	} else {
		SDLGui_ScaleMouseStateCoordinates(&x, &y);
		obj = SDLGui_FindObj(dlg, x, y);

		if (obj != SDLGUI_NOTFOUND && (dlg[obj].flags&SG_TOUCHEXIT) )
		{
			oldbutton = obj;
			if (b & SDL_BUTTON(1))
			{
				retbutton = obj;
				dlg[obj].state |= SG_SELECTED;
			}
		}
	}

	if (SDL_NumJoysticks() > 0)
		joy = SDL_JoystickOpen(0);

	Dprintf(("ENTER - obj: %d, old: %d, ret: %d\n", obj, oldbutton, retbutton));

	/* The main loop */
	while (retbutton == SDLGUI_NOTFOUND && !bQuitProgram)
	{
		if (SDL_WaitEvent(&sdlEvent) == 1)  /* Wait for events */
		{
			switch (sdlEvent.type)
			{
			 case SDL_QUIT:
				retbutton = SDLGUI_QUIT;
				break;

			 case SDL_MOUSEBUTTONDOWN:
				if (sdlEvent.button.button != SDL_BUTTON_LEFT)
				{
					/* Not left mouse button -> unsupported event */
					retbutton = SDLGUI_UNKNOWNEVENT;
					break;
				}
				/* It was the left button: Find the object under the mouse cursor */
				SDLGui_ScaleMouseButtonCoordinates(&sdlEvent.button);
				obj = SDLGui_FindObj(dlg, sdlEvent.button.x, sdlEvent.button.y);
				if (obj != SDLGUI_NOTFOUND)
				{
					if (dlg[obj].type==SGBUTTON)
					{
						dlg[obj].state |= SG_SELECTED;
						SDLGui_DrawButton(dlg, obj);
						Screen_UpdateRect(pSdlGuiScrn, (dlg[0].x+dlg[obj].x)*sdlgui_fontwidth-2, (dlg[0].y+dlg[obj].y)*sdlgui_fontheight-2,
						               dlg[obj].w*sdlgui_fontwidth+4, dlg[obj].h*sdlgui_fontheight+4);
						oldbutton=obj;
					}
					if (dlg[obj].type==SGSCROLLBAR)
					{
						dlg[obj].state |= SG_MOUSEDOWN;
						oldbutton=obj;
					}
					if ( dlg[obj].flags&SG_TOUCHEXIT )
					{
						dlg[obj].state |= SG_SELECTED;
						retbutton = obj;
					}
				}
				break;

			 case SDL_MOUSEBUTTONUP:
				if (sdlEvent.button.button != SDL_BUTTON_LEFT)
				{
					/* Not left mouse button -> unsupported event */
					retbutton = SDLGUI_UNKNOWNEVENT;
					break;
				}
				/* It was the left button: Find the object under the mouse cursor */
				SDLGui_ScaleMouseButtonCoordinates(&sdlEvent.button);
				obj = SDLGui_FindObj(dlg, sdlEvent.button.x, sdlEvent.button.y);
				if (obj != SDLGUI_NOTFOUND)
				{
					retbutton = SDLGui_HandleSelection(dlg, obj, oldbutton);
				}
				if (oldbutton != SDLGUI_NOTFOUND && dlg[oldbutton].type == SGBUTTON)
				{
					dlg[oldbutton].state &= ~SG_SELECTED;
					SDLGui_DrawButton(dlg, oldbutton);
					Screen_UpdateRect(pSdlGuiScrn, (dlg[0].x+dlg[oldbutton].x)*sdlgui_fontwidth-2, (dlg[0].y+dlg[oldbutton].y)*sdlgui_fontheight-2,
					               dlg[oldbutton].w*sdlgui_fontwidth+4, dlg[oldbutton].h*sdlgui_fontheight+4);
					oldbutton = SDLGUI_NOTFOUND;
				}
				break;

			 case SDL_JOYAXISMOTION:
				value = sdlEvent.jaxis.value;
				if (value < -3200 || value > 3200)
				{
					if(sdlEvent.jaxis.axis == 0)
					{
						/* Left-right movement */
						if (value < 0)
							retbutton = SDLGui_HandleShortcut(dlg, SG_SHORTCUT_LEFT);
						else
							retbutton = SDLGui_HandleShortcut(dlg, SG_SHORTCUT_RIGHT);
					}
					else if(sdlEvent.jaxis.axis == 1)
					{
						/* Up-Down movement */
						if (value < 0)
						{
							SDLGui_RemoveFocus(dlg, focused);
							focused = SDLGui_FocusNext(dlg, focused, -1);
						}
						else
						{
							SDLGui_RemoveFocus(dlg, focused);
							focused = SDLGui_FocusNext(dlg, focused, +1);
						}
					}
				}
				break;

			 case SDL_JOYHATMOTION:
				if (sdlEvent.jhat.value & SDL_HAT_LEFT)
					retbutton = SDLGui_HandleShortcut(dlg, SG_SHORTCUT_LEFT);
				else if (sdlEvent.jhat.value & SDL_HAT_RIGHT)
					retbutton = SDLGui_HandleShortcut(dlg, SG_SHORTCUT_RIGHT);
				if (sdlEvent.jhat.value & SDL_HAT_UP)
				{
					SDLGui_RemoveFocus(dlg, focused);
					focused = SDLGui_FocusNext(dlg, focused, -1);
				}
				else if (sdlEvent.jhat.value & SDL_HAT_DOWN)
				{
					SDLGui_RemoveFocus(dlg, focused);
					focused = SDLGui_FocusNext(dlg, focused, +1);
				}
				break;

			 case SDL_JOYBUTTONDOWN:
				retbutton = SDLGui_HandleSelection(dlg, focused, focused);
				break;

			 case SDL_JOYBALLMOTION:
			 case SDL_MOUSEMOTION:
				break;

			 case SDL_KEYDOWN:                     /* Key pressed */
				/* keys that need to support repeat,
				 * need to be checked on press
				 */
				key = sdlEvent.key.keysym.sym;
				/* keyboard shortcuts are with modifiers */
				if (sdlEvent.key.keysym.mod & KMOD_LALT
				    || sdlEvent.key.keysym.mod & KMOD_RALT)
				{
					if (key == SDLK_LEFT)
						retbutton = SDLGui_HandleShortcut(dlg, SG_SHORTCUT_LEFT);
					else if (key == SDLK_RIGHT)
						retbutton = SDLGui_HandleShortcut(dlg, SG_SHORTCUT_RIGHT);
					else if (key == SDLK_UP)
						retbutton = SDLGui_HandleShortcut(dlg, SG_SHORTCUT_UP);
					else if (key == SDLK_DOWN)
						retbutton = SDLGui_HandleShortcut(dlg, SG_SHORTCUT_DOWN);
					else
					{
						if (key >= 33 && key <= 126)
							retbutton = SDLGui_HandleShortcut(dlg, toupper(key));
					}
					if (!retbutton)
						retbutton = SDLGUI_UNKNOWNEVENT;
					break;
				}
				switch (key)
				{
				 case SDLK_UP:
				 case SDLK_LEFT:
					SDLGui_RemoveFocus(dlg, focused);
					focused = SDLGui_FocusNext(dlg, focused, -1);
					break;
				 case SDLK_TAB:
				 case SDLK_DOWN:
				 case SDLK_RIGHT:
					SDLGui_RemoveFocus(dlg, focused);
					focused = SDLGui_FocusNext(dlg, focused, +1);
					break;
				 case SDLK_HOME:
					SDLGui_RemoveFocus(dlg, focused);
					focused = SDLGui_FocusNext(dlg, 1, +1);
					break;
				 case SDLK_END:
					SDLGui_RemoveFocus(dlg, focused);
					focused = SDLGui_FocusNext(dlg, 1, -1);
					break;
				 default:
					retbutton = SDLGUI_UNKNOWNEVENT;
					break;
				}
				break;

			case SDL_KEYUP:                     /* Key released */
				/* keys potentially exiting dialog need
				 * to be handed only on release, to avoid
				 * leaking release events to emulation
				 */
				switch (sdlEvent.key.keysym.sym)
				{
				 case SDLK_SPACE:
				 case SDLK_RETURN:
				 case SDLK_KP_ENTER:
					retbutton = SDLGui_HandleSelection(dlg, focused, focused);
					break;
				 case SDLK_ESCAPE:
					retbutton = SDLGui_SearchFlags(dlg, SG_CANCEL);
					break;
				 default:
					retbutton = SDLGUI_UNKNOWNEVENT;
					break;
				}
				break;

			 case SDL_WINDOWEVENT:
				if (sdlEvent.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
				    || sdlEvent.window.event == SDL_WINDOWEVENT_RESTORED
				    || sdlEvent.window.event == SDL_WINDOWEVENT_EXPOSED)
				{
					Screen_UpdateRect(pSdlGuiScrn, 0, 0, 0, 0);
				}
				break;

			 default:
				retbutton = SDLGUI_UNKNOWNEVENT;
				break;
			}
			/* continue if unknown events were not not requested
			 * specifically for this event type
			 */
			if (retbutton == SDLGUI_UNKNOWNEVENT &&
			    !(isEventOut && isEventOut(sdlEvent.type)))
			    retbutton = SDLGUI_NOTFOUND;
		}
	}

	/* Copy event data of unsupported events if caller wants to have it */
	if (retbutton == SDLGUI_UNKNOWNEVENT)
	{
		memcpy(pEventOut, &sdlEvent, sizeof(SDL_Event));
	}

	/* Restore background */
	if (pBgSurface)
	{
		SDL_BlitSurface(pBgSurface, &bgrect, pSdlGuiScrn,  &dlgrect);
		SDL_FreeSurface(pBgSurface);
	}

	if (retbutton == SDLGUI_QUIT)
		bQuitProgram = true;

	if (joy)
		SDL_JoystickClose(joy);

	Dprintf(("EXIT - ret: %d\n", retbutton));
	return retbutton;
}

/*-----------------------------------------------------------------------*/
/**
 * Show and process a dialog. Returns either:
 * - index of the GUI item that was invoked
 * - SDLGUI_QUIT if user wants to close Hatari
 * - SDLGUI_ERROR if unable to show dialog
 * GUI item indices are positive, other return values are negative
 */
int SDLGui_DoDialog(SGOBJ *dlg)
{
	return SDLGui_DoDialogExt(dlg, NULL, NULL, 0);
}
