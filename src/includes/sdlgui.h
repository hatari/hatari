/*
  Hatari

  Header for the tiny graphical user interface for Hatari.
*/

#include <SDL.h>

enum
{
  SGBOX,
  SGTEXT,
  SGBUTTON,
  SGRADIOBUT,
  SGCHECKBOX,
  SGPOPUP
};


typedef struct
{
  int type;             /* What type of object */
  int state;		/* 0=not selected, 1=selected */
  int x, y;             /* The offset to the upper left corner */
  int w, h;             /* Width and height */
  char *txt;            /* Text string */
}  SGOBJ;


int SDLGui_Init(void);
int SDLGui_UnInit(void);
int SDLGui_DoDialog(SGOBJ *dlg);
int SDLGui_PrepareFont(void);
