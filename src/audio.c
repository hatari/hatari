/*
  Hatari
*/

#include <SDL.h>

#include "main.h"
#include "audio.h"
#include "debug.h"
#include "dialog.h"
#include "errlog.h"
#include "memAlloc.h"
#include "misc.h"
#include "sound.h"
#include "view.h"

#define WRITE_INIT_POS  ((SoundPlayBackFrequencies[OutputAudioFreqIndex]/50)*2)  /* Write 2/50th ahead of write position */

/* 11Khz, 22Khz, 44Khz playback */
int SoundPlayBackFrequencies[] = {
  11025,  /* PLAYBACK_LOW */
  22050,  /* PLAYBACK_MEDIUM */
  44100,  /* PLAYBACK_HIGH */
};

/* Bytes to download on each odd/even frame - as 11Khz does not divide by 50 exactly */
int SoundPlayBackFreqFrameLengths[][2] = {
  221,220,  /* 220.5 */
  441,441,  /* 441 */
  882,882,  /* 882 */
};

BOOL bDisableSound=TRUE;
//LPDIRECTSOUND lpDS = NULL;
//LPDIRECTSOUNDBUFFER  lpDSBPrimBuffer = NULL;
BOOL bSoundWorking=TRUE;                          /* Is sound OK */
volatile BOOL bPlayingBuffer = FALSE;             /* Is playing buffer? Start when start processing ST */
int WriteOffset=0;                                /* Write offset into buffer */
int OutputAudioFreqIndex=FREQ_22Khz;              /* Playback rate(11Khz,22Khz or 44Khz) */
float PlayVolume=0.0f;
BOOL bAquireWritePosition=FALSE;
unsigned char *PrimaryBuffer;
int PrimaryBufferSize=1024;                       /* Size of primary buffer */

SDL_AudioSpec *desiredAudioSpec=NULL;             /* We fill in the desired SDL audio options there */



/*-----------------------------------------------------------------------*/
/*
  SDL audio callback function
*/
void Audio_CallBack(void *userdata, Uint8 *stream, int len)
{
 memcpy(stream, PrimaryBuffer, len);
}


/*-----------------------------------------------------------------------*/
/*
  Create object for Direct Sound. Return TRUE if all OK
  We use direct access to the primary buffer, set to an unsigned 8-bit mono stream
*/
void Audio_Init(void)
{

  /* Is enabled? */
  if (bDisableSound) {
    /* Stop any Direct Sound access */
    ErrLog_File("Sound: Disabled\n");
    bSoundWorking = FALSE;
    return;
  }

#if 0
  /* Create the main DirectSound object, using default driver */
  dsRetVal = DirectSoundCreate(NULL, &lpDS, NULL);
  if(dsRetVal!=DS_OK) {
    ErrLog_File("ERROR DirectSound: Unable to 'DirectSoundCreate', error code %d\n",dsRetVal);
    bSoundWorking = FALSE;
    return;
  }
  ErrLog_File("DirectSound: 'DirectSoundCreate' - OK\n");

  // Report capabilities of sound card/driver
  dscaps.dwSize = sizeof(DSCAPS); 
  dsRetVal = lpDS->GetCaps(&dscaps);
  if(dsRetVal==DS_OK) {
    // Report general list which we may use
    ErrLog_File("DirectSound: Capabilities:-\n");
    if (dscaps.dwFlags&DSCAPS_EMULDRIVER)
      ErrLog_File("\tEmulatedDriver(WARNING: This may cause Hatari to play back sounds incorrectly\n");
    if (dscaps.dwFlags&DSCAPS_PRIMARY16BIT)
      ErrLog_File("\t16-Bit\n");
    if (dscaps.dwFlags&DSCAPS_PRIMARY8BIT)
      ErrLog_File("\t8-Bit\n");
    if (dscaps.dwFlags&DSCAPS_PRIMARYMONO)
      ErrLog_File("\tMono\n");
    if (dscaps.dwFlags&DSCAPS_PRIMARYSTEREO)
      ErrLog_File("\tStereo\n");

    // Are good enough?
    if ( !((dscaps.dwFlags&DSCAPS_PRIMARY8BIT) && (dscaps.dwFlags&DSCAPS_PRIMARYMONO)) ) {
      // No, MUST have 8-Bit/Mono
      ErrLog_File("ERROR DirectSound: Your sound card does not support the playback mode required(8-Bit/Mono)\n");
      bSoundWorking = FALSE;
      return;
    }
  }
#endif

  /* Init SDL audio: */
  desiredAudioSpec = (SDL_AudioSpec *)malloc(sizeof(SDL_AudioSpec));
  if( desiredAudioSpec==NULL ) {
    bSoundWorking = FALSE;
    return;
  }
  desiredAudioSpec->freq = SoundPlayBackFrequencies[OutputAudioFreqIndex];
  desiredAudioSpec->format = AUDIO_U8;            /* 8 Bit unsigned */
  desiredAudioSpec->channels = 1;                 /* Mono */
  desiredAudioSpec->samples = PrimaryBufferSize;  /* Buffer size */
  desiredAudioSpec->callback = Audio_CallBack;
  desiredAudioSpec->userdata = NULL;
  SDL_OpenAudio(desiredAudioSpec, NULL);          /* Open audio device */
   
  /* Create sound buffer, return if error */
  Audio_CreateSoundBuffer();
  SDL_PauseAudio(0);
}

/*-----------------------------------------------------------------------*/
/*
  Free object created for Direct Sound
*/
void Audio_UnInit(void)
{

  /* Free sound buffer */
  Audio_FreeSoundBuffer();
#if 0
  if( lpDS ) {
    lpDS->Release();  lpDS = NULL;
  }
#endif

  SDL_CloseAudio();
  if( desiredAudioSpec )  free(desiredAudioSpec);
}

/*-----------------------------------------------------------------------*/
/*
  Create sound buffer to write samples into
*/
BOOL Audio_CreateSoundBuffer(void)
{
#if 0
  WAVEFORMATEX pwfx;
  DSBUFFERDESC dsbdesc;
  DSBCAPS dsbcaps;
    HRESULT dsRetVal;

    /* Set up wave format structure */
    memset(&pwfx, 0, sizeof(WAVEFORMATEX));
  pwfx.wFormatTag = WAVE_FORMAT_PCM;
  pwfx.nChannels = 1;                  // Mono
  pwfx.nSamplesPerSec = SoundPlayBackFrequencies[OutputAudioFreqIndex];
  pwfx.wBitsPerSample = 8;              // 8 Bit unsigned
  pwfx.nBlockAlign = (pwfx.nChannels*pwfx.wBitsPerSample)/8;
  pwfx.nAvgBytesPerSec = pwfx.nSamplesPerSec*pwfx.nBlockAlign;
  pwfx.cbSize = 0;
    memset(&dsbdesc, 0, sizeof(DSBUFFERDESC));
    dsbdesc.dwSize = sizeof(DSBUFFERDESC); 
    // Use direct access for Primary Buffer else sound drivers can cause corrupted sound and
  // also declare 'STICKFOCUS' otherwise cannot continue sound when loose focus!
  dsbdesc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_STICKYFOCUS;
    // Buffer size is determined by sound hardware. 
    dsbdesc.dwBufferBytes = 0; 
    dsbdesc.lpwfxFormat = NULL;              // Must be NULL for primary buffers

  // Obtain write-primary cooperative level
  dsRetVal = lpDS->SetCooperativeLevel(hWnd, DSSCL_WRITEPRIMARY);
  if(dsRetVal!=DS_OK) {
    ErrLog_File("ERROR DirectSound: Unable to 'SetCooperativeLevel', error code %d\n",dsRetVal);
    lpDS->Release();  lpDS = NULL;
    bSoundWorking = FALSE;
    return(FALSE);
  }

  // Create sound buffer
  dsRetVal = lpDS->CreateSoundBuffer(&dsbdesc,&lpDSBPrimBuffer,NULL);
  if(dsRetVal!=DS_OK) {
    ErrLog_File("ERROR DirectSound: Unable to 'CreateSoundBuffer', error code %d\n",dsRetVal);
    lpDS->Release();  lpDS = NULL;
    bSoundWorking = FALSE;
    return(FALSE);
  }
  
  // Succeeded. Set primary buffer to desired format. 
  dsRetVal = lpDSBPrimBuffer->SetFormat(&pwfx); 
  if(dsRetVal!=DS_OK) {
    ErrLog_File("ERROR DirectSound: Unable to 'SetFormat', error code %d\n",dsRetVal);
    lpDS->Release();  lpDS = NULL;
    lpDSBPrimBuffer->Release();  lpDSBPrimBuffer = NULL;
    bSoundWorking = FALSE;
    return(FALSE);
  }

  // Get buffer size
  dsbcaps.dwSize = sizeof(DSBCAPS); 
  lpDSBPrimBuffer->GetCaps(&dsbcaps); 
  PrimaryBufferSize = dsbcaps.dwBufferBytes; 
#endif

  PrimaryBuffer = malloc( PrimaryBufferSize );
  if( !PrimaryBuffer ) {
    bSoundWorking = FALSE;
    return(FALSE);
  }

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
  if( PrimaryBuffer )  free(PrimaryBuffer);
}

/*-----------------------------------------------------------------------*/
/*
  Re-Create sound buffer to write samples into, will stop existing and restart with new frequency
*/
void Audio_ReCreateDirectSoundBuffer(void)
{
#if 0
  if (lpDS) {
    /* Stop and delete old buffer */
    Audio_FreeSoundBuffer();

    /* Clear sample buffer, so plays silence */
    Sound_ClearMixBuffer();

    /* And create new one(will use new 'OutputAudioFreq' value) */
    Audio_CreateSoundBuffer();
  }
#endif
}

/*-----------------------------------------------------------------------*/
/*
  Set DirectSound playback frequency variable, pass as PLAYBACK_xxxx
*/
void Audio_SetOutputAudioFreq(int Frequency)
{
  /* Set new frequency, index into SoundPlayBackFrequencies[] */
  OutputAudioFreqIndex = Frequency;
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
  SDL_PauseAudio(1);
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
  int dsRetVal;
  int i;
//  int WriteCursor,CursorDiff;

  /* Modify ramp volume - ramp down if sound not enabled or not in windows mouse mode */
  if ( (((RampSetting==RAMP_DOWN) || (!ConfigureParams.Sound.bEnableSound)) && (PlayVolume>0.0f)) || bWindowsMouseMode ) {
    PlayVolume -= RAMP_DOWN_VOLUME_LEVEL;
    if (PlayVolume<=0.0f)
      PlayVolume = 0.0f;
  }
  else if ( (RampSetting==RAMP_UP) && (PlayVolume<1.0f) ) {
    PlayVolume += RAMP_UP_VOLUME_LEVEL;
    if (PlayVolume>=1.0f)
      PlayVolume = 1.0f;
  }

  if (PrimaryBuffer/*lpDSBPrimBuffer*/) {

    /* Do need to reset 'write' position? */
    if (bAquireWritePosition) {
    //  /* Get current write position */
    //  lpDSBPrimBuffer->GetCurrentPosition(NULL,(DWORD *)&WriteCursor);
    //  WriteOffset = WriteCursor+WRITE_INIT_POS;    /* + little gap */
      WriteOffset = 0;
      bAquireWritePosition = FALSE;
    }

    /* Lock sound buffer to get pointers */
    SDL_LockAudio();
    lpWrite = PrimaryBuffer + WriteOffset;
    dwLenBytes = Length;
    if( WriteOffset+Length>PrimaryBufferSize )  Length=PrimaryBufferSize-WriteOffset;  /* Fixme! */
    //dsRetVal = lpDSBPrimBuffer->Lock( WriteOffset, Length, &lpWrite1, &dwLenBytes1, &lpWrite2, &dwLenBytes2, 0 );
    //if (dsRetVal==DSERR_BUFFERLOST) {
    //  /* Lost focus of buffer, restore and try to lock again */
    //  lpDSBPrimBuffer->Restore();
    //  dsRetVal = lpDSBPrimBuffer->Lock( WriteOffset, Length, &lpWrite1, &dwLenBytes1, &lpWrite2, &dwLenBytes2, 0 );
    //}

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

    /* Now unlock the buffer. */
    //dsRetVal = lpDSBPrimBuffer->Unlock( (LPVOID)lpWrite1, dwLenBytes1,(LPVOID)lpWrite2, dwLenBytes2 );
    SDL_UnlockAudio();

    /* Update write buffer */
    if (pSamples) {
      WriteOffset += Length;
      if (WriteOffset>=PrimaryBufferSize)
        WriteOffset -= PrimaryBufferSize;
    }

    /* Are we playing? */
    if (!bPlayingBuffer) {
      SDL_PauseAudio(0);
      //lpDSBPrimBuffer->Play( 0, 0, DSBPLAY_LOOPING );
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
