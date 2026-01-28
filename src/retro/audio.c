/*
  Hatari - audio.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to libretro.
*/
const char Audio_fileid[] = "Hatari audio.c";

#include <libretro.h>

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

static retro_audio_sample_t audio_sample_cb;
static retro_audio_sample_batch_t audio_sample_batch_cb;

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
	audio_sample_cb = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
        audio_sample_batch_cb = cb;
}

/**
 * Initialize the audio subsystem. Return true if all OK.
 * We use direct access to the sound buffer, set to a unsigned 8-bit mono stream.
 */
void Audio_Init(void)
{
	bSoundWorking = true;
	Audio_EnableAudio(true);
}


/**
 * Free audio subsystem
 */
void Audio_UnInit(void)
{
	Audio_EnableAudio(false);
	bSoundWorking = false;
}


/**
 * Lock the audio sub system - currently a no-op with libretro since we
 * don't use the callback there that signals that the buffer is ready.
 */
void Audio_Lock(void)
{
}


/**
 * Unlock the audio sub system - we use this as indication in the retro core
 * that some samples have just been generated and we can batch them now.
 */
void Audio_Unlock(void)
{
	if (bPlayingBuffer && nGeneratedSamples && audio_sample_batch_cb)
	{
		if (AudioMixBuffer_pos_read + nGeneratedSamples <= AUDIOMIXBUFFER_SIZE)
		{
			audio_sample_batch_cb(&AudioMixBuffer[AudioMixBuffer_pos_read][0],
			                      nGeneratedSamples);
		}
		else
		{
			int samples_at_end = AUDIOMIXBUFFER_SIZE - AudioMixBuffer_pos_read;
			audio_sample_batch_cb(&AudioMixBuffer[AudioMixBuffer_pos_read][0],
			                      samples_at_end);
			audio_sample_batch_cb(&AudioMixBuffer[0][0],
			                      (nGeneratedSamples - samples_at_end));
		}
	}

	AudioMixBuffer_pos_read = (AudioMixBuffer_pos_read + nGeneratedSamples)
	                          & AUDIOMIXBUFFER_SIZE_MASK;
	nGeneratedSamples = 0;
}


/**
 * Start/Stop sound buffer
 */
void Audio_EnableAudio(bool bEnable)
{
	bPlayingBuffer = bEnable;
}
