/*
  Hatari - timing.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Timing related routines.
*/

#include <time.h>
#include <errno.h>

#include "main.h"
#include "m68000.h"
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
	/* Assume that libretro does the syncing for us, so don't wait here,
	 * just tell the CPU core to yield back to libretro! */
	M68000_SetSpecial(SPCFLAG_BRK);
	quit_program = UAE_QUIT;
}


void Timing_CheckForAccurateDelays(void)
{
}
