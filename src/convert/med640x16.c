/* Screen Conversion, Medium Res to 640x16Bit */

void ConvertMediumRes_640x16Bit(void)
{
 Uint32 *edi, *ebp;
 Uint16 *esi;
 register Uint32 eax, ebx, ecx, edx;

 edx=0;

 Convert_StartFrame();            /* Start frame, track palettes */
 ScrY = STScreenStartHorizLine;   /* Starting line in ST screen */

 do      /* y-loop */
  {
/*
NAKED void ConvertMediumRes_640x16Bit_YLoop(void)
{
*/

   eax = STScreenLineOffset[ScrY] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
   edi = (Uint32 *)((Uint8 *)pSTScreen + eax);        /* ST format screen 4-plane 16 colours */
   ebp = (Uint32 *)((Uint8 *)pSTScreenCopy + eax);    /* Previous ST format screen */
   esi = (Uint16 *)pPCScreenDest;                   /* PC format screen */
/*
    // Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen, 'esi'-PC screen
    mov    eax,[ScrY]
    mov    eax,STScreenLineOffset[eax*4]      // Offset for this line
    add    eax,[STScreenLeftSkipBytes]        // Amount to skip on left hand side
    mov    edi,[pSTScreen]              // ST format screen 4-plane 16 colours
    add    edi,eax
    mov    ebp,[pSTScreenCopy]            // Previous ST format screen
    add    ebp,eax
    mov    esi,[pPCScreenDest]            // PC format screen, byte per pixel 256 colours
*/

   if( AdjustLinePaletteRemap() & 0x00030000 )  /* Change palette table */
     /*???goto Line_ConvertLowRes_640x16Bit*/;
/*
    call  AdjustLinePaletteRemap          // Change palette table, DO NOT corrupt edx,edi,esi or ebp!
    test  eax,0x00030000
    je    Line_ConvertLowRes_640x16Bit      // resolution change(MUST be first)
    jmp    Line_ConvertMediumRes_640x16Bit      // 0 palette same, can do check tests
  }
}
NAKED void Line_ConvertMediumRes_640x16Bit(void)
{
  __asm {
    mov    eax,[STScreenWidthBytes]        // Amount to draw across
    shr    eax,2                  // in 16-pixels(4 bytes)
    mov    [ScrX],eax
*/
   ScrX=STScreenWidthBytes>>2;   /* Amount to draw across in 16-pixels (4 bytes) */


   do    /* x-loop */
    {

/*
x_loop:
#ifdef TEST_SCREEN_UPDATE
    mov    WORD PTR [esi+0],0
    mov    WORD PTR [esi+4],0
    mov    WORD PTR [esi+8],0
    mov    WORD PTR [esi+12],0
    mov    WORD PTR [esi+16],0
    mov    WORD PTR [esi+20],0
    mov    WORD PTR [esi+24],0
    mov    WORD PTR [esi+28],0
#endif  //TEST_SCREEN_UPDATE
*/


     /* Do 16 pixels at one time */
     ebx=*edi;
     /* ScrUpdateFlag seems not to be set correctly?! - Thothy */
     if( 1||(ScrUpdateFlag&0xe0000000) || ebx!=*ebp )     /* Does differ? */
      { /* copy word */

/*
    // Do 16 pixels at one time
    mov    ebx,[edi]
    // Full update? or just test changes?
    test  [ScrUpdateFlag],0xe0000000
    jne    copy_word                // Force
    // Does differ?
    cmp    ebx,[ebp]
    je    next_word                // Pixels are same as last frame, so ignore

copy_word:
    mov    [bScreenContentsChanged],TRUE
*/

       bScreenContentsChanged=TRUE;


#if SDL_BYTEORDER == SDL_BIG_ENDIAN
       /* Plot in 'right-order' on big endian systems */
       if( !bScrDoubleY )                  /* Double on Y? */
        {
         MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
         PLOT_MED_640_16BIT(12) ;
         MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
         PLOT_MED_640_16BIT(4) ;
         MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
         PLOT_MED_640_16BIT(8) ;
         MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
         PLOT_MED_640_16BIT(0) ;
        }
        else
        {
         Uint32 *save_ebp;
         save_ebp = ebp;
         ebp = (Uint32 *)PCScreenBytesPerLine;
         MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
         PLOT_MED_640_16BIT_DOUBLE_Y(12) ;
         MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
         PLOT_MED_640_16BIT_DOUBLE_Y(4) ;
         MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
         PLOT_MED_640_16BIT_DOUBLE_Y(8) ;
         MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
         PLOT_MED_640_16BIT_DOUBLE_Y(0) ;
         ebp = save_ebp;
        }
#else
       /* Plot in 'wrong-order', as ebx is 68000 endian */
       if( !bScrDoubleY )                  /* Double on Y? */
        {
         MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
         PLOT_MED_640_16BIT(4) ;
         MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
         PLOT_MED_640_16BIT(12) ;
         MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
         PLOT_MED_640_16BIT(0) ;
         MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
         PLOT_MED_640_16BIT(8) ;
        }
        else
        {
         Uint32 *save_ebp;
         save_ebp = ebp;
         ebp = (Uint32 *) PCScreenBytesPerLine;
         MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
         PLOT_MED_640_16BIT_DOUBLE_Y(4) ;
         MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
         PLOT_MED_640_16BIT_DOUBLE_Y(12) ;
         MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
         PLOT_MED_640_16BIT_DOUBLE_Y(0) ;
         MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
         PLOT_MED_640_16BIT_DOUBLE_Y(8) ;
         ebp = save_ebp;
        }
#endif
      }

     esi += 16;                             /* Next PC pixels */
     edi += 1;                              /* Next ST pixels */
     ebp += 1;                              /* Next ST copy pixels */
    }
   while( --ScrX );                         /* Loop on X */
/*
next_word:
    add    esi,16*2                // Next PC pixels
    add    edi,4                  // Next ST pixels
    add    ebp,4                  // Next ST copy pixels

    dec    [ScrX]
    jne    x_loop                  // Loop on X
*/

   pPCScreenDest = (void *)(((unsigned char *)pPCScreenDest)+PCScreenBytesPerLine*2);  /* Offset to next line */
   ScrY += 1;
  }
 while( ScrY < STScreenEndHorizLine );      /* Loop on Y */
/*
    mov    eax,[pPCScreenDest]
    add    eax,[PCScreenBytesPerLine]        // Offset to next line
    add    eax,[PCScreenBytesPerLine]
    mov    [pPCScreenDest],eax

    inc    [ScrY]
    mov    eax,[STScreenEndHorizLine]
    cmp    [ScrY],eax
    jne    ConvertMediumRes_640x16Bit_YLoop    // And on Y

    pop    ebx
    pop    esi
    pop    edi
    pop    ebp

    ret
  }
*/

}

