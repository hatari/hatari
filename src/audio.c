/*
  Hatari - audio.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to the SDL library.
*/
char Audio_rcsid[] = "Hatari $Id: audio.c,v 1.23 2005-04-05 14:41:19 thothy Exp $";

#include <SDL.h>

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "log.h"
#include "sound.h"


/* 11Khz, 22Khz, 44Khz playback */
int SoundPlayBackFrequencies[] =
{
	11025,  /* PLAYBACK_LOW */
	22050,  /* PLAYBACK_MEDIUM */
	44100,  /* PLAYBACK_HIGH */
};


BOOL bSoundWorking = FALSE;               /* Is sound OK */
volatile BOOL bPlayingBuffer = FALSE;     /* Is playing buffer? */
int OutputAudioFreqIndex = FREQ_22Khz;    /* Playback rate (11Khz,22Khz or 44Khz) */
int SoundBufferSize = 1024;               /* Size of sound buffer */
int CompleteSndBufIdx;                    /* Replay-index into MixBuffer */



/*-----------------------------------------------------------------------*/
/*
  SDL audio callback function - copy emulation sound to audio system.
*/
static void Audio_CallBack(void *userdata, Uint8 *stream, int len)
{
	Uint8 *pBuffer;
	int i;

	/* If there are only some samples missing to have a complete buffer,
	 * we generate them here (sounds much better then!). However, if a lot of
	 * samples are missing, then the system is probably too slow, so we don't
	 * generate more samples to not make things worse... */
	if (nGeneratedSamples < len && len-nGeneratedSamples < 128)
		Sound_UpdateFromAudioCallBack();

	/* Pass completed buffer to audio system: Write samples into sound buffer
	 * and convert them from 'signed' to 'unsigned' */
	pBuffer = stream;
	for (i = 0; i < len; i++)
	{
		*pBuffer++ = MixBuffer[(CompleteSndBufIdx + i) % MIXBUFFER_SIZE] ^ 128;
	}

	/* We should now have generated a complete frame of samples.
	 * However, for slow systems we have to check how many generated samples
	 * we may advance... */
	if (nGeneratedSamples >= len)
	{
		CompleteSndBufIdx += len;
		nGeneratedSamples -= len;
	}
	else
	{
		CompleteSndBufIdx += nGeneratedSamples;
		nGeneratedSamples = 0;
	}
	CompleteSndBufIdx = CompleteSndBufIdx % MIXBUFFER_SIZE;
}


/*-----------------------------------------------------------------------*/
/*
  Initialize the audio subsystem. Return TRUE if all OK.
  We use direct access to the sound buffer, set to a unsigned 8-bit mono stream.
*/
void Audio_Init(void)
{
	SDL_AudioSpec desiredAudioSpec;    /* We fill in the desired SDL audio options here */

	/* Is enabled? */
	if (!ConfigureParams.Sound.bEnableSound)
	{
		/* Stop any sound access */
		Log_Printf(LOG_DEBUG, "Sound: Disabled\n");
		bSoundWorking = FALSE;
		return;
	}

	/* Init the SDL's audio subsystem: */
	if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
	{
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
		{
			fprintf(stderr, "Could not init audio: %s\n", SDL_GetError() );
			bSoundWorking = FALSE;
			return;
		}
	}

	/* Set up SDL audio: */
	desiredAudioSpec.freq = SoundPlayBackFrequencies[OutputAudioFreqIndex];
	desiredAudioSpec.format = AUDIO_U8;           /* 8 Bit unsigned */
	desiredAudioSpec.channels = 1;                /* Mono */
	desiredAudioSpec.samples = 1024;              /* Buffer size */
	desiredAudioSpec.callback = Audio_CallBack;
	desiredAudioSpec.userdata = NULL;

	if (SDL_OpenAudio(&desiredAudioSpec, NULL))   /* Open audio device */
	{
		fprintf(stderr, "Can't use audio: %s\n", SDL_GetError());
		bSoundWorking = FALSE;
		ConfigureParams.Sound.bEnableSound = FALSE;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return;
	}

	SoundBufferSize = desiredAudioSpec.size;      /* May be different than the requested one! */
	if (SoundBufferSize > MIXBUFFER_SIZE/2)
	{
		fprintf(stderr, "Warning: Soundbuffer size is too big!\n");
	}

	/* All OK */
	bSoundWorking = TRUE;
	/* And begin */
	Audio_EnableAudio(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Free audio subsystem
*/
void Audio_UnInit(void)
{
	if (bSoundWorking)
	{
		/* Stop */
		Audio_EnableAudio(FALSE);

		SDL_CloseAudio();

		bSoundWorking = FALSE;
	}
}


/*-----------------------------------------------------------------------*/
/*
  Lock the audio sub system so that the callback function will not be called.
*/
void Audio_Lock(void)
{
	SDL_LockAudio();
}


/*-----------------------------------------------------------------------*/
/*
  Unlock the audio sub system so that the callback function will be called again.
*/
void Audio_Unlock(void)
{
	SDL_UnlockAudio();
}


/*-----------------------------------------------------------------------*/
/*
  Set audio playback frequency variable, pass as PLAYBACK_xxxx
*/
void Audio_SetOutputAudioFreq(int Frequency)
{
	/* Do not reset sound system if nothing has changed! */
	if (Frequency != OutputAudioFreqIndex)
	{
		/* Set new frequency, index into SoundPlayBackFrequencies[] */
		OutputAudioFreqIndex = Frequency;

		/* Re-open SDL audio interface if necessary: */
		if (bSoundWorking)
		{
			Audio_UnInit();
			Audio_Init();
		}
	}
}


/*-----------------------------------------------------------------------*/
/*
  Start/Stop sound buffer
*/
void Audio_EnableAudio(BOOL bEnable)
{
	if (bEnable && !bPlayingBuffer)
	{
		/* Start playing */
		SDL_PauseAudio(FALSE);
		bPlayingBuffer = TRUE;
	}
	else if (!bEnable && bPlayingBuffer)
	{
		/* Stop from playing */
		SDL_PauseAudio(TRUE);
		bPlayingBuffer = FALSE;
	}
}
