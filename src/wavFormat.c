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

#include "main.h"
#include "audio.h"
#include "dialog.h"
#include "file.h"
#include "misc.h"
#include "sound.h"


//HFILE WavFile;
//OFSTRUCT WavFileInfo;
BOOL bRecordingWav=FALSE;                // Is a WAV file open and recording?
int nWavOutputBytes;                     // Number of samples bytes saved

//-----------------------------------------------------------------------
/*
*/
BOOL WAVFormat_OpenFile(/*HWND hWnd,*/ char *pszWavFileName)
{
  static char szRiff[] = { "RIFF" };
  static char szWave[] = { "WAVE" };
  static char szFmt[] = { "fmt " };
  static char szData[] = { "data" };
  static unsigned int Blank=0;
  static unsigned int FmtLength=0x10;
  static unsigned short int SOne=0x01;
  static unsigned int SampleLength;
  static unsigned int BitsPerSample=8;
/* FIXME */
/*
  // Set frequency
  SampleLength = SoundPlayBackFrequencies[ConfigureParams.Sound.nPlaybackQuality];  // 11Khz, 22Khz or 44Khz

  // Create our file
  WavFile = OpenFile(pszWavFileName,&WavFileInfo,OF_CREATE | OF_WRITE);
  if (WavFile!=HFILE_ERROR) {
    // Create 'RIFF' chunk
    _hwrite(WavFile,(char *)szRiff,4);              // "RIFF" (ASCII Characters)
    _hwrite(WavFile,(char *)&Blank,sizeof(int));        // Total Length Of Package To Follow (Binary, little endian)
    _hwrite(WavFile,(char *)szWave,4);              // "WAVE" (ASCII Characters)

    // Create 'FORMAT' chunk
    _hwrite(WavFile,(char *)szFmt,4);              // "fmt_" (ASCII Characters)
    _hwrite(WavFile,(char *)&FmtLength,sizeof(int));      // Length Of FORMAT Chunk (Binary, always 0x10)
    _hwrite(WavFile,(char *)&SOne,sizeof(short int));      // Always 0x01
    _hwrite(WavFile,(char *)&SOne,sizeof(short int));      // Channel Numbers (Always 0x01=Mono, 0x02=Stereo)
    _hwrite(WavFile,(char *)&SampleLength,sizeof(int));      // Sample Rate (Binary, in Hz)
    _hwrite(WavFile,(char *)&SampleLength,sizeof(int));      // Bytes Per Second
    _hwrite(WavFile,(char *)&SOne,sizeof(short int));      // Bytes Per Sample: 1=8 bit Mono, 2=8 bit Stereo or 16 bit Mono, 4=16 bit Stereo
    _hwrite(WavFile,(char *)&BitsPerSample,sizeof(short int));  // Bits Per Sample

    // Create 'DATA' chunk
    _hwrite(WavFile,(char *)szData,4);              // "data" (ASCII Characters)
    _hwrite(WavFile,(char *)&Blank,sizeof(int));        // Length Of Data To Follow

    nWavOutputBytes = 0;
    bRecordingWav = TRUE;

    // Set status bar
    StatusBar_SetIcon(STATUS_ICON_SOUND,ICONSTATE_ON);
    // And inform user
    if (hWnd)
      Main_Message("WAV Sound data recording started.",PROG_NAME,MB_OK | MB_ICONINFORMATION);
  }
  else
    bRecordingWav = FALSE;

  // Ok, or failed?
  return(bRecordingWav);
*/
}

//-----------------------------------------------------------------------
/*
*/
void WAVFormat_CloseFile(/*HWND hWnd*/)
{
  int nWavFileBytes;
/* FIXME */
/*
  // Turn off icon
  StatusBar_SetIcon(STATUS_ICON_SOUND,ICONSTATE_OFF);

  if (bRecordingWav) {
    // Update headers with sizes
    nWavFileBytes = (12+24+8+nWavOutputBytes)-8;        // File length, less 8 bytes for 'RIFF' and length
    _llseek(WavFile,4,FILE_BEGIN);                // 'Total Length Of Package' element
    _hwrite(WavFile,(char *)&nWavFileBytes,sizeof(int));    // Total Length Of Package in 'RIFF' chunk

    _llseek(WavFile,12+24+4,FILE_BEGIN);            // 'Length' element
    _hwrite(WavFile,(char *)&nWavOutputBytes,sizeof(int));    // Length Of Data in 'DATA' chunk

    // Close file
    _lclose(WavFile);
    bRecordingWav = FALSE;

    // And inform user(this only happens from dialog)
    if (hWnd)
      Main_Message("WAV Sound data recording stopped.",PROG_NAME,MB_OK | MB_ICONINFORMATION);
  }
*/
}

//-----------------------------------------------------------------------
/*
*/
void WAVFormat_Update(char *pSamples,int Index)
{
  char Char;
  int i;
/* FIXME */
/*
  if (bRecordingWav) {
    // Output, better if did in two section if wrap
    for(i=0; i<SAMPLES_PER_FRAME; i++) {
      // Convert sample to 'signed' byte
      Char = pSamples[(Index+i)&MIXBUFFER_LENGTH];
      Char -= 127;
      // And store
      _hwrite(WavFile,(char *)&Char,sizeof(unsigned char));
    }

    // Add samples to wav file
    nWavOutputBytes += SAMPLES_PER_FRAME;
  }
*/
}
