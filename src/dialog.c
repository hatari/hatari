/*
  Hatari - dialog.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Code to handle our options dialog.
*/
const char Dialog_fileid[] = "Hatari dialog.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "change.h"
#include "dialog.h"
#include "log.h"
#include "sdlgui.h"
#include "screen.h"


/*-----------------------------------------------------------------------*/
/**
 * Open Property sheet Options dialog.
 * 
 * We keep all our configuration details in a structure called
 * 'ConfigureParams'. When we open our dialog we make a backup
 * of this structure. When the user finally clicks on 'OK',
 * we can compare and makes the necessary changes.
 * 
 * Return true if user chooses OK, or false if cancel!
 */
bool Dialog_DoProperty(void)
{
	bool bOKDialog;  /* Did user 'OK' dialog? */
	bool bForceReset;
	bool bLoadedSnapshot;
	CNF_PARAMS current;

#if WITH_SDL2
	bool bOldMouseMode = SDL_GetRelativeMouseMode();
	SDL_SetRelativeMouseMode(SDL_FALSE);
#endif

	Main_PauseEmulation(true);
	bForceReset = false;

	/* Copy details (this is so can restore if 'Cancel' dialog) */
	current = ConfigureParams;
	ConfigureParams.Screen.bFullScreen = bInFullScreen;
	bOKDialog = Dialog_MainDlg(&bForceReset, &bLoadedSnapshot);

#if WITH_SDL2
	SDL_SetRelativeMouseMode(bOldMouseMode);
#endif

	/* If a memory snapshot has been loaded, no further changes are required */
	if (bLoadedSnapshot)
	{
		Main_UnPauseEmulation();
		return true;
	}

	/* Check if reset is required and ask user if he really wants to continue then */
	if (bOKDialog && !bForceReset
	    && Change_DoNeedReset(&current, &ConfigureParams)
	    && ConfigureParams.Log.nAlertDlgLogLevel > LOG_FATAL) {
		bOKDialog = DlgAlert_Query("The emulated system must be "
		                           "reset to apply these changes. "
		                           "Apply changes now and reset "
		                           "the emulator?");
	}

	/* Copy details to configuration */
	if (bOKDialog) {
		Change_CopyChangedParamsToConfiguration(&current, &ConfigureParams, bForceReset);
	} else {
		ConfigureParams = current;
	}

	Main_UnPauseEmulation();

	if (bQuitProgram)
		Main_RequestQuit(0);

	return bOKDialog;
}
