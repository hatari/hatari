/*
  Hatari - dlgMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgMemory_rcsid[] = "Hatari $Id: dlgMemory.c,v 1.5 2004-06-11 12:48:49 thothy Exp $";

#include "main.h"
#include "dialog.h"
#include "sdlgui.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "file.h"
#include "screen.h"


#define DLGMEM_512KB    4
#define DLGMEM_1MB      5
#define DLGMEM_2MB      6
#define DLGMEM_4MB      7
#define DLGMEM_FILENAME 11
#define DLGMEM_SAVE     12
#define DLGMEM_RESTORE  13
#define DLGMEM_EXIT     14


static char dlgSnapShotName[36+1];


/* The memory dialog: */
static SGOBJ memorydlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,21, NULL },

  { SGBOX, 0, 0, 1,1, 38,7, NULL },
  { SGTEXT, 0, 0, 15,2, 12,1, "Memory setup" },
  { SGTEXT, 0, 0, 4,4, 12,1, "ST-RAM size:" },
  { SGRADIOBUT, 0, 0, 19,4, 8,1, "512 kB" },
  { SGRADIOBUT, 0, 0, 30,4, 6,1, "1 MB" },
  { SGRADIOBUT, 0, 0, 19,6, 6,1, "2 MB" },
  { SGRADIOBUT, 0, 0, 30,6, 6,1, "4 MB" },

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
  int but;
  char *tmpname;

  /* Allocate memory for tmpname: */
  tmpname = Memory_Alloc(FILENAME_MAX);

  SDLGui_CenterDlg(memorydlg);

  memorydlg[DLGMEM_512KB].state &= ~SG_SELECTED;
  memorydlg[DLGMEM_1MB].state &= ~SG_SELECTED;
  memorydlg[DLGMEM_2MB].state &= ~SG_SELECTED;
  memorydlg[DLGMEM_4MB].state &= ~SG_SELECTED;
  if( DialogParams.Memory.nMemorySize == MEMORY_SIZE_512Kb )
    memorydlg[DLGMEM_512KB].state |= SG_SELECTED;
  else if( DialogParams.Memory.nMemorySize == MEMORY_SIZE_1Mb )
    memorydlg[DLGMEM_1MB].state |= SG_SELECTED;
  else if( DialogParams.Memory.nMemorySize == MEMORY_SIZE_2Mb )
    memorydlg[DLGMEM_2MB].state |= SG_SELECTED;
  else
    memorydlg[DLGMEM_4MB].state |= SG_SELECTED;

  File_ShrinkName(dlgSnapShotName, DialogParams.Memory.szMemoryCaptureFileName, memorydlg[DLGMEM_FILENAME].w);

  do
  {
    but = SDLGui_DoDialog(memorydlg);

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
  while( but!=DLGMEM_EXIT && !bQuitProgram );

  if( memorydlg[DLGMEM_512KB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = MEMORY_SIZE_512Kb;
  else if( memorydlg[DLGMEM_1MB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = MEMORY_SIZE_1Mb;
  else if( memorydlg[DLGMEM_2MB].state & SG_SELECTED )
    DialogParams.Memory.nMemorySize = MEMORY_SIZE_2Mb;
  else
    DialogParams.Memory.nMemorySize = MEMORY_SIZE_4Mb;

  Memory_Free(tmpname);
}
