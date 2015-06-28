/*
  Hatari - dlgMemory.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
const char DlgMemory_fileid[] = "Hatari dlgMemory.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "dialog.h"
#include "sdlgui.h"
#include "memorySnapShot.h"
#include "file.h"
#include "screen.h"
#include "options.h"


#define DLGMEM_512KB    4
#define DLGMEM_1MB      5
#define DLGMEM_2MB      6
#define DLGMEM_4MB      7
#define DLGMEM_8MB      8
#define DLGMEM_14MB     9
#define DLGMEM_TTRAM_LESS    11
#define DLGMEM_TTRAM_TEXT    12
#define DLGMEM_TTRAM_MORE    13
#define DLGMEM_FILENAME 17
#define DLGMEM_SAVE     18
#define DLGMEM_RESTORE  19
#define DLGMEM_AUTOSAVE 20
#define DLGMEM_EXIT     21


/* String for TT RAM size */
static char sTTRamSize[4];

#define DLG_TTRAM_STEP	4
#define DLG_TTRAM_MIN	0
#define DLG_TTRAM_MAX	256

static char dlgSnapShotName[36+1];


/* The memory dialog: */
static SGOBJ memorydlg[] =
{
	{ SGBOX, 0, 0, 0,0, 40,24, NULL },

	{ SGBOX, 0, 0, 1,1, 38,9, NULL },
	{ SGTEXT, 0, 0, 15,2, 12,1, "Memory setup" },
	{ SGTEXT, 0, 0, 4,4, 12,1, "ST-RAM size:" },
	{ SGRADIOBUT, 0, 0, 18,4, 9,1, "_512 KiB" },
	{ SGRADIOBUT, 0, 0, 18,5, 7,1, "_1 MiB" },
	{ SGRADIOBUT, 0, 0, 18,6, 7,1, "_2 MiB" },
	{ SGRADIOBUT, 0, 0, 29,4, 7,1, "_4 MiB" },
	{ SGRADIOBUT, 0, 0, 29,5, 7,1, "_8 MiB" },
	{ SGRADIOBUT, 0, 0, 29,6, 8,1, "14 _MiB" },
	{ SGTEXT,     0, 0,  4,8,12,1, "TT-RAM size:" },
	{ SGBUTTON,   0, 0, 18,8, 1,1, "\x04", SG_SHORTCUT_LEFT },
	{ SGTEXT,     0, 0, 20,8, 3,1, sTTRamSize },
	{ SGBUTTON,   0, 0, 24,8, 1,1, "\x03", SG_SHORTCUT_RIGHT },

	{ SGBOX,      0, 0,  1,11, 38,10, NULL },
	{ SGTEXT,     0, 0, 12,12, 17,1, "Memory state save" },
	{ SGTEXT,     0, 0,  2,14, 20,1, "Snap-shot file name:" },
	{ SGTEXT,     0, 0,  2,15, 36,1, dlgSnapShotName },
	{ SGBUTTON,   0, 0,  8,17, 10,1, "_Save" },
	{ SGBUTTON,   0, 0, 22,17, 10,1, "_Restore" },
	{ SGCHECKBOX, 0, 0,  2,19, 34,1, "_Load/save state at start-up/exit" },

	{ SGBUTTON, SG_DEFAULT, 0, 10,22, 20,1, "Back to main menu" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
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

	for (i = DLGMEM_512KB; i <= DLGMEM_14MB; i++)
	{
		memorydlg[i].state &= ~SG_SELECTED;
	}

	switch (ConfigureParams.Memory.nMemorySize)
	{
	 case 0:
		memorydlg[DLGMEM_512KB].state |= SG_SELECTED;
		break;
	 case 1:
		memorydlg[DLGMEM_1MB].state |= SG_SELECTED;
		break;
	 case 2:
		memorydlg[DLGMEM_2MB].state |= SG_SELECTED;
		break;
	 case 4:
		memorydlg[DLGMEM_4MB].state |= SG_SELECTED;
		break;
	 case 8:
		memorydlg[DLGMEM_8MB].state |= SG_SELECTED;
		break;
	 default:
		memorydlg[DLGMEM_14MB].state |= SG_SELECTED;
		break;
	}
	memsize = ConfigureParams.Memory.nTTRamSize;
#if ENABLE_WINUAE_CPU
	sprintf(sTTRamSize, "%3i", memsize);
#else
	strcpy(sTTRamSize, "N/A");
#endif
	File_ShrinkName(dlgSnapShotName, ConfigureParams.Memory.szMemoryCaptureFileName, memorydlg[DLGMEM_FILENAME].w);


	if (ConfigureParams.Memory.bAutoSave)
		memorydlg[DLGMEM_AUTOSAVE].state |= SG_SELECTED;
	else
		memorydlg[DLGMEM_AUTOSAVE].state &= ~SG_SELECTED;

	do
	{
		but = SDLGui_DoDialog(memorydlg, NULL, false);

		switch (but)
		{
#if ENABLE_WINUAE_CPU
		 case DLGMEM_TTRAM_LESS:
			memsize = Opt_ValueAlignMinMax(memsize - DLG_TTRAM_STEP, DLG_TTRAM_STEP, DLG_TTRAM_MIN, DLG_TTRAM_MAX);
			sprintf(sTTRamSize, "%3i", memsize);
			break;
		 case DLGMEM_TTRAM_MORE:
			memsize = Opt_ValueAlignMinMax(memsize + DLG_TTRAM_STEP, DLG_TTRAM_STEP, DLG_TTRAM_MIN, DLG_TTRAM_MAX);
			sprintf(sTTRamSize, "%3i", memsize);
			break;
#endif
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

	if (memorydlg[DLGMEM_512KB].state & SG_SELECTED)
		ConfigureParams.Memory.nMemorySize = 0;
	else if (memorydlg[DLGMEM_1MB].state & SG_SELECTED)
		ConfigureParams.Memory.nMemorySize = 1;
	else if (memorydlg[DLGMEM_2MB].state & SG_SELECTED)
		ConfigureParams.Memory.nMemorySize = 2;
	else if (memorydlg[DLGMEM_4MB].state & SG_SELECTED)
		ConfigureParams.Memory.nMemorySize = 4;
	else if (memorydlg[DLGMEM_8MB].state & SG_SELECTED)
		ConfigureParams.Memory.nMemorySize = 8;
	else
		ConfigureParams.Memory.nMemorySize = 14;

	ConfigureParams.Memory.nTTRamSize = memsize;
	ConfigureParams.Memory.bAutoSave = (memorydlg[DLGMEM_AUTOSAVE].state & SG_SELECTED);

	return false;
}
