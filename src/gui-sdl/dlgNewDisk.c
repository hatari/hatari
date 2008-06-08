/*
  Hatari - dlgNewDisk.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
const char DlgNewDisk_rcsid[] = "Hatari $Id: dlgNewDisk.c,v 1.2 2008-06-08 16:07:40 eerot Exp $";

#include "main.h"
#include "configuration.h"
#include "createBlankImage.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"


#define DLGNEWDISK_DECTRACK   3
#define DLGNEWDISK_TRACKSTR   4
#define DLGNEWDISK_INCTRACK   5
#define DLGNEWDISK_SECTORS9   7
#define DLGNEWDISK_SECTORS10  8
#define DLGNEWDISK_SECTORS11  9
#define DLGNEWDISK_SIDES1     11
#define DLGNEWDISK_SIDES2     12
#define DLGNEWDISK_SAVE       13
#define DLGNEWDISK_EXIT       14

static char szTracks[3];
static int nTracks = 80;

/* The new disk image dialog: */
static SGOBJ newdiskdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 28,12, NULL },
	{ SGTEXT, 0, 0, 6,1, 16,1, "New floppy image" },
	{ SGTEXT, 0, 0, 2,3, 7,1, "Tracks:" },
	{ SGBUTTON, 0, 0, 12,3, 1,1, "\x04" },   /* Left-arrow button  */
	{ SGTEXT, 0, 0, 14,3, 2,1, szTracks },
	{ SGBUTTON, 0, 0, 17,3, 1,1, "\x03" },   /* Right-arrow button */
	{ SGTEXT, 0, 0, 2,5, 8,1, "Sectors:" },
	{ SGRADIOBUT, 0, SG_SELECTED, 12,5, 4,1, "9" },
	{ SGRADIOBUT, 0, 0, 17,5, 4,1, "10" },
	{ SGRADIOBUT, 0, 0, 22,5, 4,1, "11" },
	{ SGTEXT, 0, 0, 2,7, 6,1, "Sides:" },
	{ SGRADIOBUT, 0, 0, 12,7, 4,1, "1" },
	{ SGRADIOBUT, 0, SG_SELECTED, 17,7, 4,1, "2" },
	{ SGBUTTON, 0, 0, 4,10, 8,1, "Create" },
	{ SGBUTTON, 0, 0, 18,10, 6,1, "Back" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};

#define DEFAULT_DISK_NAME "new_disk.st"

/*-----------------------------------------------------------------------*/
/*
  Show and process the "new blank disk image" dialog.
*/
void DlgNewDisk_Main(void)
{
	int but;
	char *szNewDiskName, *tmpname;
	sprintf(szTracks, "%i", nTracks);

 	SDLGui_CenterDlg(newdiskdlg);

	/* Initialize disk image name: */
	szNewDiskName = malloc(strlen(ConfigureParams.DiskImage.szDiskImageDirectory) + strlen(DEFAULT_DISK_NAME) + 1);
	if (!szNewDiskName)
	{
		perror("DlgNewDisk_Main");
		return;
	}
	strcpy(szNewDiskName, ConfigureParams.DiskImage.szDiskImageDirectory);
	strcat(szNewDiskName, "new_disk.st");

	/* Draw and process the dialog */
	do
	{
		but = SDLGui_DoDialog(newdiskdlg, NULL);
		switch(but)
		{
		 case DLGNEWDISK_DECTRACK:
			if (nTracks > 40)
				nTracks -= 1;
			sprintf(szTracks, "%i", nTracks);
			break;
		 case DLGNEWDISK_INCTRACK:
			if (nTracks < 85)
				nTracks += 1;
			sprintf(szTracks, "%i", nTracks);
			break;
		 case DLGNEWDISK_SAVE:
			tmpname = SDLGui_FileSelect(szNewDiskName, NULL, TRUE);
			if (tmpname)
			{
				if (!File_DoesFileNameEndWithSlash(tmpname))
				{
					int nSectors, nSides;

					/* Get number of sectors */
					if (newdiskdlg[DLGNEWDISK_SECTORS11].state & SG_SELECTED)
						nSectors = 11;
					else if (newdiskdlg[DLGNEWDISK_SECTORS10].state & SG_SELECTED)
						nSectors = 10;
					else
						nSectors = 9;

					/* Get number of sides */
					if (newdiskdlg[DLGNEWDISK_SIDES1].state & SG_SELECTED)
						nSides = 1;
					else
						nSides = 2;

					CreateBlankImage_CreateFile(tmpname, nTracks, nSectors, nSides);
				}
				free(tmpname);
			}
			break;
		}
	}
	while (but != DLGNEWDISK_EXIT && but != SDLGUI_QUIT
	       && but != SDLGUI_ERROR && !bQuitProgram);

	free(szNewDiskName);
}
