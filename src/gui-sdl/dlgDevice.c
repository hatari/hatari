/*
  Hatari - dlgDevice.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Device (Printer etc.) setup dialog
*/
static char rcsid[] = "Hatari $Id: dlgDevice.c,v 1.2 2003-08-12 14:44:34 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "memAlloc.h"
#include "file.h"
#include "screen.h"


#define DEVDLG_PRNENABLE    4
#define DEVDLG_PRNFILENAME  6
#define DEVDLG_PRNBROWSE    7
#define DEVDLG_EXIT         8


static char dlgPrinterName[36+1];


/* The devices dialog: */
static SGOBJ devicedlg[] =
{
	{ SGBOX, 0, 0, 0,0, 40,14, NULL },
	{ SGTEXT, 0, 0, 13,1, 13,1, "Devices setup" },
 	{ SGBOX, 0, 0, 1,3, 38,8, NULL },
 	{ SGTEXT, 0, 0, 16,4, 7,1, "Printer" },
 	{ SGCHECKBOX, 0, 0, 2,6, 28,1, "Enable printer emulation" },
 	{ SGTEXT, 0, 0, 2,8, 10,1, "File name:" },
 	{ SGTEXT, 0, 0, 2,9, 36,1, dlgPrinterName },
 	{ SGBUTTON, 0, 0, 32,8, 6,1, "Browse" },
 	{ SGBUTTON, 0, 0, 10,12, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the "Keyboard" dialog.
*/
void Dialog_DeviceDlg(void)
{
	int but;
	char *tmpname;

	/* Allocate memory for tmpname: */
	tmpname = Memory_Alloc(MAX_FILENAME_LENGTH);

	SDLGui_CenterDlg(devicedlg);

	/* Set up dialog from actual values: */

	if( DialogParams.Printer.bEnablePrinting )
		devicedlg[DEVDLG_PRNENABLE].state |= SG_SELECTED;
	else
		devicedlg[DEVDLG_PRNENABLE].state &= ~SG_SELECTED;

	File_ShrinkName(dlgPrinterName, DialogParams.Printer.szPrintToFileName, devicedlg[DEVDLG_PRNFILENAME].w);

	/* The devices dialog main loop */
	do
	{
		but = SDLGui_DoDialog(devicedlg);

		switch(but)
		{
		 case DEVDLG_PRNBROWSE:                    /* Choose a new printer file */
			strcpy(tmpname, DialogParams.Printer.szPrintToFileName);
			if( SDLGui_FileSelect(tmpname, NULL, TRUE) )
			{
				if( !File_DoesFileNameEndWithSlash(tmpname) )
				{
					strcpy(DialogParams.Printer.szPrintToFileName, tmpname);
					File_ShrinkName(dlgPrinterName, tmpname, devicedlg[DEVDLG_PRNFILENAME].w);
				}
			}
			Screen_SetFullUpdate();
			Screen_Draw();
			break;
		}
	}
	while (but != DEVDLG_EXIT && !bQuitProgram);

	/* Read values from dialog */
	DialogParams.Printer.bEnablePrinting = (devicedlg[DEVDLG_PRNENABLE].state & SG_SELECTED);

	Memory_Free(tmpname);
}
