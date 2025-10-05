/*
  Hatari - wavFormat.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  WAV File output

  As well as YM file output we also have output in .WAV format. These .WAV
  files can then be run through converters to any other format, such as MP3.
  We simply save out the WAVE format headers and then write the sample data
  (at the current rate of playback) as we build it up each frame. When we stop
  recording we complete the size information in the headers and close up.


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
const char WAVFormat_fileid[] = "Hatari wavFormat.c";

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "endianswap.h"
#include "file.h"
#include "log.h"
#include "sound.h"
#include "wavFormat.h"


static FILE *WavFileHndl;
static int nWavOutputBytes;             /* Number of samples bytes saved */
bool bRecordingWav = false;             /* Is a WAV file open and recording? */


static uint8_t WavHeader[] =
{
	/* RIFF chunk */
	'R', 'I', 'F', 'F',      /* "RIFF" (ASCII Characters) */
	0, 0, 0, 0,              /* Total Length Of Package To Follow (patched when file is closed) */
	'W', 'A', 'V', 'E',      /* "WAVE" (ASCII Characters) */
	/* Format chunk */
	'f', 'm', 't', ' ',      /* "fmt_" (ASCII Characters) */
	0x10, 0, 0, 0,           /* Length Of FORMAT Chunk (always 0x10) */
	0x01, 0,                 /* Always 0x01 */
	0x02, 0,                 /* Number of channels (2 for stereo) */
	0, 0, 0, 0,              /* Sample rate (patched when file header is written) */
	0, 0, 0, 0,              /* Bytes per second (patched when file header is written) */
	0x04, 0,                 /* Bytes per sample (4 = 16 bit stereo) */
	0x10, 0,                 /* Bits per sample (16 bit) */
	/* Data chunk */
	'd', 'a', 't', 'a',
	0, 0, 0, 0,              /* Length of data to follow (will be patched when file is closed) */
};


/**
 * Open WAV output file and write header.
 */
bool WAVFormat_OpenFile(char *pszWavFileName)
{

	uint32_t nSampleFreq, nBytesPerSec;

	bRecordingWav = false;
	nWavOutputBytes = 0;

	/* Set frequency (11Khz, 22Khz or 44Khz) */
	nSampleFreq = ConfigureParams.Sound.nPlaybackFreq;
	/* multiply by 4 for 16 bit stereo */
	nBytesPerSec = nSampleFreq * 4;

	/* Create our file */
	WavFileHndl = fopen(pszWavFileName, "wb");
	if (!WavFileHndl)
	{
		perror("WAVFormat_OpenFile");
		Log_AlertDlg(LOG_ERROR, "WAV recording: Failed to open file!");
		return false;
	}

	/* Patch sample frequency in header structure */
	WavHeader[24] = (uint8_t)nSampleFreq;
	WavHeader[25] = (uint8_t)(nSampleFreq >> 8);
	WavHeader[26] = (uint8_t)(nSampleFreq >> 16);
	WavHeader[27] = (uint8_t)(nSampleFreq >> 24);

	/* Patch bytes per second in header structure */
	WavHeader[28] = (uint8_t)nBytesPerSec;
	WavHeader[29] = (uint8_t)(nBytesPerSec >> 8);
	WavHeader[30] = (uint8_t)(nBytesPerSec >> 16);
	WavHeader[31] = (uint8_t)(nBytesPerSec >> 24);

	/* Write header to file */
	if (fwrite(&WavHeader, sizeof(WavHeader), 1, WavFileHndl) == 1)
	{
		bRecordingWav = true;
		Log_AlertDlg(LOG_INFO, "WAV sound data recording has been started.");
	}
	else
	{
		perror("WAVFormat_OpenFile");
		Log_AlertDlg(LOG_ERROR, "WAV recording: Failed to write header!");
	}

	/* Ok, or failed? */
	return bRecordingWav;
}


/**
 * Write sizes to WAV header, then close the WAV file.
 */
void WAVFormat_CloseFile(void)
{
	if (bRecordingWav)
	{
		uint32_t nWavFileBytes;
		uint32_t nWavLEOutBytes;

		bRecordingWav = false;

		/* Update headers with sizes */
		nWavFileBytes = le_swap32((12+24+8+nWavOutputBytes)-8);  /* File length, less 8 bytes for 'RIFF' and length */
		/* Seek to 'Total Length Of Package' element and
		 * write total length of package in 'RIFF' chunk */
		if (fseek(WavFileHndl, 4, SEEK_SET) != 0 ||
		    fwrite(&nWavFileBytes, sizeof(uint32_t), 1, WavFileHndl) != 1)
		{
			perror("WAVFormat_CloseFile");
			fclose(WavFileHndl);
			WavFileHndl = NULL;
			return;
		}

		nWavLEOutBytes = le_swap32(nWavOutputBytes);
		/* Seek to 'Length' element and write length of data in 'DATA' chunk */
		if (fseek(WavFileHndl, 12+24+4, SEEK_SET) != 0
		    || fwrite(&nWavLEOutBytes, sizeof(uint32_t), 1, WavFileHndl) != 1)
		{
			perror("WAVFormat_CloseFile");
		}

		/* Close file */
		fclose(WavFileHndl);
		WavFileHndl = NULL;

		/* And inform user */
		Log_AlertDlg(LOG_INFO, "WAV Sound data recording has been stopped.");
	}
}


/**
 * Update WAV file with current samples
 */
void WAVFormat_Update(int16_t pSamples[][2], int Index, int Length)
{
	int16_t sample[2];
	int i;
	int idx;

	if (bRecordingWav)
	{
		/* Output, better if did in two section if wrap */
		idx = Index & AUDIOMIXBUFFER_SIZE_MASK;
		for(i = 0; i < Length; i++)
		{
			/* Convert sample to little endian */
			sample[0] = le_swap16(pSamples[idx][0]);
			sample[1] = le_swap16(pSamples[idx][1]);
			idx = ( idx+1 ) & AUDIOMIXBUFFER_SIZE_MASK;
			/* And store */
			if (fwrite(&sample, sizeof(sample), 1, WavFileHndl) != 1)
			{
				perror("WAVFormat_Update");
				WAVFormat_CloseFile();
				return;
			}
		}

		/* Add samples to wav file length counter */
		nWavOutputBytes += Length * 4;
	}
}
