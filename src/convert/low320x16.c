/* Screen Conversion, Low Res to 320x16Bit */

static void ConvertLowRes_320x16Bit(void)
{
 Uint32 *edi, *ebp;
 Uint16 *esi;
 Uint32 eax, edx;
 Uint32 ebx, ecx;
 int y, x;

 Convert_StartFrame();            /* Start frame, track palettes */

 for (y = STScreenStartHorizLine; y < STScreenEndHorizLine; y++) {

   eax = STScreenLineOffset[y] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
   edi = (Uint32 *)((Uint8 *)pSTScreen + eax);        /* ST format screen 4-plane 16 colours */
   ebp = (Uint32 *)((Uint8 *)pSTScreenCopy + eax);    /* Previous ST format screen */
   esi = (Uint16 *)pPCScreenDest;                     /* PC format screen */

   AdjustLinePaletteRemap(y);
 
   x = STScreenWidthBytes>>3; /* Amount to draw across in 16-pixels(8 bytes) */

   do    /* x-loop */
    {
     /* Do 16 pixels at one time */
     ebx=*edi;
     ecx=*(edi+1);

     if( (ScrUpdateFlag&0xe0000000) || ebx!=*ebp || ecx!=*(ebp+1) )     /* Does differ? */
      { /* copy word */

       bScreenContentsChanged=TRUE;
   
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
       /* Plot pixels */
       LOW_BUILD_PIXELS_0 ;      /* Generate 'ecx' as pixels [4,5,6,7] */
       PLOT_LOW_320_16BIT(12) ;
       LOW_BUILD_PIXELS_1 ;      /* Generate 'ecx' as pixels [12,13,14,15] */
       PLOT_LOW_320_16BIT(4) ;
       LOW_BUILD_PIXELS_2 ;      /* Generate 'ecx' as pixels [0,1,2,3] */
       PLOT_LOW_320_16BIT(8) ;
       LOW_BUILD_PIXELS_3 ;      /* Generate 'ecx' as pixels [8,9,10,11] */
       PLOT_LOW_320_16BIT(0) ;
#else
       /* Plot pixels */
       LOW_BUILD_PIXELS_0 ;      /* Generate 'ecx' as pixels [4,5,6,7] */
       PLOT_LOW_320_16BIT(4) ;
       LOW_BUILD_PIXELS_1 ;      /* Generate 'ecx' as pixels [12,13,14,15] */
       PLOT_LOW_320_16BIT(12) ;
       LOW_BUILD_PIXELS_2 ;      /* Generate 'ecx' as pixels [0,1,2,3] */
       PLOT_LOW_320_16BIT(0) ;
       LOW_BUILD_PIXELS_3 ;      /* Generate 'ecx' as pixels [8,9,10,11] */
       PLOT_LOW_320_16BIT(8) ;
#endif
      }

     esi += 16;                             /* Next PC pixels */
     edi += 2;                              /* Next ST pixels */
     ebp += 2;                              /* Next ST copy pixels */
    }
   while( --x );                         /* Loop on X */

   pPCScreenDest = (void *)(((Uint8 *)pPCScreenDest)+PCScreenBytesPerLine);  /* Offset to next line */
  }
}

