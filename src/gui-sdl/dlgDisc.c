/*
  Hatari - dlgDisc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgDisc_rcsid[] = "Hatari $Id: dlgDisc.c,v 1.5 2003-12-29 20:10:04 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "memAlloc.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"

extern void DlgNewDisc_Main(void);    /* Prototype from dlgNewDisc.c */


#define DISCDLG_DISCA       4
#define DISCDLG_BROWSEA     5
#define DISCDLG_DISCB       7
#define DISCDLG_BROWSEB     8
#define DISCDLG_IMGDIR      10
#define DISCDLG_BROWSEIMG   11
#define DISCDLG_AUTOB       12
#define DISCDLG_CREATEIMG   13
#define DISCDLG_BROWSEHDIMG 17
#define DISCDLG_DISCHDIMG   18
#define DISCDLG_UNMOUNTGDOS 20
#define DISCDLG_BROWSEGDOS  21
#define DISCDLG_DISCGDOS    22
#define DISCDLG_BOOTHD      23
#define DISCDLG_EXIT        24


/* The discs dialog: */
static SGOBJ discdlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGBOX, 0, 0, 1,1, 38,11, NULL },
  { SGTEXT, 0, 0, 14,1, 12,1, "Floppy discs" },
  { SGTEXT, 0, 0, 2,3, 2,1, "A:" },
  { SGTEXT, 0, 0, 5,3, 26,1, NULL },
  { SGBUTTON, 0, 0, 32,3, 6,1, "Browse" },
  { SGTEXT, 0, 0, 2,5, 2,1, "B:" },
  { SGTEXT, 0, 0, 5,5, 26,1, NULL },
  { SGBUTTON, 0, 0, 32,5, 6,1, "Browse" },
  { SGTEXT, 0, 0, 2,7, 30,1, "Default disk images directory:" },
  { SGTEXT, 0, 0, 2,8, 28,1, NULL },
  { SGBUTTON, 0, 0, 32,8, 6,1, "Browse" },
  { SGCHECKBOX, 0, 0, 2,10, 16,1, "Auto insert B" },
  { SGBUTTON, 0, 0, 20,10, 18,1, "Create blank image" },
  { SGBOX, 0, 0, 1,13, 38,9, NULL },
  { SGTEXT, 0, 0, 15,13, 10,1, "Hard discs" },
  { SGTEXT, 0, 0, 2,14, 9,1, "HD image:" },
  { SGBUTTON, 0, 0, 32,14, 6,1, "Browse" },
  { SGTEXT, 0, 0, 2,15, 36,1, NULL },
  { SGTEXT, 0, 0, 2,17, 13,1, "GEMDOS drive:" },
  { SGBUTTON, 0, 0, 30,17, 1,1, "\x01" },         /* Up-arrow button for unmounting */
  { SGBUTTON, 0, 0, 32,17, 6,1, "Browse" },
  { SGTEXT, 0, 0, 2,18, 36,1, NULL },
  { SGCHECKBOX, 0, 0, 2,20, 14,1, "Boot from HD" },
  { SGBUTTON, 0, 0, 10,23, 20,1, "Back to main menu" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the disc image dialog.
*/
void Dialog_DiscDlg(void)
{
  int but;
  char dlgnamea[40], dlgnameb[40], dlgdiscdir[40];
  char dlgnamegdos[40], dlgnamehdimg[40];
  char *tmpname;
  char *zip_path;

  /* Allocate memory for tmpname and zip_path: */
  tmpname = Memory_Alloc(2 * FILENAME_MAX);
  zip_path = tmpname + FILENAME_MAX;
  zip_path[0] = 0;

  SDLGui_CenterDlg(discdlg);

  /* Set up dialog to actual values: */

  /* Disc name A: */
  if( EmulationDrives[0].bDiscInserted )
    File_ShrinkName(dlgnamea, EmulationDrives[0].szFileName, discdlg[DISCDLG_DISCA].w);
  else
    dlgnamea[0] = 0;
  discdlg[DISCDLG_DISCA].txt = dlgnamea;

  /* Disc name B: */
  if( EmulationDrives[1].bDiscInserted )
    File_ShrinkName(dlgnameb, EmulationDrives[1].szFileName, discdlg[DISCDLG_DISCB].w);
  else
    dlgnameb[0] = 0;
  discdlg[DISCDLG_DISCB].txt = dlgnameb;

  /* Default image directory: */
  File_ShrinkName(dlgdiscdir, DialogParams.DiscImage.szDiscImageDirectory, discdlg[DISCDLG_IMGDIR].w);
  discdlg[DISCDLG_IMGDIR].txt = dlgdiscdir;

  /* Auto insert disc B: */
  if( DialogParams.DiscImage.bAutoInsertDiscB )
    discdlg[DISCDLG_AUTOB].state |= SG_SELECTED;
   else
    discdlg[DISCDLG_AUTOB].state &= ~SG_SELECTED;

  /* Boot from harddisk? */
  if( DialogParams.HardDisc.bBootFromHardDisc )
    discdlg[DISCDLG_BOOTHD].state |= SG_SELECTED;
   else
    discdlg[DISCDLG_BOOTHD].state &= ~SG_SELECTED;

  /* GEMDOS Hard disc directory: */
  if( strcmp(DialogParams.HardDisc.szHardDiscDirectories[0], ConfigureParams.HardDisc.szHardDiscDirectories[0])!=0
      || GEMDOS_EMU_ON )
    File_ShrinkName(dlgnamegdos, DialogParams.HardDisc.szHardDiscDirectories[0], discdlg[DISCDLG_DISCGDOS].w);
  else
    dlgnamegdos[0] = 0;
  discdlg[DISCDLG_DISCGDOS].txt = dlgnamegdos;

  /* Hard disc image: */
  if( ACSI_EMU_ON )
    File_ShrinkName(dlgnamehdimg, DialogParams.HardDisc.szHardDiscImage, discdlg[DISCDLG_DISCHDIMG].w);
  else
    dlgnamehdimg[0] = 0;
  discdlg[DISCDLG_DISCHDIMG].txt = dlgnamehdimg;

  /* Draw and process the dialog */
  do
  {
    but = SDLGui_DoDialog(discdlg);
    switch(but)
    {
      case DISCDLG_BROWSEA:                       /* Choose a new disc A: */
        if( EmulationDrives[0].bDiscInserted )
          strcpy(tmpname, EmulationDrives[0].szFileName);
         else
          strcpy(tmpname, DialogParams.DiscImage.szDiscImageDirectory);
        if( SDLGui_FileSelect(tmpname, zip_path, FALSE) )
        {
          if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          {
            Floppy_ZipInsertDiscIntoDrive(0, tmpname, zip_path); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            File_ShrinkName(dlgnamea, tmpname, discdlg[DISCDLG_DISCA].w);
          }
          else
          {
            Floppy_EjectDiscFromDrive(0, FALSE); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            dlgnamea[0] = 0;
          }
        }
        break;
      case DISCDLG_BROWSEB:                       /* Choose a new disc B: */
        if( EmulationDrives[1].bDiscInserted )
          strcpy(tmpname, EmulationDrives[1].szFileName);
         else
          strcpy(tmpname, DialogParams.DiscImage.szDiscImageDirectory);
        if( SDLGui_FileSelect(tmpname, zip_path, FALSE) )
        {
          if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          {
            Floppy_ZipInsertDiscIntoDrive(1, tmpname, zip_path); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            File_ShrinkName(dlgnameb, tmpname, discdlg[DISCDLG_DISCB].w);
          }
          else
          {
            Floppy_EjectDiscFromDrive(1, FALSE); /* FIXME: This shouldn't be done here but in Dialog_CopyDialogParamsToConfiguration */
            dlgnameb[0] = 0;
          }
        }
        break;
      case DISCDLG_BROWSEIMG:
        strcpy(tmpname, DialogParams.DiscImage.szDiscImageDirectory);
        if( SDLGui_FileSelect(tmpname, NULL, FALSE) )
        {
          char *ptr;
          ptr = strrchr(tmpname, '/');
          if( ptr!=NULL )  ptr[1]=0;
          strcpy(DialogParams.DiscImage.szDiscImageDirectory, tmpname);
          File_ShrinkName(dlgdiscdir, DialogParams.DiscImage.szDiscImageDirectory, discdlg[DISCDLG_IMGDIR].w);
        }
        break;
      case DISCDLG_CREATEIMG:
        DlgNewDisc_Main();
        break;
      case DISCDLG_UNMOUNTGDOS:
        GemDOS_UnInitDrives();   /* FIXME: This shouldn't be done here but it's the only quick solution I could think of */
        strcpy(DialogParams.HardDisc.szHardDiscDirectories[0], ConfigureParams.HardDisc.szHardDiscDirectories[0]);
        dlgnamegdos[0] = 0;
        break;
      case DISCDLG_BROWSEGDOS:
        strcpy(tmpname, DialogParams.HardDisc.szHardDiscDirectories[0]);
        if( SDLGui_FileSelect(tmpname, NULL, FALSE) )
        {
          char *ptr;
          ptr = strrchr(tmpname, '/');
          if( ptr!=NULL )  ptr[1]=0;        /* Remove file name from path */
          strcpy(DialogParams.HardDisc.szHardDiscDirectories[0], tmpname);
          File_ShrinkName(dlgnamegdos, DialogParams.HardDisc.szHardDiscDirectories[0], discdlg[DISCDLG_DISCGDOS].w);
        }
        break;
      case DISCDLG_BROWSEHDIMG:
        strcpy(tmpname, DialogParams.HardDisc.szHardDiscImage);
        if( SDLGui_FileSelect(tmpname, NULL, FALSE) )
        {
          strcpy(DialogParams.HardDisc.szHardDiscImage, tmpname);
          if( !File_DoesFileNameEndWithSlash(tmpname) && File_Exists(tmpname) )
          {
            File_ShrinkName(dlgnamehdimg, tmpname, discdlg[DISCDLG_DISCHDIMG].w);
          }
          else
          {
            dlgnamehdimg[0] = 0;
          }
        }
        break;
    }
  }
  while(but!=DISCDLG_EXIT && !bQuitProgram);

  /* Read values from dialog */
  DialogParams.DiscImage.bAutoInsertDiscB = (discdlg[DISCDLG_AUTOB].state & SG_SELECTED);
  DialogParams.HardDisc.bBootFromHardDisc = (discdlg[DISCDLG_BOOTHD].state & SG_SELECTED);

  Memory_Free(tmpname);
}
