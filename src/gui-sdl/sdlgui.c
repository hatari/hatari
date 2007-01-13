/*
  Hatari - sdlgui.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  A tiny graphical user interface for Hatari.
*/
const char SDLGui_rcsid[] = "Hatari $Id: sdlgui.c,v 1.15 2007-01-13 11:57:41 thothy Exp $";

#include <SDL.h>
#include <ctype.h>
#include <string.h>

#include "main.h"
#include "sdlgui.h"

#include "font5x8.h"
#include "font10x16.h"


static SDL_Surface *pSdlGuiScrn;            /* Pointer to the actual main SDL screen surface */
static SDL_Surface *pSmallFontGfx = NULL;   /* The small font graphics */
static SDL_Surface *pBigFontGfx = NULL;     /* The big font graphics */
static SDL_Surface *pFontGfx = NULL;        /* The actual font graphics */
static int fontwidth, fontheight;           /* Width & height of the actual font */



/*-----------------------------------------------------------------------*/
/*
  Load an 1 plane XBM into a 8 planes SDL_Surface.
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
    fprintf(stderr, "Couldn't allocate bitmap: %s", SDL_GetError());
    return(NULL);
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

  return(bitmap);
}


/*-----------------------------------------------------------------------*/
/*
  Initialize the GUI.
*/
int SDLGui_Init(void)
{
  SDL_Color blackWhiteColors[2] = {{255, 255, 255, 0}, {0, 0, 0, 0}};

  /* Initialize the font graphics: */
  pSmallFontGfx = SDLGui_LoadXBM(font5x8_width, font5x8_height, font5x8_bits);
  pBigFontGfx = SDLGui_LoadXBM(font10x16_width, font10x16_height, font10x16_bits);
  if (pSmallFontGfx == NULL || pBigFontGfx == NULL)
  {
    fprintf(stderr, "Error: Can not init font graphics!\n");
    return -1;
  }

  /* Set color palette of the font graphics: */
  SDL_SetColors(pSmallFontGfx, blackWhiteColors, 0, 2);
  SDL_SetColors(pBigFontGfx, blackWhiteColors, 0, 2);

  /* Set font color 0 as transparent: */
  SDL_SetColorKey(pSmallFontGfx, (SDL_SRCCOLORKEY|SDL_RLEACCEL), 0);
  SDL_SetColorKey(pBigFontGfx, (SDL_SRCCOLORKEY|SDL_RLEACCEL), 0);

  return 0;
}


/*-----------------------------------------------------------------------*/
/*
  Uninitialize the GUI.
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
/*
  Inform the SDL-GUI about the actual SDL_Surface screen pointer and
  prepare the font to suit the actual resolution.
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
    fprintf(stderr, "Error: A problem with the font occured!\n");
    return -1;
  }

  /* Get the font width and height: */
  fontwidth = pFontGfx->w/16;
  fontheight = pFontGfx->h/16;

  return 0;
}


/*-----------------------------------------------------------------------*/
/*
  Center a dialog so that it appears in the middle of the screen.
  Note: We only store the coordinates in the root box of the dialog,
  all other objects in the dialog are positioned relatively to this one.
*/
void SDLGui_CenterDlg(SGOBJ *dlg)
{
  dlg[0].x = (pSdlGuiScrn->w/fontwidth-dlg[0].w)/2;
  dlg[0].y = (pSdlGuiScrn->h/fontheight-dlg[0].h)/2;
}


/*-----------------------------------------------------------------------*/
/*
  Draw a text string.
*/
static void SDLGui_Text(int x, int y, const char *txt)
{
  int i;
  char c;
  SDL_Rect sr, dr;

  for(i=0; txt[i]!=0; i++)
  {
    c = txt[i];
    sr.x=fontwidth*(c%16);  sr.y=fontheight*(c/16);
    sr.w=fontwidth;         sr.h=fontheight;
    dr.x=x+i*fontwidth;     dr.y=y;
    dr.w=fontwidth;         dr.h=fontheight;
    SDL_BlitSurface(pFontGfx, &sr, pSdlGuiScrn, &dr);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog text object.
*/
static void SDLGui_DrawText(const SGOBJ *tdlg, int objnum)
{
  int x, y;
  x = (tdlg[0].x+tdlg[objnum].x)*fontwidth;
  y = (tdlg[0].y+tdlg[objnum].y)*fontheight;
  SDLGui_Text(x, y, tdlg[objnum].txt);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a edit field object.
*/
static void SDLGui_DrawEditField(const SGOBJ *edlg, int objnum)
{
  int x, y;
  SDL_Rect rect;

  x = (edlg[0].x+edlg[objnum].x)*fontwidth;
  y = (edlg[0].y+edlg[objnum].y)*fontheight;
  SDLGui_Text(x, y, edlg[objnum].txt);

  rect.x = x;    rect.y = y + edlg[objnum].h * fontheight;
  rect.w = edlg[objnum].w * fontwidth;    rect.h = 1;
  SDL_FillRect(pSdlGuiScrn, &rect, SDL_MapRGB(pSdlGuiScrn->format,160,160,160));
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog box object.
*/
static void SDLGui_DrawBox(const SGOBJ *bdlg, int objnum)
{
  SDL_Rect rect;
  int x, y, w, h, offset;
  Uint32 grey = SDL_MapRGB(pSdlGuiScrn->format,192,192,192);
  Uint32 upleftc, downrightc;

  x = bdlg[objnum].x*fontwidth;
  y = bdlg[objnum].y*fontheight;
  if(objnum>0)                    /* Since the root object is a box, too, */
  {                               /* we have to look for it now here and only */
    x += bdlg[0].x*fontwidth;     /* add its absolute coordinates if we need to */
    y += bdlg[0].y*fontheight;
  }
  w = bdlg[objnum].w*fontwidth;
  h = bdlg[objnum].h*fontheight;

  if( bdlg[objnum].state&SG_SELECTED )
  {
    upleftc = SDL_MapRGB(pSdlGuiScrn->format,128,128,128);
    downrightc = SDL_MapRGB(pSdlGuiScrn->format,255,255,255);
  }
  else
  {
    upleftc = SDL_MapRGB(pSdlGuiScrn->format,255,255,255);
    downrightc = SDL_MapRGB(pSdlGuiScrn->format,128,128,128);
  }

  /* The root box should be bigger than the screen, so we disable the offset there: */
  if (objnum != 0)
    offset = 1;
  else
    offset = 0;

  /* Draw background: */
  rect.x = x;  rect.y = y;
  rect.w = w;  rect.h = h;
  SDL_FillRect(pSdlGuiScrn, &rect, grey);

  /* Draw upper border: */
  rect.x = x;  rect.y = y - offset;
  rect.w = w;  rect.h = 1;
  SDL_FillRect(pSdlGuiScrn, &rect, upleftc);

  /* Draw left border: */
  rect.x = x - offset;  rect.y = y;
  rect.w = 1;  rect.h = h;
  SDL_FillRect(pSdlGuiScrn, &rect, upleftc);

  /* Draw bottom border: */
  rect.x = x;  rect.y = y + h - 1 + offset;
  rect.w = w;  rect.h = 1;
  SDL_FillRect(pSdlGuiScrn, &rect, downrightc);

  /* Draw right border: */
  rect.x = x + w - 1 + offset;  rect.y = y;
  rect.w = 1;  rect.h = h;
  SDL_FillRect(pSdlGuiScrn, &rect, downrightc);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a normal button.
*/
static void SDLGui_DrawButton(const SGOBJ *bdlg, int objnum)
{
  int x,y;

  SDLGui_DrawBox(bdlg, objnum);

  x = (bdlg[0].x+bdlg[objnum].x+(bdlg[objnum].w-strlen(bdlg[objnum].txt))/2)*fontwidth;
  y = (bdlg[0].y+bdlg[objnum].y+(bdlg[objnum].h-1)/2)*fontheight;

  if( bdlg[objnum].state&SG_SELECTED )
  {
    x+=1;
    y+=1;
  }
  SDLGui_Text(x, y, bdlg[objnum].txt);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog radio button object.
*/
static void SDLGui_DrawRadioButton(const SGOBJ *rdlg, int objnum)
{
  char str[80];
  int x, y;

  x = (rdlg[0].x+rdlg[objnum].x)*fontwidth;
  y = (rdlg[0].y+rdlg[objnum].y)*fontheight;

  if( rdlg[objnum].state&SG_SELECTED )
    str[0]=SGRADIOBUTTON_SELECTED;
   else
    str[0]=SGRADIOBUTTON_NORMAL;
  str[1]=' ';
  strcpy(&str[2], rdlg[objnum].txt);

  SDLGui_Text(x, y, str);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog check box object.
*/
static void SDLGui_DrawCheckBox(const SGOBJ *cdlg, int objnum)
{
  char str[80];
  int x, y;

  x = (cdlg[0].x+cdlg[objnum].x)*fontwidth;
  y = (cdlg[0].y+cdlg[objnum].y)*fontheight;

  if( cdlg[objnum].state&SG_SELECTED )
    str[0]=SGCHECKBOX_SELECTED;
   else
    str[0]=SGCHECKBOX_NORMAL;
  str[1]=' ';
  strcpy(&str[2], cdlg[objnum].txt);

  SDLGui_Text(x, y, str);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog popup button object.
*/
static void SDLGui_DrawPopupButton(const SGOBJ *pdlg, int objnum)
{
  int x, y, w, h;
  const char *downstr = "\x02";

  SDLGui_DrawBox(pdlg, objnum);

  x = (pdlg[0].x+pdlg[objnum].x)*fontwidth;
  y = (pdlg[0].y+pdlg[objnum].y)*fontheight;
  w = pdlg[objnum].w*fontwidth;
  h = pdlg[objnum].h*fontheight;

  SDLGui_Text(x, y, pdlg[objnum].txt);
  SDLGui_Text(x+w-fontwidth, y, downstr);
}


/*-----------------------------------------------------------------------*/
/*
  Let the user insert text into an edit field object.
  NOTE: The dlg[objnum].txt must point to an an array that is big enough
  for dlg[objnum].w characters!
*/
static void SDLGui_EditField(SGOBJ *dlg, int objnum)
{
  size_t cursorPos;                     /* Position of the cursor in the edit field */
  int blinkState = 0;                   /* Used for cursor blinking */
  int bStopEditing = FALSE;             /* TRUE if user wants to exit the edit field */
  char *txt;                            /* Shortcut for dlg[objnum].txt */
  SDL_Rect rect;
  Uint32 grey, cursorCol;
  SDL_Event event;

  grey = SDL_MapRGB(pSdlGuiScrn->format,192,192,192);
  cursorCol = SDL_MapRGB(pSdlGuiScrn->format,128,128,128);

  rect.x = (dlg[0].x + dlg[objnum].x) * fontwidth;
  rect.y = (dlg[0].y + dlg[objnum].y) * fontheight;
  rect.w = (dlg[objnum].w + 1) * fontwidth - 1;
  rect.h = dlg[objnum].h * fontheight;

  txt = dlg[objnum].txt;
  cursorPos = strlen(txt);

  do
  {
    /* Look for events */
    if(SDL_PollEvent(&event) == 0)
    {
      /* No event: Wait some time for cursor blinking */
      SDL_Delay(250);
      blinkState ^= 1;
    }
    else
    {
      int keysym;

      /* Handle events */
      do
      {
        switch(event.type)
        {
          case SDL_QUIT:                        /* User wants to quit */
            bQuitProgram = TRUE;
            bStopEditing = TRUE;
            break;
          case SDL_MOUSEBUTTONDOWN:             /* Mouse pressed -> stop editing */
            bStopEditing = TRUE;
            break;
          case SDL_KEYDOWN:                     /* Key pressed */
            keysym = event.key.keysym.sym;
            switch(keysym)
            {
              case SDLK_RETURN:
              case SDLK_KP_ENTER:
                bStopEditing = TRUE;
                break;
              case SDLK_LEFT:
                if(cursorPos > 0)
                  cursorPos -= 1;
                break;
              case SDLK_RIGHT:
                if(cursorPos < strlen(txt))
                  cursorPos += 1;
                break;
              case SDLK_BACKSPACE:
                if(cursorPos > 0)
                {
                  memmove(&txt[cursorPos-1], &txt[cursorPos], strlen(&txt[cursorPos])+1);
                  cursorPos -= 1;
                }
                break;
              case SDLK_DELETE:
                if(cursorPos < strlen(txt))
                  memmove(&txt[cursorPos], &txt[cursorPos+1], strlen(&txt[cursorPos+1])+1);
                break;
              default:
                /* If it is a "good" key then insert it into the text field */
                if(keysym >= 32 && keysym < 256)
                {
                  if(strlen(txt) < (size_t)dlg[objnum].w)
                  {
                    memmove(&txt[cursorPos+1], &txt[cursorPos], strlen(&txt[cursorPos])+1);
                    if(event.key.keysym.mod & (KMOD_LSHIFT|KMOD_RSHIFT))
                      txt[cursorPos] = toupper(keysym);
                    else
                      txt[cursorPos] = keysym;
                    cursorPos += 1;
                  }
                }
                break;
            }
            break;
        }
      }
      while(SDL_PollEvent(&event));

      blinkState = 1;
    }

    /* Redraw the text field: */
    SDL_FillRect(pSdlGuiScrn, &rect, grey);  /* Draw background */
    /* Draw the cursor: */
    if(blinkState && !bStopEditing)
    {
      SDL_Rect cursorrect;
      cursorrect.x = rect.x + cursorPos * fontwidth;  cursorrect.y = rect.y;
      cursorrect.w = fontwidth;  cursorrect.h = rect.h;
      SDL_FillRect(pSdlGuiScrn, &cursorrect, cursorCol);
    }
    SDLGui_Text(rect.x, rect.y, dlg[objnum].txt);  /* Draw text */
    SDL_UpdateRects(pSdlGuiScrn, 1, &rect);
  }
  while(!bStopEditing);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a whole dialog.
*/
void SDLGui_DrawDialog(const SGOBJ *dlg)
{
  int i;
  for(i=0; dlg[i].type!=-1; i++ )
  {
    switch( dlg[i].type )
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
    }
  }
  SDL_UpdateRect(pSdlGuiScrn, 0,0,0,0);
}


/*-----------------------------------------------------------------------*/
/*
  Search an object at a certain position.
*/
static int SDLGui_FindObj(const SGOBJ *dlg, int fx, int fy)
{
  int len, i;
  int ob = -1;
  int xpos, ypos;

  len = 0;
  while( dlg[len].type!=-1)   len++;

  xpos = fx/fontwidth;
  ypos = fy/fontheight;
  /* Now search for the object: */
  for (i = len; i >= 0; i--)
  {
    if(xpos>=dlg[0].x+dlg[i].x && ypos>=dlg[0].y+dlg[i].y
       && xpos<dlg[0].x+dlg[i].x+dlg[i].w && ypos<dlg[0].y+dlg[i].y+dlg[i].h)
    {
      ob = i;
      break;
    }
  }

  return ob;
}


/*-----------------------------------------------------------------------*/
/*
  Search the default button in a dialog.
*/
static int SDLGui_SearchDefaultButton(const SGOBJ *dlg)
{
  int i = 0;

  while (dlg[i].type != -1)
  {
    if (dlg[i].flags & SG_DEFAULT)
      return i;
    i++;
  }

  return 0;
}


/*-----------------------------------------------------------------------*/
/*
  Show and process a dialog. Returns the button number that has been
  pressed or SDLGUI_UNKNOWNEVENT if an unsupported event occured (will be
  stored in parameter pEventOut).
*/
int SDLGui_DoDialog(SGOBJ *dlg, SDL_Event *pEventOut)
{
  int obj=0;
  int oldbutton=0;
  int retbutton=0;
  int i, j, b;
  SDL_Event sdlEvent;
  SDL_Rect rct;
  Uint32 grey;
  SDL_Surface *pBgSurface;
  SDL_Rect dlgrect, bgrect;

  if (pSdlGuiScrn->h / fontheight < dlg[0].h)
  {
    fprintf(stderr, "Screen size too small for dialog!\n");
    return SDLGUI_ERROR;
  }

  grey = SDL_MapRGB(pSdlGuiScrn->format,192,192,192);

  dlgrect.x = dlg[0].x * fontwidth;
  dlgrect.y = dlg[0].y * fontheight;
  dlgrect.w = dlg[0].w * fontwidth;
  dlgrect.h = dlg[0].h * fontheight;

  bgrect.x = bgrect.y = 0;
  bgrect.w = dlgrect.w;
  bgrect.h = dlgrect.h;

  /* Save background */
  pBgSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, dlgrect.w, dlgrect.h, pSdlGuiScrn->format->BitsPerPixel,
                   pSdlGuiScrn->format->Rmask, pSdlGuiScrn->format->Gmask, pSdlGuiScrn->format->Bmask, pSdlGuiScrn->format->Amask);
  if (pSdlGuiScrn->format->palette != NULL)
  {
    SDL_SetColors(pBgSurface, pSdlGuiScrn->format->palette->colors, 0, pSdlGuiScrn->format->palette->ncolors-1);
  }

  if (pBgSurface != NULL)
  {
    SDL_BlitSurface(pSdlGuiScrn,  &dlgrect, pBgSurface, &bgrect);
  }
  else
  {
    fprintf(stderr, "SDLGUI_DoDialog: CreateRGBSurface failed: %s\n", SDL_GetError());
  }

  /* (Re-)draw the dialog */
  SDLGui_DrawDialog(dlg);

  /* Is the left mouse button still pressed? Yes -> Handle TOUCHEXIT objects here */
  SDL_PumpEvents();
  b = SDL_GetMouseState(&i, &j);
  obj = SDLGui_FindObj(dlg, i, j);
  if(obj>0 && (dlg[obj].flags&SG_TOUCHEXIT) )
  {
    oldbutton = obj;
    if( b&SDL_BUTTON(1) )
    {
      dlg[obj].state |= SG_SELECTED;
      retbutton = obj;
    }
  }

  /* The main loop */
  while (retbutton == 0 && !bQuitProgram)
  {
    if (SDL_WaitEvent(&sdlEvent) == 1)  /* Wait for events */
      switch (sdlEvent.type)
      {
        case SDL_QUIT:
          retbutton = SDLGUI_QUIT;
          break;

        case SDL_MOUSEBUTTONDOWN:
          if (sdlEvent.button.button != SDL_BUTTON_LEFT)
          {
            /* Not left mouse button -> unsupported event */
            if (pEventOut)
              retbutton = SDLGUI_UNKNOWNEVENT;
            break;
          }
          /* It was the left button: Find the object under the mouse cursor */
          obj = SDLGui_FindObj(dlg, sdlEvent.button.x, sdlEvent.button.y);
          if(obj>0)
          {
            if(dlg[obj].type==SGBUTTON)
            {
              dlg[obj].state |= SG_SELECTED;
              SDLGui_DrawButton(dlg, obj);
              SDL_UpdateRect(pSdlGuiScrn, (dlg[0].x+dlg[obj].x)*fontwidth-2, (dlg[0].y+dlg[obj].y)*fontheight-2,
                             dlg[obj].w*fontwidth+4, dlg[obj].h*fontheight+4);
              oldbutton=obj;
            }
            if( dlg[obj].flags&SG_TOUCHEXIT )
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
            if (pEventOut)
              retbutton = SDLGUI_UNKNOWNEVENT;
            break;
          }
          /* It was the left button: Find the object under the mouse cursor */
          obj = SDLGui_FindObj(dlg, sdlEvent.button.x, sdlEvent.button.y);
          if(obj>0)
          {
            switch(dlg[obj].type)
            {
              case SGBUTTON:
                if(oldbutton==obj)
                  retbutton=obj;
                break;
              case SGEDITFIELD:
                SDLGui_EditField(dlg, obj);
                break;
              case SGRADIOBUT:
                for(i=obj-1; i>0 && dlg[i].type==SGRADIOBUT; i--)
                {
                  dlg[i].state &= ~SG_SELECTED;  /* Deselect all radio buttons in this group */
                  rct.x = (dlg[0].x+dlg[i].x)*fontwidth;
                  rct.y = (dlg[0].y+dlg[i].y)*fontheight;
                  rct.w = fontwidth;  rct.h = fontheight;
                  SDL_FillRect(pSdlGuiScrn, &rct, grey); /* Clear old */
                  SDLGui_DrawRadioButton(dlg, i);
                  SDL_UpdateRects(pSdlGuiScrn, 1, &rct);
                }
                for(i=obj+1; dlg[i].type==SGRADIOBUT; i++)
                {
                  dlg[i].state &= ~SG_SELECTED;  /* Deselect all radio buttons in this group */
                  rct.x = (dlg[0].x+dlg[i].x)*fontwidth;
                  rct.y = (dlg[0].y+dlg[i].y)*fontheight;
                  rct.w = fontwidth;  rct.h = fontheight;
                  SDL_FillRect(pSdlGuiScrn, &rct, grey); /* Clear old */
                  SDLGui_DrawRadioButton(dlg, i);
                  SDL_UpdateRects(pSdlGuiScrn, 1, &rct);
                }
                dlg[obj].state |= SG_SELECTED;  /* Select this radio button */
                rct.x = (dlg[0].x+dlg[obj].x)*fontwidth;
                rct.y = (dlg[0].y+dlg[obj].y)*fontheight;
                rct.w = fontwidth;  rct.h = fontheight;
                SDL_FillRect(pSdlGuiScrn, &rct, grey); /* Clear old */
                SDLGui_DrawRadioButton(dlg, obj);
                SDL_UpdateRects(pSdlGuiScrn, 1, &rct);
                break;
              case SGCHECKBOX:
                dlg[obj].state ^= SG_SELECTED;
                rct.x = (dlg[0].x+dlg[obj].x)*fontwidth;
                rct.y = (dlg[0].y+dlg[obj].y)*fontheight;
                rct.w = fontwidth;  rct.h = fontheight;
                SDL_FillRect(pSdlGuiScrn, &rct, grey); /* Clear old */
                SDLGui_DrawCheckBox(dlg, obj);
                SDL_UpdateRects(pSdlGuiScrn, 1, &rct);
                break;
              case SGPOPUP:
                dlg[obj].state |= SG_SELECTED;
                SDLGui_DrawPopupButton(dlg, obj);
                SDL_UpdateRect(pSdlGuiScrn, (dlg[0].x+dlg[obj].x)*fontwidth-2, (dlg[0].y+dlg[obj].y)*fontheight-2,
                           dlg[obj].w*fontwidth+4, dlg[obj].h*fontheight+4);
                retbutton=obj;
                break;
            }
          }
          if(oldbutton>0)
          {
            dlg[oldbutton].state &= ~SG_SELECTED;
            SDLGui_DrawButton(dlg, oldbutton);
            SDL_UpdateRect(pSdlGuiScrn, (dlg[0].x+dlg[oldbutton].x)*fontwidth-2, (dlg[0].y+dlg[oldbutton].y)*fontheight-2,
                           dlg[oldbutton].w*fontwidth+4, dlg[oldbutton].h*fontheight+4);
            oldbutton = 0;
          }
          if (obj >= 0 && (dlg[obj].flags&SG_EXIT))
          {
            retbutton = obj;
          }
          break;

        case SDL_MOUSEMOTION:
          break;

        case SDL_KEYDOWN:                     /* Key pressed */
          if (sdlEvent.key.keysym.sym == SDLK_RETURN
              || sdlEvent.key.keysym.sym == SDLK_KP_ENTER)
          {
            retbutton = SDLGui_SearchDefaultButton(dlg);
          }
          else if (pEventOut)
          {
            retbutton = SDLGUI_UNKNOWNEVENT;
          }
          break;

        default:
          if (pEventOut)
            retbutton = SDLGUI_UNKNOWNEVENT;
          break;
      }
  }

  /* Restore background */
  if (pBgSurface)
  {
    SDL_BlitSurface(pBgSurface, &bgrect, pSdlGuiScrn,  &dlgrect);
    SDL_FreeSurface(pBgSurface);
  }

  /* Copy event data of unsupported events if caller wants to have it */
  if (retbutton == SDLGUI_UNKNOWNEVENT && pEventOut)
    memcpy(pEventOut, &sdlEvent, sizeof(SDL_Event));

  if (retbutton == SDLGUI_QUIT)
    bQuitProgram = TRUE;

  return retbutton;
}

