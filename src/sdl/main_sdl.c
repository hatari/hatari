/*
  Hatari - main_sdl.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Main functions (related to building Hatari as SDL application)
*/

#include "main.h"

#if ENABLE_SDL3
# include <SDL3/SDL.h>
# if defined(__APPLE__)
#  define main SDL_main   /* See SDLMain.m */
# endif
#else
# include <SDL.h>
#endif

#include "avi_record.h"
#include "configuration.h"
#include "dialog.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "options.h"
#include "screen_sdl.h"
#include "utils.h"

#ifdef WIN32
#include "../gui-win/opencon.h"
#endif


static int nQuitValue;                    /* exit value */

/**
 * Optionally ask user whether to quit and set bQuitProgram accordingly
 */
void Main_RequestQuit(int exitval)
{
	if (ConfigureParams.Memory.bAutoSave)
	{
		bQuitProgram = true;
		MemorySnapShot_Capture(ConfigureParams.Memory.szAutoSaveFileName, false);
	}
	else if (ConfigureParams.Log.bConfirmQuit)
	{
		bQuitProgram = false;	/* if set true, dialog exits */
		bQuitProgram = DlgAlert_Query("All unsaved data will be lost.\nDo you really want to quit?");
	}
	else
	{
		bQuitProgram = true;
	}

	if (bQuitProgram)
	{
		/* Assure that CPU core shuts down */
		M68000_SetSpecial(SPCFLAG_BRK);
	}
	nQuitValue = exitval;
}

/**
 * Set exit value and enable quit flag
 */
void Main_SetQuitValue(int exitval)
{
	bQuitProgram = true;
	M68000_SetSpecial(SPCFLAG_BRK);
	nQuitValue = exitval;
}


/**
 * Error exit wrapper, to make sure user sees the error messages
 * also on Windows.
 *
 * If message is given, Windows console is opened to show it,
 * otherwise it's assumed to be already open and relevant
 * messages shown before calling this.
 *
 * User input is waited on Windows, to make sure user sees
 * the message before console closes.
 *
 * Value overrides nQuitValue as process exit/return value.
 */
void Main_ErrorExit(const char *msg1, const char *msg2, int errval)
{
	if (msg1)
	{
#ifdef WIN32
		Win_ForceCon();
#endif
		if (msg2)
			fprintf(stderr, "ERROR: %s\n\t%s\n", msg1, msg2);
		else
			fprintf(stderr, "ERROR: %s!\n", msg1);
	}

	SDL_Quit();

#ifdef WIN32
	fputs("<press Enter to exit>\n", stderr);
	(void)fgetc(stdin);
#endif
	exit(errval);
}


/**
 * Main function
 * 
 * Note: 'argv' cannot be declared const, MinGW would then fail to link.
 */
int main(int argc, char *argv[])
{
	/* Generate random seed */
	Hatari_srand(time(NULL));

	Main_Init(argc, argv);

	/* Immediately start AVI recording ? */
	if ( AviRecordEnabled )
		Avi_StartRecording_WithConfig();

	/* Run emulation */
	Main_UnPauseEmulation();
	M68000_Start();                 /* Start emulation */

	Main_UnInit();

	SDL_Quit();

	return nQuitValue;
}
