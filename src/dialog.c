/*
  Hatari - dialog.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This is normal 'C' code to handle our options dialog. We keep all our
  configuration details in a structure called 'ConfigureParams'. When we
  open our dialog we make a backup of this structure. When the user finally
  clicks on 'OK', we can compare and makes the necessary changes.
*/
const char Dialog_rcsid[] = "Hatari $Id: dialog.c,v 1.69 2008-05-19 20:34:09 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "change.h"
#include "dialog.h"
#include "log.h"
#include "sdlgui.h"
#include "screen.h"

CNF_PARAMS DialogParams;   /* List of configuration for dialogs (so the user can also choose 'Cancel') */


/*-----------------------------------------------------------------------*/
/**
 * Open Property sheet Options dialog.
 * Return TRUE if user chooses OK, or FALSE if cancel!
 */
bool Dialog_DoProperty(void)
{
	bool bOKDialog;  /* Did user 'OK' dialog? */
	bool bForceReset;

	Main_PauseEmulation();

	/* Copy details to DialogParams (this is so can restore if 'Cancel' dialog) */
	ConfigureParams.Screen.bFullScreen = bInFullScreen;
	DialogParams = ConfigureParams;

	bForceReset = FALSE;

	bOKDialog = Dialog_MainDlg(&bForceReset);

	/* Check if reset is required and ask user if he really wants to continue then */
	if (bOKDialog && !bForceReset && Change_DoNeedReset(&DialogParams)
	    && ConfigureParams.Log.nAlertDlgLogLevel >= LOG_INFO) {
		bOKDialog = DlgAlert_Query("The emulated system must be "
		                           "reset to apply these changes. "
		                           "Apply changes now and reset "
		                           "the emulator?");
	}

	/* Copy details to configuration */
	if (bOKDialog) {
		Change_CopyChangedParamsToConfiguration(&DialogParams, bForceReset);
	}

	Main_UnPauseEmulation();

	if (bQuitProgram)
		Main_RequestQuit();

	return bOKDialog;
}


/*-----------------------------------------------------------------------*/
/**
 * Loads params from the configuration file into DialogParams
 */
void Dialog_LoadParams(void)
{
	CNF_PARAMS tmpParams;
	/* Configuration_Load uses the variables from ConfigureParams.
	 * That's why we have to temporarily back it up here */
	tmpParams = ConfigureParams;
	Configuration_Load(NULL);
	DialogParams = ConfigureParams;
	ConfigureParams = tmpParams;
}


/*-----------------------------------------------------------------------*/
/**
 * Saves params in DialogParams to the configuration file
 */
void Dialog_SaveParams(void)
{
	CNF_PARAMS tmpParams;
	/* Configuration_Save uses the variables from ConfigureParams.
	 * That's why we have to temporarily back it up here */
	tmpParams = ConfigureParams;
	ConfigureParams = DialogParams;
	Configuration_Save();
	ConfigureParams = tmpParams;
}
