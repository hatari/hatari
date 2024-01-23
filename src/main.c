/*
  Hatari - main.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Main initialization and event handling routines.
*/
const char Main_fileid[] = "Hatari main.c";

#include <time.h>
#include <errno.h>
#include <SDL.h>

#include "main.h"
#include "version.h"
#include "configuration.h"
#include "control.h"
#include "options.h"
#include "dialog.h"
#include "audio.h"
#include "joy.h"
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
#include "resolution.h"
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
#include "tos.h"
#include "video.h"
#include "avi_record.h"
#include "debugui.h"
#include "clocks_timings.h"
#include "utils.h"

#include "hatari-glue.h"

#include "falcon/dsp.h"
#include "falcon/videl.h"

#if HAVE_GETTIMEOFDAY
#include <sys/time.h>
#endif

#ifdef WIN32
#include "gui-win/opencon.h"
#endif

#ifdef EMSCRIPTEN
#include "emscripten.h"
#endif

bool bQuitProgram = false;                /* Flag to quit program cleanly */
static int nQuitValue;                    /* exit value */

static uint32_t nRunVBLs;                 /* Whether and how many VBLS to run before exit */
static uint32_t nFirstMilliTick;          /* Ticks when VBL counting started */
static uint32_t nVBLCount;                /* Frame count */
static int nVBLSlowdown = 1;              /* host VBL wait multiplier */

static bool bEmulationActive = true;      /* Run emulation when started */
static bool bAccurateDelays;              /* Host system has an accurate SDL_Delay()? */

static bool bIgnoreNextMouseMotion = false;  /* Next mouse motion will be ignored (needed after SDL_WarpMouse) */
static bool bAllowMouseWarp = true;       /* disabled when Hatari window loses mouse pointer / key focus */


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
static uint32_t Main_GetTicks(void)
{
	static unsigned int ticks_to_msec = 0;
	struct tms fields;
	if (!ticks_to_msec)
	{
		ticks_to_msec = sysconf(_SC_CLK_TCK);
		Log_Printf(LOG_INFO, "OS clock ticks / second: %d\n", ticks_to_msec);
		/* Linux has 100Hz virtual clock so no accuracy loss there */
		ticks_to_msec = 1000UL / ticks_to_msec;
	}
	/* return milliseconds (clock ticks) spent in this process
	 */
	times(&fields);
	return ticks_to_msec * fields.tms_utime;
}
#else
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

static int64_t Time_GetTicks(void)
{
	int64_t ticks_micro;

#if HAVE_GETTIMEOFDAY
	struct timeval	now;
	gettimeofday ( &now , NULL );
	ticks_micro = (int64_t)now.tv_sec * 1000000 + now.tv_usec;
#else
	ticks_micro = (int64_t)SDL_GetTicks() * 1000;		/* milli sec -> micro sec */
#endif

	return ticks_micro;
}


/*-----------------------------------------------------------------------*/
/**
 * Sleep for a given number of micro seconds.
 * If nanosleep is available, we use it directly, else we use SDL_Delay
 * (which is portable, but less accurate as is uses milli-seconds)
 */

static void Time_Delay(int64_t ticks_micro)
{
#ifdef EMSCRIPTEN
	emscripten_sleep((uint32_t)(ticks_micro / 1000));	/* micro sec -> milli sec */
#else
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
	SDL_Delay((uint32_t)(ticks_micro / 1000)) ;	/* micro sec -> milli sec */
#endif
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
			Log_Printf(LOG_INFO, "SPEED: %.1f VBL/s (%d/%.1fs), diff=%.1f%%\n",
			       current, nVBLCount, interval/1000.0,
			       previous>0.0 ? 100*(current-previous)/previous : 0.0);
			nVBLCount = nFirstMilliTick = 0;
			previous = current;
		}
		
		Statusbar_AddMessage("Emulation paused", 100);
		/* make sure msg gets shown */
		Statusbar_Update(sdlscrn, true);

		if (bGrabMouse && !bInFullScreen)
			/* Un-grab mouse pointer in windowed mode */
			SDL_SetRelativeMouseMode(false);
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
		SDL_SetRelativeMouseMode(true);
	return true;
}

/*-----------------------------------------------------------------------*/
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

/*-----------------------------------------------------------------------*/
/**
 * Set how many VBLs Hatari should run, from the moment this function
 * is called and return zero.
 *
 * If zero value given instead, returns earlier set VBL count.
 */
uint32_t Main_SetRunVBLs(uint32_t vbls)
{
	if (!vbls)
		return nRunVBLs;

	nRunVBLs = vbls;
	nVBLCount = 0;
	return 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Set VBL wait slowdown factor/multiplayer
 *
 * Return NULL on success, error string on error
 */
const char* Main_SetVBLSlowdown(int factor)
{
	if (factor < 1 || factor > 30) {
		return "invalid VBL slowdown factor, should be 1-30";
	}
	nVBLSlowdown = factor;
	return NULL;
}

/*-----------------------------------------------------------------------*/
/**
 * This function waits on each emulated VBL to synchronize the real time
 * with the emulated ST.
 * Unfortunately SDL_Delay and other sleep functions like usleep or nanosleep
 * are very inaccurate on some systems like Linux 2.4 or macOS (they can only
 * wait for a multiple of 10ms due to the scheduler on these systems), so we
 * have to "busy wait" there to get an accurate timing.
 * All times are expressed as micro seconds, to avoid too much rounding error.
 */
void Main_WaitOnVbl(void)
{
	int64_t CurrentTicks;
	static int64_t DestTicks = 0;
	int64_t FrameDuration_micro;
	int64_t nDelay;

	nVBLCount++;
	if (nRunVBLs &&	nVBLCount >= nRunVBLs)
	{
		/* show VBLs/s */
		Main_PauseEmulation(true);
		exit(0);
	}

//	FrameDuration_micro = (int64_t) ( 1000000.0 / nScreenRefreshRate + 0.5 );	/* round to closest integer */
	FrameDuration_micro = ClocksTimings_GetVBLDuration_micro ( ConfigureParams.System.nMachineType , nScreenRefreshRate );
	FrameDuration_micro *= nVBLSlowdown;
	CurrentTicks = Time_GetTicks();

	if (DestTicks == 0)			/* on first call, init DestTicks */
	{
		DestTicks = CurrentTicks + FrameDuration_micro;
	}

	DestTicks += pulse_swallowing_count;	/* audio.c - Audio_CallBack() */

	nDelay = DestTicks - CurrentTicks;

	/* Do not wait if we are in fast forward mode or if we are totally out of sync */
	/* or if we are in benchmark mode */
	if (ConfigureParams.System.bFastForward == true
	    || nDelay < -4*FrameDuration_micro || nDelay > 50*FrameDuration_micro
	    || BenchmarkMode )

	{
		if ( ( ConfigureParams.System.bFastForward == true )
		  || ( BenchmarkMode == true ) )
		{
			if (!nFirstMilliTick)
				nFirstMilliTick = Main_GetTicks();
		}
		if (nFrameSkips < ConfigureParams.Screen.nFrameSkips)
		{
			nFrameSkips += 1;
			Log_Printf(LOG_DEBUG, "Increased frameskip to %d\n", nFrameSkips);
		}
		/* Only update DestTicks for next VBL */
		DestTicks = CurrentTicks + FrameDuration_micro;
		#ifdef EMSCRIPTEN
		emscripten_sleep(0);
		#endif
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
		Log_Printf(LOG_DEBUG, "Decreased frameskip to %d\n", nFrameSkips);
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
		/* If the delay is still bigger than one frame, somebody
		 * played tricks with the system clock and we have to abort */
		if (nDelay > FrameDuration_micro)
			break;
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
 * Set mouse pointer to new x,y coordinates and set flag to ignore
 * the mouse event that is generated by SDL_WarpMouse().
 *
 * Skip the request if:
 * - it's not position restore and mouse warping is disabled, or
 * - mouse warp disable due to mouse leaving Hatari window or focus loss
 *   (i.e. user isn't interacting with it)
 */
void Main_WarpMouse(int x, int y, bool restore)
{
	if (!(restore || ConfigureParams.Screen.bMouseWarp))
		return;
	if (!bAllowMouseWarp)
		return;

	SDL_WarpMouseInWindow(sdlWindow, x, y);
	bIgnoreNextMouseMotion = true;
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
			Main_RequestQuit(0);
			break;

		 case SDL_KEYDOWN:
			if (event.key.repeat) {
				bContinueProcessing = true;
				break;
			}
			Keymap_KeyDown(&event.key.keysym);
			break;

		 case SDL_KEYUP:
			Keymap_KeyUp(&event.key.keysym);
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
			break;

		 case SDL_MOUSEWHEEL:
			/* Simulate cursor keys on mouse wheel events */
			if (event.wheel.x > 0)
			{
				IKBD_PressSTKey(0x4d, true);
				IKBD_PressSTKey(0x4d, false);
			}
			else if (event.wheel.x < 0)
			{
				IKBD_PressSTKey(0x4b, true);
				IKBD_PressSTKey(0x4b, false);
			}
			if (event.wheel.y < 0)
			{
				IKBD_PressSTKey(0x50, true);
				IKBD_PressSTKey(0x50, false);
			}
			else if (event.wheel.y > 0)
			{
				IKBD_PressSTKey(0x48, true);
				IKBD_PressSTKey(0x48, false);
			}
			break;

		 case SDL_WINDOWEVENT:
			Log_Printf(LOG_DEBUG, "SDL2 window event: 0x%x\n", event.window.event);
			switch(event.window.event) {
			case SDL_WINDOWEVENT_EXPOSED:
				if (!ConfigureParams.Screen.bUseSdlRenderer)
				{
					/* Hack: Redraw screen here when going into
					 * fullscreen mode without SDL renderer */
					sdlscrn = SDL_GetWindowSurface(sdlWindow);
					Screen_SetFullUpdate();
					Statusbar_Init(sdlscrn);
				}
				/* fall through */
			case SDL_WINDOWEVENT_RESTORED:
				/* Note: any changes here should most likely
				 * be done also in sdlgui.c::SDLGui_DoDialog()
				 */
				Screen_UpdateRect(sdlscrn, 0, 0, 0, 0);
				break;
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				/* internal & external window size changes */
				Screen_SetTextureScale(sdlscrn->w, sdlscrn->h,
						       event.window.data1,
						       event.window.data2, false);
				Screen_UpdateRect(sdlscrn, 0, 0, 0, 0);
				break;
				/* mouse & keyboard focus */
			case SDL_WINDOWEVENT_ENTER:
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				bAllowMouseWarp = true;
				break;
			case SDL_WINDOWEVENT_LEAVE:
			case SDL_WINDOWEVENT_FOCUS_LOST:
				bAllowMouseWarp = false;
				break;
			}
			bContinueProcessing = true;
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
		SDL_SetWindowTitle(sdlWindow, title);
	else
		SDL_SetWindowTitle(sdlWindow, PROG_NAME);
}

/*-----------------------------------------------------------------------*/
/**
 * Initialise emulation for some hardware components
 * It is required to init those parts before parsing the parameters
 * (for example, we should init FDC before inserting a disk and we
 * need to know valid joysticks before selecting default joystick IDs)
 */
static void Main_Init_HW(void)
{
	Joy_Init();
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
		fprintf(stderr, "ERROR: logging/tracing initialization failed\n");
		exit(-1);
	}
	Log_Printf(LOG_INFO, PROG_NAME ", compiled on:  " __DATE__ ", " __TIME__ "\n");

	/* Init SDL's video subsystem. Note: Audio subsystem
	   will be initialized later (failure not fatal). */
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		fprintf(stderr, "ERROR: could not initialize the SDL library:\n %s\n", SDL_GetError() );
		exit(-1);
	}

	if ( IPF_Init() != true )
	{
		fprintf(stderr, "ERROR: could not initialize the IPF support\n" );
		exit(-1);
	}

	ClocksTimings_InitMachine ( ConfigureParams.System.nMachineType );
	Video_SetTimings ( ConfigureParams.System.nMachineType , ConfigureParams.System.VideoTimingMode );

	Resolution_Init();
	SDLGui_Init();
	Printer_Init();
	MFP_Init(MFP_Array);
	RS232_Init();
	SCC_Init();
	Midi_Init();
	Control_CheckUpdates();       /* enable window embedding? */
	Videl_Init();
	Screen_Init();
	Main_SetTitle(NULL);

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
			fprintf(stderr, "ERROR: failed to load TOS image!\n");
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
	Screen_UnInit();
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
 * Main
 * 
 * Note: 'argv' cannot be declared const, MinGW would then fail to link.
 */
int main(int argc, char *argv[])
{
	/* Generate random seed */
	Hatari_srand(time(NULL));

	/* Logs default to stderr at start */
	Log_Default();

	/* Initialize directory strings */
	Paths_Init(argv[0]);

	/* Init some HW components before parsing the configuration / parameters */
	Main_Init_HW();

	/* Set default configuration values */
	Configuration_SetDefault();

	/* Now load the values from the configuration file */
	Main_LoadInitialConfig();

	/* Check for any passed parameters */
	if (!Opt_ParseParameters(argc, (const char * const *)argv))
	{
		Control_RemoveFifo();
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

	if ( AviRecordOnStartup )	/* Immediately starts avi recording ? */
		Avi_StartRecording ( ConfigureParams.Video.AviRecordFile , ConfigureParams.Screen.bCrop ,
			ConfigureParams.Video.AviRecordFps == 0 ?
				ClocksTimings_GetVBLPerSec ( ConfigureParams.System.nMachineType , nScreenRefreshRate ) :
				ClocksTimings_GetVBLPerSec ( ConfigureParams.System.nMachineType , ConfigureParams.Video.AviRecordFps ) ,
			1 << CLOCKS_TIMINGS_SHIFT_VBL ,
			ConfigureParams.Video.AviRecordVcodec );

	/* Run emulation */
	Main_UnPauseEmulation();
	M68000_Start();                 /* Start emulation */

	Control_RemoveFifo();
	if (bRecordingAvi)
	{
		/* cleanly close the avi file */
		Statusbar_AddMessage("Finishing AVI file...", 100);
		Statusbar_Update(sdlscrn, true);
		Avi_StopRecording();
	}
	/* Un-init emulation system */
	Main_UnInit();

	return nQuitValue;
}
