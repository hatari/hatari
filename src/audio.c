/*
  Hatari - audio.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to the SDL library.
*/
static char rcsid[] = "Hatari $Id: audio.c,v 1.11 2003-03-02 15:14:05 thothy Exp $";

#include <SDL.h>

#include "main.h"
#include "audio.h"
#include "debug.h"
#include "dialog.h"
#include "errlog.h"
#include "memAlloc.h"
#include "misc.h"
#include "sound.h"

#define WRITE_INIT_POS  ((SoundPlayBackFrequencies[OutputAudioFreqIndex]/50)*2)  /* Write 2/50th ahead of write position */

/* 11Khz, 22Khz, 44Khz playback */
int SoundPlayBackFrequencies[] = {
  11025,  /* PLAYBACK_LOW */
  22050,  /* PLAYBACK_MEDIUM */
  44100,  /* PLAYBACK_HIGH */
};

/* Bytes to download on each odd/even frame - as 11Khz does not divide by 50 exactly */
int SoundPlayBackFreqFrameLengths[][2] = {
  { 221,220 },  /* 220.5 */
  { 441,441 },  /* 441 */
  { 882,882 },  /* 882 */
};

BOOL bDisableSound=FALSE;
BOOL bSoundWorking=TRUE;                          /* Is sound OK */
volatile BOOL bPlayingBuffer = FALSE;             /* Is playing buffer? Start when start processing ST */
int WriteOffset=0;                                /* Write offset into buffer */
int OutputAudioFreqIndex=FREQ_22Khz;              /* Playback rate (11Khz,22Khz or 44Khz) */
float PlayVolume=0.0f;
BOOL bAquireWritePosition=FALSE;
unsigned char *SoundBuffer1, *SoundBuffer2;
int SoundBufferSize=1024;                         /* Size of sound buffer */

SDL_AudioSpec desiredAudioSpec;                   /* We fill in the desired SDL audio options here */



/*-----------------------------------------------------------------------*/
/*
  SDL audio callback function
*/
void Audio_CallBack(void *userdata, Uint8 *stream, int len)
{
  memcpy(stream, SoundBuffer2, len);
}


/*-----------------------------------------------------------------------*/
/*
  Create object for sound. Return TRUE if all OK
  We use direct access to the primary buffer, set to an unsigned 8-bit mono stream
*/
void Audio_Init(void)
{
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
  desiredAudioSpec.format = AUDIO_U8;            /* 8 Bit unsigned */
  desiredAudioSpec.channels = 1;                 /* Mono */
  desiredAudioSpec.samples = SoundBufferSize;    /* Buffer size */
  desiredAudioSpec.callback = Audio_CallBack;
  desiredAudioSpec.userdata = NULL;
  if( SDL_OpenAudio(&desiredAudioSpec, NULL) )   /* Open audio device */
  {
    fprintf(stderr, "Can't use audio: %s\n", SDL_GetError());
    bSoundWorking = FALSE;
    ConfigureParams.Sound.bEnableSound = FALSE;
    return;
  }

  /* Create sound buffer, return if error */
  Audio_CreateSoundBuffer();
  SDL_PauseAudio(FALSE);
}


/*-----------------------------------------------------------------------*/
/*
  Free object created for Direct Sound
*/
void Audio_UnInit(void)
{
  SDL_PauseAudio(TRUE);

  /* Free sound buffer */
  Audio_FreeSoundBuffer();

  SDL_CloseAudio();
}


/*-----------------------------------------------------------------------*/
/*
  Create sound buffer to write samples into
*/
BOOL Audio_CreateSoundBuffer(void)
{
  int bufferlen;

  /* Allocate memory for the 2 sound buffers: */
  bufferlen = SoundBufferSize + SoundPlayBackFreqFrameLengths[FREQ_44Khz][0];
  SoundBuffer1 = malloc( bufferlen );
  if( !SoundBuffer1 ) {
    bSoundWorking = FALSE;
    return(FALSE);
  }
  memset(SoundBuffer1, 128, bufferlen);

  SoundBuffer2 = malloc( bufferlen );
  if( !SoundBuffer2 ) {
    bSoundWorking = FALSE;
    free( SoundBuffer1 );
    SoundBuffer1 = NULL;
    return(FALSE);
  }
  memset(SoundBuffer2, 128, bufferlen);

  /* All OK */
  bSoundWorking = TRUE;
  /* And begin */
  Audio_ResetBuffer();

  return(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Free sound buffer
*/
void Audio_FreeSoundBuffer(void)
{
  /* Stop */
  Audio_StopBuffer();
  /* And free */
  if( SoundBuffer1 )
    free(SoundBuffer1);
  if( SoundBuffer2 )
    free(SoundBuffer2);
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
  Reset sound buffer, so plays from correct position
*/
void Audio_ResetBuffer(void)
{
  /* Get new 'write' position on next frame */
  bAquireWritePosition = TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Stop sound buffer
*/
void Audio_StopBuffer(void)
{
  /* Stop from playing */
  SDL_PauseAudio(TRUE);
  bPlayingBuffer = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  Scale sample value(-128...127) according to 'PlayVolume' setting
*/
char Audio_ModifyVolume(char Sample)
{
  /* If full volume, just use current value */
  if (PlayVolume==1.0f)
    return(Sample);

  /* Else, scale volume */
  Sample = (char)((float)Sample*PlayVolume);

  return(Sample);
}


/*-----------------------------------------------------------------------*/
/*
  Write samples into Direct Sound buffer at 'Offset',
  taking care to wrap around. Pass NULL to write zero's.
*/
void Audio_WriteSamplesIntoBuffer(char *pSamples,int Index,int Length,int RampSetting)
{
  void *lpWrite;
  unsigned char *pBuffer;
  short dwLenBytes;
  int i;

  /* Modify ramp volume - ramp down if sound not enabled or not in windows mouse mode */
  if ( (((RampSetting==RAMP_DOWN) || (!ConfigureParams.Sound.bEnableSound)) && (PlayVolume>0.0f)) ) {
    PlayVolume -= RAMP_DOWN_VOLUME_LEVEL;
    if (PlayVolume<=0.0f)
      PlayVolume = 0.0f;
  }
  else if ( (RampSetting==RAMP_UP) && (PlayVolume<1.0f) ) {
    PlayVolume += RAMP_UP_VOLUME_LEVEL;
    if (PlayVolume>=1.0f)
      PlayVolume = 1.0f;
  }

  if (SoundBuffer1) {

    /* Do need to reset 'write' position? */
    if (bAquireWritePosition) {
      WriteOffset = 0;
      bAquireWritePosition = FALSE;
    }

    lpWrite = SoundBuffer1 + WriteOffset;
    dwLenBytes = Length;

    /* Write section, convert to 'unsigned' and write '128'(unsigned) if passed NULL */
    if ( (dwLenBytes>0) && (lpWrite) ) {
      if (pSamples) {
        pBuffer = (unsigned char *)lpWrite;
        for(i=0; i<(int)dwLenBytes; i++) {
          *pBuffer++ = Audio_ModifyVolume(pSamples[Index])+128;
          Index = (Index+1)&4095;
        }
      }
      else
        memset(lpWrite,128,dwLenBytes);
    }

    /* Update write buffer */
    if (pSamples) {
      WriteOffset += Length;
      if (WriteOffset>=SoundBufferSize) {
        /* If the buffer is full, swap the buffers and copy overflow space to the new buffer. */
        SDL_LockAudio();
        WriteOffset -= SoundBufferSize;
        memcpy(SoundBuffer2, SoundBuffer1+SoundBufferSize, WriteOffset);  /* Copy overflow to the next buffer */
        pBuffer = SoundBuffer2;
        SoundBuffer2 = SoundBuffer1;            /* Swap the buffers */
        SoundBuffer1 = pBuffer;
        SDL_UnlockAudio();
      }
    }

    /* Are we playing? */
    if (!bPlayingBuffer) {
      SDL_PauseAudio(FALSE);
      Audio_ResetBuffer();
      bPlayingBuffer = TRUE;
    }
    else {
      /* Check here for play/write pointers getting away from each other and set 'bAquireWritePosition' to reset */
      //lpDSBPrimBuffer->GetCurrentPosition(NULL,(DWORD *)&WriteCursor);
      /* If the writecursor is too-far away from where we think it should be cause a reset */
      //CursorDiff = WriteOffset-WriteCursor;
      /* Check for overlap */
      //if (CursorDiff<0)
      //  CursorDiff = (WriteOffset+PrimaryBufferSize)-WriteCursor;
      /* So, does need reset? */
      //if (abs(CursorDiff)>(WRITE_INIT_POS*2))
      //  Audio_ResetBuffer();
    }
  }

}
