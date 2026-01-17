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

#include <sys/time.h>


/**
 * Return a time counter in micro seconds.
 */
int64_t Timing_GetTicks(void)
{
	int64_t ticks_micro;

	struct timeval	now;
	gettimeofday ( &now , NULL );
	ticks_micro = (int64_t)now.tv_sec * 1000000 + now.tv_usec;

	return ticks_micro;
}


void Timing_PrintSpeed(void)
{
}


/**
 * Set how many VBLs Hatari should run, from the moment this function
 * is called and return zero.
 *
 * If zero value given instead, returns earlier set VBL count.
 */
uint32_t Timing_SetRunVBLs(uint32_t vbls)
{
	static uint32_t nRunVBLs;

	if (!vbls)
		return nRunVBLs;

	nRunVBLs = vbls;
	return 0;
}


/**
 * Set VBL wait slowdown factor/multiplier
 *
 * Return NULL on success, error string on error
 */
const char* Timing_SetVBLSlowdown(int factor)
{
	return NULL;
}


void Timing_WaitOnVbl(void)
{
	int64_t CurrentTicks;
	static int64_t DestTicks = 0;
	int64_t FrameDuration_micro;
	int64_t nDelay;

	FrameDuration_micro = ClocksTimings_GetVBLDuration_micro ( ConfigureParams.System.nMachineType , nScreenRefreshRate );
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
		if (nFrameSkips < ConfigureParams.Screen.nFrameSkips)
		{
			nFrameSkips += 1;
			Log_Printf(LOG_DEBUG, "Increased frameskip to %d\n", nFrameSkips);
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
		Log_Printf(LOG_DEBUG, "Decreased frameskip to %d\n", nFrameSkips);
	}

	/* Assume that libretro does the syncing for us, so don't wait here! */

	/* Update DestTicks for next VBL */
	DestTicks += FrameDuration_micro;
}


void Timing_CheckForAccurateDelays(void)
{
}
