/*
  Hatari - dlgDevice.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Device (Printer etc.) setup dialog
*/
char DlgDevice_rcsid[] = "Hatari $Id: dlgDevice.c,v 1.7 2005-02-13 16:18:52 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "screen.h"


#define DEVDLG_PRNENABLE       3
#define DEVDLG_PRNBROWSE       4
#define DEVDLG_PRNFILENAME     6
#define DEVDLG_RS232ENABLE     8
#define DEVDLG_RS232OUTBROWSE  10
#define DEVDLG_RS232OUTNAME    11
#define DEVDLG_RS232INBROWSE   13
#define DEVDLG_RS232INNAME     14
#define DEVDLG_MIDIENABLE      16
#define DEVDLG_MIDIBROWSE      18
#define DEVDLG_MIDIOUTNAME     19
#define DEVDLG_EXIT            20


static char dlgPrinterName[46+1];
static char dlgRs232OutName[46+1];
static char dlgRs232InName[46+1];
static char dlgMidiOutName[46+1];


/* The devices dialog: */
static SGOBJ devicedlg[] =
{
	{ SGBOX, 0, 0, 0,0, 52,22, NULL },
	{ SGTEXT, 0, 0, 20,1, 13,1, "Devices setup" },

	{ SGBOX, 0, 0, 1,3, 50,4, NULL },
 	{ SGCHECKBOX, 0, 0, 2,3, 28,1, "Enable printer emulation" },
 	{ SGTEXT, 0, 0, 2,5, 10,1, "Print to file:" },
 	{ SGBUTTON, 0, 0, 42,5, 8,1, "Browse" },
 	{ SGTEXT, 0, 0, 3,6, 46,1, dlgPrinterName },

	{ SGBOX, 0, 0, 1,8, 50,6, NULL },
 	{ SGCHECKBOX, 0, 0, 2,8, 28,1, "Enable RS232 emulation" },
 	{ SGTEXT, 0, 0, 2,10, 10,1, "Write RS232 output to file:" },
 	{ SGBUTTON, 0, 0, 42,10, 8,1, "Browse" },
 	{ SGTEXT, 0, 0, 3,11, 46,1, dlgRs232OutName },
 	{ SGTEXT, 0, 0, 2,12, 10,1, "Read RS232 input from file:" },
 	{ SGBUTTON, 0, 0, 42,12, 8,1, "Browse" },
 	{ SGTEXT, 0, 0, 3,13, 46,1, dlgRs232InName },

	{ SGBOX, 0, 0, 1,15, 50,4, NULL },
 	{ SGCHECKBOX, 0, 0, 2,15, 28,1, "Enable MIDI emulation" },
 	{ SGTEXT, 0, 0, 2,17, 10,1, "Write MIDI output to file:" },
 	{ SGBUTTON, 0, 0, 42,17, 8,1, "Browse" },
 	{ SGTEXT, 0, 0, 3,18, 46,1, dlgMidiOutName },

 	{ SGBUTTON, 0, 0, 16,20, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the "Device" dialog.
*/
void Dialog_DeviceDlg(void)
{
	int but;
	char *tmpname;

	/* Allocate memory for tmpname: */
	tmpname = malloc(FILENAME_MAX);
	if (!tmpname)
	{
		perror("Dialog_DeviceDlg");
		return;
	}

	SDLGui_CenterDlg(devicedlg);

	/* Set up dialog from actual values: */

	if (DialogParams.Printer.bEnablePrinting)
		devicedlg[DEVDLG_PRNENABLE].state |= SG_SELECTED;
	else
		devicedlg[DEVDLG_PRNENABLE].state &= ~SG_SELECTED;
	File_ShrinkName(dlgPrinterName, DialogParams.Printer.szPrintToFileName, devicedlg[DEVDLG_PRNFILENAME].w);

	if (DialogParams.RS232.bEnableRS232)
		devicedlg[DEVDLG_RS232ENABLE].state |= SG_SELECTED;
	else
		devicedlg[DEVDLG_RS232ENABLE].state &= ~SG_SELECTED;
	File_ShrinkName(dlgRs232OutName, DialogParams.RS232.szOutFileName, devicedlg[DEVDLG_RS232OUTNAME].w);
	File_ShrinkName(dlgRs232InName, DialogParams.RS232.szInFileName, devicedlg[DEVDLG_RS232INNAME].w);

	if (DialogParams.Midi.bEnableMidi)
		devicedlg[DEVDLG_MIDIENABLE].state |= SG_SELECTED;
	else
		devicedlg[DEVDLG_MIDIENABLE].state &= ~SG_SELECTED;
	File_ShrinkName(dlgMidiOutName, DialogParams.Midi.szMidiOutFileName, devicedlg[DEVDLG_MIDIOUTNAME].w);

	/* The devices dialog main loop */
	do
	{
		but = SDLGui_DoDialog(devicedlg, NULL);

		switch(but)
		{
		 case DEVDLG_PRNBROWSE:                 /* Choose a new printer file */
			strcpy(tmpname, DialogParams.Printer.szPrintToFileName);
			if (SDLGui_FileSelect(tmpname, NULL, TRUE))
			{
				if (!File_DoesFileNameEndWithSlash(tmpname))
				{
					strcpy(DialogParams.Printer.szPrintToFileName, tmpname);
					File_ShrinkName(dlgPrinterName, tmpname, devicedlg[DEVDLG_PRNFILENAME].w);
				}
			}
			break;
		 case DEVDLG_RS232OUTBROWSE:            /* Choose a new RS232 output file */
			strcpy(tmpname, DialogParams.RS232.szOutFileName);
			if (SDLGui_FileSelect(tmpname, NULL, TRUE))
			{
				if (!File_DoesFileNameEndWithSlash(tmpname))
				{
					strcpy(DialogParams.RS232.szOutFileName, tmpname);
					File_ShrinkName(dlgRs232OutName, tmpname, devicedlg[DEVDLG_RS232OUTNAME].w);
				}
			}
			break;
		 case DEVDLG_RS232INBROWSE:             /* Choose a new RS232 input file */
			strcpy(tmpname, DialogParams.RS232.szInFileName);
			if (SDLGui_FileSelect(tmpname, NULL, TRUE))
			{
				if (!File_DoesFileNameEndWithSlash(tmpname))
				{
					strcpy(DialogParams.RS232.szInFileName, tmpname);
					File_ShrinkName(dlgRs232InName, tmpname, devicedlg[DEVDLG_RS232INNAME].w);
				}
			}
			break;
		 case DEVDLG_MIDIBROWSE:                /* Choose a new MIDI file */
			strcpy(tmpname, DialogParams.Midi.szMidiOutFileName);
			if (SDLGui_FileSelect(tmpname, NULL, TRUE))
			{
				if (!File_DoesFileNameEndWithSlash(tmpname))
				{
					strcpy(DialogParams.Midi.szMidiOutFileName, tmpname);
					File_ShrinkName(dlgMidiOutName, tmpname, devicedlg[DEVDLG_MIDIOUTNAME].w);
				}
			}
			break;
		}
	}
	while (but != DEVDLG_EXIT && but != SDLGUI_QUIT && !bQuitProgram);

	/* Read values from dialog */
	DialogParams.Printer.bEnablePrinting = (devicedlg[DEVDLG_PRNENABLE].state & SG_SELECTED);
	DialogParams.RS232.bEnableRS232 = (devicedlg[DEVDLG_RS232ENABLE].state & SG_SELECTED);
	DialogParams.Midi.bEnableMidi = (devicedlg[DEVDLG_MIDIENABLE].state & SG_SELECTED);

	free(tmpname);
}
