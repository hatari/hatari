/*
  Hatari - dlgRom.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgRom_rcsid[] = "Hatari $Id: dlgRom.c,v 1.1 2004-12-05 23:30:19 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "memAlloc.h"
#include "screen.h"


#define DLGROM_TOSNAME    4
#define DLGROM_TOSBROWSE  5
#define DLGROM_EXIT       9


/* The ROM dialog: */
static SGOBJ romdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 40,20, NULL },
	{ SGBOX, 0, 0, 1,1, 38,8, NULL },
	{ SGTEXT, 0, 0, 16,2, 9,1, "TOS setup" },
	{ SGTEXT, 0, 0, 2,5, 25,1, "TOS image (needs reset!):" },
	{ SGTEXT, 0, 0, 2,7, 34,1, NULL },
	{ SGBUTTON, 0, 0, 30,5, 8,1, "Browse" },
	{ SGBOX, 0, 0, 1,10, 38,7, NULL },
	{ SGTEXT, 0, 0, 12,11, 15,1, "Cartridge setup" },
	{ SGTEXT, 0, 0, 10,14, 25,1, "... coming soon ..." },
	{ SGBUTTON, 0, 0, 10,18, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the ROM dialog.
*/
void DlgRom_Main(void)
{
	char *tmpname;
	char dlgromname[35];
	int but;

	tmpname = Memory_Alloc(FILENAME_MAX);

	SDLGui_CenterDlg(romdlg);
	File_ShrinkName(dlgromname, DialogParams.Rom.szTosImageFileName, 34);
	romdlg[DLGROM_TOSNAME].txt = dlgromname;

	do
	{
		but = SDLGui_DoDialog(romdlg);
		switch (but)
		{
		 case DLGROM_TOSBROWSE:
			strcpy(tmpname, DialogParams.Rom.szTosImageFileName);
			File_MakeAbsoluteName(tmpname);
			if (SDLGui_FileSelect(tmpname, NULL, FALSE))   /* Show and process the file selection dlg */
			{
				strcpy(DialogParams.Rom.szTosImageFileName, tmpname);
				File_ShrinkName(dlgromname, DialogParams.Rom.szTosImageFileName, sizeof(dlgromname)-1);
			}
			break;
		}
	}
	while (but!=DLGROM_EXIT && !bQuitProgram);

	Memory_Free(tmpname);
}
