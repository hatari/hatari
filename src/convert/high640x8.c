/* Screen Conversion, High Res to 640x8Bit */


static void ConvertHighRes_640x8Bit(void)
{
  Uint16 *edi, *ebp;
  Uint32 *esi;
  Uint16 eax, ebx;
  int y, x;

/*
  // This is the method that I first used in Hatari...
  // ...but I now rewrote the old WinSTon method, too.
  // Don't know what is faster, so I didn't removed my
  // first method here completely...
  Uint8 *src=pSTScreen;
  Uint8 *osrc=pSTScreenCopy;
  Uint8 *dst=pPCScreenDest;
  int i;
  for(i=0; i<640*400/8; i++, src++, osrc++)
   {
    if( *osrc==*src )
      dst+=8;
     else
     {
      if( *src & 128 ) *dst++ = 1;  else  *dst++ = 0;
      if( *src & 64 )  *dst++ = 1;  else  *dst++ = 0;
      if( *src & 32 )  *dst++ = 1;  else  *dst++ = 0;
      if( *src & 16 )  *dst++ = 1;  else  *dst++ = 0;
      if( *src & 8 )   *dst++ = 1;  else  *dst++ = 0;
      if( *src & 4 )   *dst++ = 1;  else  *dst++ = 0;
      if( *src & 2 )   *dst++ = 1;  else  *dst++ = 0;
      if( *src & 1 )   *dst++ = 1;  else  *dst++ = 0;
     }
   }
  bScreenContentsChanged = TRUE;
  return;
*/

  /* Here's now the rewrite of the original WinSTon method: */

  edi = (Uint16 *)pSTScreen;        /* ST format screen */
  ebp = (Uint16 *)pSTScreenCopy;    /* Previous ST format screen */
  esi = (Uint32 *)pPCScreenDest;    /* PC format screen */

  /* NOTE 'ScrUpdateFlag' is already set (to full update or check, no palettes) */
  
  for (y = STScreenStartHorizLine; y < STScreenEndHorizLine; y++) {

    for (x = 0; x < 40; x++) {

      /* Do 16 pixels at one time */
      ebx = *edi;
      
      if( (ScrUpdateFlag&0xe0000000) || ebx!=*ebp )  /* Does differ? */
       {
        bScreenContentsChanged = TRUE;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        /* Plot in 'right-order' on big endian systems */
        HIGH_BUILD_PIXELS_0 ;               /* Generate pixels [4,5,6,7] */
        PLOT_HIGH_640_8BIT(3) ;
        HIGH_BUILD_PIXELS_1 ;               /* Generate pixels [0,1,2,3] */
        PLOT_HIGH_640_8BIT(2) ;
        HIGH_BUILD_PIXELS_2 ;               /* Generate pixels [12,13,14,15] */
        PLOT_HIGH_640_8BIT(1) ;
        HIGH_BUILD_PIXELS_3 ;               /* Generate pixels [8,9,10,11] */
        PLOT_HIGH_640_8BIT(0) ;
#else
        /* Plot in 'wrong-order', as ebx is 68000 endian */
        HIGH_BUILD_PIXELS_0 ;               /* Generate pixels [4,5,6,7] */
        PLOT_HIGH_640_8BIT(1) ;
        HIGH_BUILD_PIXELS_1 ;               /* Generate pixels [0,1,2,3] */
        PLOT_HIGH_640_8BIT(0) ;
        HIGH_BUILD_PIXELS_2 ;               /* Generate pixels [12,13,14,15] */
        PLOT_HIGH_640_8BIT(3) ;
        HIGH_BUILD_PIXELS_3 ;               /* Generate pixels [8,9,10,11] */
        PLOT_HIGH_640_8BIT(2) ;
#endif
       }

     esi += 4;                              /* Next PC pixels */
     edi += 1;                              /* Next ST pixels */
     ebp += 1;                              /* Next ST copy pixels */
    }

/*??  esi = esi -40*8 +PCScreenBytesPerLine/2;*/   /* Back to start of line + Offset to next line */
  }
}

