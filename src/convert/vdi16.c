/* Screen Conversion, VDI Res to 16Colour */

void ConvertVDIRes_16Colour(void)
{
  Uint32 *edi, *ebp;
  Uint32 *esi;
  Uint32 eax, edx;
  register Uint32 ebx, ecx;

  edx = eax = 0;
  ScrY = 0;                             /* Starting line in ST screen */

  /* Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen,
   * 'esi'-PC screen */
  
  edi = (Uint32 *)pSTScreen;        /* ST format screen 4-plane 16 colours */
  ebp = (Uint32 *)pSTScreenCopy;    /* Previous ST format screen */

  do      /* y-loop */
  {
    esi = (Uint32 *)pPCScreenDest;   /* PC format screen, byte per pixel 256 colours */

    ScrX = VDIWidth >> 4;           /* Amount to draw across - in 16-pixels (8 bytes) */

    do          /* x-loop */
    {
      /* Do 16 pixels at one time */
      ebx = *edi;
      ecx = *(edi+1);

      /* Full update? or just test changes? */
      if((ScrUpdateFlag&0xe0000000) || ebx!=*ebp || ecx!=*(ebp+1))  /* Does differ? */
      {
        bScreenContentsChanged = TRUE;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        /* Plot pixels */
        LOW_BUILD_PIXELS_0 ;      /* Generate 'ecx' as pixels [4,5,6,7] */
        PLOT_LOW_320_8BIT(3) ;
        LOW_BUILD_PIXELS_1 ;      /* Generate 'ecx' as pixels [12,13,14,15] */
        PLOT_LOW_320_8BIT(1) ;
        LOW_BUILD_PIXELS_2 ;      /* Generate 'ecx' as pixels [0,1,2,3] */
        PLOT_LOW_320_8BIT(2) ;
        LOW_BUILD_PIXELS_3 ;      /* Generate 'ecx' as pixels [8,9,10,11] */
        PLOT_LOW_320_8BIT(0) ;
#else
        /* Plot pixels */
        LOW_BUILD_PIXELS_0 ;      /* Generate 'ecx' as pixels [4,5,6,7] */
        PLOT_LOW_320_8BIT(1) ;
        LOW_BUILD_PIXELS_1 ;      /* Generate 'ecx' as pixels [12,13,14,15] */
        PLOT_LOW_320_8BIT(3) ;
        LOW_BUILD_PIXELS_2 ;      /* Generate 'ecx' as pixels [0,1,2,3] */
        PLOT_LOW_320_8BIT(0) ;
        LOW_BUILD_PIXELS_3 ;      /* Generate 'ecx' as pixels [8,9,10,11] */
        PLOT_LOW_320_8BIT(2) ;
#endif
      }

      esi += 4;                         /* Next PC pixels */
      edi += 2;                         /* Next ST pixels */
      ebp += 2;                         /* Next ST copy pixels */
    }
    while( --ScrX );                    /* Loop on X */

    pPCScreenDest = (void *)(((Uint8 *)pPCScreenDest)+PCScreenBytesPerLine);  /* Offset to next line */

    ScrY += 1;
  }
  while( ScrY < VDIHeight );      /* Loop on Y */

}

