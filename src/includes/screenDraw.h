/*
  Hatari - screenDraw.h
*/

/*-----------------------------------------------------------------------*/
/* VDI Screens 640x480 */
SCREENDRAW VDIScreenDraw_640x480[] = {
  {  /* Low */
    ConvertVDIRes_16Colour,
    640,480,8,1,
    {
      { 0,640/2, 0,480,  0,0 },
      { 0,640/2, 0,480,  0,0 },
      { 0,640/2, 0,480,  0,0 },
      { 0,640/2, 0,480,  0,0 },
    }
  },
  {  /* Medium */
    ConvertVDIRes_4Colour,
    640,480,8,1,
    {
      { 0,640/4, 0,480,  0,0 },
      { 0,640/4, 0,480,  0,0 },
      { 0,640/4, 0,480,  0,0 },
      { 0,640/4, 0,480,  0,0 },
    }
  },
  {  /* High */
    ConvertVDIRes_2Colour,
    640,480,8,1,
    {
      { 0,640/8, 0,480,  0,0 },
      { 0,640/8, 0,480,  0,0 },
      { 0,640/8, 0,480,  0,0 },
      { 0,640/8, 0,480,  0,0 },
    }
  },
};

/* VDI Screens 800x600 */
SCREENDRAW VDIScreenDraw_800x600[] = {
  {  /* Low */
    ConvertVDIRes_16Colour,
    800,600,8,1,
    {
      { 0,800/2, 0,600,  0,0 },
      { 0,800/2, 0,600,  0,0 },
      { 0,800/2, 0,600,  0,0 },
      { 0,800/2, 0,600,  0,0 },
    }
  },
  {  /* Medium */
    ConvertVDIRes_4Colour,
    800,600,8,1,
    {
      { 0,800/4, 0,600,  0,0 },
      { 0,800/4, 0,600,  0,0 },
      { 0,800/4, 0,600,  0,0 },
      { 0,800/4, 0,600,  0,0 },
    }
  },
  {  /* High */
    ConvertVDIRes_2Colour,
    800,600,8,1,
    {
      { 0,800/8, 0,600,  0,0 },
      { 0,800/8, 0,600,  0,0 },
      { 0,800/8, 0,600,  0,0 },
      { 0,800/8, 0,600,  0,0 },
    }
  },
};

/* VDI Screens 1024x768 */
SCREENDRAW VDIScreenDraw_1024x768[] = {
  {  /* Low */
    ConvertVDIRes_16Colour,
    1024,768,8,1,
    {
      { 0,1024/2, 0,768,  0,0 },
      { 0,1024/2, 0,768,  0,0 },
      { 0,1024/2, 0,768,  0,0 },
      { 0,1024/2, 0,768,  0,0 },
    }
  },
  {  /* Medium */
    ConvertVDIRes_4Colour,
    1024,768,8,1,
    {
      { 0,1024/4, 0,768,  0,0 },
      { 0,1024/4, 0,768,  0,0 },
      { 0,1024/4, 0,768,  0,0 },
      { 0,1024/4, 0,768,  0,0 },
    }
  },
  {  /* High */
    ConvertVDIRes_2Colour,
    1024,768,8,1,
    {
      { 0,1024/8, 0,768,  0,0 },
      { 0,1024/8, 0,768,  0,0 },
      { 0,1024/8, 0,768,  0,0 },
      { 0,1024/8, 0,768,  0,0 },
    }
  },
};

/*-----------------------------------------------------------------------*/

SCREENDRAW ScreenDraw_Low_320x200x256 = {
  ConvertLowRes_320x8Bit,
  320,200,8,1,
  {
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 }
  }
};

SCREENDRAW ScreenDraw_Low_320x200x16Bit = {
  ConvertLowRes_320x16Bit,
  320,200,16,1,
  {
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 }
  }
};

SCREENDRAW ScreenDraw_Low_640x400x256 = {
  ConvertLowRes_640x8Bit,
  640,400,8,2,
  {
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 }
  }
};

SCREENDRAW ScreenDraw_Low_640x400x16Bit = {
  ConvertLowRes_640x16Bit,
  640,400,16,2,
  {
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 }
  }
};

SCREENDRAW ScreenDraw_Medium_640x400x256 = {
  ConvertMediumRes_640x8Bit,
  640,400,8,2,
  {
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 }
  }
};

SCREENDRAW ScreenDraw_Medium_640x400x16Bit = {
  ConvertMediumRes_640x16Bit,
  640,400,16,2,
  {
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+200,  0,0 }
  }
};

SCREENDRAW ScreenDraw_High_640x400x256 = {
  ConvertHighRes_640x8Bit,
  640,400,8,1,
  {
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+400,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+400,  0,0 },  // These are not valid!(cannot have overscan in High Res)
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+400,  0,0 },
    { SCREENBYTES_LEFT,SCREENBYTES_MIDDLE, OVERSCAN_TOP,OVERSCAN_TOP+400,  0,0 },
  }
};



//-----------------------------------------------------------------------
// Modes to select according to chosen option from dialog
// In order DISPLAYMODE_16COL_LOWRES,DISPLAYMODE_16COL_HIGHRES,DISPLAYMODE_16COL_FULL,DISPLAYMODE_HICOL_LOWRES,DISPLAYMODE_HICOL_HIGHRES and DISPLAYMODE_HICOL_FULL

SCREENDRAW_DISPLAYOPTIONS ScreenDisplayOptions[] =
{
  // Low-Colour, Low Res
  {
    &ScreenDraw_Low_320x200x256,
    &ScreenDraw_Medium_640x400x256,
    &ScreenDraw_High_640x400x256,
    &ScreenDraw_Medium_640x400x256,
  },
  // Low-Colour, High Res
  {
    &ScreenDraw_Low_640x400x256,
    &ScreenDraw_Medium_640x400x256,
    &ScreenDraw_High_640x400x256,
    &ScreenDraw_Medium_640x400x256,
  },
  // Low-Colour, Full View
  {
    NULL,
    NULL,
    NULL,
    NULL,
  },

  // Hi-Colour, Low Res
  {
    &ScreenDraw_Low_320x200x16Bit,
    &ScreenDraw_Medium_640x400x16Bit,
    &ScreenDraw_High_640x400x256,
    &ScreenDraw_Medium_640x400x16Bit,
  },
  // Hi-Colour, High Res
  {
    &ScreenDraw_Low_640x400x16Bit,
    &ScreenDraw_Medium_640x400x16Bit,
    &ScreenDraw_High_640x400x256,
    &ScreenDraw_Medium_640x400x16Bit,
  },
  // Hi-Colour, Full View
  {
    NULL,
    NULL,
    NULL,
    NULL,
  }
};
