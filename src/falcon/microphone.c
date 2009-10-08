/*
  Hatari - microphone.c
  microphone (jack connector) emulation (Falcon mode only)

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This program uses the PortAudio Portable Audio Library.
  For more information see: http://www.portaudio.com
  Copyright (c) 1999-2000 Ross Bencina and Phil Burk
*/

#if HAVE_PORTAUDIO

#include <stdio.h>
#include <stdlib.h>
#include <portaudio.h>
#include "microphone.h"


#define FRAMES_PER_BUFFER (1024)
#define NUM_SECONDS     (1)
#define NUM_CHANNELS    (2)
/* #define DITHER_FLAG     (paDitherOff)  */
#define DITHER_FLAG     (0) /**/

#define PA_SAMPLE_TYPE  paInt16
#define SAMPLE_SILENCE  (0)

typedef short SAMPLE;

typedef struct
{
    int          frameIndex;  /* Index into sample array. */
    int          maxFrameIndex;
    SAMPLE      *recordedSamples;
}
paTestData;

static paTestData data;

static PaStreamParameters micro_inputParameters;
static PaStream *micro_stream;
static PaError  micro_err;
static int  micro_totalFrames;
static int  micro_numSamples;
static int  micro_numBytes;
static int  micro_sampleRate;



/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
	paTestData *data = (paTestData*)userData;
	const SAMPLE *rptr = (const SAMPLE*)inputBuffer;
	SAMPLE *wptr = &data->recordedSamples[data->frameIndex * NUM_CHANNELS];
	long framesToCalc;
	long i;
	int finished;
	unsigned long framesLeft = data->maxFrameIndex - data->frameIndex;

	(void) outputBuffer; /* Prevent unused variable warnings. */
	(void) timeInfo;
	(void) statusFlags;
	(void) userData;

	if( framesLeft < framesPerBuffer )
	{
		framesToCalc = framesLeft;
		finished = paComplete;
	}
	else
	{
		framesToCalc = framesPerBuffer;
		finished = paContinue;
	}

	if( inputBuffer == NULL )
	{
		for( i=0; i<framesToCalc; i++ )
		{
			*wptr++ = SAMPLE_SILENCE;  /* left */
			if( NUM_CHANNELS == 2 ) 
				*wptr++ = SAMPLE_SILENCE;  /* right */
		}
	}
	else
	{
		for( i=0; i<framesToCalc; i++ )
		{
			*wptr++ = *rptr++;  /* left */
			if( NUM_CHANNELS == 2 )
				*wptr++ = *rptr++;  /* right */
		}
	}
	data->frameIndex += framesToCalc;
	return finished;
}


/*******************************************************************/

/**
 * Microphone (jack) inits :
 *   - sound frequency
 *   ...
 */
int Microphone_Start(int sampleRate)
{
	int i;

	micro_sampleRate = sampleRate;
	data.maxFrameIndex = micro_totalFrames = NUM_SECONDS * micro_sampleRate; /* Record for a few seconds. */
	data.frameIndex = 0;
	micro_numSamples = micro_totalFrames * NUM_CHANNELS;

	micro_numBytes = micro_numSamples * sizeof(SAMPLE);
	data.recordedSamples = (SAMPLE *) malloc(micro_numBytes);
	if (data.recordedSamples == NULL)
	{
		printf("Could not allocate record array.\n");
		return -1;
	}

	/* Init sound buffer */
	for (i=0; i<micro_numSamples; i++) 
		data.recordedSamples[i] = 0;

	/* Init portaudio Jack device */
	micro_err = Pa_Initialize();
	if (micro_err != paNoError) {
		Pa_Terminate();
		fprintf(stderr, "An error occured while using the portaudio stream\n");
		fprintf(stderr, "Error number: %d\n", micro_err);
		fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(micro_err));
		return -1;
	}
	micro_inputParameters.device = Pa_GetDefaultInputDevice();	/* default input device */
	micro_inputParameters.channelCount = 2;			/* stereo input */
	micro_inputParameters.sampleFormat = PA_SAMPLE_TYPE;
	micro_inputParameters.suggestedLatency = Pa_GetDeviceInfo(micro_inputParameters.device)->defaultLowInputLatency;
	micro_inputParameters.hostApiSpecificStreamInfo = NULL;

	/* Open stream */
	micro_err = Pa_OpenStream(
		&micro_stream,
		&micro_inputParameters,
		NULL,			/* &outputParameters if playback */
		micro_sampleRate,
		FRAMES_PER_BUFFER,
		paClipOff,		/* we won't output out of range samples so don't bother clipping them */
		recordCallback,
		&data );

	if (micro_err != paNoError) {
		Pa_Terminate();
		fprintf(stderr, "An error occured while using the portaudio stream\n");
		fprintf(stderr, "Error number: %d\n", micro_err);
		fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(micro_err));
		return -1;
	}

	/* Start recording */
	micro_err = Pa_StartStream(micro_stream);
	if (micro_err != paNoError) {
		Pa_Terminate();
		fprintf(stderr, "An error occured while using the portaudio stream\n");
		fprintf(stderr, "Error number: %d\n", micro_err);
		fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(micro_err));
		return -1;
	}

	return 0;
}

/**
 * Microphone (jack) stop : stops the current recording
 */
int Microphone_Stop(void)
{
	printf("Finished recording!!\n"); 
	fflush(stdout);

	micro_err = Pa_CloseStream(micro_stream);
	if (micro_err != paNoError) {
		Pa_Terminate();
		fprintf(stderr, "An error occured while using the portaudio stream\n");
		fprintf(stderr, "Error number: %d\n", micro_err);
		fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(micro_err));
		return -1;
	}

	free(data.recordedSamples);

	Pa_Terminate();

	return 0;
}

void Microphone_Run(void)
{
	while (Pa_IsStreamActive(micro_stream))
	{
		Pa_Sleep(1000);
	}
}

#endif /* HAVE PORTAUDIO */
