/*
  Hatari

  A tiny graphical user interface for Hatari.
*/

#include <SDL.h>
#include <dirent.h>

#include "main.h"
#include "screen.h"
#include "sdlgui.h"
#include "file.h"


#define SGRADIOBUTTON_NORMAL    12
#define SGRADIOBUTTON_SELECTED  13
#define SGCHECKBOX_NORMAL    14
#define SGCHECKBOX_SELECTED  15
#define SGARROWUP    1
#define SGARROWDOWN  2


extern int quit_program;        /* Declared in newcpu.c */


static SDL_Surface *stdfontgfx;
static SDL_Surface *fontgfx=NULL;   /* The actual font graphics */
static int fontwidth, fontheight;   /* Height and width of the actual font */



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
  if(stdfontgfx)
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
  if(tdlg->type==SGBUTTON && (tdlg->state&SG_SELECTED))
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

  if( bdlg->state&SG_SELECTED )
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
  if( rdlg->state&SG_SELECTED )
    str[0]=SGRADIOBUTTON_SELECTED;
   else
    str[0]=SGRADIOBUTTON_NORMAL;
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
  if( cdlg->state&SG_SELECTED )
    str[0]=SGCHECKBOX_SELECTED;
   else
    str[0]=SGCHECKBOX_NORMAL;
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
  int i;
  int cx, cy;
  SDL_Event evnt;
  SDL_Rect rct;
  Uint32 grey;

  grey = SDL_MapRGB(sdlscrn->format,192,192,192);

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
          break;
        case SDL_MOUSEBUTTONDOWN:
          obj = SDLGui_FindObj(dlg, cx, cy, evnt.button.x, evnt.button.y);
          if(obj>0)
          {
            if(dlg[obj].type==SGBUTTON)
            {
              dlg[obj].state |= SG_SELECTED;
              SDLGui_DrawButton(cx, cy, &dlg[obj]);
              SDL_UpdateRect(sdlscrn, (cx+dlg[obj].x)*fontwidth-2, (cy+dlg[obj].y)*fontheight-2,
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
          obj = SDLGui_FindObj(dlg, cx, cy, evnt.button.x, evnt.button.y);
          if(obj>0)
          {
            switch(dlg[obj].type)
            {
              case SGBUTTON:
                if(oldbutton==obj)
                  retbutton=obj;
                break;
              case SGRADIOBUT:
                for(i=obj-1; i>0 && dlg[i].type==SGRADIOBUT; i--)
                {
                  dlg[i].state &= ~SG_SELECTED;  /* Deselect all radio buttons in this group */
                  rct.x = (cx+dlg[i].x)*fontwidth;
                  rct.y = (cy+dlg[i].y)*fontheight;
                  rct.w = fontwidth;  rct.h = fontheight;
                  SDL_FillRect(sdlscrn, &rct, grey); /* Clear old */
                  SDLGui_DrawRadioButton(cx, cy, &dlg[i]);
                  SDL_UpdateRects(sdlscrn, 1, &rct);
                }
                for(i=obj+1; dlg[i].type==SGRADIOBUT; i++)
                {
                  dlg[i].state &= ~SG_SELECTED;  /* Deselect all radio buttons in this group */
                  rct.x = (cx+dlg[i].x)*fontwidth;
                  rct.y = (cy+dlg[i].y)*fontheight;
                  rct.w = fontwidth;  rct.h = fontheight;
                  SDL_FillRect(sdlscrn, &rct, grey); /* Clear old */
                  SDLGui_DrawRadioButton(cx, cy, &dlg[i]);
                  SDL_UpdateRects(sdlscrn, 1, &rct);
                }
                dlg[obj].state |= SG_SELECTED;  /* Select this radio button */
                rct.x = (cx+dlg[obj].x)*fontwidth;
                rct.y = (cy+dlg[obj].y)*fontheight;
                rct.w = fontwidth;  rct.h = fontheight;
                SDL_FillRect(sdlscrn, &rct, grey); /* Clear old */
                SDLGui_DrawRadioButton(cx, cy, &dlg[obj]);
                SDL_UpdateRects(sdlscrn, 1, &rct);
                break;
              case SGCHECKBOX:
                dlg[obj].state ^= SG_SELECTED;
                rct.x = (cx+dlg[obj].x)*fontwidth;
                rct.y = (cy+dlg[obj].y)*fontheight;
                rct.w = fontwidth;  rct.h = fontheight;
                SDL_FillRect(sdlscrn, &rct, grey); /* Clear old */
                SDLGui_DrawCheckBox(cx, cy, &dlg[obj]);
                SDL_UpdateRects(sdlscrn, 1, &rct);
                break;
              case SGPOPUP:
                dlg[obj].state |= SG_SELECTED;
                /*SDLGui_DrawPopupButton(cx, cy, &dlg[obj]);*/
                retbutton=obj;
                break;
            }
          }
          if(oldbutton>0)
          {
            dlg[oldbutton].state &= ~SG_SELECTED;
            SDLGui_DrawButton(cx, cy, &dlg[oldbutton]);
            SDL_UpdateRect(sdlscrn, (cx+dlg[oldbutton].x)*fontwidth-2, (cy+dlg[oldbutton].y)*fontheight-2,
                           dlg[oldbutton].w*fontwidth+4, dlg[oldbutton].h*fontheight+4);
            oldbutton = 0;
          }
          if( dlg[obj].flags&SG_EXIT )
          {
            retbutton = obj;
          }
          break;
      }
  }
  while(retbutton==0 && !bQuitProgram);

  if(bQuitProgram) 
    retbutton=-1;

  return retbutton;
}


/*-----------------------------------------------------------------------*/
/*
  Show and process a file select dialog.
  Returns TRUE if the use selected "okay", FALSE if "cancel".
*/
#define SGFSDLG_UP      24
#define SGFSDLG_DOWN    25
#define SGFSDLG_OKAY    26
#define SGFSDLG_CANCEL  27
int SDLGui_FileSelect(char *path_and_name)
{
  int i;
  int entries;
  char dlgfilenames[16][36];
  struct dirent **files;
  char path[MAX_FILENAME_LENGTH], f_name[128], f_ext[32];
  BOOL reloaddir = TRUE, refreshentries;
  int retbut;
  int oldcursorstate;

  SGOBJ fsdlg[] =
  {
    { SGBOX, 0, 0, 0,0, 40,25, NULL },
    { SGTEXT, 0, 0, 13,1, 13,1, "Choose a file" },
    { SGTEXT, 0, 0, 1,2, 7,1, "Folder:" },
    { SGTEXT, 0, 0, 1,3, 38,1, "/Sorry/this/dialog/does/not/work/yet" },
    { SGTEXT, 0, 0, 1,4, 6,1, "File:" },
    { SGTEXT, 0, 0, 8,4, 13,1, "test" },
    { SGBOX, 0, 0, 1,6, 38,16, NULL },
    { SGBOX, 0, 0, 38,7, 1,14, NULL },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,6, 35,1, dlgfilenames[0] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,7, 35,1, dlgfilenames[1] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,8, 35,1, dlgfilenames[2] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,9, 35,1, dlgfilenames[3] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,10, 35,1, dlgfilenames[4] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,11, 35,1, dlgfilenames[5] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,12, 35,1, dlgfilenames[6] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,13, 35,1, dlgfilenames[7] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,14, 35,1, dlgfilenames[8] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,15, 35,1, dlgfilenames[9] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,16, 35,1, dlgfilenames[10] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,17, 35,1, dlgfilenames[11] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,18, 35,1, dlgfilenames[12] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,19, 35,1, dlgfilenames[13] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,20, 35,1, dlgfilenames[14] },
    { SGTEXT, SG_TOUCHEXIT, 0, 2,21, 35,1, dlgfilenames[15] },
    { SGBUTTON, SG_TOUCHEXIT, 0, 38,6, 1,1, "\x01" },          /* Arrow up */
    { SGBUTTON, SG_TOUCHEXIT, 0, 38,21, 1,1, "\x02" },         /* Arrow down */
    { SGBUTTON, 0, 0, 10,23, 8,1, "Okay" },
    { SGBUTTON, 0, 0, 22,23, 8,1, "Cancel" },
    { -1, 0, 0, 0,0, 0,0, NULL }
  };

  for(i=0; i<16; i++)
    dlgfilenames[i][0] = 0;  /* Clear all entries */

  /* Prepare the path variable */
  File_splitpath(path_and_name, path, f_name, f_ext);

  /* Save old mouse cursor state and enable cursor anyway */
  oldcursorstate = SDL_ShowCursor(SDL_QUERY);
  if( oldcursorstate==SDL_DISABLE )
    SDL_ShowCursor(SDL_ENABLE);

  do
  {
    if( reloaddir )
    {
      if( strlen(path)>=MAX_FILENAME_LENGTH )
      {
        fprintf(stderr, "SDLGui_FileSelect: Path name too long!\n");
        return FALSE;
      }

      /* Load directory entries: */
      /* FIXME: Old reserved memory from scandir is not freed */
      entries = scandir(path, &files, 0, alphasort);
      if(entries<0)
      {
        fprintf(stderr, "SDLGui_FileSelect: Path not found.\n");
        return FALSE;
      }
      reloaddir = FALSE;
      refreshentries = TRUE;
    }

    if( refreshentries )
    {
      /* Copy entries to dialog: */
      for(i=0; i<16 && i<entries; i++)
      {
        if( strlen(files[i]->d_name)<33 )
        {
          strcpy(dlgfilenames[i], "  ");
          strcat(dlgfilenames[i], files[i]->d_name);
        }
        else
        {
          strcpy(dlgfilenames[i], "  ");
          strncat(dlgfilenames[i], files[i]->d_name, 30);
          dlgfilenames[i][32] = 0;
          strcat(dlgfilenames[i], "...");
        }
      }
    }

    /* Show dialog: */
    retbut = SDLGui_DoDialog(fsdlg);
    switch(retbut)
    {
      case SGFSDLG_UP:
        fprintf(stderr,"Up\n");
        break;
      case SGFSDLG_DOWN:
        fprintf(stderr,"Down\n");
        break;
    }

  }
  while(retbut!=SGFSDLG_OKAY && retbut!=SGFSDLG_CANCEL && !bQuitProgram);

  if( oldcursorstate==SDL_DISABLE )
    SDL_ShowCursor(SDL_DISABLE);

  return FALSE;
}

