/*
  Hatari - audio.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to the SDL library.
*/
const char Audio_fileid[] = "Hatari audio.c";

#include <SDL.h>

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "log.h"
#include "sound.h"
#include "dmaSnd.h"
#include "falcon/crossbar.h"
#include "video.h"


int nAudioFrequency = 44100;			/* Sound playback frequency */
bool bSoundWorking = false;			/* Is sound OK */
static volatile bool bPlayingBuffer = false;	/* Is playing buffer? */
int SoundBufferSize = 1024 / 4;			/* Size of sound buffer (in samples) */
int SdlAudioBufferSize = 0;			/* in ms (0 = use default) */
int pulse_swallowing_count = 0;			/* Sound disciplined emulation rate controlled by  */
						/*  window comparator and pulse swallowing counter */

/*-----------------------------------------------------------------------*/
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


/*-----------------------------------------------------------------------*/
/**
 * Initialize the audio subsystem. Return true if all OK.
 * We use direct access to the sound buffer, set to a unsigned 8-bit mono stream.
 */
void Audio_Init(void)
{
	SDL_AudioSpec desiredAudioSpec;    /* We fill in the desired SDL audio options here */

	/* Is enabled? */
	if (!ConfigureParams.Sound.bEnableSound)
	{
		/* Stop any sound access */
		Log_Printf(LOG_DEBUG, "Sound: Disabled\n");
		bSoundWorking = false;
		return;
	}

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
	desiredAudioSpec.freq = nAudioFrequency;
	desiredAudioSpec.format = AUDIO_S16SYS;		/* 16-Bit signed */
	desiredAudioSpec.channels = 2;			/* stereo */
	desiredAudioSpec.callback = Audio_CallBack;
	desiredAudioSpec.userdata = NULL;

	/* In most case, setting samples to 1024 will give an equivalent */
	/* sdl sound buffer of ~20-30 ms (depending on freq). */
	/* But setting samples to 1024 for all the freq can cause some faulty */
	/* OS sound drivers to add an important delay when playing sound at lower freq. */
	/* In that case we use SdlAudioBufferSize (in ms) to compute a value */
	/* of samples that matches the corresponding freq and buffer size. */
	if ( SdlAudioBufferSize == 0 )			/* don't compute "samples", use default value */
		desiredAudioSpec.samples = 1024;	/* buffer size in samples */
	else
	{
		int samples = (desiredAudioSpec.freq / 1000) * SdlAudioBufferSize;
		int power2 = 1;
		while ( power2 < samples )		/* compute the power of 2 just above samples */
			power2 *= 2;

		desiredAudioSpec.samples = power2;	/* number of samples corresponding to the requested SdlAudioBufferSize */
	}


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

	/* All OK */
	bSoundWorking = true;
	/* And begin */
	Audio_EnableAudio(true);
}


/*-----------------------------------------------------------------------*/
/**
 * Free audio subsystem
 */
void Audio_UnInit(void)
{
	if (bSoundWorking)
	{
		/* Stop */
		Audio_EnableAudio(false);

		SDL_CloseAudio();

		bSoundWorking = false;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Lock the audio sub system so that the callback function will not be called.
 */
void Audio_Lock(void)
{
	SDL_LockAudio();
}


/*-----------------------------------------------------------------------*/
/**
 * Unlock the audio sub system so that the callback function will be called again.
 */
void Audio_Unlock(void)
{
	SDL_UnlockAudio();
}


/*-----------------------------------------------------------------------*/
/**
 * Set audio playback frequency variable, pass as PLAYBACK_xxxx
 */
void Audio_SetOutputAudioFreq(int nNewFrequency)
{
	/* Do not reset sound system if nothing has changed! */
	if (nNewFrequency != nAudioFrequency)
	{
		/* Set new frequency */
		nAudioFrequency = nNewFrequency;

		if (Config_IsMachineFalcon())
		{
			/* Compute Ratio between host computer sound frequency and Hatari's sound frequency. */
			Crossbar_Compute_Ratio();
		}
		else if (!Config_IsMachineST())
		{
			/* Adapt LMC filters to this new frequency */
			DmaSnd_Init_Bass_and_Treble_Tables();
		}

		/* Re-open SDL audio interface if necessary: */
		if (bSoundWorking)
		{
			Audio_UnInit();
			Audio_Init();
		}
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Start/Stop sound buffer
 */
void Audio_EnableAudio(bool bEnable)
{
	if (bEnable && !bPlayingBuffer)
	{
		/* Start playing */
		SDL_PauseAudio(false);
		bPlayingBuffer = true;
	}
	else if (!bEnable && bPlayingBuffer)
	{
		/* Stop from playing */
		SDL_PauseAudio(true);
		bPlayingBuffer = false;
	}
}
