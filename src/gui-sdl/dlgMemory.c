/*
  Hatari - dlgMemory.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgMemory_fileid[] = "Hatari dlgMemory.c";

#include "main.h"
#include "dialog.h"
#include "sdlgui.h"
#include "memorySnapShot.h"
#include "file.h"
#include "screen.h"
#include "options.h"


#define DLGMEM_256KB	4
#define DLGMEM_512KB	5
#define DLGMEM_1MB	6
#define DLGMEM_2MB	7
#define DLGMEM_2_5MB	8
#define DLGMEM_4MB	9
#define DLGMEM_8MB	10
#define DLGMEM_10MB	11
#define DLGMEM_14MB	12
#define DLGMEM_TTRAM_LESS	14
#define DLGMEM_TTRAM_TEXT	15
#define DLGMEM_TTRAM_MORE	16
#define DLGMEM_FILENAME	21
#define DLGMEM_SAVE	22
#define DLGMEM_RESTORE	23
#define DLGMEM_AUTOSAVE	24
#define DLGMEM_EXIT	25


/* String for TT RAM size */
static char sTTRamSize[4];

#define DLG_TTRAM_STEP	4
#define DLG_TTRAM_MIN	0
#define DLG_TTRAM_MAX	512

static char dlgSnapShotName[36+1];


/* The memory dialog: */
static SGOBJ memorydlg[] =
{
	{ SGBOX, 0, 0, 0,0, 40,25, NULL },

	{ SGBOX, 0, 0, 1,1, 38,10, NULL },
	{ SGTEXT, 0, 0, 15,2, 12,1, "Memory setup" },
	{ SGTEXT, 0, 0, 4,4, 12,1, "ST-RAM size:" },
	{ SGRADIOBUT, 0, 0,  7,6, 9,1, "256 _KiB" },
	{ SGRADIOBUT, 0, 0,  7,7, 9,1, "512 Ki_B" },
	{ SGRADIOBUT, 0, 0, 18,4, 9,1, "  _1 MiB" },
	{ SGRADIOBUT, 0, 0, 18,5, 9,1, "  _2 MiB" },
	{ SGRADIOBUT, 0, 0, 18,6, 9,1, "2._5 MiB" },
	{ SGRADIOBUT, 0, 0, 18,7, 9,1, "  _4 MiB" },
	{ SGRADIOBUT, 0, 0, 29,4, 9,1, " _8 MiB" },
	{ SGRADIOBUT, 0, 0, 29,5, 9,1, "1_0 MiB" },
	{ SGRADIOBUT, 0, 0, 29,6, 9,1, "14 _MiB" },
	{ SGTEXT,     0, 0,  4,9,12,1, "TT-RAM size:" },
	{ SGBUTTON,   0, 0, 18,9, 1,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGTEXT,     0, 0, 20,9, 3,1, sTTRamSize },
	{ SGBUTTON,   0, 0, 24,9, 1,1, "\x03", SG_SHORTCUT_RIGHT },
	{ SGTEXT,     0, 0, 26,9,12,1, "MiB" },

	{ SGBOX,      0, 0,  1,12, 38,10, NULL },
	{ SGTEXT,     0, 0, 12,13, 17,1, "Memory state save" },
	{ SGTEXT,     0, 0,  2,15, 20,1, "Snap-shot file name:" },
	{ SGTEXT,     0, 0,  2,16, 36,1, dlgSnapShotName },
	{ SGBUTTON,   0, 0,  8,18, 10,1, "_Save" },
	{ SGBUTTON,   0, 0, 22,18, 10,1, "_Restore" },
	{ SGCHECKBOX, 0, 0,  2,20, 34,1, "_Load/save state at start-up/exit" },

	{ SGBUTTON, SG_DEFAULT, 0, 10,23, 20,1, "Back to main menu" },
	{ SGSTOP, 0, 0, 0,0, 0,0, NULL }
};



/**
 * Show and process the memory dialog.
 * @return  true if a memory snapshot has been loaded, false otherwise
 */
bool Dialog_MemDlg(void)
{
	int i, memsize;
	int but;

	SDLGui_CenterDlg(memorydlg);

	for (i = DLGMEM_256KB; i <= DLGMEM_14MB; i++)
	{
		memorydlg[i].state &= ~SG_SELECTED;
	}

	switch (ConfigureParams.Memory.STRamSize_KB)
	{
	 case 256:
		memorydlg[DLGMEM_256KB].state |= SG_SELECTED;
		break;
	 case 512:
		memorydlg[DLGMEM_512KB].state |= SG_SELECTED;
		break;
	 case 1024:
		memorydlg[DLGMEM_1MB].state |= SG_SELECTED;
		break;
	 case 2*1024:
		memorydlg[DLGMEM_2MB].state |= SG_SELECTED;
		break;
	 case 2*1024+512:
		memorydlg[DLGMEM_2_5MB].state |= SG_SELECTED;
		break;
	 case 4*1024:
		memorydlg[DLGMEM_4MB].state |= SG_SELECTED;
		break;
	 case 8*1024:
		memorydlg[DLGMEM_8MB].state |= SG_SELECTED;
		break;
	 case 10*1024:
		memorydlg[DLGMEM_10MB].state |= SG_SELECTED;
		break;
	 default:
		memorydlg[DLGMEM_14MB].state |= SG_SELECTED;
		break;
	}
	memsize = ConfigureParams.Memory.TTRamSize_KB/1024;
	if (memsize < DLG_TTRAM_MIN)
		memsize = DLG_TTRAM_MIN;
	else if (memsize > DLG_TTRAM_MAX)
		memsize = DLG_TTRAM_MAX;
	sprintf(sTTRamSize, "%3i", memsize);
	File_ShrinkName(dlgSnapShotName, ConfigureParams.Memory.szMemoryCaptureFileName, memorydlg[DLGMEM_FILENAME].w);


	if (ConfigureParams.Memory.bAutoSave)
		memorydlg[DLGMEM_AUTOSAVE].state |= SG_SELECTED;
	else
		memorydlg[DLGMEM_AUTOSAVE].state &= ~SG_SELECTED;

	do
	{
		but = SDLGui_DoDialog(memorydlg);

		switch (but)
		{
		 case DLGMEM_TTRAM_LESS:
			memsize = Opt_ValueAlignMinMax(memsize - DLG_TTRAM_STEP, DLG_TTRAM_STEP, DLG_TTRAM_MIN, DLG_TTRAM_MAX);
			sprintf(sTTRamSize, "%3i", memsize);
			break;
		 case DLGMEM_TTRAM_MORE:
			memsize = Opt_ValueAlignMinMax(memsize + DLG_TTRAM_STEP, DLG_TTRAM_STEP, DLG_TTRAM_MIN, DLG_TTRAM_MAX);
			sprintf(sTTRamSize, "%3i", memsize);
			break;

		 case DLGMEM_SAVE:              /* Save memory snap-shot */
			if (SDLGui_FileConfSelect("Save memory snapshot:", dlgSnapShotName,
			                          ConfigureParams.Memory.szMemoryCaptureFileName,
			                          memorydlg[DLGMEM_FILENAME].w, true))
			{
				MemorySnapShot_Capture(ConfigureParams.Memory.szMemoryCaptureFileName, true);
			}
			break;
		 case DLGMEM_RESTORE:           /* Load memory snap-shot */
			if (SDLGui_FileConfSelect("Load memory snapshot:", dlgSnapShotName,
			                          ConfigureParams.Memory.szMemoryCaptureFileName,
			                          memorydlg[DLGMEM_FILENAME].w, false))
			{
				MemorySnapShot_Restore(ConfigureParams.Memory.szMemoryCaptureFileName, true);
				return true;
			}
			break;
		}
	}
	while (but != DLGMEM_EXIT && but != SDLGUI_QUIT
	        && but != SDLGUI_ERROR && !bQuitProgram );

	/* Read new values from dialog: */

	if (memorydlg[DLGMEM_256KB].state & SG_SELECTED)
		ConfigureParams.Memory.STRamSize_KB = 256;
	else if (memorydlg[DLGMEM_512KB].state & SG_SELECTED)
		ConfigureParams.Memory.STRamSize_KB = 512;
	else if (memorydlg[DLGMEM_1MB].state & SG_SELECTED)
		ConfigureParams.Memory.STRamSize_KB = 1024;
	else if (memorydlg[DLGMEM_2MB].state & SG_SELECTED)
		ConfigureParams.Memory.STRamSize_KB = 2*1024;
	else if (memorydlg[DLGMEM_2_5MB].state & SG_SELECTED)
		ConfigureParams.Memory.STRamSize_KB = 2*1024+512;
	else if (memorydlg[DLGMEM_4MB].state & SG_SELECTED)
		ConfigureParams.Memory.STRamSize_KB = 4*1024;
	else if (memorydlg[DLGMEM_8MB].state & SG_SELECTED)
		ConfigureParams.Memory.STRamSize_KB = 8*1024;
	else if (memorydlg[DLGMEM_10MB].state & SG_SELECTED)
		ConfigureParams.Memory.STRamSize_KB = 10*1024;
	else
		ConfigureParams.Memory.STRamSize_KB = 14*1024;

	ConfigureParams.Memory.TTRamSize_KB = memsize*1024;

	ConfigureParams.Memory.bAutoSave = (memorydlg[DLGMEM_AUTOSAVE].state & SG_SELECTED);

	return false;
}
