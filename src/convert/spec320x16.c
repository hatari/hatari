/* Screen Conversion, Spec512 to 320x16Bit */

void ConvertSpec512_320x16Bit(void)
{
 Uint32 *edi, *ebp;
 Uint16 *esi;
 register Uint32 eax, ebx, ecx, edx;

 Spec512_StartFrame();            /* Start frame, track palettes */

 edx = 0;                         /* Clear index for loop */

 ScrY = STScreenStartHorizLine;   /* Starting line in ST screen */

 do      /* y-loop */
  {

   Spec512_StartScanLine();         /* Build up palettes for every 4 pixels, store in 'ScanLinePalettes' */
   edx = 0;                         /* Clear index for loop */

   /* Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen, 'esi'-PC screen */
   eax = STScreenLineOffset[ScrY] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
   edi = (Uint32 *)((Uint8 *)pSTScreen + eax);        /* ST format screen 4-plane 16 colours */
   ebp = (Uint32 *)((Uint8 *)pSTScreenCopy + eax);    /* Previous ST format screen */
   esi = (Uint16 *)pPCScreenDest;                     /* PC format screen */

   ScrX=STScreenWidthBytes>>3;   /* Amount to draw across in 16-pixels (8 bytes) */

   do    /* x-loop */
    {
     ebx = *edi;                /* Do 16 pixels at one time */
     ecx = *(edi+1);

#if SDL_BYTEORDER == SDL_LIL_ENDIAN

     /* Convert planes to byte indices - as works in wrong order store to workspace so can read back in order! */
     LOW_BUILD_PIXELS_0 ;               /* Generate 'ecx' as pixels [4,5,6,7] */
     PixelWorkspace[1] = ecx;
     LOW_BUILD_PIXELS_1 ;               /* Generate 'ecx' as pixels [12,13,14,15] */
     PixelWorkspace[3] = ecx;
     LOW_BUILD_PIXELS_2 ;               /* Generate 'ecx' as pixels [0,1,2,3] */
     PixelWorkspace[0] = ecx;
     LOW_BUILD_PIXELS_3 ;               /* Generate 'ecx' as pixels [8,9,10,11] */
     PixelWorkspace[2] = ecx;

     /* And plot, the Spec512 is offset by 1 pixel and works on 'chunks' of 4 pixels */
     /* So, we plot 1_4_4_3 to give 16 pixels, changing palette between */
     /* (last one is used for first of next 16-pixels) */
     ecx = PixelWorkspace[0];
     PLOT_SPEC512_LEFT_LOW_320_16BIT(0) ;
     Spec512_UpdatePaletteSpan();

     ecx = *(Uint32 *)( ((Uint8 *)PixelWorkspace)+1 );
     PLOT_SPEC512_MID_320_16BIT(1) ;
     Spec512_UpdatePaletteSpan();

     ecx = *(Uint32 *)( ((Uint8 *)PixelWorkspace)+5 );
     PLOT_SPEC512_MID_320_16BIT(5) ;
     Spec512_UpdatePaletteSpan();

     ecx = *(Uint32 *)( ((Uint8 *)PixelWorkspace)+9 );
     PLOT_SPEC512_MID_320_16BIT(9) ;
     Spec512_UpdatePaletteSpan();

     ecx = *(Uint32 *)( ((Uint8 *)PixelWorkspace)+13 );
     PLOT_SPEC512_END_LOW_320_16BIT(13) ;

#else
     /* Well, I didn't get the code above working on big-endian  machines, too,
        But I hope the following lines are good enough to also do the job... - THH */

     LOW_BUILD_PIXELS_0 ;
     PixelWorkspace[3] = ecx;
     LOW_BUILD_PIXELS_1 ;
     PixelWorkspace[1] = ecx;
     LOW_BUILD_PIXELS_2 ;
     PixelWorkspace[2] = ecx;
     LOW_BUILD_PIXELS_3 ;
     PixelWorkspace[0] = ecx;

     ecx = PixelWorkspace[0];
     PLOT_SPEC512_MID_320_16BIT(0) ;
     Spec512_UpdatePaletteSpan();

     ecx = PixelWorkspace[1];
     PLOT_SPEC512_MID_320_16BIT(4) ;
     Spec512_UpdatePaletteSpan();

     ecx = PixelWorkspace[2];
     PLOT_SPEC512_MID_320_16BIT(8) ;
     Spec512_UpdatePaletteSpan();

     ecx = PixelWorkspace[3];
     PLOT_SPEC512_MID_320_16BIT(12) ;
     Spec512_UpdatePaletteSpan();

#endif

     esi += 16;                             /* Next PC pixels */
     edi += 2;                              /* Next ST pixels */
     ebp += 2;                              /* Next ST copy pixels */
    }
   while( --ScrX );                         /* Loop on X */

   Spec512_EndScanLine();

   pPCScreenDest = (void *)(((unsigned char *)pPCScreenDest)+PCScreenBytesPerLine);  /* Offset to next line */
   ScrY += 1;
  }
 while( ScrY < STScreenEndHorizLine );      /* Loop on Y */

 bScreenContentsChanged = TRUE;

}

