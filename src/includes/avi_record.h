/*
  Hatari - avi_record.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_AVI_RECORD_H
#define HATARI_AVI_RECORD_H


#define	AVI_RECORD_VIDEO_CODEC_BMP	1
#define	AVI_RECORD_VIDEO_CODEC_PNG	2

#define	AVI_RECORD_AUDIO_CODEC_PCM	1


extern bool	AviRecording;
extern int	AviRecordDefaultVcodec;
extern bool	AviRecordDefaultCrop;
extern int	AviRecordDefaultFps;
extern char	AviRecordFile[FILENAME_MAX];

extern bool	AviRecordVideoStream ( void );
extern bool	AviRecordAudioStream ( Sint16 pSamples[][2] , int SampleIndex , int SampleLength );

extern bool	Avi_AreWeRecording ( void );
extern void	AviStartRecording ( char *FileName , bool CropGui , int Fps , int VideoCodec );
extern void	AviStopRecording ( void );


#endif /* ifndef HATARI_AVI_RECORD_H */
