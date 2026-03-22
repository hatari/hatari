/*
  Hatari - timing.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Timing related routines.
*/

#include <time.h>
#include <errno.h>

#include "main.h"
#include "audio.h"
#include "avi_record.h"
#include "configuration.h"
#include "clocks_timings.h"
#include "log.h"
#include "options.h"
#include "screen.h"
#include "sound.h"
#include "timing.h"
#include "video.h"

#if HAVE_GETTIMEOFDAY
#include <sys/time.h>
#endif

#ifdef EMSCRIPTEN
#include "emscripten.h"
#endif

#if !defined(HAVE_GETTIMEOFDAY) || !defined(HAVE_SYS_TIMES_H)
#include <SDL.h>
#endif

static uint32_t nRunVBLs;           /* Whether and how many VBLS to run before exit */
static uint32_t nFirstMilliTick;    /* Ticks when VBL counting started */
static uint32_t nVBLCount;          /* Frame count */
static int nVBLSlowdown = 1;        /* host VBL wait multiplier */

static bool bAccurateDelays;        /* Host system has an accurate SDL_Delay()? */


/**
 * Return current time as millisecond for performance measurements.
 *
 * (On Unix only time spent by Hatari itself is counted, on other
 * platforms less accurate SDL "wall clock".)
 */
#if HAVE_SYS_TIMES_H
#include <unistd.h>
#include <sys/times.h>
static uint32_t Timing_GetPerfTicks(void)
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
# define Timing_GetPerfTicks SDL_GetTicks
#endif


/**
 * Return a time counter in micro seconds.
 * If gettimeofday is available, we use it directly, else we convert the
 * return of SDL_GetTicks in micro sec.
 */

int64_t Timing_GetTicks(void)
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


/**
 * Sleep for a given number of micro seconds.
 * If nanosleep is available, we use it directly, else we use SDL_Delay
 * (which is portable, but less accurate as is uses milli-seconds)
 */

static void Timing_Delay(int64_t ticks_micro)
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


/**
 * Always print speeds in Benchmark mode, otherwise only
 * if loglevel is "info" or higher (when time is recorded).
 */
void Timing_PrintSpeed(void)
{
	if (!nFirstMilliTick)
		return;

	int interval = Timing_GetPerfTicks() - nFirstMilliTick;
	static float previous;
	float current;
	int level = LOG_INFO;

	if (BenchmarkMode && ConfigureParams.Log.nTextLogLevel < level)
		level = ConfigureParams.Log.nTextLogLevel;

	current = (1000.0 * nVBLCount) / interval;
	Log_Printf(level, "SPEED: %.1f VBL/s (%d/%.1fs), diff=%.1f%%\n",
		   current, nVBLCount, interval/1000.0,
		   previous>0.0 ? 100*(current-previous)/previous : 0.0);
	nVBLCount = nFirstMilliTick = 0;
	previous = current;
}


/**
 * When recording, show time in titlebar.
 */
static void Timing_UpdateTitle(int currentVbl)
{
	static int startVbl;
	int vbls, secs, mins, hours;
	char info[16];

	/* recording started since previous VBL? */
	if (!startVbl)
	{
		if (Sound_AreWeRecording() || Avi_AreWeRecording())
		{
			Screen_SetTitle("00:00:00");
			startVbl = currentVbl;
		}
		return;
	}

	/* recording stopped since previous VBL? */
	if (!(Sound_AreWeRecording() || Avi_AreWeRecording()))
	{
		Screen_SetTitle(NULL);
		startVbl = 0;
		return;
	}

	vbls = currentVbl - startVbl;
	/* no need to update titlebar/secs? */
	if (vbls % nScreenRefreshRate != 0)
		return;

	secs = vbls / nScreenRefreshRate;
	hours = secs / 3600;
	mins = (secs % 3600) / 60;
	secs = secs % 60;

	/* update recording time to titlebar */
	snprintf(info, sizeof(info), "%02d:%02d:%02d", hours, mins, secs);
	Screen_SetTitle(info);
}


/**
 * Set how many VBLs Hatari should run, from the moment this function
 * is called and return zero.
 *
 * If zero value given instead, returns earlier set VBL count.
 */
uint32_t Timing_SetRunVBLs(uint32_t vbls)
{
	if (!vbls)
		return nRunVBLs;

	nRunVBLs = vbls;
	nVBLCount = 0;
	return 0;
}


/**
 * Set VBL wait slowdown factor/multiplier
 *
 * Return NULL on success, error string on error
 */
const char* Timing_SetVBLSlowdown(int factor)
{
	if (factor < 1 || factor > 30) {
		return "invalid VBL slowdown factor, should be 1-30";
	}
	nVBLSlowdown = factor;
	return NULL;
}


/**
 * This function waits on each emulated VBL to synchronize the real time
 * with the emulated ST.
 * Unfortunately SDL_Delay and other sleep functions like usleep or nanosleep
 * are very inaccurate on some systems like Linux 2.4 or macOS (they can only
 * wait for a multiple of 10ms due to the scheduler on these systems), so we
 * have to "busy wait" there to get an accurate timing.
 * All times are expressed as micro seconds, to avoid too much rounding error.
 */
void Timing_WaitOnVbl(void)
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

	Timing_UpdateTitle(nVBLCount);

//	FrameDuration_micro = (int64_t) ( 1000000.0 / nScreenRefreshRate + 0.5 );	/* round to closest integer */
	FrameDuration_micro = ClocksTimings_GetVBLDuration_micro ( ConfigureParams.System.nMachineType , nScreenRefreshRate );
	FrameDuration_micro *= nVBLSlowdown;
	CurrentTicks = Timing_GetTicks();

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
				nFirstMilliTick = Timing_GetPerfTicks();
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
			Timing_Delay(nDelay - 1000);
	}
	else
	{
		/* No accurate SDL_Delay -> only wait if more than 5ms to go... */
		if (nDelay > 5000)
			Timing_Delay(nDelay<10000 ? nDelay-1000 : 9000);
	}

	/* Now busy-wait for the right tick: */
	while (nDelay > 0)
	{
		CurrentTicks = Timing_GetTicks();
		nDelay = DestTicks - CurrentTicks;
		/* If the delay is still bigger than one frame, somebody
		 * played tricks with the system clock and we have to abort */
		if (nDelay > FrameDuration_micro)
			break;
	}

	/* Update DestTicks for next VBL */
	DestTicks += FrameDuration_micro;
}


/**
 * Since SDL_Delay and friends are very inaccurate on some systems, we have
 * to check if we can rely on this delay function.
 */
void Timing_CheckForAccurateDelays(void)
{
	int64_t nStartTicks, nEndTicks;

	/* Force a task switch now, so we have a longer timeslice afterwards */
	Timing_Delay(10000);

	nStartTicks = Timing_GetTicks() / 1000;
	Timing_Delay(1000);
	nEndTicks = Timing_GetTicks() / 1000;

	/* If the delay took 10ms or more, we are on an inaccurate system! */
	bAccurateDelays = ((nEndTicks - nStartTicks) < 9);

	if (bAccurateDelays)
		Log_Printf(LOG_DEBUG, "Host system has accurate delays. (%d)\n",
		           (int)(nEndTicks - nStartTicks));
	else
		Log_Printf(LOG_WARN, "Host system does not have accurate delays. (%d)\n",
		           (int)(nEndTicks - nStartTicks));
}
