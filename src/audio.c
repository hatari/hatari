/*
  Hatari - audio.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to the SDL library.
*/
static char rcsid[] = "Hatari $Id: audio.c,v 1.16 2003-04-12 16:28:06 thothy Exp $";

#include <SDL.h>

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "debug.h"
#include "errlog.h"
#include "memAlloc.h"
#include "misc.h"
#include "sound.h"


/* 11Khz, 22Khz, 44Khz playback */
int SoundPlayBackFrequencies[] =
{
  11025,  /* PLAYBACK_LOW */
  22050,  /* PLAYBACK_MEDIUM */
  44100,  /* PLAYBACK_HIGH */
};


BOOL bDisableSound = FALSE;
BOOL bSoundWorking = TRUE;                /* Is sound OK */
volatile BOOL bPlayingBuffer = FALSE;     /* Is playing buffer? */
int OutputAudioFreqIndex = FREQ_22Khz;    /* Playback rate (11Khz,22Khz or 44Khz) */
float PlayVolume = 0.0f;
int SoundBufferSize = 1024;               /* Size of sound buffer */
int CompleteSndBufIdx;                    /* Replay-index into MixBuffer */



/*-----------------------------------------------------------------------*/
/*
  SDL audio callback function - copy emulation sound to audio system.
*/
void Audio_CallBack(void *userdata, Uint8 *stream, int len)
{
  /* If there are only some samples missing to have a complete buffer,
   * we generate them here (sounds much better then!). However, if a lot of
   * samples are missing, then the system is probably too slow, so we don't
   * generate more samples to not make things worse... */
  if(nGeneratedSamples < len && len-nGeneratedSamples < 128)
    Sound_UpdateFromAudioCallBack();

  /* Pass completed buffer to audio system: */
  Audio_WriteSamplesIntoBuffer(MixBuffer, CompleteSndBufIdx, len,
                               (bEmulationActive ? RAMP_UP : RAMP_DOWN), stream);

  /* We should now have generated a complete frame of samples.
   * However, for slow systems we have to check how many generated samples
   * we may advance... */
  if(nGeneratedSamples >= len)
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
  if(bDisableSound)
  {
    /* Stop any sound access */
    ErrLog_File("Sound: Disabled\n");
    bSoundWorking = FALSE;
    return;
  }

  /* Init the SDL's audio subsystem: */
  if( SDL_WasInit(SDL_INIT_AUDIO)==0 )
  {
    if( SDL_InitSubSystem(SDL_INIT_AUDIO)<0 )
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

  if( SDL_OpenAudio(&desiredAudioSpec, NULL) )  /* Open audio device */
  {
    fprintf(stderr, "Can't use audio: %s\n", SDL_GetError());
    bSoundWorking = FALSE;
    ConfigureParams.Sound.bEnableSound = FALSE;
    return;
  }

  SoundBufferSize = desiredAudioSpec.size;      /* May be different than the requested one! */
  if(SoundBufferSize > MIXBUFFER_SIZE/2)
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
  /* Stop */
  Audio_EnableAudio(FALSE);

  SDL_CloseAudio();
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
  if(Frequency != OutputAudioFreqIndex)
  {
    /* Set new frequency, index into SoundPlayBackFrequencies[] */
    OutputAudioFreqIndex = Frequency;

    /* Re-open SDL audio interface... */
    Audio_UnInit();
    Audio_Init();
  }
}


/*-----------------------------------------------------------------------*/
/*
  Start/Stop sound buffer
*/
void Audio_EnableAudio(BOOL bEnable)
{
  if(bEnable && !bPlayingBuffer)
  {
    /* Start playing */
    SDL_PauseAudio(FALSE);
    bPlayingBuffer = TRUE;
  }
  else if(!bEnable && bPlayingBuffer)
  {
    /* Stop from playing */
    SDL_PauseAudio(!bEnable);
    bPlayingBuffer = bEnable;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Scale sample value (-128...127) according to 'PlayVolume' setting
*/
Sint8 Audio_ModifyVolume(Sint8 Sample)
{
  /* If full volume, just use current value */
  if (PlayVolume==1.0f)
    return(Sample);

  /* Else, scale volume */
  Sample = (Sint8)((float)Sample*PlayVolume);

  return(Sample);
}


/*-----------------------------------------------------------------------*/
/*
  Write samples into sound buffer. Pass pSamples=NULL to write zero's.
*/
void Audio_WriteSamplesIntoBuffer(Sint8 *pSamples, int Index, int Length,
                                  int RampSetting, Sint8 *pDestBuffer)
{
  Sint8 *pBuffer;
  int i;

  /* Modify ramp volume - ramp down if sound not enabled or not in windows mouse mode */
  if( (((RampSetting==RAMP_DOWN) || (!ConfigureParams.Sound.bEnableSound)) && (PlayVolume>0.0f)) )
  {
    PlayVolume -= RAMP_DOWN_VOLUME_LEVEL;
    if(PlayVolume <= 0.0f)
      PlayVolume = 0.0f;
  }
  else if((RampSetting==RAMP_UP) && (PlayVolume<1.0f))
  {
    PlayVolume += RAMP_UP_VOLUME_LEVEL;
    if(PlayVolume >= 1.0f)
      PlayVolume = 1.0f;
  }

  /* Write section, convert to 'unsigned' and write '128's if passed NULL */
  if(Length > 0)
  {
    if(pSamples)
    {
      pBuffer = pDestBuffer;
      for(i = 0; i < Length; i++)
      {
        *pBuffer++ = Audio_ModifyVolume(pSamples[Index]) ^ 128;
        Index = (Index + 1) % MIXBUFFER_SIZE;
      }
    }
    else
    {
      memset(pDestBuffer, 128, Length);
    }
  }
}

