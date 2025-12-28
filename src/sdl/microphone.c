/*
  Hatari - microphone.c
  microphone (jack connector) emulation (Falcon mode only)

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "main.h"

#if ENABLE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "microphone.h"
#include "configuration.h"
#include "crossbar.h"
#include "log.h"

#define FRAMES_PER_BUFFER 512

static Sint16 micro_buffer_L[FRAMES_PER_BUFFER];	/* left buffer */
static Sint16 micro_buffer_R[FRAMES_PER_BUFFER];	/* right buffer */

#if ENABLE_SDL3
static SDL_AudioStream *mic_stream;
#else
static int nMicDevId;
#endif


/*
 * This routine will be called by the SDL2 library when audio is available.
 * It may be called at interrupt level on some machines so don't do anything
 * that could mess up the system like calling malloc() or free().
 */
static void Microphone_Callback(void *pUserData, Uint8 *inputBuffer, int nLen)
{
	unsigned int i;
	const Sint16 *in = (Sint16 *)inputBuffer;
	unsigned int framesPerBuffer = nLen / 4;

	Sint16 *out_L = (Sint16*)micro_buffer_L;
	Sint16 *out_R = (Sint16*)micro_buffer_R;

	for (i=0; i<framesPerBuffer; i++)
	{
		*out_L ++ = *in++;	/* left data */
		*out_R ++ = *in++;	/* right data */
	}

	/* send buffer to crossbar */
	Crossbar_GetMicrophoneDatas(micro_buffer_L, micro_buffer_R, framesPerBuffer);
}


#if ENABLE_SDL3
static void SDLCALL Microphone_SDL3Callback(void *userdata, SDL_AudioStream *stream,
                                            int additional_amount, int total_amount)
{
	if (additional_amount > 0)
	{
		Uint8 *data;
		data = SDL_stack_alloc(Uint8, additional_amount);
		if (data)
		{
			SDL_GetAudioStreamData(stream, data, additional_amount);
			if (additional_amount > FRAMES_PER_BUFFER * 4)
				additional_amount = FRAMES_PER_BUFFER * 4;
			Microphone_Callback(userdata, data, additional_amount);
			SDL_stack_free(data);
		}
	}
}
#endif


/**
 * Microphone (jack) inits : init microphone emulation
 *   - sampleRate : system sound frequency
 * return true on success, false on error or if mic disabled
 */
bool Microphone_Start(int sampleRate)
{
	SDL_AudioSpec req =
	{
		.format = AUDIO_S16SYS,
		.channels = 2,
		.freq = sampleRate,
	};

	if (!ConfigureParams.Sound.bEnableMicrophone)
	{
		Log_Printf(LOG_DEBUG, "Microphone: Disabled\n");
		return false;
	}

#if ENABLE_SDL3
	char fpb_str[8];
	snprintf(fpb_str, sizeof(fpb_str), "%u", FRAMES_PER_BUFFER);
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, fpb_str);
	mic_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING,
	                                       &req, Microphone_SDL3Callback,
	                                       NULL);
	SDL_ResetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
	if (!mic_stream)
	{
		Log_Printf(LOG_ERROR, "Microphone: SDL_OpenAudioDevice failed.\n");
		return false;

	}

	SDL_ResumeAudioStreamDevice(mic_stream);
#else
	SDL_AudioSpec obt;

	req.samples = FRAMES_PER_BUFFER;   /* TODO: Use SdlAudioBufferSize ? */
	req.callback = Microphone_Callback;
	req.userdata = NULL;

	nMicDevId = SDL_OpenAudioDevice(NULL, true, &req, &obt, 0);
	if (!nMicDevId)
	{
		Log_Printf(LOG_ERROR, "Microphone: SDL_OpenAudioDevice failed.\n");
		return false;
	}

	SDL_PauseAudioDevice(nMicDevId, 0);

	Log_Printf(LOG_DEBUG, "Microphone_Start: freq = %i\n", obt.freq);
#endif

	return true;
}

/**
 * Microphone (jack) stop : stops the current recording
 */
void Microphone_Stop(void)
{
	/* Close Microphone stream */
#if ENABLE_SDL3
	SDL_PauseAudioStreamDevice(mic_stream);
	SDL_DestroyAudioStream(mic_stream);
	mic_stream = NULL;
#else
	SDL_CloseAudioDevice(nMicDevId);
	nMicDevId = 0;
#endif
}
