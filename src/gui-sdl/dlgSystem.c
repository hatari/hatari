/*
  Hatari - dlgSystem.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgSystem_rcsid[] = "Hatari $Id: dlgSystem.c,v 1.2 2004-03-01 13:57:30 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"


#define DLGSYS_68000 3
#define DLGSYS_68010 4
#define DLGSYS_68020 5
#define DLGSYS_68030 6
#define DLGSYS_68040 7
#define DLGSYS_PREFETCH 8
#define DLGSYS_BLITTER 9
#define DLGSYS_TIMERD 10


/* The "System" dialog: */
static SGOBJ systemdlg[] =
{
  { SGBOX, 0, 0, 0,0, 30,18, NULL },
  { SGTEXT, 0, 0, 8,1, 14,1, "System options" },
  { SGTEXT, 0, 0, 3,4, 8,1, "CPU Type:" },
  { SGRADIOBUT, 0, 0, 16,4, 7,1, "68000" },
  { SGRADIOBUT, 0, 0, 16,5, 7,1, "68010" },
  { SGRADIOBUT, 0, 0, 16,6, 7,1, "68020" },
  { SGRADIOBUT, 0, 0, 16,7, 11,1, "68020+FPU" },
  { SGRADIOBUT, 0, 0, 16,8, 7,1, "68040" },
  { SGCHECKBOX, 0, 0, 3,10, 24,1, "Use CPU prefetch mode" },
  { SGCHECKBOX, 0, 0, 3,12, 20,1, "Blitter emulation" },
  { SGCHECKBOX, 0, 0, 3,14, 15,1, "Patch Timer-D" },
  { SGBUTTON, 0, 0, 5,16, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the "System" dialog.
*/
void Dialog_SystemDlg(void)
{
  int i;

  SDLGui_CenterDlg(systemdlg);

  /* Set up dialog from actual values: */

  for(i=DLGSYS_68000; i<=DLGSYS_68040; i++)
  {
    systemdlg[i].state &= ~SG_SELECTED;
  }

  systemdlg[DLGSYS_68000+DialogParams.System.nCpuLevel].state |= SG_SELECTED;

  if( DialogParams.System.bCompatibleCpu )
    systemdlg[DLGSYS_PREFETCH].state |= SG_SELECTED;
  else
    systemdlg[DLGSYS_PREFETCH].state &= ~SG_SELECTED;

  if( DialogParams.System.bBlitter )
    systemdlg[DLGSYS_BLITTER].state |= SG_SELECTED;
  else
    systemdlg[DLGSYS_BLITTER].state &= ~SG_SELECTED;

  if (DialogParams.System.bPatchTimerD)
    systemdlg[DLGSYS_TIMERD].state |= SG_SELECTED;
  else
    systemdlg[DLGSYS_TIMERD].state &= ~SG_SELECTED;

  /* Show the dialog: */
  SDLGui_DoDialog(systemdlg);

  /* Read values from dialog: */

  for(i=DLGSYS_68000; i<=DLGSYS_68040; i++)
  {
    if( systemdlg[i].state&SG_SELECTED )
    {
      DialogParams.System.nCpuLevel = i-DLGSYS_68000;
      break;
    }
  }

  DialogParams.System.bCompatibleCpu = (systemdlg[DLGSYS_PREFETCH].state & SG_SELECTED);
  DialogParams.System.bBlitter = ( systemdlg[DLGSYS_BLITTER].state & SG_SELECTED );
  DialogParams.System.bPatchTimerD = ( systemdlg[DLGSYS_TIMERD].state & SG_SELECTED );
}
