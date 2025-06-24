/*
  Hatari - avi_record.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_AVI_RECORD_H
#define HATARI_AVI_RECORD_H


#define	AVI_RECORD_VIDEO_CODEC_BMP	1
#define	AVI_RECORD_VIDEO_CODEC_PNG	2

#define	AVI_RECORD_AUDIO_CODEC_PCM	1


extern char	AviRecordFile[FILENAME_MAX];

extern bool	Avi_RecordVideoStream ( void );
extern bool	Avi_RecordAudioStream ( int16_t pSamples[][2] , int SampleIndex , int SampleLength );

extern bool	Avi_AreWeRecording ( void );
extern bool	Avi_SetCompressionLevel(const char *str);
extern bool	Avi_StartRecording_WithConfig ( void );
extern bool	Avi_StopRecording ( void );
extern bool	Avi_StopRecording_WithMsg ( void );
extern void	Avi_SetSurface(SDL_Surface *surf);


#endif /* ifndef HATARI_AVI_RECORD_H */
