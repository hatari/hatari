/* Screen Conversion, Low Res to 320x16Bit */

void ConvertLowRes_320x16Bit(void)
{
 unsigned long *edi, *ebp;
 unsigned short *esi;
 register unsigned long eax, ebx, ecx, edx;

 edx=0;

 Convert_StartFrame();            /* Start frame, track palettes */
 ScrY = STScreenStartHorizLine;   /* Starting line in ST screen */

 do      /* y-loop */
  {
   eax = STScreenLineOffset[ScrY] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
   edi = (unsigned long *)((char *)pSTScreen + eax);        /* ST format screen 4-plane 16 colours */
   ebp = (unsigned long *)((char *)pSTScreenCopy + eax);    /* Previous ST format screen */
   esi = (unsigned short *)pPCScreenDest;                   /* PC format screen */

   AdjustLinePaletteRemap();
 
/*
    call  AdjustLinePaletteRemap        // Change palette table, DO NOT corrupt edx,edi,esi or ebp!
    jmp    Line_ConvertLowRes_320x16Bit    // 0 palette same, can do check tests
}
*/

/*
NAKED void Line_ConvertLowRes_320x16Bit(void)
{
*/

   ScrX=STScreenWidthBytes>>3;                /* Amount to draw across in 16-pixels(8 bytes) */
/*
  __asm {
    mov    eax,[STScreenWidthBytes]    // Amount to draw across
    shr    eax,3          // in 16-pixels(8 bytes)
    mov    [ScrX],eax
*/

   do    /* x-loop */
    {
     /* Do 16 pixels at one time */
     ebx=*edi;
     ecx=*(edi+1);
     /* ScrUpdateFlag seems not to be set correctly?! - Thothy */
     if( 1||(ScrUpdateFlag&0xe0000000) || ebx!=*ebp || ecx!=*(ebp+1) )     /* Does differ? */
      { /* copy word */
/*
x_loop:

    // Do 16 pixels at one time
    mov    ebx,[edi]
    mov    ecx,4[edi]
    // Full update? or just test changes?
    test  [ScrUpdateFlag],0xe0000000
    jne    copy_word        // Force
    // Does differ?
    cmp    ebx,[ebp]
    jne    copy_word
    cmp    ecx,4[ebp]
    je    next_word        // Pixels are same as last frame, so ignore
*/

       bScreenContentsChanged=TRUE;
   
/*
copy_word:
    mov    [bScreenContentsChanged],TRUE
*/

#if __BYTE_ORDER == 4321
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
   while( --ScrX );                         /* Loop on X */
/*
next_word:
    add    esi,16*2        // Next PC pixels
    add    edi,8          // Next ST pixels
    add    ebp,8          // Next ST copy pixels

    dec    [ScrX]
    jne    x_loop          // Loop on X
*/

   pPCScreenDest = (void *)(((unsigned char *)pPCScreenDest)+PCScreenBytesPerLine);  /* Offset to next line */
   ScrY += 1;
  }
 while( ScrY < STScreenEndHorizLine );      /* Loop on Y */
/*
    mov    eax,[pPCScreenDest]
    add    eax,[PCScreenBytesPerLine]    // Offset to next line
    mov    [pPCScreenDest],eax
    
    inc    [ScrY]
    mov    eax,[STScreenEndHorizLine]
    cmp    [ScrY],eax
    jne    ConvertLowRes_320x16Bit_YLoop    // And on Y

    pop    ebx
    pop    esi
    pop    edi
    pop    ebp

    ret
  }
*/
}

