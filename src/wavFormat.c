/*
  Hatari

  WAV File output

  As well as YM file output we also have output in Windows .WAV format. As currently there is no application that
  can re-play saved sample data from an ST emulator .WAV output seems a good idea. Also it's noticed that ST Sound's
  playback routine are not as good as the ones currently in Hatari.
  These .WAV files can then be run through convertors to any other format, such as MP3.
  We simply save out the WAVE format headers and then write the sample data(at the current rate of playback)
  as we build it up each frame. When we stop recording we complete the size information in the headers and close up.


  RIFF Chunk (12 bytes in length total) Byte Number
    0 - 3  "RIFF" (ASCII Characters)
    4 - 7  Total Length Of Package To Follow (Binary, little endian)
    8 - 12  "WAVE" (ASCII Characters)

  FORMAT Chunk (24 bytes in length total) Byte Number
    0 - 3  "fmt_" (ASCII Characters)
    4 - 7  Length Of FORMAT Chunk (Binary, always 0x10)
    8 - 9  Always 0x01
    10 - 11  Channel Numbers (Always 0x01=Mono, 0x02=Stereo)
    12 - 15  Sample Rate (Binary, in Hz)
    16 - 19  Bytes Per Second
    20 - 21  Bytes Per Sample: 1=8 bit Mono, 2=8 bit Stereo or 16 bit Mono, 4=16 bit Stereo
    22 - 23  Bits Per Sample

  DATA Chunk Byte Number
    0 - 3  "data" (ASCII Characters)
    4 - 7  Length Of Data To Follow
    8 - end  Data (Samples)
*/

#include <SDL_endian.h>

#include "main.h"
#include "audio.h"
#include "dialog.h"
#include "file.h"
#include "misc.h"
#include "sound.h"


static FILE *WavFileHndl;
static int nWavOutputBytes;             /* Number of samples bytes saved */
BOOL bRecordingWav = FALSE;             /* Is a WAV file open and recording? */


/*-----------------------------------------------------------------------*/
/*
*/
BOOL WAVFormat_OpenFile(char *pszWavFileName)
{
  const Uint32 Blank = 0;
  const Uint32 FmtLength = SDL_SwapLE32(0x10);
  const Uint16 SOne = SDL_SwapLE16(0x01);
  const Uint32 BitsPerSample = SDL_SwapLE32(8);
  Uint32 SampleLength;

  /* Set frequency (11Khz, 22Khz or 44Khz) */
  SampleLength = SDL_SwapLE32(SoundPlayBackFrequencies[ConfigureParams.Sound.nPlaybackQuality]);

  /* Create our file */
  WavFileHndl = fopen(pszWavFileName, "wb");

  if (WavFileHndl!=NULL)
  {
    /* Create 'RIFF' chunk */
    fwrite("RIFF", 1, 4, WavFileHndl);               /* "RIFF" (ASCII Characters) */
    fwrite(&Blank, sizeof(Uint32), 1, WavFileHndl);  /* Total Length Of Package To Follow (Binary, little endian) */
    fwrite("WAVE", 1, 4, WavFileHndl);               /* "WAVE" (ASCII Characters) */

    /* Create 'FORMAT' chunk */
    fwrite("fmt ", 1, 4, WavFileHndl);                     /* "fmt_" (ASCII Characters) */
    fwrite(&FmtLength, sizeof(Uint32), 1, WavFileHndl);    /* Length Of FORMAT Chunk (Binary, always 0x10) */
    fwrite(&SOne, sizeof(Uint16), 1, WavFileHndl);         /* Always 0x01 */
    fwrite(&SOne, sizeof(Uint16), 1, WavFileHndl);         /* Channel Numbers (Always 0x01=Mono, 0x02=Stereo) */
    fwrite(&SampleLength, sizeof(Uint32), 1, WavFileHndl); /* Sample Rate (Binary, in Hz) */
    fwrite(&SampleLength, sizeof(Uint32), 1, WavFileHndl); /* Bytes Per Second */
    fwrite(&SOne, sizeof(Uint16), 1, WavFileHndl);         /* Bytes Per Sample: 1=8 bit Mono, 2=8 bit Stereo or 16 bit Mono, 4=16 bit Stereo */
    fwrite(&BitsPerSample, sizeof(Uint16), 1, WavFileHndl); /* Bits Per Sample */

    /* Create 'DATA' chunk */
    fwrite("data", 1, 4, WavFileHndl);               /* "data" (ASCII Characters) */
    fwrite(&Blank, sizeof(Uint32), 1, WavFileHndl);  /* Length Of Data To Follow */

    nWavOutputBytes = 0;
    bRecordingWav = TRUE;

    /* Set status bar */
    /*StatusBar_SetIcon(STATUS_ICON_SOUND,ICONSTATE_ON);*/
    /* And inform user */
    Main_Message("WAV Sound data recording started.",PROG_NAME /*,MB_OK | MB_ICONINFORMATION*/);
  }
  else
    bRecordingWav = FALSE;

  /* Ok, or failed? */
  return(bRecordingWav);
}


/*-----------------------------------------------------------------------*/
/*
*/
void WAVFormat_CloseFile()
{
  /* Turn off icon */
  /*StatusBar_SetIcon(STATUS_ICON_SOUND,ICONSTATE_OFF);*/

  if (bRecordingWav)
  {
    Uint32 nWavFileBytes;
    Uint32 nWavLEOutBytes;

    /* Update headers with sizes */
    nWavFileBytes = SDL_SwapLE32((12+24+8+nWavOutputBytes)-8);  /* File length, less 8 bytes for 'RIFF' and length */
    fseek(WavFileHndl, 4, SEEK_SET);                            /* 'Total Length Of Package' element */
    fwrite(&nWavFileBytes, sizeof(Uint32), 1, WavFileHndl);     /* Total Length Of Package in 'RIFF' chunk */

    fseek(WavFileHndl, 12+24+4, SEEK_SET);                      /* 'Length' element */
    nWavLEOutBytes = SDL_SwapLE32(nWavOutputBytes);
    fwrite(&nWavLEOutBytes, sizeof(Uint32), 1, WavFileHndl);    /* Length Of Data in 'DATA' chunk */

    /* Close file */
    fclose(WavFileHndl);
    WavFileHndl = NULL;
    bRecordingWav = FALSE;

    /* And inform user */
    Main_Message("WAV Sound data recording stopped.",PROG_NAME /*,MB_OK | MB_ICONINFORMATION*/);
  }
}


/*-----------------------------------------------------------------------*/
/*
*/
void WAVFormat_Update(char *pSamples,int Index)
{
  Sint8 sample;
  int i;

  if (bRecordingWav)
  {
    /* Output, better if did in two section if wrap */
    for(i=0; i<SAMPLES_PER_FRAME; i++)
    {
      /* Convert sample to 'signed' byte */
      sample = pSamples[(Index+i)&MIXBUFFER_LENGTH] - 127;
      /* And store */
      fwrite(&sample, sizeof(Sint8), 1, WavFileHndl);
    }

    /* Add samples to wav file */
    nWavOutputBytes += SAMPLES_PER_FRAME;
  }
}

