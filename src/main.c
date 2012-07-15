/*
  Hatari - main.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Main initialization and event handling routines.
*/
const char Main_fileid[] = "Hatari main.c : " __DATE__ " " __TIME__;

#include <time.h>
#include <errno.h>
#include <SDL.h>

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
#include "ide.h"
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
#include "resolution.h"
#include "rs232.h"
#include "screen.h"
#include "sdlgui.h"
#include "shortcut.h"
#include "sound.h"
#include "dmaSnd.h"
#include "statusbar.h"
#include "stMemory.h"
#include "str.h"
#include "tos.h"
#include "video.h"
#include "avi_record.h"
#include "debugui.h"
#include "clocks_timings.h"

#include "hatari-glue.h"

#include "falcon/hostscreen.h"
#include "falcon/dsp.h"

#if HAVE_GETTIMEOFDAY
#include <sys/time.h>
#endif

#ifdef HAVE_CAPSIMAGE
#include <caps/capsimage.h>
#endif


bool bQuitProgram = false;                /* Flag to quit program cleanly */

static Uint32 nRunVBLs;                   /* Whether and how many VBLS to run before exit */
static Uint32 nFirstMilliTick;            /* Ticks when VBL counting started */
static Uint32 nVBLCount;                  /* Frame count */

static bool bEmulationActive = true;      /* Run emulation when started */
static bool bAccurateDelays;              /* Host system has an accurate SDL_Delay()? */
static bool bIgnoreNextMouseMotion = false;  /* Next mouse motion will be ignored (needed after SDL_WarpMouse) */


/*-----------------------------------------------------------------------*/
/**
 * Return current time as millisecond for performance measurements.
 * 
 * (On Unix only time spent by Hatari itself is counted, on other
 * platforms less accurate SDL "wall clock".)
 */
#if HAVE_SYS_TIMES_H
#include <unistd.h>
#include <sys/times.h>
static Uint32 Main_GetTicks(void)
{
	static unsigned int ticks_to_msec = 0;
	struct tms fields;
	if (!ticks_to_msec)
	{
		ticks_to_msec = sysconf(_SC_CLK_TCK);
		printf("OS clock ticks / second: %d\n", ticks_to_msec);
		/* Linux has 100Hz virtual clock so no accuracy loss there */
		ticks_to_msec = 1000UL / ticks_to_msec;
	}
	/* return milliseconds (clock ticks) spent in this process
	 */
	times(&fields);
	return ticks_to_msec * fields.tms_utime;
}
#else
# warning "times() function missing, using inaccurate SDL_GetTicks() instead."
# define Main_GetTicks SDL_GetTicks
#endif


//#undef HAVE_GETTIMEOFDAY
//#undef HAVE_NANOSLEEP

/*-----------------------------------------------------------------------*/
/**
 * Return a time counter in micro seconds.
 * If gettimeofday is available, we use it directly, else we convert the
 * return of SDL_GetTicks in micro sec.
 */

static Sint64	Time_GetTicks ( void )
{
        Sint64		ticks_micro;

#if HAVE_GETTIMEOFDAY
        struct timeval	now;
        gettimeofday ( &now , NULL );
        ticks_micro = (Sint64)now.tv_sec * 1000000 + now.tv_usec;
#else
	ticks_micro = (Sint64)SDL_GetTicks() * 1000;		/* milli sec -> micro sec */
#endif

	return ticks_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Sleep for a given number of micro seconds.
 * If nanosleep is available, we use it directly, else we use SDL_Delay
 * (which is portable, but less accurate as is uses milli-seconds)
 */

static void	Time_Delay ( Sint64 ticks_micro )
{
#if HAVE_NANOSLEEP
	struct timespec	ts;
	int		ret;
	ts.tv_sec = ticks_micro / 1000000;
	ts.tv_nsec = (ticks_micro % 1000000) * 1000;	/* micro sec -> nano sec */
	/* wait until all the delay is elapsed, including possible interruptions by signals */
	do
	{
                errno = 0;
                ret = nanosleep(&ts, &ts);
	} while ( ret && ( errno == EINTR ) );		/* keep on sleeping if we were interrupted */
#else
	SDL_Delay ( (Uint32)(ticks_micro / 1000) ) ;	/* micro sec -> milli sec */
#endif
}


/*-----------------------------------------------------------------------*/
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
		if (nFirstMilliTick)
		{
			int interval = Main_GetTicks() - nFirstMilliTick;
			static float previous;
			float current;

			current = (1000.0 * nVBLCount) / interval;
			printf("SPEED: %.1f VBL/s (%d/%.1fs), diff=%.1f%%\n",
			       current, nVBLCount, interval/1000.0,
			       previous>0.0 ? 100*(current-previous)/previous : 0.0);
			nVBLCount = nFirstMilliTick = 0;
			previous = current;
		}
		
		Statusbar_AddMessage("Emulation paused", 100);
		/* make sure msg gets shown */
		Statusbar_Update(sdlscrn);

		if (bGrabMouse && !bInFullScreen)
			/* Un-grab mouse pointer in windowed mode */
			SDL_WM_GrabInput(SDL_GRAB_OFF);
	}
	return true;
}

/*-----------------------------------------------------------------------*/
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
	Screen_SetFullUpdate();

	if (bGrabMouse)
		/* Grab mouse pointer again */
		SDL_WM_GrabInput(SDL_GRAB_ON);
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Optionally ask user whether to quit and set bQuitProgram accordingly
 */
void Main_RequestQuit(void)
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
}

/*-----------------------------------------------------------------------*/
/**
 * Set how many VBLs Hatari should run, from the moment this function
 * is called.
 */
void Main_SetRunVBLs(Uint32 vbls)
{
	fprintf(stderr, "Exit after %d VBLs.\n", vbls);
	nRunVBLs = vbls;
	nVBLCount = 0;
}

/*-----------------------------------------------------------------------*/
/**
 * This function waits on each emulated VBL to synchronize the real time
 * with the emulated ST.
 * Unfortunately SDL_Delay and other sleep functions like usleep or nanosleep
 * are very inaccurate on some systems like Linux 2.4 or Mac OS X (they can only
 * wait for a multiple of 10ms due to the scheduler on these systems), so we have
 * to "busy wait" there to get an accurate timing.
 * All times are expressed as micro seconds, to avoid too much rounding error.
 */
void Main_WaitOnVbl(void)
{
	Sint64 CurrentTicks;
	static Sint64 DestTicks = 0;
	Sint64 FrameDuration_micro;
	Sint64 nDelay;

	nVBLCount++;
	if (nRunVBLs &&	nVBLCount >= nRunVBLs)
	{
		/* show VBLs/s */
		Main_PauseEmulation(true);
		exit(0);
	}

//	FrameDuration_micro = (Sint64) ( 1000000.0 / nScreenRefreshRate + 0.5 );	/* round to closest integer */
	FrameDuration_micro = ClocksTimings_GetVBLDuration_micro ( ConfigureParams.System.nMachineType , nScreenRefreshRate );
	CurrentTicks = Time_GetTicks();

	if ( DestTicks == 0 )					/* first call, init DestTicks */
		DestTicks = CurrentTicks + FrameDuration_micro;

	DestTicks += pulse_swallowing_count; /* audio.c - Audio_CallBack() */

	nDelay = DestTicks - CurrentTicks;

	/* Do not wait if we are in fast forward mode or if we are totally out of sync */
	if (ConfigureParams.System.bFastForward == true
	        || nDelay < -4*FrameDuration_micro)
	{
		if (ConfigureParams.System.bFastForward == true)
		{
			if (!nFirstMilliTick)
				nFirstMilliTick = Main_GetTicks();
		}
		if (nFrameSkips < ConfigureParams.Screen.nFrameSkips)
		{
			nFrameSkips += 1;
			// Log_Printf(LOG_DEBUG, "Increased frameskip to %d\n", nFrameSkips);
		}
		/* Only update DestTicks for next VBL */
		DestTicks = CurrentTicks + FrameDuration_micro;
		return;
	}
	/* If automatic frameskip is enabled and delay's more than twice
	 * the effect of single frameskip, decrease frameskip
	 */
	if (nFrameSkips > 0
	    && ConfigureParams.Screen.nFrameSkips >= AUTO_FRAMESKIP_LIMIT
	    && 2*nDelay > FrameDuration_micro/nFrameSkips)
	{
		nFrameSkips -= 1;
		// Log_Printf(LOG_DEBUG, "Decreased frameskip to %d\n", nFrameSkips);
	}

	if (bAccurateDelays)
	{
		/* Accurate sleeping is possible -> use SDL_Delay to free the CPU */
		if (nDelay > 1000)
			Time_Delay(nDelay - 1000);
	}
	else
	{
		/* No accurate SDL_Delay -> only wait if more than 5ms to go... */
		if (nDelay > 5000)
			Time_Delay(nDelay<10000 ? nDelay-1000 : 9000);
	}

	/* Now busy-wait for the right tick: */
	while (nDelay > 0)
	{
		CurrentTicks = Time_GetTicks();
		nDelay = DestTicks - CurrentTicks;
	}

//printf ( "tick %lld\n" , CurrentTicks );
	/* Update DestTicks for next VBL */
	DestTicks += FrameDuration_micro;
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
	bIgnoreNextMouseMotion = true;        /* Ignore mouse motion event from SDL_WarpMouse */
}


/* ----------------------------------------------------------------------- */
/**
 * Handle mouse motion event.
 */
static void Main_HandleMouseMotion(SDL_Event *pEvent)
{
	int dx, dy;
	static int ax = 0, ay = 0;

	/* Ignore motion when position has changed right after a reset or TOS
	 * (especially version 4.04) might get confused and play key clicks */
	if (bIgnoreNextMouseMotion || nVBLs < 10)
	{
		bIgnoreNextMouseMotion = false;
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
	bool bContinueProcessing;
	SDL_Event event;
	int events;
	int remotepause;

	do
	{
		bContinueProcessing = false;

		/* check remote process control */
		remotepause = Control_CheckUpdates();

		if ( bEmulationActive || remotepause )
		{
			events = SDL_PollEvent(&event);
		}
		else
		{
			ShortCut_ActKey();
			/* last (shortcut) event activated emulation? */
			if ( bEmulationActive )
				break;
			events = SDL_WaitEvent(&event);
		}
		if (!events)
		{
			/* no events -> if emulation is active or
			 * user is quitting -> return from function.
			 */
			continue;
		}
		switch (event.type)
		{

		 case SDL_QUIT:
			Main_RequestQuit();
			break;
			
		 case SDL_MOUSEMOTION:               /* Read/Update internal mouse position */
			Main_HandleMouseMotion(&event);
			bContinueProcessing = true;
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
				IKBD_PressSTKey(0x50, true);
			}
			else if (event.button.button == SDL_BUTTON_WHEELUP)
			{
				/* Simulate pressing the "cursor up" key */
				IKBD_PressSTKey(0x48, true);
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
				IKBD_PressSTKey(0x50, false);
			}
			else if (event.button.button == SDL_BUTTON_WHEELUP)
			{
				/* Simulate releasing the "cursor up" key */
				IKBD_PressSTKey(0x48, false);
			}
			break;

		 case SDL_KEYDOWN:
			Keymap_KeyDown(&event.key.keysym);
			break;

		 case SDL_KEYUP:
			Keymap_KeyUp(&event.key.keysym);
			break;

		default:
			/* don't let unknown events delay event processing */
			bContinueProcessing = true;
			break;
		}
	} while (bContinueProcessing || !(bEmulationActive || bQuitProgram));
}


/*-----------------------------------------------------------------------*/
/**
 * Set Hatari window title. Use NULL for default
 */
void Main_SetTitle(const char *title)
{
	if (title)
		SDL_WM_SetCaption(title, "Hatari");
	else
		SDL_WM_SetCaption(PROG_NAME, "Hatari");
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
		fprintf(stderr, "Logging/tracing initialization failed\n");
		exit(-1);
	}
	Log_Printf(LOG_INFO, PROG_NAME ", compiled on:  " __DATE__ ", " __TIME__ "\n");

	/* Init SDL's video subsystem. Note: Audio and joystick subsystems
	   will be initialized later (failures there are not fatal). */
	if (SDL_Init(SDL_INIT_VIDEO | Opt_GetNoParachuteFlag()) < 0)
	{
		fprintf(stderr, "Could not initialize the SDL library:\n %s\n", SDL_GetError() );
		exit(-1);
	}

#ifdef HAVE_CAPSIMAGE
	if (CAPSInit() != imgeOk)
	{
		fprintf(stderr, "Could not initialize the capsimage library\n" );
		exit(-1);
	}
#endif

	ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
	Resolution_Init();
	SDLGui_Init();
	Printer_Init();
	RS232_Init();
	Midi_Init();
	Control_CheckUpdates();       /* enable window embedding? */
	Screen_Init();
	Main_SetTitle(NULL);
	HostScreen_Init();
	DSP_Init();
	Floppy_Init();
	M68000_Init();                /* Init CPU emulation */
	Audio_Init();
	DmaSnd_Init();
	Keymap_Init();

	/* Init HD emulation */
	HDC_Init();
	Ide_Init();
	GemDOS_Init();
	if (ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
		/* uses variables set by HDC_Init()! */
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
	Midi_UnInit();
	RS232_UnInit();
	Printer_UnInit();
	IoMem_UnInit();
	NvRam_UnInit();
	GemDOS_UnInitDrives();
	Ide_UnInit();
	Joy_UnInit();
	if (Sound_AreWeRecording())
		Sound_EndRecording();
	Audio_UnInit();
	SDLGui_UnInit();
	DSP_UnInit();
	HostScreen_UnInit();
	Screen_UnInit();
	Exit680x0();

#ifdef HAVE_CAPSIMAGE
	CAPSExit();
#endif

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
 * Set TOS etc information and initial help message
 */
static void Main_StatusbarSetup(void)
{
	const char *name = NULL;
	SDLKey key;

	key = ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS];
	if (!key)
		key = ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS];
	if (key)
		name = SDL_GetKeyName(key);
	if (name)
	{
		char message[24], *keyname;
#ifdef _MUDFLAP
		__mf_register((void*)name, 32, __MF_TYPE_GUESS, "SDL keyname");
#endif
		keyname = Str_ToUpper(strdup(name));
		snprintf(message, sizeof(message), "Press %s for Options", keyname);
		free(keyname);

		Statusbar_AddMessage(message, 6000);
	}
	/* update information loaded by Main_Init() */
	Statusbar_UpdateInfo();
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
	if (!Opt_ParseParameters(argc, (const char * const *)argv))
	{
		return 1;
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

	/* Needed for proper behavior of Caps Lock on some systems */
	setenv("SDL_DISABLE_LOCK_KEYS", "1", 1);
#endif

	/* Init emulator system */
	Main_Init();

	/* Set initial Statusbar information */
	Main_StatusbarSetup();
	
	/* Check if SDL_Delay is accurate */
	Main_CheckForAccurateDelays();

	if ( AviRecordOnStartup )	/* Immediatly starts avi recording ? */
		Avi_StartRecording ( ConfigureParams.Video.AviRecordFile , ConfigureParams.Screen.bCrop ,
			ConfigureParams.Video.AviRecordFps == 0 ?
				ClocksTimings_GetVBLPerSec ( ConfigureParams.System.nMachineType , nScreenRefreshRate ) :
				(Uint32)ConfigureParams.Video.AviRecordFps << CLOCKS_TIMINGS_SHIFT_VBL ,
			1 << CLOCKS_TIMINGS_SHIFT_VBL ,
			ConfigureParams.Video.AviRecordVcodec );

	/* Run emulation */
	Main_UnPauseEmulation();
	M68000_Start();                 /* Start emulation */

	if (bRecordingAvi)
	{
		/* cleanly close the avi file */
		Statusbar_AddMessage("Finishing AVI file...", 100);
		Statusbar_Update(sdlscrn);
		Avi_StopRecording();
	}
	/* Un-init emulation system */
	Main_UnInit();

	return 0;
}
