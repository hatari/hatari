/*
  Hatari - sdlgui.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Header for the tiny graphical user interface for Hatari.
*/

#ifndef HATARI_SDLGUI_H
#define HATARI_SDLGUI_H

#include <SDL.h>

/* object types: */
enum
{
  SGSTOP = -1, /* type used at end of dialog to terminate it */
  SGBOX,
  SGTEXT,
  SGEDITFIELD,
  SGBUTTON,
  SGRADIOBUT,
  SGCHECKBOX,
  SGPOPUP,
  SGSCROLLBAR
};


/* Object flags: */
#define SG_TOUCHEXIT   1   /* Exit immediately when mouse button is pressed down */
#define SG_EXIT        2   /* Exit when mouse button has been pressed (and released) */
#define SG_DEFAULT     4   /* Marks a default button, selectable with Enter & Return keys */
#define SG_CANCEL      8   /* Marks a cancel button, selectable with ESC key */
#define SG_REPEAT     16   /* (Scrollbar) buttons which repeat regardless of mouse position */

/* Object states: */
#define SG_SELECTED    1
#define SG_MOUSEDOWN   2
#define SG_FOCUSED     4   /* Marks an object that has selection focus */
#define SG_WASFOCUSED  8   /* Marks an object that had selection focus & its bg needs redraw */

/* special shortcut keys, something that won't conflict with text shortcuts */
#define SG_SHORTCUT_LEFT	'<'
#define SG_SHORTCUT_RIGHT	'>'
#define SG_SHORTCUT_UP  	'^'
#define SG_SHORTCUT_DOWN	'|'

/* Special characters: */
#define SGRADIOBUTTON_NORMAL    12
#define SGRADIOBUTTON_SELECTED  13
#define SGCHECKBOX_NORMAL       14
#define SGCHECKBOX_SELECTED     15
#define SGARROWUP                1
#define SGARROWDOWN              2
#define SGFOLDER                 5

/* Object matching return codes: (negative so they aren't mixed with object indices) */
#define SDLGUI_ERROR         -1
#define SDLGUI_QUIT          -2
#define SDLGUI_UNKNOWNEVENT  -3
#define SDLGUI_NOTFOUND      -4

typedef struct
{
  int type;             /* What type of object */
  int flags;            /* Object flags */
  int state;            /* Object state */
  int x, y;             /* The offset to the upper left corner */
  int w, h;             /* Width and height (for scrollbar : height and position) */
  char *txt;            /* Text string */
  int shortcut;         /* shortcut key */
}  SGOBJ;

extern int sdlgui_fontwidth;	/* Width of the actual font */
extern int sdlgui_fontheight;	/* Height of the actual font */

extern int SDLGui_Init(void);
extern int SDLGui_UnInit(void);
extern int SDLGui_SetScreen(SDL_Surface *pScrn);
extern void SDLGui_GetFontSize(int *width, int *height);
extern void SDLGui_Text(int x, int y, const char *txt);
extern void SDLGui_DrawDialog(const SGOBJ *dlg);
extern void SDLGui_ScaleMouseStateCoordinates(int *x, int *y);
extern int SDLGui_DoDialogExt(SGOBJ *dlg, bool (*isEventOut)(SDL_EventType), SDL_Event *pEventOut, int current_object);
extern int SDLGui_DoDialog(SGOBJ *dlg);
extern void SDLGui_CenterDlg(SGOBJ *dlg);
extern char* SDLGui_FileSelect(const char *title, const char *path_and_name, char **zip_path, bool bAllowNew);
extern bool SDLGui_FileConfSelect(const char *title, char *dlgname, char *confname, int maxlen, bool bAllowNew);
extern bool SDLGui_DirConfSelect(const char *title, char *dlgname, char *confname, int maxlen);

#endif
