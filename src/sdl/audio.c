/*
  Hatari - audio.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to the SDL library.
*/
const char Audio_fileid[] = "Hatari audio.c";

#include "main.h"

#if ENABLE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "audio.h"
#include "configuration.h"
#include "log.h"
#include "sound.h"
#include "dmaSnd.h"
#include "crossbar.h"
#include "video.h"


#define DEFAULT_BUFFER_SAMPLES 1024

bool bSoundWorking = false;			/* Is sound OK */
static volatile bool bPlayingBuffer = false;	/* Is playing buffer? */
int SoundBufferSize = DEFAULT_BUFFER_SAMPLES;	/* Size of sound buffer (in samples) */
int SdlAudioBufferSize = 0;			/* in ms (0 = use default) */
int pulse_swallowing_count = 0;			/* Sound disciplined emulation rate controlled by  */
						/*  window comparator and pulse swallowing counter */

#if ENABLE_SDL3
static SDL_AudioStream *audio_stream;
#endif


/**
 * SDL audio callback function - copy emulation sound to audio system.
 */
static void Audio_CallBack(void *userdata, Uint8 *stream, int len)
{
	Sint16 *pBuffer;
	int i, window, nSamplesPerFrame;

	pBuffer = (Sint16 *)stream;
	len = len / 4;  // Use length in samples (16 bit stereo), not in bytes

	/* Adjust emulation rate within +/- 0.58% (10 cents) occasionally,
	 * to synchronize sound. Note that an octave (frequency doubling)
	 * has 12 semitones (12th root of two for a semitone), and that
	 * one semitone has 100 cents (1200th root of two for one cent).
	 * Ten cents are desired, thus, the 120th root of two minus one is
	 * multiplied by 1,000,000 to convert to microseconds, and divided
	 * by nScreenRefreshRate=60 to get a 96 microseconds swallow size.
	 * (2^(10cents/(12semitones*100cents)) - 1) * 10^6 / nScreenRefreshRate
	 * See: main.c - Main_WaitOnVbl()
	 */

//fprintf ( stderr , "audio cb in len=%d gensmpl=%d idx=%d\n" , len , nGeneratedSamples , AudioMixBuffer_pos_read );
	pulse_swallowing_count = 0;	/* 0 = Unaltered emulation rate */

	if (ConfigureParams.Sound.bEnableSoundSync)
	{
		/* Sound synchronized emulation */
		nSamplesPerFrame = nAudioFrequency/nScreenRefreshRate;
		window = (nSamplesPerFrame > SoundBufferSize) ? nSamplesPerFrame : SoundBufferSize;

		/* Window Comparator for SoundBufferSize */
		if (nGeneratedSamples < window + (window >> 1))
		/* Increase emulation rate to maintain sound synchronization */
			pulse_swallowing_count = -5793 / nScreenRefreshRate;
		else
		if (nGeneratedSamples > (window << 1) + (window >> 2))
		/* Decrease emulation rate to maintain sound synchronization */
			pulse_swallowing_count = 5793 / nScreenRefreshRate;

		/* Otherwise emulation rate is unaltered. */
	}

	if (nGeneratedSamples >= len)
	{
		/* Enough samples available: Pass completed buffer to audio system
		 * by write samples into sound buffer and by converting them from
		 * 'signed' to 'unsigned' */
		for (i = 0; i < len; i++)
		{
			*pBuffer++ = AudioMixBuffer[(AudioMixBuffer_pos_read + i) & AUDIOMIXBUFFER_SIZE_MASK][0];
			*pBuffer++ = AudioMixBuffer[(AudioMixBuffer_pos_read + i) & AUDIOMIXBUFFER_SIZE_MASK][1];
		}
		AudioMixBuffer_pos_read += len;
		nGeneratedSamples -= len;
	}
	else  /* Not enough samples available: */
	{
		for (i = 0; i < nGeneratedSamples; i++)
		{
			*pBuffer++ = AudioMixBuffer[(AudioMixBuffer_pos_read + i) & AUDIOMIXBUFFER_SIZE_MASK][0];
			*pBuffer++ = AudioMixBuffer[(AudioMixBuffer_pos_read + i) & AUDIOMIXBUFFER_SIZE_MASK][1];
		}
		/* Clear rest of the buffer to ensure we don't play random bytes instead */
		/* of missing samples */
		memset(pBuffer, 0, (len - nGeneratedSamples) * 4);

		AudioMixBuffer_pos_read += nGeneratedSamples;
		nGeneratedSamples = 0;
		
	}

	AudioMixBuffer_pos_read = AudioMixBuffer_pos_read & AUDIOMIXBUFFER_SIZE_MASK;
//fprintf ( stderr , "audio cb out len=%d gensmpl=%d idx=%d\n" , len , nGeneratedSamples , AudioMixBuffer_pos_read );
}


#if ENABLE_SDL3
static void SDLCALL Audio_SDL3Callback(void *userdata, SDL_AudioStream *stream,
                                       int additional_amount, int total_amount)
{
	if (additional_amount > 0)
	{
		Uint8 *data = SDL_stack_alloc(Uint8, additional_amount);
		if (data)
		{
			Audio_CallBack(userdata, data, additional_amount);
			SDL_PutAudioStreamData(stream, data, additional_amount);
			SDL_stack_free(data);
		}
	}
}
#endif


static int Audio_GetBufferSize(int freq)
{
	int buf_size;

	/* In most case, setting samples to 1024 will give an equivalent */
	/* sdl sound buffer of ~20-30 ms (depending on freq). */
	/* But setting samples to 1024 for all the freq can cause some faulty */
	/* OS sound drivers to add an important delay when playing sound at lower freq. */
	/* In that case we use SdlAudioBufferSize (in ms) to compute a value */
	/* of samples that matches the corresponding freq and buffer size. */
	if (SdlAudioBufferSize == 0)			/* don't compute "samples", use default value */
	{
		buf_size = DEFAULT_BUFFER_SAMPLES;	/* buffer size in samples */
	}
	else
	{
		int samples = (freq / 1000) * SdlAudioBufferSize;
		int power2 = 1;
		while ( power2 < samples )		/* compute the power of 2 just above samples */
			power2 *= 2;

		buf_size = power2;	/* number of samples corresponding to the requested SdlAudioBufferSize */
	}

	return buf_size;
}


/**
 * Initialize the audio subsystem. Return true if all OK.
 * We use direct access to the sound buffer, set to a unsigned 8-bit mono stream.
 */
void Audio_Init(void)
{
	SDL_AudioSpec desiredAudioSpec =
	{
		.format = AUDIO_S16SYS,
		.channels = 2,
		.freq = nAudioFrequency
	};

	/* Is enabled? */
	if (!ConfigureParams.Sound.bEnableSound)
	{
		/* Stop any sound access */
		Log_Printf(LOG_DEBUG, "Sound: Disabled\n");
		bSoundWorking = false;
		return;
	}

#if ENABLE_SDL3

	/* Init the SDL's audio subsystem: */
	if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
	{
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
		{
			Log_Printf(LOG_WARN, "Could not init audio: %s\n", SDL_GetError() );
			bSoundWorking = false;
			return;
		}
	}

	char samples_str[11];
	SoundBufferSize = Audio_GetBufferSize(nAudioFrequency);
	snprintf(samples_str, sizeof(samples_str), "%u", SoundBufferSize);
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, samples_str);

	audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
						 &desiredAudioSpec,
						 Audio_SDL3Callback,
						 NULL);
	if (!audio_stream)	/* Open audio device */
	{
		Log_Printf(LOG_WARN, "Can't use audio: %s\n", SDL_GetError());
		bSoundWorking = false;
		ConfigureParams.Sound.bEnableSound = false;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return;
	}

	SoundBufferSize =  AUDIOMIXBUFFER_SIZE / 2;

#else

	/* Init the SDL's audio subsystem: */
	if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
	{
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
		{
			Log_Printf(LOG_WARN, "Could not init audio: %s\n", SDL_GetError() );
			bSoundWorking = false;
			return;
		}
	}

	/* Set up SDL audio: */
	desiredAudioSpec.callback = Audio_CallBack;
	desiredAudioSpec.userdata = NULL;
	desiredAudioSpec.samples = Audio_GetBufferSize(desiredAudioSpec.freq);

	if (SDL_OpenAudio(&desiredAudioSpec, NULL))	/* Open audio device */
	{
		Log_Printf(LOG_WARN, "Can't use audio: %s\n", SDL_GetError());
		bSoundWorking = false;
		ConfigureParams.Sound.bEnableSound = false;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return;
	}

	SoundBufferSize = desiredAudioSpec.samples;
	if (SoundBufferSize > AUDIOMIXBUFFER_SIZE/2)
	{
		Log_Printf(LOG_WARN, "Soundbuffer size is too big (%d > %d)!\n",
			   SoundBufferSize, AUDIOMIXBUFFER_SIZE/2);
	}

#endif

	/* All OK */
	bSoundWorking = true;
	/* And begin */
	Audio_EnableAudio(true);
}


/**
 * Free audio subsystem
 */
void Audio_UnInit(void)
{
	if (bSoundWorking)
	{
		/* Stop */
		Audio_EnableAudio(false);
#if ENABLE_SDL3
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
#else
		SDL_CloseAudio();
#endif
		bSoundWorking = false;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Lock the audio sub system so that the callback function will not be called.
 */
void Audio_Lock(void)
{
#if ENABLE_SDL3
	SDL_LockAudioStream(audio_stream);
#else
	SDL_LockAudio();
#endif
}


/*-----------------------------------------------------------------------*/
/**
 * Unlock the audio sub system so that the callback function will be called again.
 */
void Audio_Unlock(void)
{
#if ENABLE_SDL3
	SDL_UnlockAudioStream(audio_stream);
#else
	SDL_UnlockAudio();
#endif
}


/**
 * Start/Stop sound buffer
 */
void Audio_EnableAudio(bool bEnable)
{
	if (bEnable && !bPlayingBuffer)
	{
		/* Start playing */
#if ENABLE_SDL3
		SDL_ResumeAudioStreamDevice(audio_stream);
#else
		SDL_PauseAudio(false);
#endif
		bPlayingBuffer = true;
	}
	else if (!bEnable && bPlayingBuffer)
	{
		/* Stop from playing */
#if ENABLE_SDL3
		SDL_PauseAudioStreamDevice(audio_stream);
#else
		SDL_PauseAudio(true);
#endif
		bPlayingBuffer = false;
	}
}
