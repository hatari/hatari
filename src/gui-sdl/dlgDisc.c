/*
  Hatari - dlgDisc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
const char DlgDisk_rcsid[] = "Hatari $Id: dlgDisc.c,v 1.16 2007-01-13 11:57:41 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "floppy.h"


#define DISKDLG_EJECTA      4
#define DISKDLG_BROWSEA     5
#define DISKDLG_DISKA       6
#define DISKDLG_EJECTB      8
#define DISKDLG_BROWSEB     9
#define DISKDLG_DISKB       10
#define DISKDLG_IMGDIR      12
#define DISKDLG_BROWSEIMG   13
#define DISKDLG_AUTOB       14
#define DISKDLG_CREATEIMG   15
#define DISKDLG_PROTOFF     17
#define DISKDLG_PROTON      18
#define DISKDLG_PROTAUTO    19
#define DISKDLG_EJECTHDIMG  23
#define DISKDLG_BROWSEHDIMG 24
#define DISKDLG_DISKHDIMG   25
#define DISKDLG_UNMOUNTGDOS 27
#define DISKDLG_BROWSEGDOS  28
#define DISKDLG_DISKGDOS    29
#define DISKDLG_BOOTHD      30
#define DISKDLG_EXIT        31


/* The disks dialog: */
static SGOBJ diskdlg[] =
{
  { SGBOX, 0, 0, 0,0, 64,25, NULL },

  { SGBOX, 0, 0, 1,1, 62,12, NULL },
  { SGTEXT, 0, 0, 25,1, 12,1, "Floppy disks" },
  { SGTEXT, 0, 0, 2,2, 8,1, "Drive A:" },
  { SGBUTTON, 0, 0, 46,2, 7,1, "Eject" },
  { SGBUTTON, 0, 0, 54,2, 8,1, "Browse" },
  { SGTEXT, 0, 0, 3,3, 58,1, NULL },
  { SGTEXT, 0, 0, 2,4, 8,1, "Drive B:" },
  { SGBUTTON, 0, 0, 46,4, 7,1, "Eject" },
  { SGBUTTON, 0, 0, 54,4, 8,1, "Browse" },
  { SGTEXT, 0, 0, 3,5, 58,1, NULL },
  { SGTEXT, 0, 0, 2,7, 32,1, "Default floppy images directory:" },
  { SGTEXT, 0, 0, 3,8, 58,1, NULL },
  { SGBUTTON, 0, 0, 54,7, 8,1, "Browse" },
  { SGCHECKBOX, 0, 0, 2,10, 16,1, "Auto insert B" },
  { SGBUTTON, 0, 0, 42,10, 20,1, "Create blank image" },
  { SGTEXT, 0, 0, 2,12, 17,1, "Write protection:" },
  { SGRADIOBUT, 0, 0, 21,12, 5,1, "Off" },
  { SGRADIOBUT, 0, 0, 28,12, 5,1, "On" },
  { SGRADIOBUT, 0, 0, 34,12, 6,1, "Auto" },

  { SGBOX, 0, 0, 1,14, 62,8, NULL },
  { SGTEXT, 0, 0, 27,14, 10,1, "Hard disks" },
  { SGTEXT, 0, 0, 2,15, 9,1, "HD image:" },
  { SGBUTTON, 0, 0, 46,15, 7,1, "Eject" },
  { SGBUTTON, 0, 0, 54,15, 8,1, "Browse" },
  { SGTEXT, 0, 0, 3,16, 58,1, NULL },
  { SGTEXT, 0, 0, 2,17, 13,1, "GEMDOS drive:" },
  { SGBUTTON, 0, 0, 46,17, 7,1, "Eject" },
  { SGBUTTON, 0, 0, 54,17, 8,1, "Browse" },
  { SGTEXT, 0, 0, 3,18, 58,1, NULL },
  { SGCHECKBOX, 0, 0, 2,20, 14,1, "Boot from HD" },

  { SGBUTTON, SG_DEFAULT, 0, 22,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the disk image dialog.
*/
void Dialog_DiskDlg(void)
{
  int but, i;
  char dlgnamea[64], dlgnameb[64], dlgdiskdir[64];
  char dlgnamegdos[64], dlgnamehdimg[64];
  char *tmpname;
  char *zip_path;

  /* Allocate memory for tmpname and zip_path: */
  tmpname = malloc(2 * FILENAME_MAX);
  if (!tmpname)
  {
    perror("Dialog_DiskDlg");
    return;
  }
  zip_path = tmpname + FILENAME_MAX;
  zip_path[0] = 0;

  SDLGui_CenterDlg(diskdlg);

  /* Set up dialog to actual values: */

  /* Disk name A: */
  if (EmulationDrives[0].bDiskInserted)
    File_ShrinkName(dlgnamea, EmulationDrives[0].szFileName, diskdlg[DISKDLG_DISKA].w);
  else
    dlgnamea[0] = 0;
  diskdlg[DISKDLG_DISKA].txt = dlgnamea;

  /* Disk name B: */
  if (EmulationDrives[1].bDiskInserted)
    File_ShrinkName(dlgnameb, EmulationDrives[1].szFileName, diskdlg[DISKDLG_DISKB].w);
  else
    dlgnameb[0] = 0;
  diskdlg[DISKDLG_DISKB].txt = dlgnameb;

  /* Default image directory: */
  File_ShrinkName(dlgdiskdir, DialogParams.DiskImage.szDiskImageDirectory, diskdlg[DISKDLG_IMGDIR].w);
  diskdlg[DISKDLG_IMGDIR].txt = dlgdiskdir;

  /* Auto insert disk B: */
  if (DialogParams.DiskImage.bAutoInsertDiskB)
    diskdlg[DISKDLG_AUTOB].state |= SG_SELECTED;
  else
    diskdlg[DISKDLG_AUTOB].state &= ~SG_SELECTED;

  /* Write protection */
  for (i = DISKDLG_PROTOFF; i <= DISKDLG_PROTAUTO; i++)
  {
    diskdlg[i].state &= ~SG_SELECTED;
  }
  diskdlg[DISKDLG_PROTOFF+DialogParams.DiskImage.nWriteProtection].state |= SG_SELECTED;

  /* Boot from harddisk? */
  if (DialogParams.HardDisk.bBootFromHardDisk)
    diskdlg[DISKDLG_BOOTHD].state |= SG_SELECTED;
   else
    diskdlg[DISKDLG_BOOTHD].state &= ~SG_SELECTED;

  /* GEMDOS hard disk directory: */
  if (DialogParams.HardDisk.bUseHardDiskDirectories)
    File_ShrinkName(dlgnamegdos, DialogParams.HardDisk.szHardDiskDirectories[0], diskdlg[DISKDLG_DISKGDOS].w);
  else
    dlgnamegdos[0] = 0;
  diskdlg[DISKDLG_DISKGDOS].txt = dlgnamegdos;

  /* Hard disk image: */
  if (DialogParams.HardDisk.bUseHardDiskImage)
    File_ShrinkName(dlgnamehdimg, DialogParams.HardDisk.szHardDiskImage, diskdlg[DISKDLG_DISKHDIMG].w);
  else
    dlgnamehdimg[0] = 0;
  diskdlg[DISKDLG_DISKHDIMG].txt = dlgnamehdimg;

  /* Draw and process the dialog */
  do
  {
    but = SDLGui_DoDialog(diskdlg, NULL);
    switch(but)
    {
      case DISKDLG_EJECTA:                        /* Eject disk in drive A: */
        Floppy_EjectDiskFromDrive(0, FALSE);
        dlgnamea[0] = 0;
        break;
      case DISKDLG_BROWSEA:                       /* Choose a new disk A: */
        if( EmulationDrives[0].bDiskInserted )
          strcpy(tmpname, EmulationDrives[0].szFileName);
         else
          strcpy(tmpname, DialogParams.DiskImage.szDiskImageDirectory);
        if( SDLGui_FileSelect(tmpname, zip_path, FALSE) )
        {
          if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          {
            Floppy_ZipInsertDiskIntoDrive(0, tmpname, zip_path); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            File_ShrinkName(dlgnamea, tmpname, diskdlg[DISKDLG_DISKA].w);
          }
          else
          {
            Floppy_EjectDiskFromDrive(0, FALSE); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            dlgnamea[0] = 0;
          }
        }
        break;
      case DISKDLG_EJECTB:                        /* Eject disk in drive B: */
        Floppy_EjectDiskFromDrive(1, FALSE);
        dlgnameb[0] = 0;
        break;
      case DISKDLG_BROWSEB:                       /* Choose a new disk B: */
        if( EmulationDrives[1].bDiskInserted )
          strcpy(tmpname, EmulationDrives[1].szFileName);
         else
          strcpy(tmpname, DialogParams.DiskImage.szDiskImageDirectory);
        if( SDLGui_FileSelect(tmpname, zip_path, FALSE) )
        {
          if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          {
            Floppy_ZipInsertDiskIntoDrive(1, tmpname, zip_path); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            File_ShrinkName(dlgnameb, tmpname, diskdlg[DISKDLG_DISKB].w);
          }
          else
          {
            Floppy_EjectDiskFromDrive(1, FALSE); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            dlgnameb[0] = 0;
          }
        }
        break;
      case DISKDLG_BROWSEIMG:
        strcpy(tmpname, DialogParams.DiskImage.szDiskImageDirectory);
        if( SDLGui_FileSelect(tmpname, NULL, FALSE) )
        {
          char *ptr;
          ptr = strrchr(tmpname, '/');
          if( ptr!=NULL )  ptr[1]=0;
          strcpy(DialogParams.DiskImage.szDiskImageDirectory, tmpname);
          File_ShrinkName(dlgdiskdir, DialogParams.DiskImage.szDiskImageDirectory, diskdlg[DISKDLG_IMGDIR].w);
        }
        break;
      case DISKDLG_CREATEIMG:
        DlgNewDisk_Main();
        break;
      case DISKDLG_UNMOUNTGDOS:
        DialogParams.HardDisk.bUseHardDiskDirectories = FALSE;
        dlgnamegdos[0] = 0;
        break;
      case DISKDLG_BROWSEGDOS:
        strcpy(tmpname, DialogParams.HardDisk.szHardDiskDirectories[0]);
        if( SDLGui_FileSelect(tmpname, NULL, FALSE) )
        {
          char *ptr;
          ptr = strrchr(tmpname, '/');
          if( ptr!=NULL )  ptr[1]=0;        /* Remove file name from path */
          strcpy(DialogParams.HardDisk.szHardDiskDirectories[0], tmpname);
          File_CleanFileName(DialogParams.HardDisk.szHardDiskDirectories[0]);
          File_ShrinkName(dlgnamegdos, DialogParams.HardDisk.szHardDiskDirectories[0], diskdlg[DISKDLG_DISKGDOS].w);
          DialogParams.HardDisk.bUseHardDiskDirectories = TRUE;
        }
        break;
      case DISKDLG_EJECTHDIMG:
        DialogParams.HardDisk.bUseHardDiskImage = FALSE;
        dlgnamehdimg[0] = 0;
        break;
      case DISKDLG_BROWSEHDIMG:
        strcpy(tmpname, DialogParams.HardDisk.szHardDiskImage);
        if( SDLGui_FileSelect(tmpname, NULL, FALSE) )
        {
          strcpy(DialogParams.HardDisk.szHardDiskImage, tmpname);
          if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          {
            File_ShrinkName(dlgnamehdimg, tmpname, diskdlg[DISKDLG_DISKHDIMG].w);
            DialogParams.HardDisk.bUseHardDiskImage = TRUE;
          }
          else
          {
            dlgnamehdimg[0] = 0;
          }
        }
        break;
    }
  }
  while (but!=DISKDLG_EXIT && but != SDLGUI_QUIT
         && but != SDLGUI_ERROR && !bQuitProgram);

  /* Read values from dialog: */

  for (i = DISKDLG_PROTOFF; i <= DISKDLG_PROTAUTO; i++)
  {
    if (diskdlg[i].state & SG_SELECTED)
    {
      DialogParams.DiskImage.nWriteProtection = i-DISKDLG_PROTOFF;
      break;
    }
  }

  DialogParams.DiskImage.bAutoInsertDiskB = (diskdlg[DISKDLG_AUTOB].state & SG_SELECTED);
  DialogParams.HardDisk.bBootFromHardDisk = (diskdlg[DISKDLG_BOOTHD].state & SG_SELECTED);

  free(tmpname);
}
