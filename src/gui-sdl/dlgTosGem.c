/*
  Hatari - dlgTosGem.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgTosGem_rcsid[] = "Hatari $Id: dlgTosGem.c,v 1.3 2003-12-25 14:19:39 thothy Exp $";

#include <unistd.h>

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "memAlloc.h"
#include "screen.h"
#include "vdi.h"


#define DLGTOSGEM_ROMNAME    4
#define DLGTOSGEM_ROMBROWSE  5
#define DLGTOSGEM_GEMRES     8
#define DLGTOSGEM_RES640     10
#define DLGTOSGEM_RES800     11
#define DLGTOSGEM_RES1024    12
#define DLGTOSGEM_BPP1       14
#define DLGTOSGEM_BPP2       15
#define DLGTOSGEM_BPP4       16
#define DLGTOSGEM_EXIT       17


/* The TOS/GEM dialog: */
static SGOBJ tosgemdlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,24, NULL },
  { SGBOX, 0, 0, 1,1, 38,8, NULL },
  { SGTEXT, 0, 0, 16,2, 9,1, "TOS setup" },
  { SGTEXT, 0, 0, 2,5, 25,1, "ROM image (needs reset!):" },
  { SGTEXT, 0, 0, 2,7, 34,1, NULL },
  { SGBUTTON, 0, 0, 30,5, 8,1, "Browse" },
  { SGBOX, 0, 0, 1,10, 38,10, NULL },
  { SGTEXT, 0, 0, 16,11, 9,1, "GEM setup" },
  { SGCHECKBOX, 0, 0, 2,13, 25,1, "Use extended resolution" },
  { SGTEXT, 0, 0, 2,15, 11,1, "Resolution:" },
  { SGRADIOBUT, 0, 0, 4,16, 9,1, "640x480" },
  { SGRADIOBUT, 0, 0, 16,16, 9,1, "800x600" },
  { SGRADIOBUT, 0, 0, 28,16, 10,1, "1024x768" },
  { SGTEXT, 0, 0, 2,18, 6,1, "Depth:" },
  { SGRADIOBUT, 0, 0, 11,18, 6,1, "1bpp" },
  { SGRADIOBUT, 0, 0, 20,18, 6,1, "2bpp" },
  { SGRADIOBUT, 0, 0, 29,18, 6,1, "4bpp" },
  { SGBUTTON, 0, 0, 10,22, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the TOS/GEM dialog.
*/
void Dialog_TosGemDlg(void)
{
  char *tmpname;
  char dlgromname[35];
  int but;
  int i;

  tmpname = Memory_Alloc(FILENAME_MAX);

  SDLGui_CenterDlg(tosgemdlg);
  File_ShrinkName(dlgromname, DialogParams.TOSGEM.szTOSImageFileName, 34);
  tosgemdlg[DLGTOSGEM_ROMNAME].txt = dlgromname;

  if( DialogParams.TOSGEM.bUseExtGEMResolutions )
    tosgemdlg[DLGTOSGEM_GEMRES].state |= SG_SELECTED;
   else
    tosgemdlg[DLGTOSGEM_GEMRES].state &= ~SG_SELECTED;

  for(i=0; i<3; i++)
  {
    tosgemdlg[DLGTOSGEM_RES640 + i].state &= ~SG_SELECTED;
    tosgemdlg[DLGTOSGEM_BPP1 + i].state &= ~SG_SELECTED;
  }
  tosgemdlg[DLGTOSGEM_RES640+DialogParams.TOSGEM.nGEMResolution-GEMRES_640x480].state |= SG_SELECTED;
  tosgemdlg[DLGTOSGEM_BPP1+DialogParams.TOSGEM.nGEMColours-GEMCOLOUR_2].state |= SG_SELECTED;

  do
  {
    but = SDLGui_DoDialog(tosgemdlg);
    switch( but )
    {
      case DLGTOSGEM_ROMBROWSE:
        strcpy(tmpname, DialogParams.TOSGEM.szTOSImageFileName);
        if(tmpname[0]=='.' && tmpname[1]=='/')  /* Is it in the actual working directory? */
        {
          getcwd(tmpname, FILENAME_MAX);
          File_AddSlashToEndFileName(tmpname);
          strcat(tmpname, &DialogParams.TOSGEM.szTOSImageFileName[2]);
        }
        if( SDLGui_FileSelect(tmpname, NULL, FALSE) )   /* Show and process the file selection dlg */
        {
          strcpy(DialogParams.TOSGEM.szTOSImageFileName, tmpname);
          File_ShrinkName(dlgromname, DialogParams.TOSGEM.szTOSImageFileName, 34);
        }
        Screen_SetFullUpdate();
        Screen_Draw();
        break;
    }
  }
  while(but!=DLGTOSGEM_EXIT && !bQuitProgram);

  DialogParams.TOSGEM.bUseExtGEMResolutions = (tosgemdlg[DLGTOSGEM_GEMRES].state & SG_SELECTED);
  for(i=0; i<3; i++)
  {
    if(tosgemdlg[DLGTOSGEM_RES640 + i].state & SG_SELECTED)
      DialogParams.TOSGEM.nGEMResolution = GEMRES_640x480 + i;
    if(tosgemdlg[DLGTOSGEM_BPP1 + i].state & SG_SELECTED)
      DialogParams.TOSGEM.nGEMColours = GEMCOLOUR_2 + i;
  }

  Memory_Free(tmpname);
}
