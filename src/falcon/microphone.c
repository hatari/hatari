/*
  Hatari - microphone.c
  microphone (jack connector) emulation (Falcon mode only)

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This program uses the PortAudio Portable Audio Library.
  For more information see: http://www.portaudio.com
  Copyright (c) 1999-2000 Ross Bencina and Phil Burk
*/

#include "main.h"

#if HAVE_PORTAUDIO

#include <portaudio.h>
#include "microphone.h"
#include "configuration.h"
#include "crossbar.h"
#include "log.h"

#define FRAMES_PER_BUFFER (64)

/* Static functions */
static bool Microphone_Error (void);
static bool Microphone_Terminate(void);

static int Microphone_Callback (const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData);

/* Static datas */
static PaStreamParameters micro_inputParameters;
static PaStream *micro_stream;
static PaError  micro_err;

static int   micro_sampleRate;
static Sint16 micro_buffer_L[FRAMES_PER_BUFFER];	/* left buffer */
static Sint16 micro_buffer_R[FRAMES_PER_BUFFER];	/* right buffer */


/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int Microphone_Callback (const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData)
{
	unsigned int i;
	const Sint16 *in = inputBuffer;
	
	Sint16 *out_L = (Sint16*)micro_buffer_L;
	Sint16 *out_R = (Sint16*)micro_buffer_R;

	if (inputBuffer == NULL) {
		memset(micro_buffer_L, 0, sizeof(micro_buffer_L));
		memset(micro_buffer_R, 0, sizeof(micro_buffer_R));
	}
	else {
		for (i=0; i<framesPerBuffer; i++) {
			*out_L ++ = *in++;	/* left data */
			*out_R ++ = *in++;	/* right data */
		}
	}

	/* send buffer to crossbar */
	Crossbar_GetMicrophoneDatas(micro_buffer_L, micro_buffer_R, framesPerBuffer);
	
	/* get Next Microphone datas */
	return paContinue;
}


/*******************************************************************/

/**
 * Microphone (jack) inits : init portaudio microphone emulation
 *   - sampleRate : system sound frequency
 * return true on success, false on error or if mic disabled
 */
bool Microphone_Start(int sampleRate)
{
	if (!ConfigureParams.Sound.bEnableMicrophone) {
		Log_Printf(LOG_DEBUG, "Microphone: Disabled\n");
		return false;
	}

	micro_sampleRate = sampleRate;

	/* Initialize portaudio */
	micro_err = Pa_Initialize();
	if (micro_err != paNoError) {
		return Microphone_Error();
	}

	/* Initialize microphone parameters */
	micro_inputParameters.device = Pa_GetDefaultInputDevice();	/* default input device */
	if (micro_inputParameters.device == paNoDevice) {
		Log_Printf(LOG_WARN, "Microphone: No input device found.\n");
		Microphone_Terminate();
		return false;
	}
	
	micro_inputParameters.channelCount = 2;				/* stereo input */
	micro_inputParameters.sampleFormat = paInt16;			/* 16 bits sound */
	micro_inputParameters.suggestedLatency = Pa_GetDeviceInfo (micro_inputParameters.device)->defaultLowInputLatency;
	micro_inputParameters.hostApiSpecificStreamInfo = NULL;

	/* Open Microphone stream */
	micro_err = Pa_OpenStream (
		&micro_stream,
		&micro_inputParameters,
		NULL,
		micro_sampleRate,
		FRAMES_PER_BUFFER,
		0, /* paClipOff, we won't output out of range samples so don't bother clipping them */
		Microphone_Callback,
		NULL);
	if (micro_err != paNoError) {
		return Microphone_Error();
	}

	/* Start microphone recording */
	micro_err = Pa_StartStream( micro_stream );
	if (micro_err != paNoError) {
		return Microphone_Error();
	}

	return true;
}

/**
 * Microphone (jack) Error : display current portaudio error
 */
static bool Microphone_Error(void)
{
	fprintf (stderr, "An error occurred while using the portaudio stream\n");
	fprintf (stderr, "Error number: %d\n", micro_err);
	fprintf (stderr, "Error message: %s\n", Pa_GetErrorText (micro_err));

	Microphone_Terminate();
	return false;
}

/**
 * Microphone (jack) Terminate : terminate the microphone emulation
 * return true for success
 */
static bool Microphone_Terminate(void)
{
	micro_stream = NULL; /* catch erroneous use */

	micro_err = Pa_Terminate();
	if (micro_err != paNoError) {
		fprintf (stderr, "PortAudio error: %s\n", Pa_GetErrorText(micro_err));
		return false;
	}

	return true;
}

/**
 * Microphone (jack) stop : stops the current recording
 * return true for success, false for error
 */
bool Microphone_Stop(void)
{
	/* Close Microphone stream */
	micro_err = Pa_CloseStream(micro_stream);
	if (micro_err != paNoError) {
		return Microphone_Error();
	}

	return Microphone_Terminate();
}

#endif /* HAVE PORTAUDIO */
