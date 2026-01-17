/*
  Hatari - audio.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to libretro.
*/
const char Audio_fileid[] = "Hatari audio.c";

#include "main.h"

#include "audio.h"
#include "configuration.h"
#include "log.h"
#include "sound.h"
#include "dmaSnd.h"
#include "crossbar.h"
#include "video.h"


bool bSoundWorking = false;			/* Is sound OK */
static volatile bool bPlayingBuffer = false;	/* Is playing buffer? */
int SoundBufferSize = 1024 / 4;			/* Size of sound buffer (in samples) */
int SdlAudioBufferSize = 0;			/* in ms (0 = use default) */
int pulse_swallowing_count = 0;			/* Sound disciplined emulation rate controlled by  */
						/*  window comparator and pulse swallowing counter */

/**
 * Initialize the audio subsystem. Return true if all OK.
 * We use direct access to the sound buffer, set to a unsigned 8-bit mono stream.
 */
void Audio_Init(void)
{
	bSoundWorking = true;
}


/**
 * Free audio subsystem
 */
void Audio_UnInit(void)
{
	bSoundWorking = false;
}


/**
 * Lock the audio sub system so that the callback function will not be called.
 */
void Audio_Lock(void)
{
}


/**
 * Unlock the audio sub system so that the callback function will be called again.
 */
void Audio_Unlock(void)
{
}


/**
 * Start/Stop sound buffer
 */
void Audio_EnableAudio(bool bEnable)
{
	bPlayingBuffer = bEnable;
}
