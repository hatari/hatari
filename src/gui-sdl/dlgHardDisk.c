/*
  Hatari - dlgHardDisk.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgHardDisk_fileid[] = "Hatari dlgHardDisk.c : " __DATE__ " " __TIME__;

#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"


#define DISKDLG_ACSIPREVID         4
#define DISKDLG_ACSIID             6
#define DISKDLG_ACSINEXTID         7
#define DISKDLG_ACSIEJECT          8
#define DISKDLG_ACSIBROWSE         9
#define DISKDLG_ACSINAME          10
#define DISKDLG_SCSIPREVID        13
#define DISKDLG_SCSIID            15
#define DISKDLG_SCSINEXTID        16
#define DISKDLG_SCSIEJECT         17
#define DISKDLG_SCSIBROWSE        18
#define DISKDLG_SCSINAME          19
#define DISKDLG_IDEPREVID         22
#define DISKDLG_IDEID             24
#define DISKDLG_IDENEXTID         25
#define DISKDLG_IDEEJECT          26
#define DISKDLG_IDEBROWSE         27
#define DISKDLG_IDENAME           28
#define DISKDLG_GEMDOSEJECT       31
#define DISKDLG_GEMDOSBROWSE      32
#define DISKDLG_GEMDOSNAME        33
#define DISKDLG_GEMDOSCONV        34
#define DISKDLG_DRIVESKIP         35
#define DISKDLG_PROTOFF           37
#define DISKDLG_PROTON            38
#define DISKDLG_PROTAUTO          39
#define DISKDLG_BOOTHD            40
#define DISKDLG_EXIT              41

static char acsi_id_txt[2];
static char scsi_id_txt[2];
static char ide_id_txt[2];
static char dlgname_gdos[64], dlgname_acsi[64];
static char dlgname_scsi[64], dlgname_ide[64];

/* The disks dialog: */
static SGOBJ diskdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 64,25, NULL },
	{ SGTEXT, 0, 0, 27,1, 10,1, "Hard disks" },

	{ SGBOX, 0, 0, 1,3, 62,2, NULL },
	{ SGBOX, 0, 0, 1,3, 62,1, NULL },
	{ SGBUTTON, 0, 0,  1,3, 3,1, "\x04" },
	{ SGTEXT, 0, 0, 5,3, 7,1, "ACSI HD" },
	{ SGTEXT, 0, 0, 13,3, 3,1, acsi_id_txt },
	{ SGBUTTON, 0, 0, 15,3, 3,1, "\x03" },
	{ SGBUTTON, 0, 0, 47,3, 7,1, "Ejec_t" },
	{ SGBUTTON, 0, 0, 55,3, 8,1, "Brow_se" },
	{ SGTEXT, 0, 0, 2,4, 60,1, dlgname_acsi },

	{ SGBOX, 0, 0, 1,6, 62,2, NULL },
	{ SGBOX, 0, 0, 1,6, 62,1, NULL },
	{ SGBUTTON, 0, 0,  1,6, 3,1, "\x04" },
	{ SGTEXT, 0, 0, 5,6, 9,1, "SCSI HD" },
	{ SGTEXT, 0, 0, 13,6, 1,1, scsi_id_txt },
	{ SGBUTTON, 0, 0, 15,6, 3,1, "\x03" },
	{ SGBUTTON, 0, 0, 47,6, 7,1, "Eje_ct" },
	{ SGBUTTON, 0, 0, 55,6, 8,1, "Bro_wse" },
	{ SGTEXT, 0, 0, 2,7, 60,1, dlgname_scsi },

	{ SGBOX, 0, 0, 1,9, 62,2, NULL },
	{ SGBOX, 0, 0, 1,9, 62,1, NULL },
	{ SGBUTTON, 0, 0,  1,9, 3,1, "\x04" },
	{ SGTEXT, 0, 0, 5,9, 19,1, "IDE HD" },
	{ SGTEXT, 0, 0, 12,9, 1,1, ide_id_txt },
	{ SGBUTTON, 0, 0, 15,9, 3,1, "\x03" },
	{ SGBUTTON, 0, 0, 47,9, 7,1, "E_ject" },
	{ SGBUTTON, 0, 0, 55,9, 8,1, "Br_owse" },
	{ SGTEXT, 0, 0, 2,10, 60,1, dlgname_ide },

	{ SGBOX, 0, 0, 1,12, 62,8, NULL },
	{ SGTEXT, 0, 0, 2,12, 13,1, "GEMDOS drive:" },
	{ SGBUTTON, 0, 0, 47,12, 7,1, "_Eject" },
	{ SGBUTTON, 0, 0, 55,12, 8,1, "B_rowse" },
	{ SGTEXT, 0, 0, 3,13, 58,1, dlgname_gdos },

	{ SGCHECKBOX, 0, 0, 8,15, 42,1, "Atari <-> _host 8-bit file name conversion" },
	{ SGCHECKBOX, 0, 0, 8,16, 42,1, "Add GEMDOS HD after ACSI/IDE _partitions" },

	{ SGTEXT, 0, 0, 8,18, 31,1, "Write protection:" },
	{ SGRADIOBUT, 0, 0, 26,18, 5,1, "O_ff" },
	{ SGRADIOBUT, 0, 0, 32,18, 4,1, "O_n" },
	{ SGRADIOBUT, 0, 0, 37,18, 6,1, "_Auto" },

	{ SGCHECKBOX, 0, 0, 2,21, 16,1, "_Boot from HD" },

	{ SGBUTTON, SG_DEFAULT, 0, 22,23, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/**
 * Let user browse given directory, set directory if one selected.
 * return false if none selected, otherwise return true.
 */
static bool DlgDisk_BrowseDir(char *dlgname, char *confname, int maxlen)
{
	char *str, *selname;

	selname = SDLGui_FileSelect("GEMDOS drive directory:", confname, NULL, false);
	if (selname)
	{
		strcpy(confname, selname);
		free(selname);

		str = strrchr(confname, PATHSEP);
		if (str != NULL)
			str[1] = 0;
		File_CleanFileName(confname);
		File_ShrinkName(dlgname, confname, maxlen);
		return true;
	}
	return false;
}

static void DlgHardDisk_PrepAcsi(int id)
{
	if (ConfigureParams.Acsi[id].bUseDevice)
	{
		File_ShrinkName(dlgname_acsi, ConfigureParams.Acsi[id].sDeviceFile,
		                diskdlg[DISKDLG_ACSINAME].w);
	}
	else
	{
		dlgname_acsi[0] = '\0';
	}

	acsi_id_txt[0] = '0' + id;
	acsi_id_txt[1] = 0;
}

static void DlgHardDisk_PrepScsi(int id)
{
	if (ConfigureParams.Scsi[id].bUseDevice)
	{
		File_ShrinkName(dlgname_scsi, ConfigureParams.Scsi[id].sDeviceFile,
		                diskdlg[DISKDLG_SCSINAME].w);
	}
	else
	{
		dlgname_scsi[0] = '\0';
	}

	scsi_id_txt[0] = '0' + id;
	scsi_id_txt[1] = 0;
}

static void DlgHardDisk_PrepIde(int id)
{
	if (id == 0)
	{
		if (ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage)
			File_ShrinkName(dlgname_ide, ConfigureParams.HardDisk.szIdeMasterHardDiskImage,
			                diskdlg[DISKDLG_IDENAME].w);
		else
			dlgname_ide[0] = '\0';
	}
	else
	{
		if (ConfigureParams.HardDisk.bUseIdeSlaveHardDiskImage)
			File_ShrinkName(dlgname_ide, ConfigureParams.HardDisk.szIdeSlaveHardDiskImage,
			                diskdlg[DISKDLG_IDENAME].w);
		else
			dlgname_ide[0] = '\0';
	}

	ide_id_txt[0] = '0' + id;
	ide_id_txt[1] = '\0';
}

/**
 * Show and process the hard disk dialog.
 */
void DlgHardDisk_Main(void)
{
	int but, i;
	static int a_id, s_id, i_id;

	SDLGui_CenterDlg(diskdlg);

	/* Set up dialog to actual values: */

	/* Boot from harddisk? */
	if (ConfigureParams.HardDisk.bBootFromHardDisk)
		diskdlg[DISKDLG_BOOTHD].state |= SG_SELECTED;
	else
		diskdlg[DISKDLG_BOOTHD].state &= ~SG_SELECTED;

	/* Hard disk images: */
	DlgHardDisk_PrepAcsi(0);
	DlgHardDisk_PrepScsi(0);
	DlgHardDisk_PrepIde(0);

	/* GEMDOS hard disk directory: */
	if (ConfigureParams.HardDisk.bUseHardDiskDirectories)
		File_ShrinkName(dlgname_gdos, ConfigureParams.HardDisk.szHardDiskDirectories[0],
		                diskdlg[DISKDLG_GEMDOSNAME].w);
	else
		dlgname_gdos[0] = '\0';
	diskdlg[DISKDLG_GEMDOSNAME].txt = dlgname_gdos;
	if (ConfigureParams.HardDisk.bFilenameConversion)
		diskdlg[DISKDLG_GEMDOSCONV].state |= SG_SELECTED;
	else
		diskdlg[DISKDLG_GEMDOSCONV].state &= ~SG_SELECTED;
	if (ConfigureParams.HardDisk.nGemdosDrive == DRIVE_SKIP)
		diskdlg[DISKDLG_DRIVESKIP].state |= SG_SELECTED;
	else
		diskdlg[DISKDLG_DRIVESKIP].state &= ~SG_SELECTED;

	/* Write protection */
	for (i = DISKDLG_PROTOFF; i <= DISKDLG_PROTAUTO; i++)
	{
		diskdlg[i].state &= ~SG_SELECTED;
	}
	diskdlg[DISKDLG_PROTOFF+ConfigureParams.HardDisk.nWriteProtection].state |= SG_SELECTED;

	/* Draw and process the dialog */
	do
	{
		but = SDLGui_DoDialog(diskdlg, NULL, false);
		switch (but)
		{
		 case DISKDLG_ACSIPREVID:
			if (a_id > 0)
			{
				--a_id;
				DlgHardDisk_PrepAcsi(a_id);
			}
			break;
		 case DISKDLG_ACSINEXTID:
			if (a_id < 7)
			{
				++a_id;
				DlgHardDisk_PrepAcsi(a_id);
			}
			break;
		 case DISKDLG_ACSIEJECT:
			ConfigureParams.Acsi[a_id].bUseDevice = false;
			dlgname_acsi[0] = '\0';
			break;
		 case DISKDLG_ACSIBROWSE:
			if (SDLGui_FileConfSelect("ACSI HD image:", dlgname_acsi,
			                          ConfigureParams.Acsi[a_id].sDeviceFile,
			                          diskdlg[DISKDLG_ACSINAME].w, false))
				ConfigureParams.Acsi[a_id].bUseDevice = true;
			break;

		 case DISKDLG_SCSIPREVID:
			if (s_id > 0)
			{
				--s_id;
				DlgHardDisk_PrepScsi(s_id);
			}
			break;
		 case DISKDLG_SCSINEXTID:
			if (s_id < 6)
			{
				++s_id;
				DlgHardDisk_PrepScsi(s_id);
			}
			break;
		 case DISKDLG_SCSIEJECT:
			ConfigureParams.Scsi[s_id].bUseDevice = false;
			dlgname_scsi[0] = '\0';
			break;
		 case DISKDLG_SCSIBROWSE:
			if (SDLGui_FileConfSelect("SCSI HD image:", dlgname_scsi,
			                          ConfigureParams.Scsi[s_id].sDeviceFile,
			                          diskdlg[DISKDLG_SCSINAME].w, false))
				ConfigureParams.Scsi[s_id].bUseDevice = true;
			break;

		 case DISKDLG_IDEPREVID:
			if (i_id > 0)
			{
				--i_id;
				DlgHardDisk_PrepIde(i_id);
			}
			break;
		 case DISKDLG_IDENEXTID:
			if (i_id < 1)
			{
				++i_id;
				DlgHardDisk_PrepIde(i_id);
			}
			break;
		 case DISKDLG_IDEEJECT:
			if (i_id == 0)
				ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage = false;
			else
				ConfigureParams.HardDisk.bUseIdeSlaveHardDiskImage = false;
			dlgname_ide[0] = '\0';
			break;
		 case DISKDLG_IDEBROWSE:
			if (i_id == 0)
			{
				if (SDLGui_FileConfSelect("IDE HD 0 image:", dlgname_ide,
				                          ConfigureParams.HardDisk.szIdeMasterHardDiskImage,
				                          diskdlg[DISKDLG_IDENAME].w, false))
					ConfigureParams.HardDisk.bUseIdeMasterHardDiskImage = true;
			}
			else
			{
				if (SDLGui_FileConfSelect("IDE HD 1 image:", dlgname_ide,
				                          ConfigureParams.HardDisk.szIdeSlaveHardDiskImage,
				                          diskdlg[DISKDLG_IDENAME].w, false))
					ConfigureParams.HardDisk.bUseIdeSlaveHardDiskImage = true;
			}
			break;

		 case DISKDLG_GEMDOSEJECT:
			ConfigureParams.HardDisk.bUseHardDiskDirectories = false;
			dlgname_gdos[0] = '\0';
			break;
		 case DISKDLG_GEMDOSBROWSE:
			if (DlgDisk_BrowseDir(dlgname_gdos,
			                     ConfigureParams.HardDisk.szHardDiskDirectories[0],
			                     diskdlg[DISKDLG_GEMDOSNAME].w))
				ConfigureParams.HardDisk.bUseHardDiskDirectories = true;
			break;
		}
	}
	while (but != DISKDLG_EXIT && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read values from dialog: */
	for (i = DISKDLG_PROTOFF; i <= DISKDLG_PROTAUTO; i++)
	{
		if (diskdlg[i].state & SG_SELECTED)
		{
			ConfigureParams.HardDisk.nWriteProtection = i-DISKDLG_PROTOFF;
			break;
		}
	}
	ConfigureParams.HardDisk.bBootFromHardDisk = (diskdlg[DISKDLG_BOOTHD].state & SG_SELECTED);

	if (diskdlg[DISKDLG_DRIVESKIP].state & SG_SELECTED)
		ConfigureParams.HardDisk.nGemdosDrive = DRIVE_SKIP;
	else if (ConfigureParams.HardDisk.nGemdosDrive == DRIVE_SKIP)
		ConfigureParams.HardDisk.nGemdosDrive = DRIVE_C;

	if (diskdlg[DISKDLG_GEMDOSCONV].state & SG_SELECTED)
		ConfigureParams.HardDisk.bFilenameConversion = true;
	else
		ConfigureParams.HardDisk.bFilenameConversion = false;
}
