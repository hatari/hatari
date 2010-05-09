/*
  Hatari - audio.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to the SDL library.
*/
const char Audio_fileid[] = "Hatari audio.c : " __DATE__ " " __TIME__;

#include <SDL.h>

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "log.h"
#include "sound.h"
#include "dmaSnd.h"


int nAudioFrequency = 44100;			/* Sound playback frequency */
bool bSoundWorking = false;			/* Is sound OK */
volatile bool bPlayingBuffer = false;		/* Is playing buffer? */
int SoundBufferSize = 1024 / 4;			/* Size of sound buffer (in samples) */
int CompleteSndBufIdx;				/* Replay-index into MixBuffer */



/*-----------------------------------------------------------------------*/
/**
 * SDL audio callback function - copy emulation sound to audio system.
 */
static void Audio_CallBack(void *userdata, Uint8 *stream, int len)
{
	Sint16 *pBuffer;
	int i;

	pBuffer = (Sint16 *)stream;
	len = len / 4;  // Use length in samples (16 bit stereo), not in bytes

	if (nGeneratedSamples >= len)
	{
		/* Enough samples available: Pass completed buffer to audio system
		 * by write samples into sound buffer and by converting them from
		 * 'signed' to 'unsigned' */
		for (i = 0; i < len; i++)
		{
			*pBuffer++ = MixBuffer[(CompleteSndBufIdx + i) % MIXBUFFER_SIZE][0];
			*pBuffer++ = MixBuffer[(CompleteSndBufIdx + i) % MIXBUFFER_SIZE][1];
		}
		CompleteSndBufIdx += len;
		nGeneratedSamples -= len;
	}
	else  /* Not enough samples available: */
	{
		for (i = 0; i < nGeneratedSamples; i++)
		{
			*pBuffer++ = MixBuffer[(CompleteSndBufIdx + i) % MIXBUFFER_SIZE][0];
			*pBuffer++ = MixBuffer[(CompleteSndBufIdx + i) % MIXBUFFER_SIZE][1];
		}
		/* If the buffer is filled more than 50%, mirror sample buffer to fake the
		 * missing samples */
		if (nGeneratedSamples >= len/2)
		{
			int remaining = len - nGeneratedSamples;
			memcpy(pBuffer, stream+(nGeneratedSamples-remaining)*4, remaining*4);
		}
		CompleteSndBufIdx += nGeneratedSamples;
		nGeneratedSamples = 0;
		
	}

	CompleteSndBufIdx = CompleteSndBufIdx % MIXBUFFER_SIZE;
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
			fprintf(stderr, "Could not init audio: %s\n", SDL_GetError() );
			bSoundWorking = false;
			return;
		}
	}

	/* Set up SDL audio: */
	desiredAudioSpec.freq = nAudioFrequency;
	desiredAudioSpec.format = AUDIO_S16SYS;		/* 16-Bit signed */
	desiredAudioSpec.channels = 2;			/* stereo */
	desiredAudioSpec.samples = 1024;		/* Buffer size in samples */
	desiredAudioSpec.callback = Audio_CallBack;
	desiredAudioSpec.userdata = NULL;

	if (SDL_OpenAudio(&desiredAudioSpec, NULL))	/* Open audio device */
	{
		fprintf(stderr, "Can't use audio: %s\n", SDL_GetError());
		bSoundWorking = false;
		ConfigureParams.Sound.bEnableSound = false;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return;
	}

	SoundBufferSize = desiredAudioSpec.size;	/* May be different than the requested one! */
	SoundBufferSize /= 4;				/* bytes -> samples (16 bit signed stereo -> 4 bytes per sample) */
	if (SoundBufferSize > MIXBUFFER_SIZE/2)
	{
		fprintf(stderr, "Warning: Soundbuffer size is too big!\n");
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

		/* Re-open SDL audio interface if necessary: */
		if (bSoundWorking)
		{
			Audio_UnInit();
			Audio_Init();
			DmaSnd_Init_Bass_and_Treble_Tables();
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
