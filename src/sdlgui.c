/*
  Hatari

  A tiny graphical user interface for Hatari.
*/

#include <SDL.h>

#include "main.h"
#include "screen.h"
#include "sdlgui.h"

#define SGRADIOBUTTON_NORMAL    12
#define SGRADIOBUTTON_SELECTED  13
#define SGCHECKBOX_NORMAL    14
#define SGCHECKBOX_SELECTED  15


extern int quit_program;        /* Declared in newcpu.c */


SDL_Surface *stdfontgfx;
SDL_Surface *fontgfx=NULL;      /* The actual font graphics */
int fontwidth, fontheight;      /* Height and width of the actual font */



/*-----------------------------------------------------------------------*/
/*
  Initialize the GUI.
*/
int SDLGui_Init()
{
  char fontname[256];
  sprintf(fontname, "%s/font8.bmp", DATADIR);

  /* Load the font graphics: */
  stdfontgfx = SDL_LoadBMP(fontname);
  if( stdfontgfx==NULL )
  {
    fprintf(stderr, "Could not load image %s:\n %s\n", fontname, SDL_GetError() );
    return 1;
  }

}


/*-----------------------------------------------------------------------*/
/*
  Uninitialize the GUI.
*/
int SDLGui_UnInit()
{
  SDL_FreeSurface(stdfontgfx);
  if(fontgfx)
    SDL_FreeSurface(fontgfx);
}


/*-----------------------------------------------------------------------*/
/*
  Prepare the font to suit the actual resolution.
*/
int SDLGui_PrepareFont()
{
  /* Convert the font graphics to the actual screen format */
  if(fontgfx)
    SDL_FreeSurface(fontgfx);
  fontgfx = SDL_DisplayFormat(stdfontgfx);
  if( fontgfx==NULL )
  {
    fprintf(stderr, "Could not convert font:\n %s\n", SDL_GetError() );
    return FALSE;
  }
  /* Set transparent pixel as the pixel at (0,0) */
  SDL_SetColorKey(fontgfx, (SDL_SRCCOLORKEY|SDL_RLEACCEL), SDL_MapRGB(fontgfx->format,255,255,255));
  /* Get the font width and height: */
  fontwidth = fontgfx->w/16;
  fontheight = fontgfx->h/16;
}


/*-----------------------------------------------------------------------*/
/*
  Draw a text string.
*/
void SDLGui_Text(int x, int y, char *txt)
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
    SDL_BlitSurface(fontgfx, &sr, sdlscrn, &dr);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog text object.
*/
int SDLGui_DrawText(int cx, int cy, SGOBJ *tdlg)
{
  int x, y;
  x = (cx+tdlg->x)*fontwidth;
  y = (cy+tdlg->y)*fontheight;
  if(tdlg->type==SGBUTTON && tdlg->state)
  {
    x+=1;
    y+=1;
  }
  SDLGui_Text(x, y, tdlg->txt);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog box object.
*/
int SDLGui_DrawBox(int cx, int cy, SGOBJ *bdlg)
{
  SDL_Rect rect;
  int x, y, w, h;
  Uint32 grey = SDL_MapRGB(sdlscrn->format,192,192,192);
  Uint32 upleftc, downrightc;

  x = (cx+bdlg->x)*fontwidth;
  y = (cy+bdlg->y)*fontheight;
  w = bdlg->w*fontwidth;
  h = bdlg->h*fontheight;

  if(bdlg->state)
  {
    upleftc = SDL_MapRGB(sdlscrn->format,128,128,128);
    downrightc = SDL_MapRGB(sdlscrn->format,255,255,255);
  }
  else
  {
    upleftc = SDL_MapRGB(sdlscrn->format,255,255,255);
    downrightc = SDL_MapRGB(sdlscrn->format,128,128,128);
  }

  rect.x = x;  rect.y = y;
  rect.w = w;  rect.h = h;
  SDL_FillRect(sdlscrn, &rect, grey);

  rect.x = x;  rect.y = y-1;
  rect.w = w;  rect.h = 1;
  SDL_FillRect(sdlscrn, &rect, upleftc);

  rect.x = x-1;  rect.y = y;
  rect.w = 1;  rect.h = h;
  SDL_FillRect(sdlscrn, &rect, upleftc);

  rect.x = x;  rect.y = y+h;
  rect.w = w;  rect.h = 1;
  SDL_FillRect(sdlscrn, &rect, downrightc);

  rect.x = x+w;  rect.y = y;
  rect.w = 1;  rect.h = h;
  SDL_FillRect(sdlscrn, &rect, downrightc);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a normal button.
*/
int SDLGui_DrawButton(int cx, int cy, SGOBJ *bdlg)
{
  SDLGui_DrawBox(cx, cy, bdlg);
  SDLGui_DrawText(cx+(bdlg->w-strlen(bdlg->txt))/2, cy, bdlg);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog radio button object.
*/
int SDLGui_DrawRadioButton(int cx, int cy, SGOBJ *rdlg)
{
  char str[80];
  if(rdlg->state==0)
    str[0]=SGRADIOBUTTON_NORMAL;
   else
    str[0]=SGRADIOBUTTON_SELECTED;
  str[1]=' ';
  strcpy(&str[2], rdlg->txt);
  SDLGui_Text( (cx+rdlg->x)*fontwidth, (cy+rdlg->y)*fontheight, str);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog check box object.
*/
int SDLGui_DrawCheckBox(int cx, int cy, SGOBJ *cdlg)
{
  char str[80];
  if(cdlg->state==0)
    str[0]=SGCHECKBOX_NORMAL;
   else
    str[0]=SGCHECKBOX_SELECTED;
  str[1]=' ';
  strcpy(&str[2], cdlg->txt);
  SDLGui_Text( (cx+cdlg->x)*fontwidth, (cy+cdlg->y)*fontheight, str);
}


/*-----------------------------------------------------------------------*/
/*
  Draw a dialog popup button object.
*/
int SDLGui_DrawPopupButton(int cx, int cy, SGOBJ *pdlg)
{
  SDLGui_Text( (cx+pdlg->x)*fontwidth, (cy+pdlg->y)*fontheight, "FIXME");
}


/*-----------------------------------------------------------------------*/
/*
  Draw a whole dialog. cx and cy are the upper left corner of the dialog.
*/
int SDLGui_DrawDialog(SGOBJ *dlg, int cx, int cy)
{
  int i;
  for(i=0; dlg[i].type!=-1; i++ )
  {
    switch( dlg[i].type )
    {
      case SGBOX:
        SDLGui_DrawBox(cx, cy, &dlg[i]);
        break;
      case SGTEXT:
        SDLGui_DrawText(cx, cy, &dlg[i]);
        break;
      case SGBUTTON:
        SDLGui_DrawButton(cx, cy, &dlg[i]);
        break;
      case SGRADIOBUT:
        SDLGui_DrawRadioButton(cx, cy, &dlg[i]);
        break;
      case SGCHECKBOX:
        SDLGui_DrawCheckBox(cx, cy, &dlg[i]);
        break;
      case SGPOPUP:
        SDLGui_DrawPopupButton(cx, cy, &dlg[i]);
        break;
    }
  }
  SDL_UpdateRect(sdlscrn, 0,0,0,0);
}


/*-----------------------------------------------------------------------*/
/*
  Search an object at a certain position.
*/
int SDLGui_FindObj(SGOBJ *dlg, int cx, int cy, int fx, int fy)
{
  int len, i;
  int ob = -1;
  int xpos, ypos;
  
  len = 0;
  while( dlg[len].type!=-1)   len++;

  xpos = fx/fontwidth;
  ypos = fy/fontheight;
  /* Now search for the object: */
  for(i=len; i>=0; i--)
  {
    if(xpos>=cx+dlg[i].x && ypos>=cy+dlg[i].y
       && xpos<cx+dlg[i].x+dlg[i].w && ypos<cy+dlg[i].y+dlg[i].h)
    {
      ob = i;
      break;
    }
  }

  return ob;
}


/*-----------------------------------------------------------------------*/
/*
  Show and process a dialog. Returns the button number that has been
  pressed or -1 if something went wrong.
*/
int SDLGui_DoDialog(SGOBJ *dlg)
{
  int obj=0;
  int oldbutton=0;
  int retbutton=0;
  SDL_Event evnt;
  int cx, cy;

  cx = (sdlscrn->w/fontwidth-dlg[0].w)/2;
  cy = (sdlscrn->h/fontheight-dlg[0].h)/2;
  SDLGui_DrawDialog(dlg, cx, cy);

  do
  {
    if( SDL_WaitEvent(&evnt)==1 )  /* Wait for events */
      switch(evnt.type)
      {
        case SDL_KEYDOWN:
          break;
        case SDL_QUIT:
          quit_program = bQuitProgram = TRUE;
          retbutton=-1;
          break;
        case SDL_MOUSEBUTTONDOWN:
          obj = SDLGui_FindObj(dlg, cx, cy, evnt.button.x, evnt.button.y);
          if(obj>0)
          {
            if(dlg[obj].type==SGBUTTON)
            {
              dlg[obj].state=1;
              SDLGui_DrawButton(cx, cy, &dlg[obj]);
              SDL_UpdateRect(sdlscrn, (cx+dlg[obj].x)*fontwidth-2, (cy+dlg[obj].y)*fontheight-2,
                             dlg[obj].w*fontwidth+4, dlg[obj].h*fontheight+4);
              oldbutton=obj;
            }
          }
          break;
        case SDL_MOUSEBUTTONUP:
          obj = SDLGui_FindObj(dlg, cx, cy, evnt.button.x, evnt.button.y);
          if(obj>0)
          {
            switch(dlg[obj].type)
            {
              case SGBUTTON:
                if(oldbutton==obj)
                  retbutton=obj;
                break;
            }
          }
          if(oldbutton>0)
          {
            dlg[oldbutton].state=0;
            SDLGui_DrawButton(cx, cy, &dlg[oldbutton]);
            SDL_UpdateRect(sdlscrn, (cx+dlg[obj].x)*fontwidth-2, (cy+dlg[obj].y)*fontheight-2,
                           dlg[obj].w*fontwidth+4, dlg[obj].h*fontheight+4);
          }
          break;
      }
  }
  while(retbutton==0);

  return retbutton;
}

