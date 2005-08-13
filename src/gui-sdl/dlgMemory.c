/*
  Hatari - dlgMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgMemory_rcsid[] = "Hatari $Id: dlgMemory.c,v 1.8 2005-08-13 11:21:44 thothy Exp $";

#include "main.h"
#include "dialog.h"
#include "sdlgui.h"
#include "memorySnapShot.h"
#include "file.h"
#include "screen.h"


#define DLGMEM_512KB    4
#define DLGMEM_1MB      5
#define DLGMEM_2MB      6
#define DLGMEM_4MB      7
#define DLGMEM_8MB      8
#define DLGMEM_14MB     9
#define DLGMEM_FILENAME 13
#define DLGMEM_SAVE     14
#define DLGMEM_RESTORE  15
#define DLGMEM_EXIT     16


static char dlgSnapShotName[36+1];


/* The memory dialog: */
static SGOBJ memorydlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,21, NULL },

  { SGBOX, 0, 0, 1,1, 38,7, NULL },
  { SGTEXT, 0, 0, 15,2, 12,1, "Memory setup" },
  { SGTEXT, 0, 0, 4,4, 12,1, "ST-RAM size:" },
  { SGRADIOBUT, 0, 0, 18,4, 9,1, "512 KiB" },
  { SGRADIOBUT, 0, 0, 18,5, 7,1, "1 MiB" },
  { SGRADIOBUT, 0, 0, 18,6, 7,1, "2 MiB" },
  { SGRADIOBUT, 0, 0, 29,4, 7,1, "4 MiB" },
  { SGRADIOBUT, 0, 0, 29,5, 7,1, "8 MiB" },
  { SGRADIOBUT, 0, 0, 29,6, 8,1, "14 MiB" },

  { SGBOX, 0, 0, 1,9, 38,8, NULL },
  { SGTEXT, 0, 0, 12,10, 17,1, "Memory state save" },
  { SGTEXT, 0, 0, 2,12, 20,1, "Snap-shot file name:" },
  { SGTEXT, 0, 0, 2,13, 36,1, dlgSnapShotName },
  { SGBUTTON, 0, 0, 8,15, 10,1, "Save" },
  { SGBUTTON, 0, 0, 22,15, 10,1, "Restore" },

  { SGBUTTON, 0, 0, 10,19, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the memory dialog.
*/
void Dialog_MemDlg(void)
{
  int i;
  int but;
  char *tmpname;

  /* Allocate memory for tmpname: */
  tmpname = malloc(FILENAME_MAX);
  if (!tmpname)
  {
    perror("Dialog_MemDlg");
    return;
  }

  SDLGui_CenterDlg(memorydlg);

  for (i = DLGMEM_512KB; i <= DLGMEM_14MB; i++)
  {
    memorydlg[i].state &= ~SG_SELECTED;
  }

  switch (DialogParams.Memory.nMemorySize)
  {
    case 0: memorydlg[DLGMEM_512KB].state |= SG_SELECTED; break;
    case 1: memorydlg[DLGMEM_1MB].state |= SG_SELECTED; break;
    case 2: memorydlg[DLGMEM_2MB].state |= SG_SELECTED; break;
    case 4: memorydlg[DLGMEM_4MB].state |= SG_SELECTED; break;
    case 8: memorydlg[DLGMEM_8MB].state |= SG_SELECTED; break;
    default: memorydlg[DLGMEM_14MB].state |= SG_SELECTED; break;
  }
  
  File_ShrinkName(dlgSnapShotName, DialogParams.Memory.szMemoryCaptureFileName, memorydlg[DLGMEM_FILENAME].w);

  do
  {
    but = SDLGui_DoDialog(memorydlg, NULL);

    switch(but)
    {
     case DLGMEM_SAVE:                  /* Save memory snap-shot */
        strcpy(tmpname, DialogParams.Memory.szMemoryCaptureFileName);
        if( SDLGui_FileSelect(tmpname, NULL, TRUE) )  /* Choose file name */
        {
          if( !File_DoesFileNameEndWithSlash(tmpname) )
          {
            strcpy(DialogParams.Memory.szMemoryCaptureFileName, tmpname);
            File_ShrinkName(dlgSnapShotName, tmpname, memorydlg[DLGMEM_FILENAME].w);
            MemorySnapShot_Capture(DialogParams.Memory.szMemoryCaptureFileName);
          }
        }
        break;
     case DLGMEM_RESTORE:               /* Load memory snap-shot */
        strcpy(tmpname, DialogParams.Memory.szMemoryCaptureFileName);
        if( SDLGui_FileSelect(tmpname, NULL, FALSE) )  /* Choose file name */
        {
          if( !File_DoesFileNameEndWithSlash(tmpname) )
          {
            strcpy(DialogParams.Memory.szMemoryCaptureFileName, tmpname);
            File_ShrinkName(dlgSnapShotName, tmpname, memorydlg[DLGMEM_FILENAME].w);
            MemorySnapShot_Restore(DialogParams.Memory.szMemoryCaptureFileName);
          }
        }
        break;
    }
  }
  while (but != DLGMEM_EXIT && but != SDLGUI_QUIT && !bQuitProgram );

  if( memorydlg[DLGMEM_512KB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = 0;
  else if( memorydlg[DLGMEM_1MB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = 1;
  else if( memorydlg[DLGMEM_2MB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = 2;
  else if( memorydlg[DLGMEM_4MB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = 4;
  else if( memorydlg[DLGMEM_8MB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = 8;
  else
    DialogParams.Memory.nMemorySize = 14;

  free(tmpname);
}
