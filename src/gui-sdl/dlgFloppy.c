/*
  Hatari - dlgFloppy.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgFloppy_fileid[] = "Hatari dlgFloppy.c : " __DATE__ " " __TIME__;

#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "floppy.h"


#define FLOPPYDLG_ENABLE_A    3
#define FLOPPYDLG_HEADS_DS_A  4
#define FLOPPYDLG_EJECTA      5
#define FLOPPYDLG_BROWSEA     6
#define FLOPPYDLG_DISKA       7

#define FLOPPYDLG_ENABLE_B    9
#define FLOPPYDLG_HEADS_DS_B  10
#define FLOPPYDLG_EJECTB      11
#define FLOPPYDLG_BROWSEB     12
#define FLOPPYDLG_DISKB       13

#define FLOPPYDLG_IMGDIR      15
#define FLOPPYDLG_BROWSEIMG   16
#define FLOPPYDLG_AUTOB       17
#define FLOPPYDLG_FASTFLOPPY  18
#define FLOPPYDLG_CREATEIMG   19
#define FLOPPYDLG_PROTOFF     21
#define FLOPPYDLG_PROTON      22
#define FLOPPYDLG_PROTAUTO    23
#define FLOPPYDLG_EXIT        24


/* The floppy disks dialog: */
static SGOBJ floppydlg[] =
{
	{ SGBOX, 0, 0, 0,0, 64,20, NULL },
	{ SGTEXT, 0, 0, 25,1, 12,1, "Floppy disks" },

	{ SGTEXT, 0, 0, 2,3, 8,1, "Drive A:" },
	{ SGCHECKBOX, 0, 0, 12,3,  9,1, "En_abled" },
	{ SGCHECKBOX, 0, 0, 23,3, 14,1, "_Double Sided" },
	{ SGBUTTON,   0, 0, 46,3,  7,1, "_Eject" },
	{ SGBUTTON,   0, 0, 54,3,  8,1, "B_rowse" },
	{ SGTEXT, 0, 0, 3,4, 58,1, NULL },

	{ SGTEXT, 0, 0, 2,6, 8,1, "Drive B:" },
	{ SGCHECKBOX, 0, 0, 12,6,  9,1, "Ena_bled" },
	{ SGCHECKBOX, 0, 0, 23,6, 14,1, "Doub_le Sided" },
	{ SGBUTTON,   0, 0, 46,6,  7,1, "E_ject" },
	{ SGBUTTON,   0, 0, 54,6,  8,1, "Bro_wse" },
	{ SGTEXT, 0, 0, 3,7, 58,1, NULL },

	{ SGTEXT, 0, 0, 2,9, 32,1, "Default floppy images directory:" },
	{ SGTEXT, 0, 0, 3,10, 58,1, NULL },
	{ SGBUTTON,   0, 0, 54, 9,  8,1, "Brow_se" },
	{ SGCHECKBOX, 0, 0,  2,12, 15,1, "Auto _insert B" },
	{ SGCHECKBOX, 0, 0,  2,14, 20,1, "_Fast floppy access" },
	{ SGBUTTON,   0, 0, 42,14, 20,1, "_Create blank image" },
	{ SGTEXT, 0, 0, 2,16, 17,1, "Write protection:" },
	{ SGRADIOBUT, 0, 0, 21,16,  5,1, "_Off" },
	{ SGRADIOBUT, 0, 0, 28,16,  4,1, "O_n" },
	{ SGRADIOBUT, 0, 0, 34,16,  6,1, "A_uto" },
	{ SGBUTTON, SG_DEFAULT, 0, 22,18, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


#define DLGMOUNT_A       2
#define DLGMOUNT_B       3
#define DLGMOUNT_CANCEL  4

/* The "Alert"-dialog: */
static SGOBJ alertdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 40,6, NULL },
	{ SGTEXT, 0, 0, 3,1, 30,1, "Insert last created disk to?" },
	{ SGBUTTON, 0, 0,  3,4, 10,1, "Drive _A:" },
	{ SGBUTTON, 0, 0, 15,4, 10,1, "Drive _B:" },
	{ SGBUTTON, SG_CANCEL, 0, 27,4, 10,1, "_Cancel" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/**
 * Let user browse given disk, insert disk if one selected.
 */
static void DlgDisk_BrowseDisk(char *dlgname, int drive, int diskid)
{
	char *selname, *zip_path;
	const char *tmpname, *realname;

	assert(drive >= 0 && drive < MAX_FLOPPYDRIVES);
	if (ConfigureParams.DiskImage.szDiskFileName[drive][0])
		tmpname = ConfigureParams.DiskImage.szDiskFileName[drive];
	else
		tmpname = ConfigureParams.DiskImage.szDiskImageDirectory;

	selname = SDLGui_FileSelect("Floppy image:", tmpname, &zip_path, false);
	if (!selname)
		return;

	if (File_Exists(selname))
	{
		realname = Floppy_SetDiskFileName(drive, selname, zip_path);
		if (realname)
			File_ShrinkName(dlgname, realname, floppydlg[diskid].w);
	}
	else
	{
		Floppy_SetDiskFileNameNone(drive);
		dlgname[0] = '\0';
	}
	if (zip_path)
		free(zip_path);
	free(selname);
}


/**
 * Let user browse given directory, set directory if one selected.
 */
static void DlgDisk_BrowseDir(char *dlgname, char *confname, int maxlen)
{
	char *str, *selname;

	selname = SDLGui_FileSelect("Floppy image directory:", confname, NULL, false);
	if (!selname)
		return;

	strcpy(confname, selname);
	free(selname);

	str = strrchr(confname, PATHSEP);
	if (str != NULL)
		str[1] = 0;
	File_CleanFileName(confname);
	File_ShrinkName(dlgname, confname, maxlen);
}


/**
 * Ask whether new disk should be inserted to A: or B: and if yes, insert.
 */
static void DlgFloppy_QueryInsert(char *namea, int ida, char *nameb, int idb, const char *path)
{
	const char *realname;
	int diskid, dlgid;
	char *dlgname;

	SDLGui_CenterDlg(alertdlg);
	switch (SDLGui_DoDialog(alertdlg, NULL, false))
	{
		case DLGMOUNT_A:
			dlgname = namea;
			dlgid = ida;
			diskid = 0;
			break;
		case DLGMOUNT_B:
			dlgname = nameb;
			dlgid = idb;
			diskid = 1;
			break;
		default:
			return;
	}

	realname = Floppy_SetDiskFileName(diskid, path, NULL);
	if (realname)
		File_ShrinkName(dlgname, realname, floppydlg[dlgid].w);
}


/**
 * Show and process the floppy disk image dialog.
 */
void DlgFloppy_Main(void)
{
	int but, i;
	char *newdisk;
	char dlgname[MAX_FLOPPYDRIVES][64], dlgdiskdir[64];

	SDLGui_CenterDlg(floppydlg);

	/* Set up dialog to actual values: */

	/* Disk name A: */
	if (EmulationDrives[0].bDiskInserted)
		File_ShrinkName(dlgname[0], ConfigureParams.DiskImage.szDiskFileName[0],
		                floppydlg[FLOPPYDLG_DISKA].w);
	else
		dlgname[0][0] = '\0';
	floppydlg[FLOPPYDLG_DISKA].txt = dlgname[0];

	/* Disk name B: */
	if (EmulationDrives[1].bDiskInserted)
		File_ShrinkName(dlgname[1], ConfigureParams.DiskImage.szDiskFileName[1],
		                floppydlg[FLOPPYDLG_DISKB].w);
	else
		dlgname[1][0] = '\0';
	floppydlg[FLOPPYDLG_DISKB].txt = dlgname[1];

	/* Default image directory: */
	File_ShrinkName(dlgdiskdir, ConfigureParams.DiskImage.szDiskImageDirectory,
	                floppydlg[FLOPPYDLG_IMGDIR].w);
	floppydlg[FLOPPYDLG_IMGDIR].txt = dlgdiskdir;

	/* Auto insert disk B: */
	if (ConfigureParams.DiskImage.bAutoInsertDiskB)
		floppydlg[FLOPPYDLG_AUTOB].state |= SG_SELECTED;
	else
		floppydlg[FLOPPYDLG_AUTOB].state &= ~SG_SELECTED;

	/* Write protection */
	for (i = FLOPPYDLG_PROTOFF; i <= FLOPPYDLG_PROTAUTO; i++)
	{
		floppydlg[i].state &= ~SG_SELECTED;
	}
	floppydlg[FLOPPYDLG_PROTOFF+ConfigureParams.DiskImage.nWriteProtection].state |= SG_SELECTED;

	/* Fast floppy access */
	if (ConfigureParams.DiskImage.FastFloppy)
		floppydlg[FLOPPYDLG_FASTFLOPPY].state |= SG_SELECTED;
	else
		floppydlg[FLOPPYDLG_FASTFLOPPY].state &= ~SG_SELECTED;

	/* Enable/disable drives A: and B: */
	if (ConfigureParams.DiskImage.EnableDriveA)
		floppydlg[FLOPPYDLG_ENABLE_A].state |= SG_SELECTED;
	else
		floppydlg[FLOPPYDLG_ENABLE_A].state &= ~SG_SELECTED;

	if (ConfigureParams.DiskImage.EnableDriveB)
		floppydlg[FLOPPYDLG_ENABLE_B].state |= SG_SELECTED;
	else
		floppydlg[FLOPPYDLG_ENABLE_B].state &= ~SG_SELECTED;

	/* Set drives to single sided or double sided */
	if (ConfigureParams.DiskImage.DriveA_NumberOfHeads == 2)
		floppydlg[FLOPPYDLG_HEADS_DS_A].state |= SG_SELECTED;
	else
		floppydlg[FLOPPYDLG_HEADS_DS_A].state &= ~SG_SELECTED;

	if (ConfigureParams.DiskImage.DriveB_NumberOfHeads == 2)
		floppydlg[FLOPPYDLG_HEADS_DS_B].state |= SG_SELECTED;
	else
		floppydlg[FLOPPYDLG_HEADS_DS_B].state &= ~SG_SELECTED;


	/* Draw and process the dialog */
	do
	{
		but = SDLGui_DoDialog(floppydlg, NULL, false);
		switch (but)
		{
		 case FLOPPYDLG_EJECTA:                         /* Eject disk in drive A: */
			Floppy_SetDiskFileNameNone(0);
			dlgname[0][0] = '\0';
			break;
		 case FLOPPYDLG_BROWSEA:                        /* Choose a new disk A: */
			DlgDisk_BrowseDisk(dlgname[0], 0, FLOPPYDLG_DISKA);
			break;
		 case FLOPPYDLG_EJECTB:                         /* Eject disk in drive B: */
			Floppy_SetDiskFileNameNone(1);
			dlgname[1][0] = '\0';
			break;
		case FLOPPYDLG_BROWSEB:                         /* Choose a new disk B: */
			DlgDisk_BrowseDisk(dlgname[1], 1, FLOPPYDLG_DISKB);
			break;
		 case FLOPPYDLG_BROWSEIMG:
			DlgDisk_BrowseDir(dlgdiskdir,
			                 ConfigureParams.DiskImage.szDiskImageDirectory,
			                 floppydlg[FLOPPYDLG_IMGDIR].w);
			break;
		 case FLOPPYDLG_CREATEIMG:
			newdisk = DlgNewDisk_Main();
			if (newdisk)
			{
				DlgFloppy_QueryInsert(dlgname[0], FLOPPYDLG_DISKA,
						      dlgname[1], FLOPPYDLG_DISKB,
						      newdisk);
				free(newdisk);
			}
			break;
		}
	}
	while (but != FLOPPYDLG_EXIT && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read values from dialog: */

	for (i = FLOPPYDLG_PROTOFF; i <= FLOPPYDLG_PROTAUTO; i++)
	{
		if (floppydlg[i].state & SG_SELECTED)
		{
			ConfigureParams.DiskImage.nWriteProtection = i-FLOPPYDLG_PROTOFF;
			break;
		}
	}

	ConfigureParams.DiskImage.bAutoInsertDiskB = (floppydlg[FLOPPYDLG_AUTOB].state & SG_SELECTED);
	ConfigureParams.DiskImage.FastFloppy = (floppydlg[FLOPPYDLG_FASTFLOPPY].state & SG_SELECTED);
	ConfigureParams.DiskImage.EnableDriveA = (floppydlg[FLOPPYDLG_ENABLE_A].state & SG_SELECTED);
	ConfigureParams.DiskImage.EnableDriveB = (floppydlg[FLOPPYDLG_ENABLE_B].state & SG_SELECTED);
	ConfigureParams.DiskImage.DriveA_NumberOfHeads = ( (floppydlg[FLOPPYDLG_HEADS_DS_A].state & SG_SELECTED) ? 2 : 1 );
	ConfigureParams.DiskImage.DriveB_NumberOfHeads = ( (floppydlg[FLOPPYDLG_HEADS_DS_B].state & SG_SELECTED) ? 2 : 1 );
}
