/*
  Hatari
*/


extern int ScrUpdateFlag;
extern BOOL bScrDoubleY;

extern void ConvertLowRes_320x16Bit(void);
extern void ConvertLowRes_640x16Bit(void);
extern void Line_ConvertLowRes_640x16Bit(void);
extern void ConvertMediumRes_640x16Bit(void);
extern void Line_ConvertMediumRes_640x16Bit(void);
extern void ConvertLowRes_320x8Bit(void);
extern void ConvertLowRes_640x8Bit(void);
extern void Line_ConvertLowRes_640x8Bit(void);
extern void ConvertMediumRes_640x8Bit(void);
extern void Line_ConvertMediumRes_640x8Bit(void);
extern void ConvertHighRes_640x8Bit(void);
extern void ConvertHighRes_640x1Bit(void);
extern void ConvertSpec512_320x16Bit(void);
extern void ConvertSpec512_640x16Bit(void);

extern void ConvertVDIRes_16Colour(void);
extern void ConvertVDIRes_4Colour(void);
extern void ConvertVDIRes_2Colour_1Bit(void);
extern void ConvertVDIRes_2Colour(void);
