/*
  Hatari - dlgMemory.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
static char rcsid[] = "Hatari $Id: dlgMemory.c,v 1.1 2003-08-04 19:37:31 thothy Exp $";

#include "main.h"
#include "dialog.h"
#include "sdlgui.h"


#define DLGMEM_512KB   4
#define DLGMEM_1MB     5
#define DLGMEM_2MB     6
#define DLGMEM_4MB     7
#define DLGMEM_EXIT    8/*14*/


/* The memory dialog: */
static SGOBJ memorydlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,11/*21*/, NULL },
  { SGBOX, 0, 0, 1,1, 38,7, NULL },
  { SGTEXT, 0, 0, 15,2, 12,1, "Memory setup" },
  { SGTEXT, 0, 0, 4,4, 12,1, "ST-RAM size:" },
  { SGRADIOBUT, 0, 0, 19,4, 8,1, "512 kB" },
  { SGRADIOBUT, 0, 0, 30,4, 6,1, "1 MB" },
  { SGRADIOBUT, 0, 0, 19,6, 6,1, "2 MB" },
  { SGRADIOBUT, 0, 0, 30,6, 6,1, "4 MB" },
/*
  { SGBOX, 0, 0, 1,11, 38,7, NULL },
  { SGTEXT, 0, 0, 12,12, 17,1, "Memory state save" },
  { SGTEXT, 0, 0, 2,14, 28,1, "/Sorry/Not/yet/supported" },
  { SGBUTTON, 0, 0, 32,14, 6,1, "Browse" },
  { SGBUTTON, 0, 0, 8,16, 10,1, "Save" },
  { SGBUTTON, 0, 0, 22,16, 10,1, "Restore" },
*/
  { SGBUTTON, 0, 0, 10,9/*19*/, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the memory dialog.
*/
void Dialog_MemDlg(void)
{
  int but;

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

  do
  {
    but = SDLGui_DoDialog(memorydlg);
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
}
