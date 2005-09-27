/*
  Hatari - sound.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This is where we emulate the YM2149. To obtain cycle-accurate timing we store the current cycle
  time and this is incremented during each instruction. When a write occurs in the PSG registers
  we take the difference in time and generate this many samples using the previous register data.
  Now we begin again from this point. To make sure we always have 1/50th of samples we update
  the buffer generation every 1/50th second, just in case no write took place on the PSG.
  As with most 'sample' emulation it appears very quiet. We detect for any sample playback on a channel
  by a decay timer on the channel amplitude - this will remain high if the PSG register is constantly
  written to. We use this decay timer to boost the output of a sampled channel so the final sound is more
  even through-out.
  NOTE: If the emulator runs slower than 50fps it cannot update the buffers, but the sound thread still
  needs some data to play to prevent a 'pop'. The ONLY feasible solution is to play the same buffer again.
  I have tried all kinds of methods to play the sound 'slower', but this produces un-even timing in the
  sound and it simply doesn't work. If the emulator cannot keep the speed, users will have to turn off
  the sound - that's it.
*/
char Sound_rcsid[] = "Hatari $Id: sound.c,v 1.21 2005-09-27 08:53:50 thothy Exp $";

#include <SDL_types.h>

#include "main.h"
#include "audio.h"
#include "dmaSnd.h"
#include "file.h"
#include "int.h"
#include "log.h"
#include "memorySnapShot.h"
#include "misc.h"
#include "psg.h"
#include "sound.h"
#include "video.h"
#include "wavFormat.h"
#include "ymFormat.h"

#define LONGLONG Uint64

#define ENVELOPE_PERIOD(Fine,Coarse)  ((((Uint32)Coarse)<<8) + (Uint32)Fine)
#define NOISE_PERIOD(Freq)            (((((Uint32)Freq)&0x1f)<<11))
#define TONE_PERIOD(Fine,Coarse)      (((((Uint32)Coarse)&0x0f)<<8) + (Uint32)Fine)
#define MIXTABLE_SIZE    (256*8)        /* Large table, so don't overflow */
#define TONEFREQ_SHIFT   28             /* 4.28 fixed point */
#define NOISEFREQ_SHIFT  28             /* 4.28 fixed point */
#define ENVFREQ_SHIFT    16             /* 16.16 fixed */

#define SAMPLES_BUFFER_SIZE  1024
/* Number of generated samples per frame (eg. 44Khz=882) : */
#define SAMPLES_PER_FRAME  ((SoundPlayBackFrequencies[OutputAudioFreqIndex]+35)/nScreenRefreshRate)
/* Frequency of generated samples: */
#define SAMPLES_FREQ   (SoundPlayBackFrequencies[OutputAudioFreqIndex])
#define YM_FREQ        (2000000/SAMPLES_FREQ)      /* YM Frequency 2Mhz */


/* Original wave samples */
static int EnvelopeShapeValues[16*1024];                        /* Shape x Length(repeat 3rd/4th entries) */
/* Frequency and time period samples */
static Uint32 ChannelFreq[3], EnvelopeFreq, NoiseFreq;          /* Current frequency of each channel A,B,C,Envelope and Noise */
static int ChannelAmpDecayTime[3];                              /* Store counter to show if amplitude is changed to generate 'samples' */
static int Envelope[SAMPLES_BUFFER_SIZE],Noise[SAMPLES_BUFFER_SIZE];   /* Current sample for this time period */
/* Output channel data */
static int Channel_A_Buffer[SAMPLES_BUFFER_SIZE],Channel_B_Buffer[SAMPLES_BUFFER_SIZE],Channel_C_Buffer[SAMPLES_BUFFER_SIZE];
/* Use table to convert from (A+B+C) to clipped 8-bit for sound buffer */
static Sint8 MixTable[MIXTABLE_SIZE];                           /* -ve and +ve range */
static Sint8 *pMixTable = &MixTable[MIXTABLE_SIZE/2];           /* Signed index into above */
static int ActiveSndBufIdx;                                     /* Current working index into above mix buffer */
static int nSamplesToGenerate;                                  /* How many samples are needed for this time-frame */

/* global values */
BOOL bWriteEnvelopeFreq;                                        /* Did write to register '13' - causes frequency reset */
BOOL bWriteChannelAAmp, bWriteChannelBAmp, bWriteChannelCAmp;   /* Did write to amplitude registers? */
BOOL bEnvelopeFreqFlag;                                         /* As above, but cleared each frame for YM saving */
/* Buffer to store circular samples */
Sint8 MixBuffer[MIXBUFFER_SIZE];
int nGeneratedSamples;                                          /* Generated samples since audio buffer update */
int SoundCycles;


/*-----------------------------------------------------------------------*/
/* Envelope shape table */
typedef struct
{
  int WaveStart[4], WaveDelta[4];
} ENVSHAPE;

/* Envelope shapes */
static ENVSHAPE EnvShapes[16] =
{
 { {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  00xx  */
 { {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  00xx  */
 { {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  00xx  */
 { {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  00xx  */
 { {-128,-128,-128,-128},   {1, 0, 0, 0} },   /*  /_____  01xx  */
 { {-128,-128,-128,-128},   {1, 0, 0, 0} },   /*  /_____  01xx  */
 { {-128,-128,-128,-128},   {1, 0, 0, 0} },   /*  /_____  01xx  */
 { {-128,-128,-128,-128},   {1, 0, 0, 0} },   /*  /_____  01xx  */
 { {127,127,127,127},       {-1,-1,-1,-1} },  /*  \\\\\\  1000  */
 { {127,-128,-128,-128},    {-1, 0, 0, 0} },  /*  \_____  1001  */
 { {127,-128,127,-128},     {-1, 1,-1, 1} },  /*  \/\/\/  1010  */
 { {127,127,127,127},       {-1, 0, 0, 0} },  /*  \~~~~~  1011  */
 { {-128,-128,-128,-128},   {1, 1, 1, 1} },   /*  //////  1100  */
 { {-128,127,127,127},      {1, 0, 0, 0} },   /*  /~~~~~  1101  */
 { {-128,127,-128,127},     {1,-1, 1,-1} },   /*  /\/\/\  1110  */
 { {-128,-128,-128,-128},   {1, 0, 0, 0} }    /*  /_____  1111  */
};

/* Square wave look up table */
static int SquareWave[16] = { 127,127,127,127,127,127,127,127, -128,-128,-128,-128,-128,-128,-128,-128 };
/* LogTable */
static int LogTable[256];
static int LogTable16[16];
static int *pEnvelopeLogTable = &LogTable[128];


/*-----------------------------------------------------------------------*/
/*
  Create Log tables
*/
static void Sound_CreateLogTables(void)
{
  float a;
  int i;

  /* Generate 'log' table for envelope output. It isn't quite a 'log' but it mimicks the ST */
  /* output very well */
  a = 1.0f;
  for(i=0; i<256; i++)
  {
    LogTable[255-i] = (int)(255*a);
    a /= 1.02f;
  }
  LogTable[0] = 0;

  /* And a 16 entry version(thanks to Nick for the '/= 1.5' bit) */
  /* This is VERY important for clear sample playback */
  a = 1.0f;
  for(i=0; i<15; i++)
  {
    LogTable16[15-i] = (int)(255*a);
    a /= 1.5f;
  }
  LogTable16[0] = 0;
}


/*-----------------------------------------------------------------------*/
/*
  Create envelope shape, store to table
  ( Wave is stored as 4 cycles, where cycles 1,2 are start and 3,4 are looped )
*/
static void Sound_CreateEnvelopeShape(ENVSHAPE *pEnvShape,int *pEnvelopeValues)
{
  int i,j,Value;

  /* Create shape */
  for(i=0; i<4; i++)
  {
    Value = pEnvShape->WaveStart[i];        /* Set starting value for gradient */
    for(j=0; j<256; j++,Value+=pEnvShape->WaveDelta[i])
      *pEnvelopeValues++ = Misc_LimitInt(Value,-128,127);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Create YM2149 envelope shapes(x16)
*/
static void Sound_CreateEnvelopeShapes(void)
{
  int i;

  /* Create 'envelopes' for YM table */
  for(i=0; i<16; i++)
    Sound_CreateEnvelopeShape(&EnvShapes[i],&EnvelopeShapeValues[i*1024]);
}


/*-----------------------------------------------------------------------*/
/*
  Create table to clip samples top 8-bit range
  This keeps then 'signed', although many sound cards want 'unsigned' values,
  but we keep them signed so we can vary the volume easily.
*/
static void Sound_CreateSoundMixClipTable(void)
{
  int i,v;

  /* Create table to 'clip' values to -128...127 */
  for(i=0; i<MIXTABLE_SIZE; i++)
  {
    v = (float)(i-(MIXTABLE_SIZE/2)) * 0.3f;    /* Scale, to prevent clipping */
    if (v<-128)  v = -128;                      /* Limit -128..128 */
    if (v>127)  v = 127;
    MixTable[i] = v;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Init sound tables and envelopes
*/
void Sound_Init(void)
{
  Sound_CreateLogTables();
  Sound_CreateEnvelopeShapes();
  Sound_CreateSoundMixClipTable();

  Sound_Reset();
}


/*-----------------------------------------------------------------------*/
/*
  Reset the sound emulation
*/
void Sound_Reset(void)
{
  int i;

  /* Lock audio system before accessing variables which are used by the
   * callback function, too! */
  Audio_Lock();

  /* Clear sound mixing buffer: */
  memset(MixBuffer, 0, MIXBUFFER_SIZE);

  /* Clear cycle counts, buffer index and register '13' flags */
  SoundCycles = 0;
  bEnvelopeFreqFlag = FALSE;
  bWriteEnvelopeFreq = FALSE;
  bWriteChannelAAmp = bWriteChannelBAmp = bWriteChannelCAmp = FALSE;

  CompleteSndBufIdx = 0;
  ActiveSndBufIdx =  (SoundBufferSize + SAMPLES_PER_FRAME) % MIXBUFFER_SIZE;
  nGeneratedSamples = 0;

  /* Clear frequency counter */
  for(i=0; i<3; i++)
  {
    ChannelFreq[i] =
    ChannelAmpDecayTime[i] = 0;
  }
  EnvelopeFreq = NoiseFreq = 0;

  Audio_Unlock();
}


/*-----------------------------------------------------------------------*/
/*
  Reset the sound buffer index variables.
*/
void Sound_ResetBufferIndex(void)
{
  Audio_Lock();
  ActiveSndBufIdx =  (CompleteSndBufIdx + SoundBufferSize + SAMPLES_PER_FRAME)
                     % MIXBUFFER_SIZE;
  Audio_Unlock();
}


/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void Sound_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  MemorySnapShot_Store(ChannelFreq,sizeof(ChannelFreq));
  MemorySnapShot_Store(&EnvelopeFreq,sizeof(EnvelopeFreq));
  MemorySnapShot_Store(&NoiseFreq,sizeof(NoiseFreq));
}


/*-----------------------------------------------------------------------*/
/*
  Find how many samples to generate and store in 'nSamplesToGenerate'
  Also update 'SoundCycles' to store how many we actually did so generates set
  amount each frame.
*/
static void Sound_SetSamplesPassed(void)
{
  int nSampleCycles;
  int nSamplesPerFrame;
  int Dec=1;

  /* Check how many cycles have passed, as we use this to help find out if we are playing sample data */

  /* First, add decay to channel amplitude variables */
  if (SoundCycles>(CYCLES_PER_FRAME/4))
    Dec = 16;                            /* Been long time between sound writes, must be normal tone sound */

  if (!bWriteChannelAAmp)                /* Not written to amplitude, decay value */
  {
    ChannelAmpDecayTime[0]-=Dec;
    if (ChannelAmpDecayTime[0]<0)  ChannelAmpDecayTime[0] = 0;
  }
  if (!bWriteChannelBAmp)
  {
    ChannelAmpDecayTime[1]-=Dec;
    if (ChannelAmpDecayTime[1]<0)  ChannelAmpDecayTime[1] = 0;
  }
  if (!bWriteChannelCAmp)
  {
    ChannelAmpDecayTime[2]-=Dec;
    if (ChannelAmpDecayTime[2]<0)  ChannelAmpDecayTime[2] = 0;
  }

  /* 160256 cycles per VBL, 44Khz = 882 samples per VBL */
  /* 882/160256 samples per clock cycle */
  nSamplesPerFrame = SAMPLES_PER_FRAME;
#if 0  /* Use floats for calculation */
  nSamplesToGenerate = (int)( (float)SoundCycles * ((float)nSamplesPerFrame/(float)CYCLES_PER_FRAME) );
  if (nSamplesToGenerate > nSamplesPerFrame)
    nSamplesToGenerate = nSamplesPerFrame;

  nSampleCycles = (int)( (float)nSamplesToGenerate / ((float)nSamplesPerFrame/(float)CYCLES_PER_FRAME) );
  SoundCycles -= nSampleCycles;
#else  /* Use integers for calculation - both of these calculations should fit into 32-bit int */
  nSamplesToGenerate = SoundCycles * nSamplesPerFrame / CYCLES_PER_FRAME;
  if (nSamplesToGenerate > nSamplesPerFrame)
    nSamplesToGenerate = nSamplesPerFrame;

  nSampleCycles = nSamplesToGenerate * CYCLES_PER_FRAME / nSamplesPerFrame;
  SoundCycles -= nSampleCycles;
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Generate envelope wave for this time-frame
*/
static void Sound_GenerateEnvelope(unsigned char EnvShape, unsigned char Fine, unsigned char Coarse)
{
  int *pEnvelopeValues;
  Uint32 EnvelopePeriod, EnvelopeFreqDelta;
  int i;

  /* Find envelope details */
  if (bWriteEnvelopeFreq)
    EnvelopeFreq = 0;
  pEnvelopeValues = &EnvelopeShapeValues[ (EnvShape&0x0f)*1024 ];          /* Envelope shape values */
  EnvelopePeriod = ENVELOPE_PERIOD((Uint32)Fine, (Uint32)Coarse);

  if (EnvelopePeriod==0)                                                   /* Handle div by zero */
    EnvelopeFreqDelta = 0;
  else
    EnvelopeFreqDelta = ((LONGLONG)YM_FREQ<<ENVFREQ_SHIFT) / (EnvelopePeriod);  /* 16.16 fixed point */

  /* Create envelope from current shape and frequency */
  for(i=0; i<nSamplesToGenerate; i++)
  {
    Envelope[i] = pEnvelopeValues[EnvelopeFreq>>ENVFREQ_SHIFT];           /* Store envelope wave, already applied 'log' function */
    EnvelopeFreq += EnvelopeFreqDelta;
    if (EnvelopeFreq&0xfe000000)
      EnvelopeFreq = 0x02000000 | (EnvelopeFreq&0x01ffffff);              /* Keep in range 512-1024 once past 511! */
  }
}


/*-----------------------------------------------------------------------*/
/*
  Generate nosie for this time-frame
*/
static void Sound_GenerateNoise(unsigned char MixerControl, unsigned char NoiseGen)
{
  int NoiseValue;
  Uint32 NoisePeriod, NoiseFreqDelta;
  int i;

  NoisePeriod = NOISE_PERIOD((Uint32)NoiseGen);

  if (NoisePeriod==0)                                            /* Handle div by zero */
    NoiseFreqDelta = 0;
  else
    NoiseFreqDelta = (((LONGLONG)YM_FREQ)<<NOISEFREQ_SHIFT) / NoisePeriod;  /* 4.28 fixed point */

  /* Generate noise samples */
  for(i=0; i<nSamplesToGenerate; i++)
  {
    NoiseValue = (unsigned int)Misc_GetRandom()%96;              /* Get random value */
    if (SquareWave[NoiseFreq>>NOISEFREQ_SHIFT]<=0)               /* Add to square wave at given frequency */
      NoiseValue = -NoiseValue;

    Noise[i] = NoiseValue;
    NoiseFreq += NoiseFreqDelta;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Generate channel of samples for this time-frame
*/
static void Sound_GenerateChannel(int *pBuffer, unsigned char ToneFine, unsigned char ToneCoarse, unsigned char Amplitude, unsigned char MixerControl, Uint32 *pChannelFreq, int MixMask)
{   
  int *pNoise = Noise, *pEnvelope = Envelope;
  Uint32 ToneFreq = *pChannelFreq;
  Uint32 TonePeriod;
  Uint32 ToneFreqDelta;
  int i,Amp,Mix;
  int ToneOutput,NoiseOutput,MixerOutput,EnvelopeOutput,AmplitudeOutput;

  TonePeriod = TONE_PERIOD((Uint32)ToneFine, (Uint32)ToneCoarse);
  /* Find frequency of channel */
  if (TonePeriod==0)
    ToneFreqDelta = 0;                                  /* Handle div by zero */
  else
    ToneFreqDelta = (((LONGLONG)YM_FREQ)<<TONEFREQ_SHIFT) / TonePeriod;    /* 4.28 fixed point */
  Amp = LogTable16[(Amplitude&0x0f)];
  Mix = (MixerControl>>MixMask)&9;                      /* Read I/O Mixer */

  /* Check if we are trying to play a 'sample' - we need to up the volume on these as they tend to be rather quiet */
  if ((Amplitude&0x10)==0)                /* Fixed level amplitude? */
  {
    ChannelAmpDecayTime[MixMask]++;       /* Increment counter to find out if we are playing samples... */
    if (ChannelAmpDecayTime[MixMask]>16)
      ChannelAmpDecayTime[MixMask] = 16;  /* And limit */
  }

  for(i=0; i<nSamplesToGenerate; i++)
  {
    /* Output from Tone Generator(0-255) */
    ToneOutput = SquareWave[ToneFreq>>TONEFREQ_SHIFT];

    /* Output from Noise Generator(0-255) */
    NoiseOutput = *pNoise++; 
    /* Output from Mixer(combines Tone+Noise) */
    switch (Mix) {
      case 0:    /* Has Noise and Tone */
        MixerOutput = NoiseOutput+ToneOutput;
        break;
      case 1:    /* Has Noise */
        MixerOutput = NoiseOutput;
        break;
      case 8:    /* Has Tone */
        MixerOutput = ToneOutput;
        break;

      default:  /* This is used to emulate samples - should give no output, but ST gives set tone!!?? */
        /* MixerControl gets set to give a continuous tone and then then Amplitude */
        /* of channels A,B and C get changed with all other registers in the PSG */
        /* staying as zero's. This produces the sounds from Quartet, Speech, NoiseTracker etc...! */
        MixerOutput = 127;
    }

    EnvelopeOutput = pEnvelopeLogTable[*pEnvelope++];

    if ((Amplitude&0x10)==0)
    {
      AmplitudeOutput = Amp;          /* Fixed level amplitude */

      /* As with most emulators, sample playback is always 'quiet'. We check to see if */
      /* the amplitude of a channel is repeatedly changing and when this is detected we */
      /* scale the volume accordingly */
      if (ChannelAmpDecayTime[MixMask]>8)
        AmplitudeOutput <<= 1;        /* Scale up by a factor of 2 */
    }
    else
      AmplitudeOutput = EnvelopeOutput;

    *pBuffer++ = (MixerOutput*AmplitudeOutput)>>8;

    ToneFreq+=ToneFreqDelta;
  }

  /* Store back incremented frequency, for next call */
  *pChannelFreq = ToneFreq;
}


/*-----------------------------------------------------------------------*/
/*
  Generate samples for all channels during this time-frame
*/
static void Sound_GenerateSamples(void)
{
  int *pChannelA=Channel_A_Buffer, *pChannelB=Channel_B_Buffer, *pChannelC=Channel_C_Buffer;
  int i;

  /* Anything to do? */
  if (nSamplesToGenerate>0)
  {
    /* Generate envelope/noise samples for this time */
    Sound_GenerateEnvelope(PSGRegisters[PSG_REG_ENV_SHAPE],PSGRegisters[PSG_REG_ENV_FINE],PSGRegisters[PSG_REG_ENV_COARSE]);
    Sound_GenerateNoise(PSGRegisters[PSG_REG_MIXER_CONTROL],PSGRegisters[PSG_REG_NOISE_GENERATOR]);

    /* Generate 3 channels, store to separate buffer so can mix/clip */
    Sound_GenerateChannel(pChannelA,PSGRegisters[PSG_REG_CHANNEL_A_FINE],PSGRegisters[PSG_REG_CHANNEL_A_COARSE],PSGRegisters[PSG_REG_CHANNEL_A_AMP],PSGRegisters[PSG_REG_MIXER_CONTROL],&ChannelFreq[0],0); 
    Sound_GenerateChannel(pChannelB,PSGRegisters[PSG_REG_CHANNEL_B_FINE],PSGRegisters[PSG_REG_CHANNEL_B_COARSE],PSGRegisters[PSG_REG_CHANNEL_B_AMP],PSGRegisters[PSG_REG_MIXER_CONTROL],&ChannelFreq[1],1);
    Sound_GenerateChannel(pChannelC,PSGRegisters[PSG_REG_CHANNEL_C_FINE],PSGRegisters[PSG_REG_CHANNEL_C_COARSE],PSGRegisters[PSG_REG_CHANNEL_C_AMP],PSGRegisters[PSG_REG_MIXER_CONTROL],&ChannelFreq[2],2);

    /* Mix channels together, using table to clip and convert to proper 8-bit type */
    for(i=0; i<nSamplesToGenerate; i++)
      MixBuffer[(i+ActiveSndBufIdx)%MIXBUFFER_SIZE] = pMixTable[(*pChannelA++) + (*pChannelB++) + (*pChannelC++)];

    DmaSnd_GenerateSamples(ActiveSndBufIdx, nSamplesToGenerate);

    ActiveSndBufIdx = (ActiveSndBufIdx + nSamplesToGenerate) % MIXBUFFER_SIZE;
    nGeneratedSamples += nSamplesToGenerate;

    /* Reset the write to register '13' flag */
    bWriteEnvelopeFreq = FALSE;
    /* And amplitude write flags */
    bWriteChannelAAmp = bWriteChannelBAmp = bWriteChannelCAmp = FALSE;
  }
}


/*-----------------------------------------------------------------------*/
/*
  This is called to built samples up until this clock cycle
*/
void Sound_Update(void)
{
  int OldSndBufIdx = ActiveSndBufIdx;

  /* Make sure that we don't interfere with the audio callback function */
  Audio_Lock();

  /* Find how many to generate */
  Sound_SetSamplesPassed();
  /* And generate */
  Sound_GenerateSamples();

  /* Allow audio callback function to occur again */
  Audio_Unlock();

  /* Save to WAV file, if open */
  WAVFormat_Update(MixBuffer, OldSndBufIdx, nSamplesToGenerate);
}


/*-----------------------------------------------------------------------*/
/*
  On each VBL (50fps) complete samples.
*/
void Sound_Update_VBL(void)
{
  Sound_Update();

  /* Clear write to register '13', used for YM file saving */
  bEnvelopeFreqFlag = FALSE;
}


/*-----------------------------------------------------------------------*/
/*
  This is called from the audio callback function to create enough samples
  to fill the current sound buffer.
*/
void Sound_UpdateFromAudioCallBack(void)
{
  /* If there are already enough samples or if we are recording, we should
   * not generate more samples here! */
  if(nGeneratedSamples >= SoundBufferSize || Sound_AreWeRecording())
    return;

  nSamplesToGenerate = SoundBufferSize - nGeneratedSamples;

  Sound_GenerateSamples();
}


/*-----------------------------------------------------------------------*/
/*
  Start recording sound, as .YM or .WAV output
*/
BOOL Sound_BeginRecording(char *pszCaptureFileName)
{
  BOOL bRet;

  if (!pszCaptureFileName || strlen(pszCaptureFileName) <= 3)
  {
    Log_Printf(LOG_ERROR, "Illegal sound recording file name!\n");
    return FALSE;
  }

  /* Did specify .YM or .WAV? If neither report error */
  if (File_DoesFileExtensionMatch(pszCaptureFileName,".ym"))
    bRet = YMFormat_BeginRecording(pszCaptureFileName);
  else if (File_DoesFileExtensionMatch(pszCaptureFileName,".wav"))
    bRet = WAVFormat_OpenFile(pszCaptureFileName);
  else
  {
    Log_AlertDlg(LOG_ERROR, "Unknown Sound Recording format.\n"
                            "Please specify a .YM or .WAV output file.");
    bRet = FALSE;
  }

  return bRet;
}


/*-----------------------------------------------------------------------*/
/*
  End sound recording
*/
void Sound_EndRecording(void)
{
  /* Stop sound recording and close files */
  if (bRecordingYM)
    YMFormat_EndRecording();
  if (bRecordingWav)
    WAVFormat_CloseFile();
}


/*-----------------------------------------------------------------------*/
/*
  Are we recording sound data?
*/
BOOL Sound_AreWeRecording(void)
{
  return(bRecordingYM || bRecordingWav);
}
