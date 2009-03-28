/*
  Hatari - dlgHardDisk.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
const char DlgHardDisk_fileid[] = "Hatari dlgHardDisk.c : " __DATE__ " " __TIME__;

#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"


#define DISKDLG_ACSIEJECT    3
#define DISKDLG_ACSIBROWSE   4
#define DISKDLG_ACSINAME     5
#define DISKDLG_IDEEJECT     7
#define DISKDLG_IDEBROWSE    8
#define DISKDLG_IDENAME      9
#define DISKDLG_GEMDOSEJECT  11
#define DISKDLG_GEMDOSBROWSE 12
#define DISKDLG_GEMDOSNAME   13
#define DISKDLG_BOOTHD       14
#define DISKDLG_EXIT         15


/* The disks dialog: */
static SGOBJ diskdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 64,16, NULL },
	{ SGTEXT, 0, 0, 27,1, 10,1, "Hard disks" },

	{ SGTEXT, 0, 0, 2,3, 14,1, "ACSI HD image:" },
	{ SGBUTTON, 0, 0, 46,3, 7,1, "Eject" },
	{ SGBUTTON, 0, 0, 54,3, 8,1, "Browse" },
	{ SGTEXT, 0, 0, 3,4, 58,1, NULL },

	{ SGTEXT, 0, 0, 2,6, 13,1, "IDE HD image:" },
	{ SGBUTTON, 0, 0, 46,6, 7,1, "Eject" },
	{ SGBUTTON, 0, 0, 54,6, 8,1, "Browse" },
	{ SGTEXT, 0, 0, 3,7, 58,1, NULL },

	{ SGTEXT, 0, 0, 2,9, 13,1, "GEMDOS drive:" },
	{ SGBUTTON, 0, 0, 46,9, 7,1, "Eject" },
	{ SGBUTTON, 0, 0, 54,9, 8,1, "Browse" },
	{ SGTEXT, 0, 0, 3,10, 58,1, NULL },

	{ SGCHECKBOX, 0, 0, 2,12, 14,1, "Boot from HD" },
	{ SGBUTTON, SG_DEFAULT, 0, 22,14, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/**
 * Let user browse given directory, set directory if one selected.
 * return FALSE if none selected, otherwise return TRUE.
 */
static bool DlgDisk_BrowseDir(char *dlgname, char *confname, int maxlen)
{
	char *str, *selname;

	selname = SDLGui_FileSelect(confname, NULL, FALSE);
	if (selname)
	{
		strcpy(confname, selname);
		free(selname);

		str = strrchr(confname, PATHSEP);
		if (str != NULL)
			str[1] = 0;
		File_CleanFileName(confname);
		File_ShrinkName(dlgname, confname, maxlen);
		return TRUE;
	}
	return FALSE;
}


/**
 * Show and process the hard disk dialog.
 */
void DlgHardDisk_Main(void)
{
	int but;
	char dlgname_gdos[64], dlgname_acsi[64], dlgname_ide[64];

	SDLGui_CenterDlg(diskdlg);

	/* Set up dialog to actual values: */

	/* Boot from harddisk? */
	if (ConfigureParams.HardDisk.bBootFromHardDisk)
		diskdlg[DISKDLG_BOOTHD].state |= SG_SELECTED;
	else
		diskdlg[DISKDLG_BOOTHD].state &= ~SG_SELECTED;

	/* ACSI hard disk image: */
	if (ConfigureParams.HardDisk.bUseHardDiskImage)
		File_ShrinkName(dlgname_acsi, ConfigureParams.HardDisk.szHardDiskImage,
		                diskdlg[DISKDLG_ACSINAME].w);
	else
		dlgname_acsi[0] = '\0';
	diskdlg[DISKDLG_ACSINAME].txt = dlgname_acsi;

	/* IDE hard disk image: */
	if (ConfigureParams.HardDisk.bUseIdeHardDiskImage)
		File_ShrinkName(dlgname_ide, ConfigureParams.HardDisk.szIdeHardDiskImage,
		                diskdlg[DISKDLG_IDENAME].w);
	else
		dlgname_ide[0] = '\0';
	diskdlg[DISKDLG_IDENAME].txt = dlgname_ide;

	/* GEMDOS hard disk directory: */
	if (ConfigureParams.HardDisk.bUseHardDiskDirectories)
		File_ShrinkName(dlgname_gdos, ConfigureParams.HardDisk.szHardDiskDirectories[0],
		                diskdlg[DISKDLG_GEMDOSNAME].w);
	else
		dlgname_gdos[0] = '\0';
	diskdlg[DISKDLG_GEMDOSNAME].txt = dlgname_gdos;

	/* Draw and process the dialog */
	do
	{
		but = SDLGui_DoDialog(diskdlg, NULL);
		switch (but)
		{
		 case DISKDLG_ACSIEJECT:
			ConfigureParams.HardDisk.bUseHardDiskImage = FALSE;
			dlgname_acsi[0] = '\0';
			break;
		 case DISKDLG_ACSIBROWSE:
			if (SDLGui_FileConfSelect(dlgname_acsi,
			                          ConfigureParams.HardDisk.szHardDiskImage,
			                          diskdlg[DISKDLG_ACSINAME].w, FALSE))
				ConfigureParams.HardDisk.bUseHardDiskImage = TRUE;
			break;
		 case DISKDLG_IDEEJECT:
			ConfigureParams.HardDisk.bUseIdeHardDiskImage = FALSE;
			dlgname_ide[0] = '\0';
			break;
		 case DISKDLG_IDEBROWSE:
			if (SDLGui_FileConfSelect(dlgname_ide,
			                          ConfigureParams.HardDisk.szIdeHardDiskImage,
			                          diskdlg[DISKDLG_IDENAME].w, FALSE))
				ConfigureParams.HardDisk.bUseIdeHardDiskImage = TRUE;
			break;
		 case DISKDLG_GEMDOSEJECT:
			ConfigureParams.HardDisk.bUseHardDiskDirectories = FALSE;
			dlgname_gdos[0] = '\0';
			break;
		 case DISKDLG_GEMDOSBROWSE:
			if (DlgDisk_BrowseDir(dlgname_gdos,
			                     ConfigureParams.HardDisk.szHardDiskDirectories[0],
			                     diskdlg[DISKDLG_GEMDOSNAME].w))
				ConfigureParams.HardDisk.bUseHardDiskDirectories = TRUE;
			break;
		}
	}
	while (but != DISKDLG_EXIT && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read values from dialog: */
	ConfigureParams.HardDisk.bBootFromHardDisk = (diskdlg[DISKDLG_BOOTHD].state & SG_SELECTED);
}
