/* Screen Conversion, VDI Res to 2Colour (1-Bit and 8-Bit) */

#if 0  /* Currently unused */
void ConvertVDIRes_2Colour_1Bit(void)
{
  /* Copy palette to bitmap (2 colours) */

  if (HBLPalettes[0]==0x777) {
    ScreenBMP.Colours[0].rgbRed = ScreenBMP.Colours[0].rgbGreen = ScreenBMP.Colours[0].rgbBlue = 0xff;
    ScreenBMP.Colours[1].rgbRed = ScreenBMP.Colours[1].rgbGreen = ScreenBMP.Colours[1].rgbBlue = 0x00;
  }
  else {
    ScreenBMP.Colours[0].rgbRed = ScreenBMP.Colours[0].rgbGreen = ScreenBMP.Colours[0].rgbBlue = 0x00;
    ScreenBMP.Colours[1].rgbRed = ScreenBMP.Colours[1].rgbGreen = ScreenBMP.Colours[1].rgbBlue = 0xff;
  }

  /* Simply copy ST screen, as same format! */
  memcpy(pPCScreenDest,pSTScreen,(VDIWidth/8)*VDIHeight);

  bScreenContentsChanged = TRUE;
}
#endif


void ConvertVDIRes_2Colour(void)
{
  Uint16 *edi, *ebp;
  Uint32 *esi;
  register Uint16 eax, ebx;

  ScrY = 0;                             /* Starting line in ST screen */

  edi = (Uint16 *)pSTScreen;            /* ST format screen */
  ebp = (Uint16 *)pSTScreenCopy;        /* Previous ST format screen */

  do      /* y-loop */
  {
    esi = (Uint32 *)pPCScreenDest;      /* PC format screen, byte per pixel 256 colours */

    ScrX = VDIWidth >> 4;               /* Amount to draw across in 16-pixels (4 bytes) */

    do          /* x-loop */
    {
      /* Do 16 pixels at one time */
      ebx = *edi;

      if( (ScrUpdateFlag&0xe0000000) || ebx!=*ebp )  /* Does differ? */
      {
        bScreenContentsChanged = TRUE;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        /* Plot in 'right-order' on big endian systems */
        HIGH_BUILD_PIXELS_0 ;           /* Generate pixels [4,5,6,7] */
        PLOT_HIGH_640_8BIT(3) ;
        HIGH_BUILD_PIXELS_1 ;           /* Generate pixels [0,1,2,3] */
        PLOT_HIGH_640_8BIT(2) ;
        HIGH_BUILD_PIXELS_2 ;           /* Generate pixels [12,13,14,15] */
        PLOT_HIGH_640_8BIT(1) ;
        HIGH_BUILD_PIXELS_3 ;           /* Generate pixels [8,9,10,11] */
        PLOT_HIGH_640_8BIT(0) ;
#else
        /* Plot in 'wrong-order', as ebx is 68000 endian */
        HIGH_BUILD_PIXELS_0 ;           /* Generate pixels [4,5,6,7] */
        PLOT_HIGH_640_8BIT(1) ;
        HIGH_BUILD_PIXELS_1 ;           /* Generate pixels [0,1,2,3] */
        PLOT_HIGH_640_8BIT(0) ;
        HIGH_BUILD_PIXELS_2 ;           /* Generate pixels [12,13,14,15] */
        PLOT_HIGH_640_8BIT(3) ;
        HIGH_BUILD_PIXELS_3 ;           /* Generate pixels [8,9,10,11] */
        PLOT_HIGH_640_8BIT(2) ;
#endif
      }

      esi += 4;                         /* Next PC pixels */
      edi += 1;                         /* Next ST pixels */
      ebp += 1;                         /* Next ST copy pixels */
    }
    while( --ScrX );                    /* Loop on X */

    pPCScreenDest = (void *)(((Uint8 *)pPCScreenDest)+PCScreenBytesPerLine);  /* Offset to next line */

    ScrY += 1;
  }
  while( ScrY < VDIHeight );            /* Loop on Y */

}

