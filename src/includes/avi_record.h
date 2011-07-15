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


extern bool	bRecordingAvi;
extern int	AviRecordDefaultVcodec;
extern bool	AviRecordDefaultCrop;
extern int	AviRecordDefaultFps;
extern char	AviRecordFile[FILENAME_MAX];

extern bool	Avi_RecordVideoStream ( void );
extern bool	Avi_RecordAudioStream ( Sint16 pSamples[][2] , int SampleIndex , int SampleLength );

extern bool	Avi_AreWeRecording ( void );
extern bool	Avi_StartRecording ( char *FileName , bool CropGui , Uint32 Fps , Uint32 Fps_scale , int VideoCodec );
extern bool	Avi_StopRecording ( void );


#endif /* ifndef HATARI_AVI_RECORD_H */
