/*
  Hatari - microphone.c
  microphone (jack connector) emulation (Falcon mode only)

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include <SDL.h>

#include "main.h"
#include "microphone.h"
#include "configuration.h"
#include "crossbar.h"
#include "log.h"

#define FRAMES_PER_BUFFER 512

static int nMicDevId;
static Sint16 micro_buffer_L[FRAMES_PER_BUFFER];	/* left buffer */
static Sint16 micro_buffer_R[FRAMES_PER_BUFFER];	/* right buffer */


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


/*******************************************************************/

/**
 * Microphone (jack) inits : init microphone emulation
 *   - sampleRate : system sound frequency
 * return true on success, false on error or if mic disabled
 */
bool Microphone_Start(int sampleRate)
{
	SDL_AudioSpec req, obt;

	if (!ConfigureParams.Sound.bEnableMicrophone)
	{
		Log_Printf(LOG_DEBUG, "Microphone: Disabled\n");
		return false;
	}

	req.freq = sampleRate;
	req.format = AUDIO_S16SYS;
	req.channels = 2;
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

	return true;
}

/**
 * Microphone (jack) stop : stops the current recording
 */
void Microphone_Stop(void)
{
	/* Close Microphone stream */
	SDL_CloseAudioDevice(nMicDevId);
	nMicDevId = 0;
}
