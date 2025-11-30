/*
  Hatari - main.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Main initialization and event handling routines.
*/
const char Main_fileid[] = "Hatari main.c";

#include <time.h>

#include "main.h"

#if ENABLE_SDL3
#define SDL_ENABLE_OLD_NAMES 1
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "version.h"
#include "configuration.h"
#include "control.h"
#include "conv_st.h"
#include "options.h"
#include "dialog.h"
#include "event.h"
#include "audio.h"
#include "joy_ui.h"
#include "file.h"
#include "floppy.h"
#include "floppy_ipf.h"
#include "floppy_stx.h"
#include "gemdos.h"
#include "mfp.h"
#include "fdc.h"
#include "hdc.h"
#include "ide.h"
#include "acia.h"
#include "ikbd.h"
#include "ioMem.h"
#include "keymap.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "midi.h"
#include "ncr5380.h"
#include "nvram.h"
#include "paths.h"
#include "printer.h"
#include "reset.h"
#include "rs232.h"
#include "rtc.h"
#include "scc.h"
#include "screen.h"
#include "sdlgui.h"
#include "shortcut.h"
#include "sound.h"
#include "dmaSnd.h"
#include "statusbar.h"
#include "stMemory.h"
#include "str.h"
#include "timing.h"
#include "tos.h"
#include "video.h"
#include "avi_record.h"
#include "debugui.h"
#include "clocks_timings.h"
#include "utils.h"

#include "hatari-glue.h"

#include "falcon/dsp.h"
#include "falcon/videl.h"

#include "sdl/screen_sdl.h"

#ifdef WIN32
#include "gui-win/opencon.h"
#endif

bool bQuitProgram = false;                /* Flag to quit program cleanly */
static int nQuitValue;                    /* exit value */

bool bEmulationActive = true;             /* Run emulation when started */


/**
 * Pause emulation, stop sound.  'visualize' should be set true,
 * unless unpause will be called immediately afterwards.
 * 
 * @return true if paused now, false if was already paused
 */
bool Main_PauseEmulation(bool visualize)
{
	if ( !bEmulationActive )
		return false;

	Audio_EnableAudio(false);
	bEmulationActive = false;
	if (visualize)
	{
		Timing_PrintSpeed();

		Screen_StatusbarMessage("Emulation paused", 100);

		/* Un-grab mouse pointer if necessary, but keep old bGrabMouse state */
		if (bGrabMouse || bInFullScreen)
			bGrabMouse = Screen_UngrabMouse();
	}
	return true;
}


/**
 * Start/continue emulation
 * 
 * @return true if continued, false if was already running
 */
bool Main_UnPauseEmulation(void)
{
	if ( bEmulationActive )
		return false;

	Sound_BufferIndexNeedReset = true;
	Audio_EnableAudio(ConfigureParams.Sound.bEnableSound);
	bEmulationActive = true;

	/* Cause full screen update (to clear all) */
	ConvST_SetFullUpdate();

	/* Grab mouse pointer again */
	Screen_GrabMouseIfNecessary();

	return true;
}


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


/* ----------------------------------------------------------------------- */
/**
 * Set mouse cursor visibility and return if it was visible before.
 */
bool Main_ShowCursor(bool show)
{
	bool bOldVisibility;

#if ENABLE_SDL3
	bOldVisibility = SDL_CursorVisible();
	if (bOldVisibility != show)
	{
		if (show)
		{
			SDL_ShowCursor();
		}
		else
		{
			SDL_HideCursor();
		}
	}
#else
	bOldVisibility = SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
	if (bOldVisibility != show)
	{
		if (show)
		{
			SDL_ShowCursor(SDL_ENABLE);
		}
		else
		{
			SDL_ShowCursor(SDL_DISABLE);
		}
	}
#endif
	return bOldVisibility;
}


/**
 * Initialise emulation for some hardware components
 * It is required to init those parts before parsing the parameters
 * (for example, we should init FDC before inserting a disk and we
 * need to know valid joysticks before selecting default joystick IDs)
 */
static void Main_Init_HW(void)
{
	JoyUI_Init();
	FDC_Init();
	STX_Init();
	Video_InitTimings();
}

/*-----------------------------------------------------------------------*/
/**
 * Initialise emulation
 */
static void Main_Init(void)
{
	/* Open debug log file */
	if (!Log_Init())
	{
		Main_ErrorExit("Logging/tracing initialization failed", NULL, -1);
	}
	Log_Printf(LOG_INFO, PROG_NAME ", compiled on:  " __DATE__ ", " __TIME__ "\n");

	/* Init SDL's video subsystem. Note: Audio subsystem
	   will be initialized later (failure not fatal). */
#if ENABLE_SDL3
	if (!SDL_Init(SDL_INIT_VIDEO))
#else
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
#endif
	{
		Main_ErrorExit("Could not initialize the SDL library:", SDL_GetError(), -1);
	}

	if ( IPF_Init() != true )
	{
		Main_ErrorExit("Could not initialize the IPF support", NULL, -1);
	}

	ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
	Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );

	SDLGui_Init();
	Printer_Init();
	MFP_Init(MFP_Array);
	RS232_Init();
	SCC_Init();
	Midi_Init();
	Videl_Init();
	ConvST_Init();
	Screen_Init();

	STMemory_Init ( ConfigureParams.Memory.STRamSize_KB * 1024 );

	ACIA_Init( ACIA_Array , MachineClocks.ACIA_Freq , MachineClocks.ACIA_Freq );
	IKBD_Init();			/* After ACIA_Init */

	DSP_Init();
	Floppy_Init();
	M68000_Init();                /* Init CPU emulation */
	Audio_Init();
	Keymap_Init();

	/* Init HD emulation */
	HDC_Init();
	Ncr5380_Init();
	Ide_Init();
	GemDOS_Init();
	if (ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
		/* uses variables set by HDC_Init/Ncr5380_Init/Ide_Init */
		GemDOS_InitDrives();
	}

	if (Reset_Cold())             /* Reset all systems, load TOS image */
	{
		/* If loading of the TOS failed, we bring up the GUI to let the
		 * user choose another TOS ROM file. */
		Dialog_DoProperty();
	}
	if (!bTosImageLoaded || bQuitProgram)
	{
		if (!bTosImageLoaded)
		{
			Main_ErrorExit("Failed to load TOS image", NULL, -2);
		}
		SDL_Quit();
		exit(-2);
	}

	IoMem_Init();
	NvRam_Init();
	Sound_Init();
	Rtc_Init();

	/* done as last, needs CPU & DSP running... */
	DebugUI_Init();
}


/*-----------------------------------------------------------------------*/
/**
 * Un-Initialise emulation
 */
static void Main_UnInit(void)
{
	Screen_ReturnFromFullScreen();
	Floppy_UnInit();
	HDC_UnInit();
	Ncr5380_UnInit();
	Midi_UnInit();
	SCC_UnInit();
	RS232_UnInit();
	Printer_UnInit();
	IoMem_UnInit(ConfigureParams.System.nMachineType);
	NvRam_UnInit();
	GemDOS_UnInitDrives();
	Ide_UnInit();
	JoyUI_UnInit();
	if (Sound_AreWeRecording())
		Sound_EndRecording();
	Audio_UnInit();
	SDLGui_UnInit();
	DSP_UnInit();
	Screen_UnInit();
	ConvST_UnInit();
	Exit680x0();

	IPF_Exit();

	/* SDL uninit: */
	SDL_Quit();

	/* Close debug log file */
	DebugUI_UnInit();
	Log_UnInit();

	Paths_UnInit();
}


/*-----------------------------------------------------------------------*/
/**
 * Load initial configuration files. The global config file is skipped in
 * test mode (i.e. if the HATARI_TEST environment variable has been set),
 * so that the test has always full control over the configuration settings.
 */
static void Main_LoadInitialConfig(void)
{
	char *psGlobalConfig;

	if (getenv("HATARI_TEST"))
		psGlobalConfig = NULL;
	else
		psGlobalConfig = malloc(FILENAME_MAX);
	if (psGlobalConfig)
	{
		File_MakePathBuf(psGlobalConfig, FILENAME_MAX, CONFDIR,
		                 "hatari", "cfg");
		/* Try to load the global configuration file */
		Configuration_Load(psGlobalConfig);

		free(psGlobalConfig);
	}

	/* Now try the users configuration file */
	Configuration_Load(NULL);
	if (ConfigureParams.Keyboard.nLanguage == TOS_LANG_UNKNOWN)
		ConfigureParams.Keyboard.nLanguage = TOS_DefaultLanguage();
}

/*-----------------------------------------------------------------------*/
/**
 * Set TOS etc information and initial help message
 */
static void Main_StatusbarSetup(void)
{
	struct {
		const int id;
		bool mod;
		char *name;
	} keys[] = {
		{ SHORTCUT_OPTIONS, false, NULL },
		{ SHORTCUT_MOUSEGRAB, false, NULL }
	};
	const char *name;
	bool named;
	SDL_Keycode key;
	int i;

	named = false;
	for (i = 0; i < ARRAY_SIZE(keys); i++)
	{
		key = ConfigureParams.Shortcut.withoutModifier[keys[i].id];
		if (!key)
		{
			key = ConfigureParams.Shortcut.withModifier[keys[i].id];
			if (!key)
				continue;
			keys[i].mod = true;
		}
		name = SDL_GetKeyName(key);
		if (!name)
			continue;
		keys[i].name = Str_ToUpper(strdup(name));
		named = true;
	}
	if (named)
	{
		char message[60];
		snprintf(message, sizeof(message), "Press %s%s for Options, %s%s for mouse grab toggle",
			 keys[0].mod ? "AltGr+": "", keys[0].name,
			 keys[1].mod ? "AltGr+": "", keys[1].name);
		for (i = 0; i < ARRAY_SIZE(keys); i++)
		{
			if (keys[i].name)
				free(keys[i].name);
		}
		Statusbar_AddMessage(message, 5000);
	}
	/* update information loaded by Main_Init() */
	Statusbar_UpdateInfo();
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
 * Main
 * 
 * Note: 'argv' cannot be declared const, MinGW would then fail to link.
 */
int main(int argc, char *argv[])
{
	/* Generate random seed */
	Hatari_srand(time(NULL));

	/* Setup for string conversions */
	Str_Init();

	/* Logs default to stderr at start */
	Log_Default();

	/* Initialize event actions */
	Event_Init();

	/* Initialize directory strings */
	Paths_Init(argv[0]);

	/* Init some HW components before parsing the configuration / parameters */
	Main_Init_HW();

	/* Set default configuration values */
	Configuration_SetDefault();

	/* Now load the values from the configuration file */
	Main_LoadInitialConfig();

	/* Check for any passed parameters */
	int exitval;
	if (!Opt_ParseParameters(argc, (const char * const *)argv, &exitval))
	{
		Control_RemoveFifo();
		Main_ErrorExit(NULL, NULL, exitval);
	}
	/* monitor type option might require "reset" -> true */
	Configuration_Apply(true);

#ifdef WIN32
	Win_OpenCon();
#endif

#if HAVE_SETENV
	/* Needed on maemo but useful also with normal X11 window managers for
	 * window grouping when you have multiple Hatari SDL windows open */
	setenv("SDL_VIDEO_X11_WMCLASS", "hatari", 1);
#endif

	/* Init emulator system */
	Main_Init();

	/* Set initial Statusbar information */
	Main_StatusbarSetup();
	
	/* Check if Timing_Delay is accurate */
	Timing_CheckForAccurateDelays();

	/* Immediately start AVI recording ? */
	if ( AviRecordEnabled )
		Avi_StartRecording_WithConfig();

	/* Run emulation */
	Main_UnPauseEmulation();
	M68000_Start();                 /* Start emulation */

	Control_RemoveFifo();
	/* cleanly close the AVI file, if needed */
	Avi_StopRecording_WithMsg();
	/* Un-init emulation system */
	Main_UnInit();

	return nQuitValue;
}
