/*
  Hatari - dlgDevice.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Device (Printer etc.) setup dialog
*/
const char DlgDevice_fileid[] = "Hatari dlgDevice.c";

#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"
#include "midi.h"  /* needed only with PortMidi */
#include "str.h"


#define DEVDLG_PRNENABLE       3
#define DEVDLG_PRNBROWSE       5
#define DEVDLG_PRNFILENAME     6
#define DEVDLG_RS232ENABLE     8
#define DEVDLG_RS232OUTBROWSE  10
#define DEVDLG_RS232OUTNAME    11
#define DEVDLG_RS232INBROWSE   13
#define DEVDLG_RS232INNAME     14
#define DEVDLG_MIDIENABLE      16
#ifndef HAVE_PORTMIDI
#define DEVDLG_MIDIINBROWSE    18
#define DEVDLG_MIDIINNAME      19
#define DEVDLG_MIDIOUTBROWSE   21
#define DEVDLG_MIDIOUTNAME     22
#define DEVDLG_EXIT            23
#else
#define DEVDLG_PREVIN          18
#define DEVDLG_NEXTIN          19
#define DEVDLG_MIDIINNAME      21
#define DEVDLG_PREVOUT         23
#define DEVDLG_NEXTOUT         24
#define DEVDLG_MIDIOUTNAME     26
#define DEVDLG_EXIT            27
#endif


#define MAX_DLG_FILENAME 46+1
static char dlgPrinterName[MAX_DLG_FILENAME];
static char dlgRs232OutName[MAX_DLG_FILENAME];
static char dlgRs232InName[MAX_DLG_FILENAME];
static char dlgMidiInName[MAX_DLG_FILENAME];
static char dlgMidiOutName[MAX_DLG_FILENAME];

/* The devices dialog: */
static SGOBJ devicedlg[] =
{
	{ SGBOX, 0, 0, 0,0, 52,24, NULL },
	{ SGTEXT, 0, 0, 20,1, 13,1, "Devices setup" },

	{ SGBOX, 0, 0, 1,3, 50,4, NULL },
 	{ SGCHECKBOX, 0, 0,  2,3, 26,1, "Enable _printer emulation" },
 	{ SGTEXT, 0, 0, 2,5, 10,1, "Print to file:" },
 	{ SGBUTTON,   0, 0, 42,5,  8,1, "_Browse" },
 	{ SGTEXT, 0, 0, 3,6, 46,1, dlgPrinterName },

	{ SGBOX, 0, 0, 1,8, 50,6, NULL },
 	{ SGCHECKBOX, 0, 0,  2,8, 24,1, "Enable _RS232 emulation" },
 	{ SGTEXT, 0, 0, 2,10, 10,1, "Write RS232 output to file:" },
 	{ SGBUTTON,   0, 0, 42,10, 8,1, "Br_owse" },
 	{ SGTEXT, 0, 0, 3,11, 46,1, dlgRs232OutName },
 	{ SGTEXT, 0, 0, 2,12, 10,1, "Read RS232 input from file:" },
 	{ SGBUTTON,   0, 0, 42,12, 8,1, "Bro_wse" },
 	{ SGTEXT, 0, 0, 3,13, 46,1, dlgRs232InName },

	{ SGBOX, 0, 0, 1,15, 50,6, NULL },
 	{ SGCHECKBOX, 0, 0, 2,15, 23,1, "Enable _MIDI emulation" },
#ifndef HAVE_PORTMIDI
 	{ SGTEXT, 0, 0, 2,17, 26,1, "Read MIDI input from file:" },
 	{ SGBUTTON,   0, 0, 42,17, 8,1, "Brow_se" },
 	{ SGTEXT, 0, 0, 3,18, 46,1, dlgMidiInName },
 	{ SGTEXT, 0, 0, 2,19, 26,1, "Write MIDI output to file:" },
 	{ SGBUTTON,   0, 0, 42,19, 8,1, "Brows_e" },
 	{ SGTEXT, 0, 0, 3,20, 46,1, dlgMidiOutName },
#else
 	{ SGTEXT, 0, 0, 5,17, 7,1, "input:" },
	{ SGBUTTON, 0, 0,  12,17, 3,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGBUTTON, 0, 0, 15,17, 3,1, "\x03", SG_SHORTCUT_RIGHT },
	{ SGBOX, 0, 0, 18,17, 32,1, NULL },
	{ SGTEXT, 0, 0, 19,17, 30,1, dlgMidiInName },
 	{ SGTEXT, 0, 0, 4,19, 7,1, "output:" },
	{ SGBUTTON, 0, 0,  12,19, 3,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGBUTTON, 0, 0, 15,19, 3,1, "\x03", SG_SHORTCUT_RIGHT },
	{ SGBOX, 0, 0, 18,19, 32,1, NULL },
	{ SGTEXT, 0, 0, 19,19, 30,1, dlgMidiOutName },
#endif
 	{ SGBUTTON, SG_DEFAULT, 0, 16,22, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/**
 * Show and process the "Device" dialog.
 */
void Dialog_DeviceDlg(void)
{
	int but;
#ifdef HAVE_PORTMIDI
	const char *name, *midiInName, *midiOutName;
#endif

	SDLGui_CenterDlg(devicedlg);

	/* Set up dialog from actual values: */

	if (ConfigureParams.Printer.bEnablePrinting)
		devicedlg[DEVDLG_PRNENABLE].state |= SG_SELECTED;
	else
		devicedlg[DEVDLG_PRNENABLE].state &= ~SG_SELECTED;
	File_ShrinkName(dlgPrinterName, ConfigureParams.Printer.szPrintToFileName, devicedlg[DEVDLG_PRNFILENAME].w);

	if (ConfigureParams.RS232.bEnableRS232)
		devicedlg[DEVDLG_RS232ENABLE].state |= SG_SELECTED;
	else
		devicedlg[DEVDLG_RS232ENABLE].state &= ~SG_SELECTED;
	File_ShrinkName(dlgRs232OutName, ConfigureParams.RS232.szOutFileName, devicedlg[DEVDLG_RS232OUTNAME].w);
	File_ShrinkName(dlgRs232InName, ConfigureParams.RS232.szInFileName, devicedlg[DEVDLG_RS232INNAME].w);

	if (ConfigureParams.Midi.bEnableMidi)
		devicedlg[DEVDLG_MIDIENABLE].state |= SG_SELECTED;
	else
		devicedlg[DEVDLG_MIDIENABLE].state &= ~SG_SELECTED;
#ifndef HAVE_PORTMIDI
	File_ShrinkName(dlgMidiInName, ConfigureParams.Midi.sMidiInFileName, devicedlg[DEVDLG_MIDIINNAME].w);
	File_ShrinkName(dlgMidiOutName, ConfigureParams.Midi.sMidiOutFileName, devicedlg[DEVDLG_MIDIOUTNAME].w);
#else
	midiInName = Midi_Host_GetPortName(ConfigureParams.Midi.sMidiInPortName, MIDI_NAME_FIND, MIDI_FOR_INPUT);
	File_ShrinkName(dlgMidiInName, midiInName ? midiInName : "Off", devicedlg[DEVDLG_MIDIINNAME].w);

	midiOutName = Midi_Host_GetPortName(ConfigureParams.Midi.sMidiOutPortName, MIDI_NAME_FIND, MIDI_FOR_OUTPUT);
	File_ShrinkName(dlgMidiOutName,  midiOutName ? midiOutName : "Off", devicedlg[DEVDLG_MIDIOUTNAME].w);
#endif

	/* The devices dialog main loop */
	do
	{
		but = SDLGui_DoDialog(devicedlg);

		switch(but)
		{
		 case DEVDLG_PRNBROWSE:                 /* Choose a new printer file */
			SDLGui_FileConfSelect("Printer output:", dlgPrinterName,
                                              ConfigureParams.Printer.szPrintToFileName,
                                              devicedlg[DEVDLG_PRNFILENAME].w,
                                              true);
			break;
		 case DEVDLG_RS232OUTBROWSE:            /* Choose a new RS232 output file */
			SDLGui_FileConfSelect("RS232 output:", dlgRs232OutName,
                                              ConfigureParams.RS232.szOutFileName,
                                              devicedlg[DEVDLG_RS232OUTNAME].w,
                                              true);
			break;
		 case DEVDLG_RS232INBROWSE:             /* Choose a new RS232 input file */
			SDLGui_FileConfSelect("RS232 input:", dlgRs232InName,
                                              ConfigureParams.RS232.szInFileName,
                                              devicedlg[DEVDLG_RS232INNAME].w,
                                              true);
			break;
#ifndef HAVE_PORTMIDI
		 case DEVDLG_MIDIINBROWSE:              /* Choose a new MIDI file */
			SDLGui_FileConfSelect("MIDI input:", dlgMidiInName,
                                              ConfigureParams.Midi.sMidiInFileName,
                                              devicedlg[DEVDLG_MIDIINNAME].w,
                                              true);
			break;
		 case DEVDLG_MIDIOUTBROWSE:             /* Choose a new MIDI file */
			SDLGui_FileConfSelect("MIDI output:", dlgMidiOutName,
                                              ConfigureParams.Midi.sMidiOutFileName,
                                              devicedlg[DEVDLG_MIDIOUTNAME].w,
                                              true);
			break;
#else
		case DEVDLG_PREVIN:
			midiInName = Midi_Host_GetPortName(midiInName, MIDI_NAME_PREV, MIDI_FOR_INPUT);
			File_ShrinkName(dlgMidiInName, midiInName ? midiInName : "Off",
					devicedlg[DEVDLG_MIDIINNAME].w);
			break;
		case DEVDLG_NEXTIN:
			name = Midi_Host_GetPortName(midiInName, MIDI_NAME_NEXT, MIDI_FOR_INPUT);
			if (name)
			{
				midiInName = name;
				File_ShrinkName(dlgMidiInName, midiInName,
						devicedlg[DEVDLG_MIDIINNAME].w);
			}
			break;
		case DEVDLG_PREVOUT:
			midiOutName = Midi_Host_GetPortName(midiOutName, MIDI_NAME_PREV, MIDI_FOR_OUTPUT);
			File_ShrinkName(dlgMidiOutName, midiOutName ? midiOutName : "Off",
					devicedlg[DEVDLG_MIDIOUTNAME].w);
			break;
		case DEVDLG_NEXTOUT:
			name = Midi_Host_GetPortName(midiOutName, MIDI_NAME_NEXT, MIDI_FOR_OUTPUT);
			if (name)
			{
				midiOutName = name;
				File_ShrinkName(dlgMidiOutName, midiOutName,
						devicedlg[DEVDLG_MIDIOUTNAME].w);
			}
			break;
#endif
		}
	}
	while (but != DEVDLG_EXIT && but != SDLGUI_QUIT
	       && but != SDLGUI_ERROR && !bQuitProgram);

	/* Read values from dialog */
	ConfigureParams.Printer.bEnablePrinting = (devicedlg[DEVDLG_PRNENABLE].state & SG_SELECTED);
	ConfigureParams.RS232.bEnableRS232 = (devicedlg[DEVDLG_RS232ENABLE].state & SG_SELECTED);
	ConfigureParams.Midi.bEnableMidi = (devicedlg[DEVDLG_MIDIENABLE].state & SG_SELECTED);
#ifdef HAVE_PORTMIDI
	Str_Copy(ConfigureParams.Midi.sMidiInPortName, midiInName ? midiInName : "Off",
	         sizeof(ConfigureParams.Midi.sMidiInPortName));
	Str_Copy(ConfigureParams.Midi.sMidiOutPortName, midiOutName ? midiOutName : "Off",
	         sizeof(ConfigureParams.Midi.sMidiOutPortName));
#endif
}
