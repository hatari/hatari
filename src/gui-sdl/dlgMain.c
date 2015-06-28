/*
  Hatari - dlgMain.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  The main dialog.
*/
const char DlgMain_fileid[] = "Hatari dlgMain.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "dialog.h"
#include "sdlgui.h"
#include "screen.h"


#define MAINDLG_ABOUT    2
#define MAINDLG_SYSTEM   3
#define MAINDLG_ROM      4
#define MAINDLG_MEMORY   5
#define MAINDLG_FLOPPYS  6
#define MAINDLG_HARDDISK 7
#define MAINDLG_MONITOR  8
#define MAINDLG_WINDOW   9
#define MAINDLG_JOY      10
#define MAINDLG_KEYBD    11
#define MAINDLG_DEVICES  12
#define MAINDLG_SOUND    13
#define MAINDLG_LOADCFG  14
#define MAINDLG_SAVECFG  15
#define MAINDLG_NORESET  16
#define MAINDLG_RESET    17
#define MAINDLG_OK       18
#define MAINDLG_QUIT     19
#define MAINDLG_CANCEL   20


/* The main dialog: */
static SGOBJ maindlg[] =
{
	{ SGBOX, 0, 0, 0,0, 50,19, NULL },
	{ SGTEXT, 0, 0, 17,1, 16,1, "Hatari main menu" },
	{ SGBUTTON, 0, 0,  2, 4, 13,1, "A_bout" },
	{ SGBUTTON, 0, 0,  2, 6, 13,1, "S_ystem" },
	{ SGBUTTON, 0, 0,  2, 8, 13,1, "_ROM" },
	{ SGBUTTON, 0, 0,  2,10, 13,1, "_Memory" },
	{ SGBUTTON, 0, 0, 17, 4, 16,1, "_Floppy disks" },
	{ SGBUTTON, 0, 0, 17, 6, 16,1, "Hard _disks" },
	{ SGBUTTON, 0, 0, 17, 8, 16,1, "_Atari screen" },
	{ SGBUTTON, 0, 0, 17,10, 16,1, "_Hatari screen" },
	{ SGBUTTON, 0, 0, 35, 4, 13,1, "_Joysticks" },
	{ SGBUTTON, 0, 0, 35, 6, 13,1, "_Keyboard" },
	{ SGBUTTON, 0, 0, 35, 8, 13,1, "D_evices" },
	{ SGBUTTON, 0, 0, 35,10, 13,1, "S_ound" },
	{ SGBUTTON, 0, 0,  7,13, 16,1, "_Load config." },
	{ SGBUTTON, 0, 0, 27,13, 16,1, "_Save config." },
	{ SGRADIOBUT, 0, 0, 3,15, 10,1, "_No Reset" },
	{ SGRADIOBUT, 0, 0, 3,17, 15,1, "Reset ma_chine" },
	{ SGBUTTON, SG_DEFAULT, 0, 21,15, 8,3, "OK" },
	{ SGBUTTON, 0, 0, 36,15, 10,1, "_Quit" },
	{ SGBUTTON, SG_CANCEL, 0, 36,17, 10,1, "Cancel" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/**
 * This functions sets up the actual font and then displays the main dialog.
 */
int Dialog_MainDlg(bool *bReset, bool *bLoadedSnapshot)
{
	int retbut;
	bool bOldMouseVisibility;
	int nOldMouseX, nOldMouseY;
	char *psNewCfg;

	*bReset = false;
	*bLoadedSnapshot = false;

	if (SDLGui_SetScreen(sdlscrn))
		return false;

	SDL_GetMouseState(&nOldMouseX, &nOldMouseY);
	bOldMouseVisibility = SDL_ShowCursor(SDL_QUERY);
	SDL_ShowCursor(SDL_ENABLE);

	SDLGui_CenterDlg(maindlg);

	maindlg[MAINDLG_NORESET].state |= SG_SELECTED;
	maindlg[MAINDLG_RESET].state &= ~SG_SELECTED;

	do
	{
		retbut = SDLGui_DoDialog(maindlg, NULL, false);
		switch (retbut)
		{
		 case MAINDLG_ABOUT:
			Dialog_AboutDlg();
			break;
		 case MAINDLG_FLOPPYS:
			DlgFloppy_Main();
			break;
		 case MAINDLG_HARDDISK:
			DlgHardDisk_Main();
			break;
		 case MAINDLG_ROM:
			DlgRom_Main();
			break;
		 case MAINDLG_MONITOR:
			Dialog_MonitorDlg();
			break;
		 case MAINDLG_WINDOW:
			Dialog_WindowDlg();
			break;
		 case MAINDLG_SYSTEM:
			Dialog_SystemDlg();
			break;
		 case MAINDLG_MEMORY:
			if (Dialog_MemDlg())
			{
				/* Memory snapshot has been loaded - leave GUI immediately */
				*bLoadedSnapshot = true;
				SDL_ShowCursor(bOldMouseVisibility);
				Main_WarpMouse(nOldMouseX, nOldMouseY, true);
				return true;
			}
			break;
		 case MAINDLG_JOY:
			Dialog_JoyDlg();
			break;
		 case MAINDLG_KEYBD:
			Dialog_KeyboardDlg();
			break;
		 case MAINDLG_DEVICES:
			Dialog_DeviceDlg();
			break;
		 case MAINDLG_SOUND:
			Dialog_SoundDlg();
			break;
		 case MAINDLG_LOADCFG:
			psNewCfg = SDLGui_FileSelect("Load configuration:", sConfigFileName, NULL, false);
			if (psNewCfg)
			{
				strcpy(sConfigFileName, psNewCfg);
				Configuration_Load(NULL);
				free(psNewCfg);
			}
			break;
		 case MAINDLG_SAVECFG:
			psNewCfg = SDLGui_FileSelect("Save configuration:", sConfigFileName, NULL, true);
			if (psNewCfg)
			{
				strcpy(sConfigFileName, psNewCfg);
				Configuration_Save();
				free(psNewCfg);
			}
			break;
		 case MAINDLG_QUIT:
			bQuitProgram = true;
			break;
		}
	}
	while (retbut != MAINDLG_OK && retbut != MAINDLG_CANCEL && retbut != SDLGUI_QUIT
	        && retbut != SDLGUI_ERROR && !bQuitProgram);


	if (maindlg[MAINDLG_RESET].state & SG_SELECTED)
		*bReset = true;

	SDL_ShowCursor(bOldMouseVisibility);
	Main_WarpMouse(nOldMouseX, nOldMouseY, true);

	return (retbut == MAINDLG_OK);
}
