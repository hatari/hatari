/*
  Hatari - screenConvert.h

  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.
*/


extern int ScrUpdateFlag;
extern BOOL bScrDoubleY;

extern void ConvertLowRes_320x16Bit(void);
extern void ConvertLowRes_640x16Bit(void);
extern void Line_ConvertLowRes_640x16Bit(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax);
extern void ConvertMediumRes_640x16Bit(void);
extern void Line_ConvertMediumRes_640x16Bit(Uint32 *edi, Uint32 *ebp, Uint16 *esi, Uint32 eax);
extern void ConvertLowRes_320x8Bit(void);
extern void ConvertLowRes_640x8Bit(void);
extern void Line_ConvertLowRes_640x8Bit(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax);
extern void ConvertMediumRes_640x8Bit(void);
extern void Line_ConvertMediumRes_640x8Bit(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax);
extern void ConvertHighRes_640x8Bit(void);
extern void ConvertHighRes_640x1Bit(void);
extern void ConvertSpec512_320x16Bit(void);
extern void ConvertSpec512_640x16Bit(void);

extern void ConvertVDIRes_16Colour(void);
extern void ConvertVDIRes_4Colour(void);
extern void ConvertVDIRes_2Colour_1Bit(void);
extern void ConvertVDIRes_2Colour(void);
