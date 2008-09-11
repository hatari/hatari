/*
  Hatari - main.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Main initialization and event handling routines.
*/
const char Opt_rcsid[] = "Hatari $Id: main.c,v 1.141 2008-09-11 20:38:38 thothy Exp $";

#include <time.h>
#include <SDL.h>

#include "config.h"
#include "main.h"
#include "configuration.h"
#include "control.h"
#include "options.h"
#include "dialog.h"
#include "audio.h"
#include "joy.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "ikbd.h"
#include "ioMem.h"
#include "keymap.h"
#include "log.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "midi.h"
#include "nvram.h"
#include "paths.h"
#include "printer.h"
#include "reset.h"
#include "rs232.h"
#include "screen.h"
#include "sdlgui.h"
#include "shortcut.h"
#include "sound.h"
#include "statusbar.h"
#include "stMemory.h"
#include "tos.h"
#include "video.h"

#include "hatari-glue.h"

#include "falcon/hostscreen.h"
#if ENABLE_DSP_EMU
#include "falcon/dsp.h"
#endif


bool bQuitProgram = FALSE;                /* Flag to quit program cleanly */
bool bEnableDebug = FALSE;                /* Enable debug UI? */
static bool bEmulationActive = TRUE;      /* Run emulation when started */
static bool bAccurateDelays;              /* Host system has an accurate SDL_Delay()? */
static bool bIgnoreNextMouseMotion = FALSE;  /* Next mouse motion will be ignored (needed after SDL_WarpMouse) */


/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
 */
void Main_MemorySnapShot_Capture(bool bSave)
{
	int nBytes;

	/* Save/Restore details */
	/* Only save/restore area of memory machine ie set to, eg 1Mb */
	if (bSave)
	{
		nBytes = STRamEnd;
		MemorySnapShot_Store(&nBytes, sizeof(nBytes));
		MemorySnapShot_Store(STRam, nBytes);
	}
	else
	{
		MemorySnapShot_Store(&nBytes, sizeof(nBytes));
		MemorySnapShot_Store(STRam, nBytes);
	}
	/* And Cart/TOS/Hardware area */
	MemorySnapShot_Store(&RomMem[0xE00000], 0x200000);
}


/*-----------------------------------------------------------------------*/
/**
 * Pause emulation, stop sound
 */
void Main_PauseEmulation(void)
{
	if ( bEmulationActive )
	{
		Audio_EnableAudio(FALSE);
		bEmulationActive = FALSE;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Start emulation
 */
void Main_UnPauseEmulation(void)
{
	if ( !bEmulationActive )
	{
		Sound_ResetBufferIndex();
		Audio_EnableAudio(ConfigureParams.Sound.bEnableSound);
		Screen_SetFullUpdate();       /* Cause full screen update (to clear all) */

		bEmulationActive = TRUE;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Optionally ask user whether to quit and set bQuitProgram accordingly
 */
void Main_RequestQuit(void)
{
	if (ConfigureParams.Memory.bAutoSave)
	{
		bQuitProgram = TRUE;
		MemorySnapShot_Capture(ConfigureParams.Memory.szAutoSaveFileName, FALSE);
	}
	else if (ConfigureParams.Log.bConfirmQuit)
	{
		bQuitProgram = FALSE;	/* if set TRUE, dialog exits */
		bQuitProgram = DlgAlert_Query("All unsaved data will be lost.\nDo you really want to quit?");
	}
	else
	{
		bQuitProgram = TRUE;
	}

	if (bQuitProgram)
	{
		/* Assure that CPU core shuts down */
		M68000_SetSpecial(SPCFLAG_BRK);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * This function waits on each emulated VBL to synchronize the real time
 * with the emulated ST.
 * Unfortunately SDL_Delay and other sleep functions like usleep or nanosleep
 * are very inaccurate on some systems like Linux 2.4 or Mac OS X (they can only
 * wait for a multiple of 10ms due to the scheduler on these systems), so we have
 * to "busy wait" there to get an accurate timing.
 */
void Main_WaitOnVbl(void)
{
	int nCurrentMilliTicks;
	static int nDestMilliTicks = 0;
	int nFrameDuration;
	signed int nDelay;

	nCurrentMilliTicks = SDL_GetTicks();

	nFrameDuration = 1000/nScreenRefreshRate;
	nDelay = nDestMilliTicks - nCurrentMilliTicks;

	/* Do not wait if we are in fast forward mode or if we are totally out of sync */
	if (ConfigureParams.System.bFastForward == TRUE
	        || nDelay < -4*nFrameDuration)
	{
		if (nFrameSkips < ConfigureParams.Screen.nFrameSkips)
		{
			nFrameSkips += 1;
			printf("Increased frameskip to %d\n", nFrameSkips);
		}
		/* Only update nDestMilliTicks for next VBL */
		nDestMilliTicks = nCurrentMilliTicks + nFrameDuration;
		return;
	}
	/* If automatic frameskip is enabled and delay's more than twice
	 * the effect of single frameskip, decrease frameskip
	 */
	if (nFrameSkips > 0
	    && ConfigureParams.Screen.nFrameSkips >= AUTO_FRAMESKIP_LIMIT
	    && 2*nDelay > nFrameDuration/nFrameSkips)
	{
		nFrameSkips -= 1;
		printf("Decreased frameskip to %d\n", nFrameSkips);
	}

	if (bAccurateDelays)
	{
		/* Accurate sleeping is possible -> use SDL_Delay to free the CPU */
		if (nDelay > 1)
			SDL_Delay(nDelay - 1);
	}
	else
	{
		/* No accurate SDL_Delay -> only wait if more than 5ms to go... */
		if (nDelay > 5)
			SDL_Delay(nDelay<10 ? nDelay-1 : 9);
	}

	/* Now busy-wait for the right tick: */
	while (nDelay > 0)
	{
		nCurrentMilliTicks = SDL_GetTicks();
		nDelay = nDestMilliTicks - nCurrentMilliTicks;
	}

	/* Update nDestMilliTicks for next VBL */
	nDestMilliTicks += nFrameDuration;
}


/*-----------------------------------------------------------------------*/
/**
 * Since SDL_Delay and friends are very inaccurate on some systems, we have
 * to check if we can rely on this delay function.
 */
static void Main_CheckForAccurateDelays(void)
{
	int nStartTicks, nEndTicks;

	/* Force a task switch now, so we have a longer timeslice afterwards */
	SDL_Delay(10);

	nStartTicks = SDL_GetTicks();
	SDL_Delay(1);
	nEndTicks = SDL_GetTicks();

	/* If the delay took longer than 10ms, we are on an inaccurate system! */
	bAccurateDelays = ((nEndTicks - nStartTicks) < 9);

	if (bAccurateDelays)
		Log_Printf(LOG_DEBUG, "Host system has accurate delays. (%d)\n", nEndTicks - nStartTicks);
	else
		Log_Printf(LOG_WARN, "Host system does not have accurate delays. (%d)\n", nEndTicks - nStartTicks);
}


/* ----------------------------------------------------------------------- */
/**
 * Set mouse pointer to new coordinates and set flag to ignore the mouse event
 * that is generated by SDL_WarpMouse().
 */
void Main_WarpMouse(int x, int y)
{
	SDL_WarpMouse(x, y);                  /* Set mouse pointer to new position */
	bIgnoreNextMouseMotion = TRUE;        /* Ignore mouse motion event from SDL_WarpMouse */
}


/* ----------------------------------------------------------------------- */
/**
 * Handle mouse motion event.
 */
static void Main_HandleMouseMotion(SDL_Event *pEvent)
{
	int dx, dy;
	static int ax = 0, ay = 0;


	if (bIgnoreNextMouseMotion)
	{
		bIgnoreNextMouseMotion = FALSE;
		return;
	}

	dx = pEvent->motion.xrel;
	dy = pEvent->motion.yrel;

	/* In zoomed low res mode, we divide dx and dy by the zoom factor so that
	 * the ST mouse cursor stays in sync with the host mouse. However, we have
	 * to take care of lowest bit of dx and dy which will get lost when
	 * dividing. So we store these bits in ax and ay and add them to dx and dy
	 * the next time. */
	if (nScreenZoomX != 1)
	{
		dx += ax;
		ax = dx % nScreenZoomX;
		dx /= nScreenZoomX;
	}
	if (nScreenZoomY != 1)
	{
		dy += ay;
		ay = dy % nScreenZoomY;
		dy /= nScreenZoomY;
	}

	KeyboardProcessor.Mouse.dx += dx;
	KeyboardProcessor.Mouse.dy += dy;
}


/* ----------------------------------------------------------------------- */
/**
 * SDL message handler.
 * Here we process the SDL events (keyboard, mouse, ...) and map it to
 * Atari IKBD events.
 */
void Main_EventHandler(void)
{
	SDL_Event event;

	/* check remote process control */
	Control_CheckUpdates();
	
	if (SDL_PollEvent(&event))
	{
		switch (event.type)
		{

		 case SDL_QUIT:
			Main_RequestQuit();
			break;

		 case SDL_MOUSEMOTION:               /* Read/Update internal mouse position */
			Main_HandleMouseMotion(&event);
			break;

		 case SDL_MOUSEBUTTONDOWN:
			if (event.button.button == SDL_BUTTON_LEFT)
			{
				if (Keyboard.LButtonDblClk == 0)
					Keyboard.bLButtonDown |= BUTTON_MOUSE;  /* Set button down flag */
			}
			else if (event.button.button == SDL_BUTTON_RIGHT)
			{
				Keyboard.bRButtonDown |= BUTTON_MOUSE;
			}
			else if (event.button.button == SDL_BUTTON_MIDDLE)
			{
				/* Start double-click sequence in emulation time */
				Keyboard.LButtonDblClk = 1;
			}
			else if (event.button.button == SDL_BUTTON_WHEELDOWN)
			{
				/* Simulate pressing the "cursor down" key */
				IKBD_PressSTKey(0x50, TRUE);
			}
			else if (event.button.button == SDL_BUTTON_WHEELUP)
			{
				/* Simulate pressing the "cursor up" key */
				IKBD_PressSTKey(0x48, TRUE);
			}
			break;

		 case SDL_MOUSEBUTTONUP:
			if (event.button.button == SDL_BUTTON_LEFT)
			{
				Keyboard.bLButtonDown &= ~BUTTON_MOUSE;
			}
			else if (event.button.button == SDL_BUTTON_RIGHT)
			{
				Keyboard.bRButtonDown &= ~BUTTON_MOUSE;
			}
			else if (event.button.button == SDL_BUTTON_WHEELDOWN)
			{
				/* Simulate releasing the "cursor down" key */
				IKBD_PressSTKey(0x50, FALSE);
			}
			else if (event.button.button == SDL_BUTTON_WHEELUP)
			{
				/* Simulate releasing the "cursor up" key */
				IKBD_PressSTKey(0x48, FALSE);
			}
			break;

		 case SDL_KEYDOWN:
			Keymap_KeyDown(&event.key.keysym);
			break;

		 case SDL_KEYUP:
			Keymap_KeyUp(&event.key.keysym);
			break;
		}
	}
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
		fprintf(stderr, "Logging/tracing initialization failed");
		exit(-1);
	}
	Log_Printf(LOG_INFO, PROG_NAME ", compiled on:  " __DATE__ ", " __TIME__ "\n");

	/* Init SDL's video subsystem. Note: Audio and joystick subsystems
	   will be initialized later (failures there are not fatal). */
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		fprintf(stderr, "Could not initialize the SDL library:\n %s\n", SDL_GetError() );
		exit(-1);
	}

	SDLGui_Init();
	Printer_Init();
	RS232_Init();
	Midi_Init();
	Screen_Init();
	HostScreen_Init();
#if ENABLE_DSP_EMU
	if (ConfigureParams.System.nDSPType == DSP_TYPE_EMU)
	{
		DSP_Init();
	}
#endif
	Floppy_Init();
	Init680x0();                  /* Init CPU emulation */
	Audio_Init();
	Keymap_Init();

	/* Init HD emulation */
	if (ConfigureParams.HardDisk.bUseHardDiskImage)
	{
		char *szHardDiskImage = ConfigureParams.HardDisk.szHardDiskImage;
		if (HDC_Init(szHardDiskImage))
			printf("Hard drive image %s mounted.\n", szHardDiskImage);
		else
			printf("Couldn't open HD file: %s, or no partitions\n", szHardDiskImage);
	}
	GemDOS_Init();
	if (ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
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
		fprintf(stderr, "Failed to load TOS image!\n");
		SDL_Quit();
		exit(-2);
	}

	IoMem_Init();
	NvRam_Init();
	Joy_Init();
	Sound_Init();
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
	Midi_UnInit();
	RS232_UnInit();
	Printer_UnInit();
	IoMem_UnInit();
	NvRam_UnInit();
	GemDOS_UnInitDrives();
	Joy_UnInit();
	if (Sound_AreWeRecording())
		Sound_EndRecording();
	Audio_UnInit();
	SDLGui_UnInit();
#if ENABLE_DSP_EMU
	if (ConfigureParams.System.nDSPType == DSP_TYPE_EMU)
	{
		DSP_UnInit();
	}
#endif
	HostScreen_UnInit();
	Screen_UnInit();
	Exit680x0();

	/* SDL uninit: */
	SDL_Quit();

	/* Close debug log file */
	Log_UnInit();
}


/*-----------------------------------------------------------------------*/
/**
 * Load initial configuration file(s)
 */
static void Main_LoadInitialConfig(void)
{
	char *psGlobalConfig;

	psGlobalConfig = malloc(FILENAME_MAX);
	if (psGlobalConfig)
	{
#if defined(__AMIGAOS4__)
		strncpy(psGlobalConfig, CONFDIR"hatari.cfg", FILENAME_MAX);
#else
		snprintf(psGlobalConfig, FILENAME_MAX, CONFDIR"%chatari.cfg", PATHSEP);
#endif
		/* Try to load the global configuration file */
		Configuration_Load(psGlobalConfig);

		free(psGlobalConfig);
	}

	/* Now try the users configuration file */
	Configuration_Load(NULL);
}


/*-----------------------------------------------------------------------*/
/**
 * Main
 * 
 * Note: 'argv' cannot be declared const, MinGW would then fail to link.
 */
int main(int argc, char *argv[])
{
	/* Generate random seed */
	srand(time(NULL));

	/* Initialize directory strings */
	Paths_Init(argv[0]);

	/* Set default configuration values: */
	Configuration_SetDefault();

	/* Now load the values from the configuration file */
	Main_LoadInitialConfig();

	/* Check for any passed parameters */
	if (!Opt_ParseParameters(argc, (const char**)argv))
	{
		return 1;
	}
	/* monitor type option might require "reset" -> TRUE */
	Configuration_Apply(TRUE);

#ifdef WIN32
	Win_OpenCon();
#endif

	/* Needed on maemo but useful also with normal X11 window managers
	 * for window grouping when you have multiple Hatari SDL windows open
	 */
#if HAVE_SETENV
	setenv("SDL_VIDEO_X11_WMCLASS", "hatari", 1);
#endif

	/* queue a message for user */
	if (ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS] == SDLK_F12) {
		Statusbar_AddMessage("Press F12 for Options", 8);
	}

	/* Init emulator system */
	Main_Init();

	/* update TOS information etc loaded by Main_Init() */
	Statusbar_UpdateInfo();
	
	/* Check if SDL_Delay is accurate */
	Main_CheckForAccurateDelays();

	/* Run emulation */
	Main_UnPauseEmulation();
	M68000_Start();                 /* Start emulation */

	/* Un-init emulation system */
	Main_UnInit();

	return 0;
}
